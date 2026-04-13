#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <time.h>
#include <algorithm>
#include "FS.h"
#include <LittleFS.h>

static const char* COMPANY_NAME = "GroWise Pvt. Ltd.";
static const char* COMPANY_SHORT = "GroWise";
static const char* FW_VERSION = "v4.0";
static const char* DEFAULT_DEVICE_NAME = "GroWise-Hydro-01";
static const char* PREFS_NS = "growise";
static const char* WIFI_PORTAL_SSID = "GroWise-Setup";
static const char* WIFI_PORTAL_PASS = "growise123";
static const char* LOG_DIR = "/logs";
static const char* NTP_PRIMARY = "pool.ntp.org";
static const char* NTP_SECONDARY = "time.nist.gov";
static const long GMT_OFFSET_SEC = 19800;

static const uint32_t SENSOR_INTERVAL_MS = 2000UL;
static const uint32_t DISPLAY_INTERVAL_MS = 1000UL;
static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000UL;
static const uint32_t WIFI_SWITCH_TIMEOUT_MS = 20000UL;
static const uint32_t NTP_RESYNC_INTERVAL_MS = 21600000UL;
static const float ADC_REF_VOLTAGE = 3.30f;
static const float ADC_MAX_VALUE = 4095.0f;

#define ONE_WIRE_BUS 4
#define FLOAT_PIN 27
#define TDS_PIN 34
#define PH_PIN 39
#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SH1106G display(128, 64, &Wire, -1);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
WebServer server(80);
Preferences prefs;
WiFiManager wm;

struct RuntimeConfig {
  char deviceName[32];
  float tdsFactor;
  float tdsVoltageOffset;
  float ecFactor;
  float phNeutralVoltage;
  float phVoltagePerPH;
  float phOffset;
  uint32_t logIntervalSec;
};

struct SensorSnapshot {
  float temperatureC;
  bool temperatureValid;
  bool waterLevelFull;
  float tdsPpm;
  float ecUsCm;
  float phValue;
  float tdsVoltage;
  float phVoltage;
  uint16_t rawTds;
  uint16_t rawPh;
  uint32_t sampleCounter;
};

static RuntimeConfig g_cfg = {};
static SensorSnapshot g_data = {};

static bool g_oledWorking = false;
static bool g_fsOK = false;
static bool g_wifiConnected = false;
static bool g_timeConfigured = false;
static bool g_serversStarted = false;
static bool g_otaReady = false;
static uint16_t g_lastDisconnectReason = 0;

static String g_pendingSsid;
static String g_pendingPass;
static String g_targetSsid;
static bool g_wifiConnectRequested = false;
static bool g_wifiSwitchInProgress = false;
static uint32_t g_wifiSwitchDeadlineMs = 0;
static uint32_t g_lastWiFiRetryMs = 0;
static bool g_restartToPortalRequested = false;

