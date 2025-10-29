// canparser.cpp - PARTE 1/3: INCLUDE E FUNZIONI ERRORI
#include "canparser.h"
#include <math.h>
#include "battery_config.h"
#include "bms_error_flags.h"
#include <TJpg_Decoder.h>
#include <SD.h>
#include <lvgl.h>
#include "ui.h"
#include "rtc_manager.h"  // Per timestamp reali nei viaggi CSV
#include "sd_logger.h"     // Sistema logging

// === Config velocità da RPM (019F) ===
// Attiva velocità da RPM (019F) come sorgente unica per il display.
#ifndef USE_RPM_SPEED
#define USE_RPM_SPEED 1
#endif
// Filtro semplice (EMA): 0..1 (più alto = più reattivo, meno filtro)
#ifndef SPEED_EMA_ALPHA
#define SPEED_EMA_ALPHA 0.25f
#endif

// === ODOMETER SYSTEM V4 - RPM BASED WITH ENCODED SD ========================
// Nuovo sistema: calcolo km da giri motore (frame 0x019F)
// Rapporto trasmissione: 9.23:1
// Circonferenza ruota 145/80 R13: 1.766m
// Formula: km = giri_motore × 0.0001913

static uint32_t total_motor_revs = 0;    // Giri motore totali accumulati
static uint32_t current_odometer_km = 0; // Km totali visualizzati
static uint32_t last_saved_odo_km = 0;   // Ultimo km salvato su SD
static unsigned long last_odo_save = 0;  // Timestamp ultimo salvataggio
static bool odo_loaded = false;          // Flag inizializzazione

static const char* ODO_FILE = "/odo.dat"; // File binario codificato
static const float KM_PER_REV = 0.0001913f; // (1.766 / 9.23) / 1000
static const uint32_t XOR_KEY = 0xA5C3E7B9; // Chiave XOR per codifica

// === FUNZIONI CODIFICA/DECODIFICA ===

// Calcola CRC16 per verifica integrità
static uint16_t odo_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}

// Codifica uint32_t con XOR
static uint32_t odo_encode(uint32_t value) {
    return value ^ XOR_KEY;
}

// Decodifica uint32_t con XOR
static uint32_t odo_decode(uint32_t value) {
    return value ^ XOR_KEY;
}

// Carica odometro da SD (file binario codificato)
static void odo_load_from_sd() {
    if (!SD.exists(ODO_FILE)) {
        current_odometer_km = 0;
        total_motor_revs = 0;
        odo_loaded = true;
        return;
    }

    File file = SD.open(ODO_FILE, FILE_READ);
    if (!file || file.size() != 10) { // 4+4+2 bytes
        if (file) file.close();
        current_odometer_km = 0;
        total_motor_revs = 0;
        odo_loaded = true;
        return;
    }

    // Leggi dati codificati
    uint32_t encoded_km, encoded_revs;
    uint16_t stored_crc;

    file.read((uint8_t*)&encoded_km, 4);
    file.read((uint8_t*)&encoded_revs, 4);
    file.read((uint8_t*)&stored_crc, 2);
    file.close();

    // Decodifica
    current_odometer_km = odo_decode(encoded_km);
    total_motor_revs = odo_decode(encoded_revs);

    // Verifica CRC
    uint8_t buffer[8];
    memcpy(buffer, &encoded_km, 4);
    memcpy(buffer + 4, &encoded_revs, 4);
    uint16_t calc_crc = odo_crc16(buffer, 8);

    if (calc_crc != stored_crc) {
        current_odometer_km = 0;
        total_motor_revs = 0;
    }

    odo_loaded = true;
    last_saved_odo_km = current_odometer_km;
}

// Salva odometro su SD (file binario codificato, throttled)
static void odo_save_to_sd(bool force = false) {
    unsigned long now = millis();

    // Salva SOLO se:
    // - Force (chiamata esplicita)
    // - Cambio >= 1 km
    // - Passati >= 60 secondi dall'ultimo salvataggio
    if (!force) {
        if (current_odometer_km == last_saved_odo_km) return;
        if (abs((int)current_odometer_km - (int)last_saved_odo_km) < 1 &&
            (now - last_odo_save) < 60000) {
            return;
        }
    }

    // Codifica dati
    uint32_t encoded_km = odo_encode(current_odometer_km);
    uint32_t encoded_revs = odo_encode(total_motor_revs);

    // Calcola CRC
    uint8_t buffer[8];
    memcpy(buffer, &encoded_km, 4);
    memcpy(buffer + 4, &encoded_revs, 4);
    uint16_t crc = odo_crc16(buffer, 8);

    // Scrivi file
    File file = SD.open(ODO_FILE, FILE_WRITE);
    if (!file) {
        return;
    }

    file.seek(0);
    file.write((uint8_t*)&encoded_km, 4);
    file.write((uint8_t*)&encoded_revs, 4);
    file.write((uint8_t*)&crc, 2);
    file.close();

    last_odo_save = now;
    last_saved_odo_km = current_odometer_km;
}

// Aggiorna odometro da giri motore
static void odo_update_from_rpm(int rpm) {
    static unsigned long last_update = 0;
    unsigned long now = millis();

    if (now - last_update < 100) return; // Aggiorna ogni 100ms

    unsigned long delta_ms = now - last_update;
    last_update = now;

    if (rpm <= 0) return; // Solo se motore gira

    // Calcola giri motore in questo intervallo
    float revs_per_min = (float)abs(rpm);
    float revs_per_ms = revs_per_min / 60000.0f;
    uint32_t delta_revs = (uint32_t)(revs_per_ms * delta_ms);

    if (delta_revs > 0) {
        total_motor_revs += delta_revs;

        // Calcola km totali
        uint32_t new_km = (uint32_t)(total_motor_revs * KM_PER_REV);

        if (new_km != current_odometer_km) {
            current_odometer_km = new_km;
            odo_save_to_sd(); // Salvataggio throttled
        }
    }
}

// Imposta km veicolo (chiamato da web - protetto password)
bool odo_set_vehicle_km(uint32_t new_km) {
    if (new_km > 999999) {
        return false;
    }

    // Imposta nuovi km mantenendo i giri coerenti
    current_odometer_km = new_km;
    total_motor_revs = (uint32_t)(new_km / KM_PER_REV);

    // Salva immediatamente
    odo_save_to_sd(true);

    return true;
}

// Ottieni km attuali (per web interface)
uint32_t odo_get_current_km() {
    return current_odometer_km;
}

// === ODOMETER SYSTEM V4 (END) ==========================================


// Istanza globale del learner
ConsumptionLearner consumptionLearner;

// === VARIABILI GLOBALI PER SISTEMA ERRORI ===
// ICONE DISABILITATE - Sistema usa panel colorati invece di icone
// static lv_obj_t *current_error_icon_1 = nullptr;
// static lv_obj_t *current_error_icon_2 = nullptr;
// static lv_img_dsc_t error_icon_dsc;
// static uint8_t *icon_buffer = nullptr;
// static size_t icon_buffer_size = 0;

