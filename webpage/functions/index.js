const admin = require('firebase-admin');
const express = require('express');
const cors = require('cors');

const { onRequest } = require('firebase-functions/v2/https');
const { onDocumentCreated } = require('firebase-functions/v2/firestore');

admin.initializeApp();
const db = admin.firestore();

const APPS_SCRIPT_URL   = 'https://script.google.com/macros/s/AKfycby_LbewiCWfNEqGzAWZqOGtT6OKVgbXqIhSCuhKGThu-Rg-AW5v36VBTKCtUk8PSCub/exec'; 
const APPS_SCRIPT_TOKEN = 'VSM3';                     

const FROM_EMAIL = 'no-reply@vsm-alerts.local';
const MOTION = { suppressHR: 0.25, suppressSpO2: 0.25 }; //for motion
const TH = { hr: { min: 50, max: 110 }, spo2: { min: 92, max: 100 }, temp: { min: 25.0, max: 38.0 } };
const COOLDOWN_MIN = 10;
const fmt = (n) => (typeof n === 'number' ? n.toFixed(2) : n);

async function sendMailViaAppsScript({ to, subject, html }) {
  const r = await fetch(APPS_SCRIPT_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ to, subject, html, token: APPS_SCRIPT_TOKEN }),
  });

  if (!r.ok) {
    const text = await r.text();
    throw new Error(`HTTP ${r.status}: ${text}`);
  }
  const json = await r.json();
  if (!json?.ok) throw new Error(`AppsScript error: ${json?.error || 'unknown'}`);
  return json;
}
// Function for thresholds
function outOfRange(d) {
  const bad = [];
  if (typeof d.hr === 'number'    && (d.hr    < TH.hr.min    || d.hr    > TH.hr.max))    bad.push(`HR ${d.hr}`);
  if (typeof d.spo2 === 'number'  && (d.spo2  < TH.spo2.min  || d.spo2  > TH.spo2.max))  bad.push(`SpO₂ ${d.spo2}`);
  if (typeof d.tempC === 'number' && (d.tempC < TH.temp.min  || d.tempC > TH.temp.max))  bad.push(`Temp ${d.tempC} °C`);
  return bad;
}

const app = express();
app.use(cors({ origin: true }));
app.use(express.json());

app.post('/ingest', async (req, res) => {
  try {
    const data = req.body || {};
    const ownerUid = String(data.ownerUid || req.query.ownerUid || 'jGfndzPYWXXHBp8k3YmLlNZ2Lck2');

    await db.collection('vital_signs').add({
      ...data,
      ownerUid,
      timestamp: admin.firestore.Timestamp.now(),
    });

    res.status(200).send({ success: true });
  } catch (err) {
    console.error('Error en /ingest:', err);
    res.status(500).send({ success: false, error: err.message });
  }
});

app.get('/latest', async (_req, res) => {
  try {
    const snap = await db.collection('vital_signs').orderBy('timestamp', 'desc').limit(1).get();
    if (snap.empty) return res.status(404).send({ message: 'No data' });
    res.status(200).send(snap.docs[0].data());
  } catch (err) {
    console.error('Error en /latest:', err);
    res.status(500).send({ success: false, error: err.message });
  }
});

exports.api = onRequest({ region: 'us-central1' }, app);

exports.emailOnAbnormalVitals = onDocumentCreated(
  'vital_signs/{docId}',
  async (event) => {
    if (!event?.data) {
      console.error('emailOnAbnormalVitals sin event.data válido:', event);
      return;
    }
    const snap = event.data;
    const data = snap.data();
    if (!data) {
      console.error('emailOnAbnormalVitals sin data en snapshot');
      return;
    }
    const issues = outOfRange(data);
    if (!issues.length) return;

    let workerEmail = null, workerPhone = null, workerFullName = null;
    if (data.ownerUid) {
      const u = await db.collection('users').doc(String(data.ownerUid)).get();
      if (u.exists) {
        const ud = u.data() || {};
        workerFullName = [ud.name, ud.lastname].filter(Boolean).join(' ') || ud.displayName || null;
        workerEmail = ud.email || null;
        workerPhone = ud.phone || null;
      }
    }

    const adminsSnap = await db.collection('users').where('role', '==', 'admin').get();
    const adminEmails = adminsSnap.docs.map(d => (d.data().email || '').trim()).filter(Boolean);
    if (!adminEmails.length) return;

    const keyId = `${data.ownerUid || 'unknown'}::${issues.join('|')}`;
    const logRef = db.collection('alert_logs').doc(keyId);
    const prev = await logRef.get();
    if (prev.exists) {
      const last = prev.data().lastSent?.toDate?.() || new Date(0);
      if ((Date.now() - last.getTime()) / 60000 < COOLDOWN_MIN) return;
    }
    await logRef.set({ lastSent: admin.firestore.Timestamp.now(), lastIssues: issues }, { merge: true });

    const whenIso = data.timestamp instanceof admin.firestore.Timestamp
      ? data.timestamp.toDate().toISOString()
      : new Date(data.timestamp || Date.now()).toISOString();

    const who = workerFullName || data.ownerUid || 'Worker';
    const subject = `⚠️ Vital Sign Warning: ${who} — ${issues.join(', ')}`;
    const html = `
      <div style="font-family:system-ui,Segoe UI,Roboto,Arial,sans-serif">
        <h2>Vital signal warning</h2>
        <p><strong>Worker:</strong> ${who}<br/>
           <strong>Email:</strong> ${workerEmail || '-'}<br/>
           <strong>Cellphone:</strong> ${workerPhone || '-'}</p>
        <p><strong>Out of range:</strong> ${issues.join(', ')}</p>
        <table cellpadding="6" cellspacing="0" border="1" style="border-collapse:collapse">
          <tr><th>HR</th><th>SpO₂</th><th>Temp (°C)</th><th>Date</th></tr>
          <tr><td>${fmt(data.hr)}</td><td>${fmt(data.spo2)}</td><td>${fmt(data.tempC)}</td><td>${whenIso}</td></tr>
        </table>
        <p style="color:#666;font-size:12px">From: ${FROM_EMAIL}</p>
      </div>`;

    try {
      await sendMailViaAppsScript({ to: adminEmails, subject, html });
      console.log('Apps Script email sent:', adminEmails);
    } catch (e) {
      console.error('Apps Script error:', e);
    }
  }
);