static uint32_t g_lastSensorMs = 0;
static uint32_t g_lastDisplayMs = 0;
static uint32_t g_lastLogMs = 0;
static uint32_t g_lastNtpSyncMs = 0;
static char g_hostname[32] = { 0 };

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GroWise Hydro Console</title>
<style>
:root{--leaf:#26a388;--leaf2:#0f6c59;--bg:#091217;--card:rgba(13,25,31,.82);--text:#ebf7f3;--muted:#96b9ae;--line:rgba(71,202,169,.18);--accent:#dffcf4;--warm:#f1c27a}
*{box-sizing:border-box}body{margin:0;color:var(--text);font-family:"Trebuchet MS","Segoe UI",sans-serif;background:radial-gradient(circle at top left,rgba(38,163,136,.22),transparent 34%),linear-gradient(135deg,#061014 0,#0b1820 55%,#11222a 100%)}
.wrap{max-width:1180px;margin:auto;padding:22px 16px 40px}.hero,.grid{display:grid;gap:16px}.hero{grid-template-columns:minmax(0,1.3fr) minmax(300px,.9fr)}.grid{grid-template-columns:minmax(0,1.45fr) minmax(310px,.95fr);margin-top:16px}
.panel{background:var(--card);border:1px solid var(--line);border-radius:24px;padding:18px;backdrop-filter:blur(10px);box-shadow:0 22px 54px rgba(0,0,0,.22)}.brand{display:flex;gap:16px;align-items:center}.brand h1{margin:0 0 6px;font-size:2rem}.muted{color:var(--muted);line-height:1.55}
.badges,.cards,.actions,.wifi-row,.list{display:flex;flex-wrap:wrap;gap:10px}.pill,.mini{border:1px solid var(--line);background:rgba(255,255,255,.04);border-radius:999px;padding:10px 14px}.cards{margin-top:12px}.card{flex:1 1 140px;min-width:140px;background:rgba(255,255,255,.04);border:1px solid var(--line);border-radius:18px;padding:14px}.card small{display:block;color:var(--muted);margin-bottom:6px;text-transform:uppercase;font-size:.72rem;letter-spacing:.08em}.card strong{font-size:1.75rem}
.chart{margin-top:14px;border:1px solid var(--line);border-radius:18px;padding:12px;background:rgba(255,255,255,.03)}.rowline{display:flex;justify-content:space-between;gap:8px;color:var(--muted);font-size:.88rem}.spark{width:100%;height:90px;display:block}
form{display:grid;gap:10px;margin-top:12px}.field2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}label{display:block;color:var(--muted);font-size:.82rem;margin-bottom:5px}input,button,a.btn{width:100%;border:none;border-radius:14px;padding:12px 13px;font-size:.95rem;font-family:inherit}
input{color:var(--text);background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08)}button,a.btn{text-decoration:none;text-align:center;cursor:pointer;font-weight:700;color:#08241d;background:linear-gradient(135deg,var(--accent),#57d8bb)}button.alt{color:var(--text);background:rgba(255,255,255,.07);border:1px solid var(--line)}button.warn{color:#fff7ea;background:linear-gradient(135deg,#a85611,#e29d4d)}
.list{flex-direction:column;margin-top:12px}.item{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:13px 14px;border:1px solid var(--line);background:rgba(255,255,255,.04);border-radius:16px}.note{margin-top:14px;padding:13px 14px;border-radius:16px;border:1px dashed rgba(255,255,255,.12);color:var(--muted);line-height:1.55}
.toast{position:fixed;right:16px;bottom:16px;max-width:340px;padding:13px 14px;border-radius:16px;background:rgba(7,16,21,.94);border:1px solid var(--line);opacity:0;transform:translateY(10px);pointer-events:none;transition:.2s}.toast.show{opacity:1;transform:none}
@media (max-width:960px){.hero,.grid,.field2{grid-template-columns:1fr}}
</style></head><body><div class="wrap">
<section class="hero">
<div class="panel">
<div class="brand">
<svg width="92" height="92" viewBox="0 0 120 120" aria-hidden="true"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#37c5a3"/><stop offset="100%" stop-color="#1d8d74"/></linearGradient></defs><path d="M73 10c-22 12-35 30-35 52 0 12 3 23 11 34 4-13 9-22 18-28 7-6 18-8 25-11 12-5 18-13 21-24 1-7 1-14 0-23-15 1-28 4-40 10z" fill="url(#g)"/><path d="M63 31c-5 8-10 17-12 29" stroke="#eefdf8" stroke-width="3.5" stroke-linecap="round"/><path d="M51 58v29M51 87L31 103M51 87l20 16" stroke="#26a388" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/><circle cx="31" cy="103" r="8" fill="none" stroke="#26a388" stroke-width="6"/><circle cx="51" cy="87" r="8" fill="none" stroke="#26a388" stroke-width="6"/><circle cx="71" cy="103" r="8" fill="none" stroke="#26a388" stroke-width="6"/><circle cx="18" cy="76" r="7" fill="none" stroke="#26a388" stroke-width="6"/><circle cx="84" cy="76" r="7" fill="none" stroke="#26a388" stroke-width="6"/><path d="M31 103L18 76M71 103L84 76" stroke="#26a388" stroke-width="6" stroke-linecap="round"/></svg>
<div><h1>GroWise Hydro Console</h1><p class="muted">Live monitoring, Wi-Fi provisioning, calibration control, and daily CSV capture for your production hydroponics node.</p></div>
</div>
<p class="muted">Built for GroWise Pvt. Ltd. to make field operation simpler: see live sensor health, adjust pH and TDS calibration without reflashing, and export data later for AI or analytics.</p>
<div class="badges" style="margin-top:14px"><div class="pill" id="fwBadge">Firmware: v4.0</div><div class="pill" id="deviceBadge">Device: --</div><div class="pill" id="wifiBadge">WiFi: starting...</div><div class="pill" id="timeBadge">Time: waiting...</div></div>
</div>
<div class="panel">
<div class="cards">
<div class="card"><small>Temperature</small><strong id="tempValue">--</strong><div class="muted">degrees Celsius</div></div>
<div class="card"><small>Water Level</small><strong id="levelValue">--</strong><div class="muted">float switch state</div></div>
<div class="card"><small>pH</small><strong id="phValue">--</strong><div class="muted">calibrated reading</div></div>
<div class="card"><small>TDS</small><strong id="tdsValue">--</strong><div class="muted">ppm</div></div>
<div class="card"><small>EC</small><strong id="ecValue">--</strong><div class="muted">uS/cm</div></div>
<div class="card"><small>Signal</small><strong id="rssiValue">--</strong><div class="muted" id="logValue">Daily log pending</div></div>
</div>
</div>
</section>
<section class="grid">
<div>
<div class="panel">
<div class="rowline"><span>Recent sensor trend</span><span id="uptimeValue">Uptime --</span></div>
<div class="chart"><div class="rowline"><span>Temperature</span><span id="tempMeta">--</span></div><svg class="spark" viewBox="0 0 420 90" preserveAspectRatio="none"><polyline id="tempLine" fill="none" stroke="#7effd3" stroke-width="3" points=""></polyline></svg></div>
<div class="chart"><div class="rowline"><span>TDS</span><span id="tdsMeta">--</span></div><svg class="spark" viewBox="0 0 420 90" preserveAspectRatio="none"><polyline id="tdsLine" fill="none" stroke="#ffd27d" stroke-width="3" points=""></polyline></svg></div>
<div class="chart"><div class="rowline"><span>pH</span><span id="phMeta">--</span></div><svg class="spark" viewBox="0 0 420 90" preserveAspectRatio="none"><polyline id="phLine" fill="none" stroke="#7bbcff" stroke-width="3" points=""></polyline></svg></div>
<div class="note">This dashboard keeps the UI simple for operators while exposing the calibration values you need for a saleable device workflow.</div>
</div>
<div class="panel" style="margin-top:16px">
<div class="rowline"><span>Wi-Fi management</span><span id="wifiStateText">Waiting...</span></div>
<p class="muted">Provision networks without hardcoding credentials. If a network switch fails, the firmware reopens GroWise setup mode automatically after restart.</p>
<div class="wifi-row" style="margin-top:12px"><div style="flex:1 1 220px"><label for="ssidInput">SSID</label><input id="ssidInput" placeholder="Select a network or type a hidden SSID"></div><div style="flex:1 1 220px"><label for="passInput">Password</label><input id="passInput" type="password" placeholder="Leave blank for open network"></div></div>
<div class="actions" style="margin-top:12px"><button type="button" onclick="scanWifi()">Scan Nearby Wi-Fi</button><button type="button" class="alt" onclick="connectWifi()">Switch Network</button><button type="button" class="warn" onclick="resetWifi()">Reset Saved Wi-Fi</button></div>
<div class="list" id="networkList"></div>
</div>
</div>
<div>
<div class="panel">
<div class="rowline"><span>Device settings</span><span>Saved in flash</span></div>
<form id="configForm">
<div><label for="deviceName">Device name</label><input id="deviceName" name="deviceName" maxlength="31"></div>
<div class="field2"><div><label for="logIntervalSec">Log interval (seconds)</label><input id="logIntervalSec" name="logIntervalSec" type="number" min="10" max="3600" step="1"></div><div><label for="ecFactor">EC factor</label><input id="ecFactor" name="ecFactor" type="number" min="0.1" max="5.0" step="0.01"></div></div>
<div class="field2"><div><label for="tdsFactor">TDS calibration factor</label><input id="tdsFactor" name="tdsFactor" type="number" min="0.05" max="5.0" step="0.01"></div><div><label for="tdsVoltageOffset">TDS voltage offset</label><input id="tdsVoltageOffset" name="tdsVoltageOffset" type="number" min="-1.0" max="1.0" step="0.01"></div></div>
<div class="field2"><div><label for="phNeutralVoltage">Voltage at pH 7 buffer</label><input id="phNeutralVoltage" name="phNeutralVoltage" type="number" min="0.5" max="3.0" step="0.01"></div><div><label for="phVoltagePerPH">Volts per pH step</label><input id="phVoltagePerPH" name="phVoltagePerPH" type="number" min="0.05" max="0.50" step="0.01"></div></div>
<div><label for="phOffset">pH trim offset</label><input id="phOffset" name="phOffset" type="number" min="-3.0" max="3.0" step="0.01"></div>
<button type="button" onclick="saveConfig()">Save settings</button>
</form>
<div class="note">pH defaults are only a starting point. Before production rollout, calibrate using pH 7 and pH 4 buffer solutions and save those tuned values here.</div>
</div>
<div class="panel" style="margin-top:16px">
<div class="rowline"><span>Data logs</span><span id="activeLogLabel">Current file --</span></div>
<p class="muted">Daily CSV files are kept in LittleFS so you can download them later for analytics or AI model training.</p>
<div class="list" id="downloadList"></div>
</div>
<div class="panel" style="margin-top:16px">
<div class="rowline"><span>System</span><span id="heapValue">Heap --</span></div>
<div class="note" id="systemSummary">Loading system status...</div>
</div>
</div>
</section></div><div class="toast" id="toast"></div>
<script>
const byId=id=>document.getElementById(id),state={temp:[],tds:[],ph:[]},maxPoints=30;
function toast(msg,bad){const el=byId("toast");el.textContent=msg;el.style.borderColor=bad?"rgba(255,127,115,.35)":"rgba(71,202,169,.22)";el.classList.add("show");clearTimeout(window.__toast);window.__toast=setTimeout(()=>el.classList.remove("show"),2600)}
async function getJSON(url,opt){const r=await fetch(url,opt);if(!r.ok)throw new Error(await r.text());return r.json()}
function pushSeries(key,val){if(typeof val!=="number"||Number.isNaN(val))return;state[key].push(val);if(state[key].length>maxPoints)state[key].shift()}
function linePoints(values){if(!values.length)return"";const min=Math.min(...values),max=Math.max(...values),span=Math.max(max-min,.001);return values.map((v,i)=>`${values.length===1?210:(420*i)/(values.length-1)},${88-(((v-min)/span)*76)}`).join(" ")}
function renderCharts(){byId("tempLine").setAttribute("points",linePoints(state.temp));byId("tdsLine").setAttribute("points",linePoints(state.tds));byId("phLine").setAttribute("points",linePoints(state.ph))}
async function refreshStatus(){try{const s=await getJSON("/api/status",{cache:"no-store"});byId("fwBadge").textContent=`Firmware: ${s.firmware}`;byId("deviceBadge").textContent=`Device: ${s.device}`;byId("wifiBadge").textContent=s.wifiConnected?`WiFi: ${s.ssid} (${s.ip})`:`WiFi: ${s.wifiState}`;byId("timeBadge").textContent=`Time: ${s.time}`;byId("tempValue").textContent=s.temperatureValid?`${s.temperature.toFixed(1)} C`:"--";byId("levelValue").textContent=s.waterLevelText;byId("phValue").textContent=s.ph.toFixed(2);byId("tdsValue").textContent=s.tds.toFixed(0);byId("ecValue").textContent=s.ec.toFixed(0);byId("rssiValue").textContent=s.wifiConnected?`${s.rssi} dBm`:"Offline";byId("logValue").textContent=s.activeLog;byId("uptimeValue").textContent=`Uptime ${s.uptime}`;byId("tempMeta").textContent=s.temperatureValid?`${s.temperature.toFixed(1)} C`:"sensor offline";byId("tdsMeta").textContent=`Voltage ${s.tdsVoltage.toFixed(3)} V`;byId("phMeta").textContent=`Voltage ${s.phVoltage.toFixed(3)} V`;byId("wifiStateText").textContent=`State: ${s.wifiState}`;byId("heapValue").textContent=`Heap ${s.heap} bytes`;byId("activeLogLabel").textContent=`Current file ${s.activeLog}`;byId("systemSummary").innerHTML=`IP: ${s.ip}<br>WiFi state: ${s.wifiState}<br>RSSI: ${s.wifiConnected?s.rssi+" dBm":"offline"}<br>LittleFS: ${s.fsOK?"mounted":"unavailable"}<br>Free heap: ${s.heap} bytes<br>Last disconnect reason: ${s.lastDisconnectReason}`;pushSeries("temp",s.temperatureValid?s.temperature:0);pushSeries("tds",s.tds);pushSeries("ph",s.ph);renderCharts()}catch(e){byId("wifiBadge").textContent="WiFi: console unreachable";byId("wifiStateText").textContent="State: reconnecting"}}
async function loadConfig(){try{const c=await getJSON("/api/config",{cache:"no-store"});["deviceName","logIntervalSec","ecFactor","tdsFactor","tdsVoltageOffset","phNeutralVoltage","phVoltagePerPH","phOffset"].forEach(k=>{if(byId(k))byId(k).value=c[k]})}catch(e){toast("Unable to load settings",true)}}
async function saveConfig(){const body=new URLSearchParams(new FormData(byId("configForm"))).toString();try{const res=await getJSON("/api/config",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});toast(res.message||"Settings saved");await refreshStatus()}catch(e){toast("Saving settings failed",true)}}
function chooseNetwork(ssid){byId("ssidInput").value=ssid;byId("passInput").focus()}
async function scanWifi(){const list=byId("networkList");list.innerHTML='<div class="item"><div>Scanning nearby networks...</div></div>';try{const res=await getJSON("/api/wifi/scan",{cache:"no-store"});const networks=res.networks||[];if(!networks.length){list.innerHTML='<div class="item"><div>No nearby networks found.</div></div>';return}list.innerHTML=networks.map(n=>`<div class="item"><div><strong>${n.ssid}</strong><br><span class="muted">${n.rssi} dBm | ch ${n.channel} | ${n.secure?"secured":"open"}</span></div><button type="button" class="alt" onclick="chooseNetwork('${n.ssid.replace(/'/g,"\\'")}')">Use</button></div>`).join("")}catch(e){list.innerHTML='<div class="item"><div>Wi-Fi scan failed. The board may be reconnecting.</div></div>'}}
async function connectWifi(){const ssid=byId("ssidInput").value.trim();if(!ssid){toast("Enter or choose an SSID first",true);return}const body=new URLSearchParams();body.set("ssid",ssid);body.set("pass",byId("passInput").value);try{const res=await getJSON("/api/wifi/connect",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:body.toString()});toast(res.message||"Wi-Fi switch requested")}catch(e){toast("Wi-Fi switch request failed",true)}}
async function resetWifi(){try{await getJSON("/api/wifi/reset",{method:"POST"});toast("Restarting into setup portal")}catch(e){toast("Unable to reset Wi-Fi",true)}}
async function refreshLogs(){try{const res=await getJSON("/api/logs",{cache:"no-store"}),items=res.files||[],root=byId("downloadList");if(!items.length){root.innerHTML='<div class="item"><div>No CSV files yet. The first log is created after the initial write interval.</div></div>';return}root.innerHTML=items.map(item=>`<div class="item"><div><strong>${item.name}</strong><br><span class="muted">${item.size} bytes</span></div><a class="btn" href="/download?file=${encodeURIComponent(item.path)}">Download</a></div>`).join("")}catch(e){byId("downloadList").innerHTML='<div class="item"><div>Unable to list log files.</div></div>'}}
loadConfig();refreshStatus();refreshLogs();scanWifi();setInterval(refreshStatus,2000);setInterval(refreshLogs,30000);
</script></body></html>
)HTML";