// === CALLBACK PER DECODIFICA ICONE === (DISABILITATO)
/*
bool icon_output_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    // Non disegniamo direttamente, salviamo in buffer
    if (!icon_buffer) {
        icon_buffer_size = w * h * 2; // 16-bit per pixel
        icon_buffer = (uint8_t*)malloc(icon_buffer_size);
    }

    if (icon_buffer && (x == 0 && y == 0)) {
        memcpy(icon_buffer, bitmap, w * h * 2);

        // Configura descrittore immagine LVGL
        error_icon_dsc.header.always_zero = 0;
        error_icon_dsc.header.w = w;
        error_icon_dsc.header.h = h;
        error_icon_dsc.data_size = w * h * 2;
        error_icon_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        error_icon_dsc.data = icon_buffer;
    }

    return true;
}

// === FUNZIONE CARICAMENTO ICONA === (DISABILITATO)
bool loadErrorIcon(const char* iconFile) {
    // Libera buffer precedente
    if (icon_buffer) {
        free(icon_buffer);
        icon_buffer = nullptr;
        icon_buffer_size = 0;
    }

    // Costruisci path completo
    String iconPath = "/icons/" + String(iconFile);

    // Verifica esistenza file
    if (!SD.exists(iconPath.c_str())) {
        Serial.printf("ERRORE: Icona non trovata: %s\n", iconPath.c_str());
        return false;
    }

    // Configura TJpgDec per icone
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(icon_output_callback);

    // Decodifica icona
    if (TJpgDec.drawSdJpg(0, 0, iconPath.c_str()) != JDR_OK) {
        Serial.printf("ERRORE: Decodifica fallita: %s\n", iconPath.c_str());
        return false;
    }

    Serial.printf("ICONA: Caricata %s (%dx%d)\n", iconFile,
                  error_icon_dsc.header.w, error_icon_dsc.header.h);
    return true;
}
*/

// === IMPLEMENTAZIONI FUNZIONI HELPER PER ERRORI ===

// Trova l'errore con priorità più alta attivo
const ErrorDefinition* getHighestPriorityError(uint32_t errorFlags) {
    for (int i = 0; i < ERROR_COUNT; i++) {
        if (errorFlags & ERROR_DEFINITIONS[i].flag) {
            return &ERROR_DEFINITIONS[i];
        }
    }
    return nullptr; // Nessun errore attivo
}

// Ottieni il numero di errori attivi
int getActiveErrorCount(uint32_t errorFlags) {
    int count = 0;
    for (int i = 0; i < ERROR_COUNT; i++) {
        if (errorFlags & ERROR_DEFINITIONS[i].flag) {
            count++;
        }
    }
    return count;
}

// Verifica se ci sono errori critici
bool hasCriticalErrors(uint32_t errorFlags) {
    for (int i = 0; i < ERROR_COUNT; i++) {
        if ((errorFlags & ERROR_DEFINITIONS[i].flag) && 
            ERROR_DEFINITIONS[i].priority == PRIORITY_CRITICAL) {
            return true;
        }
    }
    return false;
}

// === NUOVA FUNZIONE: OTTIENI ERRORE PER INDICE (ROTAZIONE) ===
const ErrorDefinition* getErrorByIndex(uint32_t errorFlags, int index) {
    int currentIndex = 0;
    
    // Scorri errori in ordine di priorità 
    for (int i = 0; i < ERROR_COUNT; i++) {
        if (errorFlags & ERROR_DEFINITIONS[i].flag) {
            if (currentIndex == index) {
                return &ERROR_DEFINITIONS[i];
            }
            currentIndex++;
        }
    }
    
    return nullptr; // Non dovrebbe mai accadere
}

