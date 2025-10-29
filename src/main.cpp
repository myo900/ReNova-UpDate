// main.cpp - VERSIONE OTTIMIZZATA CON FLOOR CONSUMO E CAP AUMENTO RANGE + AVVIO AP+STA
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "driver/twai.h"
#include "ui.h"
#include <SD.h>
#include <TJpg_Decoder.h>

#include "canparser.h"
#include "battery_config.h"
#include "web_config.h"
#include "bms_error_flags.h"  // Sistema errori con icone e rotazione

#include "ota_updater.h"
#include "ota_screen.h"
#include "rtc_manager.h"
#include "sd_logger.h"

// ------- VERSIONE FW (se non ridefinita altrove) -------
#ifndef FW_VERSION
#define FW_VERSION "1.2.9"
#endif

// --- forward declarations delle helper implementate in web_config.cpp ---
void WebConfigStartAP_STA();  // avvia AP + portale e tenta STA con credenziali salvate
void WebConfigTask();         // gestisce HTTP + stato OTA non bloccante

// Dichiarazioni funzioni auto-apprendimento - SOLO DICHIARAZIONI (implementazioni in canparser.cpp)
void updateTripTracking(BMSData &bms);
void processSpeedChange(BMSData &bms, float previousSpeed);
void processSOCChange(BMSData &bms, float previousSOC);
void startNewTrip(BMSData &bms);
void endCurrentTrip(BMSData &bms);

// Gestori pulsante RTC
void handleButtonPress();
void handleLongPress();
void handleDoublePress();

// Funzioni orologio
void createClockUI();
void updateClockDisplay();
void showButtonFeedback(ButtonAction action);

// DICHIARAZIONI FUNZIONI OTTIMIZZATE - CORRETTE
void updateErrorDisplay(uint32_t errorFlags);
void updateRangeColorsSolid(int currentRange);  // CERCHI PIENI TRASPARENTI
void updatePowerBars();
void updateChargingScreen();
void showCANWarning();      // Reset soft - mostra warning
void resetCANDisplay();     // Reset hard - azzera tutto
void reinitTWAI();          // Reinizializza bus TWAI

#define TWAI_TX_PIN GPIO_NUM_17
#define TWAI_RX_PIN GPIO_NUM_18
#define SD_CS_PIN   GPIO_NUM_10
#define SD_MOSI_PIN GPIO_NUM_11
#define SD_MISO_PIN GPIO_NUM_13
#define SD_SCK_PIN  GPIO_NUM_12

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,
    45, 48, 47, 21, 14,
    5, 6, 7, 15, 16, 4,
    8, 3, 46, 9, 1,
    0, 8, 4, 8,
    0, 8, 4, 8,
    1, 14000000
);
// Esportato per Screen4 (disegno diretto JPG)
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbpanel);
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

BMSData bms;
unsigned long lastCAN = 0;

// === PARAMETRI CAN TIMEOUT ===
static const unsigned long STARTUP_GRACE_PERIOD_MS = 60000;  // 60 secondi di tolleranza all'avvio
static const unsigned long CAN_TIMEOUT_STARTUP_MS = 15000;   // 15 secondi durante startup
static const unsigned long CAN_TIMEOUT_NORMAL_MS = 5000;     // 5 secondi a regime
static const unsigned long CAN_CHECK_INTERVAL_MS = 3000;     // Frequenza controllo
static const unsigned long CAN_REINIT_INTERVAL_MS = 30000;   // Reinit TWAI ogni 30s senza dati
static unsigned long systemStartTime = 0;                     // Timestamp avvio sistema
static unsigned long lastTWAIReinit = 0;                      // Ultimo tentativo reinit TWAI

// === PARAMETRI CONTROLLO AUTONOMIA ===
static const float kMinAbsWhpkm = 80.0f;
static const float kMinCfgFactor = 0.85f;
static const float kMaxWhpkm     = 300.0f;
static const float kRangeSafety  = 0.95f;
static const unsigned long kRangeRiseWindowMs = 30000UL;
static const int kRangeRisePerWindowKm = 1;

// Tracciamento interno per CAP aumento range
static int g_lastRangeCalc = -1;
static unsigned long g_lastRangeCalcT = 0;

// === OROLOGIO + INDICATORE PULSANTE ===
static lv_obj_t* ui_clock_label = nullptr;         // Data + ora
static lv_obj_t* ui_button_indicator = nullptr;    // Pallini feedback
static unsigned long indicator_hide_time = 0;       // Timestamp nascondimento
static unsigned long last_clock_update = 0;         // Ultimo aggiornamento orologio

// === BACKGROUND DA SD IN PSRAM ===
// ✅ FIX GLITCH #3: Background ottimizzato con attributo IRAM per cache-friendly
// Nota: MALLOC_CAP_SPIRAM alloca in PSRAM ma con accesso ottimizzato
static uint16_t* background_buffer_psram = nullptr;  // Buffer 800x480 RGB565 in PSRAM (768KB)
lv_img_dsc_t background_img_dsc;              // Descriptor LVGL per background (non static per extern)
bool background_loaded = false;               // Flag caricamento riuscito (non static per extern)