static String jsonEscape(const String& input);
static String wifiIpString();
static String wifiStateString();
static String getTimeHM();
static String getTimestamp();
static String getDailyLogPath();
static String uptimeString();
static void printBanner();
static void buildHostname();
static void setDefaults();
static void clampConfig();
static void loadConfig();
static void saveConfig();
static void initPins();
static void initSensors();
static void initDisplay();
static void bootScreen();
static void drawBrandMark(int16_t x, int16_t y);
static void initFileSystem();
static void ensureLogDir();
static void ensureLogFile(const String& path);
static void logDataIfDue(uint32_t now);
static void logData();
static void printStatus();
static void readSensors();
static float readStableVoltage(uint8_t pin, uint8_t sampleCount, uint16_t* rawAverage);
static float computeTdsPpm(float voltage, float temperatureC);
static float computePhValue(float voltage);
static void updateDisplay();
static void configureTime();
static void setupWifiManager();
static void connectWifiOnBoot();
static void queueWifiConnect(const String& ssid, const String& pass);
static void processWifiConnectRequest();
static void handleWifiStateMachine();
static void requestPortalRestart(const String& reason);
static void performPortalRestart();
static void markWifiConnected();
static void handleWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
static void startWebServer();
static void startOta();
static void handleRoot();
static void handleStatus();
static void handleConfigGet();
static void handleConfigPost();
static void handleWifiScan();
static void handleWifiConnect();
static void handleWifiReset();
static void handleLogs();
static void handleDownload();
static void handleNotFound();
static bool isSafeLogPath(const String& path);