// === FUNZIONE AGGIORNAMENTO VISUALIZZAZIONE ERRORI CON PANEL COLORATI ===
void updateErrorDisplay(uint32_t errorFlags) {
    static uint32_t lastErrorFlags = 0;
    static unsigned long lastErrorUpdate = 0;
    static unsigned long lastRotation = 0;
    static int currentErrorIndex = 0;

    // Conta errori attivi
    int totalErrors = getActiveErrorCount(errorFlags);

    // Reset indice se errori cambiano
    if (errorFlags != lastErrorFlags) {
        currentErrorIndex = 0;
        lastRotation = millis();
        lastErrorFlags = errorFlags;
        lastErrorUpdate = millis();
    }

    // Aggiorna solo se necessario
    bool needsUpdate = false;

    if (totalErrors == 0) {
        needsUpdate = true; // Sempre aggiorna quando nessun errore
    } else if (totalErrors == 1) {
        needsUpdate = (millis() - lastErrorUpdate) > 5000; // Aggiorna ogni 5 sec per errore singolo
    } else {
        // ERRORI MULTIPLI: Rotazione ogni 5 secondi
        if (millis() - lastRotation > 5000) {
            currentErrorIndex = (currentErrorIndex + 1) % totalErrors;
            lastRotation = millis();
            needsUpdate = true;
        }
    }

    if (!needsUpdate) return;

    lastErrorUpdate = millis();

    // === NESSUN ERRORE ===
    if (totalErrors == 0) {
        // Testo messaggi
        lv_label_set_text(ui_messaggi, "Sistema OK");
        lv_label_set_text(ui_messaggiC, "Sistema OK");

        // Panel VERDE automotive morbido (Sistema OK) - 0x2D8659
        lv_obj_set_style_bg_color(ui_Panel3, lv_color_hex(0x2D8659), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_Panel_messaggiC, lv_color_hex(0x2D8659), LV_PART_MAIN);

        return;
    }

    // === ERRORE ATTIVO (SINGOLO O ROTAZIONE) ===
    const ErrorDefinition* activeError = nullptr;

    if (totalErrors == 1) {
        // Errore singolo: usa priorità massima
        activeError = getHighestPriorityError(errorFlags);
    } else {
        // Errori multipli: usa rotazione
        activeError = getErrorByIndex(errorFlags, currentErrorIndex);
    }

    if (!activeError) return;

    // Prepara testo con indicatore multipli
    String displayText1, displayText2;
    if (totalErrors > 1) {
        displayText1 = String(activeError->textScreen1) + " (" + String(currentErrorIndex + 1) + "/" + String(totalErrors) + ")";
        displayText2 = String(activeError->textScreen2) + " (" + String(currentErrorIndex + 1) + "/" + String(totalErrors) + ")";
    } else {
        displayText1 = activeError->textScreen1;
        displayText2 = activeError->textScreen2;
    }

    // Aggiorna testi (NERO fisso)
    lv_label_set_text(ui_messaggi, displayText1.c_str());
    lv_label_set_text(ui_messaggiC, displayText2.c_str());

    // Aggiorna colore PANEL (invece del testo)
    lv_obj_set_style_bg_color(ui_Panel3, lv_color_hex(activeError->color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_Panel_messaggiC, lv_color_hex(activeError->color), LV_PART_MAIN);

    // Effetto lampeggiante per errori critici (cambia colore PANEL)
    if (activeError->priority == PRIORITY_CRITICAL) {
        static bool blinkState = false;
        static unsigned long lastBlink = 0;

        if (millis() - lastBlink > 500) { // Lampeggia ogni 500ms
            blinkState = !blinkState;
            lastBlink = millis();

            if (blinkState) {
                // Rosso morbido automotive (0xCC3333)
                lv_obj_set_style_bg_color(ui_Panel3, lv_color_hex(0xCC3333), LV_PART_MAIN);
                lv_obj_set_style_bg_color(ui_Panel_messaggiC, lv_color_hex(0xCC3333), LV_PART_MAIN);
            } else {
                // Rosso scuro automotive (0x802020)
                lv_obj_set_style_bg_color(ui_Panel3, lv_color_hex(0x802020), LV_PART_MAIN);
                lv_obj_set_style_bg_color(ui_Panel_messaggiC, lv_color_hex(0x802020), LV_PART_MAIN);
            }
        }
    }
}
// canparser.cpp - PARTE 2/3: CLASSE CONSUMPTIONLEARNER

// === IMPLEMENTAZIONI METODI ConsumptionLearner ===

// COSTRUTTORE - ✅ AGGIORNATO CON DURATION E RIGENERAZIONE
ConsumptionLearner::ConsumptionLearner() : tripIndex(0), hasData(false) {
    // Inizializza array trips
    for (int i = 0; i < 10; i++) {
        trips[i].valid = false;
        trips[i].timestamp = 0;
        trips[i].tripStartTime = 0;   // millis() inizio viaggio
        trips[i].startSOC = 0;
        trips[i].endSOC = 0;
        trips[i].distance = 0;
        trips[i].consumption = 0;
        trips[i].duration = 0;        // Durata viaggio
        trips[i].regenEnergy = 0;     // ← NUOVO: Energia rigenerata
        trips[i].regenPercent = 0;    // ← NUOVO: % rigenerazione
    }
    
    // Inizializza variabili per calcolo live
    liveConsumption = 0;
    liveConsumptionValid = false;
    usingLiveEstimate = false;
    lastLiveUpdate = 0;
}

// METODI PRIVATI
bool ConsumptionLearner::hasValidStartData() {
    return trips[tripIndex].timestamp > 0;
}

bool ConsumptionLearner::isValidTrip(float startSOC, float endSOC, float distance, float consumption) {
    if (distance < 1.0f) return false;
    if (startSOC <= endSOC) return false;
    if (startSOC < 20.0f) return false;
    if (consumption < 40.0f || consumption > 250.0f) return false;
    if ((startSOC - endSOC) < 2.0f) return false;
    return true;
}

// METODI PRINCIPALI
void ConsumptionLearner::begin() {
    // ✅ FIX: SD è già inizializzata in main.cpp, non re-inizializziamo
    // Verifica solo che SD sia disponibile
    if (!SD.exists("/")) {
        sdLogger.warning("SD Card non disponibile per trips.csv");
        return;
    }

    sdLogger.info("Caricamento trips.csv dalla SD...");
    loadTripsFromCSV(); // Carica i viaggi dal CSV
}

void ConsumptionLearner::update(const BMSData& bms) {
    // Implementazione vuota - la logica è nelle funzioni esistenti
}

// GESTIONE ABILITAZIONE
bool ConsumptionLearner::isEnabled() {
    return batteryConfig.getConfigMode() == "auto";
}

// GESTIONE VIAGGI
void ConsumptionLearner::startTrip(float currentSOC, float currentDistance) {
    if (!isEnabled() || currentSOC < 20.0f) return;

    trips[tripIndex].startSOC = currentSOC;
    trips[tripIndex].distance = currentDistance;

    // ✅ TIMESTAMP UNIX in SECONDI (per data/ora reale via RTC)
    if (rtcManager.isRTCAvailable()) {
        trips[tripIndex].timestamp = rtcManager.getUnixTime();  // Unix timestamp (secondi)
    } else {
        trips[tripIndex].timestamp = millis() / 1000;  // Fallback: secondi da boot
    }

    // ✅ FIX CRITICO: Salva millis() per calcolare duration correttamente
    trips[tripIndex].tripStartTime = millis();  // Millisecondi da boot

    trips[tripIndex].valid = false;
    trips[tripIndex].duration = 0;  // Sarà calcolata in endTrip()
}

// ✅ METODO AGGIORNATO CON CALCOLO DURATION E RIGENERAZIONE
void ConsumptionLearner::endTrip(float currentSOC, float currentDistance, float regenEnergy, float batteryCapacity) {
    if (!isEnabled() || !hasValidStartData()) return;

    float tripDistance = currentDistance - trips[tripIndex].distance;
    float energyUsed = (trips[tripIndex].startSOC - currentSOC) *
                      batteryCapacity * 57.6f / 100.0f; // Wh
    float consumption = energyUsed / tripDistance; // Wh/km

    if (isValidTrip(trips[tripIndex].startSOC, currentSOC, tripDistance, consumption)) {
        trips[tripIndex].endSOC = currentSOC;
        trips[tripIndex].distance = tripDistance;
        trips[tripIndex].consumption = consumption;
        trips[tripIndex].valid = true;

        // ✅ FIX CRITICO: Calcola duration usando tripStartTime (millis - millis)
        trips[tripIndex].duration = millis() - trips[tripIndex].tripStartTime;

        // ✅ NUOVO: Salva dati rigenerazione
        trips[tripIndex].regenEnergy = regenEnergy;  // Wh rigenerati
        if (energyUsed > 0) {
            trips[tripIndex].regenPercent = (regenEnergy / energyUsed) * 100.0f;  // % rigenerazione
        } else {
            trips[tripIndex].regenPercent = 0.0f;
        }

        hasData = true;

        // SALVA IMMEDIATAMENTE IN CSV
        appendTripToCSV(trips[tripIndex]);

        sdLogger.logTrip("TRIP_SAVED", tripDistance, trips[tripIndex].startSOC, currentSOC);

        tripIndex = (tripIndex + 1) % 10;
    }
}

// PERSISTENZA DATI - Wrapper per compatibilità
void ConsumptionLearner::saveTrips() {
    // Non fa nulla - i dati sono già salvati in CSV via appendTripToCSV()
}

void ConsumptionLearner::loadTrips() {
    // Non fa nulla - loadTripsFromCSV() è chiamata da begin()
}

void ConsumptionLearner::clearSavedTrips() {
    clearCSVFile();
}

// STATISTICHE
float ConsumptionLearner::getAverageConsumption() {
    float totalConsumption = 0;
    int validTrips = 0;
    
    for (int i = 0; i < 10; i++) {
        if (trips[i].valid) {
            totalConsumption += trips[i].consumption;
            validTrips++;
        }
    }
    
    return (validTrips >= 3) ? (totalConsumption / validTrips) : 0.0f;
}

int ConsumptionLearner::getValidTripsCount() {
    int count = 0;
    for (int i = 0; i < 10; i++) {
        if (trips[i].valid) count++;
    }
    return count;
}

int ConsumptionLearner::getTripsCount() {
    int count = 0;
    for (int i = 0; i < 10; i++) {
        if (trips[i].timestamp > 0) count++;
    }
    return count;
}

// === ACCESSO DATI VIAGGI PER UI ===
const TripData* ConsumptionLearner::getTripData(int index) {
    if (index < 0 || index >= 10) return nullptr;
    if (!trips[index].valid) return nullptr;
    return &trips[index];
}

float ConsumptionLearner::getAverageRegenPercent() {
    float totalRegen = 0;
    int validTrips = 0;

    for (int i = 0; i < 10; i++) {
        if (trips[i].valid && trips[i].regenPercent > 0) {
            totalRegen += trips[i].regenPercent;
            validTrips++;
        }
    }

    return (validTrips > 0) ? (totalRegen / validTrips) : 0.0f;
}

float ConsumptionLearner::getAverageRegenEnergy() {
    float totalEnergy = 0;
    int validTrips = 0;

    for (int i = 0; i < 10; i++) {
        if (trips[i].valid && trips[i].regenEnergy > 0) {
            totalEnergy += trips[i].regenEnergy;
            validTrips++;
        }
    }

    return (validTrips > 0) ? (totalEnergy / validTrips) : 0.0f;
}

float ConsumptionLearner::getBestConsumption() {
    float best = 999.0f;

    for (int i = 0; i < 10; i++) {
        if (trips[i].valid && trips[i].consumption < best) {
            best = trips[i].consumption;
        }
    }

    return (best < 999.0f) ? best : 0.0f;
}

// METODI CSV PER WEB INTERFACE
bool ConsumptionLearner::hasCSVFile() {
    return SD.exists(CSV_FILE);
}

String ConsumptionLearner::getCSVFilePath() {
    return String(CSV_FILE);
}

String ConsumptionLearner::getCSVContent() {
    if (!SD.exists(CSV_FILE)) {
        return "timestamp,startSOC,endSOC,distance,consumption,duration,valid\n# Nessun viaggio registrato";
    }
    
    File file = SD.open(CSV_FILE, FILE_READ);
    if (!file) {
        return "# Errore lettura file trips.csv";
    }
    
    String content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    
    return content;
}

// ============ NUOVO: FUNZIONI CALCOLO LIVE ============

// Aggiorna calcolo consumo in tempo reale
void ConsumptionLearner::updateLiveConsumption(const BMSData& bms) {
    // Solo se viaggio in corso
    if (!bms.tripInProgress || bms.tripStartTime == 0) {
        usingLiveEstimate = false;
        liveConsumptionValid = false;
        return;
    }

    // 🔒 Filtri anti-ottimismo:
    // - niente campioni a bassa velocità (rumore)
    // - niente campioni in rigenerazione (current >= 0 => regen)
    if (bms.speed < 5.0f || bms.current >= 0.0f) {
        return;
    }

    // Calcola dati viaggio attuale
    unsigned long tripDuration = millis() - bms.tripStartTime;
    float tripDistance = bms.drivenDistance - bms.tripStartDistance;
    float socUsed = bms.tripStartSOC - bms.soc;

    // Requisiti minimi per calcolo affidabile
    if (tripDuration < 60000 ||    // Min 1 minuto
        tripDistance < 0.5f ||     // Min 500 metri
        socUsed < 0.5f) {          // Min 0.5% SOC
        return;
    }

    // Consumo attuale del viaggio (Wh/km)
    // NB: 57.6V nominali per stimare Wh dal SOC (coerente col resto del progetto)
    float energyUsedWh = socUsed * bms.setAh * 57.6f / 100.0f;
    float currentConsumption = energyUsedWh / tripDistance; // Wh/km

    // Floor dinamico per evitare valori "impossibili" in discesa
    // MIN_FLOOR = max(80 Wh/km, 0.85 * consumo configurato)
    float cfgWhpkm = batteryConfig.getConsumption();
    float minFloor = max(80.0f, cfgWhpkm * 0.85f);

    // Clamp
    if (currentConsumption < minFloor) currentConsumption = minFloor;
    if (currentConsumption > 300.0f)   return; // Fuori range superiore → ignora

    // Aggiorna dati live (EWMA molto lenta imposto lato media storica)
    liveConsumption = currentConsumption;
    liveConsumptionValid = true;
    lastLiveUpdate = millis();

    // Confronta con media storica per decidere se usare la stima live
    float historicAverage = getAverageConsumption();
    if (historicAverage > 0) {
        float difference = fabsf(liveConsumption - historicAverage);
        float changePercent = (difference / historicAverage) * 100.0f;

        // Se la differenza è significativa (>15%), usa live (più reattivo)
        if (changePercent > 15.0f) {
            usingLiveEstimate = true;
        } else {
            usingLiveEstimate = false;
        }
    }
}

// Ottieni consumo live se disponibile
float ConsumptionLearner::getLiveConsumption() {
    return liveConsumption;
}

// Verifica se dati live sono validi
bool ConsumptionLearner::hasValidLiveData() {
    // Scadenza dopo 30 secondi senza aggiornamenti
    if (liveConsumptionValid && (millis() - lastLiveUpdate) > 30000) {
        liveConsumptionValid = false;
        usingLiveEstimate = false;
    }
    
    return liveConsumptionValid && usingLiveEstimate;
}

// METODO PRINCIPALE: Sceglie automaticamente il consumo migliore
float ConsumptionLearner::getActiveConsumption() {
    // Prima prova calcolo live (se in viaggio e dati validi)
    if (hasValidLiveData()) {
        return liveConsumption; // ⚡ USA LIVE!
    }
    
    // Altrimenti usa calcolo standard esistente
    return getAverageConsumption(); // 📊 USA STORICO
}
// canparser.cpp - PARTE 3/3: CSV, CAN PARSING E AUTO-APPRENDIMENTO

// === METODI CSV PRIVATI ===

// ✅ SALVA SINGOLO VIAGGIO NEL CSV CON DURATION E RIGENERAZIONE
void ConsumptionLearner::appendTripToCSV(const TripData& trip) {
    // ✅ FIX CRITICO: Crea PRIMA il file con header se non esiste
    if (!SD.exists(CSV_FILE)) {
        File file = SD.open(CSV_FILE, FILE_WRITE);
        if (file) {
            file.print("# Trip data - timestamp in Unix time (seconds since 1970-01-01 UTC)\n");
            file.print("timestamp,startSOC,endSOC,distance,consumption,regenEnergy,regenPercent,duration,valid\n");
            file.close();
            sdLogger.info("File trips.csv creato con header");
        } else {
            sdLogger.error("Impossibile creare /trips.csv");
            return;
        }
    }

    // POI apre in append per aggiungere il viaggio
    File file = SD.open(CSV_FILE, FILE_APPEND);
    if (!file) {
        sdLogger.error("Impossibile aprire /trips.csv per scrittura");
        return;
    }

    // ✅ APPEND DEL NUOVO VIAGGIO CON DURATION E RIGENERAZIONE
    file.printf("%lu,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%lu,%d\n",
                trip.timestamp,
                trip.startSOC,
                trip.endSOC,
                trip.distance,
                trip.consumption,
                trip.regenEnergy,      // ← NUOVO: Energia rigenerata (Wh)
                trip.regenPercent,     // ← NUOVO: % rigenerazione
                trip.duration,
                trip.valid ? 1 : 0);
    file.close();

    sdLogger.debug("Viaggio salvato in trips.csv");
}

// ✅ CARICA ULTIMI 10 VIAGGI DAL CSV - COMPATIBILE CON FORMATO VECCHIO E NUOVO
void ConsumptionLearner::loadTripsFromCSV() {
    // Reset array
    for (int i = 0; i < 10; i++) {
        trips[i].valid = false;
        trips[i].timestamp = 0;
        trips[i].tripStartTime = 0;  // ← NUOVO CAMPO
        trips[i].duration = 0;
    }
    tripIndex = 0;
    hasData = false;

    if (!SD.exists(CSV_FILE)) {
        sdLogger.info("File trips.csv non trovato, verrà creato al primo viaggio");
        return;
    }

    File file = SD.open(CSV_FILE, FILE_READ);
    if (!file) {
        return;
    }
    
    // Leggi prima riga per determinare formato
    String headerLine = "";
    if (file.available()) {
        headerLine = file.readStringUntil('\n');
        headerLine.trim();
    }

    // ✅ COMPATIBILITÀ: Rileva il formato del CSV
    bool hasLegacyFormat = (headerLine.indexOf("duration") == -1);
    bool hasRegenFormat = (headerLine.indexOf("regenEnergy") != -1);
    
    // Array temporaneo per tutti i viaggi
    TripData allTrips[100]; // Max 100 viaggi nel file
    int totalTrips = 0;
    
    // Leggi tutti i viaggi
    while (file.available() && totalTrips < 100) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0 || line.startsWith("#")) continue;
        
        // ✅ PARSE COMPATIBILE: gestisce formato vecchio, con durata, e con rigenerazione
        if (hasLegacyFormat) {
            // Formato vecchio: timestamp,startSOC,endSOC,distance,consumption,valid
            int commaIndexes[5];
            int commaCount = 0;

            for (int i = 0; i < line.length() && commaCount < 5; i++) {
                if (line.charAt(i) == ',') {
                    commaIndexes[commaCount++] = i;
                }
            }

            if (commaCount >= 5) {
                allTrips[totalTrips].timestamp = line.substring(0, commaIndexes[0]).toInt();
                allTrips[totalTrips].startSOC = line.substring(commaIndexes[0] + 1, commaIndexes[1]).toFloat();
                allTrips[totalTrips].endSOC = line.substring(commaIndexes[1] + 1, commaIndexes[2]).toFloat();
                allTrips[totalTrips].distance = line.substring(commaIndexes[2] + 1, commaIndexes[3]).toFloat();
                allTrips[totalTrips].consumption = line.substring(commaIndexes[3] + 1, commaIndexes[4]).toFloat();
                allTrips[totalTrips].duration = 0;       // Default per formato legacy
                allTrips[totalTrips].regenEnergy = 0;    // Default per formato legacy
                allTrips[totalTrips].regenPercent = 0;   // Default per formato legacy
                allTrips[totalTrips].valid = (line.substring(commaIndexes[4] + 1).toInt() == 1);

                if (allTrips[totalTrips].valid) {
                    totalTrips++;
                }
            }
        } else if (!hasRegenFormat) {
            // Formato con durata: timestamp,startSOC,endSOC,distance,consumption,duration,valid
            int commaIndexes[6];
            int commaCount = 0;

            for (int i = 0; i < line.length() && commaCount < 6; i++) {
                if (line.charAt(i) == ',') {
                    commaIndexes[commaCount++] = i;
                }
            }

            if (commaCount >= 6) {
                allTrips[totalTrips].timestamp = line.substring(0, commaIndexes[0]).toInt();
                allTrips[totalTrips].startSOC = line.substring(commaIndexes[0] + 1, commaIndexes[1]).toFloat();
                allTrips[totalTrips].endSOC = line.substring(commaIndexes[1] + 1, commaIndexes[2]).toFloat();
                allTrips[totalTrips].distance = line.substring(commaIndexes[2] + 1, commaIndexes[3]).toFloat();
                allTrips[totalTrips].consumption = line.substring(commaIndexes[3] + 1, commaIndexes[4]).toFloat();
                allTrips[totalTrips].duration = line.substring(commaIndexes[4] + 1, commaIndexes[5]).toInt();
                allTrips[totalTrips].regenEnergy = 0;    // Default per formato senza regen
                allTrips[totalTrips].regenPercent = 0;   // Default per formato senza regen
                allTrips[totalTrips].valid = (line.substring(commaIndexes[5] + 1).toInt() == 1);

                if (allTrips[totalTrips].valid) {
                    totalTrips++;
                }
            }
        } else {
            // Formato completo: timestamp,startSOC,endSOC,distance,consumption,regenEnergy,regenPercent,duration,valid
            int commaIndexes[8];
            int commaCount = 0;

            for (int i = 0; i < line.length() && commaCount < 8; i++) {
                if (line.charAt(i) == ',') {
                    commaIndexes[commaCount++] = i;
                }
            }

            if (commaCount >= 8) {
                allTrips[totalTrips].timestamp = line.substring(0, commaIndexes[0]).toInt();
                allTrips[totalTrips].startSOC = line.substring(commaIndexes[0] + 1, commaIndexes[1]).toFloat();
                allTrips[totalTrips].endSOC = line.substring(commaIndexes[1] + 1, commaIndexes[2]).toFloat();
                allTrips[totalTrips].distance = line.substring(commaIndexes[2] + 1, commaIndexes[3]).toFloat();
                allTrips[totalTrips].consumption = line.substring(commaIndexes[3] + 1, commaIndexes[4]).toFloat();
                allTrips[totalTrips].regenEnergy = line.substring(commaIndexes[4] + 1, commaIndexes[5]).toFloat();    // ← NUOVO
                allTrips[totalTrips].regenPercent = line.substring(commaIndexes[5] + 1, commaIndexes[6]).toFloat();  // ← NUOVO
                allTrips[totalTrips].duration = line.substring(commaIndexes[6] + 1, commaIndexes[7]).toInt();
                allTrips[totalTrips].valid = (line.substring(commaIndexes[7] + 1).toInt() == 1);

                if (allTrips[totalTrips].valid) {
                    totalTrips++;
                }
            }
        }
    }
    file.close();
    
    if (totalTrips > 0) {
        // Ordinamento per timestamp (più recenti per primi)
        for (int i = 0; i < totalTrips - 1; i++) {
            for (int j = 0; j < totalTrips - 1 - i; j++) {
                if (allTrips[j].timestamp < allTrips[j + 1].timestamp) {
                    TripData temp = allTrips[j];
                    allTrips[j] = allTrips[j + 1];
                    allTrips[j + 1] = temp;
                }
            }
        }
        
        // Copia ultimi 10 viaggi nell'array di lavoro
        int copyCount = (totalTrips > 10) ? 10 : totalTrips;
        for (int i = 0; i < copyCount; i++) {
            trips[i] = allTrips[i];
        }
        
        hasData = true;
        tripIndex = copyCount % 10;

        // Sincronizza con BatteryConfig
        int validCount = getValidTripsCount();
        batteryConfig.setTripsCount(validCount);

        if (validCount >= 3) {
            float avgConsumption = getAverageConsumption();
        }
    }
}

