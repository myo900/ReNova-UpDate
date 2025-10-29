# TEST E VERIFICA - Fix Glitch Display

**Data implementazione:** 2025-10-28
**Versione firmware:** 1.2.8 (da 1.2.7)
**Fix applicati:** 7/7

---

## CHECKLIST PRE-COMPILAZIONE

- [ ] Backup file originali completato (BACKUP_*.*)
- [ ] File FIX_GLITCH_DISPLAY.md letto e compreso
- [ ] File ROLLBACK_SCRIPT.md disponibile per emergenze
- [ ] SD card inserita con background.jpg e splash.jpg
- [ ] Cavo USB connesso stabilmente

---

## FASE 1: COMPILAZIONE

### Comando compilazione
```bash
cd "c:\Users\chiara\Documents\PlatformIO\Projects\ESP32-Twizy-Display-8-OTA-KM-RIC-t-c---Copia-main"
pio run
```

### Verifica output compilazione
**Atteso:**
```
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [====      ]  XX.X% (used XXXXX bytes from 327680 bytes)
Flash: [=====     ]  XX.X% (used XXXXXX bytes from XXXXXXX bytes)
Building .pio\build\esp32s3box\firmware.bin
SUCCESS
```

**Errori comuni:**
- `fatal error: sdLogger.h`: File mancante → verifica include/
- `undefined reference to getCachedConsumption`: Controllare ordine funzioni
- `region 'iram0_0_seg' overflowed`: IRAM saturo → rimuovere flag ottimizzazioni

### Log compilazione - Annotare qui:
```
[Incolla output compilazione qui]
```

---

## FASE 2: UPLOAD FIRMWARE

### Comando upload
```bash
pio run --target upload
```

**ATTENZIONE:**
- NON disconnettere USB durante upload
- NON spegnere ESP32 durante flash
- Upload richiede 30-60 secondi

### Verifica upload
**Output atteso:**
```
Writing at 0x000XXXXX... (XX %)
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
SUCCESS
```

### Se upload fallisce:
1. Premi BOOT + RESET su ESP32
2. Rilascia RESET, tieni BOOT 2 secondi
3. Rilascia BOOT
4. Riprova: `pio run --target upload`

---

## FASE 3: MONITOR SERIALE

### Avvia monitor
```bash
pio device monitor -b 115200
```

### Log boot atteso:
```
[BOOT] Reset reason: POWER_ON
[INFO] Sistema inizializzato con successo
[INFO] Background caricato da SD in PSRAM (768KB)
[INFO] Buffer LVGL allocato: 100 righe (156 KB)  ← VERIFICA QUI!
[INFO] RTC disponibile
[CAN] Waiting for bus...
```

### Verifica critiche:
- ✅ Buffer LVGL deve essere ≥60 righe (ideale 100)
- ✅ Background caricato senza errori
- ✅ Nessun "CRITICO" o "ERROR" nei primi 10 secondi
- ✅ CAN inizia a ricevere dati entro 60 secondi

### Log boot reale - Annotare qui:
```
[Incolla primi 20 righe monitor qui]
```

---

## FASE 4: TEST VISUALI (Display Acceso)

### Test 4.1: Display Base
- [ ] Display si accende entro 5 secondi
- [ ] Splash screen visibile e nitido
- [ ] Nessun flickering durante splash
- [ ] Transizione a Screen1 smooth

### Test 4.2: Glitch Risolti
- [ ] **Nessun pixel corrotto** durante aggiornamento SOC/velocità
- [ ] **Nessun tearing** durante scroll archi (SOC/autonomia)
- [ ] **Nessun flickering** durante cambio schermata (Screen1→Screen3→Screen4)
- [ ] **Background stabile** - nessun glitch su sfondo
- [ ] **Transizioni errori smooth** - rotazione ogni 5s senza scatti

### Test 4.3: Performance Percepita
- [ ] Animazioni fluide (archi, barre)
- [ ] Nessun freeze durante aggiornamento autonomia
- [ ] Risposta immediata pulsante RTC (<100ms)
- [ ] Scroll tra schermate istantaneo

### Test 4.4: Confronto Prima/Dopo
**PRIMA (v1.2.7):**
- Flickering: 5-10 volte/minuto
- Tearing: Visibile durante animazioni
- Freeze: 2-3 micro-freeze/minuto

**DOPO (v1.2.8):**
- Flickering: [Annotare qui: ____ volte/minuto]
- Tearing: [Annotare: Presente SI/NO]
- Freeze: [Annotare: ____ volte/minuto]

---

## FASE 5: TEST FUNZIONALI

