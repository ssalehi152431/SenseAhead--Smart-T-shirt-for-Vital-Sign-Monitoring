#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include <ArduinoGraphics.h>
#include <Wire.h>
#include <SparkFun_TMP117.h>
#include <ICM_20948.h>
#include <MAX30105.h>
#include "spo2_algorithm.h"   
#include <math.h>
#include <string.h>

//=======Wi fi config========
const char* STA_SSID = "iPhone";
const char* STA_PASS = "5859Irem";

//=======Cloud function===================
const char* CLOUD_HOST = "us-central1-vsm3-741ac.cloudfunctions.net";
const char* CLOUD_INGEST_PATH = "/api/ingest";
const uint16_t HTTPS_PORT = 443;

WiFiServer server(80);

TMP117 tmp117;
ICM_20948_I2C icm;
MAX30105 max30101;
float lastTempC = NAN;
bool max30101_ok = false;

//======ECG =======
const int PIN_ECG_OUT  = A0;
const int PIN_LO_PLUS  = 10;
const int PIN_LO_MINUS = 11;

//=======ECG configuration=======
const float FS_HZ = 250.0f;
const unsigned long TS_US = (unsigned long)(1000000.0f / FS_HZ);
const size_t ECG_BATCH = 250; //1 second per batch
uint16_t ecgBuf[ECG_BATCH];
size_t ecgIdx = 0;
unsigned long next_us = 0;

// ====== ECG detection ======
int ecgValue = 0;
int ecgMin = 1023;
int ecgMax = 0;
int ecgThreshold = 400;

unsigned long lastThreshUpdate = 0;
const unsigned long ECG_WINDOW = 1000;  

unsigned long lastBeatECG = 0;
float bpmECG = 0.0f;
bool ecgAbove = false;

const int N_ECG = 8;
float bpmBuffer[N_ECG];
int bpmIdx = 0;
bool bpmFull = false;

const unsigned long RR_MIN_MS = 580;   // antes 700
const unsigned long RR_MAX_MS = 1300;  // antes 1300
const float        BPM_MIN   = 45.0f;  // antes 45
const float        BPM_MAX   = 100.0f; // antes 90

//=============spo2============
volatile int32_t lastHR = 0;
volatile int32_t lastSpO2 = -1;
volatile int8_t  hrValid = 0;
volatile int8_t  spo2Valid = 0;

// Buffers PPG (IR / RED) 
const uint8_t PPG_BUF_LEN = 100;
uint32_t irBuf[PPG_BUF_LEN];
uint32_t redBuf[PPG_BUF_LEN];
uint8_t  ppgIndex = 0;

bool  spo2HavePrev  = false;
float spo2Prev      = 0.0f;
const float SPO2_ALPHA = 0.7f;

const float SPO2_OFFSET = 16.5f;
const float SPO2_SCALE  = 1.0f;

float lastR = 0.0f;

//======Values======
struct Sample {
  unsigned long ms;
  float acc[3];
  float gyr[3];
} latest;

//========Motion score===========
float accMagEMA = 0.0f;
float motionScore = 0.0f;
const float EMA_ALPHA = 0.05f;
const float MOTION_THRESH = 0.20f;

const float MOTION_GOOD = 0.03f;
const float MOTION_BAD  = 0.20f;

inline float motion_from_acc(const float a[3]) {
  float mag = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
  return fabsf(mag - 1.0f);
}

const char* motionQuality() {
  if (motionScore < MOTION_GOOD) return "GOOD";
  if (motionScore > MOTION_BAD)  return "BAD";
  return "MEDIUM";
}

//Simple timing for IMU, ECG, SPO2
unsigned long tIMU = 0;
unsigned long tUpload = 0;
const unsigned long IMU_PERIOD_MS = 50;
const unsigned long UPLOAD_PERIOD_MS = 1000;
const int16_t ECG_OFFSET = 512;

const byte ledBrightness = 47;
const byte sampleAverage = 2;   // 1,2,4,8,16,32
const byte ledMode       = 2;   // 2 = Red+IR
const byte sampleRate    = 100; // Hz
const int  pulseWidth    = 411; // us
const int  adcRange      = 4096;

