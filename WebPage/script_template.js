import { initializeApp } from
  "https://www.gstatic.com/firebasejs/13.1.0/firebase-app.js";

import {
  getDatabase,
  ref,
  set,
  onValue
} from
  "https://www.gstatic.com/firebasejs/13.1.0/firebase-database.js";

import {
  getAuth,
  onAuthStateChanged,
  signInWithEmailAndPassword,
  signOut
} from
  "https://www.gstatic.com/firebasejs/13.1.0/firebase-auth.js";

const firebaseConfig = {
  apiKey:,
  authDomain:,
  databaseURL:,
  projectId:,
  storageBucket:,
  messagingSenderId:,
  appId:,
  measurementId:
};

const elements = {
  connectionBadge: document.getElementById("connectionBadge"),
  connectionMessage: document.getElementById("connectionMessage"),
  authBadge: document.getElementById("authBadge"),
  loginForm: document.getElementById("loginForm"),
  loginButton: document.getElementById("loginButton"),
  signedInPanel: document.getElementById("signedInPanel"),
  signedInAs: document.getElementById("signedInAs"),
  signOutButton: document.getElementById("signOutButton"),
  authMessage: document.getElementById("authMessage"),
  fanState: document.getElementById("fanState"),
  stateMessage: document.getElementById("stateMessage"),
  commandMessage: document.getElementById("commandMessage"),
  voiceButton: document.getElementById("voiceButton"),
  voiceMessage: document.getElementById("voiceMessage"),
  transcript: document.getElementById("transcript")
};

const speedButtons = [...document.querySelectorAll("[data-speed]")];
let database = null;
let auth = null;
let signedInUser = null;
let stopStateListener = null;
let speechRecognition = null;

function setBadge(element, label, kind = "waiting") {
  element.textContent = label;
  element.classList.remove("status-waiting", "status-good", "status-error");
  element.classList.add(`status-${kind}`);
}

function setCommandControlsEnabled(enabled) {
  speedButtons.forEach((button) => { button.disabled = !enabled; });
  elements.voiceButton.disabled = !enabled || !speechRecognition;
}

function configIsComplete() {
  const required = [
    firebaseConfig.apiKey,
    firebaseConfig.authDomain,
    firebaseConfig.databaseURL,
    firebaseConfig.projectId,
    firebaseConfig.appId
  ];
  return required.every((value) => value && !value.startsWith("PASTE_"));
}

function friendlyError(error) {
  const code = error?.code || "";
  if (code.includes("auth/invalid-credential") || code.includes("auth/wrong-password") || code.includes("auth/user-not-found")) {
    return "Sign-in failed. Check the email and password and confirm the user exists in Firebase Authentication.";
  }
  if (code.includes("auth/invalid-email")) return "Enter a valid email address.";
  if (code.includes("auth/too-many-requests")) return "Too many attempts. Wait a bit, then try again.";
  if (code.includes("PERMISSION_DENIED") || code.includes("permission_denied")) {
    return "Firebase denied access. Check Authentication and Realtime Database security rules; do not make the database public.";
  }
  if (code.includes("network-request-failed")) return "Network error. Check your internet connection and try again.";
  return error?.message || "An unexpected error occurred.";
}

async function sendFanCommand(speed) {
  if (!signedInUser || !database) {
    elements.commandMessage.textContent = "Sign in before sending commands.";
    return;
  }

  elements.commandMessage.textContent = `Sending ${speed} command…`;
  speedButtons.forEach((button) => { button.disabled = true; });
  try {
    // This matches the imported JSON structure: fan/command/speed and commandId.
    await set(ref(database, "fan/command"), {
      speed,
      commandId: Date.now()
    });
    elements.commandMessage.textContent = `Command sent: ${speed}. Waiting for the ESP32 to report the actual fan state.`;
  } catch (error) {
    elements.commandMessage.textContent = `Command not sent: ${friendlyError(error)}`;
  } finally {
    speedButtons.forEach((button) => { button.disabled = !signedInUser; });
    elements.voiceButton.disabled = !signedInUser || !speechRecognition;
  }
}

speedButtons.forEach((button) => {
  button.addEventListener("click", () => sendFanCommand(button.dataset.speed));
});

elements.loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!auth) {
    elements.authMessage.textContent = "First paste your Firebase web app configuration into script.js.";
    return;
  }

  const email = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value;
  elements.loginButton.disabled = true;
  elements.authMessage.textContent = "Signing in…";
  try {
    await signInWithEmailAndPassword(auth, email, password);
    elements.authMessage.textContent = "Signed in.";
    elements.loginForm.reset();
  } catch (error) {
    elements.authMessage.textContent = friendlyError(error);
  } finally {
    elements.loginButton.disabled = false;
  }
});

elements.signOutButton.addEventListener("click", async () => {
  if (!auth) return;
  try {
    await signOut(auth);
    elements.authMessage.textContent = "You have signed out.";
  } catch (error) {
    elements.authMessage.textContent = friendlyError(error);
  }
});

