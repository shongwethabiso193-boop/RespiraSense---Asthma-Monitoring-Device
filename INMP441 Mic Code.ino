// ESP32: INMP441 (I2S) + Parallel 16x2 LCD + Web server showing Breathing Status
// Includes: Fancy breathing UI + danger alert (🚨🔥 using decimal HTML entities)
// Libraries
#include <WiFi.h>
#include <WebServer.h>
#include <driver/i2s.h>
#include <LiquidCrystal.h>

// ---------- WiFi ----------
const char* ssid = "Thabiso";
const char* password = "222134455";

// ---------- LCD Pin Config ----------
#define LCD_RS  16
#define LCD_E   17
#define LCD_D4  18
#define LCD_D5  19
#define LCD_D6  23
#define LCD_D7  5

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ---------- I2S Mic Config (INMP441) ----------
#define I2S_WS   25  // LRCL
#define I2S_SD   32  // Data
#define I2S_SCK  26  // BCLK
#define I2S_PORT I2S_NUM_0
#define BUFFER_LEN 256

int16_t sBuffer[BUFFER_LEN];

// ---------- I2S ----------
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_set_pin(I2S_PORT, &pin_config);
}

// ---------- Compute amplitude ----------
double getAmplitude() {
  size_t bytes_read = 0;
  esp_err_t result = i2s_read(I2S_PORT, (void*)sBuffer, BUFFER_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY);
  if (result != ESP_OK || bytes_read == 0) return 0;

  int samples_read = bytes_read / sizeof(int16_t);
  double sum_squares = 0;

  for (int i = 0; i < samples_read; i++) {
    float sample = sBuffer[i] / 32768.0;
    sum_squares += sample * sample;
  }

  double rms = sqrt(sum_squares / samples_read);
  return rms * 1000.0; // amplified
}

// ---------- Moving average ----------
#define AVG_SIZE 8
double ampBuffer[AVG_SIZE] = {0};
int indexAvg = 0;

double smoothAmplitude(double newVal) {
  ampBuffer[indexAvg] = newVal;
  indexAvg = (indexAvg + 1) % AVG_SIZE;

  double sum = 0;
  for (int i = 0; i < AVG_SIZE; i++) sum += ampBuffer[i];
  return sum / AVG_SIZE;
}

// ---------- Global ----------
String gStatus = "Unknown";
double gAmplitude = 0;

WebServer server(80);

// ---------- HTML PAGE ----------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Breathing Monitor</title>

