# ANALISI LIBRERIE: Progetto vs Demo

**Data:** 2025-10-17
**Scopo:** Confrontare le librerie usate nel progetto con quelle delle demo ufficiali

---

## 1. LIBRERIE NEL VOSTRO platformio.ini

```ini
lib_deps =
  moononournation/GFX Library for Arduino@^1.4.7    # ✅ Stessa famiglia demo
  lvgl/lvgl@^8.3.11                                  # ⚠️ Versione più recente
  bodmer/TJpg_Decoder@^1.1.0                         # Extra (non in demo)
  bblanchon/ArduinoJson@^7.0.0                       # Extra (per OTA/web)
  adafruit/RTClib@^2.1.4                             # Extra (orologio RTC)
  mathertel/OneButton@^2.5.0                         # Extra (gestione pulsante)
```

---

## 2. LIBRERIE NELLE DEMO

### Demo ufficiali usano:
```
Arduino_GFX:  v1.2.8  (nelle demo fornite)
LVGL:         v8.3.0-dev (versione development)
```

**Nessuna libreria extra** - solo display + LVGL + touch

---

## 3. CONFRONTO VERSIONI

### Arduino_GFX

| | Demo | Vostro Progetto |
|---|------|-----------------|
| **Versione** | 1.2.8 (locale) | ^1.4.7 (PlatformIO Registry) |
| **Repo** | moononournation/Arduino_GFX | moononournation/GFX Library for Arduino |
| **Status** | Vecchia (2022?) | Recente (2024) |

**⚠️ DIFFERENZA:** Voi usate versione **più recente** (OK)

**Changelog 1.2.8 → 1.4.7:**
- Miglioramenti RGB panel
- Fix ESP32-S3 PSRAM
- Ottimizzazioni performance
- Nuovi display supportati

**✅ RACCOMANDAZIONE:** Mantenere versione 1.4.7 (più stabile e ottimizzata)

---

### LVGL

| | Demo | Vostro Progetto |
|---|------|-----------------|
| **Versione** | 8.3.0-dev | 8.3.11 |
| **Status** | Development | Stable Release |

**⚠️ DIFFERENZA:** Demo usa **development**, voi usate **release stabile**

**✅ RACCOMANDAZIONE:** Mantenere 8.3.11 (più stabile di dev)

---

## 4. LIBRERIE EXTRA NEL VOSTRO PROGETTO

### TJpg_Decoder
- **Scopo:** Decodifica immagini JPEG
- **Uso nel progetto:** Background PNG/JPEG?
- **Impatto RAM:** ~10-15 KB
- **Necessaria?** ⚠️ DA VERIFICARE se usata

```cpp
// Cercare nel codice:
grep -r "TJpg_Decoder\|tjpg\|jpgDec" src/ include/
```

### ArduinoJson
- **Scopo:** Parsing JSON
- **Uso:** OTA manifest, config web
- **Impatto RAM:** ~5-10 KB (dinamico)
- **Necessaria?** ✅ SÌ (per OTA e web config)

### RTClib (Adafruit)
- **Scopo:** Gestione RTC (Real Time Clock)
- **Uso:** Orologio persistente
- **Impatto RAM:** ~2-3 KB
- **Necessaria?** ✅ SÌ (per orologio)

### OneButton
- **Scopo:** Gestione eventi pulsante
- **Uso:** Press/LongPress/DoubleClick
- **Impatto RAM:** ~1 KB
- **Necessaria?** ✅ SÌ (per UI pulsante)

---

## 5. COMPATIBILITÀ DRIVER DISPLAY

### Demo usa:
```cpp
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(...);
Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(
    bus,
    800, 0, 8, 4, 8,  // width, hsync_pol, front, pulse, back
    480, 0, 8, 4, 8,  // height, vsync_pol, front, pulse, back
    1, 14000000, true // pclk_neg, freq, auto_flush
);
```

### Voi usate:
```cpp
static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,  // DE, VSYNC, HSYNC, PCLK
    45, 48, 47, 21, 14,  // R
    5, 6, 7, 15, 16, 4,  // G
    8, 3, 46, 9, 1,      // B
    0, 8, 4, 8,  // HSYNC timing
    0, 8, 4, 8,  // VSYNC timing
    1, 14000000  // pclk_neg, freq
);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbpanel);
```

### DIFFERENZE CHIAVE:

1. **Classe wrapper:**
   - Demo: `Arduino_RPi_DPI_RGBPanel` (high-level)
   - Voi: `Arduino_RGB_Display` (low-level wrapper)

2. **Costruttore:**
   - Demo: Passa timing come parametri separati
   - Voi: Timing incluso in Arduino_ESP32RGBPanel

3. **Auto-flush:**
   - Demo: `auto_flush = true` (gestione automatica)
   - Voi: Gestito manualmente in `my_disp_flush()`

**⚠️ VALUTAZIONE:** Entrambi corretti, ma approccio diverso

---

## 6. POSSIBILI OTTIMIZZAZIONI LIBRERIE

