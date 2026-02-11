# 🚘 Flask–ESP32 License Plate Recognition System
![Python](https://img.shields.io/badge/Python-3.8%20%7C%203.9%20%7C%203.10-blue)
![Flask](https://img.shields.io/badge/Framework-Flask-green)
![YOLOv5](https://img.shields.io/badge/AI-YOLOv5-yellow)
![MQTT](https://img.shields.io/badge/IoT-MQTT-orange)

---
## 🛠️ Prerequisites
Sebelum menjalankan program, pastikan komputer kamu memenuhi syarat berikut:
1.  **Python**: Versi **3.8 - 3.10**.
    * *⚠️ Catatan:* Hindari Python 3.11+ karena sering terjadi isu kompatibilitas dengan library Machine Learning.
2.  **MQTT Broker**: (Contoh: Mosquitto).
    * Program ini menggunakan **Port 1884**. Pastikan broker berjalan pada port tersebut (bukan default 1883), atau sesuaikan konfigurasi di `server.py`.

---
## 📦 Installation
### 1. Clone & Masuk Direktori
Buka terminal dan jalankan perintah berikut untuk mengunduh proyek ini:
```bash
git clone https://github.com/ridhmm/flask-esp32.git
cd flask-esp32
```
### 2. Install Dependency
Pastikan Python sudah terinstall, lalu jalankan:
```bash
pip install -r requirements.txt
```
Catatan: Jika terjadi error pada library torch atau fast-plate-ocr, disarankan untuk menginstallnya secara manual sesuai dengan OS dan Hardware (CPU/GPU) perangkat Anda.

---
## 🧪 Testing
1. Buka browser dan akses: http://localhost:5001
2. Kamu akan melihat dashboard monitoring.
3. Gunakan form upload di web untuk menguji deteksi gambar secara manual.

---
## 📂 Struktur Folder (Wajib)

Agar sistem berjalan tanpa error `File Not Found` atau `TemplateNotFound`, susunan folder proyek **harus** seperti ini:

```text
📁 folder_proyek_ta/
├── 📄 server.py              # File utama server Flask (Backend)
├── 📄 best.pt                # Model YOLOv5 yang sudah dilatih (WAJIB ADA)
├── 📄 requirements.txt       # Daftar pustaka (opsional)
├── 📁 templates/             # Folder template HTML (Wajib untuk Flask)
│   └── 📄 index.html         # File tampilan Dashboard Web
├── 📁 static/                # Folder aset (Dibuat otomatis oleh sistem)
│   ├── 📁 uploads            # Menyimpan gambar full dari ESP32
│   └── 📁 plates             # Menyimpan hasil crop plat nomor
└── 📁 yolov5/                # (Opsional) Clone repo ultralytics jika load 'local' error
```