void setup() {
  Serial.begin(115200);
  delay(200);
  printBanner();
  buildHostname();
  loadConfig();
  initPins();
  initSensors();
  initDisplay();
  initFileSystem();

  WiFi.onEvent(handleWifiEvent);
  connectWifiOnBoot();
  startWebServer();
  startOta();

  readSensors();
  updateDisplay();
  printStatus();
}

void loop() {
  const uint32_t now = millis();

  handleWifiStateMachine();
  if (g_otaReady) ArduinoOTA.handle();
  server.handleClient();

  if (now - g_lastSensorMs >= SENSOR_INTERVAL_MS) {
    g_lastSensorMs = now;
    readSensors();
    printStatus();
  }

  if (now - g_lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    g_lastDisplayMs = now;
    updateDisplay();
  }

  logDataIfDue(now);

  if (g_wifiConnected && (now - g_lastNtpSyncMs >= NTP_RESYNC_INTERVAL_MS || !g_timeConfigured)) {
    configureTime();
  }

  if (g_restartToPortalRequested) performPortalRestart();
  delay(10);
}

static void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(" GroWise Hydro Monitor " + String(FW_VERSION));
  Serial.println(" " + String(COMPANY_NAME));
  Serial.println(" DS18B20 + Float + TDS/EC + pH + OLED + WiFiManager + Web UI");
  Serial.println("============================================================");
}

static void buildHostname() {
  const uint32_t chipId = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  snprintf(g_hostname, sizeof(g_hostname), "growise-%06lX", (unsigned long)chipId);
}

static void setDefaults() {
  memset(&g_cfg, 0, sizeof(g_cfg));
  strlcpy(g_cfg.deviceName, DEFAULT_DEVICE_NAME, sizeof(g_cfg.deviceName));
  g_cfg.tdsFactor = 0.38f;
  g_cfg.tdsVoltageOffset = 0.05f;
  g_cfg.ecFactor = 0.50f;
  g_cfg.phNeutralVoltage = 2.50f;
  g_cfg.phVoltagePerPH = 0.18f;
  g_cfg.phOffset = 0.0f;
  g_cfg.logIntervalSec = 60UL;
}

static void clampConfig() {
  if (strlen(g_cfg.deviceName) == 0) strlcpy(g_cfg.deviceName, DEFAULT_DEVICE_NAME, sizeof(g_cfg.deviceName));
  g_cfg.tdsFactor = constrain(g_cfg.tdsFactor, 0.05f, 5.0f);
  g_cfg.tdsVoltageOffset = constrain(g_cfg.tdsVoltageOffset, -1.0f, 1.0f);
  g_cfg.ecFactor = constrain(g_cfg.ecFactor, 0.10f, 5.0f);
  g_cfg.phNeutralVoltage = constrain(g_cfg.phNeutralVoltage, 0.50f, 3.00f);
  g_cfg.phVoltagePerPH = constrain(g_cfg.phVoltagePerPH, 0.05f, 0.50f);
  g_cfg.phOffset = constrain(g_cfg.phOffset, -3.0f, 3.0f);
  g_cfg.logIntervalSec = constrain(g_cfg.logIntervalSec, 10UL, 3600UL);
}

