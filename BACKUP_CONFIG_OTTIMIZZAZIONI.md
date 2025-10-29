# BACKUP CONFIGURAZIONE - Prima delle Ottimizzazioni LVGL
**Data:** 2025-10-17
**Versione FW:** 1.1.7
**Scopo:** Documentare stato attuale prima delle modifiche per ottimizzazione memoria e performance

---

## 1. BACKUP lv_conf.h (CONFIGURAZIONE ATTUALE)

### Parametri Critici da Modificare:

```c
// ========== MEMORIA LVGL ==========
#define LV_MEM_SIZE (48U * 1024U)  // 48 KB - OK, come demo

// ========== HAL & TIMING ==========
#define LV_DISP_DEF_REFR_PERIOD 30      // ⚠️ DA MODIFICARE: 30ms -> 15ms
#define LV_INDEV_DEF_READ_PERIOD 30     // OK
#define LV_DPI_DEF 130                   // OK

// ========== BUFFER LAYER (TRASPARENZE) ==========
#define LV_LAYER_SIMPLE_BUF_SIZE          (24 * 1024)  // ⚠️ 24 KB - VALUTARE riduzione
#define LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE (3 * 1024)   // 3 KB

// ========== COLORE & TRASPARENZA ==========
#define LV_COLOR_DEPTH 16                // OK
#define LV_COLOR_16_SWAP 0               // OK (RGB parallelo)
#define LV_COLOR_SCREEN_TRANSP (__LVGL_V8_3 ? 1 : 0)  // Attiva trasparenza v8.3

// ========== FONT ATTIVI (CONSUMA ~180 KB RAM!) ==========
#define LV_FONT_MONTSERRAT_14 1  // ✅ Default
#define LV_FONT_MONTSERRAT_16 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_20 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_22 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_24 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_26 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_28 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_34 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_36 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_40 1  // ⚠️ Valutare se necessario
#define LV_FONT_MONTSERRAT_48 1  // ⚠️ Valutare se necessario

// ========== LAYOUT & FEATURES ==========
#define LV_USE_FLEX 1        // ✅ OK
#define LV_USE_GRID 0        // ⚠️ DA ATTIVARE: 0 -> 1 (demo lo usa)

// ========== CACHE & OTTIMIZZAZIONI ==========
#define LV_SHADOW_CACHE_SIZE 0           // OK (disabilitato)
#define LV_CIRCLE_CACHE_SIZE 4           // OK
#define LV_IMG_CACHE_DEF_SIZE 0          // OK (nessuna cache immagini)
#define LV_GRAD_CACHE_DEF_SIZE 0         // OK

// ========== DEBUG & MONITORING ==========
#define LV_USE_PERF_MONITOR 0    // ⚠️ SUGGERITO: attivare temporaneamente per test
#define LV_USE_MEM_MONITOR 0     // ⚠️ SUGGERITO: attivare temporaneamente per test
#define LV_USE_LOG 0             // OK (disabilitato in produzione)
```

---

## 2. BACKUP main.cpp - Buffer Display (CONFIGURAZIONE ATTUALE)

### Allocazione Buffer Display (linea ~430-460):

```cpp
// ========== BUFFER LVGL DISPLAY (ATTUALE) ==========
uint32_t buf_pixels = 800 * 40;  // ⚠️ 32,000 pixel = 64 KB

disp_draw_buf = (lv_color_t*) heap_caps_malloc(
    buf_pixels * sizeof(lv_color_t),
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA  // DMA per RGB parallelo
);

if (!disp_draw_buf) {
    // Fallback ridotto
    buf_pixels = 800 * 24;  // 24 linee = 38.4 KB
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(
        buf_pixels * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );
}

// ⚠️ NOTA: Demo usa (800*480)/4 = 96,000 pixel = 192 KB
```

### Driver Display (linea ~60-69):

```cpp
static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,              // DE, VSYNC, HSYNC, PCLK
    45, 48, 47, 21, 14,          // R0-R4
    5, 6, 7, 15, 16, 4,          // G0-G5
    8, 3, 46, 9, 1,              // B0-B4
    0, 8, 4, 8,                  // HSYNC: pol, front, pulse, back
    0, 8, 4, 8,                  // VSYNC: pol, front, pulse, back
    1, 14000000                  // pclk_neg, freq
);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbpanel);

// ⚠️ NOTA: Demo usa Arduino_RPi_DPI_RGBPanel invece
```

---

## 3. ANALISI REQUISITI DEMO LVGL

### Demo Benchmark (lv_demo_benchmark)
- **Buffer minimo:** 192 KB (screenWidth * screenHeight / 4)
- **LV_MEM_SIZE:** 48 KB
- **REFRESH_PERIOD:** 15 ms
- **Font richiesti:** Solo Montserrat 14
- **Features:** Tutti i widget abilitati, GRID attivo

### Demo Widgets (lv_demo_widgets)
- **Buffer minimo:** 192 KB
- **LV_MEM_SIZE:** 48 KB minimo, **raccomandato 64 KB** per slideshow
- **Font richiesti:** 14 + eventuali custom
- **Features:** Tutti i widget, calendar, chart, meter, ecc.

### Demo Music (più pesante)
- **Buffer minimo:** 256 KB (screenWidth * screenHeight / 3)
- **LV_MEM_SIZE:** 64-96 KB raccomandato
- **Features:** Animazioni complesse, molte immagini

---

## 4. STIMA CONSUMO RAM ATTUALE vs OTTIMIZZATO

