// src/web_config.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

#include "web_config.h"
#include "battery_config.h"

// Istanza globale del portale
// Definizioni dei membri statici di WebConfig
String WebConfig::lastComputedSha256_ = "";

WebConfig webConfig;

// -----------------------------------------------------------------------------
// Supporto STA (AP + STA): avvia AP + portale, poi tenta la connessione STA
// leggendo le credenziali già salvate in Preferences ("wifi_cfg"/ssid,password).
// Così il telefono resta collegato all'AP del device, ma il device usa la STA
// per andare su Internet (es. scaricare OTA da GitHub).
// -----------------------------------------------------------------------------

namespace {

// credenziali salvate (copiate da Preferences)
String g_ssid;
String g_pwd;

Preferences g_prefs;

// Event log utile in seriale
void registerWiFiEvents()
{
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    // Events registered but not printed to save space
  });
}

void loadSavedStaCreds()
{
  g_prefs.begin("wifi_cfg", /*readOnly=*/true);
  g_ssid = g_prefs.getString("ssid", "");
  g_pwd  = g_prefs.getString("password", "");
  g_prefs.end();
}

void tryConnectSTA(unsigned long timeoutMs = 15000)
{
  if (!g_ssid.length()) return;

  // abilita STA mantenendo l'AP attivo
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("twizydash");

  // Avvia connessione in modo ASINCRONO - non blocca il boot
  // La pagina /wifi monitora lo stato tramite polling /api/net
  WiFi.begin(g_ssid.c_str(), g_pwd.c_str());

  // NON aspettiamo la connessione - il WiFi si connetterà in background
  // Questo evita di bloccare il boot per 15 secondi quando la rete non è disponibile
}

} // namespace

// -----------------------------------------------------------------------------
// Handler/HTML aggiuntivi definiti qui (dichiarati nella classe in web_config.h)
// -----------------------------------------------------------------------------

// Stato rete: usato dalla pagina /wifi per mostrare "Connesso: IP ..." o "Connessione in corso..."
void WebConfig::handleNetStatus() {
  bool sta_connected = (WiFi.status() == WL_CONNECTED);
  String j = "{";
  j += "\"mode\":\"" + String(WiFi.getMode() == WIFI_AP_STA ? "AP_STA" :
                               WiFi.getMode() == WIFI_AP ? "AP" :
                               WiFi.getMode() == WIFI_STA ? "STA" : "OFF") + "\",";
  j += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  j += "\"sta_connected\":" + String(sta_connected ? "true" : "false") + ",";
  j += "\"sta_ip\":\""   + (sta_connected ? WiFi.localIP().toString()   : String("")) + "\",";
  j += "\"gw\":\""       + (sta_connected ? WiFi.gatewayIP().toString() : String("")) + "\",";
  j += "\"dns\":\""      + (sta_connected ? WiFi.dnsIP().toString()     : String("")) + "\"";
  j += "}";
  server.sendHeader("Cache-Control","no-store");
  server.send(200, "application/json", j);
}

// Pagina /wifi completa con polling a /api/net

// -----------------------------------------------------------------------------
// API da usare nel tuo main.cpp
// -----------------------------------------------------------------------------

// Chiama questa in setup():
void WebConfigStartAP_STA()
{
  // 1) Inizializza il portale (handler HTTP, OTA callbacks, ecc.)
  webConfig.begin();

  // 2) Avvia AP + server HTTP (rimane sempre attivo)
  webConfig.startSimpleAP();

  // Log eventi Wi-Fi
  registerWiFiEvents();

  // 3) Carica credenziali salvate e prova la connessione STA
  loadSavedStaCreds();

  // Forza AP+STA e ottimizza la stabilità
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("twizydash");

  // prima scansione asincrona per la UI /wifi
  WiFi.scanNetworks(true, /*hidden=*/true);

  if (g_ssid.length()) {
    // Passiamo a AP+STA e tentiamo il collegamento
    tryConnectSTA();
  }
}

// Chiama questa nel loop():
void WebConfigTask()
{
  // gestisce richieste HTTP e stato OTA
  webConfig.handleClient();
  webConfig.tick();
}

