#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";

const HIDRAW_ROOT = "/sys/class/hidraw";
const VENDOR = 0x057e;
const PRODUCT = 0x0337;
const READ_BYTES = 64;
const RUN_MS = 12000;

function parseHidId(text) {
  const match = text.match(/^HID_ID=([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9a-fA-F]+)/m);
  if (!match) return null;
  return {
    bus: Number.parseInt(match[1], 16),
    vendor: Number.parseInt(match[2], 16),
    product: Number.parseInt(match[3], 16),
  };
}

function findAdapterNode() {
  for (const entry of fs.readdirSync(HIDRAW_ROOT)) {
    const ueventPath = path.join(HIDRAW_ROOT, entry, "device", "uevent");
    let uevent = "";
    try {
      uevent = fs.readFileSync(ueventPath, "utf8");
    } catch {
      continue;
    }
    const id = parseHidId(uevent);
    if (id?.vendor === VENDOR && id?.product === PRODUCT) {
      const name = uevent.match(/^HID_NAME=(.*)$/m)?.[1] || "unknown";
      return { path: `/dev/${entry}`, name, uevent };
    }
  }
  return null;
}

function hex(buffer, n) {
  return Array.from(buffer.subarray(0, n), (byte) => byte.toString(16).padStart(2, "0")).join(" ");
}

const adapter = findAdapterNode();
if (!adapter) {
  console.error("No hidraw node found for 057e:0337.");
  console.error("Run: tools/viewer/live/linux_webhid_setup.sh");
  console.error("If it says driver: none, run: tools/viewer/live/linux_webhid_setup.sh --bind");
  process.exit(1);
}

console.log(`Using ${adapter.path}: ${adapter.name}`);
console.log("Move sticks and press buttons for 12 seconds.");

const fd = fs.openSync(adapter.path, fs.constants.O_RDWR | fs.constants.O_NONBLOCK);

for (const init of [Buffer.from([0x13, 0x00]), Buffer.from([0x13])]) {
  try {
    const written = fs.writeSync(fd, init);
    console.log(`init write ${hex(init, init.length)} (${written} bytes)`);
  } catch (error) {
    console.log(`init write ${hex(init, init.length)} failed: ${error.code || error.message}`);
  }
}

const buf = Buffer.alloc(READ_BYTES);
const startedAt = Date.now();
let readCount = 0;
let lastHex = "";

function readLoop() {
  try {
    const n = fs.readSync(fd, buf, 0, buf.length, null);
    if (n > 0) {
      const line = hex(buf, n);
      if (line !== lastHex || readCount < 5) {
        console.log(`${String(readCount).padStart(4, "0")} ${n} bytes: ${line}`);
        lastHex = line;
      }
      readCount += 1;
    }
  } catch (error) {
    if (error.code !== "EAGAIN" && error.code !== "EWOULDBLOCK") {
      console.error(`read failed: ${error.code || error.message}`);
      process.exit(1);
    }
  }

  if (Date.now() - startedAt >= RUN_MS) {
    console.log(`done, reports read: ${readCount}`);
    fs.closeSync(fd);
    return;
  }
  setTimeout(readLoop, 2);
}

readLoop();
