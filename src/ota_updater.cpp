#include "ota_updater.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include "mbedtls/sha256.h"

// -----------------------------------------------------------------------------
// TLS helpers (configurabili via macro):
//  - Definisci OTA_USE_INSECURE_TLS per forzare setInsecure()
//  - Oppure definisci OTA_ROOT_CA_PEM con la root CA in formato PEM (const char*)
//  - Oppure definisci OTA_TLS_FINGERPRINT con il fingerprint SHA1 del server
// Se non definisci nulla, cade su setInsecure() per retrocompatibilità.
// -----------------------------------------------------------------------------
static inline void initSecureClient(WiFiClientSecure& client) {
#if defined(OTA_USE_INSECURE_TLS)
    client.setInsecure();
#elif defined(OTA_ROOT_CA_PEM)
    client.setCACert(OTA_ROOT_CA_PEM);
#elif defined(OTA_TLS_FINGERPRINT)
    client.setFingerprint(OTA_TLS_FINGERPRINT);
#else
    client.setInsecure();
#endif
}

// -----------------------------------------------------------------------------
// Utility locali
// -----------------------------------------------------------------------------
static bool ensureDirRecursive(const String& fullPath) {
    // Crea le cartelle intermedie su SD: /a/b/c/file.bin -> crea /a, /a/b, /a/b/c
    int idx = 1; // salta lo slash iniziale
    while (true) {
        int next = fullPath.indexOf('/', idx);
        if (next < 0) break;
        if (next > 0) {
            String dir = fullPath.substring(0, next);
            if (!SD.exists(dir)) {
                if (!SD.mkdir(dir)) return false;
            }
        }
        idx = next + 1;
    }
    return true;
}

// -----------------------------------------------------------------------------
// OTAUpdater methods
// -----------------------------------------------------------------------------
void OTAUpdater::report(size_t done, size_t total, const char* phase) {
    if (progressCb) progressCb(done, total, phase);
}

int OTAUpdater::versionCmp(const String& a, const String& b) {
    int ax=0, ay=0, az=0, bx=0, by=0, bz=0;
    sscanf(a.c_str(), "%d.%d.%d", &ax, &ay, &az);
    sscanf(b.c_str(), "%d.%d.%d", &bx, &by, &bz);
    if (ax != bx) return (ax > bx) ? 1 : -1;
    if (ay != by) return (ay > by) ? 1 : -1;
    if (az != bz) return (az > bz) ? 1 : -1;
    return 0;
}

String OTAUpdater::toHex(const uint8_t* data, size_t len) {
    static const char* HEX_CHARS = "0123456789ABCDEF";
    String s; s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s += HEX_CHARS[(data[i] >> 4) & 0x0F];
        s += HEX_CHARS[data[i] & 0x0F];
    }
    return s;
}


bool OTAUpdater::checkForUpdate(const char* manifestUrl,
                                const char* currentVersion,
                                String& outVersion,
                                String& outBinUrl,
                                size_t& outSize,
                                String& outSha256)
{
    WiFiClientSecure client;
    initSecureClient(client);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(F("TwizyDash-OTA/1.0"));
    http.setTimeout(15000); // 15s

    if (!http.begin(client, manifestUrl)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }

    JsonDocument doc; // ArduinoJson v7 (dinamico)
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return false;

    String manifestVer = doc["version"] | "";
    String url         = doc["url"]     | "";
    size_t size        = doc["size"]    | 0;
    String sha256      = doc["sha256"]  | "";

    if (manifestVer.isEmpty() || url.isEmpty() || size == 0 || sha256.isEmpty())
        return false;

    if (versionCmp(manifestVer, String(currentVersion)) <= 0) {
        // già aggiornato (uguale o più nuovo)
        return false;
    }

    outVersion = manifestVer;
    outBinUrl  = url;
    outSize    = size;
    outSha256  = sha256;
    return true;
}