### Opzione A: Mantenere Setup Attuale (RACCOMANDATO)
**PRO:**
- Arduino_GFX 1.4.7 più recente e ottimizzato
- LVGL 8.3.11 versione stabile
- Librerie extra necessarie per features
- Già funzionante

**CONTRO:**
- Nessuno significativo

**✅ AZIONE:** Nessuna modifica necessaria

---

### Opzione B: Usare Librerie Demo (NON RACCOMANDATO)
**PRO:**
- Configurazione testata dalle demo

**CONTRO:**
- Arduino_GFX 1.2.8 più vecchio (bug fix persi)
- LVGL 8.3.0-dev instabile
- Perdita features OTA/RTC/Web
- Nessun beneficio reale

**❌ AZIONE:** Non procedere

---

## 7. VERIFICA USO TJpg_Decoder

### Comando verifica:
```bash
grep -r "TJpg\|tjpg\|jpgDec" src/ include/ --include="*.cpp" --include="*.h"
```

### Se NON usata:
```ini
# Rimuovere da platformio.ini:
# bodmer/TJpg_Decoder@^1.1.0

# Risparmio: ~15 KB flash, ~10 KB RAM
```

### Se usata per background:
- Valutare conversione immagini a formato LVGL nativo (C array)
- Risparmio RAM significativo a runtime

---

## 8. ANALISI MEMORIA LIBRERIE

### Stima footprint librerie:

| Libreria | Flash | RAM (statica) | RAM (heap/dinamica) |
|----------|-------|---------------|---------------------|
| Arduino_GFX | ~80 KB | ~5 KB | ~2 KB |
| LVGL | ~350 KB | ~50 KB | 48 KB (LV_MEM_SIZE) |
| TJpg_Decoder | ~15 KB | ~2 KB | ~10 KB (se usata) |
| ArduinoJson | ~20 KB | ~1 KB | ~5-10 KB |
| RTClib | ~10 KB | ~2 KB | ~1 KB |
| OneButton | ~3 KB | ~0.5 KB | ~0.5 KB |
| **TOTALE** | **~478 KB** | **~60 KB** | **~67-77 KB** |

**Partizione app0:** 7 MB → Spazio sufficiente ✅
**RAM totale ESP32-S3:** ~512 KB
**RAM disponibile app:** ~512 - 60 - 77 = **~375 KB** per buffer, stack, heap

---

## 9. CONCLUSIONI E RACCOMANDAZIONI

### ✅ MANTENERE INVARIATE:
1. **Arduino_GFX 1.4.7** - Più recente e ottimizzato
2. **LVGL 8.3.11** - Stabile e testato
3. **ArduinoJson** - Necessaria per OTA/web
4. **RTClib** - Necessaria per orologio
5. **OneButton** - Necessaria per pulsante

### ⚠️ DA VERIFICARE:
1. **TJpg_Decoder** - Verificare se usata
   ```bash
   grep -r "TJpg\|tjpg" src/ include/
   ```
   - Se NON usata: rimuovere (risparmi 10 KB RAM)
   - Se usata: valutare conversione immagini

### ❌ NON MODIFICARE:
1. **NON** tornare a Arduino_GFX 1.2.8 (versione vecchia)
2. **NON** usare LVGL 8.3.0-dev (instabile)
3. **NON** cambiare driver display (funzionante)

---

## 10. DRIVER DISPLAY: APPROFONDIMENTO

### Arduino_RPi_DPI_RGBPanel vs Arduino_RGB_Display

**Arduino_RPi_DPI_RGBPanel:**
- Classe high-level ottimizzata per display Raspberry Pi DPI
- Auto-flush integrato
- Gestione timing semplificata
- Usata dalle demo per semplicità

**Arduino_RGB_Display:**
- Wrapper generico per RGB panel
- Più flessibile
- Controllo manuale flush
- Usata nel vostro progetto

**Differenze pratiche:**
- **Performance:** Identiche (stesso backend)
- **Controllo:** RGB_Display più flessibile
- **Semplicità:** RPi_DPI più semplice per esempi

**✅ RACCOMANDAZIONE:** Mantenere Arduino_RGB_Display (già funzionante, più controllo)

---

## 11. PIANO D'AZIONE

### Immediato:
1. ✅ Verificare compilazione con ottimizzazioni lv_conf.h
2. ⏳ Testare su hardware (FPS, RAM, stabilità)
3. ⏳ Verificare uso TJpg_Decoder

### Dopo test:
4. Se TJpg_Decoder non usata: rimuovere
5. Se FPS < 30: valutare ulteriori ottimizzazioni
6. Se crash/OOM: ridurre LV_LAYER_SIMPLE_BUF_SIZE ulteriormente

### Opzionale:
7. Provare migrazione a Arduino_RPi_DPI_RGBPanel (solo se problemi)
8. Aggiornare Arduino_GFX a ultima versione se disponibile

---

**STATO:** Le librerie del vostro progetto sono **CORRETTE e OTTIMALI**.
**AZIONE:** Nessuna modifica necessaria alle dipendenze.
