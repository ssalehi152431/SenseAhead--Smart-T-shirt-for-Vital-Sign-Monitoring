import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.0/firebase-app.js";
import {getAuth, onAuthStateChanged, signOut} from "https://www.gstatic.com/firebasejs/10.14.0/firebase-auth.js";
import {getFirestore, collection, query, orderBy, limit, onSnapshot, doc,getDoc, setDoc, serverTimestamp} from "https://www.gstatic.com/firebasejs/10.14.0/firebase-firestore.js";
import { getMessaging, getToken, onMessage, isSupported } from "https://www.gstatic.com/firebasejs/10.14.0/firebase-messaging.js";

    const firebaseConfig = {
      apiKey: "AIzaSyCa_ZtXzClZVb66sdCPNXTntKeomFqMIEw",
      authDomain: "vsm3-741ac.firebaseapp.com",
      projectId: "vsm3-741ac",
      storageBucket: "vsm3-741ac.firebasestorage.app",
      messagingSenderId: "37278827953",
      appId: "1:37278827953:web:9a238c62b5c4a1e579a990",
      measurementId: "G-JFFB5ZZEB9"
    };

    const app  = initializeApp(firebaseConfig);
    const auth = getAuth(app);
    const db   = getFirestore(app);

    let messaging = null;
    isSupported().then((ok) => {if (ok) messaging = getMessaging(app);});
    async function saveFcmTokenForUser(uid){
      try {
        if(!messaging) return;
        const perm = await Notification.requestPermission();
        if (perm !== "granted") return;
        const reg = await navigator.serviceWorker.ready;
        const token = await getToken(messaging, {
          vapidKey: "BIbQ66dvgo-xKrSqgmIY-WPw8xu-9sdA3n78B8ZCmoaj97LSZcqyEm_LTbjt9Lhx4IhgIJyaQCY_uSFtDYaVUjQ",
          serviceWorkerRegistration: reg
        });
        if (!token) return;  
        await setDoc(
        doc(db, "users", uid, "tokens", token),
        { createdAt: serverTimestamp(), ua: navigator.userAgent },
        { merge: true }
        );
        onMessage(messaging, (payload) => {
        console.log("Push en foreground:", payload);
        const { title, body } = payload.notification || {};
        if (title && Notification.permission === "granted") {
          new Notification(title, { body });
        }
        });
       } catch (e) {
    console.warn("Error to save token FCM:", e);
    }
  }
    
    const $ = (id) => document.getElementById(id);
    const setText = (id, text) => { const el = $(id); if (el) el.textContent = text;};
    const toF = (c) => (c*9/5) +32;

    $("btnLogout")?.addEventListener("click", () => signOut(auth))

    // === ECG chart setup ===
  const ECG_POINTS = 500;      
  const ecgData = [];          
  const ecgCanvas = $("ecgChart");
  if (!ecgCanvas) {
    console.error("NO EXISTE <canvas id='ecgChart'> en el HTML");
  }

  const ecgCtx = ecgCanvas.getContext("2d");

  const ecgChart = new Chart(ecgCtx, {
    type: 'line',
    data: {
      labels: [],              /
      datasets: [{
        label: 'ECG',
        data: [],
        pointRadius: 0,
        borderWidth: 1,
        tension: 0,
        spanGaps: true
      }]
    },
    options: {
      animation: false,
      responsive: true,
      scales: {
        x: { display: false },
        y: {
            beginAtZero: false,
            min: (ctx) => Math.min(...ecgData) - 50,
            max: (ctx) => Math.max(...ecgData) + 50
          }
      }
    }
  });

  function pushECGSamples(arr) {
    for (const v of arr) {
      ecgData.push(Number(v));
    }

    // last ECG_POINTS
    if (ecgData.length > ECG_POINTS) {
      ecgData.splice(0, ecgData.length - ECG_POINTS);
    }

    ecgChart.data.labels = ecgData.map((_, i) => i);   
    ecgChart.data.datasets[0].data = ecgData;
    ecgChart.update('none');   
  }

    onAuthStateChanged(auth, async (user) => {
    if (!user) { location.href = "./index.html"; return; }
    try {
    const userRef = doc(db, "users", user.uid);
    const snap = await getDoc(userRef);
    if (snap.exists()) {
      const data = snap.data();
      const fullName = [data.name, data.lastname].filter(Boolean).join(" ");
      document.getElementById("userName").textContent = fullName || "User";
    } else {
      document.getElementById("userName").textContent = "User";
    }
    } catch (err) {
      console.error("Error loading user info:", err);
    }
    
    saveFcmTokenForUser(user.uid);
    const q = query(collection(db, "vital_signs"), orderBy("timestamp", "desc"), limit(1));
    onSnapshot(q, (snap) => {
      if (snap.empty) return;
      const d = snap.docs[0].data();

      if (d.acc) setText("acc", d.acc.map(v => Number(v).toFixed(2)).join(", "));
      if (d.gyr) setText("gyr", d.gyr.map(v => Number(v).toFixed(2)).join(", "));
      if (typeof d.tempC === "number") {
          setText("tempC", d.tempC.toFixed(2));
          setText("tempF", toF(d.tempC).toFixed(2));
        }
      if (typeof d.spo2 === "number") setText("spo2", d.spo2.toFixed(0));
      if (typeof d.hr   === "number") setText("hr",   d.hr.toFixed(0));
      if (Array.isArray(d.ecg) && d.ecg.length) {
        pushECGSamples(d.ecg.map(Number)); 
      }
    }, async (err) => {
      console.warn("Firestore error:", err);
      
      // Fallback HTTP 
      try {
        const r = await fetch("https://us-central1-vsm3-741ac.cloudfunctions.net/api/latest", { cache: "no-store" });
        const d = await r.json();
        if (d.acc) setText("acc", d.acc.map(v => Number(v).toFixed(2)).join(", "));
        if (d.gyr) setText("gyr", d.gyr.map(v => Number(v).toFixed(2)).join(", "));
        if (typeof d.tempC === "number") {
          setText("tempC", d.tempC.toFixed(2));
          setText("tempF", toF(d.tempC).toFixed(2));
        }
        if (typeof d.spo2 === "number") setText("spo2", d.spo2.toFixed(0));
        if (typeof d.hr   === "number") setText("hr",   d.hr.toFixed(0));
        if (Array.isArray(d.ecg) && d.ecg.length) pushECGSamples(d.ecg.map(Number));
      } catch (e2) {
        console.warn("HTTP fallback failed:", e2);
      }
    });
  });