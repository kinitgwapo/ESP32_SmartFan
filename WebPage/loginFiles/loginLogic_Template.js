// Import Firebase web app services functions from Firebase SDK
import { initializeApp } from "https://www.gstatic.com/firebasejs/13.1.0/firebase-app.js";
import { getAuth, signInWithEmailAndPassword, onAuthStateChanged } from "https://www.gstatic.com/firebasejs/13.1.0/firebase-auth.js"

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

// Initialize Firebase App and Auth
const app = initializeApp(firebaseConfig);
const auth = getAuth(app);

// DOM element connected to login.html IDs
const errorMessage = document.getElementById("error-message");
const loginForm = document.getElementById("login-form");
const emailInput = document.getElementById("email-input");
const passwordInput = document.getElementById("password-input");

// Check if user already logged-in then redirect automatically to control page
onAuthStateChanged(auth, (user) => {
    if(user) {
        window.location.href = "../control.html";
    }
});

loginForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    errorMessage.textContent = "";

    const email = emailInput.value;
    const password = passwordInput.value;

    try {
        // If credentials correct, onAuthStateChanged redirects logged in user to control page
        await signInWithEmailAndPassword(auth, email, password);
    } catch(error) {
        switch(error.code) {
            case "auth/invalid-email":
                errorMessage.textContent = "Invalid email format!";
                break;
            case "auth/user-not-found":
            case "auth/wrong-password":
            case "auth/invalid-credential":
                errorMessage.textContent = "Incorrect email or password!";
                break;
            case "auth/too-many-requests":
                errorMessage.textContent = "Multiple failed attempts! try again later.";
                break;
            case "auth/network-request-failed":
                errorMessage.textContent = "Network error! check your connection.";
                break;
            default:
                errorMessage.textContent = 'Error: ${error.message}';
                break;
        }
    }
});