// Pagina /wifi completa con polling a /api/net
String WebConfig::getWiFiPage() {
  return String(F(R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Gestione WiFi</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
    .container{max-width:600px;margin:0 auto;background:rgba(255,255,255,0.95);backdrop-filter:blur(10px);border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,.1);overflow:hidden}
    .header{background:linear-gradient(135deg,#2196F3,#21CBF3);color:#fff;padding:24px;text-align:center}
    .header h1{font-size:24px;font-weight:600;margin-bottom:4px}
    .header-subtitle{font-size:12px;opacity:0.9}
    .content{padding:20px}
    .card{background:linear-gradient(135deg,#f5f7fa,#c3cfe2);padding:18px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,.08);margin-bottom:14px}
    .card-title{font-size:11px;color:#666;font-weight:700;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between}
    .badge{display:inline-block;padding:3px 8px;border-radius:10px;font-size:9px;font-weight:700;text-transform:uppercase}
    .badge-success{background:#d4edda;color:#155724}
    .badge-secondary{background:#e2e3e5;color:#383d41}
    .status-box{background:#fff;padding:12px;border-radius:10px;margin-bottom:12px}
    .status-label{font-size:11px;color:#666;text-transform:uppercase;margin-bottom:4px}
    .status-value{font-size:14px;font-weight:700;color:#2c3e50}
    .network-list{margin:12px 0}
    .network-item{background:#fff;padding:14px;border-radius:10px;margin-bottom:8px;cursor:pointer;transition:transform 0.1s,box-shadow 0.2s;display:flex;align-items:center;justify-content:space-between}
    .network-item:hover{transform:translateY(-2px);box-shadow:0 4px 12px rgba(0,0,0,.1)}
    .network-item:active{transform:scale(0.98)}
    .network-info{flex:1}
    .network-ssid{font-size:15px;font-weight:700;color:#2c3e50;margin-bottom:4px}
    .network-details{font-size:11px;color:#7f8c8d}
    .signal-bars{display:flex;gap:2px;align-items:flex-end;height:20px;margin-left:12px}
    .signal-bar{width:4px;background:#ccc;border-radius:2px}
    .signal-bar.active{background:#28a745}
    .lock-icon{color:#6c757d;margin-left:8px;font-size:14px}
    .form-group{margin-bottom:14px}
    .form-label{font-size:11px;color:#666;font-weight:600;text-transform:uppercase;margin-bottom:6px;display:block}
    .form-control{width:100%;padding:12px;border:2px solid #ddd;border-radius:10px;font-size:14px;transition:border-color 0.2s}
    .form-control:focus{outline:none;border-color:#3498db}
    .input-group{position:relative}
    .toggle-password{position:absolute;right:12px;top:50%;transform:translateY(-50%);cursor:pointer;color:#7f8c8d;font-size:18px;user-select:none}
    .btn{padding:14px;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:13px;width:100%;transition:transform 0.1s,opacity 0.2s}
    .btn:active{transform:scale(0.98)}
    .btn-primary{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff;margin-bottom:8px}
    .btn-secondary{background:#6c757d;color:#fff}
    .btn-scan{background:#17a2b8;color:#fff;margin-bottom:12px}
    .btn:disabled{opacity:0.5;cursor:not-allowed;transform:none}
    .message{padding:12px;border-radius:10px;margin-top:12px;font-size:13px}
    .message-info{background:#d1ecf1;color:#0c5460}
    .message-success{background:#d4edda;color:#155724}
    .message-error{background:#f8d7da;color:#721c24}
    .spinner{display:inline-block;width:14px;height:14px;border:2px solid #fff;border-top-color:transparent;border-radius:50%;animation:spin 0.6s linear infinite;margin-left:8px}
    @keyframes spin{to{transform:rotate(360deg)}}
    .empty-state{text-align:center;padding:24px;color:#7f8c8d;font-size:13px}
    @media(max-width:520px){.network-item{flex-direction:column;align-items:flex-start}.signal-bars{margin-left:0;margin-top:8px}}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Gestione WiFi</h1>
      <div class="header-subtitle">Connessione e Configurazione Rete</div>
    </div>
    <div class="content">
      <div class="card">
        <div class="card-title"><span>Stato Connessione</span><span class="badge badge-secondary" id="statusBadge">Controllo...</span></div>
        <div class="status-box" id="statusBox"><div class="status-label">Stato</div><div class="status-value" id="statusText">Verifica in corso...</div></div>
      </div>
      <div class="card">
        <div class="card-title"><span>Reti Disponibili</span></div>
        <button class="btn btn-scan" id="btnScan" onclick="startScan()"><span id="scanText">Scansiona Reti</span></button>
        <div class="network-list" id="networkList"><div class="empty-state">Clicca "Scansiona Reti" per cercare reti WiFi vicine</div></div>
      </div>
      <div class="card">
        <div class="card-title">Connetti a Rete</div>
        <div class="form-group"><label class="form-label">Nome Rete (SSID)</label><input type="text" class="form-control" id="ssid" placeholder="Inserisci o seleziona dalla lista"></div>
        <div class="form-group"><label class="form-label">Password</label><div class="input-group"><input type="password" class="form-control" id="password" placeholder="Inserisci password"><span class="toggle-password" onclick="togglePassword()">&#128065;</span></div></div>
        <button class="btn btn-primary" id="btnConnect" onclick="connectWiFi()">Salva e Connetti</button>
        <button class="btn btn-secondary" onclick="location.href='/'">Torna alla Dashboard</button>
        <div id="message"></div>
      </div>
    </div>
  </div>
<script>
let scanTimer=null,statusTimer=null;
function getSignalBars(rssi){const bars=rssi>=-50?4:rssi>=-60?3:rssi>=-70?2:1;let html='<div class="signal-bars">';for(let i=0;i<4;i++){const h=(i+1)*5;html+=`<div class="signal-bar ${i<bars?'active':''}" style="height:${h}px"></div>`;}html+='</div>';return html;}
async function startScan(){const btn=document.getElementById('btnScan'),list=document.getElementById('networkList');btn.disabled=true;document.getElementById('scanText').innerHTML='Scansione in corso<span class="spinner"></span>';list.innerHTML='<div class="empty-state">Ricerca reti in corso...</div>';try{await fetch('/api/scan_wifi');setTimeout(pollScan,1000);}catch(e){showMessage('Errore durante scansione','error');btn.disabled=false;document.getElementById('scanText').textContent='Scansiona Reti';}}
async function pollScan(){try{const r=await fetch('/api/scan_wifi'),data=await r.json();if(data.started||data.running){setTimeout(pollScan,1200);return;}const list=document.getElementById('networkList');if(!data||data.length===0){list.innerHTML='<div class="empty-state">Nessuna rete trovata</div>';}else{const sorted=data.sort((a,b)=>b.rssi-a.rssi);list.innerHTML='';sorted.forEach(n=>{const div=document.createElement('div');div.className='network-item';const lock=n.enc&&n.enc!==0?'<span class="lock-icon">&#128274;</span>':'';const ssid=n.ssid||'<Rete nascosta>',channel=`Ch ${n.channel}`,rssi=`${n.rssi} dBm`;div.innerHTML=`<div class="network-info"><div class="network-ssid">${ssid}${lock}</div><div class="network-details">${channel} • ${rssi}</div></div>${getSignalBars(n.rssi)}`;div.onclick=()=>{if(n.ssid){document.getElementById('ssid').value=n.ssid;document.getElementById('password').focus();}};list.appendChild(div);});}document.getElementById('btnScan').disabled=false;document.getElementById('scanText').textContent='Scansiona Reti';}catch(e){showMessage('Errore lettura risultati','error');document.getElementById('btnScan').disabled=false;document.getElementById('scanText').textContent='Scansiona Reti';}}
async function pollStatus(){try{const r=await fetch('/api/net'),data=await r.json(),badge=document.getElementById('statusBadge'),box=document.getElementById('statusBox');if(data.sta_connected){badge.textContent='Connesso';badge.className='badge badge-success';box.innerHTML=`<div class="status-label">SSID</div><div class="status-value">${data.sta_ssid||'N/D'}</div><div class="status-label" style="margin-top:8px">Indirizzo IP</div><div class="status-value">${data.sta_ip||'N/D'}</div>`;if(statusTimer){clearInterval(statusTimer);statusTimer=null;}}else{badge.textContent='Non connesso';badge.className='badge badge-secondary';box.innerHTML=`<div class="status-label">Stato</div><div class="status-value">Access Point attivo (${data.ap_ip||'192.168.4.1'})</div>`;}}catch(e){}}
async function connectWiFi(){const ssid=document.getElementById('ssid').value.trim(),password=document.getElementById('password').value;if(!ssid){showMessage('Inserisci il nome della rete','error');return;}const btn=document.getElementById('btnConnect');btn.disabled=true;btn.textContent='Connessione in corso...';try{const fd=new FormData();fd.append('ssid',ssid);fd.append('password',password);await fetch('/api/save_wifi',{method:'POST',body:fd});showMessage('Credenziali salvate. Connessione in corso...','info');if(statusTimer)clearInterval(statusTimer);statusTimer=setInterval(pollStatus,2000);pollStatus();setTimeout(()=>{btn.disabled=false;btn.textContent='Salva e Connetti';},3000);}catch(e){showMessage('Errore durante salvataggio','error');btn.disabled=false;btn.textContent='Salva e Connetti';}}
function togglePassword(){const input=document.getElementById('password');input.type=input.type==='password'?'text':'password';}
function showMessage(text,type){const msg=document.getElementById('message');msg.textContent=text;msg.className=`message message-${type}`;setTimeout(()=>{msg.textContent='';msg.className='';},5000);}
pollStatus();setInterval(pollStatus,10000);
</script>
</body>
</html>
)rawliteral"));
}
