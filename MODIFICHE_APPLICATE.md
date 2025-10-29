# MODIFICHE APPLICATE - Ottimizzazione LVGL

**Data:** 2025-10-17
**Versione FW:** 1.1.7
**Tipo:** Ottimizzazione performance e debug

---

## ✅ MODIFICHE APPLICATE AL FILE lv_conf.h

### 1. Refresh Display: 30ms → 15ms
**Riga:** 84
```diff
-#define LV_DISP_DEF_REFR_PERIOD 30      /*[ms]*/
+#define LV_DISP_DEF_REFR_PERIOD 15      /*[ms]*/ // OTTIMIZZATO: era 30ms (33 FPS) -> 15ms (67 FPS)
```

**Effetto:**
- FPS teorici: 33 → 67 FPS
- Rendering più fluido
- Animazioni più smooth
- CPU usage leggermente aumentato (accettabile)

**Rischio:** NULLO
**Beneficio:** ✅ ALTO (migliore esperienza utente)

---

### 2. Layer Buffer: 24KB → 16KB
**Riga:** 139
```diff
-#define LV_LAYER_SIMPLE_BUF_SIZE          (24 * 1024)
+#define LV_LAYER_SIMPLE_BUF_SIZE          (16 * 1024)  // OTTIMIZZATO: era 24KB -> 16KB (risparmio 8KB RAM)
```

**Effetto:**
- Risparmio RAM: 8 KB
- Buffer per widget con opacità ridotto
- Se non usate trasparenze pesanti: nessun impatto visibile

**Rischio:** BASSO (verificare rendering trasparenze)
**Beneficio:** ✅ MEDIO (più RAM disponibile)

---

### 3. Performance Monitor: Attivato
**Riga:** 274
```diff
-#define LV_USE_PERF_MONITOR 0
+#define LV_USE_PERF_MONITOR 1  // ATTIVATO per DEBUG - mostra FPS real-time
```

**Effetto:**
- Display FPS e CPU% in tempo reale
- Posizione: Angolo basso-destra
- Overlay verde su schermo
- **TEMPORANEO** per analisi performance

**Rischio:** NULLO (solo debug)
**Beneficio:** ✅ ALTO (diagnostica performance)

---

### 4. Memory Monitor: Attivato
**Riga:** 281
```diff
-#define LV_USE_MEM_MONITOR 0
+#define LV_USE_MEM_MONITOR 1  // ATTIVATO per DEBUG - mostra RAM LVGL usata
```

**Effetto:**
- Display RAM LVGL usata/libera
- Posizione: Angolo basso-sinistra
- Mostra frammentazione memoria
- **TEMPORANEO** per analisi memoria

**Rischio:** NULLO (solo debug)
**Beneficio:** ✅ ALTO (diagnostica memoria)

---

### 5. Layout Grid: Attivato
**Riga:** 595
```diff
-#define LV_USE_GRID 0 //1
+#define LV_USE_GRID 1  // ATTIVATO: usato dalle demo, migliora layout complessi
```

**Effetto:**
- Supporto CSS Grid layout
- Migliore gestione layout complessi
- Usato dalle demo LVGL ufficiali
- Overhead: ~1-2 KB RAM

**Rischio:** NULLO
**Beneficio:** ✅ MEDIO (futuro sviluppo UI)

---

## 📁 FILE BACKUP CREATI

1. **lv_conf.h.backup_20251017**
   - Backup configurazione originale
   - Ripristino: `cp lv_conf.h.backup_20251017 lv_conf.h`

2. **BACKUP_CONFIG_OTTIMIZZAZIONI.md**
   - Documentazione completa stato pre-modifica
   - Analisi RAM e strategia

3. **ANALISI_LIBRERIE.md**
   - Confronto librerie progetto vs demo
   - Conclusioni: librerie attuali OTTIME

4. **MODIFICHE_APPLICATE.md** (questo file)
   - Riepilogo modifiche applicate
   - Piano test e rollback

---

## 🎯 RISULTATI ATTESI

