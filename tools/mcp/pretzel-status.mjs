#!/usr/bin/env node
// Minimal MCP server (stdio, zero dependencies) exposing pretzel daemon state.
// Protocol: JSON-RPC 2.0 over stdin/stdout, one message per line.
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { readFile } from "node:fs/promises";

const run = promisify(execFile);
const LOG_DIR = "/var/log/pretzel";
const DAEMONS = ["apid", "authd", "collectord", "engined", "ipcd", "mgmtd", "probed", "topologyd"];

const TOOLS = [
  {
    name: "daemon_status",
    description: "Show systemd active state for every pretzel daemon (pz-*.service and pretzel-ai).",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
  },
  {
    name: "tail_log",
    description: `Tail the last lines of a pretzel daemon log from ${LOG_DIR}.`,
    inputSchema: {
      type: "object",
      properties: {
        daemon: { type: "string", enum: DAEMONS, description: "Daemon name, without the pz- prefix." },
        lines: { type: "integer", minimum: 1, maximum: 500, default: 50 },
      },
      required: ["daemon"],
      additionalProperties: false,
    },
  },
];

async function daemonStatus() {
  const units = [...DAEMONS.map((d) => `pz-${d}.service`), "pretzel-ai.service"];
  // is-active exits non-zero when any unit is down, so capture output either way.
  const out = await run("systemctl", ["is-active", ...units]).catch((e) => ({ stdout: e.stdout ?? "" }));
  const states = out.stdout.trim().split("\n");
  return units.map((u, i) => `${(states[i] ?? "unknown").padEnd(10)} ${u}`).join("\n");
}

async function tailLog({ daemon, lines = 50 }) {
  if (!DAEMONS.includes(daemon)) throw new Error(`unknown daemon: ${daemon}`);
  const n = Math.min(Math.max(Number(lines) || 50, 1), 500);
  const text = await readFile(`${LOG_DIR}/${daemon}.log`, "utf8");
  // Strip ANSI color codes that spdlog writes into the file.
  return text.replace(/\x1b\[[0-9;]*m/g, "").trimEnd().split("\n").slice(-n).join("\n");
}

async function callTool(name, args) {
  if (name === "daemon_status") return daemonStatus();
  if (name === "tail_log") return tailLog(args ?? {});
  throw new Error(`unknown tool: ${name}`);
}

async function handle(msg) {
  switch (msg.method) {
    case "initialize":
      return {
        protocolVersion: msg.params?.protocolVersion ?? "2025-06-18",
        capabilities: { tools: {} },
        serverInfo: { name: "pretzel-status", version: "0.1.0" },
      };
    case "tools/list":
      return { tools: TOOLS };
    case "tools/call": {
      const text = await callTool(msg.params?.name, msg.params?.arguments);
      return { content: [{ type: "text", text }] };
    }
    case "ping":
      return {};
    default:
      return null; // unsupported -> method-not-found
  }
}

const send = (obj) => process.stdout.write(JSON.stringify(obj) + "\n");

let buf = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", async (chunk) => {
  buf += chunk;
  let nl;
  while ((nl = buf.indexOf("\n")) !== -1) {
    const line = buf.slice(0, nl).trim();
    buf = buf.slice(nl + 1);
    if (!line) continue;

    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      continue;
    }
    if (msg.id === undefined) continue; // notification: nothing to answer

    try {
      const result = await handle(msg);
      if (result === null) send({ jsonrpc: "2.0", id: msg.id, error: { code: -32601, message: `method not found: ${msg.method}` } });
      else send({ jsonrpc: "2.0", id: msg.id, result });
    } catch (e) {
      send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: `error: ${e.message}` }], isError: true } });
    }
  }
});
