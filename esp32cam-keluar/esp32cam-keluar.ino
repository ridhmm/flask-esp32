#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "WiFi";
const char* password = "!@#!@#!@#";

const char* mqttServer = "192.168.1.111";
const int mqttPort = 1884;
const char* serverUrl = "http://192.168.1.111:5001/upload";

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

WiFiClient espClient;
PubSubClient mqtt(espClient);

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.println("JSON parse error");
    return;
  }

  String uid = doc["uid"];
  String gate = doc["gate"];
  String time = doc["timestamp"];

  if (gate != "keluar") {
    return;
  }
  Serial.println("Triggered: Gerbang Keluar. Capturing...");

  for (int i = 0; i < 2; i++) {
    camera_fb_t* fb_old = esp_camera_fb_get();
    if (fb_old) {
      esp_camera_fb_return(fb_old);
      delay(5);  // Beri jeda singkat agar sensor stabil
    }
  }
  // 2. Ambil gambar yang benar-benar baru
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(30000);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-UID", uid);
  http.addHeader("X-GATE", gate);
  http.addHeader("X-TIME", time);

  int httpResponseCode = http.POST(fb->buf, fb->len);
  http.end();
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Kamera gagal diinisialisasi!");
    while (true)
      ;
  }

  mqtt.setServer(mqttServer, mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);  // Set ke 60 detik agar lebih toleran terhadap gangguan kecil

  String clientId = "ESP32CAM-KELUAR-" + WiFi.macAddress();
  if (mqtt.connect(clientId.c_str())) {
    mqtt.subscribe("gerbang/cam/capture");
    Serial.println("✅ Terhubung dengan ID: " + clientId);
  }
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi Lost. Reconnecting...");
    WiFi.begin(ssid, password);  // Coba hubungkan kembali
    long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }
    return;
  }

  if (!mqtt.connected()) {
    String clientId = "ESP32CAM-KELUAR-" + WiFi.macAddress();
    Serial.print("Attempting MQTT connection...");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
      mqtt.subscribe("gerbang/cam/capture");  // Subscribe ulang setiap kali connect
    }
  }
  mqtt.loop();
}