static void loadConfig() {
  setDefaults();
  if (!prefs.begin(PREFS_NS, false)) {
    Serial.println("WARN: Preferences unavailable, using defaults");
    return;
  }
  String savedName = prefs.getString("devName", DEFAULT_DEVICE_NAME);
  savedName.trim();
  strlcpy(g_cfg.deviceName, savedName.c_str(), sizeof(g_cfg.deviceName));
  g_cfg.tdsFactor = prefs.getFloat("tdsFactor", g_cfg.tdsFactor);
  g_cfg.tdsVoltageOffset = prefs.getFloat("tdsVOff", g_cfg.tdsVoltageOffset);
  g_cfg.ecFactor = prefs.getFloat("ecFactor", g_cfg.ecFactor);
  g_cfg.phNeutralVoltage = prefs.getFloat("ph7Volt", g_cfg.phNeutralVoltage);
  g_cfg.phVoltagePerPH = prefs.getFloat("phVoltPH", g_cfg.phVoltagePerPH);
  g_cfg.phOffset = prefs.getFloat("phOffset", g_cfg.phOffset);
  g_cfg.logIntervalSec = prefs.getUInt("logSec", g_cfg.logIntervalSec);
  clampConfig();
}

static void saveConfig() {
  clampConfig();
  prefs.putString("devName", g_cfg.deviceName);
  prefs.putFloat("tdsFactor", g_cfg.tdsFactor);
  prefs.putFloat("tdsVOff", g_cfg.tdsVoltageOffset);
  prefs.putFloat("ecFactor", g_cfg.ecFactor);
  prefs.putFloat("ph7Volt", g_cfg.phNeutralVoltage);
  prefs.putFloat("phVoltPH", g_cfg.phVoltagePerPH);
  prefs.putFloat("phOffset", g_cfg.phOffset);
  prefs.putUInt("logSec", g_cfg.logIntervalSec);
}

static void initPins() {
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  pinMode(TDS_PIN, INPUT);
  pinMode(PH_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
}

static void initSensors() {
  ds18b20.begin();
  ds18b20.setResolution(12);
}

static void initDisplay() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (display.begin(0x3C, true)) {
    g_oledWorking = true;
    bootScreen();
  }
}

static void drawBrandMark(int16_t x, int16_t y) {
  display.drawCircle(x + 2, y + 13, 2, SH110X_WHITE);
  display.drawCircle(x + 14, y + 13, 2, SH110X_WHITE);
  display.drawCircle(x + 8, y + 18, 2, SH110X_WHITE);
  display.drawCircle(x + 8, y + 9, 2, SH110X_WHITE);
  display.drawLine(x + 2, y + 13, x + 8, y + 18, SH110X_WHITE);
  display.drawLine(x + 14, y + 13, x + 8, y + 18, SH110X_WHITE);
  display.drawLine(x + 8, y + 18, x + 8, y + 9, SH110X_WHITE);
  display.drawLine(x + 8, y + 9, x + 10, y + 3, SH110X_WHITE);
  display.fillTriangle(x + 7, y + 0, x + 15, y + 4, x + 9, y + 10, SH110X_WHITE);
}

static void bootScreen() {
  if (!g_oledWorking) return;
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  drawBrandMark(6, 6);
  display.setCursor(28, 8);
  display.println(F("GroWise Pvt. Ltd."));
  display.setCursor(28, 22);
  display.setTextSize(2);
  display.println(F("Hydro"));
  display.setTextSize(1);
  display.setCursor(28, 44);
  display.println(F("Production console"));
  display.setCursor(28, 56);
  display.println(FW_VERSION);
  display.display();
  delay(1800);
}

static void initFileSystem() {
  g_fsOK = LittleFS.begin(true);
  if (!g_fsOK) return;
  ensureLogDir();
  ensureLogFile(getDailyLogPath());
}

static void ensureLogDir() {
  if (g_fsOK && !LittleFS.exists(LOG_DIR)) LittleFS.mkdir(LOG_DIR);
}

static void ensureLogFile(const String& path) {
  if (!g_fsOK || path.length() == 0 || LittleFS.exists(path)) return;
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) return;
  file.println("company,device,timestamp,temp_c,temp_valid,water_level,tds_ppm,ec_uscm,ph,tds_voltage,ph_voltage,wifi_status,rssi,ip,uptime_sec");
  file.close();
}

static String getDailyLogPath() {
  if (g_wifiConnected) {
    struct tm timeinfo = {};
    if (getLocalTime(&timeinfo, 50)) {
      char path[32];
      snprintf(path, sizeof(path), "/logs/%04d-%02d-%02d.csv", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      return String(path);
    }
  }
  return String("/logs/unsynced.csv");
}

static String wifiIpString() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("0.0.0.0");
}

static String wifiStateString() {
  if (g_restartToPortalRequested) return "restarting_to_portal";
  if (g_wifiSwitchInProgress) return "switching";
  if (WiFi.status() == WL_CONNECTED) return "connected";
  if (WiFi.SSID().length() > 0) return "reconnecting";
  return "setup_required";
}

static String uptimeString() {
  uint32_t totalSeconds = millis() / 1000UL;
  char buf[20];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)(totalSeconds / 3600UL), (unsigned long)((totalSeconds % 3600UL) / 60UL), (unsigned long)(totalSeconds % 60UL));
  return String(buf);
}

static String getTimeHM() {
  struct tm timeinfo = {};
  if (g_wifiConnected && getLocalTime(&timeinfo, 50)) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
  }
  return String("--:--");
}

static String getTimestamp() {
  struct tm timeinfo = {};
  if (g_wifiConnected && getLocalTime(&timeinfo, 50)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
  }
  return String("UPTIME_") + String(millis() / 1000UL) + String("s");
}