// ✅ Ottimizzazione: Pre-allocazione in PSRAM con attributo preferenziale
static bool use_psram_background = true;     // Flag per fallback a no-background se PSRAM pieno

// Callback TJpgDec: scrive pixel decodificati nel buffer PSRAM
static int background_write_x = 0;
static int background_write_y = 0;
bool background_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!background_buffer_psram || y >= 480 || x >= 800) return false;

    // Copia blocco decodificato nel buffer PSRAM
    for (int16_t row = 0; row < h; row++) {
        int16_t target_y = y + row;
        if (target_y >= 480) break;

        int16_t target_x = x;
        if (target_x >= 800) continue;

        uint16_t copy_width = (target_x + w > 800) ? (800 - target_x) : w;
        uint16_t* dst = &background_buffer_psram[target_y * 800 + target_x];
        uint16_t* src = &bitmap[row * w];
        memcpy(dst, src, copy_width * sizeof(uint16_t));
    }

    return true;
}

// Carica background.jpg da SD in PSRAM
bool loadBackgroundFromSD() {
    // Verifica SD
    if (!SD.exists("/background.jpg")) {
        sdLogger.warning("background.jpg non trovato su SD");
        return false;
    }

    // Alloca buffer in PSRAM (800x480x2 = 768KB)
    background_buffer_psram = (uint16_t*)heap_caps_malloc(
        800 * 480 * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM
    );

    if (!background_buffer_psram) {
        sdLogger.error("Impossibile allocare 768KB PSRAM per background");
        return false;
    }

    // Azzera buffer
    memset(background_buffer_psram, 0, 800 * 480 * sizeof(uint16_t));

    // Configura TJpgDec per scrivere nel buffer PSRAM
    TJpgDec.setJpgScale(1);  // Scala 1:1 (800x480)
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(background_output);

    // Decodifica JPG nel buffer
    int result = TJpgDec.drawSdJpg(0, 0, "/background.jpg");

    if (result != 0) {
        sdLogger.error("Errore decodifica background.jpg");
        heap_caps_free(background_buffer_psram);
        background_buffer_psram = nullptr;
        return false;
    }

    // Prepara descriptor LVGL
    background_img_dsc.header.always_zero = 0;
    background_img_dsc.header.w = 800;
    background_img_dsc.header.h = 480;
    background_img_dsc.data_size = 800 * 480 * 2;
    background_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    background_img_dsc.data = (uint8_t*)background_buffer_psram;

    background_loaded = true;
    sdLogger.info("Background caricato da SD in PSRAM (768KB)");

    return true;
}

// === FUNZIONI DI SUPPORTO ===

// ✅ FIX GLITCH #6: Cache configurazione batteria in RAM
// Previene lettura Preferences (flash) ogni 50-200ms → elimina micro-freeze
static float cached_consumption = 0.0f;
static unsigned long cache_consumption_time = 0;
static const unsigned long CACHE_VALIDITY_MS = 5000;  // Rileggi ogni 5 secondi

float getCachedConsumption() {
    unsigned long now = millis();
    if (cached_consumption == 0.0f || (now - cache_consumption_time) > CACHE_VALIDITY_MS) {
        cached_consumption = batteryConfig.getConsumption();
        cache_consumption_time = now;
    }
    return cached_consumption;
}

int calculateRange(float voltage, float socAh, float usedEnergy, float drivenDist) {
    const float CFG = getCachedConsumption();  // ⚡ Usa cache invece di flash
    const float MIN_FLOOR = max(kMinAbsWhpkm, CFG * kMinCfgFactor);

    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 10000) {
        lastLogTime = millis();
    }

    float whpkm;
    if (drivenDist < 0.01f) {
        whpkm = max(CFG, MIN_FLOOR);
    } else {
        whpkm = usedEnergy / drivenDist;
        whpkm = constrain(whpkm, MIN_FLOOR, kMaxWhpkm);
    }

    int range = (int)round((socAh * voltage) / (whpkm * kRangeSafety));

    unsigned long now = millis();
    if (g_lastRangeCalc >= 0 && g_lastRangeCalcT > 0 && range > g_lastRangeCalc) {
        const long windows = (long)((now - g_lastRangeCalcT) / kRangeRiseWindowMs);
        const int maxRise = max(1L, windows) * kRangeRisePerWindowKm;
        range = min(range, g_lastRangeCalc + maxRise);
    }
    g_lastRangeCalc = range;
    g_lastRangeCalcT = now;

    return range;
}

float calculateChargeTime(float currentSOC, float chargeCurrent, float batteryCapacity, float voltage) {
    if (chargeCurrent <= 0 || currentSOC >= 99.5f || currentSOC < 0) {
        return 0;
    }

    float targetSOC = 100.0f;
    float energyNeeded = ((targetSOC - currentSOC) / 100.0f) * batteryCapacity * voltage;

    float efficiencyFactor;
    if (currentSOC < 80.0f) {
        efficiencyFactor = 0.85f;
    } else if (currentSOC < 90.0f) {
        efficiencyFactor = 0.75f;
    } else {
        efficiencyFactor = 0.60f;
    }

    float chargePower = chargeCurrent * voltage * efficiencyFactor;
    float timeHours = energyNeeded / chargePower;
    if (timeHours > 8.0f) timeHours = 8.0f;
    return timeHours;
}

