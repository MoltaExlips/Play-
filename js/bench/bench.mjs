// usage: node bench.mjs <baseUrl> <elf> <seconds> [screenshot]
import { chromium } from "playwright";
import { readdirSync, readFileSync } from "node:fs";

// Per-thread CPU (ms) inside the renderer process, keyed by thread name + tid.
function rendererThreads() {
  const out = {};
  for (const pid of readdirSync("/proc").filter((d) => /^\d+$/.test(d))) {
    let cmd = "";
    try { cmd = readFileSync(`/proc/${pid}/cmdline`, "utf8"); } catch { continue; }
    if (!cmd.includes("--type=renderer") || !cmd.includes("ms-playwright")) continue;
    for (const tid of readdirSync(`/proc/${pid}/task`)) {
      try {
        const raw = readFileSync(`/proc/${pid}/task/${tid}/stat`, "utf8");
        const name = raw.slice(raw.indexOf("(") + 1, raw.lastIndexOf(")"));
        const f = raw.slice(raw.lastIndexOf(")") + 2).split(" ");
        out[`${name}#${tid}`] = (Number(f[11]) + Number(f[12])) * 10;
      } catch {}
    }
  }
  return out;
}

// CPU time (ms) used so far by this script's child processes (the browser), split by Chrome process type.
function cpuMs() {
  const procs = {};
  for (const pid of readdirSync("/proc").filter((d) => /^\d+$/.test(d))) {
    try {
      const raw = readFileSync(`/proc/${pid}/stat`, "utf8");
      const f = raw.slice(raw.lastIndexOf(")") + 2).split(" ");
      procs[pid] = { ppid: f[1], ms: (Number(f[11]) + Number(f[12])) * 10 }; // utime + stime, 100 Hz ticks
    } catch {}
  }
  const isDescendant = (pid) => {
    for (let p = pid, n = 0; p && procs[p] && n < 50; p = procs[p].ppid, n++) {
      if (procs[p].ppid === String(process.pid)) return true;
    }
    return false;
  };
  const out = { renderer: 0, gpu: 0, other: 0 };
  for (const [pid, info] of Object.entries(procs)) {
    if (!isDescendant(pid)) continue;
    let cmd = "";
    try { cmd = readFileSync(`/proc/${pid}/cmdline`, "utf8"); } catch {}
    const type = cmd.includes("--type=renderer") ? "renderer" : cmd.includes("--type=gpu-process") ? "gpu" : "other";
    out[type] += info.ms;
  }
  return out;
}
const [base, elf, secs = "20", shot] = process.argv.slice(2);
import { mkdtempSync } from "node:fs";
const dataDir = mkdtempSync("/tmp/pwbench-");
const ctx = await chromium.launchPersistentContext(dataDir, { viewport: { width: 1100, height: 900 }, args: ["--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--disable-background-timer-throttling", "--disable-renderer-backgrounding"] });
const b = ctx;
const p = ctx.pages()[0] ?? await ctx.newPage();
const logs = [];
p.on("console", (m) => { const t = m.text(); if (!/Registered function/.test(t)) logs.push(t); });
p.on("pageerror", (e) => logs.push("pageerror: " + e.message));
await p.goto(base + "/ps2.html");
await p.waitForFunction(() => /ready/.test(document.getElementById("status").textContent), null, { timeout: 90000 });
await p.setInputFiles("#file", elf);
// Warm-up: let the JIT compile the hot blocks, then sample the f/s counter once per second.
await p.waitForTimeout(8000);
const samples = [];
const cpu0 = cpuMs();
const thr0 = rendererThreads();
for (let i = 0; i < Number(secs); i++) {
  await p.waitForTimeout(1000);
  samples.push(Number((await p.textContent("#fps")).split(" ")[0]) || 0);
}
const cpu1 = cpuMs();
const thr1 = rendererThreads();
const frames = samples.reduce((a, b) => a + b, 0);
const perFrame = Object.fromEntries(Object.keys(cpu0).map((k) => [k, +((cpu1[k] - cpu0[k]) / frames).toFixed(2)]));
if (shot) { await p.locator("#outputCanvas").scrollIntoViewIfNeeded(); await p.screenshot({ path: shot }); }
const avg = samples.reduce((a, b) => a + b, 0) / samples.length;
console.log(JSON.stringify({ avg: +avg.toFixed(1), min: Math.min(...samples), cpuMsPerFrame: perFrame, samples }));
if (process.env.THREADS) {
  const rows = Object.entries(thr1).map(([k, v]) => [k, (v - (thr0[k] ?? 0)) / frames]).filter(([, v]) => v > 0.05).sort((a, b) => b[1] - a[1]);
  console.log(rows.map(([k, v]) => `  ${k.padEnd(28)} ${v.toFixed(2)} ms/frame`).join("\n"));
}
if (process.env.LOGS) console.log(logs.slice(0, 15).join("\n"));
await b.close();
