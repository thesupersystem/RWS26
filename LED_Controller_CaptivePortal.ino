/*
 * ESP32 WS2812B LED Strip Web Server — Captive Portal / AP Mode
 * ==============================================================
 * Creates its own WiFi network. Connect your phone to it, and the
 * control UI opens automatically via captive portal.
 *
 * Libraries Required (install via Arduino Library Manager):
 *   - FastLED          by Daniel Garcia
 *   - ESPAsyncWebServer by Me-No-Dev
 *   - AsyncTCP         by Me-No-Dev
 *   - ArduinoJson      by Benoit Blanchon
 *
 * Wiring:
 *   WS2812B Data In  → GPIO 5 (DATA_PIN)
 *   WS2812B 5V       → External 5V PSU (do NOT power from ESP32 pin for >10 LEDs)
 *   WS2812B GND      → Common GND with ESP32
 *   Add a 300-500Ω resistor in series on the data line.
 *   Add a 100-1000µF capacitor across 5V and GND near the strip.
 *
 * Usage:
 *   1. Power on the ESP32.
 *   2. On your phone, connect to WiFi network: "LEDCTRL"  (password: "ledctrl1")
 *   3. A captive portal notification appears — tap it, or open any browser
 *      and navigate to http://192.168.4.1
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>        // ← NEW: for captive portal DNS redirect
#include <FastLED.h>
#include <ArduinoJson.h>

// ─── Configuration ────────────────────────────────────────────────
#define DATA_PIN      5
#define NUM_LEDS      100
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB

// Access Point credentials — change these if you like
const char* AP_SSID = "LEDCTRL";
const char* AP_PASS = "ledctrl1";   // min 8 chars, or set to nullptr for open network

// DNS server listens on port 53 and redirects everything to 192.168.4.1
const byte DNS_PORT = 53;
DNSServer dnsServer;

// ─── Globals ──────────────────────────────────────────────────────
CRGB leds[NUM_LEDS];
AsyncWebServer server(80);

uint8_t  g_brightness = 128;
CRGB     g_solidColor = CRGB(255, 100, 0);
String   g_mode       = "solid";
bool     g_running    = true;

// ─── HTML / CSS / JS ──────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LED CTRL</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=Bebas+Neue&display=swap');

  :root {
    --bg:       #0a0a0f;
    --surface:  #13131f;
    --border:   #2a2a40;
    --accent:   #ff6b35;
    --accent2:  #7c3aed;
    --text:     #e8e8f0;
    --muted:    #555570;
    --on:       #22c55e;
    --rad:      6px;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Space Mono', monospace;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 32px 16px 64px;
    gap: 24px;
  }

  header {
    width: 100%;
    max-width: 540px;
    display: flex;
    justify-content: space-between;
    align-items: flex-end;
    border-bottom: 1px solid var(--border);
    padding-bottom: 16px;
  }

  h1 {
    font-family: 'Bebas Neue', sans-serif;
    font-size: 2.8rem;
    letter-spacing: 0.12em;
    line-height: 1;
    color: var(--text);
  }

  h1 span { color: var(--accent); }

  .status {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 0.7rem;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.1em;
  }

  .dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--on);
    box-shadow: 0 0 6px var(--on);
    animation: pulse-dot 2s ease-in-out infinite;
  }

  @keyframes pulse-dot {
    0%, 100% { opacity: 1; } 50% { opacity: 0.4; }
  }

  .card {
    width: 100%;
    max-width: 540px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--rad);
    padding: 20px 24px;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  .card-label {
    font-size: 0.65rem;
    text-transform: uppercase;
    letter-spacing: 0.14em;
    color: var(--muted);
    border-bottom: 1px solid var(--border);
    padding-bottom: 10px;
  }

  .preview {
    height: 28px;
    border-radius: 4px;
    background: #1a1a2e;
    overflow: hidden;
    position: relative;
  }

  .preview-inner {
    height: 100%;
    width: 100%;
    transition: background 0.3s ease;
  }

  .mode-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 8px;
  }

  .mode-btn {
    background: transparent;
    border: 1px solid var(--border);
    color: var(--muted);
    font-family: 'Space Mono', monospace;
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    padding: 10px 6px;
    border-radius: var(--rad);
    cursor: pointer;
    transition: all 0.15s ease;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
  }

  .mode-btn .icon { font-size: 1.3rem; }

  .mode-btn:hover { border-color: var(--accent); color: var(--text); }

  .mode-btn.active {
    border-color: var(--accent);
    background: rgba(255, 107, 53, 0.08);
    color: var(--accent);
  }

  .color-row { display: flex; align-items: center; gap: 16px; }

  #colorPicker {
    width: 52px; height: 52px;
    border-radius: 50%;
    border: 2px solid var(--border);
    cursor: pointer;
    padding: 0;
    background: none;
    overflow: hidden;
    flex-shrink: 0;
    outline: none;
    transition: border-color 0.2s;
  }

  #colorPicker:hover { border-color: var(--accent); }

  .presets { display: flex; gap: 8px; flex-wrap: wrap; }

  .preset {
    width: 28px; height: 28px;
    border-radius: 50%;
    cursor: pointer;
    border: 2px solid transparent;
    transition: transform 0.15s, border-color 0.15s;
    flex-shrink: 0;
  }

  .preset:hover { transform: scale(1.2); border-color: #fff3; }

  .slider-group { display: flex; flex-direction: column; gap: 8px; }

  .slider-header {
    display: flex;
    justify-content: space-between;
    font-size: 0.7rem;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.08em;
  }

  .slider-val { color: var(--text); font-weight: 700; }

  input[type=range] {
    -webkit-appearance: none;
    width: 100%;
    height: 4px;
    border-radius: 2px;
    background: var(--border);
    outline: none;
    cursor: pointer;
  }

  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 18px; height: 18px;
    border-radius: 50%;
    background: var(--accent);
    box-shadow: 0 0 8px rgba(255,107,53,0.6);
    cursor: pointer;
    transition: transform 0.1s;
  }

  input[type=range]::-webkit-slider-thumb:hover { transform: scale(1.2); }

  .power-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .power-label { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.1em; }

  .toggle {
    width: 56px; height: 28px;
    background: var(--border);
    border-radius: 14px;
    position: relative;
    cursor: pointer;
    transition: background 0.25s;
    border: none;
    outline: none;
    flex-shrink: 0;
  }

  .toggle.on { background: var(--on); }

  .toggle::after {
    content: '';
    position: absolute;
    width: 22px; height: 22px;
    border-radius: 50%;
    background: #fff;
    top: 3px; left: 3px;
    transition: transform 0.25s;
  }

  .toggle.on::after { transform: translateX(28px); }

  #toast {
    position: fixed;
    bottom: 24px;
    left: 50%;
    transform: translateX(-50%) translateY(80px);
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text);
    font-size: 0.72rem;
    padding: 10px 20px;
    border-radius: 20px;
    transition: transform 0.3s ease;
    pointer-events: none;
    letter-spacing: 0.06em;
  }

  #toast.show { transform: translateX(-50%) translateY(0); }
</style>
</head>
<body>

<header>
  <h1>LED<span>CTRL</span></h1>
  <div class="status"><div class="dot"></div><span id="statusText">CONNECTED</span></div>
</header>

<div class="card">
  <div class="card-label">Preview</div>
  <div class="preview"><div class="preview-inner" id="previewBar"></div></div>
</div>

<div class="card">
  <div class="power-row">
    <span class="power-label">⚡ Power</span>
    <button class="toggle on" id="powerToggle" onclick="togglePower()"></button>
  </div>
</div>

<div class="card">
  <div class="card-label">Mode</div>
  <div class="mode-grid">
    <button class="mode-btn active" data-mode="solid"   onclick="setMode(this)"><span class="icon">●</span>Solid</button>
    <button class="mode-btn"        data-mode="rainbow" onclick="setMode(this)"><span class="icon">🌈</span>Rainbow</button>
    <button class="mode-btn"        data-mode="fire"    onclick="setMode(this)"><span class="icon">🔥</span>Fire</button>
    <button class="mode-btn"        data-mode="pulse"   onclick="setMode(this)"><span class="icon">💓</span>Pulse</button>
    <button class="mode-btn"        data-mode="chase"   onclick="setMode(this)"><span class="icon">➤</span>Chase</button>
    <button class="mode-btn"        data-mode="off"     onclick="setMode(this)"><span class="icon">○</span>Off</button>
  </div>
</div>

<div class="card">
  <div class="card-label">Color (Solid / Pulse / Chase)</div>
  <div class="color-row">
    <input type="color" id="colorPicker" value="#ff6400" oninput="onColorChange(this.value)">
    <div class="presets" id="presets"></div>
  </div>
</div>

<div class="card">
  <div class="card-label">Settings</div>
  <div class="slider-group">
    <div class="slider-header"><span>Brightness</span><span class="slider-val" id="brightVal">50%</span></div>
    <input type="range" id="brightness" min="0" max="255" value="128" oninput="onSlider()">
  </div>
  <div class="slider-group">
    <div class="slider-header"><span>Speed</span><span class="slider-val" id="speedVal">50%</span></div>
    <input type="range" id="speed" min="1" max="100" value="50" oninput="onSlider()">
  </div>
</div>

<div id="toast"></div>

<script>
  const PRESETS = [
    '#ff6400','#ff2020','#ff00a8','#7c3aed',
    '#2563eb','#06b6d4','#22c55e','#fbbf24','#ffffff'
  ];

  let state = { power: true, mode: 'solid', color: '#ff6400', brightness: 128, speed: 50 };
  let debounceTimer;

  const presetsEl = document.getElementById('presets');
  PRESETS.forEach(hex => {
    const d = document.createElement('div');
    d.className = 'preset';
    d.style.background = hex;
    d.title = hex;
    d.onclick = () => { document.getElementById('colorPicker').value = hex; onColorChange(hex); };
    presetsEl.appendChild(d);
  });

  function togglePower() {
    state.power = !state.power;
    const btn = document.getElementById('powerToggle');
    btn.classList.toggle('on', state.power);
    if (!state.power) document.getElementById('previewBar').style.background = '#111';
    sendState();
  }

  function setMode(btn) {
    document.querySelectorAll('.mode-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    state.mode = btn.dataset.mode;
    updatePreview();
    sendState();
  }

  function onColorChange(hex) {
    state.color = hex;
    updatePreview();
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(sendState, 120);
  }

  function onSlider() {
    state.brightness = parseInt(document.getElementById('brightness').value);
    state.speed = parseInt(document.getElementById('speed').value);
    document.getElementById('brightVal').textContent = Math.round(state.brightness / 255 * 100) + '%';
    document.getElementById('speedVal').textContent = state.speed + '%';
    updatePreview();
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(sendState, 150);
  }

  function updatePreview() {
    const bar = document.getElementById('previewBar');
    if (!state.power || state.mode === 'off') {
      bar.style.background = '#111'; bar.style.animation = 'none'; return;
    }
    const alpha = state.brightness / 255;
    switch (state.mode) {
      case 'solid':
        bar.style.background = hexToRgba(state.color, alpha); bar.style.animation = 'none'; break;
      case 'rainbow':
        bar.style.background = `linear-gradient(90deg,rgba(255,0,0,${alpha}),rgba(255,165,0,${alpha}),rgba(255,255,0,${alpha}),rgba(0,255,0,${alpha}),rgba(0,0,255,${alpha}),rgba(238,130,238,${alpha}))`;
        bar.style.animation = 'none'; break;
      case 'fire':
        bar.style.background = `linear-gradient(90deg,rgba(255,0,0,${alpha}),rgba(255,100,0,${alpha}),rgba(255,200,0,${alpha}),rgba(255,80,0,${alpha}),rgba(200,0,0,${alpha}))`;
        bar.style.animation = 'none'; break;
      case 'pulse':
        bar.style.background = hexToRgba(state.color, alpha); bar.style.animation = 'none'; break;
      case 'chase':
        bar.style.background = `repeating-linear-gradient(90deg,${hexToRgba(state.color,alpha)} 0px,${hexToRgba(state.color,alpha)} 20px,rgba(0,0,0,0.1) 20px,rgba(0,0,0,0.1) 40px)`;
        bar.style.animation = 'none'; break;
    }
  }

  function hexToRgba(hex, alpha) {
    const r = parseInt(hex.slice(1,3),16), g = parseInt(hex.slice(3,5),16), b = parseInt(hex.slice(5,7),16);
    return `rgba(${r},${g},${b},${alpha})`;
  }

  async function sendState() {
    const payload = {
      power: state.power, mode: state.mode,
      r: parseInt(state.color.slice(1,3),16),
      g: parseInt(state.color.slice(3,5),16),
      b: parseInt(state.color.slice(5,7),16),
      brightness: state.brightness, speed: state.speed
    };
    try {
      const res = await fetch('/api/set', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (res.ok) showToast('Updated ✓');
    } catch(e) { showToast('Error — check connection'); }
  }

  function showToast(msg) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 1800);
  }

  document.getElementById('brightVal').textContent = Math.round(128/255*100)+'%';
  document.getElementById('speedVal').textContent = '50%';
  updatePreview();
</script>
</body>
</html>
)rawliteral";


// ─── LED Effect Variables ─────────────────────────────────────────
uint8_t  g_r = 255, g_g = 100, g_b = 0;
uint8_t  g_speed = 50;
uint32_t g_lastUpdate = 0;
uint8_t  g_hue = 0;
uint8_t  g_chasePos = 0;
float    g_pulseVal = 0;
bool     g_pulseDir = true;


// ─── FastLED Effect Functions ─────────────────────────────────────
void effectSolid() {
  fill_solid(leds, NUM_LEDS, CRGB(g_r, g_g, g_b));
}

void effectRainbow() {
  uint32_t now = millis();
  uint16_t delayMs = map(g_speed, 1, 100, 80, 1);
  if (now - g_lastUpdate > delayMs) {
    fill_rainbow(leds, NUM_LEDS, g_hue++, 7);
    g_lastUpdate = now;
  }
}

void effectFire() {
  static uint8_t heat[NUM_LEDS];
  uint32_t now = millis();
  uint16_t delayMs = map(g_speed, 1, 100, 100, 10);
  if (now - g_lastUpdate < delayMs) return;
  g_lastUpdate = now;
  for (int i = 0; i < NUM_LEDS; i++) heat[i] = qsub8(heat[i], random8(0, 55));
  for (int k = NUM_LEDS - 1; k >= 2; k--) heat[k] = (heat[k-1] + heat[k-2] + heat[k-2]) / 3;
  if (random8() < 120) { int y = random8(7); heat[y] = qadd8(heat[y], random8(160, 255)); }
  for (int j = 0; j < NUM_LEDS; j++) leds[j] = HeatColor(heat[j]);
}

void effectPulse() {
  uint32_t now = millis();
  uint16_t delayMs = map(g_speed, 1, 100, 30, 1);
  if (now - g_lastUpdate > delayMs) {
    if (g_pulseDir) {
      g_pulseVal += 4.0f;
      if (g_pulseVal >= 255.0f) { g_pulseVal = 255.0f; g_pulseDir = false; }
    } else {
      g_pulseVal -= 4.0f;
      if (g_pulseVal <= 0.0f) { g_pulseVal = 0.0f; g_pulseDir = true; }
    }
    float factor = g_pulseVal / 255.0f;
    fill_solid(leds, NUM_LEDS, CRGB((uint8_t)(g_r*factor),(uint8_t)(g_g*factor),(uint8_t)(g_b*factor)));
    g_lastUpdate = now;
  }
}

void effectChase() {
  uint32_t now = millis();
  uint16_t delayMs = map(g_speed, 1, 100, 200, 10);
  if (now - g_lastUpdate > delayMs) {
    fadeToBlackBy(leds, NUM_LEDS, 64);
    leds[g_chasePos] = CRGB(g_r, g_g, g_b);
    g_chasePos = (g_chasePos + 1) % NUM_LEDS;
    g_lastUpdate = now;
  }
}

void effectOff() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}


// ─── Captive Portal Helper ────────────────────────────────────────
// Returns a tiny redirect page — used for all OS captive portal probes
void serveCaptivePortal(AsyncWebServerRequest* req) {
  // A 302 redirect to our root is the most reliable cross-platform approach
  req->redirect("http://192.168.4.1/");
}


// ─── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  // Init FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(g_brightness);
  effectOff();
  FastLED.show();

  // ── Start Access Point ──────────────────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress apIP = WiFi.softAPIP();   // always 192.168.4.1
  Serial.printf("\nAP started: SSID=%s  IP=%s\n", AP_SSID, apIP.toString().c_str());

  // ── Start DNS server (redirects all domains → 192.168.4.1) ─────
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started (wildcard → AP IP)");

  // ── Web server routes ───────────────────────────────────────────

  // Main UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  // ── Captive portal detection endpoints ─────────────────────────
  // iOS / macOS
  server.on("/hotspot-detect.html",           HTTP_GET, serveCaptivePortal);
  server.on("/library/test/success.html",     HTTP_GET, serveCaptivePortal);
  // Android / Chrome
  server.on("/generate_204",                  HTTP_GET, serveCaptivePortal);
  server.on("/gen_204",                       HTTP_GET, serveCaptivePortal);
  // Windows
  server.on("/connecttest.txt",               HTTP_GET, serveCaptivePortal);
  server.on("/redirect",                      HTTP_GET, serveCaptivePortal);
  server.on("/ncsi.txt",                      HTTP_GET, serveCaptivePortal);
  // Firefox
  server.on("/success.txt",                   HTTP_GET, serveCaptivePortal);

  // Catch-all: any other unrecognised URL → redirect to UI
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("http://192.168.4.1/");
  });

  // ── API: set state ──────────────────────────────────────────────
  server.on("/api/set", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(200, "application/json", "{\"ok\":true}");
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) { req->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

      bool    power = doc["power"]      | true;
      String  mode  = doc["mode"]       | "solid";
      uint8_t r     = doc["r"]          | 255;
      uint8_t g     = doc["g"]          | 100;
      uint8_t b     = doc["b"]          | 0;
      uint8_t br    = doc["brightness"] | 128;
      uint8_t sp    = doc["speed"]      | 50;

      g_r = r; g_g = g; g_b = b;
      g_brightness = br;
      g_speed = sp;
      g_running = power;
      g_mode = mode;

      FastLED.setBrightness(power ? br : 0);
      Serial.printf("[LED] mode=%s r=%d g=%d b=%d bri=%d spd=%d pwr=%d\n",
                    mode.c_str(), r, g, b, br, sp, power);
      req->send(200, "application/json", "{\"ok\":true}");
    }
  );

  // ── API: get state ──────────────────────────────────────────────
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    String json = "{\"mode\":\"" + g_mode + "\","
                  "\"r\":" + g_r + ","
                  "\"g\":" + g_g + ","
                  "\"b\":" + g_b + ","
                  "\"brightness\":" + g_brightness + ","
                  "\"speed\":" + g_speed + ","
                  "\"power\":" + (g_running ? "true" : "false") + "}";
    req->send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Web server started. Connect to WiFi: " + String(AP_SSID));
}


// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
  dnsServer.processNextRequest();   // ← Must be called every loop for captive portal

  if (!g_running || g_mode == "off") {
    effectOff();
  } else if (g_mode == "solid") {
    effectSolid();
  } else if (g_mode == "rainbow") {
    effectRainbow();
  } else if (g_mode == "fire") {
    effectFire();
  } else if (g_mode == "pulse") {
    effectPulse();
  } else if (g_mode == "chase") {
    effectChase();
  }

  FastLED.show();
  delay(1);
}
