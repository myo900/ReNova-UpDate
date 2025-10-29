// sd_logger.cpp - Implementazione sistema logging su SD
#include "sd_logger.h"
#include <SD.h>
#include <sys/time.h>
#include <esp_system.h>

// Istanza globale
SDLogger sdLogger;

bool SDLogger::begin() {
    _initialized = false;
    _bufferPos = 0;
    _lastFlush = millis();
    _sessionStart = millis();

    // Inizializza throttling
    _lastErrorTime = 0;
    _errorCount = 0;
    memset(_lastErrorMsg, 0, sizeof(_lastErrorMsg));
    _lastWarningTime = 0;
    _warningCount = 0;
    memset(_lastWarningMsg, 0, sizeof(_lastWarningMsg));

    // Verifica che SD sia già inizializzata
    if (!SD.begin()) {
        return false;
    }

    // Crea cartella logs se non esiste
    if (!SD.exists("/logs")) {
        if (!SD.mkdir("/logs")) {
            return false;
        }
    }

    // Scrivi header iniziale nel buffer
    char header[256];
    snprintf(header, sizeof(header),
        "\n========================================\n"
        "SESSION START: %lu ms\n"
        "ESP32 Reset Reason: %d\n"
        "Free Heap: %u bytes\n"
        "========================================\n",
        millis(),
        esp_reset_reason(),
        ESP.getFreeHeap()
    );
    writeToBuffer(header);

    _initialized = true;

    return true;
}

void SDLogger::getLogFileName(char* buffer, size_t bufSize) {
    // Calcola quale giorno di test siamo (basato su uptime)
    uint32_t uptimeHours = millis() / 3600000;
    uint32_t day = (uptimeHours / 24) % 2; // Rotazione ogni 2 giorni (48h)

    snprintf(buffer, bufSize, "/logs/system_day%lu.log", day);
}

const char* SDLogger::getLevelName(LogLevel level) {
    switch (level) {
        case LOG_DEBUG:    return "DEBUG";
        case LOG_INFO:     return "INFO";
        case LOG_WARNING:  return "WARN";
        case LOG_ERROR:    return "ERROR";
        case LOG_CRITICAL: return "CRIT";
        default:           return "UNKN";
    }
}

void SDLogger::getCurrentTimestamp(char* buffer, size_t bufSize) {
    uint32_t uptime = millis();
    uint32_t hours = uptime / 3600000;
    uint32_t minutes = (uptime % 3600000) / 60000;
    uint32_t seconds = (uptime % 60000) / 1000;
    uint32_t ms = uptime % 1000;

    snprintf(buffer, bufSize, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, ms);
}

void SDLogger::writeLog(LogLevel level, const char* category, const char* message) {
    if (!_initialized) return;

    char timestamp[32];
    getCurrentTimestamp(timestamp, sizeof(timestamp));

    char logLine[256];
    snprintf(logLine, sizeof(logLine), "[%s][%s][%s] %s\n",
             timestamp, getLevelName(level), category, message);

    writeToBuffer(logLine);
}

void SDLogger::writeToBuffer(const char* text) {
    if (!_initialized) return;

    uint16_t len = strlen(text);

    // Se il buffer è quasi pieno, forza flush
    if (_bufferPos + len >= sizeof(_logBuffer) - 1) {
        flushToSD();
    }

    // Aggiungi al buffer
    if (_bufferPos + len < sizeof(_logBuffer)) {
        strcpy(_logBuffer + _bufferPos, text);
        _bufferPos += len;
    }
}

bool SDLogger::flushToSD() {
    if (!_initialized || _bufferPos == 0) return true;

    char logFile[64];
    getLogFileName(logFile, sizeof(logFile));

    File file = SD.open(logFile, FILE_APPEND);
    if (!file) {
        return false;
    }

    // Scrivi buffer su SD
    size_t written = file.write((uint8_t*)_logBuffer, _bufferPos);
    file.close();

    // Reset buffer
    _bufferPos = 0;
    memset(_logBuffer, 0, sizeof(_logBuffer));
    _lastFlush = millis();

    return true;
}

void SDLogger::flush() {
    flushToSD();
}

void SDLogger::task() {
    if (!_initialized) return;

    // Flush automatico ogni 10 secondi
    if (millis() - _lastFlush > 10000) {
        flushToSD();
    }
}

// === API PUBBLICHE ===

void SDLogger::debug(const char* message) {
    writeLog(LOG_DEBUG, "SYSTEM", message);
}

void SDLogger::info(const char* message) {
    writeLog(LOG_INFO, "SYSTEM", message);
}

