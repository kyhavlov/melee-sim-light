#!/usr/bin/env node
import { createHash } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { URL } from "node:url";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "../..");
const staticRoot = path.resolve(process.env.MSL_VIEWER_STATIC_ROOT || repoRoot);
const urlPath = process.env.MSL_VIEWER_URL_PATH || "/tools/viewer/";
const sourcePath = path.join(__dirname, "live/gcadapter_stream.c");
const binaryPath = path.join(repoRoot, "reports/triage/gcadapter_stream");
const port = Number(process.env.MSL_VIEWER_PORT || 8001);
const host = process.env.MSL_VIEWER_HOST || "127.0.0.1";
const shouldOpenBrowser = process.env.MSL_VIEWER_OPEN !== "0";
const clients = new Set();

let latest = null;
let streamProcess = null;

const contentTypes = new Map([
  [".css", "text/css; charset=utf-8"],
  [".data", "application/octet-stream"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".map", "application/json; charset=utf-8"],
  [".wasm", "application/wasm"],
  [".zip", "application/zip"],
]);

const allowedStaticPrefixes = [
  "/tools/viewer/",
  "/tools/viewer/slippi-viewer/dist/",
  "/tools/viewer/slippi-viewer/public/",
];

function checkAssets() {
  const required = [
    urlPath === "/tools/viewer/" ? "tools/viewer/index.html" : "tools/viewer/live/index.html",
    "tools/viewer/msltrace1.js",
    "tools/viewer/live/public/msl_sim.js",
    "tools/viewer/live/public/msl_sim.wasm",
    "tools/viewer/live/public/msl_sim.data",
    "tools/viewer/slippi-viewer/dist/index.js",
  ];
  const missing = required.filter((rel) => !fs.existsSync(path.join(staticRoot, rel)));
  if (missing.length > 0) {
    console.error("Missing viewer assets:");
    for (const rel of missing) {
      console.error(`  ${rel}`);
    }
    console.error("Build them with:");
    console.error("  make viewer-build");
    process.exit(1);
  }
}

function compileStream() {
  fs.mkdirSync(path.dirname(binaryPath), { recursive: true });
  const pkg = spawnSync("pkg-config", ["--cflags", "--libs", "libusb-1.0"], {
    encoding: "utf8",
  });
  if (pkg.status !== 0) {
    throw new Error("pkg-config could not find libusb-1.0");
  }

  const args = [
    sourcePath,
    ...pkg.stdout.trim().split(/\s+/).filter(Boolean),
    "-O2",
    "-Wall",
    "-Wextra",
    "-o",
    binaryPath,
  ];
  const cc = spawnSync("cc", args, { encoding: "utf8", cwd: repoRoot });
  if (cc.status !== 0) {
    throw new Error(`cc failed:\n${cc.stderr || cc.stdout}`);
  }
}

function sendFrame(socket, text) {
  const payload = Buffer.from(text);
  let header;
  if (payload.length < 126) {
    header = Buffer.from([0x81, payload.length]);
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x81;
    header[1] = 126;
    header.writeUInt16BE(payload.length, 2);
  } else {
    throw new Error("websocket payload too large");
  }
  socket.write(Buffer.concat([header, payload]));
}

function broadcast(text) {
  for (const socket of clients) {
    if (!socket.destroyed) {
      sendFrame(socket, text);
    }
  }
}

function startStream() {
  streamProcess = spawn(binaryPath, [], {
    cwd: repoRoot,
    stdio: ["ignore", "pipe", "pipe"],
  });

  let pending = "";
  streamProcess.stdout.setEncoding("utf8");
  streamProcess.stdout.on("data", (chunk) => {
    pending += chunk;
    for (;;) {
      const newline = pending.indexOf("\n");
      if (newline < 0) break;
      const line = pending.slice(0, newline);
      pending = pending.slice(newline + 1);
      if (!line) continue;
      latest = line;
      broadcast(line);
    }
  });

  streamProcess.stderr.setEncoding("utf8");
  streamProcess.stderr.on("data", (chunk) => {
    process.stderr.write(`[gcadapter] ${chunk}`);
  });

  streamProcess.on("exit", (code, signal) => {
    const msg = JSON.stringify({
      error: `gcadapter_stream exited code=${code} signal=${signal}`,
      ports: [],
    });
    latest = msg;
    broadcast(msg);
  });
}

