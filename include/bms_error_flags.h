#ifndef BMS_ERROR_FLAGS_H
#define BMS_ERROR_FLAGS_H

#include <stdint.h>

// === BITMASK ERRORI BMS (frame CAN 0x0081) ===
#define TWIZY_SERV_TEMP       0x000001  // Temperatura alta
#define TWIZY_SERV_STOP       0x000002  // Richiesta arresto
#define TWIZY_SERV_FAULT      0x000004  // Guasto generico
#define TWIZY_LOW_12V         0x000008  // Batteria 12V bassa
#define TWIZY_CAN_ERROR       0x000010  // Errore comunicazione CAN
#define TWIZY_CELL_DIFF       0x000020  // Celle sbilanciate
#define TWIZY_VOLTAGE_HIGH    0x000040  // Tensione alta
#define TWIZY_VOLTAGE_LOW     0x000080  // Tensione bassa
#define TWIZY_OVERCURRENT     0x000100  // Corrente eccessiva
#define TWIZY_TEMP_SENSOR_ERR 0x000200  // Sensore temperatura guasto
#define TWIZY_EEPROM_ERROR    0x000400  // EEPROM guasta
#define TWIZY_MOSFET_FAULT    0x000800  // Guasto MOSFET
#define TWIZY_CHARGER_FAULT   0x001000  // Caricatore guasto
#define TWIZY_CONFIG_FAULT    0x002000  // Configurazione invalida
#define TWIZY_TIMEOUT         0x004000  // Timeout comunicazione
#define TWIZY_FW_INCOMPLETE   0x008000  // Firmware incompleto
#define TWIZY_OVERHEAT_WARN   0x010000  // Avviso surriscaldamento
#define TWIZY_SERVICE_REQ     0x020000  // Richiesta servizio
#define TWIZY_CHARGE_FLAP_FAULT     0x040000  // DF137 - Charging Flap
#define TWIZY_VOLTAGE_LOW_WAKEUP    0x080000  // DF174 - Undervoltage at Wake Up
#define TWIZY_VOLTAGE_LOW_DRIVING   0x100000  // DF176 - Undervoltage While Driving

// === PRIORITÀ ERRORI ===
enum ErrorPriority {
    PRIORITY_CRITICAL = 0,  // Rosso - Arresto immediato
    PRIORITY_HIGH = 1,      // Arancione - Attenzione alta
    PRIORITY_MEDIUM = 2,    // Giallo - Monitoraggio
    PRIORITY_INFO = 3       // Bianco - Informativo
};

// === STRUTTURA DEFINIZIONE ERRORE ===
struct ErrorDefinition {
    uint32_t flag;              // Bitmask errore
    ErrorPriority priority;     // Livello priorità
    const char* iconFile;       // Nome file icona
    const char* textScreen1;    // Testo per Screen 1 (max 22 char)
    const char* textScreen2;    // Testo per Screen 2 (max 18 char)
    uint32_t color;            // Colore HEX per il testo
};

