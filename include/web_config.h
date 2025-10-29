// web_config.h - VERSIONE COMPLETA con OTA non-bloccante (task FreeRTOS) e UI coerente
#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "battery_config.h"
#include "canparser.h"

// ==== OTA ====
#include "ota_updater.h"
#include "ota_screen.h"
#include "sd_logger.h"

// === Pagina OTA unificata (ELIMINATA - ora usiamo solo /ota principale) ===

// ===== Auto-learning (CSV) opzionale =====
#ifdef CONSUMPTION_LEARNER_H
extern void resetAutoLearning();
extern void enableAutoLearning();
extern ConsumptionLearner consumptionLearner;
#define HAS_AUTO_LEARNING true
#else
#define HAS_AUTO_LEARNING false
#endif

// Dato condiviso dal resto del progetto
extern BMSData bms;

// ===== Versione firmware corrente (usata per confronto con manifest) =====
#ifndef FW_VERSION
#define FW_VERSION "1.2.9"   // Rimossi tutti i Serial.print per ottimizzazione memoria
#endif
//***************************************************************************** */
class WebConfig {
private:
    WebServer  server;
    DNSServer  dnsServer;
    Preferences prefs;

    // AP locale (per setup + portal)
    const char* AP_SSID = "Twizy-Config";
    const char* AP_PASS = "twizy123";

    bool configMode     = false;
    bool serverRunning  = false;

    // Credenziali WiFi STA (facoltative)
    String wifiSSID;
    String wifiPassword;