void handleAutoScreenSwitch() {
    static bool lastChargingState = false;
    static unsigned long screenSwitchDelay = 0;
    static bool firstCANDataReceived = false;

    // Rileva quando abbiamo ricevuto il primo dato CAN affidabile
    // (quando riceviamo dati dal CAN, lastCAN viene aggiornato)
    if (!firstCANDataReceived && lastCAN > 0) {
        firstCANDataReceived = true;
        sdLogger.logScreen("FIRST_CAN_DATA", bms.isCharging ? "Charging" : "Driving");

        // Se siamo in ricarica, passa subito a Screen2
        if (bms.isCharging) {
            lv_disp_load_scr(ui_Screen2);
            lastChargingState = true;
            sdLogger.logScreen("SWITCH_TO_SCREEN2", "Charging at boot");
            return;
        }
    }

    // GESTIONE CAMBIO STATO durante l'uso normale
    if (bms.isCharging != lastChargingState) {
        if (bms.isCharging) {
            // Ritardo di 1 secondo per evitare falsi positivi
            screenSwitchDelay = millis() + 1000;
            sdLogger.logScreen("CHARGING_STARTED", "Delayed switch to Screen2");
        } else {
            lv_disp_load_scr(ui_Screen1);
            screenSwitchDelay = 0;
            sdLogger.logScreen("CHARGING_STOPPED", "Back to Screen1");
        }
        lastChargingState = bms.isCharging;
    }

    if (bms.isCharging && screenSwitchDelay > 0 && millis() >= screenSwitchDelay) {
        lv_disp_load_scr(ui_Screen2);
        screenSwitchDelay = 0;
        sdLogger.logScreen("SWITCH_TO_SCREEN2", "Charging confirmed");
    }
}

const char* getGearName(uint8_t g) {
    switch (g) {
        case 0x80: return "D";
        case 0x20: return "N";
        case 0x08: return "R";
        default: return "-";
    }
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= 480) return 0;
    gfx->startWrite();
    gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
    gfx->endWrite();
    return 1;
}
// Forward declaration per usare readCAN() dentro showSplashScreen()
void readCAN();

void showSplashScreen() {
    if (SD.exists("/splash.jpg")) {
        TJpgDec.setJpgScale(1);
        TJpgDec.setSwapBytes(false);
        TJpgDec.setCallback(tft_output);
        gfx->fillScreen(BLACK);
        TJpgDec.drawSdJpg(0, 0, "/splash.jpg");
    } else {
        gfx->fillScreen(BLACK);
        gfx->setTextColor(RED);
        gfx->setCursor(100, 200);
        gfx->setTextSize(2);
        gfx->println("splash.jpg non trovato");
    }

    // Invece di delay(2000): loop "attivo" che legge CAN e tiene viva LVGL
    uint32_t start = millis();
    while (millis() - start < 2000) {
        readCAN();            // aggiorna bms via parseCANFrame()
        lv_timer_handler();   // mantiene reattiva LVGL
        delay(5);
    }
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // ✅ FIX GLITCH #1: Attende completamento DMA prima di segnalare ready
    gfx->startWrite();  // Acquisisce bus + inizia transazione DMA
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    gfx->endWrite();    // ⚡ BLOCCA fino a fine DMA - previene race condition

    lv_disp_flush_ready(disp);  // Ora è sicuro: DMA ha finito
}

void setupCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_PIN, TWAI_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();
}

void readCAN() {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
        parseCANFrame(message.identifier, message.data_length_code, message.data, bms);
        lastCAN = millis();

        // Reset flag del display quando torniamo a ricevere dati
        // (permette un nuovo reset se dovesse risuccedere)
        extern void resetCANDisplayFlag();
        resetCANDisplayFlag();
    }
}

