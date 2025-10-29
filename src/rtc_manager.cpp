// rtc_manager.cpp - Implementazione RTC PCF8523 + OneButton Manager
#include "rtc_manager.h"
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Istanza globale
RTCManager rtcManager;

// === COSTRUTTORE ===
RTCManager::RTCManager()
    : button_(BUTTON_GPIO, true, true) // GPIO, activeLow=true, pullupActive=true
    , buttonAvailable_(false)
    , pendingAction_(BTN_NONE)
    , rtcAvailable_(false)
    , lastNTPSync_(0)
    , timezoneOffset_(0)
    , timezoneDSTActive_(false)
    , timezoneName_("UTC")
    , timezoneDetected_(false)
{}

// === INIZIALIZZAZIONE ===
bool RTCManager::begin() {
    // Avvia I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ);

    // === INIZIALIZZA RTC PCF8523 ===
    if (rtc_.begin()) {
        rtcAvailable_ = true;

        // Verifica se RTC ha perso alimentazione
        if (rtc_.lostPower()) {
            rtc_.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }

        // Configura RTC per batteria backup
        rtc_.start(); // Avvia oscillatore
    }

    // === INIZIALIZZA GPIO PULSANTE (PRE-CONFIGURAZIONE) ===
    // Configura manualmente il GPIO PRIMA di inizializzare OneButton
    // per evitare spike e transitori durante boot ESP32
    pinMode(BUTTON_GPIO, INPUT_PULLUP);

    // Attendi stabilizzazione hardware (filtro capacitivo parassite + RC pull-up)
    delay(100);

    // Leggi stato multiple volte per verificare stabilità
    bool stableState = digitalRead(BUTTON_GPIO);
    for (int i = 0; i < 10; i++) {
        delay(10);
        if (digitalRead(BUTTON_GPIO) != stableState) {
            // Stato instabile, aspetta ancora
            delay(100);
            stableState = digitalRead(BUTTON_GPIO);
            i = 0; // Ricomincia il check
        }
    }

    // === INIZIALIZZA PULSANTE CON ONEBUTTON ===
    button_.setDebounceMs(BUTTON_DEBOUNCE_MS);  // 100ms per filtrare rimbalzi meccanici
    button_.setClickMs(200);                      // Max durata singolo click
    button_.setPressMs(BUTTON_LONG_PRESS_MS);     // Long press threshold

    // Attendi ulteriore stabilizzazione dopo configurazione OneButton
    delay(BUTTON_SETTLE_TIME);

    // Clear qualsiasi evento spurio accumulato durante inizializzazione
    button_.reset();

    // Registra callback
    button_.attachClick(handleClick);
    button_.attachDoubleClick(handleDoubleClick);
    button_.attachLongPressStart(handleLongPress);

    buttonAvailable_ = true;

    // === CARICA TIMEZONE DA PREFERENCES ===
    Preferences prefs;
    prefs.begin("rtc_cfg", true); // read-only
    timezoneOffset_ = prefs.getInt("tz_offset", 0);
    timezoneName_ = prefs.getString("tz_name", "UTC");
    timezoneDSTActive_ = prefs.getBool("tz_dst", false);
    timezoneDetected_ = prefs.getBool("tz_detected", false);
    prefs.end();

    return (rtcAvailable_ || buttonAvailable_);
}

// === LOOP PRINCIPALE ===
void RTCManager::loop() {
    // Aggiorna state machine OneButton (IMPORTANTE!)
    if (buttonAvailable_) {
        button_.tick();
    }

    // Al primo WiFi connect: rileva timezone e sincronizza
    static bool firstSyncDone = false;

    if (rtcAvailable_ && WiFi.status() == WL_CONNECTED) {
        // Primo sync immediato quando WiFi si connette
        if (!firstSyncDone) {
            forceSync();
            firstSyncDone = true;
            lastNTPSync_ = millis();
        }
        // Sync periodico ogni ora
        else {
            unsigned long now = millis();
            if (now - lastNTPSync_ > NTP_SYNC_INTERVAL) {
                syncWithNTP();
                lastNTPSync_ = now;
            }
        }
    } else {
        // Reset flag se WiFi si disconnette (per ri-sincronizzare al prossimo connect)
        if (firstSyncDone && WiFi.status() != WL_CONNECTED) {
            firstSyncDone = false;
        }
    }
}

