import paho.mqtt.client as mqtt
import json
from datetime import datetime

# Konfigurasi Broker
MQTT_BROKER = "192.168.1.111"
MQTT_PORT = 1884
TOPICS = [
    "gerbang/cam/capture",   # Trigger dari ESP32 Utama ke Kamera [cite: 78]
    "gerbang/server/result", # Hasil OCR dari Server ke Gerbang
    "gerbang/log/access",    # Data log untuk Dashboard
    "gerbang/state/request"  # Permintaan sinkronisasi data [cite: 54]
]

def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] ✅ MONITOR AKTIF")
        print("-" * 50)
        for topic in TOPICS:
            client.subscribe(topic)
            print(f"Subscribed ke: {topic}")
    else:
        print(f"❌ Gagal terhubung, kode: {reason_code}")

def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode())
        time_now = datetime.now().strftime('%H:%M:%S')
        
        print(f"\n[{time_now}] 📬 TOPIK: {topic}")
        # Format tampilan berdasarkan konten agar mudah dibaca
        if "capture" in topic:
            print(f"   📸 TRIGGER: UID {payload.get('uid')} -> Gate {payload.get('gate')}") [cite: 79]
        elif "result" in topic:
            print(f"   🤖 OCR RESULT: {payload.get('plate')} (UID: {payload.get('uid')})")
        elif "access" in topic:
            print(f"   📝 ACCESS LOG: {payload.get('plate')} masuk via {payload.get('gate')}")
        else:
            print(f"   📦 PAYLOAD: {payload}")
    except Exception as e:
        print(f"   ⚠️ Raw Message: {msg.payload.decode()}")

# Inisialisasi Client
monitor = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
monitor.on_connect = on_connect
monitor.on_message = on_message

print("Menghubungkan ke broker...")
monitor.connect(MQTT_BROKER, MQTT_PORT, 60)

try:
    monitor.loop_forever() # Berjalan terus untuk memantau
except KeyboardInterrupt:
    print("\nMonitor dimatikan.")
