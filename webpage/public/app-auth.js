import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.0/firebase-app.js";
import {getAuth, onAuthStateChanged, signInWithEmailAndPassword,createUserWithEmailAndPassword} from "https://www.gstatic.com/firebasejs/10.14.0/firebase-auth.js";
import {getFirestore, doc, setDoc, getDoc} from "https://www.gstatic.com/firebasejs/10.14.0/firebase-firestore.js";

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

const $ = (id) =>document.getElementById(id);

const msg = $("msg");
const phoneEl = $("phone");
const status = $("status");
const nameEl = $("name");
const lastNameEl = $("lastname");

function updatePhoneVisibility() {
  const isWorker = status.checked;
  phoneEl.style.display = isWorker ? "block" : "none";
  nameEl.style.display = isWorker ? "block" : "none";
  lastNameEl.style.display = isWorker ? "block" : "none";
}

status.addEventListener("change", updatePhoneVisibility);
updatePhoneVisibility();

async function getUserProfile(uid){
  const snap = await getDoc(doc(db,"users",uid));
  return snap.exists()? snap.data() : null;
}

function roleFromToggle(){
  return status.checked ? "worker" : "admin";
}

$("btnSignup").addEventListener("click", async () => {
  try {
    msg.textContent = "Creating account...";
    const email = $("email").value.trim();
    const pass  = $("password").value.trim();
    const role = roleFromToggle();
    const phone = phoneEl.value.trim();
    const name = nameEl.value.trim();
    const lastname = lastNameEl.value.trim();

    if (!email || !pass) throw new Error("Email and password are required.");
    if (role === "worker" && (!phone || !name || !lastname))
      throw new Error("Name, last name, and phone are required for Worker.");

    const cred = await createUserWithEmailAndPassword(auth,email,pass);
    await setDoc(doc(db, "users", cred.user.uid), {
      role,
      email,
      phone: role === "worker" ? phone : null,
      name: role === "worker" ? name : null,
      lastname: role === "worker" ? lastname : null,
      createdAt: Date.now()
    });
    msg.textContent = "Account created";
    location.href = (role === "admin") ? "./admin.html":"./dashboard.html"
  } catch (e) {
    msg.textContent = e.message;
  }
});

$("btnLogin").addEventListener("click", async () => {
  try {
    msg.textContent = "Singing in...";
    const email = $("email").value.trim();
    const pass  = $("password").value.trim();

    const cred = await signInWithEmailAndPassword(auth,email,pass);
    const profile = await getUserProfile(cred.user.uid);
    if (!profile) throw new Error("User profile not found");

    location.href=(profile.role === "admin") ? "./admin.html": "./dashboard.html";
  } catch (e) {
    msg.textContent = e.message;
  }
});

onAuthStateChanged(auth, async (user)=>{
  if (!user) return;
  const profile = await getUserProfile(user.uid);
  if (!profile) return;
  location.href = (profile.role === "admin") ? "./admin.html":"./dashboard.html";

});