// ========= Función SpO2 =========
float computeSpO2FromBuffer(const uint32_t* ir, const uint32_t* red, uint8_t len) {
  if (len == 0) return -1.0f;

  double sumIr = 0.0;
  double sumRed = 0.0;
  for (uint8_t i = 0; i < len; i++) {
    sumIr  += (double)ir[i];
    sumRed += (double)red[i];
  }

  double dcIr  = sumIr  / (double)len;
  double dcRed = sumRed / (double)len;

  if (dcIr < 1000 || dcRed < 1000) {
    Serial.println("SpO2: DC muy bajo -> mala colocacion (descartado)");
    return -1.0f;
  }

  // AC = RMS de (x - DC)
  double sumSqIrAC  = 0.0;
  double sumSqRedAC = 0.0;
  for (uint8_t i = 0; i < len; i++) {
    double diffIr  = (double)ir[i]  - dcIr;
    double diffRed = (double)red[i] - dcRed;
    sumSqIrAC  += diffIr  * diffIr;
    sumSqRedAC += diffRed * diffRed;
  }

  double rmsIrAC  = sqrt(sumSqIrAC  / (double)len);
  double rmsRedAC = sqrt(sumSqRedAC / (double)len);

  if (rmsIrAC < 30 || rmsRedAC < 30) {
    Serial.println("SpO2: AC muy pequeno -> sin pulso claro (descartado)");
    return -1.0f;
  }

  // Ratio R = (AC_red/DC_red) / (AC_ir/DC_ir)
  double rIr  = rmsIrAC  / dcIr;
  double rRed = rmsRedAC / dcRed;
  if (rIr <= 0 || rRed <= 0) {
    Serial.println("SpO2: ratio invalido (rIr<=0 o rRed<=0)");
    return -1.0f;
  }

  double R = rRed / rIr;
  lastR = (float)R;

  double spo2 = -45.060 * R * R + 30.354 * R + 94.845;

  Serial.print("DC_IR=");   Serial.print(dcIr, 1);
  Serial.print("  DC_RED=");Serial.print(dcRed, 1);
  Serial.print("  AC_IR="); Serial.print(rmsIrAC, 1);
  Serial.print("  AC_RED=");Serial.print(rmsRedAC, 1);
  Serial.print("  R=");     Serial.print(R, 3);
  Serial.print("  SpO2_raw=");
  Serial.println(spo2, 2);

  if (spo2 < 80.0) spo2 = 80.0;
  if (spo2 > 100.0) spo2 = 100.0;

  return (float)spo2;
}

//================ WiFi & HTTP helpers ==================
void startWiFiSTA() {
  Serial.println("Starting WiFi conection...");
  WiFi.begin(STA_SSID, STA_PASS);
  unsigned long t0 = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n Wi-Fi connected!");
    Serial.print("SSID: "); Serial.println(WiFi.SSID());
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n Wi-Fi connection failed.");
    Serial.println("Check SSID/password and ensure 2.4GHz network.");
  }

  server.begin();
}

void sendJsonLatest(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println("Access-Control-Allow-Origin: *");
  client.println();
  client.print("{\"ms\":"); client.print(latest.ms);
  client.print(",\"acc\":[");
  client.print(latest.acc[0],3); client.print(",");
  client.print(latest.acc[1],3); client.print(",");
  client.print(latest.acc[2],3);
  client.print("],\"gyr\":[");
  client.print(latest.gyr[0],3); client.print(",");
  client.print(latest.gyr[1],3); client.print(",");
  client.print(latest.gyr[2],3);
  if (!isnan(lastTempC)) {
    client.print(",\"tempC\":");
    client.print(lastTempC,2);
  }
  if (hrValid)   { client.print(",\"hr\":");   client.print(lastHR); }     
  if (spo2Valid) { client.print(",\"spo2\":"); client.print(lastSpO2); }
  client.print(",\"motion\":"); client.print(motionScore, 3);
  client.println("]}");
}