static void configureTime() {
  if (!g_wifiConnected) return;
  configTime(GMT_OFFSET_SEC, 0, NTP_PRIMARY, NTP_SECONDARY);
  struct tm timeinfo = {};
  g_timeConfigured = getLocalTime(&timeinfo, 3000);
  g_lastNtpSyncMs = millis();
}

static float readStableVoltage(uint8_t pin, uint8_t sampleCount, uint16_t* rawAverage) {
  const uint8_t count = constrain(sampleCount, (uint8_t)5, (uint8_t)25);
  uint16_t samples[25];
  for (uint8_t i = 0; i < count; ++i) {
    samples[i] = (uint16_t)analogRead(pin);
    delayMicroseconds(250);
  }
  std::sort(samples, samples + count);
  uint8_t trim = count / 5;
  if (trim * 2 >= count) trim = 1;
  uint32_t sum = 0;
  uint8_t kept = 0;
  for (uint8_t i = trim; i < count - trim; ++i) {
    sum += samples[i];
    ++kept;
  }
  uint16_t avgRaw = kept > 0 ? (uint16_t)(sum / kept) : samples[count / 2];
  if (rawAverage) *rawAverage = avgRaw;
  return ((float)avgRaw * ADC_REF_VOLTAGE) / ADC_MAX_VALUE;
}

static float computeTdsPpm(float voltage, float temperatureC) {
  const float tempForComp = g_data.temperatureValid ? temperatureC : 25.0f;
  float adjustedVoltage = max(0.0f, voltage + g_cfg.tdsVoltageOffset);
  float compensationCoefficient = 1.0f + 0.02f * (tempForComp - 25.0f);
  if (compensationCoefficient <= 0.0f) compensationCoefficient = 1.0f;
  float compensationVoltage = adjustedVoltage / compensationCoefficient;
  float tds = (133.42f * compensationVoltage * compensationVoltage * compensationVoltage
               - 255.86f * compensationVoltage * compensationVoltage
               + 857.39f * compensationVoltage)
              * g_cfg.tdsFactor;
  if (isnan(tds) || tds < 0.0f) tds = 0.0f;
  return min(tds, 3000.0f);
}

static float computePhValue(float voltage) {
  float ph = 7.0f + ((g_cfg.phNeutralVoltage - voltage) / g_cfg.phVoltagePerPH) + g_cfg.phOffset;
  if (isnan(ph)) ph = 7.0f;
  return constrain(ph, 0.0f, 14.0f);
}

static void readSensors() {
  ds18b20.requestTemperatures();
  const float temp = ds18b20.getTempCByIndex(0);
  g_data.temperatureValid = (temp != DEVICE_DISCONNECTED_C && temp > -55.0f && temp < 125.0f);
  g_data.temperatureC = g_data.temperatureValid ? temp : -127.0f;
  g_data.waterLevelFull = (digitalRead(FLOAT_PIN) == LOW);
  g_data.tdsVoltage = readStableVoltage(TDS_PIN, 21, &g_data.rawTds);
  g_data.phVoltage = readStableVoltage(PH_PIN, 21, &g_data.rawPh);
  g_data.tdsPpm = computeTdsPpm(g_data.tdsVoltage, g_data.temperatureC);
  g_data.ecUsCm = g_data.tdsPpm / g_cfg.ecFactor;
  g_data.phValue = computePhValue(g_data.phVoltage);
  g_data.sampleCounter++;
}

static void updateDisplay() {
  if (!g_oledWorking) return;
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("GroWise Hydro"));
  drawBrandMark(108, 0);
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  display.setCursor(0, 16);
  display.print(F("T: "));
  if (g_data.temperatureValid) {
    display.print(g_data.temperatureC, 1);
    display.print(F(" C"));
  } else {
    display.print(F("--.- C"));
  }
  display.setCursor(68, 16);
  display.print(F("L:"));
  display.print(g_data.waterLevelFull ? F("FULL") : F("LOW"));
  display.setCursor(0, 28);
  display.print(F("TDS:"));
  display.print(g_data.tdsPpm, 0);
  display.print(F(" ppm"));
  display.setCursor(0, 40);
  display.print(F("EC:"));
  display.print(g_data.ecUsCm, 0);
  display.print(F(" pH:"));
  display.print(g_data.phValue, 1);
  display.setCursor(0, 52);
  display.print(F("WiFi:"));
  display.print(g_wifiConnected ? F("ON ") : F("OFF"));
  display.print(F(" "));
  display.print(getTimeHM());
  display.display();
}

