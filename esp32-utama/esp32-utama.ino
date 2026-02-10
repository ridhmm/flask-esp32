#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <vector>

/* ================= KONFIGURASI ================= */
#define LED_DENY_PIN 13
#define LED_OK_PIN 27
#define BUZZER_PIN 14

#define PIN_RST 0
#define PIN_SDA1 5
#define PIN_SDA2 17

#define JML_READER 2

#define TCA_ADDR 0x70
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define PIR_IGNORE_TIME 3000
#define PIR_CONFIRM_TIME 3000
#define INVERT_DURATION 2000
#define EVENT_DISPLAY_DURATION 3000
#define BUZZER_DENY_MS 200
#define BUZZER_OK_MS 600

#define MAX_CARD 100
#define SERVER_TIMEOUT 60000

/* ================= WIFI & NTP ================= */
const char* ssid = "WiFi";
const char* password = "!@#!@#!@#";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // WIB
const int daylightOffset_sec = 0;

/* ================= MQTT ================= */
const char* mqttServer = "192.168.1.111";
const int mqttPort = 1884;
WiFiClient espClient;
PubSubClient mqtt(espClient);

/* ================= ENUM ================= */
enum GateType { GATE_MASUK,
                GATE_KELUAR };
GateType gateType[JML_READER] = { GATE_MASUK, GATE_KELUAR };

enum GateState { IDLE,
                 WAIT_SERVER };
GateState gateState[JML_READER] = { IDLE, IDLE };

/* ================= STRUCT ================= */
struct CardRecord {
  String uid;
  String plate;
  bool inside;
};

struct AccessLog {
  String timestamp;
  String uid;
  String plate;
  String gate;  // "MASUK" / "KELUAR"
};


/* ================= WIFI ICON ================= */
const uint8_t wifi_icon[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x03, 0xfc, 0x00, 0x1f, 0xff, 0x80,
  0x3f, 0x9f, 0xc0, 0xf8, 0x01, 0xf0, 0xe1,
  0xf8, 0x70, 0xc7, 0xfe, 0x30, 0x1f, 0xff,
  0x80, 0x1c, 0x03, 0x80, 0x18, 0xf1, 0x80,
  0x03, 0xfc, 0x00, 0x03, 0xfc, 0x00, 0x03,
  0x0c, 0x00, 0x00, 0x60, 0x00, 0x00, 0x60,
  0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00
};

const uint8_t wifi_x_icon[] PROGMEM = {
  0b10000001, 0b00000001,
  0b01000010, 0b00000010,
  0b00100100, 0b00000100,
  0b00011000, 0b00001000,
  0b00011000, 0b00001000,
  0b00100100, 0b00000100,
  0b01000010, 0b00000010,
  0b10000001, 0b00000001
};

unsigned long wifiBlinkTimer = 0;
bool wifiBlinkState = false;

/* ================= RFID ================= */
MFRC522 mfrc522[JML_READER] = {
  MFRC522(PIN_SDA1, PIN_RST),
  MFRC522(PIN_SDA2, PIN_RST)
};

/* ================= OLED ================= */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void tcaSelect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

/* ================= I/O ================= */
byte servoPin[JML_READER] = { 25, 26 };
byte pirPin[JML_READER] = { 33, 32 };

bool buzzerActive = false;
unsigned long buzzerStart = 0;
unsigned long buzzerDuration = 0;

int activeLedPin = -1;

Servo servo[JML_READER];
bool gateOpen[JML_READER] = { false, false };

int servoOpenAngle[JML_READER] = { 110, 180 };
int servoCloseAngle[JML_READER] = { 173, 112 };

unsigned long gateOpenTime[JML_READER] = { 0, 0 };
unsigned long pirDetectTime[JML_READER] = { 0, 0 };

bool oledInverted[JML_READER] = { false, false };
unsigned long invertStart[JML_READER];
bool eventActive[JML_READER] = { false, false };
unsigned long eventStart[JML_READER];

unsigned long waitServerStart[JML_READER] = { 0, 0 };


String lastUID[JML_READER];

/* ================= STATUS OLED ================= */
String statusLine1[JML_READER] = { "Ready", "Ready" };
String statusLine2[JML_READER] = { "", "" };

/* ================= DATABASE ================= */
std::vector<CardRecord> cardDB;
std::vector<AccessLog> accessLogs;

/* ================= FORWARD DECLARATION ================= */
void sendAccessLogToServer(const AccessLog& log);
String getDateTimeString();
bool checkUIDAlreadyInside(String uid);
int countInsideCards();
void updateCardStatus(String uid, String plate, bool inside);

/* ================= FUNGSI ================= */
bool checkUIDAlreadyInside(String uid) {
  for (auto& c : cardDB) {
    if (c.uid == uid) return c.inside;
  }
  return false;
}

