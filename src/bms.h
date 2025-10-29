#ifndef BMS_H
#define BMS_H

#include <stdint.h>

struct BMSData {
    float current = 0;
    float voltage = 0;
    float soc = 0;
    float socAh = 0;
    float setAh = 163;
    float cellTemps[7] = {-40};
    float avgTemp = -40;
    uint8_t gear = 0;
    uint16_t error = 0;
    uint32_t errorFlags = 0;        // NUOVO: Flag errori estesi per sistema icone
    bool errorIconLoaded = false;   // NUOVO: Stato caricamento icona
    float speed = 0;
    float displaySpeed = 0;
    uint32_t odometer = 0;
    unsigned long lastTempUpdate = 0;
    
    // Campi ricarica
    bool isCharging = false;
    bool chargeReady = false;
    uint8_t chargeStage = 0;
    uint8_t chargeProtocol = 0;
    float chargeCurrent = 0;
    float chargeTimeRemaining = 0;
    unsigned long chargeStartTime = 0;
    float chargeStartSOC = 0;
    
    // Campi energia e distanza
    float usedEnergy = 0;
    float drivenDistance = 0;
    
    // Campi auto-apprendimento
    bool tripInProgress = false;
    float tripStartSOC = 0;
    float tripStartDistance = 0;
    unsigned long tripStartTime = 0;
    float lastValidSOC = 0;
    bool wasMoving = false;

    // ✅ NUOVO: Tracciamento rigenerazione durante il viaggio
    float tripRegenEnergy = 0;      // Energia rigenerata nel viaggio corrente (Wh)
    unsigned long lastRegenUpdate = 0; // Timestamp ultimo aggiornamento regen
};

extern BMSData bms;

#endif