# SenseAhead Smart Shirt for Occupational Health
# VSM-3 Vital Sign Monitoring System

This repository contains all design, firmware, hardware, and web application files required to run the VSM‑3 wearable system, which collects **ECG, SpO2, IMU, and temperature data** and displays them on a local web dashboard.

## System Diagram

![System diagram](images/ourapproach.jpg)


---

## 📦 Project Folder Structure

```
V3/
│
├── Bill_of_Materials.xlsx
│
├── CAD Files/                 → 3D printable enclosure components
│   ├── Arduino_ICM_bottom_v5.step
│   ├── Arduino_ICM_top_v5.step
│   ├── MAX_bottom_v2.step
│   ├── MAX_top_v4.step
│   ├── TMP_bottom_v2.step
│   ├── TMP_top_v2.step
│
├── web_arduino_2/             → Arduino firmware for vital-sign sensors
│   └── web_arduino_2.ino
│
└── webpage/                   → Local-only Web Dashboard (no Firebase needed)
    ├── export/
    ├── functions/
    ├── images/
    ├── node_modules/
    ├── public/
    ├── .firebaserc
    ├── .gitignore
    ├── firebase.json
    ├── firestore.indexes.json
    ├── firestore.rules
    ├── package.json
    └── package-lock.json
```

---

# How to Run the VSM‑3 System (USER GUIDE)

This guide explains how **any user** can run the system locally on their laptop and collect vital‑sign data from the wearable shirt.


---

# 1️⃣ Hardware Setup (Wearable Shirt)

### Required Components
- Arduino UNO R4 WiFi  
- MAX30101 (SpO₂ + HR)  
- AD8232 (ECG)  
- TMP117 (Temperature)  
- ICM‑20948 (9‑axis IMU)  
- Jumper wires  
- Enclosure 3D printed parts (from CAD folder)  
- Shirt with embedded sensors (as designed by team)

### Assembly Overview
1. Attach all sensors to the Arduino using Qwiic/I2C and analog ECG connection.  
2. Mount sensors inside the enclosure parts (CAD folder).  
3. Position electrodes on the chest and tighten straps.  
4. Power the system via USB cable to the laptop.

---

# 2️⃣ Upload Arduino Code

1. Open **Arduino IDE**.
2. Open:

```
web_arduino_2/web_arduino_2.ino
```

3. Scroll to the section:

```cpp
//=======Wi fi config========
const char* STA_SSID = "Salehin";
const char* STA_PASS = "GGunited";
```

Change these values to match your **hotspot name + password**.

4. Connect Arduino UNO R4 WiFi with USB.

5. Click:

✔ **Verify**  
✔ **Upload**

6. Open **Serial Monitor @ 115200 baud** and confirm you see lines like:

```
ECG=700   Thr=400   motionScore=0.02   motionQuality=GOOD
```

This confirms the shirt is sending sensor data.

---

# 3️⃣ Launch the Web Dashboard (Local Mode)

The dashboard is located at:

```
V3/webpage/public/index.html
```

### To open the dashboard:

1. Open VS Code  
2. Install the extension **Live Server**
3. Right‑click:

```
webpage/public/index.html
```

4. Select **"Open With Live Server"**

Your browser will open:

```
http://127.0.0.1:5500/webpage/public/index.html
```

---

# 4️⃣ Create User Account (Local Authentication)

1. On the dashboard login page, select **“Create account”**  
2. Enter:
   - Name  
   - Email  
   - Password  
3. Log in  
4. Your dashboard loads with:

- ACC  
- GYR  
- TEMP  
- SpO₂  
- HR  
- ECG waveform (real‑time)  

**This works fully offline (local Firestore emulator).**

---

# 5️⃣ Start Collecting Data

With Arduino running AND dashboard open:

✔ ECG plots appear  
✔ Temperature updates  
✔ SpO₂ shows when finger sensor works  
✔ HR from ECG is averaged  
✔ Motion score reflects IMU activity

---

# 6️⃣ Where Is Data Stored?

Even without Firebase, the dashboard stores data in:

```
webpage/export/
```

If Firebase is connected later, the same data is pushed to Firestore.

---

# 🛠 Developer Notes (for the team)

### If deploying Firebase Functions again:
Only the **project owner** can run:

```
firebase deploy --only functions
```

Other teammates need IAM permission  
**“Service Account User”**  
in Google Cloud.

Normal users running data collection **do NOT need Firebase**.

---

# 👥 Authors

- Artemis Badger  
- Luisa Chavez  
- Marc Dobos  
- Sultanus Salehin  
- Irem Yunculer

---

# 📄 License
This project is for educational use under ECE‑522 (NC State University).