function handleRecognizedText(text) {
  const spoken = text.toLowerCase().replace(/[.,!?]/g, " ").replace(/\s+/g, " ").trim();
  elements.transcript.textContent = text || "No speech detected.";

  if (/\b(off|stop|turn off|switch off|shut down)\b/.test(spoken)) {
    sendFanCommand("off");
  } else if (/\b(low|slow|speed one|speed 1|first speed|level one|level 1)\b/.test(spoken)) {
    sendFanCommand("low");
  } else if (/\b(normal|medium|moderate|speed two|speed 2|second speed|level two|level 2)\b/.test(spoken)) {
    sendFanCommand("normal");
  } else if (/\b(high|fast|speed three|speed 3|third speed|level three|level 3)\b/.test(spoken)) {
    sendFanCommand("high");
  } else if (/\b(turn on|switch on|fan on|start fan|turn the fan on)\b/.test(spoken)) {
    // “Fan on” uses normal speed as the default ON speed.
    sendFanCommand("normal");
  } else {
    elements.commandMessage.textContent = "I did not recognize that as a fan command. Try off, low, normal, or high speed.";
  }
}

function setupSpeechRecognition() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    elements.voiceButton.disabled = true;
    elements.voiceMessage.textContent = "Speech recognition is not available in this browser. Try a current version of Chrome or Edge.";
    return;
  }

  speechRecognition = new SpeechRecognition();
  speechRecognition.lang = "en-US";
  speechRecognition.interimResults = false;
  speechRecognition.continuous = false;

  speechRecognition.addEventListener("start", () => {
    elements.voiceButton.textContent = "Listening… tap to stop";
    elements.voiceButton.classList.add("listening");
    elements.voiceMessage.textContent = "Listening for a fan command.";
  });
  speechRecognition.addEventListener("result", (event) => {
    const transcript = event.results?.[0]?.[0]?.transcript || "";
    handleRecognizedText(transcript);
  });
  speechRecognition.addEventListener("error", (event) => {
    elements.voiceMessage.textContent =
	`Voice recognition: ${event.error}. Check microphone permission and try again.`;
  });
  speechRecognition.addEventListener("end", () => {
    elements.voiceButton.textContent = "Start listening";
    elements.voiceButton.classList.remove("listening");
  });

  elements.voiceButton.disabled = true;
  elements.voiceButton.addEventListener("click", () => {
    if (elements.voiceButton.classList.contains("listening")) {
      speechRecognition.stop();
      return;
    }
    try {
      speechRecognition.start();
    } catch (error) {
      elements.voiceMessage.textContent = "Could not start voice recognition. Wait a moment and try again.";
    }
  });
}

setupSpeechRecognition();

if (!configIsComplete()) {
  setBadge(elements.connectionBadge, "Setup needed", "waiting");
  elements.connectionMessage.textContent = "Open script.js and replace the PASTE_… placeholders with your actual Firebase web app configuration.";
  elements.authMessage.textContent = "After adding the configuration, enable Email/Password Authentication and create a user in Firebase Console.";
  setCommandControlsEnabled(false);
} else {
  try {
    const app = initializeApp(firebaseConfig);
    auth = getAuth(app);
    database = getDatabase(app);
    setBadge(elements.connectionBadge, "Firebase initialized", "good");
    elements.connectionMessage.textContent = "Firebase SDK initialized. Sign in to read the fan state and send commands.";

    onAuthStateChanged(auth, (user) => {
      signedInUser = user;
      if (stopStateListener) {
        stopStateListener();
        stopStateListener = null;
      }

      if (user) {
        setBadge(elements.authBadge, "Signed in", "good");
        elements.loginForm.hidden = true;
        elements.signedInPanel.hidden = false;
        elements.signedInAs.textContent =
		`Signed in as ${user.email || "authenticated user"}`;
        elements.authMessage.textContent = "Signed in. If data does not load, check the database rules.";
        setCommandControlsEnabled(true);
        elements.commandMessage.textContent = "Ready to send a fan command.";

        stopStateListener = onValue(
          ref(database, "fan/state/speed"),
          (snapshot) => {
            const state = snapshot.val();
            if (["off", "low", "normal", "high"].includes(state)) {
              elements.fanState.textContent = state;
              elements.stateMessage.textContent = "This is the latest speed reported by the ESP32.";
            } else {
              elements.fanState.textContent = state == null ? "Unknown" : String(state);
              elements.stateMessage.textContent = "No recognized speed has been reported yet. Check that the ESP32 writes off, low, normal, or high to fan/state/speed.";
            }
          },
          (error) => {
            elements.stateMessage.textContent = friendlyError(error);
          }
        );
      } else {
        setBadge(elements.authBadge, "Signed out", "waiting");
        elements.loginForm.hidden = false;
        elements.signedInPanel.hidden = true;
        elements.fanState.textContent = "Unknown";
        elements.stateMessage.textContent = "Sign in to read the latest state reported by the ESP32.";
        elements.commandMessage.textContent = "Sign in to send a command.";
        setCommandControlsEnabled(false);
      }
    });
  } catch (error) {
    setBadge(elements.connectionBadge, "Initialization error", "error");
    elements.connectionMessage.textContent = friendlyError(error);
    setCommandControlsEnabled(false);
  }
}