// === TABELLA ERRORI ORDINATA PER PRIORITÀ ===
// Palette automotive: colori morbidi non affaticanti per uso notturno/diurno
static const ErrorDefinition ERROR_DEFINITIONS[] = {
    // === CRITICI (Rosso morbido 0xCC3333 - meno aggressivo) ===
    {TWIZY_SERV_STOP,       PRIORITY_CRITICAL, "serv_stop.jpg",      "ARRESTO RICHIESTO",   "ARRESTO!",          0xCC3333},
    {TWIZY_SERV_FAULT,      PRIORITY_CRITICAL, "serv_fault.jpg",     "GUASTO SISTEMA",      "GUASTO SISTEMA",    0xCC3333},
    {TWIZY_OVERHEAT_WARN,   PRIORITY_CRITICAL, "overheat_warn.jpg",  "SURRISCALDAMENTO",    "SURRISCALDAMENTO",  0xCC3333},

    // === ALTI (Arancio automotive 0xE65C00 - warm orange) ===
    {TWIZY_SERV_TEMP,       PRIORITY_HIGH,     "temp_high.jpg",      "TEMP ALTA",           "TEMP ALTA",         0xE65C00},
    {TWIZY_VOLTAGE_HIGH,    PRIORITY_HIGH,     "volt_high.jpg",      "TENSIONE ALTA",       "TENSIONE ALTA",     0xE65C00},
    {TWIZY_VOLTAGE_LOW,     PRIORITY_HIGH,     "volt_low.jpg",       "TENSIONE BASSA",      "TENSIONE BASSA",    0xE65C00},
    {TWIZY_OVERCURRENT,     PRIORITY_HIGH,     "overcurrent.jpg",    "CORRENTE ALTA",       "CORRENTE ALTA",     0xE65C00},
    {TWIZY_VOLTAGE_LOW_DRIVING, PRIORITY_HIGH, "volt_driving.jpg",   "VOLT BASSA GUIDA",    "VOLT BASSA GUIDA",  0xE65C00},

    // === MEDI (Ambra automotive 0xFFBF00 - amber, meno stridulo) ===
    {TWIZY_CELL_DIFF,       PRIORITY_MEDIUM,   "cell_diff.jpg",      "CELLE SBILANCIATE",   "CELLE SBILANCIATE", 0xFFBF00},
    {TWIZY_LOW_12V,         PRIORITY_MEDIUM,   "low_12v.jpg",        "12V BASSA",           "12V BASSA",         0xFFBF00},
    {TWIZY_CAN_ERROR,       PRIORITY_MEDIUM,   "can_error.jpg",      "ERRORE CAN",          "ERRORE CAN",        0xFFBF00},
    {TWIZY_TEMP_SENSOR_ERR, PRIORITY_MEDIUM,   "temp_sensor.jpg",    "SENSORE TEMP",        "SENSORE TEMP",      0xFFBF00},
    {TWIZY_MOSFET_FAULT,    PRIORITY_MEDIUM,   "mosfet_fault.jpg",   "MOSFET GUASTO",       "MOSFET GUASTO",     0xFFBF00},
    {TWIZY_CHARGER_FAULT,   PRIORITY_MEDIUM,   "charger_fault.jpg",  "CARICATORE KO",       "CARICATORE KO",     0xFFBF00},
    {TWIZY_CHARGE_FLAP_FAULT, PRIORITY_MEDIUM, "charge_flap.jpg",    "SPORTELLO RICARICA",  "SPORTELLO RIC",     0xFFBF00},
    {TWIZY_VOLTAGE_LOW_WAKEUP, PRIORITY_MEDIUM, "volt_wakeup.jpg",   "VOLT BASSA AVVIO",    "VOLT BASSA AVVIO",  0xFFBF00},

    // === INFORMATIVI (Blu chiaro 0x4A90E2 - meno abbagliante del bianco) ===
    {TWIZY_EEPROM_ERROR,    PRIORITY_INFO,     "eeprom_err.jpg",     "EEPROM GUASTA",       "EEPROM GUASTA",     0x4A90E2},
    {TWIZY_CONFIG_FAULT,    PRIORITY_INFO,     "config_fault.jpg",   "CONFIG ERRATA",       "CONFIG ERRATA",     0x4A90E2},
    {TWIZY_TIMEOUT,         PRIORITY_INFO,     "timeout.jpg",        "TIMEOUT COMM",        "TIMEOUT COMM",      0x4A90E2},
    {TWIZY_FW_INCOMPLETE,   PRIORITY_INFO,     "fw_incomplete.jpg",  "FW INCOMPLETO",       "FW INCOMPLETO",     0x4A90E2},
    {TWIZY_SERVICE_REQ,     PRIORITY_INFO,     "service_req.jpg",    "SERVIZIO RICHIESTO",  "SERVIZIO REQ",      0x4A90E2}
};

#define ERROR_COUNT (sizeof(ERROR_DEFINITIONS) / sizeof(ErrorDefinition))

// === DICHIARAZIONI FUNZIONI (implementazioni in canparser.cpp) ===

// Trova l'errore con priorità più alta attivo
const ErrorDefinition* getHighestPriorityError(uint32_t errorFlags);

// Ottieni il numero di errori attivi
int getActiveErrorCount(uint32_t errorFlags);

// Verifica se ci sono errori critici
bool hasCriticalErrors(uint32_t errorFlags);

// Ottieni errore per indice (per rotazione)
const ErrorDefinition* getErrorByIndex(uint32_t errorFlags, int index);

#endif // BMS_ERROR_FLAGS_H