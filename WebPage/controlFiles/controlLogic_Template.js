// Import Firebase web app services functions from Firebase SDK
import { initializeApp } from "https://www.gstatic.com/firebasejs/13.1.0/firebase-app.js";
import { getAuth, onAuthStateChanged, signOut } from "https://www.gstatic.com/firebasejs/13.1.0/firebase-auth.js"
import { getDatabase, ref, onValue, set } from "https://www.gstatic.com/firebasejs/13.1.0/firebase-database.js";

/////////////////////////////////// !!!DO NOTE PUBLICIZE!!! //////////////////////////////////
const firebaseConfig = {
  apiKey: "PASTE_API_KEY",
  authDomain: "PASTE_AUTH_DOMAIN",
  databaseURL: "PASTE_DATABASE_URL",
  projectId: "PASTE_PROJECT_ID",
  storageBucket: "PASTE_STORAGE_BUCKET",
  messagingSenderId: "PASTE_MESSAGING_SENDER_ID",
  appId: "PASTE_APP_ID"
};
///////////////////////////////////////////////////////////////////////////////////////////////

// Initialize Firebase App, Auth, and Database
const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getDatabase(app);

// DOM element connected to control.html IDs
const userEmailSpan = document.getElementById("user-email");
const logoutButton = document.getElementById("logout-button");
const fanStatusSpan = document.getElementById("fan-status");
const micButton = document.getElementById("mic-button");
const transcriptDisplay = document.getElementById("transcript-display");
const voiceError = document.getElementById("voice-error");

// Redirect to login page if user is not logged-in
onAuthStateChanged(auth, (user) => {
    if(user) {
        userEmailSpan.textContent = user.email;
    } else {
        window.location.href = "../login.html";
    }
});

// Handle lougout button logic
logoutButton.addEventListener("click", async () => {
    try {
        // If log out was successful, onAuthStateChanged redirects back to login page
        await signOut(auth);
    } catch(error) { // Else check error code
        console.error("Error signing out:", error);
    }
});

// Real-time Firebase Fan Status Listener (Reading from state/speed)
const fanStateRef = ref(db, "state/speed");

onValue(fanSateRef, (snapshot) => {
    const currentSpeed = snapshot.val();
    if (currentSpeed) {
        fanStatusSpan.textContent = currentSpeed.toUpperCase();
    } else {
        fanStatusSpan.textContent = "OFF";
    }
});