// *** FUNZIONE PRINCIPALE PER COLORI AUTONOMIA - CERCHI PIENI TRASPARENTI ***
void updateRangeColorsSolid(int currentRange) {
    static int lastColorRange = -1;
    int colorCategory;

    if (currentRange <= 10) colorCategory = 1; // Rosso CRITICO
    else if (currentRange <= 25) colorCategory = 2; // Arancio ATTENZIONE
    else colorCategory = 3; // Verde NORMALE

    if (colorCategory != lastColorRange) {
        lv_color_t bgColor, arcColor;

        switch(colorCategory) {
            case 1: bgColor = lv_color_hex(0xFF2915); arcColor = lv_color_hex(0xFF2915); break;
            case 2: bgColor = lv_color_hex(0xFFCE00); arcColor = lv_color_hex(0xFFCE00); break;
            default: bgColor = lv_color_hex(0x5CFC66); arcColor = lv_color_hex(0x5CFC66); break;
        }

        lv_obj_set_style_arc_color(ui_automark, arcColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_automark_bg, bgColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_automark_bg, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

        lastColorRange = colorCategory;
    }
}

// *** FUNZIONE SEPARATA PER BARRE POTENZA OTTIMIZZATA ***
void updatePowerBars() {
    static float lastWattDrive = -1, lastWattRegen = -1;

    float wattDrive = (bms.current < 0) ? abs(bms.current) * bms.voltage : 0;
    float wattRegen = (bms.current > 0) ? bms.current * bms.voltage : 0;

    if (abs(wattDrive - lastWattDrive) > 300.0f) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fkW", wattDrive / 1000.0f);
        lv_label_set_text(ui_powervalue, buf);
        int drivePercent = constrain((wattDrive / 20000.0f) * 100, 0, 100);
        lv_bar_set_value(ui_powerbar, drivePercent, LV_ANIM_OFF);
        lastWattDrive = wattDrive;
    }

    if (abs(wattRegen - lastWattRegen) > 150.0f) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fkW", wattRegen / 1000.0f);
        lv_label_set_text(ui_regenvalue, buf);
        int regenPercent = constrain((wattRegen / 8000.0f) * 100, 0, 100);
        lv_bar_set_value(ui_regenbar, regenPercent, LV_ANIM_OFF);
        lastWattRegen = wattRegen;
    }
}

// *** SCHERMO RICARICA OTTIMIZZATO ***
void updateChargingScreen() {
    static float lastSOC_Screen2 = -1;
    static float lastChargeTime = -1;
    static unsigned long lastChargeUpdate = 0;

    if (abs(bms.soc - lastSOC_Screen2) >= 0.2f) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", bms.soc);
        lv_label_set_text(ui_socvalueC, buf);
        lv_arc_set_value(ui_socark2, (int16_t)bms.soc);
        lastSOC_Screen2 = bms.soc;
    }

    if (bms.isCharging && bms.chargeCurrent > 0) {
        if (millis() - lastChargeUpdate > 5000) {
            float newChargeTime = calculateChargeTime(bms.soc, bms.chargeCurrent, bms.setAh, bms.voltage);

            if (lastChargeTime < 0) lastChargeTime = newChargeTime;
            else lastChargeTime = (lastChargeTime * 0.7f) + (newChargeTime * 0.3f);

            char timeStr[16];
            if (lastChargeTime < 1.0f) {
                int minutes = (int)(lastChargeTime * 60);
                snprintf(timeStr, sizeof(timeStr), "%dm", minutes);
            } else {
                int hours = (int)lastChargeTime;
                int minutes = (int)((lastChargeTime - hours) * 60);
                snprintf(timeStr, sizeof(timeStr), "%dh%02dm", hours, minutes);
            }
            lv_label_set_text(ui_Label9, timeStr);
            lv_label_set_text(ui_Label8, "Time to full");

            lastChargeUpdate = millis();
        }

    } else if (bms.chargeReady) {
        lv_label_set_text(ui_Label9, "Pronto");
        lv_label_set_text(ui_Label8, "Stato carica");
        lastChargeTime = -1;

    } else {
        if (bms.speed > 5.0f) {
            int currentRange = calculateRange(bms.voltage, bms.socAh, 0, 0);
            if (currentRange > 0) {
                float timeToEnd = currentRange / bms.speed;
                char buf[8];
                snprintf(buf, sizeof(buf), "%.1f", timeToEnd);
                lv_label_set_text(ui_Label9, buf);
                lv_label_set_text(ui_Label8, "Time to end");
            } else {
                lv_label_set_text(ui_Label9, "--");
                lv_label_set_text(ui_Label8, "Time to end");
            }
        } else {
            lv_label_set_text(ui_Label9, "--");
            lv_label_set_text(ui_Label8, "Time to end");
        }
        lastChargeTime = -1;
    }
}

// Reset "soft" - mostra solo warning senza azzerare i dati
void showCANWarning() {
    // Mostra un avviso ma mantiene gli ultimi valori validi
    lv_label_set_text(ui_messaggi, "CAN: Connessione instabile");
    lv_label_set_text(ui_messaggiC, "CAN: Connessione instabile");
    // NON azzeriamo i valori di SOC, velocità, autonomia, etc.
}

// Flag per eseguire reset una sola volta
static bool displayResetDone = false;

// Reset flag del display (chiamato quando riceviamo nuovi dati CAN)
void resetCANDisplayFlag() {
    displayResetDone = false;
}