void SDLogger::warning(const char* message) {
    if (!_initialized) return;

    // Throttling: max 1 warning al secondo
    unsigned long now = millis();

    // Se è lo stesso messaggio e siamo dentro 1 secondo, conta e salta
    if (now - _lastWarningTime < 1000 && strcmp(_lastWarningMsg, message) == 0) {
        _warningCount++;
        return;
    }

    // Se abbiamo warning accumulati dello stesso tipo, loggali
    if (_warningCount > 0 && strcmp(_lastWarningMsg, message) == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s (ripetuto %lu volte in %lus)",
                 message, _warningCount + 1, (now - _lastWarningTime) / 1000);
        writeLog(LOG_WARNING, "SYSTEM", msg);
        _warningCount = 0;
    } else {
        // Nuovo messaggio o dopo throttling
        writeLog(LOG_WARNING, "SYSTEM", message);
    }

    // Aggiorna stato throttling
    strncpy(_lastWarningMsg, message, sizeof(_lastWarningMsg) - 1);
    _lastWarningMsg[sizeof(_lastWarningMsg) - 1] = '\0';
    _lastWarningTime = now;
}

void SDLogger::error(const char* message) {
    if (!_initialized) return;

    // Throttling: max 1 errore al secondo
    unsigned long now = millis();

    // Se è lo stesso messaggio e siamo dentro 1 secondo, conta e salta
    if (now - _lastErrorTime < 1000 && strcmp(_lastErrorMsg, message) == 0) {
        _errorCount++;
        return;
    }

    // Se abbiamo errori accumulati dello stesso tipo, loggali
    if (_errorCount > 0 && strcmp(_lastErrorMsg, message) == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s (ripetuto %lu volte in %lus)",
                 message, _errorCount + 1, (now - _lastErrorTime) / 1000);
        writeLog(LOG_ERROR, "SYSTEM", msg);
        _errorCount = 0;
    } else {
        // Nuovo messaggio o dopo throttling
        writeLog(LOG_ERROR, "SYSTEM", message);
    }

    // Aggiorna stato throttling
    strncpy(_lastErrorMsg, message, sizeof(_lastErrorMsg) - 1);
    _lastErrorMsg[sizeof(_lastErrorMsg) - 1] = '\0';
    _lastErrorTime = now;
}

void SDLogger::critical(const char* message) {
    writeLog(LOG_CRITICAL, "SYSTEM", message);
}

void SDLogger::logf(LogLevel level, const char* format, ...) {
    if (!_initialized) return;

    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    writeLog(level, "SYSTEM", message);
}

void SDLogger::logBoot(const char* resetReason, uint32_t freeHeap, const char* fwVersion) {
    char msg[256];
    snprintf(msg, sizeof(msg), "BOOT - Reason: %s, Heap: %u, FW: %s",
             resetReason, freeHeap, fwVersion);
    writeLog(LOG_CRITICAL, "BOOT", msg);
}

void SDLogger::logMemory(uint32_t freeHeap, uint32_t largestBlock, uint32_t minFreeHeap) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Free: %u, Largest: %u, Min: %u",
             freeHeap, largestBlock, minFreeHeap);
    writeLog(LOG_DEBUG, "MEMORY", msg);
}

void SDLogger::logCAN(const char* event, uint32_t canID, const uint8_t* data, uint8_t len) {
    char msg[256];
    char dataStr[64] = "";

    if (data && len > 0) {
        char temp[8];
        for (uint8_t i = 0; i < len && i < 8; i++) {
            snprintf(temp, sizeof(temp), "%02X ", data[i]);
            strcat(dataStr, temp);
        }
    }

    snprintf(msg, sizeof(msg), "%s - ID: 0x%03X, Data: %s", event, canID, dataStr);
    writeLog(LOG_INFO, "CAN", msg);
}

void SDLogger::logBMS(float soc, float voltage, float current, float temp, uint32_t errorFlags) {
    char msg[256];
    snprintf(msg, sizeof(msg), "SOC: %.1f%%, V: %.2fV, I: %.2fA, T: %.1fC, Err: 0x%04X",
             soc, voltage, current, temp, errorFlags);

    LogLevel level = (errorFlags != 0) ? LOG_WARNING : LOG_DEBUG;
    writeLog(level, "BMS", msg);
}

void SDLogger::logScreen(const char* event, const char* details) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s - %s", event, details);
    writeLog(LOG_INFO, "DISPLAY", msg);
}

void SDLogger::logButton(const char* action, const char* details) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s - %s", action, details);
    writeLog(LOG_INFO, "BUTTON", msg);
}

void SDLogger::logTrip(const char* event, float distance, float socStart, float socEnd) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s - Dist: %.2fkm, SOC: %.1f%% -> %.1f%%",
             event, distance, socStart, socEnd);
    writeLog(LOG_INFO, "TRIP", msg);
}

void SDLogger::logPerformance(uint32_t loopTime, uint32_t lvglTime, uint32_t stackWatermark) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Loop: %lums, LVGL: %lums, Stack: %lu bytes",
             loopTime, lvglTime, stackWatermark);
    writeLog(LOG_DEBUG, "PERF", msg);
}
