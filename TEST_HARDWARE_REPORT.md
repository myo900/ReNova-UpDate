# TEST HARDWARE - Report Ottimizzazioni LVGL

**Data test:** 2025-10-17
**Versione FW:** 1.1.7 (con ottimizzazioni)
**Tester:** Utente

---

## 📊 VALORI MONITOR ON-SCREEN

### Performance Monitor (angolo basso-destra):
```
FPS osservati:
- Minimo:  _____
- Massimo: _____
- Medio:   _____

CPU%:
- Minimo:  _____
- Massimo: _____
- Medio:   _____
```

### Memory Monitor (angolo basso-sinistra):
```
RAM LVGL:
- Usata min:     _____/48000 bytes
- Usata max:     _____/48000 bytes
- Usata media:   _____/48000 bytes

Frammentazione:
- Min:  _____%
- Max:  _____%
- Media: _____%
```

---

## 🐛 GLITCH OSSERVATI

### Frequenza:
- [ ] Rari (1-2 volte in 10 minuti)
- [ ] Occasionali (ogni 2-3 minuti)
- [ ] Frequenti (ogni minuto)
- [ ] Costanti

### Tipo di Glitch:

#### 1. Artefatti Grafici
- [ ] Pixel sparsi/casuali
- [ ] Linee orizzontali/verticali
- [ ] Aree non aggiornate (stale)
- [ ] Colori sbagliati/invertiti
- [ ] Testo sfocato/distorto

#### 2. Problemi Trasparenze
- [ ] Overlay semi-trasparenti difettosi
- [ ] Messaggi con sfondo corrotto
- [ ] Icone con alone/artefatti
- [ ] Widget opachi invece di trasparenti

#### 3. Problemi Animazioni
- [ ] Transizioni schermo scattose
- [ ] Barre animate (power/regen) lag
- [ ] Cerchi range update non fluidi
- [ ] Orologio update glitch

#### 4. Problemi Rendering
- [ ] Widget disegnati parzialmente
- [ ] Font/testo corrotto
- [ ] Immagini/icone corrotte
- [ ] Background glitch

### Quando Accadono?
- [ ] All'avvio (boot)
- [ ] Cambio schermo (Screen1 ↔ Screen2)
- [ ] Update dati CAN frequenti
- [ ] Durante ricarica (Screen2)
- [ ] Durante guida (Screen1)
- [ ] Visualizzazione orologio
- [ ] Visualizzazione errori BMS
- [ ] Casuale/non prevedibile

### Schermo Più Affetto:
- [ ] Screen 1 (guida)
- [ ] Screen 2 (ricarica)
- [ ] Orologio
- [ ] Tutti ugualmente

---

## 🔍 CONFRONTO CON VERSIONE PRECEDENTE

### Glitch Migliorati?
- [ ] Sì, **MENO** glitch di prima ✅
- [ ] No, **STESSO** livello di prima
- [ ] No, **PEGGIO** di prima ⚠️

### Fluidità Generale:
- [ ] Molto migliorata ✅✅
- [ ] Leggermente migliorata ✅
- [ ] Invariata
- [ ] Peggiorata ⚠️

### FPS Percepiti:
- [ ] Molto più fluido
- [ ] Leggermente più fluido
- [ ] Uguale
- [ ] Meno fluido

---

## 📝 NOTE DETTAGLIATE

### Descrizione Glitch Specifici:
```
[Descrivere qui i glitch osservati in dettaglio]

Esempio:
- Screen1: Barra power a volte mostra linee orizzontali per 1 frame
- Screen2: Transizione da Screen1 lascia ghost del numero SOC vecchio
- Orologio: Cifre si sovrappongono 1-2 pixel durante update
```

### Screenshot/Foto Glitch:
```
[Se possibile, fotografare i glitch e annotare qui]
```

---

## 🧪 TEST SPECIFICI

### Test 1: Screen1 → Screen2 (10 transizioni)
```
Transizioni totali:     10
Transizioni pulite:     ___
Transizioni con glitch: ___
Tipo glitch:            _______________
```

### Test 2: Aggiornamento Rapido Dati CAN
```
Durata test:            5 minuti
Glitch osservati:       ___
FPS medio durante test: ___
RAM media durante test: ___/48000
```

### Test 3: Visualizzazione Errori BMS
```
Icone caricate:         SI / NO
Icone corrette:         SI / NO
Glitch durante caricamento: SI / NO
```

### Test 4: Stabilità Prolungata
```
Tempo test:             ___ minuti
Crash/reboot:           SI / NO
Memory leak (RAM cresce?): SI / NO
RAM inizio:             ___/48000
RAM fine:               ___/48000
```

---

## 🎯 POSSIBILI CAUSE GLITCH

### Causa 1: Layer Buffer Ridotto (24KB → 16KB)
**Probabilità:** ALTA se glitch su trasparenze/overlay

**Test:**
- Glitch aumentano con overlay/messaggi? SI / NO
- Glitch solo su elementi semi-trasparenti? SI / NO