int countInsideCards() {
  int count = 0;
  for (auto& c : cardDB) {
    if (c.inside) count++;
  }
  return count;
}

void updateCardStatus(String uid, String plate, bool inside) {
  for (auto& c : cardDB) {
    if (c.uid == uid) {
      c.inside = inside;
      c.plate = plate;
      return;
    }
  }
  cardDB.push_back({ uid, plate, inside });
}

void addAccessLog(String uid, String plate, GateType gate) {
  AccessLog log;
  log.timestamp = getDateTimeString();
  log.uid = uid;
  log.plate = plate;
  log.gate = (gate == GATE_MASUK) ? "masuk" : "keluar";

  accessLogs.push_back(log);
  if (accessLogs.size() > 200) accessLogs.erase(accessLogs.begin());

  Serial.println("LOG:");
  Serial.println(log.timestamp + " | " + log.uid + " | " + log.plate + " | " + log.gate);

  sendAccessLogToServer(log);
}

void sendAccessLogToServer(const AccessLog& log) {
  DynamicJsonDocument doc(8192);
  doc["timestamp"] = log.timestamp;
  doc["uid"] = log.uid;
  doc["plate"] = log.plate;
  doc["gate"] = log.gate;

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt.publish("gerbang/log/access", buffer);
}

/* ================= WAKTU ================= */
String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--:--";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--/--/----";
  char buf[11];
  strftime(buf, sizeof(buf), "%d/%m/%Y", &timeinfo);
  return String(buf);
}

String getDateTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--/--/---- --:--:--";
  char buf[20];
  strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

void syncActiveVehiclesFromServer() {
  mqtt.publish("gerbang/state/request", "active");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  if (String(topic) == "gerbang/state/active") {

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) return;
    cardDB.clear();  // 🔥 rebuild state

    for (JsonObject obj : doc.as<JsonArray>()) {
      String uid = obj["uid"];
      String plate = obj["plate"];
      updateCardStatus(uid, plate, true);
    }
    Serial.println("SYNC ACTIVE VEHICLES DONE");
    return;
  }

  if (String(topic) != "gerbang/server/result") return;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.println("JSON parse failed");
    return;
  }

  String uid = doc["uid"] | "";
  String gate = doc["gate"] | "";
  gate.toLowerCase();
  String plate = doc["plate"] | "";

  int r = -1;
  if (gate == "masuk") r = 0;
  else if (gate == "keluar") r = 1;
  else return;

  // ⛔ Validasi dasar
  if (gateState[r] != WAIT_SERVER) return;
  if (uid != lastUID[r]) return;

  if (plate.length() < 3 || plate == "GAGAL DETEKSI" || plate == "PLAT TIDAK TERDETEKSI" || plate == "null") {
    lastUID[r] = "";
    gateState[r] = IDLE;

    statusLine1[r] = "OCR GAGAL";
    statusLine2[r] = "COBA LAGI";

    oledInverted[r] = true;
    invertStart[r] = millis();
    tcaSelect(r);
    display.invertDisplay(true);

    triggerFeedback(LED_DENY_PIN, BUZZER_DENY_MS);
    return;
  }

  /* ================= KEPUTUSAN ADA DI ESP32 ================= */
  bool allowAccess = false;

  if (gateType[r] == GATE_MASUK) {
    if (checkUIDAlreadyInside(uid)) {
      allowAccess = false;
      statusLine1[r] = "DITOLAK";
      statusLine2[r] = "SUDAH DI DALAM";
    }
    // 2. JIKA BELUM, CEK KAPASITAS
    else if (countInsideCards() < MAX_CARD) {
      allowAccess = true;
    } else {
      allowAccess = false;
      statusLine1[r] = "PENUH";
      statusLine2[r] = "KAPASITAS MAX";
    }
  } else if (gateType[r] == GATE_KELUAR) {

    // --- LOGIKA BARU: VALIDASI GANDA ---
    bool isInside = false;
    String registeredPlate = "";

    // 1. CARI DATA DI DATABASE LOKAL
    for (auto& c : cardDB) {
      if (c.uid == uid && c.inside) {
        isInside = true;
        registeredPlate = c.plate;
        break;
      }
    }

    if (!isInside) {
      // KETENTUAN 1: TIDAK BISA KELUAR JIKA BELUM MASUK
      allowAccess = false;
      statusLine1[r] = "DITOLAK";
      statusLine2[r] = "BELUM MASUK";
    } else {
      bool plateMatch = false;
      if (plate == registeredPlate) {
        plateMatch = true;
      } else if (plate.indexOf(registeredPlate) >= 0 || registeredPlate.indexOf(plate) >= 0) {
        plateMatch = true;
      }

      if (plateMatch) {
        allowAccess = true;
      } else {
        allowAccess = false;
        statusLine1[r] = "PLAT BEDA";
        // Tampilkan plat yang seharusnya (agar user tau)
        statusLine2[r] = registeredPlate;
      }
    }
  }

  if (allowAccess) {
    servo[r].write(servoOpenAngle[r]);
    gateOpen[r] = true;
    gateOpenTime[r] = millis();
    oledInverted[r] = true;
    invertStart[r] = millis();
    tcaSelect(r);
    display.invertDisplay(true);


    if (gateType[r] == GATE_MASUK) {
      updateCardStatus(uid, plate, true);  // masuk
    } else {
      updateCardStatus(uid, plate, false);  // keluar
    }
    addAccessLog(uid, plate, gateType[r]);
    statusLine1[r] = "AKSES DIBUKA";
    statusLine2[r] = plate;
    gateState[r] = IDLE;
    triggerFeedback(LED_OK_PIN, BUZZER_OK_MS);
  } else {
    oledInverted[r] = true;
    invertStart[r] = millis();
    tcaSelect(r);
    display.invertDisplay(true);

    gateState[r] = IDLE;
    statusLine1[r] = "AKSES DITOLAK";
    statusLine2[r] = plate;

    triggerFeedback(LED_DENY_PIN, BUZZER_DENY_MS);
  }

  eventActive[r] = true;
  eventStart[r] = millis();
}

