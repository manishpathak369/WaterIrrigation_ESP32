/*
  ESP32 Automatic Water Irrigation System
  - Creates its own Wi-Fi access point
  - Web portal at http://192.168.4.1
  - Displays soil moisture percentage and pump status
  - Supports AUTO and MANUAL modes
  - AUTO mode: pump ON at/below 30%, OFF at/above 45%
  - 16x2 LCD in 4-bit parallel mode

  Required library:
    LiquidCrystal (usually included with Arduino IDE)

  IMPORTANT:
  1. Calibrate DRY_RAW and WET_RAW for your own sensor.
  2. Check whether your relay is active HIGH or active LOW.
  3. Never power a pump directly from an ESP32 GPIO.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h>

// ---------------- Wi-Fi access point ----------------
const char* AP_SSID = "Smart_Irrigation";
const char* AP_PASSWORD = "Water123";  // At least 8 characters

// ---------------- Pin configuration -----------------
const int SOIL_PIN  = 34;  // Analog output from soil moisture sensor
const int RELAY_PIN = 26;  // Relay module IN

// LCD pins: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(21, 22, 18, 19, 23, 25);

// ---------------- Sensor calibration ----------------
// Replace these with readings from YOUR sensor.
// Read the raw value with the sensor in dry soil/air and wet soil.
const int DRY_RAW = 3000;
const int WET_RAW = 1300;

// ---------------- Irrigation thresholds -------------
const int PUMP_ON_BELOW  = 30;  // Start watering at/below 30%
const int PUMP_OFF_ABOVE = 45;  // Stop watering at/above 45%

// Many relay modules are active LOW. Change to false if yours is active HIGH.
const bool RELAY_ACTIVE_LOW = true;

// ---------------- Application state -----------------
WebServer server(80);

enum ControlMode { AUTO_MODE, MANUAL_MODE };
ControlMode controlMode = AUTO_MODE;

bool pumpOn = false;
int soilRaw = 0;
int soilPercent = 0;

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL_MS = 1000;

// Set relay output and remember state.
void setPump(bool turnOn) {
  pumpOn = turnOn;

  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_PIN, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);
  }
}

// Read sensor and convert raw ADC value to an approximate percentage.
// The map function works whether WET_RAW is lower or higher than DRY_RAW.
void readSoilSensor() {
  soilRaw = analogRead(SOIL_PIN);

  if (DRY_RAW == WET_RAW) {
    soilPercent = 0;
  } else {
    long percent = map(soilRaw, DRY_RAW, WET_RAW, 0, 100);
    soilPercent = constrain((int)percent, 0, 100);
  }
}

// Automatic control with hysteresis to prevent rapid relay switching.
void updateAutomaticControl() {
  if (controlMode != AUTO_MODE) return;

  if (!pumpOn && soilPercent <= PUMP_ON_BELOW) {
    setPump(true);
  } else if (pumpOn && soilPercent >= PUMP_OFF_ABOVE) {
    setPump(false);
  }
}

String modeText() {
  return controlMode == AUTO_MODE ? "AUTO" : "MANUAL";
}

String pumpText() {
  return pumpOn ? "ON" : "OFF";
}

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("Moisture: ");
  if (soilPercent < 100) lcd.print(" ");
  if (soilPercent < 10) lcd.print(" ");
  lcd.print(soilPercent);
  lcd.print("%   ");

  lcd.setCursor(0, 1);
  lcd.print("Pump:");
  lcd.print(pumpText());
  lcd.print(" ");
  lcd.print(modeText());
  lcd.print("       ");
}

String makePage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Irrigation</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 20px; font-family: Arial, sans-serif;
    background: #eef4f0; color: #1d3026;
  }
  .container { max-width: 560px; margin: 25px auto; }
  .card {
    background: white; padding: 22px; margin-bottom: 16px;
    border-radius: 16px; box-shadow: 0 4px 18px rgba(0,0,0,.07);
  }
  h1 { margin: 0 0 8px; font-size: 26px; }
  .muted { color: #64756b; }
  .reading { font-size: 48px; font-weight: bold; margin: 12px 0; }
  .bar { height: 14px; background: #dce8df; border-radius: 20px; overflow: hidden; }
  .fill { height: 100%; width: 0%; background: #23965b; transition: width .3s; }
  .row { display: flex; justify-content: space-between; gap: 12px; }
  .value { font-weight: bold; }
  button {
    border: 0; border-radius: 10px; padding: 13px 17px;
    margin: 6px 5px 0 0; font-size: 15px; cursor: pointer;
    color: white; background: #287b50;
  }
  button.secondary { background: #52665a; }
  button.off { background: #b44b43; }
  button:disabled { opacity: .4; cursor: not-allowed; }
  .status { font-size: 20px; font-weight: bold; }
  .notice { font-size: 13px; line-height: 1.5; }
</style>
</head>
<body>
<div class="container">
  <div class="card">
    <h1>Automatic Water Irrigation</h1>
    <div class="muted">ESP32 live monitoring and pump control</div>
  </div>

  <div class="card">
    <div class="row"><span>Soil moisture</span><span class="muted">Live reading</span></div>
    <div class="reading"><span id="moisture">--</span><span>%</span></div>
    <div class="bar"><div class="fill" id="bar"></div></div>
    <p class="muted">Raw sensor value: <span id="raw">--</span></p>
  </div>

  <div class="card">
    <div class="row"><span>Pump status</span><span class="status" id="pump">--</span></div>
    <div class="row"><span>Control mode</span><span class="value" id="mode">--</span></div>
    <p class="muted">Automatic: ON at 30% or below; OFF at 45% or above.</p>
  </div>

  <div class="card">
    <h3>Control mode</h3>
    <button onclick="setMode('auto')">Automatic mode</button>
    <button class="secondary" onclick="setMode('manual')">Manual mode</button>
    <h3 style="margin-top:22px">Manual pump control</h3>
    <button onclick="setPump('on')" id="onBtn">Pump ON</button>
    <button class="off" onclick="setPump('off')" id="offBtn">Pump OFF</button>
    <p class="notice muted">Manual pump buttons work only in Manual mode. In Automatic mode, the ESP32 controls the pump from the moisture reading.</p>
    <p id="message" class="notice"></p>
  </div>
</div>
<script>
async function updateStatus() {
  try {
    const response = await fetch('/status');
    const data = await response.json();
    document.getElementById('moisture').textContent = data.moisture;
    document.getElementById('raw').textContent = data.raw;
    document.getElementById('bar').style.width = data.moisture + '%';
    document.getElementById('pump').textContent = data.pump;
    document.getElementById('mode').textContent = data.mode;
    document.getElementById('onBtn').disabled = data.mode !== 'MANUAL';
    document.getElementById('offBtn').disabled = data.mode !== 'MANUAL';
  } catch (error) {
    document.getElementById('message').textContent = 'Connection lost. Reconnecting...';
  }
}
async function sendCommand(url) {
  try {
    const response = await fetch(url, {method: 'POST'});
    const result = await response.text();
    document.getElementById('message').textContent = result;
    updateStatus();
  } catch (error) {
    document.getElementById('message').textContent = 'Command failed. Check connection.';
  }
}
function setMode(mode) { sendCommand('/mode?value=' + mode); }
function setPump(state) { sendCommand('/pump?value=' + state); }
updateStatus();
setInterval(updateStatus, 1500);
</script>
</body>
</html>
)rawliteral";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", makePage());
}

void handleStatus() {
  String json = "{";
  json += "\"moisture\":" + String(soilPercent) + ",";
  json += "\"raw\":" + String(soilRaw) + ",";
  json += "\"pump\":\"" + pumpText() + "\",";
  json += "\"mode\":\"" + modeText() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleMode() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing mode");
    return;
  }

  String value = server.arg("value");
  if (value == "auto") {
    controlMode = AUTO_MODE;
    updateAutomaticControl();
    server.send(200, "text/plain", "Automatic mode enabled");
  } else if (value == "manual") {
    controlMode = MANUAL_MODE;
    server.send(200, "text/plain", "Manual mode enabled");
  } else {
    server.send(400, "text/plain", "Invalid mode");
  }
  updateLCD();
}

void handlePump() {
  if (controlMode != MANUAL_MODE) {
    server.send(409, "text/plain", "Switch to Manual mode to control the pump");
    return;
  }

  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing pump state");
    return;
  }

  String value = server.arg("value");
  if (value == "on") {
    setPump(true);
    server.send(200, "text/plain", "Pump turned ON");
  } else if (value == "off") {
    setPump(false);
    server.send(200, "text/plain", "Pump turned OFF");
  } else {
    server.send(400, "text/plain", "Invalid pump state");
  }
  updateLCD();
}

void setup() {
  Serial.begin(115200);

  // Set the relay OFF before normal operation.
  pinMode(RELAY_PIN, OUTPUT);
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
  pumpOn = false;

  analogReadResolution(12); // ESP32 ADC range: 0 to 4095
  pinMode(SOIL_PIN, INPUT);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Irrigation");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println();
  Serial.println("Wi-Fi Access Point started");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/pump", HTTP_POST, handlePump);
  server.begin();

  readSoilSensor();
  updateAutomaticControl();
  updateLCD();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    readSoilSensor();
    updateAutomaticControl();
    updateLCD();

    Serial.print("Raw: ");
    Serial.print(soilRaw);
    Serial.print(" | Moisture: ");
    Serial.print(soilPercent);
    Serial.print("% | Pump: ");
    Serial.print(pumpText());
    Serial.print(" | Mode: ");
    Serial.println(modeText());
  }
}
