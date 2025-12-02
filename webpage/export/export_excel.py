import firebase_admin
from firebase_admin import credentials, firestore
import openpyxl

cred = credentials.Certificate("firebase_key.json")  
firebase_admin.initialize_app(cred)

db = firestore.client()

wb = openpyxl.Workbook()
ws = wb.active
ws.title = "vital_signs"

headers = [
    "docId", "timestamp",
    "tempC", "spo2", "hr", "motion",
    "acc_x", "acc_y", "acc_z",
    "gyr_x", "gyr_y", "gyr_z",
    "ecg"
]
ws.append(headers)

docs = db.collection("vital_signs").stream()

for d in docs:
    data = d.to_dict()

    ts_raw = data.get("timestamp")
    temp = data.get("tempC")
    spo2 = data.get("spo2")
    hr   = data.get("hr")
    motion = data.get("motion")

    acc = data.get("acc", [])
    acc_x = acc[0] if len(acc) > 0 else None
    acc_y = acc[1] if len(acc) > 1 else None
    acc_z = acc[2] if len(acc) > 2 else None

    gyr = data.get("gyr", [])
    gyr_x = gyr[0] if len(gyr) > 0 else None
    gyr_y = gyr[1] if len(gyr) > 1 else None
    gyr_z = gyr[2] if len(gyr) > 2 else None

    ecg_list = data.get("ecg", [])
    ecg_str = ",".join(map(str, ecg_list))

    row_raw = [
        d.id, ts_raw,
        temp, spo2, hr, motion,
        acc_x, acc_y, acc_z,
        gyr_x, gyr_y, gyr_z,
        ecg_str
    ]

    row = ["" if v is None else str(v) for v in row_raw]

    ws.append(row)

wb.save("vital_signs_export.xlsx")
print("✔ Excel generado: vital_signs_export.xlsx")