function urlPathToFile(reqUrl) {
  let rawPath = reqUrl.split(/[?#]/, 1)[0];
  try {
    rawPath = decodeURIComponent(rawPath);
  } catch {
    return { status: 403 };
  }
  if (rawPath.split("/").some((part) => part === ".." || part.startsWith("."))) {
    return { status: 403 };
  }
  const parsed = new URL(reqUrl, `http://127.0.0.1:${port}`);
  let pathname = decodeURIComponent(parsed.pathname);
  if (pathname === "/") {
    pathname = urlPath;
  }
  if (pathname.split("/").some((part) => part === ".." || part.startsWith("."))) {
    return { status: 403 };
  }
  if (!allowedStaticPrefixes.some((prefix) => pathname.startsWith(prefix))) {
    return { status: 403 };
  }
  if (pathname.endsWith("/")) {
    pathname += "index.html";
  }
  const resolved = path.resolve(staticRoot, `.${pathname}`);
  if (resolved !== staticRoot && !resolved.startsWith(`${staticRoot}${path.sep}`)) {
    return { status: 403 };
  }
  return { filePath: resolved };
}

function staticHeaders(filePath, stat) {
  const ext = path.extname(filePath);
  const etag = `W/"${stat.size.toString(16)}-${Math.trunc(stat.mtimeMs).toString(16)}"`;
  return {
    "content-type": contentTypes.get(ext) || "application/octet-stream",
    "content-length": stat.size,
    "cache-control": "no-cache",
    "last-modified": stat.mtime.toUTCString(),
    etag,
  };
}

function requestHasFreshStaticAsset(req, stat, etag) {
  if (req.headers["if-none-match"] === etag) {
    return true;
  }
  const modifiedSince = req.headers["if-modified-since"];
  if (!modifiedSince) {
    return false;
  }
  const sinceMs = Date.parse(modifiedSince);
  if (!Number.isFinite(sinceMs)) {
    return false;
  }
  return stat.mtimeMs <= sinceMs + 999;
}

function serveStatic(req, res) {
  const result = urlPathToFile(req.url || "/");
  if (!result.filePath) {
    res.writeHead(403, { "content-type": "text/plain; charset=utf-8" });
    res.end("forbidden\n");
    return;
  }
  const filePath = result.filePath;
  fs.stat(filePath, (statError, stat) => {
    if (statError || !stat.isFile()) {
      res.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
      res.end("not found\n");
      return;
    }
    const headers = staticHeaders(filePath, stat);
    if (requestHasFreshStaticAsset(req, stat, headers.etag)) {
      res.writeHead(304, {
        "cache-control": headers["cache-control"],
        "last-modified": headers["last-modified"],
        etag: headers.etag,
      });
      res.end();
      return;
    }
    res.writeHead(200, headers);
    if (req.method === "HEAD") {
      res.end();
      return;
    }
    fs.createReadStream(filePath).pipe(res);
  });
}

function acceptKey(key) {
  return createHash("sha1")
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest("base64");
}

function isAllowedOrigin(req) {
  const origin = req.headers.origin;
  if (!origin) {
    return true;
  }
  const requestHost = req.headers.host;
  if (!requestHost) {
    return false;
  }
  return origin === `http://${requestHost}`;
}

function rejectUpgrade(socket, statusCode, message) {
  socket.write(
    [
      `HTTP/1.1 ${statusCode} ${message}`,
      "Connection: close",
      "Content-Length: 0",
      "",
      "",
    ].join("\r\n")
  );
  socket.destroy();
}

const server = http.createServer((req, res) => {
  if (req.url === "/favicon.ico") {
    res.writeHead(204);
    res.end();
    return;
  }
  if (req.url === "/status") {
    res.writeHead(200, {
      "content-type": "application/json",
    });
    res.end(latest || JSON.stringify({ status: "waiting for adapter" }));
    return;
  }
  serveStatic(req, res);
});

server.on("upgrade", (req, socket) => {
  if (req.url !== "/gcadapter") {
    socket.destroy();
    return;
  }
  if (!isAllowedOrigin(req)) {
    rejectUpgrade(socket, 403, "Forbidden");
    return;
  }
  const key = req.headers["sec-websocket-key"];
  if (!key) {
    rejectUpgrade(socket, 400, "Bad Request");
    return;
  }
  socket.write(
    [
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${acceptKey(key)}`,
      "",
      "",
    ].join("\r\n")
  );
  clients.add(socket);
  socket.on("close", () => clients.delete(socket));
  socket.on("error", () => clients.delete(socket));
  socket.on("data", () => {});
  if (latest) {
    sendFrame(socket, latest);
  }
});

checkAssets();
compileStream();
startStream();
server.on("error", (error) => {
  if (error.code === "EADDRINUSE") {
    console.error(`Viewer server could not bind ${host}:${port}: address already in use.`);
    console.error("Stop the existing server or choose another port with VIEWER_PORT=... / make VIEWER_PORT=...");
    if (streamProcess) {
      streamProcess.kill("SIGTERM");
    }
    process.exit(1);
  }
  throw error;
});
server.listen(port, host, () => {
  const browserHost = host === "0.0.0.0" ? "127.0.0.1" : host;
  const url = `http://${browserHost}:${port}${urlPath}`;
  const listenUrl = `http://${host}:${port}${urlPath}`;
  console.log(`MSL viewer: ${url}`);
  if (listenUrl !== url) {
    console.log(`Listening: ${listenUrl}`);
  }
  console.log(`GameCube adapter bridge: ws://${browserHost}:${port}/gcadapter`);
  console.log("Press Ctrl+C to stop.");
  if (shouldOpenBrowser) {
    const opener = process.platform === "darwin" ? "open" : "xdg-open";
    const child = spawn(opener, [url], {
      stdio: "ignore",
      detached: true,
    });
    child.on("error", () => {
      console.log(`Open ${url}`);
    });
    child.unref();
  }
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    server.close();
    if (streamProcess) {
      streamProcess.kill("SIGTERM");
    }
    process.exit(0);
  });
}