### Dopo il caricamento firmware:

#### Display Angolo Basso-Destra (PERF MONITOR):
```
FPS: XX
CPU: YY%
```

**Valori target:**
- FPS: 45-67 (ottimo), 30-45 (buono), <30 (problema)
- CPU: <50% (ottimo), 50-70% (buono), >70% (problema)

#### Display Angolo Basso-Sinistra (MEM MONITOR):
```
MEM: XXXX/48000
FRAG: YY%
```

**Valori target:**
- MEM: <40000 bytes (buono), <45000 (accettabile), >46000 (critico)
- FRAG: <20% (ottimo), 20-40% (buono), >40% (problema)

---

## 🧪 PIANO TEST SU HARDWARE

### Test 1: Boot e Stabilità
- [ ] Firmware carica senza errori
- [ ] Display si accende correttamente
- [ ] Splash screen visualizzato
- [ ] Nessun crash/reboot spontaneo

### Test 2: Performance Monitor
- [ ] FPS visibile in basso-destra
- [ ] Valore FPS stabile (no flicker numeri)
- [ ] FPS > 30 su schermata ferma
- [ ] FPS > 25 durante animazioni

### Test 3: Memory Monitor
- [ ] RAM usage visibile in basso-sinistra
- [ ] Memoria usata < 45 KB
- [ ] Frammentazione < 30%
- [ ] Nessun incremento continuo (leak)

### Test 4: Funzionalità UI
- [ ] Screen 1 (guida) rendering corretto
- [ ] Screen 2 (ricarica) rendering corretto
- [ ] Orologio visualizzato
- [ ] Transizioni screen fluide
- [ ] Icone errori BMS caricate
- [ ] Nessun artefatto grafico

### Test 5: Trasparenze/Layer
- [ ] Widget semi-trasparenti OK
- [ ] Overlay messaggi funzionanti
- [ ] Nessun glitch su cambio schermo

### Test 6: CAN + UI
- [ ] Dati CAN aggiornati correttamente
- [ ] UI responsive durante ricezione CAN
- [ ] Nessun lag/freeze
- [ ] Autonomia calcolata correttamente

---

## ⚠️ PROBLEMI POSSIBILI E SOLUZIONI

### Problema 1: FPS Bassi (<25)
**Sintomo:** Performance monitor mostra FPS < 25

**Cause possibili:**
1. Buffer display troppo piccolo (64 KB)
2. Troppi refresh simultanei
3. Rendering complesso

**Soluzione:**
```diff
# In main.cpp, linea ~430:
-uint32_t buf_pixels = 800 * 40;
+uint32_t buf_pixels = 800 * 50;  // Aumenta a 50 linee
```

**⚠️ ATTENZIONE:** Verificare RAM disponibile prima!

---

### Problema 2: RAM Critica (>46 KB usati)
**Sintomo:** Memory monitor mostra >46000/48000

**Cause possibili:**
1. Frammentazione memoria
2. Leak memoria
3. Widget troppo complessi

**Soluzione immediata:**
```diff
# In lv_conf.h:
-#define LV_LAYER_SIMPLE_BUF_SIZE (16 * 1024)
+#define LV_LAYER_SIMPLE_BUF_SIZE (12 * 1024)  // Riduce a 12 KB
```

---

### Problema 3: Glitch Grafici
**Sintomo:** Artefatti, elementi mal disegnati

**Causa:** Layer buffer insufficiente per trasparenze

**Soluzione:**
```diff
# ROLLBACK layer buffer:
-#define LV_LAYER_SIMPLE_BUF_SIZE (16 * 1024)
+#define LV_LAYER_SIMPLE_BUF_SIZE (24 * 1024)  // Torna a 24 KB
```

---

### Problema 4: Crash/Reboot
**Sintomo:** ESP32 si resetta improvvisamente

**Causa:** Out Of Memory (OOM)

**Soluzione ROLLBACK COMPLETO:**
```bash
cp lv_conf.h.backup_20251017 lv_conf.h
platformio run -t upload
```

