import paho.mqtt.client as mqtt
import json
import time
import random 
from datetime import datetime

# Konfigurasi - Harus sama dengan server.py
MQTT_BROKER = "192.168.1.111"
MQTT_PORT = 1884
TOPIC_CAPTURE = "gerbang/cam/capture"
TOPIC_RESULT = "gerbang/server/result"

# Fungsi Utama Simulasi
def simulasi_tap(gate, use_random=True):
    # Jika random, buat UID baru. Jika tidak, gunakan UID dummy tetap.
    if use_random:
        uid = ":".join(["%02x" % random.randint(0, 255) for _ in range(4)])
    else:
        uid = "aa:bb:cc:dd"
    
    print(f"\n[SIMULASI] Mengetuk kartu!")
    print(f"UID: {uid} | Gerbang: {gate}")
    
    payload = {
        "uid": uid,
        "gate": gate,
        "timestamp": datetime.now().strftime("%d/%m/%Y %H:%M:%S")
    }
    
    # Kirim perintah capture
    client.publish(TOPIC_CAPTURE, json.dumps(payload))
    print(f"🚀 Perintah Capture terkirim ke {TOPIC_CAPTURE}")

# Callback saat terhubung
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print(f"✅ Terhubung ke Broker!")
        client.subscribe(TOPIC_RESULT) # Berlangganan hasil dari server
    else:
        print(f"❌ Koneksi gagal, kode: {reason_code}")

# Callback saat menerima pesan
def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        print("\n" + "="*40)
        print("📥 RESPONS DARI SERVER TERDETEKSI!")
        print(f"UID   : {data.get('uid')}")
        print(f"Gate  : {data.get('gate')}")
        print(f"Plate : {data.get('plate')}")
        print("="*40 + "\n")
    except Exception as e:
        print(f"Gagal memproses pesan: {e}")

# Inisialisasi MQTT Client
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_start()

if __name__ == "__main__":
    try:
        while True:
            print("\n--- MENU SIMULASI RFID (RANDOM UID) ---")
            print("1. Tap Kartu Masuk")
            print("2. Tap Kartu Keluar")
            print("q. Keluar")
            pilihan = input("Pilih menu: ")
            
            if pilihan == '1':
                simulasi_tap("masuk")
            elif pilihan == '2':
                # Catatan: Jika UID random, server mungkin menolak keluar 
                # karena UID tersebut tidak ada di daftar active_vehicles
                simulasi_tap("keluar")
            elif pilihan == 'q':
                break
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()
