# 🚗 Sistem Cerdas Deteksi Plat Nomor (ALPR Server)

> **Tugas Akhir / Skripsi**
> Server backend berbasis Python Flask untuk mendeteksi dan mengenali plat nomor kendaraan secara otomatis menggunakan YOLOv5 dan OCR, terintegrasi dengan sistem IoT via MQTT.

![Python](https://img.shields.io/badge/Python-3.8%20%7C%203.9%20%7C%203.10-blue)
![Flask](https://img.shields.io/badge/Framework-Flask-green)
![YOLOv5](https://img.shields.io/badge/AI-YOLOv5-yellow)
![MQTT](https://img.shields.io/badge/IoT-MQTT-orange)

## 📋 Fitur Utama
* **Deteksi Plat Nomor:** Menggunakan model YOLOv5 custom (`best.pt`).
* **OCR (Optical Character Recognition):** Membaca teks plat menggunakan `fast-plate-ocr`.
* **Integrasi IoT:** Komunikasi dua arah dengan ESP32/Mikrokontroler via protokol MQTT.
* **Web Dashboard:** Antarmuka sederhana untuk memantau log akses masuk/keluar.
* **API Endpoint:** Menerima upload gambar dari perangkat IoT.

---

## 🛠️ Persiapan Lingkungan (Prerequisites)

Sebelum menjalankan program, pastikan komputer kamu memenuhi syarat berikut:

1.  **Python**: Versi **3.8 - 3.10**.
    * *⚠️ Catatan:* Hindari Python 3.11+ karena sering terjadi isu kompatibilitas dengan library Machine Learning.
2.  **MQTT Broker**: (Contoh: Mosquitto).
    * Program ini menggunakan **Port 1884**. Pastikan broker berjalan pada port tersebut (bukan default 1883), atau sesuaikan konfigurasi di `server.py`.

---

## 📂 Struktur Folder

Agar program berjalan tanpa error `File Not Found`, susunan folder **wajib** seperti berikut:

```text
📁 folder_proyek_ta/
├── 📄 server.py              # File utama server Flask
├── 📄 best.pt                # Model YOLOv5 yang sudah dilatih (WAJIB ADA)
├── 📄 requirements.txt       # Daftar library (opsional)
├── 📁 templates/             # Folder template HTML (WAJIB ADA untuk Flask)
│   └── 📄 index.html         # File tampilan web dashboard
├── 📁 static/                # Folder aset statis (Dibuat otomatis jika belum ada)
│   ├── 📁 uploads            # Menyimpan gambar asli dari ESP32/Web
│   └── 📁 plates             # Menyimpan hasil crop plat nomor
└── 📁 yolov5/                # (Opsional) Clone repo ultralytics/yolov5 jika load local bermasalah
