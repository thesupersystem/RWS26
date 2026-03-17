#include <ETH.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <OSCMessage.h>
#include <Preferences.h>

// ── ETH01 pin config ──────────────────────────────────────────
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  16
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN

// ── EMG config ────────────────────────────────────────────────
#define EMG_PIN        35
#define EMG_SAMPLES    64

// ── OSC defaults ──────────────────────────────────────────────
#define OSC_HOST_DEFAULT    "192.168.1.100"
#define OSC_PORT_DEFAULT    8000
#define OSC_ADDRESS         "/emg/raw"
#define OSC_INTERVAL_MS     20   // 50 Hz

WebServer   server(80);
WiFiUDP udp;
Preferences prefs;

static bool     eth_connected = false;
static String   oscHost       = OSC_HOST_DEFAULT;
static uint16_t oscPort       = OSC_PORT_DEFAULT;
static bool     oscEnabled    = true;
unsigned long   lastOscSend   = 0;

// ── ETH event handler ─────────────────────────────────────────
void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] Started");
      ETH.setHostname("esp32-eth01");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] Cable connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[ETH] IP: %s  Speed: %d Mbps  %s-duplex\n",
                    ETH.localIP().toString().c_str(),
                    ETH.linkSpeed(),
                    ETH.fullDuplex() ? "full" : "half");
      eth_connected = true;
      udp.begin(oscPort);
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      eth_connected = false;
      Serial.println("[ETH] Lost IP");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      eth_connected = false;
      Serial.println("[ETH] Disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      eth_connected = false;
      Serial.println("[ETH] Stopped");
      break;
    default:
      break;
  }
}

// ── EMG helper ────────────────────────────────────────────────
int readEMG() {
  long sum = 0;
  for (int i = 0; i < EMG_SAMPLES; i++) {
    sum += analogRead(EMG_PIN);
    delayMicroseconds(500);
  }
  return sum / EMG_SAMPLES;
}

// ── OSC sender ────────────────────────────────────────────────
void sendOSC(int raw) {
  if (!oscEnabled) return;
  float voltage = raw * (3.3f / 4095.0f);

  OSCMessage msg(OSC_ADDRESS);
  msg.add((int)raw);
  msg.add(voltage);

  udp.beginPacket(oscHost.c_str(), oscPort);
  msg.send(udp);
  udp.endPacket();
  msg.empty();
}

// ── Persist OSC settings ──────────────────────────────────────
void saveOSCSettings() {
  prefs.begin("osc", false);
  prefs.putString("host",    oscHost);
  prefs.putUShort("port",    oscPort);
  prefs.putBool("enabled",   oscEnabled);
  prefs.end();
  Serial.printf("[OSC] Saved → %s:%d  enabled:%d\n",
                oscHost.c_str(), oscPort, oscEnabled);
}

void loadOSCSettings() {
  prefs.begin("osc", true);
  oscHost    = prefs.getString("host",  OSC_HOST_DEFAULT);
  oscPort    = prefs.getUShort("port",  OSC_PORT_DEFAULT);
  oscEnabled = prefs.getBool("enabled", true);
  prefs.end();
  Serial.printf("[OSC] Loaded → %s:%d  enabled:%d\n",
                oscHost.c_str(), oscPort, oscEnabled);
}

