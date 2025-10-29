// battery_config.h - VERSIONE COMPLETA CON AUTO-APPRENDIMENTO
#ifndef BATTERY_CONFIG_H
#define BATTERY_CONFIG_H

#include <Preferences.h>

class BatteryConfig {
private:
    Preferences prefs;
    const char* NAMESPACE = "battery_cfg";
    const char* CAPACITY_KEY = "capacity_ah";
    const char* CONSUMPTION_KEY = "consumption";
    const char* CONFIG_MODE_KEY = "config_mode";
    const char* TRIPS_COUNT_KEY = "trips_count";
    const char* LAST_CONSUMPTION_KEY = "last_consumption";
    const char* LAST_UPDATE_KEY = "last_update";
    
    const float DEFAULT_CAPACITY = 163.0f;
    const float DEFAULT_CONSUMPTION = 100.0f;

public:
    // === METODI BASE ===
    
    // Inizializza e carica configurazione
    void begin() {
        prefs.begin(NAMESPACE, false);
    }
    
    // === CONFIGURAZIONE BATTERIA ===
    
    // Salva capacita batteria
    void setCapacity(float capacityAh) {
        prefs.putFloat(CAPACITY_KEY, capacityAh);
    }
    
    // Carica capacita batteria (con fallback)
    float getCapacity() {
        float capacity = prefs.getFloat(CAPACITY_KEY, DEFAULT_CAPACITY);
        return capacity;
    }
    
    // === CONFIGURAZIONE CONSUMO ===
    
    // Salva consumo medio
    void setConsumption(float consumption) {
        prefs.putFloat(CONSUMPTION_KEY, consumption);
    }
    
    // Carica consumo medio
    float getConsumption() {
        return prefs.getFloat(CONSUMPTION_KEY, DEFAULT_CONSUMPTION);
    }
    
    // === MODALITA CONFIGURAZIONE ===
    
    // Imposta modalita: "wizard", "auto", "manual"
    void setConfigMode(String mode) {
        prefs.putString(CONFIG_MODE_KEY, mode);
    }
    
    // Ottieni modalita configurazione
    String getConfigMode() {
        return prefs.getString(CONFIG_MODE_KEY, "wizard");
    }
    
    // === STATISTICHE AUTO-APPRENDIMENTO ===
    
    // Numero di viaggi analizzati
    void setTripsCount(int count) {
        prefs.putInt(TRIPS_COUNT_KEY, count);
    }
    
    int getTripsCount() {
        return prefs.getInt(TRIPS_COUNT_KEY, 0);
    }
    
    // Ultimo consumo calcolato
    void setLastConsumption(float consumption) {
        prefs.putFloat(LAST_CONSUMPTION_KEY, consumption);
    }
    
    float getLastConsumption() {
        return prefs.getFloat(LAST_CONSUMPTION_KEY, 0.0f);
    }
    
    // Timestamp ultimo aggiornamento
    void setLastUpdate(unsigned long timestamp) {
        prefs.putULong(LAST_UPDATE_KEY, timestamp);
    }
    
    unsigned long getLastUpdate() {
        return prefs.getULong(LAST_UPDATE_KEY, 0);
    }
    
    // === METODI UTILITY ===
    
    // Reset a valori di default
    void resetToDefault() {
        prefs.putFloat(CAPACITY_KEY, DEFAULT_CAPACITY);
        prefs.putFloat(CONSUMPTION_KEY, DEFAULT_CONSUMPTION);
        prefs.putString(CONFIG_MODE_KEY, "wizard");
        prefs.putInt(TRIPS_COUNT_KEY, 0);
        prefs.putFloat(LAST_CONSUMPTION_KEY, 0.0f);
        prefs.putULong(LAST_UPDATE_KEY, 0);
    }

    // Reset solo auto-apprendimento
    void resetAutoLearning() {
        prefs.putInt(TRIPS_COUNT_KEY, 0);
        prefs.putFloat(LAST_CONSUMPTION_KEY, 0.0f);
        prefs.putULong(LAST_UPDATE_KEY, 0);
        prefs.putFloat(CONSUMPTION_KEY, DEFAULT_CONSUMPTION);
        prefs.putString(CONFIG_MODE_KEY, "wizard");
    }
    
    // Verifica se configurazione esiste
    bool hasConfiguration() {
        return prefs.isKey(CAPACITY_KEY);
    }
    
    // Verifica se auto-apprendimento e attivo
    bool isAutoLearningActive() {
        return getConfigMode() == "auto" && getTripsCount() >= 3;
    }
    
    void end() {
        prefs.end();
    }
};

extern BatteryConfig batteryConfig;

#endif