### Test 5.1: CAN Bus
- [ ] Velocità aggiornata (frame 0x019F)
- [ ] SOC aggiornato (frame 0x0155)
- [ ] Autonomia calcolata correttamente
- [ ] Potenza Drive/Regen visualizzata
- [ ] Temperatura celle mostrata

### Test 5.2: Navigazione Schermate
- [ ] Screen1 (dashboard) OK
- [ ] Screen2 (ricarica) OK - solo durante charging
- [ ] Screen3 (statistiche) OK - grafico viaggi
- [ ] Screen4 (JPG custom) OK - carica immagine SD

### Test 5.3: Funzioni Speciali
- [ ] Web config accessibile (AP mode)
- [ ] OTA update funzionante
- [ ] SD logging attivo (file system.log cresce)
- [ ] RTC mantiene ora corretta
- [ ] Odometro incrementa durante movimento

---

## FASE 6: TEST STRESS (30 minuti)

### Setup test stress
1. Avvia veicolo (o simula con test bench)
2. Lascia display acceso 30 minuti
3. Esegui ciclo: Velocità 0→50→0 km/h × 10 volte
4. Forza cambio schermate ogni 30 secondi

### Metriche da monitorare (monitor seriale)
```bash
# Ogni 60 secondi, verificare nel log:
[MEM] Free: XXXXX bytes  ← deve rimanere >100KB
[PERF] Loop: XX ms       ← deve rimanere <20ms
[BMS] SOC: XX.X%         ← deve seguire simulazione
```

### Risultati test stress:
- Heap minimo raggiunto: [_____ KB]
- Loop time massimo: [_____ ms]
- Watchdog reset: [SI/NO]
- Crash/reboot: [SI/NO]
- Glitch visuali rilevati: [_____]

---

## FASE 7: TEST COMPARATIVO FPS

### Con monitor LVGL attivo (lv_conf.h:274)
**Verifica nell'angolo display:**
```
CPU: XX%
FPS: XX
```

### Metriche attese POST-FIX:
- **FPS:** 50-65 (era 30-40)
- **CPU Load:** 40-50% (era 70-85%)
- **RAM Libera:** >120KB (era ~80KB)

### Metriche reali misurate:
- FPS medio: [_____]
- FPS minimo: [_____]
- CPU Load medio: [_____]%
- RAM libera minima: [_____ KB]

---

## FASE 8: TEST MEMORIA DETTAGLIATO

### Comando analisi memoria
```bash
pio run --target size
```

### Output atteso:
```
.dram0.data:    XXXXX B  ← deve essere <200KB
.dram0.bss:     XXXXX B  ← deve essere <150KB
.iram0.text:    XXXXX B  ← codice in IRAM
.flash.text:    XXXXXX B ← codice in Flash
```

### Verifica allocazioni critiche (monitor seriale):
```
Buffer LVGL: 156 KB (100 righe)  ← Fix #2
Pool LVGL:   80 KB               ← Fix #5
Background:  768 KB (PSRAM)      ← Fix #3
```

---

## FASE 9: VERIFICA ROLLBACK (Opzionale)

### Se hai riscontrato problemi critici:
1. Segui ROLLBACK_SCRIPT.md
2. Ripristina versione 1.2.7
3. Verifica che sistema torni funzionante
4. Contatta supporto con log

---

## RISULTATO FINALE

### ✅ TEST SUPERATO SE:
- [x] Compilazione OK
- [x] Upload OK
- [x] Boot senza errori critici
- [x] Glitch visuali ridotti >90%
- [x] FPS migliorato >30%
- [x] Nessun crash in 30 minuti
- [x] Tutte le funzioni operative

### ⚠️ TEST PARZIALE SE:
- [x] Alcuni glitch ancora presenti (ma ridotti)
- [x] FPS migliorato <30%
- [x] Buffer LVGL allocato <80 righe

### ❌ TEST FALLITO SE:
- [ ] Crash/reboot entro 5 minuti
- [ ] Display nero o corrotto
- [ ] FPS peggiore rispetto a prima
- [ ] Glitch aumentati

---

## NOTE E OSSERVAZIONI

### Comportamenti anomali rilevati:
```
[Annotare qui eventuali comportamenti strani]
```

### Differenze rispetto a v1.2.7:
```
[Annotare qui le differenze percepite]
```

### Suggerimenti per ulteriori miglioramenti:
```
[Annotare qui idee per ottimizzazioni future]
```

---

## SUPPORTO

**Log completi:** Monitor seriale + SD:/system.log
**Screenshot:** Usa telefono per fotografare glitch se presenti
**Contatto:** Repository Issues o documentazione progetto

---

**VERSIONE TEST:** 1.0
**DATA:** 2025-10-28
**TESTER:** ________________
**ESITO:** [ ] SUPERATO  [ ] PARZIALE  [ ] FALLITO
