// ESP32 + PCA9685 — Captive portal servo controller
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ── WiFi AP config ───────────────────────────────────────
#define AP_SSID     "servoMotor"
#define AP_PASS     "servoMotor123"
// ─────────────────────────────────────────────────────────

#define PCA_ADDR    0x40
#define PWM_FREQ    50
#define NUM_MOTORS  16

#define SERVO_MIN_US  500
#define SERVO_MAX_US  2500
#define STEP_MIN    1
#define STEP_MAX    2
#define UPDATE_MS   30
#define HOLD_MIN_MS 300
#define HOLD_MAX_MS 2000

Adafruit_PWMServoDriver pwm(PCA_ADDR);
WebServer server(80);
DNSServer dns;

bool running = false;
bool enabled = true;

uint16_t usToPulse(uint16_t us) {
  float tickUs = 1000000.0f / (PWM_FREQ * 4096);
  return (uint16_t)(us / tickUs);
}

const uint16_t PMIN = usToPulse(SERVO_MIN_US);
const uint16_t PMAX = usToPulse(SERVO_MAX_US);

struct Motor {
  uint16_t pos;
  uint16_t target;
  uint8_t  step;
  uint32_t holdUntil;
};

Motor motors[NUM_MOTORS];
uint32_t lastUpdate = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Servo Control</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #0f0f0f;
    color: #e0e0e0;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 32px;
    padding: 32px 16px;
  }
  h1 { font-size: 18px; font-weight: 500; letter-spacing: 0.05em; color: #888; text-transform: uppercase; }
  .grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
    width: 100%;
    max-width: 360px;
  }
  .bar {
    background: #1a1a1a;
    border: 1px solid #2a2a2a;
    border-radius: 8px;
    height: 64px;
    position: relative;
    overflow: hidden;
  }
  .bar-fill {
    position: absolute;
    bottom: 0; left: 0; right: 0;
    background: #3a8fff;
    height: 0%;
    transition: height 0.3s ease;
    border-radius: 0 0 7px 7px;
  }
  .bar-label {
    position: absolute;
    top: 6px; left: 0; right: 0;
    text-align: center;
    font-size: 10px;
    color: #555;
    z-index: 1;
  }
  .controls {
    display: flex;
    gap: 12px;
  }
  button {
    padding: 14px 36px;
    border: none;
    border-radius: 10px;
    font-size: 15px;
    font-weight: 500;
    cursor: pointer;
    transition: opacity 0.15s, transform 0.1s;
  }
  button:active { transform: scale(0.96); }
  #btn-start   { background: #2a2a2a; color: #aaa; }
  #btn-stop    { background: #2a2a2a; color: #aaa; }
  #btn-enable  { background: #2a2a2a; color: #aaa; }
  #btn-disable { background: #2a2a2a; color: #aaa; }
  #btn-start.active   { background: #3a8fff; color: #fff; }
  #btn-stop.active    { background: #ff4a4a; color: #fff; }
  #btn-enable.active  { background: #3a8fff; color: #fff; }
  #btn-disable.active { background: #ff4a4a; color: #fff; }
  #status { font-size: 12px; color: #444; }
</style>
</head>
<body>
<h1>Servo Control</h1>

<div class="grid" id="grid"></div>

<div class="controls">
  <button id="btn-start"  onclick="send('/start')">Start</button>
  <button id="btn-stop"   onclick="send('/stop')">Stop</button>
</div>
<div class="controls">
  <button id="btn-enable"  onclick="send('/enable')">Enable</button>
  <button id="btn-disable" onclick="send('/disable')">Disable</button>
</div>
<div id="status">idle</div>

