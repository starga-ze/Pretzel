import { fetchSessionStats, fetchEngineStats, fetchShardStats, fetchMarketStats, login, logout } 
from "./api.js";

import { renderDashboard } 
from "./dashboard.js";

import { renderSessions } 
from "./sessions.js";

import { renderMarkets }
from "./markets.js";

let intervalId = null;

function setupTabs() {
    document.querySelectorAll(".tab").forEach(btn => {
        btn.addEventListener("click", () => {
            document.querySelectorAll(".tab").forEach(t => t.classList.remove("active"));
            document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));

            btn.classList.add("active");
            document.getElementById(btn.dataset.tab).classList.add("active");
        });
    });
}

function setupLogout() {
    const btn = document.querySelector(".logout-btn");
    if (!btn) return;

    btn.addEventListener("click", async () => {
        try {
            await logout();
        } finally {
            showLogin();
        }
    });
}

async function update() {
    try {
        const [engineStats, sessionStats, shardStats, marketStats] = await Promise.all([
            fetchEngineStats(),
            fetchSessionStats(),
            fetchShardStats(),
            fetchMarketStats()
        ]);

        renderDashboard(engineStats, sessionStats.sessions, shardStats);
        renderSessions(sessionStats.sessions);
        renderMarkets(marketStats.markets);
    } catch (e) {
        console.error("Fetch failed", e);
    }
}

function startLoop() {
    if (intervalId) return;
    intervalId = setInterval(update, 1000);
    update();
}

function stopLoop() {
    if (intervalId) {
        clearInterval(intervalId);
        intervalId = null;
    }
}

function showLogin() {
    stopLoop();
    document.getElementById("loginOverlay").style.display = "flex";
}

function hideLogin() {
    document.getElementById("loginOverlay").style.display = "none";
}

async function handleLogin() {
    const username = document.getElementById("username").value;
    const password = document.getElementById("password").value;

    try {
        await login(username, password);
        hideLogin();
        startLoop();
    } catch {
        document.getElementById("loginError").innerText =
            "Invalid username or password";
    }
}

async function handleLogout() {
    try {
        await logout();
    } finally {
        showLogin();
    }
}

window.handleLogout = handleLogout;

window.addEventListener("unauthorized", () => {
    showLogin();
});

window.onload = async function () {
    setupTabs();
    setupLogout();

    try {
        await fetchEngineStats();
        hideLogin();
        startLoop();
    } catch {
        showLogin();
    }
};

window.handleLogin = handleLogin;
