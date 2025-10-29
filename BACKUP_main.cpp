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
#define FW_VERSION "1.2.7"
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