// Cancella file CSV
void ConsumptionLearner::clearCSVFile() {
    if (SD.exists(CSV_FILE)) {
        SD.remove(CSV_FILE);
    }
}

void ConsumptionLearner::saveTripsToCSV() {
    // Implementazione vuota - compatibilità 
}

// ✅ METODO AGGIORNATO CON DURATION E VELOCITÀ MEDIA
String ConsumptionLearner::getStatsHTML() {
    String html = "<h3>Statistiche Auto-Apprendimento CSV</h3>";
    
    int validTrips = getValidTripsCount();
    int totalTrips = getTripsCount();
    
    // INFO SISTEMA
    html += "<div style='background:#e3f2fd; padding:15px; border-radius:8px; margin:10px 0;'>";
    html += "<strong>Sistema CSV Attivo</strong><br>";
    html += "File: <code>/trips.csv</code> su SD Card<br>";
    html += "Stato: " + String(isEnabled() ? "ATTIVO" : "DISATTIVO") + "<br>";
    html += "Viaggi in memoria: " + String(validTrips) + "/10<br>";
    html += "File esistente: " + String(hasCSVFile() ? "SI" : "NO") + "<br>";
    html += "</div>";
    
    if (validTrips > 0) {
        float avgCons = getAverageConsumption();
        
        // STATISTICHE PRINCIPALI
        html += "<div style='background:#e8f5e8; padding:15px; border-radius:8px; margin:10px 0;'>";
        html += "<strong>Dati Analizzati</strong><br>";
        html += "Viaggi validi: " + String(validTrips) + " (in memoria)<br>";
        html += "Consumo medio: " + String(avgCons, 1) + " Wh/km<br>";
        
        if (avgCons > 0) {
            html += "Stato apprendimento: <span style='color:green;'>ATTIVO</span><br>";
            html += "Configurazione automatica disponibile!<br>";
        }
        html += "</div>";
        
        // AZIONI DISPONIBILI
        html += "<div style='background:#fff3cd; padding:15px; border-radius:8px; margin:10px 0;'>";
        html += "<strong>Azioni Disponibili</strong><br>";
        html += "<a href='/csv' style='color:#2196F3; text-decoration:none; margin:5px;'>";
        html += "Visualizza File CSV Completo</a><br>";
        html += "<a href='/api/download_csv' download='trips.csv' style='color:#28a745; text-decoration:none; margin:5px;'>";
        html += "Scarica File CSV</a><br>";
        html += "</div>";
        
        // LISTA VIAGGI RECENTI (Max 5) - ✅ CON DURATA E VELOCITÀ MEDIA
        html += "<h4>Ultimi Viaggi (memoria interna):</h4>";
        
        TripData sortedTrips[10];
        int validCount = 0;
        
        // Copia viaggi validi
        for (int i = 0; i < 10; i++) {
            if (trips[i].valid) {
                sortedTrips[validCount] = trips[i];
                validCount++;
            }
        }
        
        // Ordinamento per timestamp (più recenti primi)
        for (int i = 0; i < validCount - 1; i++) {
            for (int j = 0; j < validCount - 1 - i; j++) {
                if (sortedTrips[j].timestamp < sortedTrips[j + 1].timestamp) {
                    TripData temp = sortedTrips[j];
                    sortedTrips[j] = sortedTrips[j + 1];
                    sortedTrips[j + 1] = temp;
                }
            }
        }
        
        // Mostra max 5 viaggi più recenti
        int maxShow = (validCount < 5) ? validCount : 5;
        for (int i = 0; i < maxShow; i++) {
            html += "<div style='border:1px solid #ddd; padding:10px; margin:5px 0; border-radius:5px; background:#f8f9fa;'>";
            html += "<strong>Viaggio " + String(i + 1) + "</strong><br>";
            html += "SOC: " + String(sortedTrips[i].startSOC, 1) + "% → " + String(sortedTrips[i].endSOC, 1) + "% ";
            html += "(" + String(sortedTrips[i].startSOC - sortedTrips[i].endSOC, 1) + "% usato)<br>";
            html += "Distanza: " + String(sortedTrips[i].distance, 1) + " km<br>";
            html += "Consumo: <strong>" + String(sortedTrips[i].consumption, 1) + " Wh/km</strong><br>";
            
            // ✅ NUOVO: Durata e velocità media
            if (sortedTrips[i].duration > 0) {
                float durationMin = sortedTrips[i].duration / 60000.0f;
                float avgSpeed = (durationMin > 0) ? (sortedTrips[i].distance / (durationMin / 60.0f)) : 0;
                
                if (durationMin < 60) {
                    html += "Durata: " + String(durationMin, 1) + " min<br>";
                } else {
                    int hours = (int)(durationMin / 60);
                    int mins = (int)(durationMin) % 60;
                    html += "Durata: " + String(hours) + "h" + String(mins) + "m<br>";
                }
                
                if (avgSpeed > 0) {
                    html += "Velocità media: " + String(avgSpeed, 1) + " km/h<br>";
                }
            }
            
            // ✅ TIMESTAMP: Mostra data/ora reale se RTC disponibile
            if (rtcManager.isRTCAvailable() && sortedTrips[i].timestamp > 1000000) {
                // Unix timestamp valido: converti in data leggibile
                uint32_t currentTime = rtcManager.getUnixTime();
                uint32_t age = currentTime - sortedTrips[i].timestamp;

                // Data assoluta (formattazione semplificata)
                // Per conversione completa servirebbero librerie, usiamo tempo relativo
                if (age < 3600) {
                    html += "Data: " + String(age / 60) + " minuti fa";
                } else if (age < 86400) {
                    html += "Data: " + String(age / 3600) + " ore fa";
                } else if (age < 2592000) { // < 30 giorni
                    html += "Data: " + String(age / 86400) + " giorni fa";
                } else {
                    // Mostra timestamp grezzo per viaggi vecchi
                    html += "Data: " + String(sortedTrips[i].timestamp) + " (Unix time)";
                }
            } else {
                // Fallback per timestamp senza RTC (millis approssimato)
                html += "Data: timestamp=" + String(sortedTrips[i].timestamp);
            }
            html += "</div>";
        }
        
        if (validCount > 5) {
            html += "<p><em>... e altri " + String(validCount - 5) + " viaggi. ";
            html += "<a href='/csv'>Visualizza tutti nel file CSV</a></em></p>";
        }
        
    } else {
        // NESSUN DATO
        html += "<div style='background:#ffebee; padding:15px; border-radius:8px; margin:10px 0;'>";
        html += "<strong>Nessun Viaggio Valido</strong><br>";
        html += "Per attivare l'auto-apprendimento:<br>";
        html += "1. Effettua almeno 3 viaggi di almeno 1 km<br>";
        html += "2. Consumo deve essere tra 40-250 Wh/km<br>";
        html += "3. SOC deve scendere di almeno 2%<br>";
        html += "4. SOC iniziale deve essere ≥ 20%<br><br>";
        html += "I dati saranno salvati automaticamente su <code>/trips.csv</code>";
        html += "</div>";
    }
    
    return html;
}

