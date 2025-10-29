#pragma once
#include <Arduino.h>
#include <SD.h>
#include <functional>  // std::function

// Callback avanzamento (download/verify/install)
using OtaProgressFn = std::function<void(size_t bytesDone, size_t bytesTotal, const char* phaseMsg)>;

class OTAUpdater {
public:
    // Imposta callback (opzionale)
    void onProgress(OtaProgressFn cb) { progressCb = cb; }

    // Utilità
    static String toHex(const uint8_t* data, size_t len);
    static int    versionCmp(const String& a, const String& b); // semver semplice x.y.z

    // Manifest → decide se aggiornare e restituisce i campi utili
    bool checkForUpdate(const char* manifestUrl,
                        const char* currentVersion,
                        String& outVersion,
                        String& outBinUrl,
                        size_t& outSize,
                        String& outSha256);

    // Download su SD (es. "/fw/update.bin")
    bool downloadToSD(const String& binUrl, const char* sdPath, size_t expectedSize);

    // SHA-256 su file SD (pubbliche: usate anche dalla state-machine)
    bool computeSha256SD(const char* sdPath, uint8_t out[32]);
    bool verifySha256OnSD(const char* sdPath, const String& expectedHex);

    // Installazione OTA dal file su SD
    bool installFromSD(const char* sdPath);

private:
    OtaProgressFn progressCb = nullptr;
    void report(size_t done, size_t total, const char* phase);
};
