import { spawn } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const production = process.argv.includes("--production");
const staticRoot = production ? path.join(root, "build/viewer") : root;
const entryPath = production
  ? "/tools/viewer/live/index.html"
  : "/tests/melee_core/viewer_browser_smoke.html";
const systemChromePaths = [
  "/opt/google/chrome/chrome",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
];
const chrome =
  process.env.CHROME ||
  systemChromePaths.find((path) => fs.existsSync(path)) ||
  "google-chrome";
const profile = path.join(root, "reports/triage/viewer-browser-profile");
const contentTypes = new Map([
  [".data", "application/octet-stream"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".wasm", "application/wasm"],
]);

function localFile(url) {
  const pathname = decodeURIComponent(new URL(url, "http://127.0.0.1").pathname);
  const resolved = path.resolve(staticRoot, `.${pathname}`);
  if (resolved !== staticRoot && resolved.startsWith(`${staticRoot}${path.sep}`)) return resolved;
  return null;
}

fs.rmSync(profile, { recursive: true, force: true });
const server = http.createServer((request, response) => {
  const file = localFile(request.url || "/");
  if (!file || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404).end("not found\n");
    return;
  }
  response.writeHead(200, {
    "content-type": contentTypes.get(path.extname(file)) || "application/octet-stream",
    "cache-control": "no-store",
  });
  fs.createReadStream(file).pipe(response);
});

await new Promise((resolve, reject) => {
  server.once("error", reject);
  server.listen(0, "127.0.0.1", resolve);
});
const { port } = server.address();
const child = spawn(chrome, [
  "--headless=new",
  "--no-sandbox",
  "--disable-gpu",
  "--disable-dev-shm-usage",
  `--user-data-dir=${profile}`,
  "--remote-debugging-port=0",
  `http://127.0.0.1:${port}${entryPath}`,
], { detached: true, stdio: ["ignore", "pipe", "pipe"] });

function signalChromeGroup(signal) {
  try {
    process.kill(-child.pid, signal);
  } catch (error) {
    if (error.code !== "ESRCH") throw error;
  }
}

process.once("exit", () => {
  signalChromeGroup("SIGKILL");
  server.close();
  fs.rmSync(profile, { recursive: true, force: true });
});

let stderr = "";
let devtoolsResolve;
const devtoolsUrl = new Promise((resolve) => { devtoolsResolve = resolve; });
child.stderr.setEncoding("utf8").on("data", (chunk) => {
  stderr += chunk;
  const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
  if (match) devtoolsResolve(match[1]);
});

const deadline = Date.now() + 6000;
const browserWs = await Promise.race([
  devtoolsUrl,
  new Promise((_, reject) => setTimeout(() => reject(new Error("Chrome did not expose DevTools")), 2000)),
]);
const debugPort = new URL(browserWs).port;
let target;
while (!target && Date.now() < deadline) {
  const targets = await fetch(`http://127.0.0.1:${debugPort}/json/list`).then((response) => response.json());
  target = targets.find((entry) => entry.type === "page");
  if (!target) await new Promise((resolve) => setTimeout(resolve, 25));
}
if (!target) throw new Error("Chrome did not create a page target");

const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});
let nextId = 1;
const pending = new Map();
socket.addEventListener("message", (event) => {
  const message = JSON.parse(event.data);
  const callback = pending.get(message.id);
  if (callback) {
    pending.delete(message.id);
    callback(message);
  }
});
const call = (method, params = {}) => new Promise((resolve) => {
  const id = nextId++;
  pending.set(id, resolve);
  socket.send(JSON.stringify({ id, method, params }));
});

let result = "PENDING";
if (production) {
  let keyboardStarted = false;
  let changedSelection = false;
  while (Date.now() < deadline) {
    const response = await call("Runtime.evaluate", {
      expression: `(() => {
        const status = document.querySelector("#status");
        const viewer = document.querySelector("slippi-viewer");
        const replay = window.__mslViewerReplayData;
        const frameCount = window.__mslViewerFrameCount || 0;
        const current = replay?.frames?.[frameCount];
        const svg = viewer?.shadowRoot?.querySelector("svg") || viewer?.querySelector("svg");
        return JSON.stringify({
          error: status?.classList.contains("error") ? status.textContent : "",
          frames: frameCount,
          stage: replay?.settings?.stageId,
          character: replay?.settings?.playerSettings?.[0]?.internalCharacterIds?.[0],
          keyboard: current?.players?.[0]?.inputs?.processed?.joystickX === 1,
          trace: window.__mslTraceSaved === true,
          api: typeof viewer?.setLiveReplayData === "function" && typeof viewer?.setFrame === "function",
          svg: Boolean(svg),
        });
      })()`,
      returnByValue: true,
    });
    const state = JSON.parse(response.result?.result?.value || "{}");
    if (state.error) {
      result = `FAIL|${state.error}`;
      break;
    }
    if (!keyboardStarted && state.frames >= 2 && state.api) {
      keyboardStarted = true;
      await call("Runtime.evaluate", {
        expression: `window.dispatchEvent(new KeyboardEvent("keydown", { key: "d", bubbles: true }))`,
      });
    } else if (keyboardStarted && state.keyboard && !changedSelection) {
      changedSelection = true;
      await call("Runtime.evaluate", {
        expression: `(() => {
          window.dispatchEvent(new KeyboardEvent("keyup", { key: "d", bubbles: true }));
          window.prompt = () => "phase6-browser-smoke";
          URL.createObjectURL = () => "blob:phase6-browser-smoke";
          URL.revokeObjectURL = () => {};
          HTMLAnchorElement.prototype.click = function () {
            window.__mslTraceSaved = this.download.endsWith(".msltrace.json");
          };
          document.querySelector("#save-trace").click();
          const character = document.querySelector("#p1-character");
          character.value = "15";
          character.dispatchEvent(new Event("change"));
          document.querySelector('button[data-stage-id="8"]').click();
        })()`,
      });
    } else if (
      changedSelection &&
      state.frames >= 2 &&
      state.stage === 8 &&
      state.character === 15 &&
      state.trace &&
      state.api &&
      state.svg
    ) {
      result = "PASS|production viewer";
      break;
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
} else {
  while (Date.now() < deadline) {
    const response = await call("Runtime.evaluate", {
      expression: "`${document.body?.dataset.smoke || 'PENDING'}|${document.querySelector('#result')?.textContent || ''}`",
      returnByValue: true,
    });
    result = response.result?.result?.value || "PENDING";
    if (result.startsWith("PASS|") || result.startsWith("FAIL|")) break;
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
}
socket.close();
signalChromeGroup("SIGTERM");
await Promise.race([
  new Promise((resolve) => child.once("exit", resolve)),
  new Promise((resolve) => setTimeout(resolve, 250)),
]);
signalChromeGroup("SIGKILL");
server.close();
fs.rmSync(profile, { recursive: true, force: true });

if (!result.startsWith("PASS|")) {
  throw new Error(`browser smoke failed: ${result}\n${stderr.slice(-1000)}`);
}
console.log(JSON.stringify({
  status: "PASS",
  browser: chrome,
  target: production ? "production live viewer" : "live viewer",
}));
