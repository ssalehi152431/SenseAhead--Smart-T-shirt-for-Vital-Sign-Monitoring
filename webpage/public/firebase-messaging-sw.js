importScripts('https://www.gstatic.com/firebasejs/10.14.0/firebase-app-compat.js');
importScripts('https://www.gstatic.com/firebasejs/10.14.0/firebase-messaging-compat.js');

firebase.initializeApp({
  apiKey: "AIzaSyCa_ZtXzClZVb66sdCPNXTntKeomFqMIEw",
  authDomain: "vsm3-741ac.firebaseapp.com",
  projectId: "vsm3-741ac",
  storageBucket: "vsm3-741ac.firebasestorage.app",
  messagingSenderId: "37278827953",
  appId: "1:37278827953:web:9a238c62b5c4a1e579a990",
});

const messaging = firebase.messaging();

self.addEventListener("notificationclick", (event) => {
  event.notification.close();
  event.waitUntil(clients.openWindow("/dashboard.html"));
});