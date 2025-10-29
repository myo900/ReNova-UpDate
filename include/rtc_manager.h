// rtc_manager.h - Gestione RTC PCF8523 + OneButton Manager
#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <OneButton.h>

// === CONFIGURAZIONE HARDWARE ===
#define I2C_SDA_PIN         19    // GPIO per SDA (pin 19 sul modulo display)
#define I2C_SCL_PIN         20    // GPIO per SCL (pin 20 sul modulo display)
#define I2C_FREQ            100000 // 100kHz standard

#define PCF8523_ADDR        0x68   // Indirizzo RTC (fisso)

// === CONFIGURAZIONE PULSANTE (OneButton) ===
#define BUTTON_GPIO         38      // GPIO38 collegato al pulsante (pull-up interno, GND quando premuto)
#define BUTTON_DEBOUNCE_MS  100     // Tempo debounce aumentato per filtrare rimbalzi meccanici
#define BUTTON_LONG_PRESS_MS 1500   // Tempo per long press (1.5 secondi)
#define BUTTON_DOUBLE_CLICK_MS 400  // Timeout per double-click
#define BUTTON_SETTLE_TIME  50      // Tempo di stabilizzazione lettura GPIO (ms)

// === AZIONI PULSANTE ===
enum ButtonAction {
    BTN_NONE = 0,
    BTN_SHORT_PRESS,   // Pressione breve: 1 pallino verde
    BTN_LONG_PRESS,    // Pressione lunga (3s): 3 pallini blu
    BTN_DOUBLE_PRESS   // Doppio click: 2 pallini arancio
};

// === CLASSE RTC MANAGER ===
class RTCManager {
private:
    RTC_PCF8523 rtc_;

    // OneButton instance
    OneButton button_;
    bool buttonAvailable_;

    // Button action tracking
    volatile ButtonAction pendingAction_;

    // RTC sync
    bool rtcAvailable_;
    unsigned long lastNTPSync_;
    const unsigned long NTP_SYNC_INTERVAL = 3600000UL; // 1 ora

    // Timezone auto-detection
    int timezoneOffset_;        // Offset in secondi (es. 3600 per UTC+1)
    bool timezoneDSTActive_;    // true se ora legale attiva
    String timezoneName_;       // es. "Europe/Rome"
    bool timezoneDetected_;     // true se timezone rilevato almeno una volta

    // Callback statiche per OneButton
    static void handleClick();
    static void handleDoubleClick();
    static void handleLongPress();

public:
    RTCManager();

    // === METODI PRINCIPALI ===
    bool begin();
    void loop();

    // === RTC ===
    bool isRTCAvailable() { return rtcAvailable_; }
    DateTime now();
    void adjust(const DateTime& dt);
    bool autoDetectTimezone(); // Rileva timezone da IP (worldtimeapi.org)
    void syncWithNTP(const char* ntpServer = "pool.ntp.org");
    void forceSync(); // Forza sincronizzazione immediata (timezone + NTP)
    String getTimeString(const char* format = "%H:%M"); // es. "14:35"
    String getDateString(const char* format = "%d/%m/%Y"); // es. "01/10/2025"
    String getTimestamp(); // ISO8601: "2025-10-01T14:35:22"

    // Timezone info
    String getTimezoneName() { return timezoneName_; }
    int getTimezoneOffset() { return timezoneOffset_; }
    bool isDSTActive() { return timezoneDSTActive_; }

    // === PULSANTE ===
    bool isButtonAvailable() { return buttonAvailable_; }
    ButtonAction getButtonAction(); // Chiamare nel loop() - legge e resetta pendingAction_

    // === UTILITY ===
    void printStatus();
    uint32_t getUnixTime();
};

// Istanza globale
extern RTCManager rtcManager;

#endif // RTC_MANAGER_H