void setupMQTT() {
  mqtt.setServer(mqttServer, mqttPort);

  while (!mqtt.connected()) {
    Serial.print("Connecting MQTT...");
    String clientId = "ESP32_GATE_" + String(random(0xffff), HEX);

    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
    }
  }

  mqtt.setCallback(mqttCallback);
  mqtt.subscribe("gerbang/server/result");
  mqtt.subscribe("gerbang/state/active");
}

void triggerESP32CAM(String uid, GateType gate) {
  DynamicJsonDocument doc(8192);
  doc["uid"] = uid;
  doc["gate"] = (gate == GATE_MASUK) ? "masuk" : "keluar";
  doc["timestamp"] = getDateTimeString();

  char buffer[512];
  serializeJson(doc, buffer);

  mqtt.publish("gerbang/cam/capture", buffer);
}

void triggerFeedback(int ledPin, unsigned long buzzerMs) {
  digitalWrite(ledPin, LOW);
  activeLedPin = ledPin;

  digitalWrite(BUZZER_PIN, HIGH);
  buzzerActive = true;
  buzzerStart = millis();
  buzzerDuration = buzzerMs;
}

void drawWiFiIcon() {
  wl_status_t st = WiFi.status();

  if (st == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int interval = 0;

    if (rssi > -60) interval = 0;         // kuat
    else if (rssi > -75) interval = 800;  // sedang
    else interval = 300;                  // lemah

    if (interval > 0) {
      if (millis() - wifiBlinkTimer > interval) {
        wifiBlinkTimer = millis();
        wifiBlinkState = !wifiBlinkState;
      }
      if (!wifiBlinkState) return;
    }

    display.drawBitmap(108, 46, wifi_icon, 20, 20, SSD1306_WHITE);
  } else if (st == WL_IDLE_STATUS || st == WL_DISCONNECTED) {
    if (millis() - wifiBlinkTimer > 400) {
      wifiBlinkTimer = millis();
      wifiBlinkState = !wifiBlinkState;
    }
    if (wifiBlinkState)
      display.drawBitmap(108, 46, wifi_icon, 20, 20, SSD1306_WHITE);
  } else {
    display.drawBitmap(108, 46, wifi_x_icon, 20, 20, SSD1306_WHITE);
  }
}

