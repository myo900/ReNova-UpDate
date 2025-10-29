# SCRIPT DI ROLLBACK - Fix Glitch Display

**IMPORTANTE:** Questo script permette di tornare alla versione precedente in caso di problemi.

---

## ROLLBACK COMPLETO (Ritorno allo stato originale)

### Windows (PowerShell o CMD)
```powershell
# Vai nella cartella del progetto
cd "c:\Users\chiara\Documents\PlatformIO\Projects\ESP32-Twizy-Display-8-OTA-KM-RIC-t-c---Copia-main"

# Ripristina i file originali
copy /Y BACKUP_main.cpp src\main.cpp
copy /Y BACKUP_lv_conf.h lv_conf.h
copy /Y BACKUP_platformio.ini platformio.ini

# Pulisci e ricompila
pio run --target clean
pio run

# Upload (se compilazione OK)
pio run --target upload
```

### Linux/Mac (Bash)
```bash
# Vai nella cartella del progetto
cd ~/Documents/PlatformIO/Projects/ESP32-Twizy-Display-8-OTA-KM-RIC-t-c---Copia-main

# Ripristina i file originali
cp BACKUP_main.cpp src/main.cpp
cp BACKUP_lv_conf.h lv_conf.h
cp BACKUP_platformio.ini platformio.ini

# Pulisci e ricompila
pio run --target clean
pio run

# Upload (se compilazione OK)
pio run --target upload
```

---

## ROLLBACK SELETTIVO

### Fix #1: Rollback DMA Sync (main.cpp linee 326-330)
```cpp
// Ripristina versione originale
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}
```

### Fix #2: Rollback Buffer LVGL (main.cpp linee 828)
```cpp
// Ripristina buffer 40 righe
uint32_t buf_pixels = 800 * 40;
```

### Fix #3: Rollback PSRAM (platformio.ini linea 35)
```ini
# Ripristina workaround
-DCONFIG_SPIRAM_CACHE_WORKAROUND=1
```

### Fix #4: Rollback Throttling UI (main.cpp linea 549)
```cpp
// Ripristina throttling 50ms
if (now - lastUIUpdate < 50) return;
```

### Fix #5: Rollback Memoria LVGL (lv_conf.h linea 55)
```c
// Ripristina 48KB
#define LV_MEM_SIZE (48U * 1024U)
```

### Fix #6: Rollback Cache Config (main.cpp linea 189)
```cpp
// Rimuovere cache, ripristinare lettura diretta
const float CFG = batteryConfig.getConsumption();
```

---

## VERIFICA DOPO ROLLBACK

### 1. Verifica compilazione
```bash
pio run
```
Deve completare senza errori.

### 2. Verifica upload
```bash
pio run --target upload
```
NON interrompere durante flash.

### 3. Verifica funzionamento base
- Display si accende
- CAN riceve dati (velocità, SOC)
- Nessun crash

---

## RISOLUZIONE PROBLEMI ROLLBACK

### Problema: "File not found" durante copy
**Causa:** Backup non eseguito correttamente
**Soluzione:** Ripristinare da git (se disponibile) o richiedere backup originale

### Problema: Errori di compilazione dopo rollback
**Causa:** File .pio/build corrotti
**Soluzione:**
```bash
# Elimina cartella build completamente
rm -rf .pio/build   # Linux/Mac
rmdir /s .pio\build  # Windows

# Ricompila da zero
pio run --target clean
pio run
```

### Problema: ESP32 non si connette dopo upload
**Causa:** Bootloop o crash firmware
**Soluzione:**
```bash
# Entra in modalità download manualmente
# 1. Tieni premuto GPIO0 (BOOT)
# 2. Premi e rilascia RESET
# 3. Rilascia GPIO0
# 4. Riprova upload
pio run --target upload
```

### Problema: Display nero dopo rollback
**Causa:** Inizializzazione LVGL fallita
**Soluzione:**
1. Verifica alimentazione 5V stabile
2. Verifica presenza file background.jpg e splash.jpg su SD
3. Monitor seriale per vedere log errori:
```bash
pio device monitor
```

---

## CREAZIONE NUOVO BACKUP (Prima di ritentare fix)

```powershell
# Windows
copy src\main.cpp BACKUP_main_v2.cpp
copy lv_conf.h BACKUP_lv_conf_v2.h
copy platformio.ini BACKUP_platformio_v2.ini

# Linux/Mac
cp src/main.cpp BACKUP_main_v2.cpp
cp lv_conf.h BACKUP_lv_conf_v2.h
cp platformio.ini BACKUP_platformio_v2.ini
```

---

## CONTATTI SUPPORTO

**File di log:** Controllare SD:/system.log per diagnostica
**Monitor seriale:** `pio device monitor -b 115200`
**Forum:** [community.platformio.org](https://community.platformio.org)

---

**VERSIONE:** 1.0
**DATA:** 2025-10-28
**AUTORE:** Claude Code - Analisi Senior
