from flask import Flask, request, render_template
import cv2
import os
import torch
import json
import re
import numpy as np
from fast_plate_ocr import LicensePlateRecognizer
import paho.mqtt.client as mqtt
from datetime import datetime
from collections import Counter
import uuid
import time
import threading

# --- KONFIGURASI ---
DAFTAR_KODE_WILAYAH = [
    'A', 'B', 'D', 'E', 'F', 'G', 'H', 'K', 'L', 'M', 'N', 'P', 'R', 'S', 'T', 'W', 'Z',
    'AA', 'AB', 'AD', 'AE', 'AG', 'BA', 'BB', 'BD', 'BE', 'BG', 'BH', 'BK', 'BL', 'BM', 
    'BP', 'DA', 'DB', 'DD', 'DE', 'DG', 'DH', 'DK', 'DL', 'DM', 'DN', 'DP', 'DR', 
    'DS', 'DT', 'DW', 'EA', 'EB', 'ED', 'EF', 'EG', 'KH', 'KT', 'KU', 'PA', 'PB'
]

app = Flask(__name__)
app.config['UPLOAD_FOLDER'] = 'static/uploads'
app.config['PLATE_FOLDER'] = 'static/plates'
LOG_FILE = 'log.txt'
STATE_FILE = 'parking_state.json'

os.makedirs(app.config['UPLOAD_FOLDER'], exist_ok=True)
os.makedirs(app.config['PLATE_FOLDER'], exist_ok=True)

# --- LOAD MODELS ---
print("[INFO] Memuat model AI...")
yolo = torch.hub.load('ultralytics/yolov5', 'custom', path='best.pt', source='local')
yolo.conf = 0.4
ocr_model = LicensePlateRecognizer("global-plates-mobile-vit-v2-model", device="cpu")

# --- MANAJEMEN STATE ---
def load_state():
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE, 'r') as f:
                return json.load(f)
        except: return {}
    return {}

def save_state():
    try:
        with open(STATE_FILE, 'w') as f:
            json.dump(active_vehicles, f, indent=4)
    except Exception as e:
        print(f"[ERROR] Save state: {e}")

active_vehicles = load_state()

# --- MQTT SETUP ---
random_id = f"server_{uuid.uuid4().hex[:6]}"
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=random_id)

def on_mqtt_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        if msg.topic == "gerbang/state/request" and payload.lower() == "active":
            arr = []
            for u, data in active_vehicles.items():
                plate_val = data['plate'] if isinstance(data, dict) else data
                arr.append({"uid": u, "plate": plate_val})
            
            mqtt_client.publish("gerbang/state/active", json.dumps(arr))
    except Exception as e:
        print(f"MQTT Error: {e}")

mqtt_client.on_message = on_mqtt_message
mqtt_client.connect("127.0.0.1", 1884, 60) 
mqtt_client.subscribe("gerbang/state/request")

# --- UTILITIES ---
def log_result(timestamp, gate, plate, uid, image_url):
    with open(LOG_FILE, 'a') as f:
        f.write(f"{timestamp}|{gate}|{uid}|{plate}|{image_url}\n")

def load_logs():
    if not os.path.exists(LOG_FILE): return []
    with open(LOG_FILE, 'r') as f:
        return [line.strip().split('|') for line in f.readlines() if line.strip()]

def detect_plate_and_read(img):
    results = yolo(img)
    detections = results.xyxy[0]
    if len(detections) == 0: return None, None

    detections = detections.cpu().numpy()
    detections = sorted(detections, key=lambda x: x[4], reverse=True)
    x1, y1, x2, y2, conf, cls = detections[0].astype(int)

    pad = 5
    plate_img = img[max(0, y1-pad):min(img.shape[0], y2+pad), 
                    max(0, x1-pad):min(img.shape[1], x2+pad)]
                    
    plate_img = cv2.convertScaleAbs(plate_img, alpha=1.3, beta=10)
    gray = cv2.cvtColor(plate_img, cv2.COLOR_BGR2GRAY)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    gray = clahe.apply(gray)
    gray = cv2.bilateralFilter(gray, 11, 17, 17)
    
    try:
        res = ocr_model.run([gray]) 
        raw_text = str(res[0]).upper() if res else ""
    except: raw_text = ""

    joined = re.sub(r'[^A-Z0-9]', '', raw_text)
    parts = re.findall(r"([A-Z]+)|([0-9]+)", joined)
    combined = [p[0] or p[1] for p in parts]
    
    region, numbers, suffix = "", "", ""
    for p in combined:
        if p.isalpha() and not region: region = p
        elif p.isdigit() and not numbers: numbers = p
        elif not suffix: suffix = p
    
    if region:
        region = region[:2]
        if region not in DAFTAR_KODE_WILAYAH:
             vmap = {'0':'D','8':'B','5':'S','1':'I','6':'G'}
             region = "".join([vmap.get(c,c) for c in region])
             if region not in DAFTAR_KODE_WILAYAH and len(region)>1: region = region[0]
    
    if numbers and len(numbers)>4:
        if not suffix: suffix = numbers[4:]
        numbers = numbers[:4]
    
    if suffix: suffix = re.sub(r'[^A-Z]', '', suffix)[:3]

    plate_text = f"{region} {numbers} {suffix}".strip()
    return (plate_text if plate_text else "TIDAK TERDETEKSI"), plate_img

