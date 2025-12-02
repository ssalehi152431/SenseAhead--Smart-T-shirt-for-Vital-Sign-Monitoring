import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.0/firebase-app.js";
import { getAuth, onAuthStateChanged, signOut } from "https://www.gstatic.com/firebasejs/10.14.0/firebase-auth.js";

import {
  getFirestore,
  collection,
  query,
  where,
  orderBy,
  limit,
  getDocs
} from "https://www.gstatic.com/firebasejs/10.14.0/firebase-firestore.js";

const firebaseConfig = {
  apiKey: "AIzaSyCa_ZtXzClZVb66sdCPNXTntKeomFqMIEw",
  authDomain: "vsm3-741ac.firebaseapp.com",
  projectId: "vsm3-741ac",
  storageBucket: "vsm3-741ac.firebasestorage.app",
  messagingSenderId: "37278827953",
  appId: "1:37278827953:web:9a238c62b5c4a1e579a990",
};

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getFirestore(app);

const $ = (id) => document.getElementById(id);

// Cache worker for filtering
let workersCache = [];


onAuthStateChanged(auth, async (user) => {
  if (!user) {
    location.href = "./index.html";
    return;
  }

  $("adminEmail").textContent = user.email ?? "admin@email.com";
  $("adminName").textContent = user.displayName ?? "Admin";

  await loadWorkers();

  setInterval(loadWorkers, 5000);
});


async function loadWorkers() {
  const usersRef = collection(db, "users");
  const qWorkers = query(usersRef, where("role", "==", "worker"));

  const snap = await getDocs(qWorkers);

  const workers = [];
  for (const doc of snap.docs) {
    const data = doc.data();
    workers.push({
      uid: doc.id,
      name: data.name ?? "",
      lastname: data.lastname ?? "",
      email: data.email ?? "",
      phone: data.phone ?? "",
    });
  }

  // Get vital sign
  await attachVitalSigns(workers);

  workersCache = workers;
  renderWorkers(workers);
}

// Last vital sign saved
async function attachVitalSigns(workers) {
  for (const w of workers) {
    const vsRef = collection(db, "vital_signs");
    const q = query(
      vsRef,
      where("ownerUid", "==", w.uid),
      orderBy("timestamp", "desc"),
      limit(1)
    );

    const snap = await getDocs(q);

    if (!snap.empty) {
      const v = snap.docs[0].data();
      w.motion = v.motion ?? "-";
      w.spo2   = v.spo2   ?? "-";
      w.hr     = v.hr     ?? "-";      
      w.temp   = v.tempC  ?? "-";
      w.timestamp = v.timestamp ?? "-";

    } else {
      w.motion = "-";
      w.spo2   = "-";
      w.hr     = "-";
      w.temp   = "-";
      w.timestamp = "-";
    }
  }
}
function formatDate(ts) {
  if (!ts) return "-";

  if (ts.seconds) {
    return new Date(ts.seconds * 1000).toLocaleString();
  }

  // Por si acaso es string
  if (typeof ts === "string") return ts;

  return "-";
}

function renderWorkers(list) {
  const tbody = $("workersBody");
  tbody.innerHTML = "";

  if (!list.length) {
    tbody.innerHTML = `
      <tr class="placeholder">
        <td colspan="8">No hay workers.</td>
      </tr>`;
    return;
  }

  for (const w of list) {
    const tr = document.createElement("tr");

    tr.innerHTML = `
      <td>${w.name} ${w.lastname}</td>
      <td>${w.email}</td>
      <td>${w.phone}</td>
      <td>${w.motion}</td>
      <td>${w.spo2}</td>
      <td>${w.hr}</td>
      <td>${w.temp}</td>
      <td>${formatDate(w.timestamp)}</td>
    `;

    tbody.appendChild(tr);
  }
}

$("btnSearch").addEventListener("click", applySearch);
$("searchInput").addEventListener("keyup", (e) => {
  if (e.key === "Enter") applySearch();
  if (e.target.value === "") renderWorkers(workersCache);
});

function applySearch() {
  const term = $("searchInput").value.toLowerCase();

  const filtered = workersCache.filter((w) =>
    w.name.toLowerCase().includes(term) ||
    w.lastname.toLowerCase().includes(term) ||
    w.email.toLowerCase().includes(term) ||
    w.phone.toLowerCase().includes(term)
  );

  renderWorkers(filtered);
}

// Sign out
$("btnSignOut").addEventListener("click", async () => {
  try {
    await signOut(auth);
    location.href = "./index.html";
  } catch (e) {
    alert("No se pudo cerrar sesión: " + e.message);
  }
});