// ── Route handlers ────────────────────────────────────────────
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-ETH01</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: sans-serif; background: #eef1f7;
           max-width: 620px; margin: 40px auto; padding: 0 16px; }
    h1   { color: #2c7be5; margin-bottom: 20px; }
    .card { background: #fff; border-radius: 10px; padding: 20px;
            margin: 12px 0; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }
    h2   { font-size: 0.85rem; text-transform: uppercase;
            letter-spacing: 0.08em; color: #888; margin-bottom: 14px; }
    button { background: #2c7be5; color: #fff; border: none;
             padding: 10px 22px; border-radius: 6px;
             cursor: pointer; font-size: 0.9rem; }
    button:hover { background: #1a5bbf; }
    button.stop  { background: #e74c3c; }
    button.stop:hover { background: #c0392b; }
    button.save  { background: #27ae60; }
    button.save:hover { background: #1e8449; }

    .emg-value { font-size: 2rem; font-weight: 700; color: #2c7be5; margin: 8px 0; }
    .emg-unit  { font-size: 0.8rem; color: #aaa; }
    .bar-wrap  { background: #eef1f7; border-radius: 999px;
                 height: 14px; margin: 10px 0; overflow: hidden; }
    .bar-fill  { height: 100%; border-radius: 999px; width: 0%;
                 background: linear-gradient(90deg, #2c7be5, #38d9a9);
                 transition: width 0.15s ease; }
    canvas { width: 100%; height: 120px; display: block;
             background: #f8f9fc; border-radius: 6px; margin-top: 10px; }

    .form-row { display: flex; gap: 10px; align-items: flex-end; margin-bottom: 12px; }
    .form-group { display: flex; flex-direction: column; gap: 4px; }
    .form-group label { font-size: 0.75rem; color: #888; font-weight: 600; }
    .form-group input { border: 1.5px solid #dde3f0; border-radius: 6px;
                        padding: 8px 10px; font-size: 0.95rem; outline: none;
                        transition: border-color 0.2s; }
    .form-group input:focus { border-color: #2c7be5; }
    .form-group.grow { flex: 1; }
    .form-group.port { width: 90px; }

    .toggle-row { display: flex; align-items: center; gap: 10px; margin-bottom: 14px; }
    .toggle { position: relative; width: 44px; height: 24px; }
    .toggle input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; inset: 0; background: #ccc;
              border-radius: 999px; cursor: pointer; transition: 0.3s; }
    .slider:before { content:""; position:absolute; width:18px; height:18px;
                     left:3px; bottom:3px; background:#fff; border-radius:50%;
                     transition:0.3s; }
    input:checked + .slider { background: #27ae60; }
    input:checked + .slider:before { transform: translateX(20px); }
    .toggle-label { font-size: 0.9rem; color: #444; }

    .osc-status { font-size: 0.8rem; margin-top: 10px; padding: 8px 12px;
                  border-radius: 6px; display: none; }
    .osc-status.ok  { background: #e8f8f0; color: #27ae60; display: block; }
    .osc-status.err { background: #fdecea; color: #e74c3c; display: block; }

    .info-grid { display: grid; grid-template-columns: 1fr 1fr;
                 gap: 8px; margin-top: 8px; }
    .info-item { background: #f4f6f9; border-radius: 6px; padding: 10px; }
    .info-item span { display: block; font-size: 0.75rem; color: #888; }
    .info-item strong { font-size: 1rem; color: #333; }

    .status { display: inline-block; width: 8px; height: 8px;
              border-radius: 50%; background: #38d9a9;
              margin-right: 6px; animation: pulse 1.5s infinite; }
    @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }
  </style>
</head>
<body>
  <h1>ESP32-ETH01</h1>

  <!-- EMG card -->
  <div class="card">
    <h2><span class="status"></span>EMG Signal</h2>
    <div class="emg-value" id="emgVal">—</div>
    <div class="emg-unit">raw ADC (0–4095) &nbsp;|&nbsp; <span id="emgV">—</span> V</div>
    <div class="bar-wrap"><div class="bar-fill" id="emgBar"></div></div>
    <canvas id="chart"></canvas>
    <div style="margin-top:12px;">
      <button onclick="startEMG()">▶ Start</button>
      <button class="stop" onclick="stopEMG()" style="margin-left:8px;">■ Stop</button>
    </div>
  </div>

  <!-- OSC config card -->
  <div class="card">
    <h2>OSC Configuration</h2>

    <div class="toggle-row">
      <label class="toggle">
        <input type="checkbox" id="oscEnabled" onchange="toggleOSC()">
        <span class="slider"></span>
      </label>
      <span class="toggle-label">Send OSC</span>
    </div>

    <div class="form-row">
      <div class="form-group grow">
        <label>Host IP</label>
        <input type="text" id="oscHost" placeholder="192.168.1.100">
      </div>
      <div class="form-group port">
        <label>Port</label>
        <input type="number" id="oscPort" placeholder="8000" min="1" max="65535">
      </div>
      <button class="save" onclick="saveOSC()">Save</button>
    </div>

    <div style="font-size:0.8rem; color:#aaa;">
      OSC address: <code>/emg/raw</code> &nbsp;·&nbsp;
      args: <code>int raw, float voltage</code> &nbsp;·&nbsp;
      rate: 50 Hz
    </div>
    <div class="osc-status" id="oscStatus"></div>
  </div>

  <!-- Device info card -->
  <div class="card">
    <h2>Device Info</h2>
    <div class="info-grid">
      <div class="info-item"><span>IP Address</span><strong id="ip">—</strong></div>
      <div class="info-item"><span>MAC</span><strong id="mac">—</strong></div>
      <div class="info-item"><span>Link Speed</span><strong id="spd">—</strong></div>
      <div class="info-item"><span>Uptime</span><strong id="upt">—</strong></div>
    </div>
    <button onclick="loadInfo()" style="margin-top:12px;">Refresh</button>
  </div>

  <script>
    // ── Chart ────────────────────────────────────────────────
    const canvas = document.getElementById('chart');
    const ctx    = canvas.getContext('2d');
    const BUF    = 120;
    const data   = new Array(BUF).fill(0);

    function drawChart() {
      canvas.width  = canvas.offsetWidth;
      canvas.height = 120;
      const w = canvas.width, h = canvas.height;
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = '#e0e4ef'; ctx.lineWidth = 1;
      [0.25, 0.5, 0.75].forEach(y => {
        ctx.beginPath(); ctx.moveTo(0,h*y); ctx.lineTo(w,h*y); ctx.stroke();
      });
      ctx.strokeStyle = '#2c7be5'; ctx.lineWidth = 2;
      ctx.beginPath();
      data.forEach((v,i) => {
        const x = (i/(BUF-1))*w, y = h-(v/4095)*h;
        i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
      });
      ctx.stroke();
    }

    // ── EMG polling ──────────────────────────────────────────
    let pollTimer = null;
    function startEMG() {
      if (pollTimer) return;
      pollTimer = setInterval(async () => {
        try {
          const obj = await fetch('/emg').then(r=>r.json());
          document.getElementById('emgVal').textContent = obj.raw;
          document.getElementById('emgV').textContent   = obj.voltage;
          document.getElementById('emgBar').style.width = (obj.raw/4095*100)+'%';
          data.push(obj.raw);
          if (data.length > BUF) data.shift();
          drawChart();
        } catch(e) {}
      }, 50);
    }
    function stopEMG() { clearInterval(pollTimer); pollTimer = null; }

    // ── OSC config ───────────────────────────────────────────
    async function loadOSCConfig() {
      try {
        const obj = await fetch('/osc/config').then(r=>r.json());
        document.getElementById('oscHost').value      = obj.host;
        document.getElementById('oscPort').value      = obj.port;
        document.getElementById('oscEnabled').checked = obj.enabled;
      } catch(e) {}
    }

    async function saveOSC() {
      const host    = document.getElementById('oscHost').value.trim();
      const port    = parseInt(document.getElementById('oscPort').value);
      const enabled = document.getElementById('oscEnabled').checked;
      const status  = document.getElementById('oscStatus');

      if (!host || isNaN(port) || port < 1 || port > 65535) {
        status.textContent = 'Invalid host or port.';
        status.className   = 'osc-status err';
        return;
      }

      try {
        const res = await fetch('/osc/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ host, port, enabled })
        });
        const obj = await res.json();
        status.textContent = obj.ok
          ? `Saved! Sending to ${obj.host}:${obj.port}`
          : 'Save failed.';
        status.className = obj.ok ? 'osc-status ok' : 'osc-status err';
      } catch(e) {
        status.textContent = 'Request failed.';
        status.className   = 'osc-status err';
      }
    }

    function toggleOSC() { saveOSC(); }

    // ── Device info ──────────────────────────────────────────
    async function loadInfo() {
      try {
        const obj = await fetch('/info').then(r=>r.json());
        document.getElementById('ip').textContent  = obj.ip;
        document.getElementById('mac').textContent = obj.mac;
        document.getElementById('spd').textContent = obj.speed + ' Mbps';
        document.getElementById('upt').textContent = obj.uptime_s + ' s';
      } catch(e) {}
    }

    loadOSCConfig();
    loadInfo();
    drawChart();
    window.addEventListener('resize', drawChart);
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleEMG() {
  int raw = readEMG();
  float voltage = raw * (3.3f / 4095.0f);
  String json = "{\"raw\":" + String(raw) + ",\"voltage\":" + String(voltage, 3) + "}";
  server.send(200, "application/json", json);
}

void handleOSCConfigGet() {
  String json = "{";
  json += "\"host\":\""  + oscHost                            + "\",";
  json += "\"port\":"    + String(oscPort)                    + ",";
  json += "\"enabled\":" + String(oscEnabled ? "true":"false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleOSCConfigPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  String body = server.arg("plain");

  auto extract = [&](const String& key) -> String {
    int i = body.indexOf("\"" + key + "\"");
    if (i < 0) return "";
    i = body.indexOf(":", i) + 1;
    while (body[i] == ' ') i++;
    if (body[i] == '"') {
      int s = i + 1, e = body.indexOf('"', s);
      return body.substring(s, e);
    } else {
      int e = body.indexOf(',', i);
      if (e < 0) e = body.indexOf('}', i);
      return body.substring(i, e);
    }
  };

  String newHost    = extract("host");
  String newPortStr = extract("port");
  String newEnaStr  = extract("enabled");

  if (newHost.length() == 0 || newPortStr.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  oscHost    = newHost;
  oscPort    = (uint16_t)newPortStr.toInt();
  oscEnabled = (newEnaStr == "true");

  udp.stop();
  udp.begin(oscPort);

  saveOSCSettings();

  String resp = "{\"ok\":true,\"host\":\"" + oscHost +
                "\",\"port\":"             + String(oscPort) + "}";
  server.send(200, "application/json", resp);
}

void handleInfo() {
  String json = "{";
  json += "\"ip\":\""        + ETH.localIP().toString()                  + "\",";
  json += "\"mac\":\""       + ETH.macAddress()                          + "\",";
  json += "\"hostname\":\""  + String(ETH.getHostname())                 + "\",";
  json += "\"speed\":"       + String(ETH.linkSpeed())                   + ",";
  json += "\"full_duplex\":" + String(ETH.fullDuplex() ? "true":"false") + ",";
  json += "\"uptime_s\":"    + String(millis() / 1000);
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "404 – Not found");
}

// ── setup / loop ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  loadOSCSettings();

  Network.onEvent(onEthEvent);

  ETH.begin(ETH_PHY_TYPE,
            ETH_PHY_ADDR,
            ETH_PHY_MDC,
            ETH_PHY_MDIO,
            ETH_PHY_POWER,
            ETH_CLK_MODE);

  uint32_t t = millis();
  while (!eth_connected && millis() - t < 10000) delay(100);

  if (!eth_connected) Serial.println("[ETH] No link – check cable / switch");

  server.on("/",           handleRoot);
  server.on("/emg",        handleEMG);
  server.on("/info",       handleInfo);
  server.on("/osc/config", HTTP_GET,  handleOSCConfigGet);
  server.on("/osc/config", HTTP_POST, handleOSCConfigPost);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Server started on port 80");
  Serial.printf("[OSC] Target → %s:%d  address: %s\n",
                oscHost.c_str(), oscPort, OSC_ADDRESS);
}

void loop() {
  if (!eth_connected) return;

  server.handleClient();

  unsigned long now = millis();
  if (now - lastOscSend >= OSC_INTERVAL_MS) {
    lastOscSend = now;
    sendOSC(readEMG());
  }
}