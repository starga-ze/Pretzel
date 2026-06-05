async function request(url, options = {}) {
    const res = await fetch(url, {
        credentials: "include",
        ...options
    });

    if (res.status === 401) {
        window.dispatchEvent(new Event("unauthorized"));
        throw new Error("Unauthorized");
    }

    if (!res.ok) {
        throw new Error("Request failed");
    }

    return res.json();
}

export async function fetchSessionStats() {
    return request("/api/v1/stats/session");
}

export async function fetchEngineStats() {
    return request("/api/v1/stats/engine");
}

export async function fetchShardStats() {
    return request("/api/v1/stats/shard");
}

export async function fetchMarketStats() {
    return request("/api/v1/stats/market");
}

export async function login(username, password) {
    const res = await fetch("/api/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "include",
        body: JSON.stringify({ username, password })
    });

    if (!res.ok) {
        throw new Error("Invalid credentials");
    }

    return true;
}

export async function logout() {
    await request("/api/logout", {
        method: "POST"
    });
}