void sendTinyHTML (WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();
  client.println("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  client.println("<title>VSM Device</title></head><body style='font-family:sans-serif'>");
  client.println("<h2>VSM Device</h2>");
  client.println("<p>Use <code>/api/latest</code> for JSON. Your main web UI runs in VS Code.</p>");
  client.println("</body></html>");
}

//======POST JSON a Cloud Function==============
bool postToCloudIngest(const uint16_t* ecg, size_t n) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String payload;
  payload.reserve(128 + n * 4);
  payload += "{\"timestamp\":";
  payload += String((unsigned long)millis());
  payload += ",\"acc\":[";
  payload += String(latest.acc[0], 3) + "," + String(latest.acc[1], 3) + "," + String(latest.acc[2], 3);
  payload += "],\"gyr\":[";
  payload += String(latest.gyr[0], 3) + "," + String(latest.gyr[1], 3) + "," + String(latest.gyr[2], 3);
  payload += "]";
  if (!isnan(lastTempC)) {
    payload += ",\"tempC\":";
    payload += String(lastTempC, 2);
  }
  if (hrValid) {
    payload += ",\"hr\":";
    payload += String(lastHR);      
  }
  if (spo2Valid) {
    payload += ",\"spo2\":";
    payload += String(lastSpO2);
  }

  payload += ",\"ecg\":[";
  for (size_t i = 0; i < n; i++) {
    int16_t centered = (int16_t)ecg[i] - ECG_OFFSET;  
    payload += String(centered);                      
    if (i + 1 < n) payload += ",";
  }
  payload += "],\"motion\":";
  payload += String(motionScore, 3);
  payload += "}";

  WiFiSSLClient https;

  Serial.print("HTTPS connect -> "); Serial.print(CLOUD_HOST); Serial.print(" ... ");
  if (!https.connect(CLOUD_HOST, HTTPS_PORT)) {
    Serial.println("FAIL");
    return false;
  }
  Serial.println("OK");

  https.print(String("POST ") + CLOUD_INGEST_PATH + " HTTP/1.1\r\n");
  https.print(String("Host: ") + CLOUD_HOST + "\r\n");
  https.print("User-Agent: UNO-R4-WiFi\r\n");
  https.print("Content-Type: application/json\r\n");
  https.print("Connection: close\r\n");
  https.print(String("Content-Length: ") + payload.length() + "\r\n\r\n");
  https.print(payload);

  unsigned long t0 = millis();
  String statusLine;
  while (https.connected() && !https.available() && millis() - t0 < 3000) {
    delay(10);
  }
  if (https.available()) {
    statusLine = https.readStringUntil('\n');
    statusLine.trim();
  }

  while (https.available()) { https.read(); }
  https.stop();

  Serial.print("HTTP status: ");
  Serial.println(statusLine);
  return statusLine.indexOf("200") >= 0;
}

//====================SETUP======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C on Qwiic
  Wire1.begin();
  Wire1.setClock(400000);

  // ICM-20948
  Serial.println("ICM-20948 init...");
  if (icm.begin(Wire1, 0x69) != ICM_20948_Stat_Ok) {
    Serial.println("ICM not found");
    while (1);
  } else {
    Serial.println("ICM OK");
  }

  // ECG
  pinMode(PIN_LO_PLUS, INPUT);
  pinMode(PIN_LO_MINUS, INPUT);

  // TMP117
  Serial.println("TMP117 init...");
  if (!tmp117.begin(0x48, Wire1)) {
    Serial.println("TMP117 not found");
  } else {
    Serial.println("TMP117 OK");
    tmp117.setConversionAverageMode(3); //for noise
  }

  // SpO2 (MAX30101)
  Serial.println("MAX30101 init...");
  if (max30101.begin(Wire1, I2C_SPEED_FAST)) {
    max30101.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    max30101.setPulseAmplitudeRed(ledBrightness);
    max30101.setPulseAmplitudeIR(ledBrightness);
    Serial.println("MAX30101 OK");
    max30101_ok = true;
  } else {
    Serial.println("MAX30101 not found (continuing without SpO2).");
    max30101_ok = false;
  }

  // WiFi
  startWiFiSTA();

  latest.ms = millis();
  latest.acc[0] = latest.acc[1] = latest.acc[2] = 0;
  latest.gyr[0] = latest.gyr[1] = latest.gyr[2] = 0;

  ecgMin = 1023;
  ecgMax = 0;
  lastThreshUpdate = millis();
  for (int i = 0; i < N_ECG; i++) bpmBuffer[i] = 0;

  Serial.println("Ready");
}