// RESET - ✅ AGGIORNATO CON DURATION E RIGENERAZIONE
void ConsumptionLearner::reset() {
    // Reset in memoria
    for (int i = 0; i < 10; i++) {
        trips[i].valid = false;
        trips[i].timestamp = 0;
        trips[i].tripStartTime = 0;   // ← CAMPO AGGIUNTO
        trips[i].startSOC = 0;
        trips[i].endSOC = 0;
        trips[i].distance = 0;
        trips[i].consumption = 0;
        trips[i].duration = 0;        // Durata viaggio
        trips[i].regenEnergy = 0;     // ← NUOVO: Energia rigenerata
        trips[i].regenPercent = 0;    // ← NUOVO: % rigenerazione
    }
    tripIndex = 0;
    hasData = false;

    // Reset calcolo live
    liveConsumption = 0;
    liveConsumptionValid = false;
    usingLiveEstimate = false;
    lastLiveUpdate = 0;

    // Reset CSV
    clearCSVFile();

    sdLogger.info("Auto-apprendimento resettato");
}

// === FUNZIONI HELPER ===

// Funzione per aggiornare consumo appreso - CORRETTO!
void updateLearnedConsumption() {
    // ⚡ CORREZIONE: Usa getActiveConsumption() invece di getAverageConsumption()
    float avgConsumption = consumptionLearner.getActiveConsumption();
    int validTrips = consumptionLearner.getValidTripsCount();
    
    if (avgConsumption > 0 && validTrips >= 3) {
        batteryConfig.setConsumption(avgConsumption);
        batteryConfig.setConfigMode("auto");
        batteryConfig.setTripsCount(validTrips);
        batteryConfig.setLastConsumption(avgConsumption);
        batteryConfig.setLastUpdate(millis());
    }
}