static void printStatus() {
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.print("Company      : ");
  Serial.println(COMPANY_NAME);
  Serial.print("Device       : ");
  Serial.println(g_cfg.deviceName);
  Serial.print("Timestamp    : ");
  Serial.println(getTimestamp());
  Serial.print("Temperature  : ");
  if (g_data.temperatureValid) {
    Serial.print(g_data.temperatureC, 2);
    Serial.println(" C");
  } else Serial.println("SENSOR ERROR");
  Serial.print("Water Level  : ");
  Serial.println(g_data.waterLevelFull ? "FULL" : "LOW");
  Serial.print("TDS          : ");
  Serial.print(g_data.tdsPpm, 1);
  Serial.println(" ppm");
  Serial.print("EC           : ");
  Serial.print(g_data.ecUsCm, 1);
  Serial.println(" uS/cm");
  Serial.print("pH           : ");
  Serial.println(g_data.phValue, 2);
  Serial.print("WiFi         : ");
  Serial.println(g_wifiConnected ? "CONNECTED" : "OFFLINE");
  if (g_wifiConnected) {
    Serial.print("SSID         : ");
    Serial.println(WiFi.SSID());
    Serial.print("IP           : ");
    Serial.println(wifiIpString());
    Serial.print("RSSI         : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  Serial.print("Active log   : ");
  Serial.println(getDailyLogPath());
}

static void logDataIfDue(uint32_t now) {
  const uint32_t intervalMs = g_cfg.logIntervalSec * 1000UL;
  if (now - g_lastLogMs >= intervalMs) {
    g_lastLogMs = now;
    logData();
  }
}

static void logData() {
  if (!g_fsOK) return;
  const String logPath = getDailyLogPath();
  ensureLogFile(logPath);
  File file = LittleFS.open(logPath, FILE_APPEND);
  if (!file) return;
  file.print(COMPANY_SHORT);
  file.print(',');
  file.print(g_cfg.deviceName);
  file.print(',');
  file.print(getTimestamp());
  file.print(',');
  file.print(g_data.temperatureValid ? String(g_data.temperatureC, 2) : String("nan"));
  file.print(',');
  file.print(g_data.temperatureValid ? "true" : "false");
  file.print(',');
  file.print(g_data.waterLevelFull ? "FULL" : "LOW");
  file.print(',');
  file.print(g_data.tdsPpm, 1);
  file.print(',');
  file.print(g_data.ecUsCm, 1);
  file.print(',');
  file.print(g_data.phValue, 2);
  file.print(',');
  file.print(g_data.tdsVoltage, 3);
  file.print(',');
  file.print(g_data.phVoltage, 3);
  file.print(',');
  file.print(g_wifiConnected ? "ON" : "OFF");
  file.print(',');
  file.print(g_wifiConnected ? WiFi.RSSI() : 0);
  file.print(',');
  file.print(wifiIpString());
  file.print(',');
  file.println(millis() / 1000UL);
  file.close();
}

static void setupWifiManager() {
  wm.setDebugOutput(false);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(0);
  wm.setSaveConfigCallback([]() {
    Serial.println("WiFi credentials saved");
  });
  wm.setAPCallback([](WiFiManager* manager) {
    (void)manager;
    Serial.println("GroWise WiFi setup portal started");
    Serial.println("Connect to AP : GroWise-Setup");
    Serial.println("Portal URL    : http://192.168.4.1/");
  });
}

static void markWifiConnected() {
  g_wifiConnected = true;
  g_wifiSwitchInProgress = false;
  g_lastDisconnectReason = 0;
  configureTime();
  Serial.println("WiFi connected");
  Serial.println("IP: " + wifiIpString());
}

static void handleWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      markWifiConnected();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_wifiConnected = false;
      g_lastDisconnectReason = info.wifi_sta_disconnected.reason;
      Serial.printf("WiFi disconnected, reason=%u\n", g_lastDisconnectReason);
      break;
    default:
      break;
  }
}

static void connectWifiOnBoot() {
  bool forcePortal = prefs.getBool("forcePortal", false);
  if (forcePortal) prefs.putBool("forcePortal", false);

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(g_hostname);

  setupWifiManager();

  bool connected = false;
  if (forcePortal) {
    Serial.println("Starting GroWise WiFi portal by request...");
    connected = wm.startConfigPortal(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
  } else {
    Serial.println("Connecting with saved WiFi credentials...");
    connected = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
  }

  if (!connected) {
    Serial.println("WiFi portal exited without connection. Restarting...");
    delay(500);
    ESP.restart();
  }

  WiFi.softAPdisconnect(true);
  g_wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (g_wifiConnected) markWifiConnected();
}

static void queueWifiConnect(const String& ssid, const String& pass) {
  g_pendingSsid = ssid;
  g_pendingPass = pass;
  g_wifiConnectRequested = true;
}

static void processWifiConnectRequest() {
  if (!g_wifiConnectRequested) return;
  g_wifiConnectRequested = false;
  g_targetSsid = g_pendingSsid;
  const String targetPass = g_pendingPass;
  if (g_targetSsid.length() == 0) return;

  Serial.println("Switching WiFi to: " + g_targetSsid);
  g_wifiSwitchInProgress = true;
  g_wifiSwitchDeadlineMs = millis() + WIFI_SWITCH_TIMEOUT_MS;

  WiFi.disconnect(false, false);
  delay(120);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_targetSsid.c_str(), targetPass.c_str());

  g_pendingSsid = "";
  g_pendingPass = "";
}

static void requestPortalRestart(const String& reason) {
  if (g_restartToPortalRequested) return;
  g_restartToPortalRequested = true;
  Serial.println(reason);
}

static void performPortalRestart() {
  Serial.println("Clearing saved WiFi and restarting into setup portal...");
  prefs.putBool("forcePortal", true);
  wm.resetSettings();
  WiFi.disconnect(true, true);
  delay(250);
  ESP.restart();
}

static void handleWifiStateMachine() {
  processWifiConnectRequest();

  if (g_wifiSwitchInProgress && (int32_t)(millis() - g_wifiSwitchDeadlineMs) >= 0 && WiFi.status() != WL_CONNECTED) {
    g_wifiSwitchInProgress = false;
    requestPortalRestart("WiFi switch failed. Rebooting into setup portal.");
  }

  if (!g_wifiConnected && !g_wifiSwitchInProgress && !g_restartToPortalRequested) {
    if (WiFi.SSID().length() > 0 && millis() - g_lastWiFiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
      g_lastWiFiRetryMs = millis();
      Serial.println("Retrying WiFi connection...");
      WiFi.reconnect();
    }
  }
}

static void startOta() {
  if (g_otaReady) return;
  ArduinoOTA.setHostname(g_hostname);
  ArduinoOTA.setPassword(prefs.getString("otaPass", "growise123").c_str());
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update started");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA update finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static uint32_t lastProgressLog = 0;
    uint32_t now = millis();
    if (now - lastProgressLog > 600) {
      lastProgressLog = now;
      Serial.printf("OTA progress: %u%%\n", (progress * 100U) / total);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error: %u\n", error);
  });
  ArduinoOTA.begin();
  g_otaReady = true;
}

static String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

