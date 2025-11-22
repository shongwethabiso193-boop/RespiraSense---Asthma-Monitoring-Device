#include <Wire.h>
#include <LiquidCrystal.h>
#include <MAX30105.h>
#include "spo2_algorithm.h"
#include <WiFi.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "Thabiso";
const char* password = "222134455";

// Create web server on port 80
WebServer server(80);

// Create sensor and LCD objects
MAX30105 particleSensor;
LiquidCrystal lcd(16, 17, 18, 19, 23, 5);  // RS, E, D4, D5, D6, D7

#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// Piezo sensor for RR
#define SENSOR_PIN 34       // Piezo stretch sensor input
#define SAMPLE_TIME 10000   // Measurement window in milliseconds (10 seconds)
float rrBps = 0;            // Respiration rate BPM

// ============ HTML PAGE WITH AUTO-REFRESH ============
String htmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Health Monitoring System</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 30px; background-color: #f7f7f7; }
    h1 { color: #00796B; }
    p.note { font-size: 14px; color: #666; }
    .card { background: white; margin: 20px auto; padding: 20px; border-radius: 15px; width: 80%; max-width: 350px; box-shadow: 0 2px 6px rgba(0,0,0,0.2); }
    .card h2 { margin: 10px 0; font-weight: normal; }
    .icon { font-size: 32px; vertical-align: middle; margin-right: 8px; }
    .value { font-size: 26px; font-weight: bold; color: #333; }
  </style>
</head>
<body>
  <h1>Asthma Monitoring Device</h1>
  <p class="note">NOTE: Test readings might differ from actual sensor readings</p>
  <div class="card">
  <h2>
    <span class="icon">
      <!-- Heart SVG -->
      <svg width="30" height="30" viewBox="0 0 24 24" fill="#ff4d4d">
        <path d="M12 21s-6.7-4.35-10-9c-3-4.2-.5-10 5-10 3 0 5 2 5 2s2-2 5-2c5.5 0 8 5.8 5 10-3.3 4.65-10 9-10 9z"/>
      </svg>
    </span>
    Heart Rate
  </h2>
  <div id="hr" class="value">-- BPM</div>
</div>

<div class="card">
  <h2><span class="icon">&#128167;</span>SpO2</h2> <!-- 💧 emoji using HTML entity -->
  <div id="spo2" class="value">-- %</div>
</div>


<div class="card">  
  <h2>
    <span class="icon">
      <!-- ULTRA-REALISTIC LUNG SVG -->
      <svg width="60" height="60" viewBox="0 0 200 200">
        
        <!-- Soft breathing shading -->
        <defs>
          <radialGradient id="lungGradL" cx="40%" cy="40%" r="70%">
            <stop offset="0%" stop-color="#ff9999"/>
            <stop offset="60%" stop-color="#e94a4a"/>
            <stop offset="100%" stop-color="#b82525"/>
          </radialGradient>

          <radialGradient id="lungGradR" cx="60%" cy="40%" r="70%">
            <stop offset="0%" stop-color="#ffb3b3"/>
            <stop offset="60%" stop-color="#e84848"/>
            <stop offset="100%" stop-color="#b12222"/>
          </radialGradient>

          <linearGradient id="bronchiGrad" x1="0%" y1="0%" x2="0%" y2="100%">
            <stop offset="0%" stop-color="#ffffff"/>
            <stop offset="100%" stop-color="#cfcfcf"/>
          </linearGradient>
        </defs>

        <!-- Trachea -->
        <path d="M95 20 C95 10, 105 10, 105 20 V70 H95 Z"
              fill="url(#bronchiGrad)" stroke="#888" stroke-width="2"/>

        <!-- Bronchi main split -->
        <path d="M100 70 
                 C70 90, 60 110, 55 140
                 M100 70
                 C130 90, 140 110, 145 140"
              stroke="#eee" stroke-width="6" fill="none"
              stroke-linecap="round"/>

        <!-- Left lung (3D shaded) -->
        <path d="M95 70 
                 C40 60, 20 110, 30 160 
                 C40 200, 90 190, 95 150 Z"
              fill="url(#lungGradL)" stroke="#8a1f1f" stroke-width="3"/>

        <!-- Right lung -->
        <path d="M105 70 
                 C160 60, 180 110, 170 160 
                 C160 200, 110 190, 105 150 Z"
              fill="url(#lungGradR)" stroke="#8a1f1f" stroke-width="3"/>

        <!-- Bronchi branches -->
        <path d="M100 75 
                 C80 95, 75 115, 78 135
                 M100 78
                 C120 95, 125 115, 122 135

                 M78 135 
                 C70 150, 65 160, 68 170
                 M122 135
                 C130 150, 135 160, 132 170"
              stroke="#f2f2f2" stroke-width="4" fill="none"
              stroke-linecap="round"/>

      </svg>
    </span>

    Resp Rate
  </h2>
  <div id="rr" class="value">-- BPS</div>
</div>
  <div id="alertMsg" style="color:red; font-weight:bold; margin-top:20px">
  .alertValue {color: red !important;font-weight: bold;}
  </div>
  <script>
async function fetchData() {
  const res = await fetch('/data');
  const json = await res.json();

  document.getElementById('hr').innerHTML = json.hr + " BPM";
  document.getElementById('spo2').innerHTML = json.spo2 + " %";
  document.getElementById('rr').innerHTML = json.rr + " BPS";

  // Remove previous alert styling
  document.getElementById('hr').classList.remove('alertValue');
  document.getElementById('spo2').classList.remove('alertValue');
  document.getElementById('rr').classList.remove('alertValue');

  // Apply red alert dynamically
  let alerts = [];
  if (json.rr > 1200) {
    document.getElementById('rr').classList.add('alertValue');
    alerts.push("RR > 1200");
  }
  if (json.spo2 > 100) {
    document.getElementById('spo2').classList.add('alertValue');
    alerts.push("SpO2 > 100");
  }
  if (json.hr > 100) {
    document.getElementById('hr').classList.add('alertValue');
    alerts.push("HR > 100");
  }

  // Update alert message
  document.getElementById('alertMsg').innerText = 
      alerts.length > 0 ? "ALERT: " + alerts.join(" & ") : "";
}

// Fetch every second
setInterval(fetchData, 1000);
fetchData();
</script>
</body>
</html>
  )rawliteral";
  return html;
}
void handleRoot() {
  server.send(200, "text/html", htmlPage());
}
// ============ SERVER HANDLERS ============
  void handleData() {
  // RR alert threshold > 3050
  bool rrAlert = (rrBps > 1200);

  // Alert if both SpO2 > 100 AND HR > 100
  bool spo2HrAlert = (spo2 > 100 && heartRate > 100);

  // Trigger alert if any condition is true
  bool alert = rrAlert || spo2HrAlert;

  String json = "{\"hr\":" + String(heartRate) +
                ",\"spo2\":" + String(spo2) +
                ",\"rr\":" + String(rrBps) +
                ",\"alert\":" + String(alert ? "true" : "false") + "}";

  server.send(200, "application/json", json);
}
// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.print("Initializing...");

  pinMode(SENSOR_PIN, INPUT);

  // Initialize MAX30102
  if (!particleSensor.begin(Wire)) {
    lcd.clear();
    lcd.print("MAX30102 error");
    Serial.println("MAX30102 not found. Check wiring!");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);

  lcd.clear();
  lcd.print("Place Finger...");
  delay(2000);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  lcd.clear();
  lcd.print("Connecting WiFi...");
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  lcd.clear();
  lcd.print("WiFi Connected");
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Start web server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP server started");
}