// === FUNZIONE PARSE CAN FRAME ===
void parseCANFrame(uint32_t id, uint8_t len, uint8_t *data, BMSData &bms) {
    // Lazy init odometro: carica da SD al primo frame
    if (!odo_loaded) {
        odo_load_from_sd();
    }
    switch (id) {
        // Frame 0x0155: SOC, Corrente e Stadio Carica
        case 0x0155: {
            if (len >= 6) {
                // Byte 0: Stadio carica (0-7, 0=100% SOC)
                bms.chargeStage = data[0] & 0x07;
                
                // Corrente (bytes 1-2)
                uint16_t rawI = ((uint16_t)data[1] << 8) | data[2];
                rawI &= 0x0FFF;
                bms.current = ((int16_t)rawI - 2000) / 4.0f;

                // Durante ricarica, corrente positiva = carica
                if (bms.isCharging && bms.current > 0) {
                    bms.chargeCurrent = bms.current;
                }

                // ✅ NUOVO: Tracciamento energia rigenerata durante viaggio
                if (bms.tripInProgress && !bms.isCharging && bms.current > 0) {
                    // Rigenerazione: corrente positiva durante guida
                    unsigned long now = millis();
                    if (bms.lastRegenUpdate > 0) {
                        unsigned long deltaT = now - bms.lastRegenUpdate; // ms
                        // Energia = Potenza * Tempo
                        // P(W) = V(V) * I(A), E(Wh) = P(W) * t(h)
                        float powerW = bms.voltage * bms.current;  // Watt
                        float energyWh = powerW * (deltaT / 3600000.0f);  // Wh
                        bms.tripRegenEnergy += energyWh;
                    }
                    bms.lastRegenUpdate = now;
                }

                // SOC (bytes 4-5) - IMPORTANTE PER AUTO-APPRENDIMENTO
                float previousSOC = bms.soc;
                uint16_t rawSOC = ((uint16_t)data[4] << 8) | data[5];
                bms.soc = constrain(rawSOC / 400.0f, 0.0f, 100.0f);
                bms.socAh = bms.soc * bms.setAh / 100.0f;
                
                // AUTO-APPRENDIMENTO: Monitora cambio SOC significativo
                if (abs(bms.soc - previousSOC) > 0.5f && !bms.isCharging) {
                    bms.lastValidSOC = bms.soc;
                    
                    // Se AUTO-APPRENDIMENTO attivo, processa evento SOC
                    String configMode = batteryConfig.getConfigMode();
                    if (configMode == "auto") {
                        processSOCChange(bms, previousSOC);
                    }
                }
            }
            break;
        }

        // Frame 0x055F: Tensione batteria
        case 0x055F: {
            if (len >= 8) {
                uint32_t raw = ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];
                bms.voltage = (raw >> 12) / 10.0f;
            }
            break;
        }

        // Frame 0x0554: Temperature celle
        case 0x0554: {
            float sum = 0;
            int validCells = 0;
            for (int i = 0; i < 7 && i < len; i++) {
                float temp = data[i] - 40;
                if (temp >= -40 && temp <= 150) {
                    bms.cellTemps[i] = temp;
                    sum += temp;
                    validCells++;
                }
            }
            if (validCells > 0) {
                bms.avgTemp = round(sum / validCells);
                bms.lastTempUpdate = millis();
            }
            break;
        }

        // Frame 0x05D7: Velocita principale - IMPORTANTE PER AUTO-APPRENDIMENTO
        case 0x05D7: {
        if (len >= 6) {
        uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
        float spd05D7 = raw * 0.01f;   // km/h dal cluster

        // NON aggiornare più la velocità ufficiale
        // bms.speed = spd05D7;

        // (Opzionale) log o campo diagnostico
        // Serial.printf("[05D7] kph_cluster=%.2f\n", spd05D7);
    }
    break;
    }


        // Frame 0x019F: Velocita display + ODOMETRO DA RPM
        case 0x019F: {
    if (len >= 4) {
        // Ricostruzione RPM
        uint16_t raw  = ((uint16_t)data[2] << 4) | (data[3] >> 4);
        int16_t delta = (int16_t)raw - 2000;   // offset
        int rpm       = delta * 10;            // 10 rpm/LSB

        // Formula per km/h
        float kph = fabsf(rpm) / 7250.0f * 80.0f;

        #if USE_RPM_SPEED
        // Filtro anti-rumore (EMA)
        static float speedFilt = 0.0f;
        const float a = SPEED_EMA_ALPHA;
        speedFilt = a * kph + (1.0f - a) * speedFilt;
        bms.speed = speedFilt;     // <-- ORA la velocità ufficiale
        #else
        bms.displaySpeed = kph;    // alternativa
        #endif

        // === NUOVO: AGGIORNA ODOMETRO DA RPM ===
        odo_update_from_rpm(rpm);
        bms.odometer = (float)current_odometer_km;

        // (Opzionale) log di verifica
        // Serial.printf("[019F] rpm=%d  kph=%.2f  odo=%u km\n", rpm, kph, current_odometer_km);
    }
    break;
}


        // Frame 0x0426: NON PIÙ USATO (odometro ora da RPM)
        case 0x0426: {
            // Ignorato - odometro ora calcolato da giri motore (frame 0x019F)
            break;
        }


        // Frame 0x059B: Marcia
        case 0x059B: {
            if (len >= 1) {
                bms.gear = data[0];
            }
            break;
        }

        // Frame 0x0597: Diagnostica 12V + RILEVAMENTO RICARICA
        case 0x0597: {
            if (len >= 4) {
                // Byte 2: Corrente DC/DC per diagnostica 12V
                uint8_t currentRaw = data[2];
                float current12v = currentRaw * 0.2f;
                
                // Gestione batteria 12V tramite sistema errori
                static unsigned long last12vCheck = 0;
                if (millis() - last12vCheck > 10000) { // Controlla ogni 10 secondi
                    last12vCheck = millis();
                    
                    // Logica batteria 12V
                    bool batteryLow = false;
                    if (bms.speed > 5.0f) {
                        // In movimento: corrente DC/DC alta = batteria scarica
                        batteryLow = (current12v > 8.0f);
                    } else {
                        // Fermo: corrente DC/DC molto alta = batteria molto scarica
                        batteryLow = (current12v > 12.0f);
                    }
                    
                    // Aggiorna flag errore 12V
                    if (batteryLow) {
                        bms.errorFlags |= TWIZY_LOW_12V;  // Attiva flag
                    } else {
                        bms.errorFlags &= ~TWIZY_LOW_12V; // Disattiva flag
                    }
                }

                // Byte 3: Protocollo carica CHG→BMS
                uint8_t chargeProtocol = data[3];
                bool wasCharging = bms.isCharging;
                bms.chargeProtocol = chargeProtocol;

                // Rilevamento stato ricarica
                bms.isCharging = (chargeProtocol == 0xB1);
                bms.chargeReady = (chargeProtocol == 0x41 || chargeProtocol == 0x91);

                // Log cambio stato ricarica
                if (bms.isCharging != wasCharging) {
                    if (bms.isCharging) {
                        bms.chargeStartTime = millis();
                        bms.chargeStartSOC = bms.soc;
                        bms.chargeCurrent = 0;

                        // AUTO-APPRENDIMENTO: Termina viaggio se in corso
                        String configMode = batteryConfig.getConfigMode();
                        if (bms.tripInProgress && configMode == "auto") {
                            endCurrentTrip(bms);
                        }
                    } else {
                        bms.chargeCurrent = 0;
                        bms.chargeTimeRemaining = 0;
                    }
                }
            }
            break;
        }

        // Frame 0x0081: Errori
        case 0x0081: {
            if (len >= 6) {
                // Leggi flag errori estesi (32-bit)
                uint32_t newErrorFlags = ((uint16_t)data[4] << 8) | data[5];
                
                // Espandi a 32-bit se disponibili bytes aggiuntivi
                if (len >= 8) {
                    newErrorFlags |= ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
                }
                
                // Mantieni flag 12V da diagnostica locale se non presente in CAN
                if (!(newErrorFlags & TWIZY_LOW_12V) && (bms.errorFlags & TWIZY_LOW_12V)) {
                    newErrorFlags |= TWIZY_LOW_12V;
                }
                
                // Log cambio errori
                if (newErrorFlags != bms.errorFlags) {
                    if (newErrorFlags > 0) {
                        const ErrorDefinition* err = getHighestPriorityError(newErrorFlags);
                        if (err) {
                            sdLogger.logf(LOG_ERROR, "BMS Error: %s (0x%08X)", err->textScreen1, newErrorFlags);
                        }
                    } else {
                        sdLogger.info("BMS: All errors resolved");
                    }
                }
                
                bms.errorFlags = newErrorFlags;
                bms.error = (uint16_t)newErrorFlags; // Mantieni compatibilità 
            }
            break;
        }

        default:
            // Frame non riconosciuto
            break;
    }
}

