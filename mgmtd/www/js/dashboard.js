export function renderDashboard(engine, sessions, shard)
{
    const total = sessions?.length ?? 0;
    const auth  = sessions?.filter(s => s.state === "AUTH").length ?? 0;
    const world = sessions?.filter(s => s.state === "WORLD").length ?? 0;

    document.getElementById("totalSessions").textContent = total;
    document.getElementById("authSessions").textContent  = auth;
    document.getElementById("worldSessions").textContent = world;

    document.getElementById("shardCount").textContent = 
        shard?.shardCount != null ? shard.shardCount : "-";

    document.getElementById("engineUptime").textContent =
        formatUptime(engine?.uptime);

    document.getElementById("cpuUsage").textContent =
        engine?.cpuPercent != null
        ? engine.cpuPercent.toFixed(2) + " %"
        : "-";

    document.getElementById("rssUsage").textContent =
        engine?.rssMB != null ? engine.rssMB + " MB" : "-";

    document.getElementById("heapUsage").textContent =
        engine?.heapMB != null ? engine.heapMB + " MB" : "-";

    document.getElementById("dataUsage").textContent =
        engine?.dataMB != null ? engine.dataMB + " MB" : "-";

    document.getElementById("virtualUsage").textContent =
        engine?.virtualMB != null ? engine.virtualMB + " MB" : "-";

    document.getElementById("ipcStatus").textContent =
        engine?.ok ? "CONNECTED" : "DISCONNECTED";

    document.getElementById("ipcStatus").className =
        engine?.ok
        ? "status-badge connected"
        : "status-badge disconnected";
}

function formatUptime(sec) 
{
    if (sec == null) return "-";
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    return `${h}h ${m}m ${s}s`;
}