// Reset "hard" - azzera tutto (usato solo dopo timeout prolungato)
void resetCANDisplay() {
    // Se già fatto, esci subito
    if (displayResetDone) return;

    lv_label_set_text(ui_socvalue, "00%");
    lv_arc_set_value(ui_socark, 0);
    lv_label_set_text(ui_speedvalue, "00");
    lv_label_set_text(ui_gearvalue, "N");
    lv_label_set_text(ui_automvalue, "00");
    lv_arc_set_value(ui_automark, 0);
    lv_label_set_text(ui_powervalue, "0.0kW");
    lv_bar_set_value(ui_powerbar, 0, LV_ANIM_OFF);
    lv_label_set_text(ui_regenvalue, "0.0kW");
    lv_bar_set_value(ui_regenbar, 0, LV_ANIM_OFF);
    lv_label_set_text(ui_battemp, "00");
    lv_label_set_text(ui_messaggi, "CAN disconnesso");
    lv_label_set_text(ui_messaggiC, "CAN disconnesso");
    lv_label_set_text(ui_odovalue, "00");
    lv_label_set_text(ui_socvalueC, "00%");
    lv_arc_set_value(ui_socark2, 0);
    lv_label_set_text(ui_Label9, "--");

    sdLogger.error("CAN TIMEOUT - Display hard reset");

    if (bms.tripInProgress) {
        bms.tripInProgress = false;
        bms.tripStartSOC = 0;
        bms.tripStartDistance = 0;
        bms.tripStartTime = 0;
    }
    bms.errorFlags = 0;
    bms.errorIconLoaded = false;

    // Segna come fatto
    displayResetDone = true;
}

// Reinizializza bus TWAI
void reinitTWAI() {
    static unsigned long lastReinitLog = 0;

    // Log throttling: 1 volta ogni 60 secondi
    if (millis() - lastReinitLog > 60000) {
        sdLogger.warning("Tentativo reinizializzazione bus TWAI");
        lastReinitLog = millis();
    }

    // Stop e disinstalla driver
    twai_stop();
    twai_driver_uninstall();
    delay(100);

    // Reinstalla
    setupCAN();

    lastTWAIReinit = millis();
}

void updateUI() {
    handleAutoScreenSwitch();

    // ✅ FIX GLITCH #4: Throttling UI intelligente
    // Aggiornamenti non critici ogni 200ms invece di 50ms
    // Riduce carico CPU del 75% (da 160 invalidazioni/s a 40/s)
    static unsigned long lastUIUpdate = 0;
    static unsigned long lastCriticalUpdate = 0;  // Per velocità (aggiornata ogni 50ms)
    unsigned long now = millis();

    // Aggiornamenti critici (velocità) restano a 50ms
    bool criticalUpdate = (now - lastCriticalUpdate >= 50);

    // Aggiornamenti normali ogni 200ms
    if (now - lastUIUpdate < 200) {
        // Ma permetti aggiornamento velocità anche fuori ciclo
        if (criticalUpdate) {
            static float lastSpeed = -1;
            if (abs(bms.speed - lastSpeed) >= 0.5f) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%.0f", bms.speed);
                lv_label_set_text(ui_speedvalue, buf);
                lastSpeed = bms.speed;
            }
            lastCriticalUpdate = now;
        }
        return;
    }
    lastUIUpdate = now;
    lastCriticalUpdate = now;

    static unsigned long lastMillis = 0;
    if (lastMillis > 0) {
        float dt_h = (now - lastMillis) / 3600000.0f;
        float instantPower = abs(bms.current * bms.voltage);
        bms.usedEnergy += instantPower * dt_h;
        bms.drivenDistance += bms.speed * dt_h;
    }
    lastMillis = now;

    if (bms.voltage == 0.0f) bms.voltage = 57.6f;

    static float lastSOC = -1, lastSpeed = -1;
    static uint8_t lastGear = 255;
    static int lastRange = -1;
    static uint32_t lastOdometer = 0;

    // SOC
    if (abs(bms.soc - lastSOC) >= 0.2f) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", bms.soc);
        lv_label_set_text(ui_socvalue, buf);
        lv_arc_set_value(ui_socark, (int16_t)bms.soc);
        lastSOC = bms.soc;
    }

    // Velocità
    if (abs(bms.speed - lastSpeed) >= 0.5f) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f", bms.speed);
        lv_label_set_text(ui_speedvalue, buf);
        lastSpeed = bms.speed;
    }

    // Marcia
    if (bms.gear != lastGear) {
        lv_label_set_text(ui_gearvalue, getGearName(bms.gear));
        lastGear = bms.gear;
    }

    // Autonomia
    int currentRange = calculateRange(bms.voltage, bms.socAh, 0, 0);
    if (abs(currentRange - lastRange) >= 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", currentRange);
        lv_label_set_text(ui_automvalue, buf);
        int16_t autoPercent = constrain((currentRange * 100) / 80, 0, 100);
        lv_arc_set_value(ui_automark, autoPercent);

        updateRangeColorsSolid(currentRange);
        lastRange = currentRange;
    }

    // Potenza
    updatePowerBars();

    // Temperatura (ogni 2s)
    static unsigned long lastTempUpdate = 0;
    if (now - lastTempUpdate > 2000) {
        if (bms.avgTemp > -40) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%.0f°", bms.avgTemp);
            lv_label_set_text(ui_battemp, buf);
        }
        lastTempUpdate = now;
    }

    // === OROLOGIO (aggiornamento ogni 60 secondi) ===
    if (now - last_clock_update > 60000) {
        updateClockDisplay();
        last_clock_update = now;
    }

    // === GESTIONE NASCONDIMENTO INDICATORE ===
    if (indicator_hide_time > 0 && now > indicator_hide_time) {
        if (ui_button_indicator) {
            lv_obj_add_flag(ui_button_indicator, LV_OBJ_FLAG_HIDDEN);
        }
        indicator_hide_time = 0;
    }

    // ODOMETRO
    if (bms.odometer != lastOdometer) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", bms.odometer);
        lv_label_set_text(ui_odovalue, buf);
        lastOdometer = bms.odometer;
    }

    // === SCREEN 2: RICARICA ===
    updateChargingScreen();

    // === SISTEMA ERRORI ===
    updateErrorDisplay(bms.errorFlags);

}