// === FUNZIONI AUTO-APPRENDIMENTO ===

void processSpeedChange(BMSData &bms, float previousSpeed) {
    bool isMovingNow = (bms.speed > 2.0f); // Soglia movimento: 2 km/h
    bool wasMovingBefore = bms.wasMoving;
    
    // INIZIO MOVIMENTO
    if (isMovingNow && !wasMovingBefore && !bms.isCharging) {
        if (!bms.tripInProgress && bms.soc > 20.0f) {
            startNewTrip(bms);
        }
    }
    
    // FINE MOVIMENTO (veicolo fermo per più di 5 secondi)
    static unsigned long stoppedTime = 0;
    if (!isMovingNow && wasMovingBefore) {
        stoppedTime = millis();
    }
    
    if (!isMovingNow && bms.tripInProgress && 
        stoppedTime > 0 && (millis() - stoppedTime) > 5000) { // 5 secondi fermo
        endCurrentTrip(bms);
        stoppedTime = 0;
    }
    
    bms.wasMoving = isMovingNow;
}

void processSOCChange(BMSData &bms, float previousSOC) {
    // Solo se in movimento e non in carica
    if (bms.speed > 2.0f && !bms.isCharging) {
        // Verifica se SOC sta diminuendo significativamente
        if (bms.soc < (previousSOC - 0.3f)) {
            // Se non c'è viaggio in corso, iniziane uno
            if (!bms.tripInProgress) {
                startNewTrip(bms);
            }
        }
    }
}