//====================LOOP======================
void loop() {
  const unsigned long now_ms = millis();
  const unsigned long now_us = micros();
  static unsigned long tTemp = 0;

  //======== IMU =========
  if (now_ms - tIMU >= IMU_PERIOD_MS) {
    tIMU = now_ms;
    if (icm.dataReady()) {
      icm.getAGMT();
      latest.acc[0] = icm.accX() / 1000.0f;
      latest.acc[1] = icm.accY() / 1000.0f;
      latest.acc[2] = icm.accZ() / 1000.0f;
      latest.gyr[0] = icm.gyrX();
      latest.gyr[1] = icm.gyrY();
      latest.gyr[2] = icm.gyrZ();
    }
    latest.ms = now_ms;
    float m = motion_from_acc(latest.acc);
    accMagEMA = (1.0f - EMA_ALPHA) * accMagEMA + EMA_ALPHA * m;
    motionScore = accMagEMA;
  }

  const char* mQuality = motionQuality();

  //======== ECG =========
  if ((long)(now_us - next_us) >= 0) {
    next_us += TS_US;

    if (digitalRead(PIN_LO_PLUS) == HIGH || digitalRead(PIN_LO_MINUS) == HIGH) {
      return;   
    }

    uint16_t sample = analogRead(PIN_ECG_OUT);

    if (sample <= 5 || sample >= 1018) {
      return;
    }

    ecgBuf[ecgIdx++] = sample;
    if (ecgIdx >= ECG_BATCH) ecgIdx = 0;

    ecgValue = sample;
    unsigned long now = millis();

    if (lastBeatECG != 0 && (now - lastBeatECG > 4000)) {
      hrValid     = 0;
      lastBeatECG = 0;   
    }

    if (ecgValue < ecgMin) ecgMin = ecgValue;
    if (ecgValue > ecgMax) ecgMax = ecgValue;

    if (now - lastThreshUpdate > ECG_WINDOW) {
      int amp = ecgMax - ecgMin;
      if (amp > 20) {
        ecgThreshold = ecgMin + 0.3 * amp;
      }
      Serial.print("ecgMin="); Serial.print(ecgMin);
      Serial.print(" ecgMax="); Serial.print(ecgMax);
      Serial.print(" amp=");    Serial.print(amp);
      Serial.print(" -> Thr="); Serial.println(ecgThreshold);

      ecgMin = 1023;
      ecgMax = 0;
      lastThreshUpdate = now;
    }

    bool ecgSignalOk    = (ecgThreshold > 150 && ecgThreshold < 900);
    bool pocoMovimiento = true; 

    if (pocoMovimiento &&
        ecgSignalOk &&
        !ecgAbove &&
        ecgValue > ecgThreshold) {

      ecgAbove = true;
      unsigned long nowBeat = now;

      if (lastBeatECG == 0 || (now - lastBeatECG > 2000)) {
        lastBeatECG = nowBeat;
        Serial.print("PICO: ecg="); Serial.print(ecgValue);
        Serial.print(" thr=");      Serial.print(ecgThreshold);
        Serial.println("  -> primer latido / reinicio RR");
      } else {
        unsigned long rr = nowBeat - lastBeatECG;

        Serial.print("PICO: ecg="); Serial.print(ecgValue);
        Serial.print(" thr=");      Serial.print(ecgThreshold);
        Serial.print(" rr=");       Serial.print(rr);

        float inst = 60000.0f / rr;   
        Serial.print(" instBPM="); Serial.print(inst);

        bool okRR  = (rr  >= RR_MIN_MS && rr  <= RR_MAX_MS);
        bool okBPM = (inst >= BPM_MIN   && inst <= BPM_MAX);

        if (!okRR) {
          Serial.println("  -> RR fuera de rango (latido ignorado)");
        }
        else if (!okBPM) {
          Serial.println("  -> BPM fuera de rango (latido ignorado)");
        }
        else {
          lastBeatECG = nowBeat;  

          bpmBuffer[bpmIdx] = inst;
          bpmIdx++;
          if (bpmIdx >= N_ECG) { bpmIdx = 0; bpmFull = true; }

          int   count = bpmFull ? N_ECG : bpmIdx;
          float sum   = 0;
          for (int i = 0; i < count; i++) sum += bpmBuffer[i];
          bpmECG = sum / count;

          lastHR  = (int32_t)round(bpmECG);
          hrValid = 1;

          Serial.print("  -> LATIDO ACEPTADO  (HR_ECG_prom=");
          Serial.print(bpmECG);
          Serial.println(")");
        }
      }
    }

    if (ecgAbove && ecgValue < ecgThreshold - 5) {
      ecgAbove = false;
    }
  }

  //======== TEMP =========
  if (millis() - tTemp >= 100) {
    tTemp = millis();
    if (tmp117.dataReady()) {
      lastTempC = tmp117.readTempC();
    }
  }

  //======== SPO2 =========
  if (max30101_ok && max30101.check()) {
    while (max30101.available()) {
      uint32_t ir  = max30101.getIR();
      uint32_t red = max30101.getRed();
      max30101.nextSample();

      irBuf[ppgIndex]  = ir;
      redBuf[ppgIndex] = red;
      ppgIndex++;

      if (ppgIndex >= PPG_BUF_LEN) {
        Serial.println("************* VENTANA COMPLETA (100 muestras) *************");
        ppgIndex = 0;

        float spo2Raw = computeSpO2FromBuffer(irBuf, redBuf, PPG_BUF_LEN);
        bool lowMotion = (motionScore < MOTION_THRESH);

        Serial.println("-------------------------------------------------");
        Serial.print("SpO2 crudo (ventana): ");
        Serial.print(spo2Raw, 2);
        Serial.print(" | R = ");
        Serial.print(lastR, 3);
        Serial.print(" | motionScore = ");
        Serial.print(motionScore, 3);
        Serial.print(" (");
        Serial.print(lowMotion ? "BAJO" : "ALTO");
        Serial.println(" movimiento)");

        if (spo2Raw > 0 && spo2Raw <= 100 && lowMotion) {
          float spo2Cal = SPO2_SCALE * spo2Raw + SPO2_OFFSET;
          if (spo2Cal > 100.0f) spo2Cal = 100.0f;
          if (spo2Cal < 70.0f)  spo2Cal = 70.0f;

          Serial.print("SpO2 calibrado (antes de suavizar) = ");
          Serial.println(spo2Cal, 2);

          if (!spo2HavePrev || fabs(spo2Cal - spo2Prev) > 3.0f) {
            spo2Prev     = spo2Cal;
            spo2HavePrev = true;
          } else {
            spo2Prev = SPO2_ALPHA * spo2Cal + (1.0f - SPO2_ALPHA) * spo2Prev;
          }

          int spo2Smooth = (int)round(spo2Prev);

          lastSpO2  = spo2Smooth;
          spo2Valid = 1;

        } else {
          spo2Valid = 0;
        }

      }
    }
  }

  //======== UPLOAD TO CLOUD – CADA ~5 s =========
  const unsigned long UPLOAD_PERIOD_MS_NEW = 5000;
  if (WiFi.status() == WL_CONNECTED &&
      (now_ms - tUpload >= UPLOAD_PERIOD_MS_NEW) &&
      ecgIdx > 0) {

    bool ok = postToCloudIngest(ecgBuf, ecgIdx);
    Serial.println(ok ? "Upload ECG+SPO2: OK" : "Upload ECG+SPO2: FAIL");
    ecgIdx = 0;
    tUpload = now_ms;
  }

  // ---------- HTTP local debug ----------
  WiFiClient client = server.available();
  if (client) {
    String line = "";
    bool firstLine = true;
    String reqLine;

    while (client.connected()) {
      if (!client.available()) continue;
      char c = client.read();
      line += c;

      if (c == '\n') {
        if (firstLine) { reqLine = line; firstLine = false; }
        if (line == "\r\n" || line == "\n") {
          if (reqLine.startsWith("GET /api/latest")) {
            sendJsonLatest(client);
          } else {
            sendTinyHTML(client);
          }
          break;
        }
        line = "";
      }
    }
    client.stop();
  }
  Serial.print("ECG=");
  Serial.print(ecgValue);
  Serial.print("  Thr=");
  Serial.print(ecgThreshold);
  Serial.print("  motionScore=");
  Serial.print(motionScore,3);
  Serial.print("  motionQuality=");
  Serial.println(motionQuality());

  delay(5);
}