---

## 📊 RACCOLTA DATI POST-TEST

### Da annotare dopo test:

**Valori Performance Monitor:**
```
FPS minimo osservato:    ___
FPS massimo osservato:   ___
FPS medio stabile:       ___
CPU% minimo:             ___
CPU% massimo:            ___
CPU% medio:              ___
```

**Valori Memory Monitor:**
```
RAM usata minima:        ___/48000
RAM usata massima:       ___/48000
RAM usata media:         ___/48000
Frammentazione min:      ___%
Frammentazione max:      ___%
```

**Heap ESP32 (da Serial Monitor):**
```cpp
// Aggiungere in setup() per debug:
Serial.printf("Free heap all'avvio: %d bytes\n", ESP.getFreeHeap());
Serial.printf("Largest free block: %d bytes\n", ESP.getMaxAllocHeap());
```

**Osservazioni qualitative:**
```
Fluidità UI (1-10):      ___
Presenza glitch:         SI/NO
Crash osservati:         SI/NO
Durata test stabile:     ___ minuti
```

---

## 🔄 PROSSIMI PASSI

### Dopo test positivi (FPS OK, RAM OK):
1. ✅ Mantenere modifiche
2. ⏸️ **Disattivare monitor** (release):
   ```c
   #define LV_USE_PERF_MONITOR 0
   #define LV_USE_MEM_MONITOR 0
   ```
3. Ricompilare firmware finale
4. Aggiornare versione (1.1.7 → 1.1.8?)

### Se problemi RAM ma FPS OK:
1. Ridurre ulteriormente layer buffer (12 KB)
2. Verificare leak memoria con valgrind-esp32
3. Ottimizzare allocazioni dinamiche

### Se problemi FPS ma RAM OK:
1. Provare buffer display più grande (50-60 linee)
2. Ridurre complessità UI (meno gradient, shadow)
3. Ottimizzare rendering custom

### Se crash/OOM:
1. **ROLLBACK immediato** a lv_conf.h.backup
2. Analizzare stack trace (se disponibile)
3. Ridurre consumi RAM:
   - Disattivare font inutilizzati (se trovati)
   - Ridurre layer buffer a 12 KB
   - Ottimizzare buffer display

---

## 📝 CHECKLIST FINALE

Prima di dichiarare ottimizzazione completata:

- [ ] Test su hardware eseguiti
- [ ] FPS >= 30 confermati
- [ ] RAM usage < 45 KB confermato
- [ ] Nessun crash per 30+ minuti uso
- [ ] Tutte le funzioni UI operative
- [ ] Monitor disattivati per release
- [ ] Firmware finale compilato
- [ ] Versione incrementata (se necessario)
- [ ] Documentazione aggiornata
- [ ] Commit git con modifiche

---

## 🔐 ROLLBACK RAPIDO

### Ripristino configurazione originale:

```bash
# Comando singolo:
cp lv_conf.h.backup_20251017 lv_conf.h && platformio run -t upload

# O manuale:
# 1. Copiare backup
cp lv_conf.h.backup_20251017 lv_conf.h

# 2. Ricompilare
platformio run

# 3. Caricare
platformio run -t upload
```

---

## 📚 RIFERIMENTI

- [BACKUP_CONFIG_OTTIMIZZAZIONI.md](BACKUP_CONFIG_OTTIMIZZAZIONI.md) - Stato pre-modifica completo
- [ANALISI_LIBRERIE.md](ANALISI_LIBRERIE.md) - Analisi dipendenze
- [lv_conf.h.backup_20251017](lv_conf.h.backup_20251017) - Backup file configurazione
- [LVGL Documentation](https://docs.lvgl.io/8.3/) - Riferimento LVGL 8.3
- [Arduino_GFX Wiki](https://github.com/moononournation/Arduino_GFX/wiki) - Riferimento display driver

---

**STATO:** ✅ Modifiche applicate, compilazione in corso
**PROSSIMO STEP:** Test su hardware + raccolta metriche
