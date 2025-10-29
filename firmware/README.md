# Firmware Releases

Questa directory contiene i firmware binari compilati per l'ESP32-S3 Twizy Display.

## Files

- `firmware-X.Y.Z.bin` - Binary del firmware (versione X.Y.Z)
- `manifest.json` - Metadati completi del firmware (versione, SHA256, dimensione)
- `latest.json` - Punta sempre all'ultima versione disponibile (per OTA updates)

## OTA Update

Il dispositivo scarica automaticamente gli aggiornamenti da questo repository utilizzando il file `latest.json`.

### URL Raw (per OTA):
```
https://raw.githubusercontent.com/TUO_USERNAME/REPO_NAME/main/firmware/latest.json
```

## Pubblicazione Automatica

Il firmware viene pubblicato automaticamente dopo ogni build tramite lo script `tools/publish_fw.py`.