static void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
  String json;
  json.reserve(560);
  json += '{';
  json += "\"firmware\":\"" + jsonEscape(String(FW_VERSION)) + "\"";
  json += ",\"device\":\"" + jsonEscape(String(g_cfg.deviceName)) + "\"";
  json += ",\"temperature\":" + String(g_data.temperatureC, 2);
  json += ",\"temperatureValid\":" + String(g_data.temperatureValid ? "true" : "false");
  json += ",\"waterLevelText\":\"" + String(g_data.waterLevelFull ? "FULL" : "LOW") + "\"";
  json += ",\"tds\":" + String(g_data.tdsPpm, 1);
  json += ",\"ec\":" + String(g_data.ecUsCm, 1);
  json += ",\"ph\":" + String(g_data.phValue, 2);
  json += ",\"tdsVoltage\":" + String(g_data.tdsVoltage, 3);
  json += ",\"phVoltage\":" + String(g_data.phVoltage, 3);
  json += ",\"time\":\"" + jsonEscape(getTimeHM()) + "\"";
  json += ",\"timestamp\":\"" + jsonEscape(getTimestamp()) + "\"";
  json += ",\"uptime\":\"" + jsonEscape(uptimeString()) + "\"";
  json += ",\"wifiConnected\":" + String(g_wifiConnected ? "true" : "false");
  json += ",\"wifiState\":\"" + jsonEscape(wifiStateString()) + "\"";
  json += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
  json += ",\"ip\":\"" + jsonEscape(wifiIpString()) + "\"";
  json += ",\"rssi\":" + String(g_wifiConnected ? WiFi.RSSI() : 0);
  json += ",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"fsOK\":" + String(g_fsOK ? "true" : "false");
  json += ",\"lastDisconnectReason\":" + String(g_lastDisconnectReason);
  json += ",\"activeLog\":\"" + jsonEscape(getDailyLogPath()) + "\"";
  json += '}';
  server.send(200, "application/json", json);
}

static void handleConfigGet() {
  String json;
  json.reserve(280);
  json += '{';
  json += "\"deviceName\":\"" + jsonEscape(String(g_cfg.deviceName)) + "\"";
  json += ",\"tdsFactor\":" + String(g_cfg.tdsFactor, 3);
  json += ",\"tdsVoltageOffset\":" + String(g_cfg.tdsVoltageOffset, 3);
  json += ",\"ecFactor\":" + String(g_cfg.ecFactor, 3);
  json += ",\"phNeutralVoltage\":" + String(g_cfg.phNeutralVoltage, 3);
  json += ",\"phVoltagePerPH\":" + String(g_cfg.phVoltagePerPH, 3);
  json += ",\"phOffset\":" + String(g_cfg.phOffset, 3);
  json += ",\"logIntervalSec\":" + String(g_cfg.logIntervalSec);
  json += '}';
  server.send(200, "application/json", json);
}

static void handleConfigPost() {
  if (server.hasArg("deviceName")) {
    String value = server.arg("deviceName");
    value.trim();
    if (value.length() == 0) value = DEFAULT_DEVICE_NAME;
    strlcpy(g_cfg.deviceName, value.c_str(), sizeof(g_cfg.deviceName));
  }
  if (server.hasArg("tdsFactor")) g_cfg.tdsFactor = server.arg("tdsFactor").toFloat();
  if (server.hasArg("tdsVoltageOffset")) g_cfg.tdsVoltageOffset = server.arg("tdsVoltageOffset").toFloat();
  if (server.hasArg("ecFactor")) g_cfg.ecFactor = server.arg("ecFactor").toFloat();
  if (server.hasArg("phNeutralVoltage")) g_cfg.phNeutralVoltage = server.arg("phNeutralVoltage").toFloat();
  if (server.hasArg("phVoltagePerPH")) g_cfg.phVoltagePerPH = server.arg("phVoltagePerPH").toFloat();
  if (server.hasArg("phOffset")) g_cfg.phOffset = server.arg("phOffset").toFloat();
  if (server.hasArg("logIntervalSec")) g_cfg.logIntervalSec = (uint32_t)server.arg("logIntervalSec").toInt();
  saveConfig();
  readSensors();
  updateDisplay();
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Settings saved.\"}");
}

static void handleWifiScan() {
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) found = 0;
  if (found > 15) found = 15;
  String json = "{\"networks\":[";
  bool first = true;
  for (int i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    if (!first) json += ',';
    first = false;
    json += '{';
    json += "\"ssid\":\"" + jsonEscape(ssid) + "\"";
    json += ",\"rssi\":" + String(WiFi.RSSI(i));
    json += ",\"channel\":" + String(WiFi.channel(i));
    json += ",\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    json += '}';
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

static void handleWifiConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"SSID is required.\"}");
    return;
  }
  queueWifiConnect(ssid, pass);
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Wi-Fi switch requested.\"}");
}

static void handleWifiReset() {
  requestPortalRestart("WiFi reset requested from GroWise web console.");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Restarting into setup portal.\"}");
}

static bool isSafeLogPath(const String& path) {
  return path.startsWith("/logs/") && path.indexOf("..") < 0;
}

static void handleLogs() {
  if (!g_fsOK) {
    server.send(503, "application/json", "{\"files\":[]}");
    return;
  }
  File root = LittleFS.open(LOG_DIR);
  if (!root || !root.isDirectory()) {
    root.close();
    server.send(200, "application/json", "{\"files\":[]}");
    return;
  }
  root.close();
  server.send(200, "application/json", json);
  String json = "{\"files\":[";
  bool first = true;
  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      if (!first) json += ',';
      first = false;
      const String fullPath = String(entry.name());
      String name = fullPath;
      int slashPos = name.lastIndexOf('/');
      if (slashPos >= 0) name = name.substring(slashPos + 1);
      json += '{';
      json += "\"name\":\"" + jsonEscape(name) + "\"";
      json += ",\"path\":\"" + jsonEscape(fullPath) + "\"";
      json += ",\"size\":" + String(entry.size());
      json += '}';
    }
    entry = root.openNextFile();
  }
  json += "]}";
  server.send(200, "application/json", json);
}

static void handleDownload() {
  const String path = server.arg("file");
  if (!isSafeLogPath(path) || !LittleFS.exists(path)) {
    server.send(404, "text/plain", "Log file not found");
    return;
  }
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Unable to open log file");
    return;
  }
  server.streamFile(file, "text/csv");
  file.close();
}

static void handleNotFound() {
  server.send(404, "application/json", "{\"ok\":false,\"message\":\"Endpoint not found.\"}");
}

static void startWebServer() {
  if (g_serversStarted) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/download", HTTP_GET, handleDownload);
  server.onNotFound(handleNotFound);
  server.begin();
  g_serversStarted = true;
  Serial.println("Dashboard: http://" + wifiIpString() + "/");
}