<style>
body {
  font-family: Arial;
  display:flex;
  align-items:center;
  justify-content:center;
  height:100vh;
  margin:0;
  background: linear-gradient(160deg,#0f172a,#0b1220);
  color:#e6eef8;
}

.card {
  width:320px;
  padding:20px;
  border-radius:16px;
  box-shadow: 0 10px 30px rgba(0,0,0,0.6);
  background: rgba(255,255,255,0.04);
  backdrop-filter: blur(6px);
  text-align:center;
}

h1 { margin:6px 0 12px; font-size:20px; }

.breath-wrap {
  display:flex;
  align-items:center;
  justify-content:center;
  margin:10px 0 18px;
}

.bubble {
  width:120px;
  height:120px;
  border-radius:50%;
  display:flex;
  align-items:center;
  justify-content:center;
  background: radial-gradient(circle, rgba(255,255,255,0.1), rgba(255,255,255,0.02));
  animation:breathe 2400ms ease-in-out infinite;
}

@keyframes breathe {
  0% { transform: scale(0.92); }
  50% { transform: scale(1.06); }
  100% { transform: scale(0.92); }
}

.face { font-size:45px; }

.status {
  margin-top:6px;
  padding:6px 12px;
  border-radius:999px;
  font-weight:600;
  display:inline-block;
}

.status.normal { color:#06b6d4; background:rgba(6,182,212,0.08); }
.status.background { color:#94a3b8; background:rgba(148,163,184,0.08); }
.status.high { color:#ff6b6b; background:rgba(255,107,107,0.1); }

.num { margin-top:12px; font-size:13px; color:#cde7ff; }

/* FLASHING RED ALERT */
.alertBox {
  margin-top:16px;
  padding:12px;
  border-radius:12px;
  font-size:15px;
  font-weight:700;
  color:#ff4d4d;
  background:rgba(255,50,50,0.15);
  display:none;
  animation:flashAlert 1.2s linear infinite;
  box-shadow:0 0 10px rgba(255,0,0,0.5);
}

@keyframes flashAlert {
  0% { opacity:0.55; }
  50% { opacity:1; }
  100% { opacity:0.55; }
}
</style>
</head>

<body>
  <div class="card">
    <h1>Breathing Monitor</h1>

    <div class="breath-wrap">
      <div class="bubble" id="bubble">
        <div class="face" id="face">&#128522;</div>
      </div>
    </div>

    <div id="statBadge" class="status">--</div>
    <div class="num" id="num">Amplitude: --</div>

    <!-- 🔥🚨 DANGER ALERT BOX -->
    <div id="dangerAlert" class="alertBox">
      &#128680; ALERT: High Breathing Detected! &#128293;
    </div>
  </div>

<script>
async function fetchStatus() {
  try {
    const resp = await fetch("/status");
    const j = await resp.json();

    const statusText = j.status;
    const amp = Number(j.amplitude).toFixed(2);

    // Update badge
    const badge = document.getElementById("statBadge");
    badge.textContent = statusText;
    badge.className = "status";
    if (statusText === "Normal") badge.classList.add("normal");
    else if (statusText === "Background") badge.classList.add("background");
    else badge.classList.add("high");

    document.getElementById("num").textContent = "Amplitude: " + amp;

    // Update face emoji
    const face = document.getElementById("face");
    if (statusText === "Background") face.innerHTML = "&#128528;";
    else if (statusText === "Normal") face.innerHTML = "&#128522;";
    else face.innerHTML = "&#128557;";

    // Breathing speed
    const bubble = document.getElementById("bubble");
    bubble.style.animationDuration = (j.amplitude > 40 ? 1600 : j.amplitude > 20 ? 2000 : 2400) + "ms";

    // SHOW / HIDE danger alert
    const alertBox = document.getElementById("dangerAlert");
    alertBox.style.display = (statusText === "High Breathing!!") ? "block" : "none";

  } catch(e) {}
}

setInterval(fetchStatus, 400);
fetchStatus();
</script>

</body>
</html>
)rawliteral";

// ---------- SERVER ----------
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleStatus() {
  String payload = "{";
  payload += "\"status\":\"" + gStatus + "\",";
  payload += "\"amplitude\":" + String(gAmplitude, 2);
  payload += "}";
  server.send(200, "application/json", payload);
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.print("Asthma Monitor");
  delay(1500);

  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);

  Serial.println("Connecting to WiFi...");
WiFi.mode(WIFI_STA);
WiFi.begin(ssid, password);

int tries = 0;
while (WiFi.status() != WL_CONNECTED && tries < 40) {  
    Serial.print(".");
    delay(250);
    tries++;
}

Serial.println();

if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.print("IP:");
    lcd.setCursor(0,1);
    lcd.print(WiFi.localIP().toString());
} else {
    Serial.println("WiFi FAILED!");
    lcd.clear();
    lcd.print("WiFi FAILED");
}

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();

  delay(1000);
  lcd.clear();
  lcd.print("Listening...");
}

// ---------- LOOP ----------
void loop() {
  double amp = getAmplitude();
  double smoothAmp = smoothAmplitude(amp);
  gAmplitude = smoothAmp;

  if (smoothAmp < 10) gStatus = "Background";
  else if (smoothAmp < 40) gStatus = "Normal";
  else gStatus = "High Breathing!!";

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Breathing:");
  lcd.setCursor(0,1);
  lcd.print(gStatus);

  server.handleClient();
  delay(400);
}
