// node --experimental-websocket tools/web_shot.mjs WIDTHxHEIGHT steps.json
// Drives headless Chromium; screenshots are saved in the current directory.
// steps: {go:url} {eval:js} {wait:ms} {shot:name} {key:"Enter"} {type:"text"}
import { spawn } from "child_process";
import fs from "fs";
import os from "os";
import path from "path";
const [size, stepsFile] = process.argv.slice(2);
const [W, H] = size.split("x").map(Number);
const dir = process.cwd() + "/";
const prof = fs.mkdtempSync(path.join(os.tmpdir(), "webshot-"));
const chrome = spawn("chromium", ["--headless=new", "--remote-debugging-port=9333", `--user-data-dir=${prof}`,
  `--window-size=${W},${H}`, "--no-first-run", "about:blank"], { stdio: "ignore" });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let ws;
for (let i = 0; i < 50 && !ws; i++) {
  await sleep(200);
  try { const t = await (await fetch("http://127.0.0.1:9333/json")).json(); const p = t.find(x => x.type === "page"); if (p) ws = new WebSocket(p.webSocketDebuggerUrl); } catch {}
}
await new Promise(r => ws.onopen = r);
let id = 0; const pend = {};
ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend[d.id]) { pend[d.id](d); delete pend[d.id]; } if (d.method === "Runtime.exceptionThrown") console.log("EXC", JSON.stringify(d.params.exceptionDetails).slice(0, 400)); if (d.method === "Runtime.consoleAPICalled") console.log("console:", d.params.args.map(a => a.value).join(" ")); };
const send = (method, params = {}) => new Promise(r => { pend[++id] = r; ws.send(JSON.stringify({ id, method, params })); });
await send("Runtime.enable"); await send("Page.enable");
await send("Emulation.setDeviceMetricsOverride", { width: W, height: H, deviceScaleFactor: 1, mobile: W < 600 });
for (const s of JSON.parse(fs.readFileSync(stepsFile, "utf8"))) {
  if (s.go) { await send("Page.navigate", { url: s.go }); await sleep(1500); }
  if (s.wait) await sleep(s.wait);
  if (s.eval) { const r = await send("Runtime.evaluate", { expression: s.eval, awaitPromise: true, returnByValue: true }); const v = r.result?.result?.value ?? r.result?.exceptionDetails?.exception?.description; console.log("eval:", typeof v === "string" ? v.slice(0, 1500) : JSON.stringify(v)); }
  if (s.type) await send("Input.insertText", { text: s.type });
  if (s.key) { for (const t of ["keyDown", "keyUp"]) await send("Input.dispatchKeyEvent", { type: t, key: s.key, code: s.key, windowsVirtualKeyCode: s.key === "Enter" ? 13 : 0 }); }
  if (s.shot) { const r = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: !!s.full }); fs.writeFileSync(dir + s.shot + ".png", Buffer.from(r.result.data, "base64")); console.log("shot", s.shot); }
}
chrome.kill(); await sleep(500); fs.rmSync(prof, { recursive: true, force: true }); process.exit(0);