    // wizard (form-data) â†’ calcolo guidato di capacitÃ /consumo
void handleWizardConfig() {
    // Richiede i tre campi base
    if (!(server.hasArg("battery") && server.hasArg("style") && server.hasArg("terrain"))) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Dati mancanti\"}");
        return;
    }

    float capacity = 163.0f;    // default
    float consumption = 100.0f; // default

    // Batteria: valore numerico o "custom" + custom_battery
    String batteryType = server.arg("battery");
    if (batteryType == "custom") {
        if (server.hasArg("custom_battery")) {
            capacity = server.arg("custom_battery").toFloat();
        }
    } else {
        capacity = batteryType.toFloat(); // es. "70", "163", "200"
    }

    // Stile/terreno â†’ consumo stimato
    float baseConsumption = server.arg("style").toFloat();   // es. 80,100,130
    float terrainFactor   = server.arg("terrain").toFloat(); // es. 0.9,1.0,1.1
    if (terrainFactor <= 0.0f) terrainFactor = 1.0f;
    consumption = baseConsumption / terrainFactor;

    // Validazione e applicazione
    if (capacity >= 50.0f && capacity <= 500.0f &&
        consumption >= 40.0f && consumption <= 250.0f) {

        batteryConfig.setCapacity(capacity);
        batteryConfig.setConsumption(consumption);
        batteryConfig.setConfigMode("wizard");
        bms.setAh = capacity;

        // range stimato (stessa formula usata in UI)
        long rangeKm = lround((capacity * 57.6f) / consumption * 0.95f);

        String result = "{\"success\":true";
        result += ",\"capacity\":" + String(capacity, 1);
        result += ",\"consumption\":" + String(consumption, 1);
        result += ",\"estimatedRange\":" + String(rangeKm);
        result += "}";
        server.send(200, "application/json", result);
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Valori fuori range\"}");
    }
}


    // ==================== UTIL ====================
    void loadWiFiConfig() {
        prefs.begin("wifi_cfg", false);
        wifiSSID     = prefs.getString("ssid", "");
        wifiPassword = prefs.getString("password", "");
        prefs.end();
    }
    void saveWiFiConfig(const String& ssid, const String& password) {
        prefs.begin("wifi_cfg", false);
        prefs.putString("ssid", ssid);
        prefs.putString("password", password);
        prefs.end();
        wifiSSID     = ssid;
        wifiPassword = password;
    }
    // --- NET DIAG ---
    void handleNetStatus();  // GET /api/net  (definita in web_config.cpp)
    String getWiFiPage();    // definita in web_config.cpp

    // ==================== API BASE ====================
    void handleGetConfig() {
        float capacity    = batteryConfig.getCapacity();
        float consumption = batteryConfig.getConsumption();
        String mode       = batteryConfig.getConfigMode();

        int trips = 0;
        #if HAS_AUTO_LEARNING
        trips = consumptionLearner.getTripsCount();
        #endif

        String json = "{";
        json += "\"capacity\":" + String(capacity, 1) + ",";
        json += "\"consumption\":" + String(consumption, 1) + ",";
        json += "\"mode\":\"" + mode + "\",";
        json += "\"trips\":" + String(trips) + ",";
        json += "\"hasAutoLearning\":" + String(HAS_AUTO_LEARNING ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleSetConfig() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Dati mancanti\"}");
            return;
        }
        const String body = server.arg("plain");
        // parsing minimale (capacity, consumption)
        float capacity    = NAN;
        float consumption = NAN;

        int capStart = body.indexOf("\"capacity\":");
        if (capStart >= 0) {
            capStart += 11;
            int capEnd = body.indexOf(",", capStart);
            if (capEnd < 0) capEnd = body.indexOf("}", capStart);
            capacity = body.substring(capStart, capEnd).toFloat();
        }
        int consStart = body.indexOf("\"consumption\":");
        if (consStart >= 0) {
            consStart += 14;
            int consEnd = body.indexOf(",", consStart);
            if (consEnd < 0) consEnd = body.indexOf("}", consStart);
            consumption = body.substring(consStart, consEnd).toFloat();
        }

        if (!isnan(capacity) && !isnan(consumption) &&
            capacity >= 50.0f && capacity <= 500.0f &&
            consumption >= 40.0f && consumption <= 250.0f) {
            batteryConfig.setCapacity(capacity);
            batteryConfig.setConsumption(consumption);
            batteryConfig.setConfigMode("manual");
            bms.setAh = capacity;
            server.send(200, "application/json", "{\"success\":true}");
        } else {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Valori non validi\"}");
        }
    }

    void handleGetStatus() {
        String json = "{";
        json += "\"soc\":"      + String(bms.soc, 1) + ",";
        json += "\"speed\":"    + String(bms.speed, 1) + ",";
        json += "\"current\":"  + String(bms.current, 1) + ",";
        json += "\"voltage\":"  + String(bms.voltage, 1) + ",";
        json += "\"temp\":"     + String(bms.avgTemp, 0) + ",";
        json += "\"charging\":" + String(bms.isCharging ? "true" : "false") + ",";
        json += "\"driving\":"  + String(bms.speed > 1 ? "true" : "false") + ",";
        json += "\"tripInProgress\":" + String(bms.tripInProgress ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleReset() {
        batteryConfig.resetToDefault();
        bms.setAh = 163.0f;
        server.send(200, "application/json", "{\"success\":true}");
    }

    // =========== AUTO-LEARNING ===========
    void handleEnableAuto() {
        #if HAS_AUTO_LEARNING
        enableAutoLearning();
        server.send(200, "application/json", "{\"success\":true}");
        #else
        server.send(501, "application/json", "{\"success\":false,\"error\":\"Auto-apprendimento non disponibile\"}");
        #endif
    }
    void handleResetAuto() {
        #if HAS_AUTO_LEARNING
        resetAutoLearning();
        server.send(200, "application/json", "{\"success\":true}");
        #else
        server.send(501, "application/json", "{\"success\":false,\"error\":\"Auto-apprendimento non disponibile\"}");
        #endif
    }

    // ===== CSV =====
    void handleDownloadCSV() {
        #if HAS_AUTO_LEARNING
        if (!consumptionLearner.hasCSVFile()) {
            server.send(404, "text/plain", "File CSV non trovato");
            return;
        }
        String csvContent = consumptionLearner.getCSVContent();
        server.sendHeader("Content-Disposition", "attachment; filename=trips.csv");
        server.send(200, "text/csv", csvContent);
        #else
        server.send(501, "text/plain", "Auto-apprendimento non disponibile");
        #endif
    }
    void handleViewCSV() {
        #if HAS_AUTO_LEARNING
        if (!consumptionLearner.hasCSVFile()) {
            String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CSV</title>
<style>body{font-family:Arial;margin:20px;background:#f5f5f5}.container{max-width:600px;margin:0 auto;background:#fff;padding:20px;border-radius:8px}</style>
</head><body><div class="container"><h1>File trips.csv</h1><p>Nessun file CSV trovato. Inizia a guidare per generare dati!</p><a href="/">Torna alla Dashboard</a></div></body></html>
)rawliteral";
            server.send(404, "text/html", html);
            return;
        }
        String csvContent = consumptionLearner.getCSVContent();
        String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Visualizza CSV</title>
<style>
body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5;}
.container{max-width:900px;margin:0 auto;background:white;padding:20px;border-radius:8px;}
.csv-content{background:#f8f9fa;padding:15px;border-radius:5px;font-family:monospace;white-space:pre-wrap;font-size:11px;max-height:400px;overflow-y:auto;border:1px solid #ddd;}
button{padding:10px 20px;margin:10px 10px 0 0;border:none;border-radius:5px;cursor:pointer;}
.btn-primary{background:#007bff;color:#fff}.btn-success{background:#28a745;color:#fff}.btn-secondary{background:#6c757d;color:#fff}
</style></head><body><div class="container">
<h1>File trips.csv</h1>
<div class="csv-content)">rawliteral" + csvContent + R"rawliteral(</div>
<a href="/api/download_csv" download="trips.csv"><button class="btn-success">Scarica File CSV</button></a>
<button class="btn-secondary" onclick="location.href='/stats'">Statistiche</button>
<button class="btn-primary" onclick="location.href='/'">Dashboard</button>
</div></body></html>
)rawliteral";
        server.send(200, "text/html", html);
        #else
        server.send(501, "text/html", "<h1>Auto-apprendimento non disponibile</h1>");
        #endif
    }
    void handleCSVInfo() {
        #if HAS_AUTO_LEARNING
        String json = "{";
        json += "\"hasFile\":" + String(consumptionLearner.hasCSVFile() ? "true" : "false") + ",";
        json += "\"filePath\":\"" + consumptionLearner.getCSVFilePath() + "\",";
        json += "\"validTrips\":" + String(consumptionLearner.getValidTripsCount()) + ",";
        json += "\"totalTrips\":" + String(consumptionLearner.getTripsCount());
        json += "}";
        server.send(200, "application/json", json);
        #else
        server.send(501, "application/json", "{\"error\":\"Auto-apprendimento non disponibile\"}");
        #endif
    }

    // ===== ODOMETRO (PROTETTO PASSWORD) =====

    // Ottieni km attuali
    void handleGetOdometer() {
        uint32_t km = odo_get_current_km();
        String json = "{\"km\":" + String(km) + "}";
        server.send(200, "application/json", json);
    }

    // Imposta km del veicolo (richiede password)
    void handleSetOdometer() {
        if (!server.hasArg("km") || !server.hasArg("password")) {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Parametri mancanti\"}");
            return;
        }

        String passwordInput = server.arg("password");
        uint32_t newKm = server.arg("km").toInt();

        // Verifica password
        Preferences prefs;
        prefs.begin("odo_security", true);
        String storedPassword = prefs.getString("pwd", "");
        prefs.end();

        // Prima volta: imposta password
        if (storedPassword.isEmpty()) {
            prefs.begin("odo_security", false);
            prefs.putString("pwd", passwordInput);
            prefs.end();
        }
        // Verifica password
        else if (passwordInput != storedPassword) {
            server.send(403, "application/json", "{\"success\":false,\"error\":\"Password errata\"}");
            return;
        }

        // Imposta km
        if (odo_set_vehicle_km(newKm)) {
            server.send(200, "application/json", "{\"success\":true,\"km\":" + String(newKm) + "}");
        } else {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Valore km non valido\"}");
        }
    }

    // Reset password (richiede conferma)
    void handleResetOdoPassword() {
        Preferences prefs;
        prefs.begin("odo_security", false);
        prefs.remove("pwd");
        prefs.end();

        server.send(200, "application/json", "{\"success\":true}");
    }

    // ==================== PAGINE HTML (UI coerente) ====================
    String getMainConfigPage() {
        String currentMode = batteryConfig.getConfigMode();
        String modeIcon = (currentMode == "auto") ? "Auto" : (currentMode == "wizard") ? "Wizard" : "Manuale";
        return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Twizy Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1"><meta charset="UTF-8">
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
         background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
    .container{max-width:600px;margin:0 auto;background:rgba(255,255,255,0.95);backdrop-filter:blur(10px);
               border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,.1);overflow:hidden}
    .header{background:linear-gradient(135deg,#2196F3,#21CBF3);color:#fff;padding:24px 20px;text-align:center;position:relative}
    .header h1{font-size:26px;font-weight:600;margin-bottom:4px}
    .header-subtitle{font-size:12px;opacity:0.9}
    .content{padding:20px}
    .card{background:linear-gradient(135deg,#f5f7fa,#c3cfe2);padding:18px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,.08);margin-bottom:14px}
    .card-title{font-size:11px;color:#666;font-weight:700;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between}
    .badge{display:inline-block;padding:3px 8px;border-radius:10px;font-size:9px;font-weight:700;text-transform:uppercase;letter-spacing:0.3px}
    .badge-info{background:#d1ecf1;color:#0c5460}
    .badge-success{background:#d4edda;color:#155724}
    .badge-warning{background:#fff3cd;color:#856404}
    .soc-display{text-align:center;padding:20px 0}
    .soc-value{font-size:48px;font-weight:800;color:#2c3e50;line-height:1}
    .soc-label{font-size:11px;color:#7f8c8d;margin-top:4px;text-transform:uppercase;letter-spacing:0.5px}
    .range-display{text-align:center;padding:12px;background:rgba(255,255,255,0.6);border-radius:10px;margin-top:12px}
    .range-value{font-size:32px;font-weight:700;color:#1565c0}
    .range-label{font-size:11px;color:#666;margin-top:2px}
    .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}
    .metric{text-align:center;padding:10px;background:rgba(255,255,255,0.5);border-radius:8px}
    .metric-value{font-size:16px;font-weight:700;color:#2c3e50}
    .metric-label{font-size:9px;color:#666;margin-top:2px;text-transform:uppercase}
    .config-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid rgba(0,0,0,0.06)}
    .config-row:last-child{border-bottom:none}
    .config-label{font-size:12px;color:#666;font-weight:500}
    .config-value{font-size:14px;font-weight:700;color:#2c3e50}
    .btn-group{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
    .btn{padding:11px;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:13px;transition:transform 0.1s,opacity 0.2s}
    .btn:active{transform:scale(0.98)}
    .btn-primary{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff}
    .btn-secondary{background:#6c757d;color:#fff}
    .btn-success{background:#28a745;color:#fff}
    .btn-info{background:#17a2b8;color:#fff}
    .btn-danger{background:#dc3545;color:#fff}
    .btn-full{grid-column:1/-1}
    .info-box{padding:10px 12px;background:rgba(255,255,255,0.6);border-radius:8px;font-size:12px;color:#555;line-height:1.5;margin-top:10px}
    .footer{text-align:center;padding:14px;color:#7f8c8d;font-size:10px;opacity:0.8}
    .status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
    .status-charging{background:#28a745;animation:pulse 1.5s infinite}
    .status-driving{background:#3498db}
    .status-idle{background:#6c757d}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}
    @media(max-width:520px){.metrics{grid-template-columns:1fr;gap:8px}.btn-group{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Twizy Dashboard</h1>
      <div class="header-subtitle">Sistema di Monitoraggio e Configurazione</div>
    </div>
    <div class="content">

      <div class="card">
        <div class="card-title">
          <span>Stato Veicolo</span>
          <span id="status-badge" class="badge badge-info">Fermo</span>
        </div>
        <div class="soc-display">
          <div class="soc-value" id="soc">--</div>
          <div class="soc-label">Stato di Carica</div>
        </div>
        <div class="range-display">
          <div class="range-value" id="range">-- km</div>
          <div class="range-label">Autonomia stimata</div>
        </div>
        <div class="metrics">
          <div class="metric">
            <div class="metric-value" id="voltage">--</div>
            <div class="metric-label">Volt</div>
          </div>
          <div class="metric">
            <div class="metric-value" id="current">--</div>
            <div class="metric-label">Ampere</div>
          </div>
          <div class="metric">
            <div class="metric-value" id="temp">--</div>
            <div class="metric-label">Temp</div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-title">
          <span>Configurazione Batteria</span>
          <span id="mode-badge" class="badge badge-info">)rawliteral" + modeIcon + R"rawliteral(</span>
        </div>
        <div class="config-row">
          <span class="config-label">Capacita</span>
          <span class="config-value"><span id="capacity">--</span> Ah</span>
        </div>
        <div class="config-row">
          <span class="config-label">Consumo medio</span>
          <span class="config-value"><span id="consumption">--</span> Wh/km</span>
        </div>
        <div class="btn-group">
          <button class="btn btn-primary" onclick="location.href='/wizard'">Wizard</button>
          <button class="btn btn-primary" onclick="location.href='/manual'">Manuale</button>
        </div>
      </div>

      <div class="card" id="auto-card">
        <div class="card-title">
          <span>Auto-apprendimento</span>
          <span id="auto-badge" class="badge badge-secondary">Controllo...</span>
        </div>
        <div id="auto-content" class="info-box">Caricamento dati...</div>
      </div>

      <div class="card">
        <div class="card-title">
          <span>Odometro Veicolo</span>
          <span id="odo-badge" class="badge badge-info">RPM</span>
        </div>
        <div class="info-box">
          <div class="info-row">
            <span class="info-label">Km attuali:</span>
            <span class="info-value" id="current-km">--</span>
          </div>
          <div style="font-size:10px;color:#666;margin-top:8px;padding:8px;background:#f0f0f0;border-radius:5px">
            ℹ️ Calcolo da giri motore - Dati protetti su SD
          </div>
        </div>
        <button class="btn btn-warning btn-full" onclick="showOdoModal()" style="margin-top:8px">Imposta Km Veicolo</button>
      </div>

      <div class="card">
        <div class="card-title">Sistema</div>
        <button class="btn btn-info btn-full" onclick="location.href='/ota'" id="ota-btn">Aggiornamento Firmware</button>
        <button class="btn btn-secondary btn-full" onclick="location.href='/wifi'" style="margin-top:8px">Gestione WiFi</button>
        <button class="btn btn-danger btn-full" onclick="resetConfig()" style="margin-top:8px">Reset Configurazione</button>
      </div>

    </div>
    <div class="footer">Aggiornamento automatico - Ultima sincronizzazione: <span id="last-update">mai</span></div>
  </div>

  <!-- Modal Odometro -->
  <div id="odo-modal" style="display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.7);z-index:1000;align-items:center;justify-content:center">
    <div style="background:white;padding:30px;border-radius:12px;max-width:400px;width:90%">
      <h2 style="margin:0 0 20px 0;color:#333">Imposta Km Veicolo</h2>
      <div style="margin-bottom:15px">
        <label style="display:block;margin-bottom:5px;color:#666;font-weight:600">Km attuali veicolo:</label>
        <input type="number" id="odo-km-input" placeholder="es: 23560" style="width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;font-size:16px">
      </div>
      <div style="margin-bottom:20px">
        <label style="display:block;margin-bottom:5px;color:#666;font-weight:600">Password protezione:</label>
        <input type="password" id="odo-pwd-input" placeholder="Inserisci password" style="width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;font-size:16px">
        <small style="color:#999;display:block;margin-top:5px">Prima volta: crea nuova password. Successivamente: inserisci password esistente</small>
      </div>
      <div id="odo-error" style="display:none;padding:10px;background:#f8d7da;color:#721c24;border-radius:5px;margin-bottom:15px"></div>
      <div style="display:flex;gap:10px">
        <button onclick="saveOdometer()" class="btn btn-primary" style="flex:1">Salva</button>
        <button onclick="closeOdoModal()" class="btn btn-secondary" style="flex:1">Annulla</button>
      </div>
    </div>
  </div>

<script>
const POLL_INTERVAL_DRIVING=2000,POLL_INTERVAL_CHARGING=3000,POLL_INTERVAL_IDLE=5000;
let currentInterval=POLL_INTERVAL_IDLE,pollTimer=null;
function formatTime(){const t=new Date();return t.getHours().toString().padStart(2,'0')+':'+t.getMinutes().toString().padStart(2,'0')+':'+t.getSeconds().toString().padStart(2,'0');}

// === FUNZIONI ODOMETRO ===
async function loadOdometer(){
  try{
    const r=await fetch('/api/odometer',{cache:'no-store'});
    const d=await r.json();
    document.getElementById('current-km').textContent=d.km+' km';
    document.getElementById('odo-badge').textContent=d.km+' km';
    document.getElementById('odo-badge').className='badge badge-success';
  }catch(e){
    document.getElementById('current-km').textContent='Errore';
    document.getElementById('odo-badge').textContent='Errore';
    document.getElementById('odo-badge').className='badge badge-danger';
  }
}
function showOdoModal(){
  document.getElementById('odo-modal').style.display='flex';
  document.getElementById('odo-km-input').value='';
  document.getElementById('odo-pwd-input').value='';
  document.getElementById('odo-error').style.display='none';
}
function closeOdoModal(){
  document.getElementById('odo-modal').style.display='none';
}
async function saveOdometer(){
  const km=document.getElementById('odo-km-input').value;
  const pwd=document.getElementById('odo-pwd-input').value;
  const errDiv=document.getElementById('odo-error');

  if(!km||!pwd){
    errDiv.textContent='Compila tutti i campi';
    errDiv.style.display='block';
    return;
  }

  try{
    const fd=new FormData();
    fd.append('km',km);
    fd.append('password',pwd);
    const r=await fetch('/api/set_odometer',{method:'POST',body:fd});
    const d=await r.json();

    if(d.success){
      closeOdoModal();
      loadOdometer();
      alert('Km veicolo aggiornati con successo!');
    }else{
      errDiv.textContent=d.error||'Errore sconosciuto';
      errDiv.style.display='block';
    }
  }catch(e){
    errDiv.textContent='Errore di connessione';
    errDiv.style.display='block';
  }
}

async function loadData(){
  try{
    const[s,c]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/config',{cache:'no-store'})]);
    const status=await s.json(),config=await c.json();
    const soc=status.soc||0,voltage=status.voltage||0,current=status.current||0,temp=status.avgTemp||status.temp||0;
    const charging=status.charging||false,driving=status.driving||false;
    document.getElementById('soc').textContent=Math.round(soc)+'%';
    document.getElementById('voltage').textContent=voltage.toFixed(1)+'V';
    document.getElementById('current').textContent=current.toFixed(1)+'A';
    document.getElementById('temp').textContent=Math.round(temp)+'C';
    const capacity=config.capacity||163,consumption=config.consumption||100;
    const range=capacity&&consumption?Math.round((capacity*57.6)/consumption*0.95):0;
    document.getElementById('range').textContent=range+' km';
    const sb=document.getElementById('status-badge');
    if(charging){sb.textContent='In Ricarica';sb.className='badge badge-success';}
    else if(driving){sb.textContent='In Movimento';sb.className='badge badge-info';}
    else{sb.textContent='Fermo';sb.className='badge badge-secondary';}
    document.getElementById('capacity').textContent=capacity.toFixed(1);
    document.getElementById('consumption').textContent=consumption.toFixed(1);
    const mode=config.mode||'manual',mb=document.getElementById('mode-badge');
    if(mode==='auto'){mb.textContent='Auto';mb.className='badge badge-success';}
    else if(mode==='wizard'){mb.textContent='Wizard';mb.className='badge badge-info';}
    else{mb.textContent='Manuale';mb.className='badge badge-warning';}
    const hasAuto=config.hasAutoLearning||false,trips=config.trips||0;
    const ab=document.getElementById('auto-badge'),ac=document.getElementById('auto-content');
    if(!hasAuto){ab.textContent='Non disponibile';ab.className='badge badge-secondary';ac.innerHTML='Modulo auto-apprendimento non presente nel firmware.';}
    else if(mode==='auto'){ab.textContent='Attivo ('+trips+' viaggi)';ab.className='badge badge-success';
      ac.innerHTML='Sistema attivo. Dati salvati su SD in /trips.csv<div class="btn-group" style="margin-top:12px"><button class="btn btn-primary" onclick="location.href=\'/csv\'">Visualizza CSV</button><button class="btn btn-primary" onclick="location.href=\'/stats\'">Statistiche</button></div><button class="btn btn-danger btn-full" onclick="resetAutoLearning()" style="margin-top:8px">Reset Dati</button>';}
    else{ab.textContent='Disponibile';ab.className='badge badge-info';
      ac.innerHTML='Attiva per registrare automaticamente consumi e viaggi su SD.<button class="btn btn-success btn-full" onclick="enableAutoLearning()" style="margin-top:12px">Attiva Auto-apprendimento</button>';}
    let newInterval=POLL_INTERVAL_IDLE;
    if(driving)newInterval=POLL_INTERVAL_DRIVING;else if(charging)newInterval=POLL_INTERVAL_CHARGING;
    if(newInterval!==currentInterval){currentInterval=newInterval;if(pollTimer)clearInterval(pollTimer);pollTimer=setInterval(loadData,currentInterval);}
    document.getElementById('last-update').textContent=formatTime();
  }catch(e){console.error('Errore:',e);}
}
function enableAutoLearning(){if(!confirm('Attivare auto-apprendimento?'))return;fetch('/api/enable_auto',{method:'POST'}).then(r=>r.json()).then(d=>{if(d.success)loadData();else alert('Errore attivazione');}).catch(()=>alert('Errore rete'));}
function resetAutoLearning(){if(!confirm('Eliminare tutti i dati dei viaggi?'))return;fetch('/api/reset_auto',{method:'POST'}).then(r=>r.json()).then(d=>{if(d.success)loadData();else alert('Errore reset');}).catch(()=>alert('Errore rete'));}
function resetConfig(){if(!confirm('Ripristinare configurazione default?'))return;fetch('/api/reset',{method:'POST'}).then(r=>r.json()).then(d=>{if(d.success)loadData();else alert('Errore reset');}).catch(()=>alert('Errore rete'));}
async function checkOTA(){try{const r=await fetch('/ota/check');const d=await r.json();if(d.ok){const b=document.getElementById('ota-btn');b.textContent='Aggiorna Firmware (v'+d.version+' disponibile)';b.className='btn btn-warning btn-full';}}catch(e){}}
(async function(){await loadData();await loadOdometer();await checkOTA();pollTimer=setInterval(loadData,currentInterval);})();
</script>
</body></html>
)rawliteral";
    }
    String getWizardPage() {
        return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Wizard Configurazione</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
         background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
    .container{max-width:600px;margin:0 auto;background:rgba(255,255,255,0.95);backdrop-filter:blur(10px);
               border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,.1);overflow:hidden}
    .header{background:linear-gradient(135deg,#2196F3,#21CBF3);color:#fff;padding:24px;text-align:center}
    .header h1{font-size:24px;font-weight:600;margin-bottom:4px}
    .header-subtitle{font-size:12px;opacity:0.9}
    .content{padding:24px}
    .section{margin-bottom:24px}
    .section-title{font-size:11px;color:#666;font-weight:700;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:12px}
    .step-number{display:inline-block;width:24px;height:24px;background:#3498db;color:#fff;border-radius:50%;
                 text-align:center;line-height:24px;font-size:12px;font-weight:700;margin-right:8px}
    .option-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
    .option{position:relative}
    .option input{position:absolute;opacity:0;width:0;height:0}
    .option label{display:block;padding:14px;background:linear-gradient(135deg,#f5f7fa,#c3cfe2);border-radius:12px;
                  text-align:center;cursor:pointer;transition:all 0.2s;border:2px solid transparent;font-weight:600;font-size:14px}
    .option label:hover{transform:translateY(-2px);box-shadow:0 4px 12px rgba(0,0,0,.15)}
    .option input:checked + label{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff;border-color:#2980b9;
                                   box-shadow:0 4px 15px rgba(52,152,219,.4)}
    .option-detail{font-size:11px;opacity:0.8;margin-top:4px}
    .custom-input{margin-top:10px;display:none}
    .custom-input input{width:100%;padding:12px;border:2px solid #ddd;border-radius:10px;font-size:14px;font-weight:600}
    .custom-input input:focus{outline:none;border-color:#3498db}
    .preview-card{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;padding:24px;border-radius:15px;
                  margin:24px 0;text-align:center;box-shadow:0 8px 20px rgba(102,126,234,.3)}
    .preview-title{font-size:12px;opacity:0.9;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px}
    .preview-value{font-size:48px;font-weight:800;line-height:1;margin:12px 0}
    .preview-label{font-size:14px;opacity:0.9}
    .preview-details{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px;padding-top:16px;
                     border-top:1px solid rgba(255,255,255,0.2)}
    .preview-detail{text-align:center}
    .preview-detail-value{font-size:20px;font-weight:700}
    .preview-detail-label{font-size:10px;opacity:0.8;margin-top:4px;text-transform:uppercase}
    .btn{padding:14px;border:none;border-radius:12px;font-weight:700;cursor:pointer;font-size:15px;width:100%;
         transition:transform 0.1s,opacity 0.2s}
    .btn:active{transform:scale(0.98)}
    .btn-primary{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff;box-shadow:0 4px 15px rgba(52,152,219,.3)}
    .btn-secondary{background:#6c757d;color:#fff;margin-top:10px}
    .success-card{background:linear-gradient(135deg,#56ab2f,#a8e063);color:#fff;padding:24px;border-radius:15px;
                  text-align:center;display:none}
    .success-icon{font-size:48px;margin-bottom:12px}
    .success-title{font-size:20px;font-weight:700;margin-bottom:8px}
    .success-text{font-size:14px;opacity:0.9}
    @media(max-width:520px){.option-group{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Configurazione Guidata</h1>
      <div class="header-subtitle">Calcolo Automatico Capacita e Consumo</div>
    </div>
    <div class="content">

      <div id="wizard-form">
        <div class="section">
          <div class="section-title"><span class="step-number">1</span>Tipo Batteria</div>
          <div class="option-group">
            <div class="option">
              <input type="radio" name="battery" id="bat70" value="70">
              <label for="bat70">Standard<div class="option-detail">70 Ah</div></label>
            </div>
            <div class="option">
              <input type="radio" name="battery" id="bat163" value="163" checked>
              <label for="bat163">Modificata<div class="option-detail">163 Ah</div></label>
            </div>
            <div class="option">
              <input type="radio" name="battery" id="bat200" value="200">
              <label for="bat200">Grande<div class="option-detail">200 Ah</div></label>
            </div>
            <div class="option">
              <input type="radio" name="battery" id="batCustom" value="custom">
              <label for="batCustom">Personalizzata<div class="option-detail">Custom Ah</div></label>
            </div>
          </div>
          <div class="custom-input" id="customBatteryInput">
            <input type="number" id="customBattery" placeholder="Inserisci capacita (Ah)" min="50" max="500" step="0.1">
          </div>
        </div>

        <div class="section">
          <div class="section-title"><span class="step-number">2</span>Stile di Guida</div>
          <div class="option-group">
            <div class="option">
              <input type="radio" name="style" id="styleEco" value="80">
              <label for="styleEco">Eco<div class="option-detail">80 Wh/km</div></label>
            </div>
            <div class="option">
              <input type="radio" name="style" id="styleNormal" value="100" checked>
              <label for="styleNormal">Normale<div class="option-detail">100 Wh/km</div></label>
            </div>
            <div class="option">
              <input type="radio" name="style" id="styleSport" value="130">
              <label for="styleSport">Sportivo<div class="option-detail">130 Wh/km</div></label>
            </div>
          </div>
        </div>

        <div class="section">
          <div class="section-title"><span class="step-number">3</span>Tipo di Percorso</div>
          <div class="option-group">
            <div class="option">
              <input type="radio" name="terrain" id="terrainMountain" value="0.9">
              <label for="terrainMountain">Montagna<div class="option-detail">Salite</div></label>
            </div>
            <div class="option">
              <input type="radio" name="terrain" id="terrainCity" value="1.0" checked>
              <label for="terrainCity">Citta<div class="option-detail">Pianura</div></label>
            </div>
            <div class="option">
              <input type="radio" name="terrain" id="terrainHighway" value="1.1">
              <label for="terrainHighway">Extraurbano<div class="option-detail">Veloce</div></label>
            </div>
          </div>
        </div>

        <div class="preview-card">
          <div class="preview-title">Autonomia Stimata</div>
          <div class="preview-value" id="previewRange">--</div>
          <div class="preview-label">chilometri</div>
          <div class="preview-details">
            <div class="preview-detail">
              <div class="preview-detail-value" id="previewCapacity">--</div>
              <div class="preview-detail-label">Capacita (Ah)</div>
            </div>
            <div class="preview-detail">
              <div class="preview-detail-value" id="previewConsumption">--</div>
              <div class="preview-detail-label">Consumo (Wh/km)</div>
            </div>
          </div>
        </div>

        <button class="btn btn-primary" id="btnApply">Applica Configurazione</button>
        <button class="btn btn-secondary" onclick="location.href='/'">Annulla</button>
      </div>

      <div class="success-card" id="success-card">
        <div class="success-icon">✓</div>
        <div class="success-title">Configurazione Applicata</div>
        <div class="success-text">Le impostazioni sono state salvate con successo</div>
        <button class="btn btn-primary" onclick="location.href='/'" style="margin-top:20px">Torna alla Dashboard</button>
      </div>

    </div>
  </div>

<script>
let capacity=163,consumption=100,terrain=1.0;

function updatePreview(){
  const batRadio=document.querySelector('input[name="battery"]:checked');
  const styleRadio=document.querySelector('input[name="style"]:checked');
  const terrainRadio=document.querySelector('input[name="terrain"]:checked');

  if(batRadio.value==='custom'){
    const customVal=parseFloat(document.getElementById('customBattery').value);
    capacity=customVal&&customVal>=50&&customVal<=500?customVal:163;
  }else{
    capacity=parseFloat(batRadio.value);
  }

  const baseConsumption=parseFloat(styleRadio.value);
  terrain=parseFloat(terrainRadio.value);
  consumption=baseConsumption/terrain;

  const range=Math.round((capacity*57.6)/consumption*0.95);

  document.getElementById('previewRange').textContent=range;
  document.getElementById('previewCapacity').textContent=capacity.toFixed(1);
  document.getElementById('previewConsumption').textContent=consumption.toFixed(1);
}

document.querySelectorAll('input[name="battery"]').forEach(r=>{
  r.addEventListener('change',e=>{
    document.getElementById('customBatteryInput').style.display=
      e.target.value==='custom'?'block':'none';
    updatePreview();
  });
});

document.getElementById('customBattery').addEventListener('input',updatePreview);
document.querySelectorAll('input[name="style"]').forEach(r=>r.addEventListener('change',updatePreview));
document.querySelectorAll('input[name="terrain"]').forEach(r=>r.addEventListener('change',updatePreview));

document.getElementById('btnApply').addEventListener('click',async()=>{
  const fd=new FormData();
  const batRadio=document.querySelector('input[name="battery"]:checked');
  if(batRadio.value==='custom'){
    fd.append('battery','custom');
    fd.append('custom_battery',capacity);
  }else{
    fd.append('battery',batRadio.value);
  }
  fd.append('style',document.querySelector('input[name="style"]:checked').value);
  fd.append('terrain',document.querySelector('input[name="terrain"]:checked').value);

  try{
    const res=await fetch('/api/wizard',{method:'POST',body:fd});
    const data=await res.json();
    if(data.success){
      document.getElementById('wizard-form').style.display='none';
      document.getElementById('success-card').style.display='block';
    }else{
      alert('Errore: '+(data.error||'Operazione fallita'));
    }
  }catch(err){
    alert('Errore di rete: '+err.message);
  }
});

updatePreview();
</script>
</body>
</html>
)rawliteral";
    }

    String getManualPage() {
        return R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Config Manuale</title>
<style>body{font-family:Arial,sans-serif;background:#f0f0f0;padding:20px}.container{max-width:500px;margin:0 auto;background:#fff;padding:24px;border-radius:12px}.fg{margin:12px 0}</style>
</head><body><div class="container">
<h1>Configurazione Manuale</h1>
<div class="fg"><label>CapacitÃ  (Ah)</label><input id="capacity" type="number" step="0.1" min="50" max="500" style="width:100%"></div>
<div class="fg"><label>Consumo (Wh/km)</label><input id="consumption" type="number" step="1" min="40" max="250" style="width:100%"></div>
<button id="save">Salva</button> <button onclick="location.href='/'">Annulla</button>
<script>
fetch('/api/config').then(r=>r.json()).then(d=>{
  capacity.value=d.capacity||163; consumption.value=d.consumption||100;
});
save.onclick=()=>{
  const cap=parseFloat(capacity.value), con=parseFloat(consumption.value);
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({capacity:cap,consumption:con})})
   .then(r=>r.json()).then(j=>{ if(j.success) location.href='/'; else alert('Errore');});
};
</script>
</div></body></html>
)rawliteral";
    }

 String getStatsPage() {
    return R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Statistiche</title>
<style>body{font-family:Arial,sans-serif;background:#f0f0f0;padding:20px}.container{max-width:700px;margin:0 auto;background:#fff;padding:24px;border-radius:12px}</style>
</head><body><div class="container">
<h1>Statistiche Auto-Apprendimento</h1>
<div id="stats">Caricamentoâ€¦</div>
<div style="margin-top:10px">
  <button onclick="location.href='/'">Dashboard</button>
  <button onclick="location.href='/csv'">Visualizza CSV</button>
  <button onclick="location.href='/api/download_csv'">Scarica CSV</button>
  <button onclick="location.reload()">Aggiorna</button>
</div>
<script>
fetch('/api/stats').then(r=>{if(!r.ok)throw new Error('N/D');return r.text();})
  .then(h=>{document.getElementById('stats').innerHTML=h;})
  .catch(()=>{document.getElementById('stats').innerHTML='<p style="color:red">Statistiche non disponibili</p>';});
</script>
</div></body></html>
)rawliteral";
}


    // ==================== OTA ====================
    // File su SD per staging
    static constexpr const char* kOtaSDPath_ = "/fw/update.bin";

    // Stato OTA
    enum class OtaState : uint8_t { IDLE, UPLOADING, READY, VERIFYING, INSTALLING, DONE, ERROR };
    OtaState otaState_ = OtaState::IDLE;
    String   otaMsg_;
    int      otaProgress_ = 0;      // 0..100
    String   otaUploadedName_;
    size_t   otaUploadedSize_ = 0;
    bool     otaStartRequested_ = false;

    // --- PATCH OTA: nuove variabili stato/verifica ---
//static String updSha256_;           // SHA atteso (da manifest o da form upload)
static String lastComputedSha256_;  // SHA calcolato sul file caricato (solo informativo)


    // Auto-update (manifest pubblico)
    const char* kManifestURL_ = "https://raw.githubusercontent.com/myo900/ReNova-UpDate/main/manifest.json";

    // Risultato ultimo check manifest
    bool   updAvailable_ = false;
    String updVersion_, updUrl_, updSha256_;
    size_t updSize_ = 0;

    OTAUpdater ota_;

    // --- Download OTA in background (task FreeRTOS) ---
    TaskHandle_t otaFetchTask_ = nullptr;

    static void otaFetchTaskTrampoline(void* pv) {
        WebConfig* self = reinterpret_cast<WebConfig*>(pv);
        if (self) self->runOtaFetchTask();
        vTaskDelete(nullptr);
    }

    void runOtaFetchTask() {
    // Interrompi eventuale scansione WiFi che bloccherebbe la rete
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) WiFi.scanDelete();

    // Esegue il download vero e proprio (emette progress via onProgress)
    bool ok = ota_.downloadToSD(updUrl_, kOtaSDPath_, updSize_);
    if (!ok) {
        otaState_ = OtaState::ERROR;
        otaMsg_   = "download failed";
        otaFetchTask_ = nullptr;
        String errMsg = "OTA: Download fallito da " + updUrl_;
        sdLogger.error(errMsg.c_str());
        return;
    }

    sdLogger.info("OTA: Download completato, file salvato su SD");

    // Verifica SHA256 se presente nel manifest
    if (updSha256_.length() == 64) {
        otaMsg_      = "Verifica hash";
        otaProgress_ = 99;
        sdLogger.info("OTA: Verifica SHA256 in corso...");
        bool verified = ota_.verifySha256OnSD(kOtaSDPath_, updSha256_);
        if (!verified) {
            otaState_ = OtaState::ERROR;
            otaMsg_   = "sha256 mismatch";
            otaFetchTask_ = nullptr;
            sdLogger.error("OTA: SHA256 non corrispondente! File corrotto.");
            return;
        }
        sdLogger.info("OTA: SHA256 verificato correttamente");
    }

    // Pronto per installazione
    otaState_    = OtaState::READY;
    otaProgress_ = 100;
    otaMsg_      = "File pronto per installazione";
    sdLogger.info("OTA: File pronto per installazione");

    // ⬇️ Avvio automatico dell'installazione dopo download da Internet
    otaStartRequested_ = true;

    otaFetchTask_ = nullptr;
}

    String jsonOtaStatus_() {
        auto st2s = [](OtaState s)->const char* {
            switch(s){
                case OtaState::IDLE: return "idle";
                case OtaState::UPLOADING: return "uploading";
                case OtaState::READY: return "ready";
                case OtaState::VERIFYING: return "verifying";
                case OtaState::INSTALLING: return "installing";
                case OtaState::DONE: return "done";
                case OtaState::ERROR: return "error";
            }
            return "idle";
        };
        String j = "{\"state\":\""; j += st2s(otaState_);
        j += "\",\"progress\":"; j += otaProgress_;
        j += ",\"message\":\""; { String m=otaMsg_; m.replace("\"","\\\""); j+=m; }
        j += "\",\"file\":\"";  { String n=otaUploadedName_; n.replace("\"","\\\""); j+=n; }
        j += "\",\"size\":"; j += String((unsigned long)otaUploadedSize_);
        j += ",\"update\":{";
        j += "\"available\":"; j += updAvailable_?"true":"false";
        j += ",\"version\":\"" + updVersion_ + "\"";
        j += ",\"size\":" + String((unsigned long)updSize_);
        j += ",\"expected_sha256\":\""; { String s=updSha256_; s.replace("\"","\\\""); j+=s; } j += "\"";
        j += ",\"computed_sha256\":\""; { String s=lastComputedSha256_; s.replace("\"","\\\""); j+=s; } j += "\"";
        j += "}}";
        return j;
    }

    String getOtaPage() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Twizy - Aggiornamento Firmware</title>
  <meta name="viewport" content="width=device-width, initial-scale=1"><meta charset="UTF-8">
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
         background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
    .container{max-width:600px;margin:0 auto;background:rgba(255,255,255,0.95);backdrop-filter:blur(10px);
               border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,.1);overflow:hidden}
    .header{background:linear-gradient(135deg,#2196F3,#21CBF3);color:#fff;padding:24px;text-align:center}
    .header h1{font-size:24px;font-weight:600}
    .content{padding:24px}
    .card{background:linear-gradient(135deg,#f5f7fa,#c3cfe2);padding:18px;border-radius:15px;
          box-shadow:0 4px 15px rgba(0,0,0,.08);margin-bottom:16px}
    .version-box{background:#fff;padding:12px;border-radius:10px;margin:8px 0;display:flex;justify-content:space-between;align-items:center}
    .version-label{font-size:11px;color:#666;text-transform:uppercase;font-weight:600}
    .version-value{font-size:18px;font-weight:700;color:#2c3e50;font-family:ui-monospace,monospace}
    .badge{display:inline-block;padding:4px 10px;border-radius:12px;font-size:10px;font-weight:700;text-transform:uppercase}
    .badge-success{background:#d4edda;color:#155724}
    .badge-info{background:#d1ecf1;color:#0c5460}
    .badge-warning{background:#fff3cd;color:#856404}
    .label{font-size:12px;color:#666;font-weight:600;text-transform:uppercase;margin-bottom:8px}
    .btn{padding:14px;border:none;border-radius:10px;font-weight:700;cursor:pointer;width:100%;transition:opacity 0.2s,transform 0.1s}
    .btn:active{transform:scale(0.98)}
    .btn-primary{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff;font-size:16px}
    .btn-secondary{background:#6c757d;color:#fff;font-size:14px;margin-top:8px}
    .btn:disabled{opacity:.4;cursor:not-allowed;transform:none}
    .muted{font-size:12px;color:#6b7280;line-height:1.4}
    .progress-container{margin:16px 0}
    .progress{height:24px;background:#e9eef6;border-radius:12px;overflow:hidden;border:1px solid #d5deea;position:relative}
    .bar{height:100%;width:0;background:linear-gradient(90deg,#3498db,#77b7f0);transition:width 0.4s ease}
    .progress-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:13px;font-weight:700;color:#333;z-index:1}
    .status-box{background:#fff;padding:12px;border-radius:10px;margin-top:12px;display:flex;align-items:center}
    .status-indicator{width:10px;height:10px;border-radius:50%;margin-right:10px;flex-shrink:0}
    .status-idle{background:#ccc}
    .status-working{background:#ff9800;animation:pulse 1.2s infinite}
    .status-success{background:#4caf50}
    .status-error{background:#f44336}
    .status-text{font-size:14px;color:#2c3e50;font-weight:500}
    .time-estimate{font-size:11px;color:#7f8c8d;margin-top:4px}
    .footer{text-align:center;padding:16px;color:#7f8c8d;font-size:11px}
    .link{color:#1f53ff;text-decoration:none;font-weight:600}
    .divider{height:1px;background:#ddd;margin:16px 0}
    .collapse{max-height:0;overflow:hidden;transition:max-height 0.3s ease}
    .collapse.open{max-height:500px}
    .toggle-link{color:#3498db;cursor:pointer;font-size:12px;text-decoration:underline;margin-top:8px;display:inline-block}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}
    @media(max-width:520px){.version-box{flex-direction:column;align-items:flex-start;gap:8px}}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Aggiornamento Firmware</h1>
      <div style="font-size:12px;margin-top:6px;opacity:0.9">Sistema OTA Automatico</div>
    </div>
    <div class="content">

      <div class="card" id="versionCard">
        <div class="label">Versioni</div>
        <div class="version-box">
          <div>
            <div class="version-label">Attuale</div>
            <div class="version-value" id="currentVer">--</div>
          </div>
          <span class="badge badge-success" id="verBadge">Aggiornato</span>
        </div>
        <div class="version-box" id="latestBox" style="display:none">
          <div>
            <div class="version-label">Disponibile</div>
            <div class="version-value" id="latestVer">--</div>
          </div>
        </div>
        <button class="btn btn-primary" id="btnUpdate" style="margin-top:12px">Aggiorna Automaticamente</button>
        <div class="muted" id="updateNote" style="margin-top:8px;display:none"></div>
      </div>

      <div class="card" id="progressCard" style="display:none">
        <div class="label">Avanzamento</div>
        <div class="progress-container">
          <div class="progress">
            <div class="bar" id="bar"></div>
            <div class="progress-text" id="progressText">0%</div>
          </div>
        </div>
        <div class="status-box">
          <span class="status-indicator" id="statusDot" class="status-idle"></span>
          <div style="flex:1">
            <div class="status-text" id="statusMsg">In attesa...</div>
            <div class="time-estimate" id="timeEst"></div>
          </div>
        </div>
      </div>

      <div style="text-align:center;margin-top:12px">
        <span class="toggle-link" id="toggleAdvanced">Mostra opzioni avanzate</span>
      </div>

      <div class="collapse" id="advancedSection">
        <div class="divider"></div>
        <div class="card">
          <div class="label">Caricamento Manuale</div>
          <p class="muted">Seleziona un file .bin dal tuo dispositivo</p>
          <label class="btn btn-secondary" style="margin-top:12px">
            <input id="fileInput" type="file" accept=".bin" hidden>Scegli File Firmware
          </label>
          <p id="fileInfo" class="muted" style="margin-top:8px"></p>
          <button id="btnManualInstall" class="btn btn-secondary" disabled style="margin-top:8px">Carica e Installa</button>
        </div>
      </div>

      <div style="text-align:center;margin-top:16px">
        <a class="link" href="/">Torna alla Dashboard</a>
      </div>
    </div>
    <div class="footer">ATTENZIONE: Non spegnere o disconnettere durante l'aggiornamento. Il dispositivo si riavviera' automaticamente.</div>
  </div>

<script>
// ===== CONFIGURAZIONE =====
const POLL_INTERVAL_ACTIVE = 2500;
const POLL_INTERVAL_IDLE = 5000;
const MAX_RETRIES = 3;

// ===== ELEMENTI DOM =====
const $ = s => document.querySelector(s);
const currentVer = $('#currentVer');
const latestVer = $('#latestVer');
const latestBox = $('#latestBox');
const verBadge = $('#verBadge');
const btnUpdate = $('#btnUpdate');
const updateNote = $('#updateNote');
const progressCard = $('#progressCard');
const bar = $('#bar');
const progressText = $('#progressText');
const statusDot = $('#statusDot');
const statusMsg = $('#statusMsg');
const timeEst = $('#timeEst');
const toggleAdvanced = $('#toggleAdvanced');
const advancedSection = $('#advancedSection');
const fileInput = $('#fileInput');
const fileInfo = $('#fileInfo');
const btnManualInstall = $('#btnManualInstall');

// ===== STATO GLOBALE =====
let polling = null;
let updateAvailable = false;
let currentVersion = null;
let newVersion = null;
let startTime = null;
let lastProgress = 0;
let retryCount = 0;
let manualFile = null;
let wakeLock = null;

// ===== UTILITY =====
function formatTime(seconds) {
  if (!seconds || seconds < 0) return '';
  if (seconds < 60) return Math.round(seconds) + 's rimanenti';
  const mins = Math.floor(seconds / 60);
  const secs = Math.round(seconds % 60);
  return mins + 'm ' + secs + 's rimanenti';
}

function estimateTimeLeft(progress, elapsed) {
  if (progress <= 0 || progress >= 100) return null;
  const totalTime = (elapsed / progress) * 100;
  return totalTime - elapsed;
}

// ===== AGGIORNAMENTO UI =====
function setProgress(percent, message, status) {
  percent = Math.max(0, Math.min(100, percent || 0));
  bar.style.width = percent + '%';
  progressText.textContent = percent + '%';
  if (message) statusMsg.textContent = message;

  const classes = ['status-idle', 'status-working', 'status-success', 'status-error'];
  statusDot.classList.remove(...classes);
  statusDot.classList.add('status-' + (status || 'idle'));

  if (startTime && percent > 0 && percent < 100 && status === 'working') {
    const elapsed = (Date.now() - startTime) / 1000;
    const remaining = estimateTimeLeft(percent, elapsed);
    timeEst.textContent = remaining ? formatTime(remaining) : '';
  } else {
    timeEst.textContent = '';
  }
  lastProgress = percent;
}

function updateVersionUI(current, latest, available) {
  currentVer.textContent = current || '--';
  currentVersion = current;

  if (available && latest) {
    latestVer.textContent = latest;
    latestBox.style.display = 'flex';
    verBadge.textContent = 'Aggiornamento disponibile';
    verBadge.className = 'badge badge-warning';
    btnUpdate.textContent = 'Aggiorna a ' + latest;
    btnUpdate.disabled = false;
    updateNote.textContent = 'Nuovo firmware disponibile. Clicca per avviare l\'aggiornamento automatico.';
    updateNote.style.display = 'block';
    newVersion = latest;
    updateAvailable = true;
  } else {
    latestBox.style.display = 'none';
    verBadge.textContent = 'Aggiornato';
    verBadge.className = 'badge badge-success';
    btnUpdate.textContent = 'Controlla Aggiornamenti';
    btnUpdate.disabled = false;
    updateNote.style.display = 'none';
    updateAvailable = false;
  }
}

// ===== POLLING INTELLIGENTE =====
async function pollStatus() {
  try {
    const res = await fetch('/ota/status', { cache: 'no-store' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    handleOTAStatus(data);
    retryCount = 0;
  } catch (err) {
    console.error('Poll error:', err);
    retryCount++;
    if (retryCount >= MAX_RETRIES) {
      stopPolling();
      setProgress(lastProgress, 'Errore di comunicazione', 'error');
    }
  }
}

function startPolling(interval) {
  interval = interval || POLL_INTERVAL_ACTIVE;
  if (polling) return;
  polling = setInterval(pollStatus, interval);
}

function stopPolling() {
  if (polling) { clearInterval(polling); polling = null; }
}

// ===== GESTIONE STATO OTA =====
function handleOTAStatus(data) {
  const state = data.state || 'idle';
  const progress = data.progress || 0;
  const message = data.message || '';

  switch(state) {
    case 'idle':
      if (progressCard.style.display === 'none') return;
      setProgress(0, 'In attesa', 'idle');
      stopPolling();
      releaseWakeLock();
      break;
    case 'uploading':
      progressCard.style.display = 'block';
      setProgress(progress, 'Download firmware in corso...', 'working');
      if (!startTime) startTime = Date.now();
      break;
    case 'ready':
      setProgress(progress, 'File pronto, avvio installazione...', 'success');
      setTimeout(function() { fetch('/ota/start', { method: 'POST' }); }, 1000);
      break;
    case 'verifying':
      setProgress(progress, 'Verifica integrita\' firmware...', 'working');
      break;
    case 'installing':
      setProgress(progress, 'Installazione in corso...', 'working');
      btnUpdate.disabled = true;
      break;
    case 'done':
      setProgress(100, 'Completato! Riavvio del dispositivo...', 'success');
      stopPolling();
      setTimeout(function() {
        statusMsg.textContent = 'Riavvio completato. Ricaricare la pagina tra 5 secondi...';
        setTimeout(function() { location.reload(); }, 5000);
      }, 3000);
      break;
    case 'error':
      setProgress(progress, message || 'Errore durante aggiornamento', 'error');
      stopPolling();
      btnUpdate.disabled = false;
      releaseWakeLock();
      break;
  }
}

// ===== WAKE LOCK =====
async function requestWakeLock() {
  if ('wakeLock' in navigator) {
    try {
      wakeLock = await navigator.wakeLock.request('screen');
      console.log('Wake lock attivato');
    } catch (err) { console.warn('Wake lock non supportato:', err); }
  }
}

function releaseWakeLock() {
  if (wakeLock) { wakeLock.release(); wakeLock = null; console.log('Wake lock rilasciato'); }
}

// ===== CHECK AGGIORNAMENTI =====
async function checkForUpdate() {
  btnUpdate.disabled = true;
  btnUpdate.textContent = 'Controllo...';
  try {
    const res = await fetch('/ota/check');
    const data = await res.json();
    updateVersionUI(data.current, data.version, data.ok);
  } catch (err) {
    console.error('Check error:', err);
    updateNote.textContent = 'Errore di rete. Verifica la connessione Internet.';
    updateNote.style.display = 'block';
    btnUpdate.disabled = false;
    btnUpdate.textContent = 'Riprova';
  }
}

// ===== AGGIORNAMENTO AUTOMATICO =====
async function startAutoUpdate() {
  if (!updateAvailable) {
    await checkForUpdate();
    if (!updateAvailable) return;
  }
  btnUpdate.disabled = true;
  progressCard.style.display = 'block';
  startTime = Date.now();
  await requestWakeLock();
  setProgress(0, 'Avvio download...', 'working');
  startPolling(POLL_INTERVAL_ACTIVE);
  try {
    const res = await fetch('/ota/fetch', { method: 'POST' });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error || 'Errore avvio download');
  } catch (err) {
    stopPolling();
    setProgress(0, 'Errore: ' + err.message, 'error');
    btnUpdate.disabled = false;
    releaseWakeLock();
  }
}

// ===== UPLOAD MANUALE =====
fileInput.addEventListener('change', function(e) {
  const file = e.target.files[0];
  if (!file) return;
  manualFile = file;
  const sizeMB = (file.size / 1024 / 1024).toFixed(2);
  fileInfo.textContent = file.name + ' (' + sizeMB + ' MB)';
  btnManualInstall.disabled = false;
});

btnManualInstall.addEventListener('click', async function() {
  if (!manualFile) return;
  btnManualInstall.disabled = true;
  progressCard.style.display = 'block';
  startTime = Date.now();
  await requestWakeLock();
  setProgress(0, 'Caricamento file...', 'working');
  const formData = new FormData();
  formData.append('firmware', manualFile, manualFile.name);
  try {
    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
        const percent = Math.round((e.loaded / e.total) * 90);
        setProgress(percent, 'Caricamento file...', 'working');
      }
    };
    xhr.onload = function() {
      if (xhr.status >= 200 && xhr.status < 300) {
        setProgress(90, 'File caricato, avvio installazione...', 'success');
        startPolling(POLL_INTERVAL_ACTIVE);
      } else {
        setProgress(0, 'Errore caricamento: HTTP ' + xhr.status, 'error');
        btnManualInstall.disabled = false;
        releaseWakeLock();
      }
    };
    xhr.onerror = function() {
      setProgress(0, 'Errore di rete', 'error');
      btnManualInstall.disabled = false;
      releaseWakeLock();
    };
    xhr.open('POST', '/ota/upload');
    xhr.send(formData);
  } catch (err) {
    setProgress(0, 'Errore: ' + err.message, 'error');
    btnManualInstall.disabled = false;
    releaseWakeLock();
  }
});

// ===== TOGGLE SEZIONE AVANZATA =====
toggleAdvanced.addEventListener('click', function() {
  const isOpen = advancedSection.classList.toggle('open');
  toggleAdvanced.textContent = isOpen ? 'Nascondi opzioni avanzate' : 'Mostra opzioni avanzate';
});

// ===== EVENTO PULSANTE PRINCIPALE =====
btnUpdate.addEventListener('click', function() {
  if (updateAvailable) startAutoUpdate();
  else checkForUpdate();
});

// ===== GESTIONE DISCONNESSIONE =====
window.addEventListener('offline', function() {
  if (polling) {
    stopPolling();
    statusMsg.textContent = 'Connessione persa. Riconnetti per continuare.';
  }
});

window.addEventListener('online', function() {
  if (progressCard.style.display === 'block' && !polling) {
    startPolling(POLL_INTERVAL_ACTIVE);
  }
});

// ===== INIZIALIZZAZIONE =====
(async function init() {
  await checkForUpdate();
  await pollStatus();
})();
</script>
</body>
</html>
)rawliteral";
}

    // === API OTA ===
    void handleOtaStatus() { server.send(200, "application/json", jsonOtaStatus_()); }

    void handleOtaStart() {
        if (!SD.exists(kOtaSDPath_)) {
            otaState_ = OtaState::ERROR; otaMsg_ = F("Nessun file su SD (/fw/update.bin)");
            otaProgress_ = 0;
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"file mancante\"}");
        } else {
            otaStartRequested_ = true;
            server.send(202, "application/json", "{\"ok\":true,\"state\":\"starting\"}");
        }
    }

    void handleOtaCheck() {
    String ver, url, sha;
    size_t size = 0;
    bool ok = ota_.checkForUpdate(kManifestURL_, FW_VERSION, ver, url, size, sha);

    updAvailable_ = ok;
    if (ok) {
        updVersion_ = ver;
        updUrl_     = url;
        updSize_    = size;
        updSha256_  = sha;

        // Log aggiornamento disponibile
        String infoMsg = String("OTA: Aggiornamento disponibile v") + ver +
                        " (attuale: " FW_VERSION "), dimensione: " +
                        String((unsigned long)(size/1024)) + " KB";
        sdLogger.info(infoMsg.c_str());
    } else {
        sdLogger.info("OTA: Nessun aggiornamento disponibile (versione corrente: " FW_VERSION ")");
    }

    String j = "{\"ok\":";
    j += ok ? "true" : "false";
    j += ",\"current\":\"" FW_VERSION "\"";
    if (ok) {
        j += ",\"version\":\"" + ver + "\"";
        j += ",\"size\":" + String((unsigned long)size);
    }
    j += "}";
    server.send(200, "application/json", j);
}


    void handleOtaUpload() {
    HTTPUpload& upload = server.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START: {
            String path = String(kOtaSDPath_);
            int slash = path.lastIndexOf('/');
            if (slash > 0) {
                String dir = path.substring(0, slash);
                if (!SD.exists(dir)) SD.mkdir(dir);
            }
            if (SD.exists(kOtaSDPath_)) SD.remove(kOtaSDPath_);

            otaState_         = OtaState::UPLOADING;
            otaMsg_           = F("uploading");
            otaProgress_      = 0;
            otaUploadedName_  = upload.filename;
            otaUploadedSize_  = 0;
            break;
        }

        case UPLOAD_FILE_WRITE: {
            File f = SD.open(kOtaSDPath_, FILE_APPEND);
            if (!f) {
                otaState_ = OtaState::ERROR;
                otaMsg_   = F("SD open fail");
                break;
            }
            size_t w = f.write(upload.buf, upload.currentSize);
            f.close();

            if (w != upload.currentSize) {
                otaState_ = OtaState::ERROR;
                otaMsg_   = F("SD write fail");
                break;
            }

            otaUploadedSize_ += w;

            // percentuale reale: 0..89 durante l'upload (lasciamo 90..100 al post)
            if (upload.totalSize > 0) {
                int pct = (int)((otaUploadedSize_ * 89ULL) / upload.totalSize);
                if (pct < 0)   pct = 0;
                if (pct > 89)  pct = 89;
                otaProgress_ = pct;
            }
            break;
        }

        case UPLOAD_FILE_END: {
            if (otaState_ != OtaState::ERROR) {
                otaState_    = OtaState::READY;
                otaMsg_      = F("file pronto");
                otaProgress_ = 90; // pronto all'installazione
            }
            break;
        }

        case UPLOAD_FILE_ABORTED: {
            otaState_    = OtaState::ERROR;
            otaMsg_      = F("upload aborted");
            otaProgress_ = 0;
            if (SD.exists(kOtaSDPath_)) SD.remove(kOtaSDPath_);
            break;
        }

        default: break;
    }
}

    // === Scarica direttamente il .bin da Internet su SD (NON BLOCCANTE) ===
    void handleOtaFetch() {
        if (!updAvailable_ || updUrl_.isEmpty()) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"nessun update disponibile\"}");
        return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            server.send(503, "application/json", "{\"ok\":false,\"error\":\"no internet\"}");
            return;
        }
        if (otaFetchTask_ != nullptr) {
            server.send(409, "application/json", "{\"ok\":false,\"error\":\"download giÃ  in corso\"}");
            return;
        }

        // Verifica spazio disponibile su SD (serve almeno 1.5x la dimensione del file)
        uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();
        uint64_t requiredSpace = (uint64_t)updSize_ + (updSize_ / 2); // 1.5x per sicurezza
        if (freeSpace < requiredSpace) {
            String errMsg = "{\"ok\":false,\"error\":\"Spazio SD insufficiente: ";
            errMsg += String((unsigned long)(freeSpace / 1024)) + " KB liberi, ";
            errMsg += String((unsigned long)(requiredSpace / 1024)) + " KB richiesti\"}";
            server.send(507, "application/json", errMsg);
            return;
        }

        // Log inizio download
        String downloadMsg = String("OTA: Avvio download v") + updVersion_ +
                            " da " + updUrl_ + " (" +
                            String((unsigned long)(updSize_/1024)) + " KB)";
        sdLogger.info(downloadMsg.c_str());

        // Stato iniziale per UI
        otaState_    = OtaState::UPLOADING;
        otaMsg_      = "Download";
        otaProgress_ = 0;

        // Prepara SD
        if (!SD.exists("/fw")) SD.mkdir("/fw");
        if (SD.exists(kOtaSDPath_)) SD.remove(kOtaSDPath_);

        // Avvio task (core 0; puoi usare 1)
        BaseType_t ok = xTaskCreatePinnedToCore(
            otaFetchTaskTrampoline, "otaFetch", 8192, this, 1, &otaFetchTask_, 0
        );

        if (ok != pdPASS) {
            otaFetchTask_ = nullptr;
            otaState_ = OtaState::ERROR;
            otaMsg_   = "task create failed";
            sdLogger.error("OTA: Errore creazione task download");
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"task create failed\"}");
            return;
        }

        // Risposta immediata: la pagina web puÃ² fare polling su /ota/status
        server.send(202, "application/json", "{\"ok\":true,\"started\":true}");
    }

    // ==================== (NUOVO) UI & API Wi-Fi ====================

    // GET /api/scan_wifi  â†’ scansione asincrona (non blocca l'AP)
    void handleScanWiFi() {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            server.sendHeader("Cache-Control","no-store");
            server.send(200, "application/json", R"({"running":true})");
            return;
        }
        if (n == -2) {
            WiFi.scanNetworks(true, /*hidden=*/true); // avvio asincrono
            server.sendHeader("Cache-Control","no-store");
            server.send(200, "application/json", R"({"started":true})");
            return;
        }

        String out;
        out.reserve(64 * (n > 0 ? n : 1));
        out += "[";
        for (int i = 0; i < n; ++i) {
            if (i) out += ",";
            out += "{";
            out += "\"ssid\":\""; out += WiFi.SSID(i); out += "\",";
            out += "\"rssi\":"; out += String(WiFi.RSSI(i)); out += ",";
            out += "\"bssid\":\""; out += WiFi.BSSIDstr(i); out += "\",";
            out += "\"channel\":"; out += String(WiFi.channel(i)); out += ",";
            out += "\"enc\":"; out += String((int)WiFi.encryptionType(i));
            out += "}";
        }
        out += "]";
        WiFi.scanDelete();
        server.sendHeader("Cache-Control","no-store");
        server.send(200, "application/json", out);
    }

    // POST /api/save_wifi â†’ salva credenziali e tenta subito la connessione (AP resta attivo)
    void handleSaveWiFi() {
        if (!server.hasArg("ssid")) {
            server.send(400, "application/json", R"({"ok":false,"error":"missing ssid"})");
            return;
        }
        const String ssid = server.arg("ssid");
        const String pwd  = server.hasArg("password") ? server.arg("password") : "";

        saveWiFiConfig(ssid, pwd);

        WiFi.mode(WIFI_AP_STA);
        WiFi.setSleep(false);
        WiFi.setHostname("twizydash");
        WiFi.begin(ssid.c_str(), pwd.c_str());

        server.sendHeader("Cache-Control","no-store");
        server.send(200, "application/json", R"({"ok":true})");
    }


public:
    WebConfig() : server(80) {}

    void begin() {
        loadWiFiConfig();

        // Callback OTA migliorata: aggiorna stato web e display (LVGL)
        ota_.onProgress([this](size_t done, size_t total, const char* phase){
            int pct = (total > 0) ? (int)((done * 100ULL) / total) : 0;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;

            // Stato web - SEMPRE aggiornato per polling HTTP
            otaProgress_ = pct;
            if (phase && *phase) otaMsg_ = String(phase);

            // Throttling per LVGL
            static int lastLvglPct = -1;
            static unsigned long lastLvglUpdate = 0;
            static String lastLvglPhase = "";

            unsigned long now = millis();
            String currentPhase = phase ? String(phase) : "";

            // ⚠️ DURANTE INSTALLAZIONE: NON TOCCARE LVGL! (conflitto accesso flash)
            if (currentPhase == "installing" || currentPhase == "install") {
                return; // ← ESCI SENZA TOCCARE LVGL!
            }

            // Per download/verify: aggiorna LVGL normalmente
            bool shouldUpdateLvgl =
                abs(pct - lastLvglPct) >= 5 ||
                (now - lastLvglUpdate) > 1000 ||
                currentPhase != lastLvglPhase ||
                lastLvglPct == -1;

            if (shouldUpdateLvgl) {
                OTAView::begin();

                String statusMsg;
                if (phase) {
                    if (!strcmp(phase, "download") || !strcmp(phase, "downloading")) statusMsg = "Download " + String(pct) + "%";
                    else if (!strcmp(phase, "verify") || !strcmp(phase, "verifying")) statusMsg = "Verifica " + String(pct) + "%";
                    else statusMsg = String(phase) + " " + String(pct) + "%";
                } else statusMsg = "Progresso " + String(pct) + "%";

                OTAView::setProgress(done, total, statusMsg.c_str());
                OTAView::show();
                lv_timer_handler();
                delay(1);

                lastLvglPct = pct;
                lastLvglUpdate = now;
                lastLvglPhase = currentPhase;
            }
        });
    }

  

    // Avvio AP e portal
    void startSimpleAP() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);

        // Pagine principali
        server.on("/",        [this](){ server.send(200, "text/html", getMainConfigPage()); });
        server.on("/wizard",  [this](){ server.send(200, "text/html", getWizardPage()); });
        server.on("/manual",  [this](){ server.send(200, "text/html", getManualPage()); });
        server.on("/stats",   [this](){ server.send(200, "text/html", getStatsPage()); });

        // (NUOVE) UI/API Wi-Fi
        server.on("/wifi",            [this](){ server.send(200, "text/html", getWiFiPage()); });
        server.on("/api/scan_wifi", HTTP_GET,  [this](){ handleScanWiFi(); });
        server.on("/api/save_wifi", HTTP_POST, [this](){ handleSaveWiFi(); });
        server.on("/api/net", HTTP_GET, [this](){ handleNetStatus(); });

        // API base
        server.on("/api/config",   HTTP_GET,  [this](){ handleGetConfig(); });
        server.on("/api/config",   HTTP_POST, [this](){ handleSetConfig(); });
        server.on("/api/status",   HTTP_GET,  [this](){ handleGetStatus(); });
        server.on("/api/wizard",   HTTP_POST, [this](){ handleWizardConfig(); });
        server.on("/api/reset",    HTTP_POST, [this](){ handleReset(); });

        // Auto-learning
        server.on("/api/enable_auto", HTTP_POST, [this](){ handleEnableAuto(); });
        server.on("/api/reset_auto",  HTTP_POST, [this](){ handleResetAuto(); });

        // CSV
        server.on("/api/download_csv", HTTP_GET, [this](){ handleDownloadCSV(); });
        server.on("/api/view_csv",     HTTP_GET, [this](){ handleViewCSV(); });
        server.on("/api/csv_info",     HTTP_GET, [this](){ handleCSVInfo(); });
        server.on("/csv",              [this](){ handleViewCSV(); });
        server.on("/api/stats",        HTTP_GET, [this](){
            #if HAS_AUTO_LEARNING
            server.send(200, "text/html", consumptionLearner.getStatsHTML());
            #else
            server.send(501, "text/html", "<p>Auto-apprendimento non disponibile</p>");
            #endif
        });

        // Odometro (protetto password)
        server.on("/api/odometer",       HTTP_GET,  [this](){ handleGetOdometer(); });
        server.on("/api/set_odometer",   HTTP_POST, [this](){ handleSetOdometer(); });
        server.on("/api/reset_odo_pwd",  HTTP_POST, [this](){ handleResetOdoPassword(); });

        // OTA
        server.on("/ota",           [this](){ server.send(200, "text/html", getOtaPage()); });
        server.on("/ota/status",  HTTP_GET,  [this](){ handleOtaStatus(); });
        server.on("/ota/start",   HTTP_POST, [this](){ handleOtaStart(); });
        server.on("/ota/check",   HTTP_GET,  [this](){ handleOtaCheck(); });
        server.on("/ota/fetch",   HTTP_POST, [this](){ handleOtaFetch(); });
        server.on("/ota/upload",  HTTP_POST,
            [this]() {
    // Leggi eventuale SHA atteso inviato dal form
    if (server.hasArg("sha256")) {
        updSha256_ = server.arg("sha256");
        updSha256_.trim();
    }
    if (otaState_ == OtaState::READY) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        String err = "{\"ok\":false,\"error\":\"" + otaMsg_ + "\"}";
        server.send(500, "application/json", err);
    }
},

            [this]() { handleOtaUpload(); }
        );

        // NotFound â†’ dashboard
        server.onNotFound([this](){ server.send(200, "text/html", getMainConfigPage()); });
        
        // Versione firmware corrente
        server.on("/api/version", HTTP_GET, [this](){
            String out = String("{\"version\":\"") + FW_VERSION + "\"}";
            server.send(200, "application/json", out);
        });

        server.begin();
        serverRunning = true;
    }

    void handleClient() { if (serverRunning) server.handleClient(); }

    // STA (se vuoi anche modalitÃ  Internet)
    void connectToWiFi() {
        if (wifiSSID.isEmpty()) return;
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(500);
        }
    }

    bool isConnected() { return WiFi.status() == WL_CONNECTED && !configMode; }

    // Helper pubblico per sapere se l'OTA Ã¨ in corso (per bloccare lv_scr_load() altrove)
    bool isOtaActive() const {
        return otaState_ == OtaState::VERIFYING || otaState_ == OtaState::INSTALLING;
    }

    // Avanzamento OTA non bloccante
    void tick() {
        static unsigned long lastTick = 0;
        unsigned long now = millis();

        if (now - lastTick < 200) {
            if (serverRunning) server.handleClient();
            return;
        }
        lastTick = now;

        // ✅ FIX: Controlla otaStartRequested_ PRIMA dello switch
        // Questo permette la transizione READY → VERIFYING nello stesso tick
        if (otaStartRequested_) {
            otaStartRequested_ = false;
            OTAView::begin();
            OTAView::setStatus("Preparazione...");
            OTAView::setProgress(0, 100, "preparing");
            OTAView::show();

            otaState_ = OtaState::VERIFYING;
            otaMsg_ = "Preparazione";
            otaProgress_ = 0;
        }

        switch (otaState_) {
            case OtaState::IDLE:
            case OtaState::UPLOADING:
            case OtaState::READY:
            case OtaState::ERROR:
                break;

            case OtaState::VERIFYING: {
    OTAView::begin();
    OTAView::setStatus("Verifica file...");
    OTAView::setProgress(5, 100, "verifying");
    OTAView::show();

    bool ok = true;

    if (updSha256_.length() > 0) {
        // Percorso ONLINE (o upload con SHA fornita dal form) → verifica contro expected
        ok = ota_.verifySha256OnSD(kOtaSDPath_, updSha256_);
    } else {
        // Percorso UPLOAD senza SHA attesa: calcola e logga l’hash (per trasparenza)
        uint8_t hash[32];
        if (!ota_.computeSha256SD(kOtaSDPath_, hash)) ok = false;
        else {
            // calcola hex e memorizza
            String actual;
            static const char* hex = "0123456789abcdef";
            actual.reserve(64);
            for (int i = 0; i < 32; ++i) {
                actual += hex[(hash[i] >> 4) & 0x0F];
                actual += hex[hash[i] & 0x0F];
            }
            lastComputedSha256_ = actual;
        }
    }

    if (!ok) {
        otaState_    = OtaState::ERROR;
        otaMsg_      = F("Verifica fallita");
        otaProgress_ = 0;
        OTAView::setStatus("Verifica fallita");
        OTAView::setProgress(0, 100, "error");
        OTAView::show();
        break;
    }

    otaState_    = OtaState::INSTALLING;
    otaMsg_      = F("Installazione");
    otaProgress_ = 10;
    break;
}


            case OtaState::INSTALLING: {
                // ✅ MOSTRA SCHERMATA STATICA PRIMA DELL'INSTALLAZIONE
                OTAView::begin();
                OTAView::setStatus("Installazione firmware...");
                OTAView::setProgress(1, 2, "preparing");  // 50% visivo
                OTAView::showWarning(true);  // ⚠️ MOSTRA AVVISO "NON SPEGNERE"
                OTAView::show();

                // Renderizza tutto ORA (prima di bloccare LVGL)
                lv_timer_handler();
                delay(500);  // Dai tempo all'utente di vedere il messaggio

                // Log inizio installazione
                String installMsg = "OTA: Avvio installazione firmware v" + updVersion_;
                sdLogger.info(installMsg.c_str());

                // Stato web per polling HTTP
                otaMsg_ = "Installazione firmware in corso";
                otaProgress_ = 10;

                // ⚠️ DA QUI IN POI: NO CHIAMATE A LVGL!
                bool ok = ota_.installFromSD(kOtaSDPath_);

                // ✅ INSTALLAZIONE COMPLETA: sicuro usare LVGL di nuovo

                if (ok) {
                    otaState_ = OtaState::DONE;
                    otaProgress_ = 100;
                    otaMsg_ = "Completato";
                    sdLogger.info("OTA: Installazione completata con successo! Riavvio in corso...");

                    // CLEANUP AUTOMATICO DEL FIRMWARE
                    if (SD.exists(kOtaSDPath_)) {
                        SD.remove(kOtaSDPath_);
                    }

                    // Mostra completamento
                    OTAView::begin();
                    OTAView::setStatus("Completato!");
                    OTAView::setProgress(100, 100, "done");
                    OTAView::showWarning(false);  // Nascondi avviso
                    OTAView::show();
                    lv_timer_handler();
                } else {
                    otaState_ = OtaState::ERROR;
                    otaProgress_ = 0;
                    otaMsg_ = "Errore installazione";
                    sdLogger.error("OTA: Installazione fallita! File mantenuto su SD per retry.");

                    // In caso di errore: MANTIENI il file per retry

                    OTAView::begin();
                    OTAView::setStatus("Errore installazione!");
                    OTAView::showWarning(false);  // Nascondi avviso
                    OTAView::show();
                    lv_timer_handler();
                }
                break;
            }

            case OtaState::DONE: {
                static unsigned long doneTime = 0;
                if (doneTime == 0) {
                    doneTime = now;
                }
                if (now - doneTime > 3000) {
                    ESP.restart();
                }
                break;
            }
        }

        if (serverRunning) server.handleClient();
    }
};

extern WebConfig webConfig;

#endif // WEB_CONFIG_H