### CONFIGURAZIONE ATTUALE:
```
Buffer Display LVGL:      64 KB    (32K pixel × 2 byte)
LVGL Heap (LV_MEM_SIZE):  48 KB
Layer Buffers:            24 KB    (trasparenze)
Font caricati (11):      ~180 KB   ⚠️ PROBLEMA PRINCIPALE
LVGL Strutture interne:  ~50 KB
Widget + UI custom:      ~80 KB
CAN + BMS + Logger:      ~30 KB
Stack + System:          ~40 KB
-------------------------------------------
TOTALE STIMATO:          ~516 KB  ⚠️ CRITICO per ESP32-S3
```

### DOPO OTTIMIZZAZIONE PROPOSTA:
```
Buffer Display LVGL:     192 KB    (+128 KB) ⚠️ MIGLIOR RENDERING
LVGL Heap:                48 KB    (invariato)
Layer Buffers:            12 KB    (-12 KB) ridotto 50%
Font ridotti (4-5):      ~70 KB    (-110 KB) ✅ RISPARMIO MAGGIORE
LVGL Strutture:          ~50 KB
Widget + UI:             ~80 KB
CAN + BMS:               ~30 KB
Stack:                   ~40 KB
-------------------------------------------
TOTALE STIMATO:          ~522 KB   (+6 KB ma rendering migliore)
```

**⚠️ ATTENZIONE:** ESP32-S3 ha ~512 KB RAM totale disponibile!

---

## 5. STRATEGIA OTTIMIZZAZIONE - APPROCCIO GRADUALE

### FASE 1 - OTTIMIZZAZIONI SICURE (FARE SUBITO):
1. ✅ **Disattivare font inutilizzati** (-110 KB)
2. ✅ **Attivare LV_USE_GRID** (migliora layout, +0 KB)
3. ✅ **Ridurre LV_DISP_DEF_REFR_PERIOD** a 15ms (migliora FPS)

### FASE 2 - TEST BUFFER DISPLAY (VALUTARE):
4. ⚠️ **Aumentare buffer a 60 linee** (800×60 = 96 KB, +32 KB)
   - Test rendering smooth
   - Se RAM sufficiente, provare 96K pixel come demo

### FASE 3 - OTTIMIZZAZIONI LAYER (OPZIONALE):
5. ⏸️ **Ridurre LV_LAYER_SIMPLE_BUF_SIZE** a 12 KB (se no trasparenze)

---

## 6. PIANO ROLLBACK

### Per ripristinare configurazione originale:

1. **Backup file originali:**
   ```
   lv_conf.h             -> lv_conf.h.backup_20251017
   src/main.cpp (sezione buffer) -> annotato in questo file
   ```

2. **Comandi rollback:**
   ```bash
   # Se problemi, ripristinare:
   cp lv_conf.h.backup_20251017 lv_conf.h
   # E modificare manualmente main.cpp linea ~430:
   # uint32_t buf_pixels = 800 * 40;  // Tornare a 40 linee
   ```

---

## 7. FONT EFFETTIVAMENTE USATI NEL PROGETTO

**DA VERIFICARE NEL CODICE:**
- Cercare `lv_font_montserrat_XX` in tutto il progetto
- Disattivare quelli non referenziati

**COMANDO DI RICERCA:**
```bash
grep -r "lv_font_montserrat" src/ include/ --include="*.cpp" --include="*.h"
```

---

## 8. MODIFICHE DA APPLICARE

### File: lv_conf.h

```diff
# Riga 84
-#define LV_DISP_DEF_REFR_PERIOD 30
+#define LV_DISP_DEF_REFR_PERIOD 15

# Riga 139
-#define LV_LAYER_SIMPLE_BUF_SIZE          (24 * 1024)
+#define LV_LAYER_SIMPLE_BUF_SIZE          (12 * 1024)

# Riga 595
-#define LV_USE_GRID 0
+#define LV_USE_GRID 1

# Font (disattivare inutilizzati dopo verifica):
# Riga 362-376 - SOLO DOPO GREP!
```

### File: src/main.cpp

```diff
# Linea ~430 (FASE 2 - SOLO SE RAM DISPONIBILE)
-uint32_t buf_pixels = 800 * 40;  // 32K pixel
+uint32_t buf_pixels = 800 * 60;  // 48K pixel = 96 KB

# Fallback:
-buf_pixels = 800 * 24;
+buf_pixels = 800 * 40;  // Fallback più generoso
```

---

## 9. CHECKLIST PRE-MODIFICA

- [ ] Backup completo progetto creato
- [ ] File BACKUP_CONFIG_OTTIMIZZAZIONI.md creato
- [ ] Verificati font usati con grep
- [ ] Letto e compreso piano rollback
- [ ] Test compilazione attuale OK
- [ ] RAM disponibile verificata (monitor seriale al boot)

---

## 10. TEST POST-MODIFICA

Dopo ogni modifica verificare:

1. **Compilazione:** `pio run` senza errori
2. **Upload:** Firmware caricato correttamente
3. **Boot:** Sistema avvia senza crash
4. **RAM:** Monitorare heap libero:
   ```cpp
   Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
   ```
5. **Display:** Rendering fluido, no artefatti
6. **FPS:** Se attivato PERF_MONITOR, verificare >30 FPS
7. **UI:** Tutti gli schermi funzionanti (Screen1, Screen2, Clock)

---

**NOTA FINALE:** Procedere con FASE 1 (font + grid + refresh), poi valutare FASE 2 solo se RAM sufficiente!