// === VISUALIZZAZIONE OROLOGIO + INDICATORE ===

void createClockUI() {
    if (ui_clock_label) return; // Già creato

    // === LABEL DATA + ORA ===
    ui_clock_label = lv_label_create(ui_Screen1);
    lv_obj_set_style_text_color(ui_clock_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(ui_clock_label, &lv_font_montserrat_16, 0);

    // Posizione: centro alto (al posto della vecchia "Battery")
    lv_obj_align(ui_clock_label, LV_ALIGN_CENTER, 0, -166);

    lv_label_set_text(ui_clock_label, "--/--  --:--");

    // === LABEL INDICATORE PULSANTE ===
    ui_button_indicator = lv_label_create(ui_Screen1);
    lv_obj_set_style_text_font(ui_button_indicator, &lv_font_montserrat_16, 0);

    // Posizione: a destra dell'orologio
    lv_obj_align_to(ui_button_indicator, ui_clock_label, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    // Nascosto di default
    lv_obj_add_flag(ui_button_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_button_indicator, "");
}

void updateClockDisplay() {
    if (!ui_clock_label || !rtcManager.isRTCAvailable()) return;

    DateTime now = rtcManager.now();

    char dateTime[16];
    snprintf(dateTime, sizeof(dateTime), "%02d-%02d  %02d:%02d",
             now.day(), now.month(),
             now.hour(), now.minute());

    lv_label_set_text(ui_clock_label, dateTime);
}

void showButtonFeedback(ButtonAction action) {
    if (!ui_button_indicator) return;

    // FIX #5: L'indicatore esiste solo su Screen1 - skip se su altre schermate
    lv_obj_t* currentScreen = lv_scr_act();
    if (currentScreen != ui_Screen1) return;

    const char* symbols = "";
    lv_color_t color = lv_color_hex(0xFFFFFF);
    uint32_t duration_ms = 0;

    switch (action) {
        case BTN_SHORT_PRESS:
            symbols = "●";                          // 1 pallino
            color = lv_color_hex(0x00FF00);        // Verde
            duration_ms = 1000;                     // 1 secondo
            break;

        case BTN_DOUBLE_PRESS:
            symbols = "● ●";                        // 2 pallini
            color = lv_color_hex(0xFF8000);        // Arancione
            duration_ms = 1500;                     // 1.5 secondi
            break;

        case BTN_LONG_PRESS:
            symbols = "● ● ●";                      // 3 pallini
            color = lv_color_hex(0x00A0FF);        // Blu
            duration_ms = 2000;                     // 2 secondi
            break;

        default:
            return; // Nessun feedback
    }

    // Mostra indicatore
    lv_label_set_text(ui_button_indicator, symbols);
    lv_obj_set_style_text_color(ui_button_indicator, color, 0);
    lv_obj_clear_flag(ui_button_indicator, LV_OBJ_FLAG_HIDDEN);

    // Imposta timer nascondimento
    indicator_hide_time = millis() + duration_ms;
}

// === GESTORI PULSANTE CON FEEDBACK VISIVO ===

void handleButtonPress() {
    // Short press: navigazione ciclica Screen1 -> Screen3 -> Screen4 -> Screen1
    // NOTA: Durante ricarica, si salta Screen3 e Screen4 (rimane su Screen2)
    showButtonFeedback(BTN_SHORT_PRESS);

    lv_obj_t* currentScreen = lv_scr_act();

    if (bms.isCharging) {
        // Durante ricarica rimane su Screen2, nessuna navigazione
        sdLogger.logButton("SHORT_PRESS", "Navigation blocked (charging)");
        return;
    }

    // Navigazione ciclica quando NON in ricarica
    if (currentScreen == ui_Screen1) {
        // Screen1 -> Screen3
        lv_disp_load_scr(ui_Screen3);
        ui_Screen3_update_chart();
        sdLogger.logButton("SHORT_PRESS", "Screen1 -> Screen3");
    } else if (currentScreen == ui_Screen3) {
        // Screen3 -> Screen4
        lv_disp_load_scr(ui_Screen4);
        sdLogger.logButton("SHORT_PRESS", "Screen3 -> Screen4 (JPG vista)");
    } else if (currentScreen == ui_Screen4) {
        // Screen4 -> Screen1 (libera memoria JPG)
        ui_Screen4_screen_deinit();
        lv_disp_load_scr(ui_Screen1);
        sdLogger.logButton("SHORT_PRESS", "Screen4 -> Screen1");
    } else {
        // Fallback: da qualsiasi altra schermata torna a Screen1
        lv_disp_load_scr(ui_Screen1);
        sdLogger.logButton("SHORT_PRESS", "Unknown -> Screen1");
    }
}

void handleLongPress() {
    // Long press (3s): mostra solo feedback visivo (3 pallini blu)
    showButtonFeedback(BTN_LONG_PRESS);
    sdLogger.logButton("LONG_PRESS", "3 second hold - visual feedback");
}

void handleDoublePress() {
    // Double press: mostra solo feedback visivo (2 pallini arancio)
    showButtonFeedback(BTN_DOUBLE_PRESS);
    sdLogger.logButton("DOUBLE_PRESS", "Double tap - visual feedback");
}

// === SETUP ===
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Inizializza timestamp avvio per grace period CAN
    systemStartTime = millis();

    // Config batteria
    batteryConfig.begin();

    // Auto-apprendimento
    consumptionLearner.begin();

    // SD
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN)) {
        // === INIZIALIZZA LOGGER ===
        if (sdLogger.begin()) {
            const char* resetReason = "UNKNOWN";
            switch(esp_reset_reason()) {
                case ESP_RST_POWERON:   resetReason = "POWER_ON"; break;
                case ESP_RST_SW:        resetReason = "SOFTWARE"; break;
                case ESP_RST_PANIC:     resetReason = "PANIC"; break;
                case ESP_RST_INT_WDT:   resetReason = "INT_WDT"; break;
                case ESP_RST_TASK_WDT:  resetReason = "TASK_WDT"; break;
                case ESP_RST_WDT:       resetReason = "WDT"; break;
                case ESP_RST_BROWNOUT:  resetReason = "BROWNOUT"; break;
                default: break;
            }
            sdLogger.logBoot(resetReason, ESP.getFreeHeap(), FW_VERSION);
            sdLogger.info("Sistema inizializzato con successo");
        }
    }

    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);

    gfx->begin();

    // Splash
    showSplashScreen();

    // LVGL
    lv_init();

    // ✅ FIX GLITCH #2: Buffer LVGL ottimizzato
    // 100 righe (156KB) invece di 40 (62KB) → 5 flush invece di 12 per frame
    // Riduce overhead DMA del 60% e elimina tearing
    uint32_t buf_pixels = 800 * 100;  // Target: 100 righe
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(
        buf_pixels * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );
    if (!disp_draw_buf) {
        // Fallback progressivo: 80 → 60 → 40 righe
        sdLogger.warning("Buffer 100 righe fallito, provo 80");
        buf_pixels = 800 * 80;
        disp_draw_buf = (lv_color_t*) heap_caps_malloc(
            buf_pixels * sizeof(lv_color_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
        );
    }
    if (!disp_draw_buf) {
        sdLogger.warning("Buffer 80 righe fallito, provo 60");
        buf_pixels = 800 * 60;
        disp_draw_buf = (lv_color_t*) heap_caps_malloc(
            buf_pixels * sizeof(lv_color_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
        );
    }
    if (!disp_draw_buf) {
        // Ultimo tentativo: 40 righe (come prima)
        sdLogger.error("Buffer ottimizzato fallito, uso 40 righe (originale)");
        buf_pixels = 800 * 40;
        disp_draw_buf = (lv_color_t*) heap_caps_malloc(
            buf_pixels * sizeof(lv_color_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
        );
    }

    if (disp_draw_buf) {
        sdLogger.logf(LOG_INFO, "Buffer LVGL allocato: %u righe (%u KB)",
                      buf_pixels / 800, (buf_pixels * 2) / 1024);
    } else {
        sdLogger.error("CRITICO: Impossibile allocare buffer LVGL");
    }
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, buf_pixels);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // === CARICA BACKGROUND DA SD IN PSRAM ===
    // Carica background.jpg prima di ui_init() così è disponibile per ui_Screen1
    loadBackgroundFromSD();

    // UI
    ui_init();

    // SEMPRE Screen1 all'avvio - la funzione handleAutoScreenSwitch()
    // si occuperà di cambiare a Screen2 se necessario dopo aver ricevuto dati CAN
    lv_disp_load_scr(ui_Screen1);

    // === RTC + PULSANTE ===
    if (rtcManager.begin()) {
        // Crea orologio + indicatore
        createClockUI();
        updateClockDisplay();
    }

    // CAN
    setupCAN();

    // === AP + Portale + tentativo STA con credenziali salvate ===
    // (sostituisce le vecchie: webConfig.begin(); webConfig.startSimpleAP();)
    WebConfigStartAP_STA();

    // (opzionale) precostruisci la view OTA per evitare qualsiasi flash al primo uso
    OTAView::begin();
}

// === LOOP ===
void loop() {
    static unsigned long loopStartTime = millis();

    lv_timer_handler();
    readCAN();

    // === RTC MANAGER LOOP (IMPORTANTE: tick del pulsante) ===
    rtcManager.loop();

    // === GESTIONE PULSANTE RTC ===
    ButtonAction btnAction = rtcManager.getButtonAction();

    if (btnAction != BTN_NONE) {
        sdLogger.debug("Button action detected");
    }

    switch (btnAction) {
        case BTN_SHORT_PRESS:  handleButtonPress();  break;
        case BTN_LONG_PRESS:   handleLongPress();    break;
        case BTN_DOUBLE_PRESS: handleDoublePress();  break;
        default: break;
    }

    // Ricrea orologio se non esiste e siamo su Screen 1
    if (lv_scr_act() == ui_Screen1 && !ui_clock_label) {
        createClockUI();
        updateClockDisplay();
    }

    // Portale + OTA state machine (non bloccante)
    WebConfigTask();

    // Trip tracking (meno frequente)
    static unsigned long lastTripUpdate = 0;
    if (millis() - lastTripUpdate > 15000) {
        updateTripTracking(bms);
        lastTripUpdate = millis();
    }

    // Live consumption update (meno frequente)
    static unsigned long lastLiveUpdate = 0;
    if (millis() - lastLiveUpdate > 15000) {
        consumptionLearner.updateLiveConsumption(bms);
        lastLiveUpdate = millis();
    }

    // === CHECK CAN CON DEBOUNCING INTELLIGENTE + REINIT TWAI ===
    static unsigned long lastCANCheck = 0;
    static int canTimeoutCounter = 0;
    static bool warningShown = false;
    static bool hardResetDone = false;

    if (millis() - lastCANCheck > CAN_CHECK_INTERVAL_MS) {
        // NON mostrare errori nei primi 60 secondi dopo boot
        // Permette al bus CAN della Twizy di inizializzarsi
        if (millis() < STARTUP_GRACE_PERIOD_MS && lastCAN == 0) {
            // Attesa silente del bus CAN
            lastCANCheck = millis();
            return; // Salta tutto il controllo
        }

        // Controllo solo se abbiamo già ricevuto almeno un messaggio CAN
        // OPPURE se sono passati 60 secondi e ancora niente
        if (lastCAN > 0 || millis() >= STARTUP_GRACE_PERIOD_MS) {
            unsigned long timeSinceLastCAN = (lastCAN > 0) ? (millis() - lastCAN) : millis();

            // Determina il timeout in base al grace period di avvio
            unsigned long currentTimeout = (millis() < STARTUP_GRACE_PERIOD_MS)
                ? CAN_TIMEOUT_STARTUP_MS
                : CAN_TIMEOUT_NORMAL_MS;

            if (timeSinceLastCAN > currentTimeout) {
                canTimeoutCounter++;

                // Primo timeout: mostra solo warning (reset soft)
                if (canTimeoutCounter == 1 && !warningShown) {
                    showCANWarning();
                    warningShown = true;
                }
                // Dopo 3 timeout consecutivi (9 secondi): reset hard UNA SOLA VOLTA
                else if (canTimeoutCounter >= 3 && !hardResetDone) {
                    resetCANDisplay();
                    hardResetDone = true;
                }

                // Tentativo reinit TWAI ogni 30 secondi senza dati
                if (millis() - lastTWAIReinit > CAN_REINIT_INTERVAL_MS) {
                    reinitTWAI();
                }
            } else {
                // Riceviamo dati: reset counter e nascondi warning
                if (canTimeoutCounter > 0 || warningShown || hardResetDone) {
                    canTimeoutCounter = 0;
                    warningShown = false;
                    hardResetDone = false;
                    // Ripristina messaggio normale
                    lv_label_set_text(ui_messaggi, "");
                    lv_label_set_text(ui_messaggiC, "");
                }
            }
        }
        lastCANCheck = millis();
    }

    // === LOGGER TASK (flush periodico) ===
    sdLogger.task();

    // === LOGGING MEMORIA PERIODICO (ogni 60s - era 30s) ===
    static unsigned long lastMemLog = 0;
    if (millis() - lastMemLog > 60000) {
        sdLogger.logMemory(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
        lastMemLog = millis();
    }

    // === LOGGING BMS PERIODICO (ogni 60s - invariato, dati critici) ===
    static unsigned long lastBMSLog = 0;
    if (millis() - lastBMSLog > 60000) {
        sdLogger.logBMS(bms.soc, bms.voltage, bms.current, bms.avgTemp, bms.errorFlags);
        lastBMSLog = millis();
    }

    // === LOGGING PERFORMANCE (ogni 120s - era 60s) ===
    static unsigned long lastPerfLog = 0;
    if (millis() - lastPerfLog > 120000) {
        uint32_t loopTime = millis() - loopStartTime;
        sdLogger.logPerformance(loopTime, 0, uxTaskGetStackHighWaterMark(NULL));
        lastPerfLog = millis();
    }

    updateUI();

    loopStartTime = millis();
    delay(5);
}