<script>
  const grid = document.getElementById('grid');
  const bars = [];

  for (let i = 0; i < 16; i++) {
    const bar = document.createElement('div');
    bar.className = 'bar';
    bar.innerHTML = `<div class="bar-label">${i}</div><div class="bar-fill" id="f${i}"></div>`;
    grid.appendChild(bar);
    bars.push(document.getElementById('f' + i));
  }

  function send(path) {
    fetch(path).catch(() => {});
  }

  async function poll() {
    try {
      const r = await fetch('/state');
      const d = await r.json();
      document.getElementById('btn-start').classList.toggle('active', d.running);
      document.getElementById('btn-stop').classList.toggle('active', d.running);
      document.getElementById('btn-enable').classList.toggle('active', d.enabled);
      document.getElementById('btn-disable').classList.toggle('active', !d.enabled);
      document.getElementById('status').textContent = !d.enabled ? 'disabled' : d.running ? 'running' : 'idle';
      d.positions.forEach((p, i) => {
        bars[i].style.height = p + '%';
      });
    } catch(e) {}
    setTimeout(poll, 200);
  }
  poll();
</script>
</body>
</html>
)rawliteral";

// ── Helpers ──────────────────────────────────────────────
uint16_t randomTarget() { return random(PMIN, PMAX + 1); }
uint8_t  randomStep()   { return random(STEP_MIN, STEP_MAX + 1); }

void initMotors() {
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motors[i].pos       = random(PMIN, PMAX + 1);
    motors[i].target    = randomTarget();
    motors[i].step      = randomStep();
    motors[i].holdUntil = millis() + random(HOLD_MIN_MS, HOLD_MAX_MS);
    pwm.setPWM(i, 0, motors[i].pos);
  }
}

void stopMotors() {
  uint16_t mid = (PMIN + PMAX) / 2;
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motors[i].target = mid;
  }
}

// ── Route handlers ───────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStart() {
  if (!running && enabled) { running = true; initMotors(); }
  server.send(200, "text/plain", "ok");
}

void handleStop() {
  running = false;
  stopMotors();
  server.send(200, "text/plain", "ok");
}

void handleEnable() {
  enabled = true;
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    pwm.setPWM(i, 0, motors[i].pos);
  }
  server.send(200, "text/plain", "ok");
}

void handleDisable() {
  enabled = false;
  running = false;
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    pwm.setPWM(i, 0, 4096);
  }
  server.send(200, "text/plain", "ok");
}

void handleState() {
  String json = "{\"running\":";
  json += running ? "true" : "false";
  json += ",\"enabled\":";
  json += enabled ? "true" : "false";
  json += ",\"positions\":[";
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    uint8_t pct = map(motors[i].pos, PMIN, PMAX, 0, 100);
    json += pct;
    if (i < NUM_MOTORS - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "");
}

// ── Setup & loop ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(36));

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(PWM_FREQ);
  delay(10);

  uint16_t mid = (PMIN + PMAX) / 2;
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motors[i].pos = motors[i].target = mid;
    pwm.setPWM(i, 0, mid);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  dns.start(53, "*", WiFi.softAPIP());

  server.on("/",        handleRoot);
  server.on("/start",   handleStart);
  server.on("/stop",    handleStop);
  server.on("/enable",  handleEnable);
  server.on("/disable", handleDisable);
  server.on("/state",   handleState);
  server.onNotFound(    handleNotFound);
  server.begin();

  Serial.println("Ready — join WiFi \"servoMotor\" / servoMotor123");
}

void loop() {
  dns.processNextRequest();
  server.handleClient();

  if (!running || !enabled) return;

  uint32_t now = millis();
  if (now - lastUpdate < UPDATE_MS) return;
  lastUpdate = now;

  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    Motor &m = motors[i];
    if (m.pos < m.target)      m.pos = min((uint16_t)(m.pos + m.step), m.target);
    else if (m.pos > m.target) m.pos = max((uint16_t)(m.pos - m.step), m.target);

    if (m.pos == m.target && now >= m.holdUntil) {
      m.target    = randomTarget();
      m.step      = randomStep();
      m.holdUntil = now + random(HOLD_MIN_MS, HOLD_MAX_MS);
    }
    pwm.setPWM(i, 0, m.pos);
  }
}