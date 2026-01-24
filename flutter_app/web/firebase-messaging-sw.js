importScripts("https://www.gstatic.com/firebasejs/10.7.1/firebase-app-compat.js");
importScripts("https://www.gstatic.com/firebasejs/10.7.1/firebase-messaging-compat.js");

firebase.initializeApp({
  apiKey:  "AIzaSyCGTZhunlvoZ_ygxDX9YWTBTWqo8B-Hz_Y",
  authDomain: "smart-pet-feeder-dev2.firebaseapp.com",
  projectId: "smart-pet-feeder-dev2",
  messagingSenderId: "268604128388",
  appId: "1:268604128388:web:a1f69014ff710f408056d1"
});

const messaging = firebase.messaging();

// When a push arrives and the site is in the background / closed
messaging.onBackgroundMessage((payload) => {
  self.registration.showNotification(
    payload.notification?.title || "Notification",
    { body: payload.notification?.body || "" }
  );
});
