#ifndef CANPARSER_H
#define CANPARSER_H

// AGGIUNTO: Define per web_config.h
#define CONSUMPTION_LEARNER_H
#include "bms.h"  // ← USA SOLO QUESTA DEFINIZIONE DI BMSData
#include <Arduino.h>
#include <SD.h>  // NUOVO: Per file CSV invece di Preferences

// Forward declaration per BatteryConfig
class BatteryConfig;
extern BatteryConfig batteryConfig;

// === RIMOSSA STRUCT BMSData DUPLICATA ===
// La struct BMSData è ora definita SOLO in bms.h

// ✅ AGGIORNATA: Struttura per singolo viaggio CON DURATA E RIGENERAZIONE
typedef struct {
    float startSOC;
    float endSOC;
    float distance;
    float consumption;
    float regenEnergy;          // ← NUOVO: Energia rigenerata totale (Wh)
    float regenPercent;         // ← NUOVO: % rigenerazione rispetto al consumo
    unsigned long timestamp;    // Unix timestamp in SECONDI (per data reale via RTC)
    unsigned long tripStartTime; // millis() all'inizio viaggio (per calcolare duration)
    unsigned long duration;      // Durata viaggio in millisecondi
    bool valid;
} TripData;

// Classe ConsumptionLearner - CON SISTEMA CSV AFFIDABILE + DURATA
class ConsumptionLearner {
private:
    TripData trips[10];
    int tripIndex;
    bool hasData;
    
    // Live consumption tracking
    float liveConsumption = 0.0f;
    bool liveConsumptionValid = false;
    unsigned long lastLiveUpdate = 0;
    bool usingLiveEstimate = false;

    // ✅ AGGIORNATO: File CSV con durata e rigenerazione
    const char* CSV_FILE = "/trips.csv";
    const char* CSV_HEADER = "timestamp,startSOC,endSOC,distance,consumption,regenEnergy,regenPercent,duration,valid\n";
    
    // === METODI PRIVATI - SOLO DICHIARAZIONI ===
    bool hasValidStartData();
    bool isValidTrip(float startSOC, float endSOC, float distance, float consumption);
    void saveTripsToCSV();
    void loadTripsFromCSV();
    void clearCSVFile();
    void appendTripToCSV(const TripData& trip);
    
public:
    // === COSTRUTTORE - SOLO DICHIARAZIONE ===
    ConsumptionLearner();

    // Live consumption methods
    void updateLiveConsumption(const BMSData& bms);
    float getLiveConsumption();
    bool hasValidLiveData();
    float getActiveConsumption();

    // === METODI PRINCIPALI - SOLO DICHIARAZIONI ===
    void begin();
    void update(const BMSData& bms);
    
    // === GESTIONE ABILITAZIONE - SOLO DICHIARAZIONI ===
    bool isEnabled();
    
    // === GESTIONE VIAGGI - SOLO DICHIARAZIONI ===
    void startTrip(float currentSOC, float currentDistance);
    void endTrip(float currentSOC, float currentDistance, float regenEnergy = 0.0f, float batteryCapacity = 163.0f);
    
    // === PERSISTENZA DATI - DICHIARAZIONI ===
    void saveTrips();        
    void loadTrips();        
    void clearSavedTrips();  
    
    // === STATISTICHE - SOLO DICHIARAZIONI ===
    float getAverageConsumption();
    int getValidTripsCount();
    int getTripsCount();

    // === ACCESSO DATI VIAGGI PER UI - SOLO DICHIARAZIONI ===
    const TripData* getTripData(int index);  // Ottieni viaggio per indice (0-9)
    float getAverageRegenPercent();          // % rigenerazione media
    float getAverageRegenEnergy();           // Wh rigenerati medi
    float getBestConsumption();              // Miglior consumo registrato
    
    // === METODI CSV PER WEB INTERFACE - SOLO DICHIARAZIONI ===
    bool hasCSVFile();
    String getCSVFilePath();
    String getCSVContent();
    String getStatsHTML();
    
    // === RESET - SOLO DICHIARAZIONE ===
    void reset();
};

// === ISTANZA GLOBALE ===
extern ConsumptionLearner consumptionLearner;

// === FUNZIONI PRINCIPALI ===
void parseCANFrame(uint32_t id, uint8_t len, uint8_t *data, BMSData &bms);

// === FUNZIONI AUTO-APPRENDIMENTO ===
void updateTripTracking(BMSData &bms);
void processSpeedChange(BMSData &bms, float previousSpeed);
void processSOCChange(BMSData &bms, float previousSOC);
void startNewTrip(BMSData &bms);
void endCurrentTrip(BMSData &bms);

// === FUNZIONI GLOBALI PER WEB_CONFIG.H ===
void resetAutoLearning();     // Reset completo auto-apprendimento
void enableAutoLearning();    // Abilita auto-apprendimento

// === FUNZIONI ODOMETRO (SD BASED) ===
bool odo_set_vehicle_km(uint32_t new_km);  // Imposta km veicolo (protetto password)
uint32_t odo_get_current_km();             // Ottieni km attuali

// === FUNZIONI SISTEMA ERRORI ===
void updateErrorDisplay(uint32_t errorFlags);
const struct ErrorDefinition* getHighestPriorityError(uint32_t errorFlags);
int getActiveErrorCount(uint32_t errorFlags);
bool hasCriticalErrors(uint32_t errorFlags);
const struct ErrorDefinition* getErrorByIndex(uint32_t errorFlags, int index);

// === FUNZIONI HELPER ERRORI ===
bool loadErrorIcon(const char* iconFile);
bool icon_output_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

#endif // CANPARSER_H