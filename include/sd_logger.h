// sd_logger.h - Sistema di logging avanzato su SD card per debugging
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

// Livelli di log
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
};

class SDLogger {
public:
    // Inizializzazione (chiamare dopo SD.begin())
    bool begin();

    // Logging con livelli
    void debug(const char* message);
    void info(const char* message);
    void warning(const char* message);
    void error(const char* message);
    void critical(const char* message);

    // Logging formattato (stile printf)
    void logf(LogLevel level, const char* format, ...);

    // Logging specifici per categorie
    void logBoot(const char* resetReason, uint32_t freeHeap, const char* fwVersion);
    void logMemory(uint32_t freeHeap, uint32_t largestBlock, uint32_t minFreeHeap);
    void logCAN(const char* event, uint32_t canID, const uint8_t* data, uint8_t len);
    void logBMS(float soc, float voltage, float current, float temp, uint32_t errorFlags);
    void logScreen(const char* event, const char* details);
    void logButton(const char* action, const char* details);
    void logTrip(const char* event, float distance, float socStart, float socEnd);
    void logPerformance(uint32_t loopTime, uint32_t lvglTime, uint32_t stackWatermark);

    // Forza scrittura buffer su SD
    void flush();

    // Task periodico (chiamare nel loop)
    void task();

    // Utilità
    bool isReady() const { return _initialized; }
    void getLogFileName(char* buffer, size_t bufSize);

private:
    bool _initialized;
    char _logBuffer[4096];  // Buffer 4KB in RAM
    uint16_t _bufferPos;
    unsigned long _lastFlush;
    unsigned long _sessionStart;

    // Throttling per errori ripetuti
    unsigned long _lastErrorTime;
    uint32_t _errorCount;
    char _lastErrorMsg[128];
    unsigned long _lastWarningTime;
    uint32_t _warningCount;
    char _lastWarningMsg[128];

    void writeLog(LogLevel level, const char* category, const char* message);
    void writeToBuffer(const char* text);
    bool flushToSD();
    const char* getLevelName(LogLevel level);
    void getCurrentTimestamp(char* buffer, size_t bufSize);
};

// Istanza globale
extern SDLogger sdLogger;

// Macro per logging semplificato
#define LOG_D(msg) sdLogger.debug(msg)
#define LOG_I(msg) sdLogger.info(msg)
#define LOG_W(msg) sdLogger.warning(msg)
#define LOG_E(msg) sdLogger.error(msg)
#define LOG_C(msg) sdLogger.critical(msg)

#endif // SD_LOGGER_H