// ============ MAIN LOOP ============
void loop() {
  // ---- Respiration Rate Measurement ----
  unsigned long startTime = millis();
  int breathCount = 0;
  int sensorValue = 0;
  bool lastState = false;

  while (millis() - startTime < SAMPLE_TIME) {
    sensorValue = analogRead(SENSOR_PIN);
    bool currentState = (sensorValue > 2000);  // Adjust threshold if needed
    if (currentState && !lastState) {
      breathCount++;
    }
    lastState = currentState;
  }

  rrBps = (breathCount / (SAMPLE_TIME / 1000.0));

  // ---- MAX30102 Measurement ----
  for (int i = 0; i < BUFFER_SIZE; i++) {
    while (!particleSensor.check()) { delay(1); }
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE, redBuffer,
    &spo2, &validSPO2, &heartRate, &validHeartRate
  );

  // ---- Update LCD ----
  lcd.clear();
  if (validSPO2 && validHeartRate) {
    lcd.setCursor(0, 0);
    lcd.print("SpO2:");
    lcd.print(spo2);
    lcd.print("%");
    lcd.setCursor(0, 1);
    lcd.print("HR:");
    lcd.print(heartRate);
    lcd.print(" bpm");
  } else {
    lcd.print("Reading...");
  }

  // ---- Serial Alerts ----
  if (rrBps > 1200) { Serial.println("ALERT: RR > 1200!"); }
  if (spo2 > 100) { Serial.println("ALERT: SpO2 > 100!"); }
  if (heartRate > 100) { Serial.println("ALERT: HR > 100!"); }

  // ---- Handle web server clients ----
  server.handleClient();

  delay(2000);
}  // <- THIS closing brace now correctly closes loop()