# --- WORKER (BAGIAN INI YANG SUDAH DIPERBAIKI) ---
def background_ai_worker(file_bytes, uid, gate, timestamp, filename):
    try:
        nparr = np.frombuffer(file_bytes, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        plate_text, plate_img = detect_plate_and_read(img)
        
        # Penanganan jika deteksi gagal
        if plate_text is None: plate_text = "GAGAL DETEKSI"
        
        plate_url = f"/static/uploads/{filename}"
        if plate_img is not None:
            cv2.imwrite(os.path.join(app.config['PLATE_FOLDER'], filename), plate_img)
            plate_url = f"/static/plates/{filename}"

        # --- VALIDASI HASIL DETEKSI ---
        invalid_keywords = ["TIDAK TERDETEKSI", "GAGAL", "null", "NONE"]
        is_valid_plate = True
        
        # Cek jika plat mengandung kata error atau terlalu pendek
        if len(plate_text) < 3 or any(k in plate_text.upper() for k in invalid_keywords):
            is_valid_plate = False

        # --- UPDATE DATABASE (HANYA JIKA VALID) ---
        if is_valid_plate:
            changed = False
            if gate == "masuk": 
                active_vehicles[uid] = {
                    "plate": plate_text,
                    "timestamp": timestamp
                }
                changed = True
            elif gate == "keluar": 
                if uid in active_vehicles: 
                    active_vehicles.pop(uid, None)
                    changed = True
            
            if changed: save_state()
            print(f"[AI SUKSES] Plat Valid: {plate_text} -> Disimpan.")
        else:
            print(f"[AI SKIP] Plat Tidak Valid: {plate_text} -> TIDAK DISIMPAN ke Database.")
        
        # --- KIRIM LOG & MQTT (SELALU KIRIM UNTUK RESPON ESP32) ---
        log_result(timestamp, gate, plate_text, uid, plate_url)
        
        payload = {"uid": uid, "gate": gate, "plate": plate_text, "timestamp": timestamp, "image_url": plate_url}
        mqtt_client.publish("gerbang/log/access", json.dumps(payload))
        mqtt_client.publish("gerbang/server/result", json.dumps({"uid":uid, "gate":gate, "plate":plate_text}))
        
        print(f"[AI SELESAI] {plate_text}")

    except Exception as e:
        print(f"[AI ERROR] {e}")

# --- ROUTES ---
@app.route('/', methods=['GET','POST'])
def index():
    if request.method == 'POST':
        file = request.files.get('image')
        if file:
            f_bytes = file.read()
            uid = request.form.get('uid', 'manual').lower()
            gate = request.form.get('gate', 'manual').lower()
            ts = datetime.now().strftime("%d/%m/%Y %H:%M:%S")
            fname = f"manual_{int(time.time())}.jpg"
            with open(os.path.join(app.config['UPLOAD_FOLDER'], fname), 'wb') as f: f.write(f_bytes)
            
            threading.Thread(target=background_ai_worker, args=(f_bytes, uid, gate, ts, fname)).start()
            return {"status": "success"}, 200

    raw = load_logs()
    logs_rev = list(reversed(raw))
    
    last_plate = logs_rev[0][3] if logs_rev else "---"
    last_img = logs_rev[0][4] if logs_rev else ""

    dates = [l[0].split(' ')[0] for l in raw]
    counts = Counter(dates)
    g_labels = list(counts.keys())[-7:]
    g_values = [counts[d] for d in g_labels]

    active_list = []
    for u, data in active_vehicles.items():
        if isinstance(data, dict):
            active_list.append({"uid": u, "plate": data['plate'], "timestamp": data['timestamp']})
        else:
            active_list.append({"uid": u, "plate": data, "timestamp": "-"})

    return render_template("index.html", 
                           logs=logs_rev, 
                           total_masuk=len(raw),
                           masuk_hari_ini=sum(1 for l in raw if l[0].startswith(datetime.now().strftime("%d/%m/%Y"))),
                           graph_labels=g_labels, graph_values=g_values,
                           last_plate_server=last_plate,
                           last_img_server=last_img,
                           active_vehicles_server=active_list)

@app.route('/upload', methods=['POST'])
def upload_esp():
    data = request.data
    if not data: return {"status":"error"}, 400
    
    uid = request.headers.get("X-UID", "unknown")
    gate = request.headers.get("X-GATE", "esp32")
    ts = datetime.now().strftime("%d/%m/%Y %H:%M:%S")
    fname = f"{datetime.now().strftime('%Y%m%d%H%M%S')}_{uid}.jpg"
    
    with open(os.path.join(app.config['UPLOAD_FOLDER'], fname), 'wb') as f: f.write(data)
    threading.Thread(target=background_ai_worker, args=(data, uid, gate, ts, fname)).start()
    return {"status":"processing"}, 200

@app.route('/reset', methods=['POST'])
def reset_database():
    global active_vehicles
    try:
        active_vehicles = {}
        save_state()
        mqtt_client.publish("gerbang/state/active", json.dumps([]))
        print("[INFO] Database kendaraan berhasil di-reset!")
        return {"status": "success", "message": "Data parkir dikosongkan"}, 200
    except Exception as e:
        print(f"[ERROR] Gagal reset: {e}")
        return {"status": "error"}, 500

if __name__ == '__main__':
    mqtt_client.loop_start()
    app.run(host='0.0.0.0', port=5001, debug=False)