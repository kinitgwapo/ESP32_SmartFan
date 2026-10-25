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

onValue(fanStateRef, (snapshot) => {
    const currentSpeed = snapshot.val();
    if (currentSpeed) {
        fanStatusSpan.textContent = currentSpeed.toUpperCase();
    } else {
        fanStatusSpan.textContent = "OFF";
    }
});

// Speech-to-Text Configuration & Writing to Command Path (fan/command)
const fanCommandRef = ref(db, "fan/command");
const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;

//Compatibility Guard & Engine Configuration
if (!SpeechRecognition) {
    micButton.disabled = true;
    voiceError.textContent = "Speech Recognition not supported in this browser. Use Chrome or Edge for best experience.";
} else {
    const recognition = new SpeechRecognition();
    recognition.continuous = false;
    recognition.lang = "en-US";
    recognition.interimResults = false;

    // Triggering Microphone Recording
    micButton.addEventListener("click", () => {
        voiceError.textContent = "";
        transcriptDisplay.textContent = "Listening...";
        try {
            recognition.start();
        } catch (e) {
            console.error(e);
        }
    });

    // Extracting Spoken Text
    recognition.onresult = async (event) => {
        const speechToText = event.results[0][0].transcript.toLowerCase().trim();
        transcriptDisplay.textContent = `"${speechToText}"`;

        // Parsing Commands & Writing to Firebase
        let targetSpeed = null;

        // Check speech keywords against the 4 speed settings
        if (speechToText.includes("high") || speechToText.includes("fast") || speechToText.includes("max")) {
            targetSpeed = "high";
        } else if (speechToText.includes("normal") || speechToText.includes("medium")) {
            targetSpeed = "normal";
        } else if (speechToText.includes("low") || speechToText.includes("slow")) {
            targetSpeed = "low";
        } else if (speechToText.includes("off") || speechToText.includes("stop")) {
            targetSpeed = "off";
        } else if (speechToText.includes("on") || speechToText.includes("start")) {
            // Default generic "turn on" command to normal speed
            targetSpeed = "normal";
        }

        // If a valid command was recognized, write to Firebase
        if (targetSpeed) {
            try {
                // Update speed and new timestamp commandId under fan/command/
                await set(fanCommandRef, {
                    speed: targetSpeed,
                    commandId: Date.now()
                });
            } catch (error) {
                voiceError.textContent = "Failed to send command to Firebase.";
            }
        } else {
            voiceError.textContent = "Command not recognized. Say 'low', 'normal', 'high', or 'turn off'.";
        }
    };

    // Handle microphone errors
    recognition.onerror = (event) => {
        voiceError.textContent = `Voice recognition error: ${event.error}`;
        transcriptDisplay.textContent = "Stopped listening.";
    };

    // Handle end of recognition
    recognition.onend = () => {
        if (transcriptDisplay.textContent === "Listening...") {
            transcriptDisplay.textContent = "Stopped listening.";
        }
    };
}