// === CALLBACK ONEBUTTON ===
void RTCManager::handleClick() {
    rtcManager.pendingAction_ = BTN_SHORT_PRESS;
}

void RTCManager::handleDoubleClick() {
    rtcManager.pendingAction_ = BTN_DOUBLE_PRESS;
}

void RTCManager::handleLongPress() {
    rtcManager.pendingAction_ = BTN_LONG_PRESS;
}

// === GESTIONE PULSANTE CON ONEBUTTON ===
ButtonAction RTCManager::getButtonAction() {
    if (!buttonAvailable_) return BTN_NONE;

    // Leggi e resetta l'azione pendente
    ButtonAction action = pendingAction_;
    if (action != BTN_NONE) {
        pendingAction_ = BTN_NONE; // Reset per prossimo evento
    }

    return action;
}

// === RTC - METODI PUBBLICI ===
DateTime RTCManager::now() {
    if (rtcAvailable_) {
        return rtc_.now();
    }
    // Fallback: usa millis() + offset (impreciso)
    return DateTime(2025, 1, 1, 0, 0, 0);
}

void RTCManager::adjust(const DateTime& dt) {
    if (rtcAvailable_) {
        rtc_.adjust(dt);
    }
}

void RTCManager::syncWithNTP(const char* ntpServer) {
    if (!rtcAvailable_) return;
    if (WiFi.status() != WL_CONNECTED) return;

    // Configura NTP client con offset timezone rilevato
    configTime(timezoneOffset_, 0, ntpServer);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {
        // Sincronizza RTC con NTP (già in ora locale)
        DateTime ntpTime(timeinfo.tm_year + 1900,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_mday,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec);

        adjust(ntpTime);
    }
}

String RTCManager::getTimeString(const char* format) {
    DateTime now = this->now();
    char buf[32];
    snprintf(buf, sizeof(buf), format, now.hour(), now.minute());
    return String(buf);
}

String RTCManager::getDateString(const char* format) {
    DateTime now = this->now();
    char buf[32];
    snprintf(buf, sizeof(buf), format, now.day(), now.month(), now.year());
    return String(buf);
}

String RTCManager::getTimestamp() {
    DateTime now = this->now();
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buf);
}

uint32_t RTCManager::getUnixTime() {
    if (rtcAvailable_) {
        return now().unixtime();
    }
    return 0;
}

// === AUTO-DETECT TIMEZONE ===
bool RTCManager::autoDetectTimezone() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    HTTPClient http;
    http.setTimeout(10000); // 10s timeout
    http.begin("http://worldtimeapi.org/api/ip");

    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        return false;
    }

    // Estrai dati
    const char* tz = doc["timezone"];
    int offset = doc["raw_offset"] | 0;          // Offset base (senza DST)
    int dst_offset = doc["dst_offset"] | 0;      // Offset DST
    bool dst = doc["dst"] | false;

    if (!tz) {
        return false;
    }

    // Calcola offset totale (base + DST se attivo)
    int totalOffset = offset + (dst ? dst_offset : 0);

    // Salva in memoria
    timezoneName_ = String(tz);
    timezoneOffset_ = totalOffset;
    timezoneDSTActive_ = dst;
    timezoneDetected_ = true;

    // Salva in Preferences (persistente)
    Preferences prefs;
    prefs.begin("rtc_cfg", false); // read-write
    prefs.putInt("tz_offset", totalOffset);
    prefs.putString("tz_name", timezoneName_);
    prefs.putBool("tz_dst", dst);
    prefs.putBool("tz_detected", true);
    prefs.end();

    return true;
}

// === FORCE SYNC (timezone + NTP) ===
void RTCManager::forceSync() {
    if (!rtcAvailable_) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    // 1. Rileva timezone (se non già fatto o se cambiato WiFi)
    if (!timezoneDetected_ || timezoneName_ == "UTC") {
        if (!autoDetectTimezone()) {
            timezoneOffset_ = 0;
            timezoneName_ = "UTC";
            timezoneDSTActive_ = false;
        }
    }

    // 2. Sincronizza NTP con timezone corretto
    syncWithNTP("pool.ntp.org");
}

// === DIAGNOSTICA ===
void RTCManager::printStatus() {
    // Stub per compatibilità
}