/* ================= OLED DRAW ================= */
void drawOLED(int i) {
  tcaSelect(i);
  display.clearDisplay();

  display.setCursor(1, 1);
  display.print(getDateString());
  display.setCursor(80, 1);
  display.print(getTimeString());

  drawWiFiIcon();

  display.drawLine(1, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(1, 14);
  display.println(gateType[i] == GATE_MASUK ? "GERBANG MASUK" : "GERBANG KELUAR");
  display.println(statusLine1[i]);
  display.println(statusLine2[i]);
  display.display();
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  Wire.begin();
  Wire.setClock(100000);
  pinMode(LED_DENY_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_DENY_PIN, HIGH);
  digitalWrite(LED_OK_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  for (int i = 0; i < JML_READER; i++) {
    mfrc522[i].PCD_Init();
    servo[i].attach(servoPin[i]);
    servo[i].write(servoCloseAngle[i]);
    pinMode(pirPin[i], INPUT);

    Serial.print("Reader ");
    Serial.print(i);
    Serial.print(" : ");
    mfrc522[i].PCD_DumpVersionToSerial();

    tcaSelect(i);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.setRotation(2);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.invertDisplay(false);

    drawOLED(i);
    int inside = countInsideCards();
    int sisa = MAX_CARD - inside;

    if (gateType[i] == GATE_MASUK) {
      statusLine2[i] = "SISA SLOT: " + String(sisa);
    } else {
      statusLine2[i] = "KENDARAAN: " + String(inside);
    }
  }
  mqtt.setKeepAlive(60);
  setupMQTT();
  syncActiveVehiclesFromServer();
}

/* ================= LOOP ================= */
void loop() {

  static unsigned long lastMQTTTry = 0;
  if (!mqtt.connected() && millis() - lastMQTTTry > 3000) {
    lastMQTTTry = millis();
    setupMQTT();
  }
  mqtt.loop();

  /* ===== OLED REALTIME ===== */
  static unsigned long lastOLED = 0;
  if (millis() - lastOLED >= 1000) {
    lastOLED = millis();
    for (int i = 0; i < JML_READER; i++) {
      if (!eventActive[i] && gateState[i] == IDLE) {
        int inside = countInsideCards();
        int sisa = MAX_CARD - inside;

        if (gateType[i] == GATE_MASUK) {
          statusLine2[i] = "SISA SLOT: " + String(sisa);
        } else {
          statusLine2[i] = "KENDARAAN: " + String(inside);
        }
      }
      drawOLED(i);
    }
  }

  for (int i = 0; i < JML_READER; i++) {
    if (gateState[i] == WAIT_SERVER && millis() - waitServerStart[i] > SERVER_TIMEOUT) {

      // ⛔ TIMEOUT
      gateState[i] = IDLE;
      gateOpen[i] = false;

      statusLine1[i] = "SERVER TIMEOUT";
      statusLine2[i] = "";

      triggerFeedback(LED_DENY_PIN, BUZZER_DENY_MS);

      // Pastikan OLED normal
      oledInverted[i] = false;
      tcaSelect(i);
      display.invertDisplay(false);
    }
  }


  for (int i = 0; i < JML_READER; i++) {
    if (eventActive[i] && millis() - eventStart[i] > EVENT_DISPLAY_DURATION) {
      statusLine1[i] = "SIAP";
      int inside = countInsideCards();
      int sisa = MAX_CARD - inside;

      if (gateType[i] == GATE_MASUK) {
        statusLine2[i] = "SISA SLOT: " + String(sisa);
      } else {
        statusLine2[i] = "KENDARAAN: " + String(inside);
      }
      eventActive[i] = false;

      // Pastikan OLED kembali normal
      oledInverted[i] = false;
      tcaSelect(i);
      display.invertDisplay(false);
    }
  }

  /* ===== RFID ===== */
  for (int reader = 0; reader < JML_READER; reader++) {
    if (gateState[reader] == IDLE && !gateOpen[reader] && mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial()) {

      String uid = "";
      for (byte i = 0; i < mfrc522[reader].uid.size; i++) {
        if (i > 0) uid += ":";
        if (mfrc522[reader].uid.uidByte[i] < 0x10) uid += "0";
        uid += String(mfrc522[reader].uid.uidByte[i], HEX);
      }
      uid.toLowerCase();
      lastUID[reader] = uid;

      gateState[reader] = WAIT_SERVER;
      waitServerStart[reader] = millis();

      statusLine1[reader] = "MENUNGGU SERVER";
      statusLine2[reader] = getDateTimeString() + "\n" + uid;

      eventActive[reader] = true;
      eventStart[reader] = millis();

      // Kirim ke ESP32-CAM / server
      triggerESP32CAM(uid, gateType[reader]);


      mfrc522[reader].PICC_HaltA();
      mfrc522[reader].PCD_StopCrypto1();
    }
    if (gateOpen[reader] && millis() - gateOpenTime[reader] > PIR_IGNORE_TIME) {
      if (digitalRead(pirPin[reader]) == HIGH) {
        pirDetectTime[reader] = millis();
      } else {
        if (pirDetectTime[reader] != 0 && millis() - pirDetectTime[reader] > PIR_CONFIRM_TIME) {
          servo[reader].write(servoCloseAngle[reader]);
          gateOpen[reader] = false;
          gateState[reader] = IDLE;
          pirDetectTime[reader] = 0;

          statusLine1[reader] = "PINTU DITUTUP";
          statusLine2[reader] = "";

          oledInverted[reader] = false;
          tcaSelect(reader);
          display.invertDisplay(false);
        }
      }
    }
    if (oledInverted[reader] && millis() - invertStart[reader] > INVERT_DURATION) {
      oledInverted[reader] = false;
      tcaSelect(reader);
      display.invertDisplay(false);
    }
  }

  if (buzzerActive && millis() - buzzerStart > buzzerDuration) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;

    if (activeLedPin != -1) {
      digitalWrite(activeLedPin, HIGH);
      activeLedPin = -1;
    }
  }
}