void startNewTrip(BMSData &bms) {
    if (bms.soc < 20.0f || bms.isCharging) return; // Condizioni non adatte

    bms.tripInProgress = true;
    bms.tripStartSOC = bms.soc;
    bms.tripStartDistance = bms.drivenDistance; // Usa distanza totale accumulata
    bms.tripStartTime = millis();

    // ✅ NUOVO: Inizializza contatori rigenerazione
    bms.tripRegenEnergy = 0;
    bms.lastRegenUpdate = millis();

    sdLogger.logTrip("TRIP_START", 0.0f, bms.soc, 0.0f);

    // Notifica al learner
    consumptionLearner.startTrip(bms.soc, bms.drivenDistance);
}

void endCurrentTrip(BMSData &bms) {
    if (!bms.tripInProgress) return;

    float tripDistance = bms.drivenDistance - bms.tripStartDistance;
    unsigned long tripDuration = millis() - bms.tripStartTime;

    // Solo se viaggio valido (> 1 km e > 2 minuti)
    if (tripDistance > 1.0f && tripDuration > 120000) {
        // Salva il viaggio con energia rigenerata (che chiamerà automaticamente appendTripToCSV())
        consumptionLearner.endTrip(bms.soc, bms.drivenDistance, bms.tripRegenEnergy, bms.setAh);

        // Aggiorna configurazione se ci sono abbastanza viaggi
        updateLearnedConsumption();

        sdLogger.logTrip("TRIP_END", tripDistance, bms.tripStartSOC, bms.soc);
    } else {
        sdLogger.logTrip("TRIP_REJECTED", tripDistance, bms.tripStartSOC, bms.soc);
    }

    // Reset stato viaggio
    bms.tripInProgress = false;
    bms.tripStartSOC = 0;
    bms.tripStartDistance = 0;
    bms.tripStartTime = 0;

    // ✅ NUOVO: Reset contatori rigenerazione
    bms.tripRegenEnergy = 0;
    bms.lastRegenUpdate = 0;
}

void updateTripTracking(BMSData &bms) {
    // Solo se auto-apprendimento attivo
    String configMode = batteryConfig.getConfigMode();
    if (configMode != "auto") return;
    
    static unsigned long lastTrackingUpdate = 0;
    static unsigned long tripStoppedTime = 0;
    unsigned long now = millis();
    
    // Aggiorna ogni 5 secondi
    if (now - lastTrackingUpdate < 5000) return;
    lastTrackingUpdate = now;
    
    // Verifica timeout viaggio (più di 1 ora fermo)
    if (bms.tripInProgress && bms.speed < 1.0f) {
        if (tripStoppedTime == 0) {
            tripStoppedTime = now;
        } else if (now - tripStoppedTime > 3600000) { // 1 ora
            endCurrentTrip(bms);
            tripStoppedTime = 0;
        }
    } else {
        tripStoppedTime = 0; // Reset se in movimento
    }
}

void resetAutoLearning() {
    // Reset completo: RAM + file CSV + BatteryConfig
    consumptionLearner.reset();
    batteryConfig.resetAutoLearning();
}

void enableAutoLearning() {
    batteryConfig.setConfigMode("auto");

    // Verifica se ci sono già viaggi salvati
    int savedTrips = consumptionLearner.getValidTripsCount();
    if (savedTrips > 0) {
        if (savedTrips >= 3) {
            float avgConsumption = consumptionLearner.getAverageConsumption();
            batteryConfig.setConsumption(avgConsumption);
        }
    }
}