**Fix da provare:**
```c
// In lv_conf.h linea 139:
#define LV_LAYER_SIMPLE_BUF_SIZE (20 * 1024)  // Provare 20 KB come compromesso
```

---

### Causa 2: Refresh Troppo Veloce (30ms → 15ms)
**Probabilità:** BASSA se FPS stabili

**Test:**
- CPU% > 70%? SI / NO
- FPS scendono durante glitch? SI / NO

**Fix da provare:**
```c
// In lv_conf.h linea 84:
#define LV_DISP_DEF_REFR_PERIOD 20  // Compromesso 20ms = 50 FPS
```

---

### Causa 3: Buffer Display Piccolo (64 KB)
**Probabilità:** MEDIA se glitch su rendering generale

**Test:**
- Glitch su grandi aree schermo? SI / NO
- Glitch durante scroll/pan? SI / NO

**Fix da provare:**
```c
// In main.cpp linea ~430:
uint32_t buf_pixels = 800 * 50;  // Aumenta a 50 linee (100 KB)
// SOLO SE RAM disponibile > 150 KB!
```

---

### Causa 4: Problemi Driver/Timing RGB
**Probabilità:** BASSA (stesso di prima)

**Test:**
- Glitch sono lampi/flash? SI / NO
- Glitch a bande/strisce verticali? SI / NO

**Fix:** Probabilmente non necessario (driver OK)

---

### Causa 5: Interferenza CAN/LVGL
**Probabilità:** MEDIA se glitch durante update CAN

**Test:**
- Glitch sincronizzati con update velocità/SOC? SI / NO
- Glitch solo quando auto in movimento? SI / NO

**Fix da provare:**
```cpp
// Verificare in main.cpp che update CAN non blocchino LVGL
// Eventualmente aggiungere lv_task_handler() più frequente
```

---

## 🔧 PIANO OTTIMIZZAZIONE INCREMENTALE

### STEP 1: Se glitch su trasparenze/overlay
```diff
# lv_conf.h linea 139:
-#define LV_LAYER_SIMPLE_BUF_SIZE (16 * 1024)
+#define LV_LAYER_SIMPLE_BUF_SIZE (20 * 1024)  // +4 KB
```
Ricompilare e testare. RAM usage: 32.6% → ~34%

---

### STEP 2: Se glitch su rendering generale
```diff
# main.cpp linea ~430:
-uint32_t buf_pixels = 800 * 40;
+uint32_t buf_pixels = 800 * 50;  // +20 KB
```
⚠️ PRIMA verificare RAM disponibile > 150 KB!
Ricompilare e testare. RAM usage: 32.6% → ~38%

---

### STEP 3: Se CPU% troppo alto (>70%)
```diff
# lv_conf.h linea 84:
-#define LV_DISP_DEF_REFR_PERIOD 15
+#define LV_DISP_DEF_REFR_PERIOD 20  // 50 FPS
```
Riduce carico CPU mantenendo fluidità.

---

### STEP 4: Se problemi persistono
**ROLLBACK completo:**
```bash
cp lv_conf.h.backup_20251017 lv_conf.h
platformio run -t upload
```

Poi valutare approccio diverso (es. semplificare UI).

---

## 📊 DECISIONE FINALE

### Dopo test prolungato (30+ minuti):

**SCENARIO A: Glitch accettabili, performance migliorate**
- [X] Mantenere modifiche attuali
- [ ] Disattivare monitor per release:
  ```c
  #define LV_USE_PERF_MONITOR 0
  #define LV_USE_MEM_MONITOR 0
  ```
- [ ] Incrementare versione → 1.1.8

---

**SCENARIO B: Glitch inaccettabili, serve fix**
- [ ] Applicare STEP 1 (layer buffer 20 KB)
- [ ] Test → Se OK, procedere a release
- [ ] Test → Se NO, applicare STEP 2
- [ ] Test → Se ancora NO, ROLLBACK

---

**SCENARIO C: Performance peggiorate**
- [ ] ROLLBACK immediato
- [ ] Analisi approfondita cause
- [ ] Approccio diverso (semplificare UI?)

---

## 🎯 METRICHE TARGET POST-OTTIMIZZAZIONE

### Minimi Accettabili:
```
FPS:              > 30 (accettabile), > 45 (ottimo)
CPU%:             < 70% (accettabile), < 50% (ottimo)
RAM LVGL:         < 45000/48000 bytes
Frammentazione:   < 40%
Glitch/10min:     < 3 (rari)
Stabilità:        > 30 min senza crash
```

### Se raggiunti → ✅ OTTIMIZZAZIONE RIUSCITA

---

## 📝 COMPILARE DURANTE TEST

**Avviare test:** Ora: _______
**Terminare test:** Ora: _______
**Durata totale:** _______ minuti

**CONCLUSIONE:**
```
[Scrivere qui il verdetto finale dopo i test]
```

---

**NEXT STEPS:**
- [ ] Compilare questo report durante test
- [ ] Fotografare monitor FPS/RAM se possibile
- [ ] Decidere se mantenere/modificare/rollback
- [ ] Se OK: disattivare monitor e release finale