bool OTAUpdater::downloadToSD(const String& binUrl, const char* sdPath, size_t expectedSize) {
    // Prepara directory
    String p = String(sdPath);
    if (!ensureDirRecursive(p)) return false;

    WiFiClientSecure client;
    initSecureClient(client);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(F("TwizyDash-OTA/1.0"));
    http.setTimeout(60000); // 60s - file grandi richiedono più tempo

    if (!http.begin(client, binUrl)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int len = http.getSize(); // può essere -1 (chunked)
    File f = SD.open(sdPath, FILE_WRITE);
    if (!f) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    const size_t BUFSZ = 4096;
    uint8_t buf[BUFSZ];
    size_t written = 0;

    // Usa expectedSize se Content-Length assente
    size_t totalExpected = (len > 0) ? (size_t)len : expectedSize;

    // Throttling progress
    size_t lastReport = 0;
    unsigned long lastTime = 0;
    size_t minDelta = totalExpected ? totalExpected / 50 : 8192; // ogni 2% o min 8KB
    if (minDelta < 4096) minDelta = 4096;

    while (http.connected()) {
        size_t avail = stream->available();
        if (avail) {
            size_t toRead = (avail > BUFSZ) ? BUFSZ : avail;
            int r = stream->readBytes((char*)buf, toRead);
            if (r <= 0) break;
            int w = f.write(buf, r);
            if (w != r) {
                f.close(); http.end();
                return false;
            }
            written += r;

            unsigned long now = millis();
            if (totalExpected == 0) {
                // senza totale: report con throttling temporale
                if ((now - lastTime) > 250) {
                    report(written, totalExpected, "download");
                    lastTime = now;
                }
            } else {
                if (written >= totalExpected || (written - lastReport) >= minDelta || (now - lastTime) > 250) {
                    report(written, totalExpected, "download");
                    lastReport = written;
                    lastTime = now;
                }
            }
        } else {
            delay(1);
        }
    }
    f.close();
    http.end();

    // Verifica dimensione finale se nota
    if (expectedSize > 0 && written != expectedSize) {
        return false;
    }

    return (written > 0);
}

bool OTAUpdater::computeSha256SD(const char* sdPath, uint8_t outHash[32]) {
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return false;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) { // 0 = SHA-256
        mbedtls_sha256_free(&ctx);
        f.close();
        return false;
    }

    const size_t BUFSZ = 4096;
    uint8_t buf[BUFSZ];
    size_t totalRead = 0;
    size_t fileSize = f.size();

    // Throttling progress
    size_t lastReport = 0;
    unsigned long lastTime = 0;
    size_t minDelta = fileSize ? fileSize / 50 : 8192; // 2% o 8KB
    if (minDelta < 4096) minDelta = 4096;

    while (true) {
        int r = f.read(buf, BUFSZ);
        if (r < 0) { f.close(); mbedtls_sha256_free(&ctx); return false; }
        if (r == 0) break;
        if (mbedtls_sha256_update_ret(&ctx, buf, r) != 0) {
            f.close(); mbedtls_sha256_free(&ctx); return false;
        }

        totalRead += r;

        unsigned long now = millis();
        if (fileSize == 0) {
            if ((now - lastTime) > 250) {
                report(totalRead, fileSize, "verify");
                lastTime = now;
            }
        } else {
            if (totalRead >= fileSize || (totalRead - lastReport) >= minDelta || (now - lastTime) > 250) {
                report(totalRead, fileSize, "verify");
                lastReport = totalRead;
                lastTime = now;
            }
        }
    }

    bool ok = (mbedtls_sha256_finish_ret(&ctx, outHash) == 0);
    mbedtls_sha256_free(&ctx);
    f.close();

    // report finale verify
    report(fileSize, fileSize, "verify");
    return ok;
}

bool OTAUpdater::verifySha256OnSD(const char* sdPath, const String& expectedHex) { // <-- expectedHex
    uint8_t hash[32];
    if (!computeSha256SD(sdPath, hash)) return false;
    String hex = toHex(hash, 32);
    String A = hex;          A.toUpperCase();
    String B = expectedHex;  B.toUpperCase();
    return (A == B);
}

bool OTAUpdater::installFromSD(const char* sdPath) {
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return false;

    size_t size = f.size();
    if (size == 0) { f.close(); return false; }

    if (!Update.begin(size)) { f.close(); return false; }

    // Progress OTA con throttling
    Update.onProgress([this, size](size_t written, size_t /*unused*/) {
        static size_t lastReport = 0;
        static unsigned long lastTime = 0;

        unsigned long now = millis();
        size_t minDelta = size / 50; // 2%
        if (minDelta < 4096) minDelta = 4096;

        if (written >= size || (written - lastReport) >= minDelta || (now - lastTime) > 200) {
            report(written, size, "installing");
            lastReport = written;
            lastTime = now;
        }
    });

    const size_t BUFSZ = 4096;
    uint8_t buf[BUFSZ];
    size_t written = 0;

    while (written < size) {
        int r = f.read(buf, BUFSZ);
        if (r <= 0) break;
        if (Update.write(buf, r) != (size_t)r) {
            f.close();
            Update.abort();
            return false;
        }
        written += r;
    }
    f.close();

    if (!Update.end(true)) {
        return false;
    }

    // Report finale install
    report(size, size, "installing");
    return true; // reboot richiesto a valle
}
