import { BUTTONS } from "./schema.js";

const NINTENDO_VENDOR_ID = 0x057e;
const WII_U_GC_ADAPTER_PRODUCT_ID = 0x0337;
const INPUT_REPORT_ID = 0x21;
const START_REPORT_ID = 0x13;
const PORT_COUNT = 4;
const PORT_BYTES = 9;
const STALE_PACKET_MS = 500;
const BRIDGE_CONNECT_TIMEOUT_MS = 900;
const GAMEPAD_ACTIVITY_EPSILON = 0.08;

const neutral = Object.freeze({
  buttons: 0,
  mainX: 0,
  mainY: 0,
  cX: 0,
  cY: 0,
  l: 0,
  r: 0,
});

function makeController() {
  return {
    buttons: 0,
    mainX: 0,
    mainY: 0,
    cX: 0,
    cY: 0,
    l: 0,
    r: 0,
  };
}

function clearController(controller) {
  controller.buttons = 0;
  controller.mainX = 0;
  controller.mainY = 0;
  controller.cX = 0;
  controller.cY = 0;
  controller.l = 0;
  controller.r = 0;
}

function clamp(value, lo, hi) {
  return Math.max(lo, Math.min(hi, value));
}

function centeredByteToStick(value) {
  return clamp((value - 128) / 80, -1, 1);
}

function byteToTrigger(value) {
  return clamp(value / 255, 0, 1);
}

function deviceName(device) {
  return device.productName || `USB ${device.vendorId.toString(16)}:${device.productId.toString(16)}`;
}

export class GameCubeAdapterInput {
  constructor({ port = 0, onStatus = () => {} } = {}) {
    this.port = port;
    this.onStatus = onStatus;
    this.device = null;
    this.transport = "";
    this.controllers = Array.from({ length: PORT_COUNT }, makeController);
    this.connectedPorts = new Array(PORT_COUNT).fill(false);
    this.packetCount = 0;
    this.lastPacketAt = 0;
    this.lastError = "";
    this.socket = null;
    this.lastGamepadSummary = "";
    this.selectedGamepadIndex = port;
    this.boundInputReport = (event) => this.#handleInputReport(event);
    this.boundGamepadConnected = () => this.#handleGamepadChange();
    this.boundGamepadDisconnected = () => this.#handleGamepadChange();
    this.gamepadEventsInstalled = false;
  }

  get supported() {
    return "WebSocket" in window || "getGamepads" in navigator || "hid" in navigator;
  }

  get available() {
    return this.transport === "Bridge" || this.transport === "Gamepad" || Boolean(this.device?.opened);
  }

  get active() {
    if (this.transport === "Bridge") {
      return (
        this.connectedPorts[this.port] &&
        performance.now() - this.lastPacketAt < STALE_PACKET_MS
      );
    }
    if (this.transport === "Gamepad") {
      this.#pollGamepads();
      return this.connectedPorts[this.port];
    }
    return (
      this.available &&
      this.connectedPorts[this.port] &&
      performance.now() - this.lastPacketAt < STALE_PACKET_MS
    );
  }

  setPort(port) {
    this.port = clamp(Number(port) || 0, 0, PORT_COUNT - 1);
    this.selectedGamepadIndex = this.port;
    this.#emitStatus();
  }

  transportLabel() {
    if (this.transport === "Gamepad") return "gamepad";
    if (this.transport === "Bridge") return "bridge controller";
    if (this.transport) return this.transport;
    return "controller";
  }

  readController() {
    if (!this.active) {
      return neutral;
    }
    return this.controllers[this.port];
  }

  startGamepadPolling() {
    if (!("getGamepads" in navigator)) {
      this.#emitStatus();
      return false;
    }
    this.#installGamepadEvents();
    this.transport = "Gamepad";
    this.device = null;
    this.lastError = "";
    const found = this.#pollGamepads();
    this.#emitStatus();
    return found;
  }

  inputSummary() {
    const controller = this.controllers[this.port] || neutral;
    return [
      `buttons=0x${(controller.buttons || 0).toString(16).padStart(4, "0")}`,
      `main=(${controller.mainX.toFixed(2)},${controller.mainY.toFixed(2)})`,
      `c=(${controller.cX.toFixed(2)},${controller.cY.toFixed(2)})`,
      `lr=(${controller.l.toFixed(2)},${controller.r.toFixed(2)})`,
    ].join(" ");
  }

  #gamepadHasActivity(gamepad) {
    if (!gamepad) {
      return false;
    }
    return (
      gamepad.buttons.some((button) => button.pressed || button.value > 0.25) ||
      gamepad.axes.some((axis) => Math.abs(axis) > GAMEPAD_ACTIVITY_EPSILON)
    );
  }

  async connect() {
    if (!this.supported) {
      throw new Error("Gamepad/WebHID APIs are not available in this browser. Use Chromium/Chrome on localhost.");
    }
    if (!window.isSecureContext) {
      throw new Error("Adapter access requires a secure context. localhost is OK; file:// is not.");
    }

    if ("getGamepads" in navigator) {
      if (this.#connectGamepad({ requirePad: true })) {
        return this;
      }
      this.lastError = "no browser gamepad found; press any controller button if using XInput mode";
      this.#emitStatus();
    }

    if ("WebSocket" in window) {
      try {
        await this.#connectBridge();
        return this;
      } catch (error) {
        this.lastError = `local bridge unavailable: ${error.message}`;
        this.#emitStatus();
      }
    }

    if ("getGamepads" in navigator) {
      this.#connectGamepad({ requirePad: false });
      this.#emitStatus();
      return this;
    }

    if (!("hid" in navigator)) {
      throw new Error(this.lastError || "WebHID is not available in this browser.");
    }
    try {
      await this.#connectWebHid();
    } catch (error) {
      if (this.lastError) {
        throw new Error(`${this.lastError}; WebHID failed: ${error.message}`);
      }
      throw error;
    }
    return this;
  }

  #connectGamepad({ requirePad }) {
    this.#installGamepadEvents();
    this.transport = "Gamepad";
    this.device = null;
    this.lastError = "";
    const found = this.#pollGamepads();
    if (requirePad && !found) {
      return false;
    }
    return true;
  }

  #installGamepadEvents() {
    if (this.gamepadEventsInstalled) {
      return;
    }
    window.addEventListener("gamepadconnected", this.boundGamepadConnected);
    window.addEventListener("gamepaddisconnected", this.boundGamepadDisconnected);
    this.gamepadEventsInstalled = true;
  }

  #handleGamepadChange() {
    if (this.transport !== "Gamepad") {
      this.transport = "Gamepad";
      this.device = null;
    }
    this.#pollGamepads();
    this.#emitStatus();
  }

  async #connectBridge() {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }

    await new Promise((resolve, reject) => {
      const bridgeUrl = new URL("/gcadapter", window.location.href);
      bridgeUrl.protocol = bridgeUrl.protocol === "https:" ? "wss:" : "ws:";
      const socket = new WebSocket(bridgeUrl);
      let settled = false;
      const timeout = window.setTimeout(() => {
        if (settled) return;
        settled = true;
        socket.close();
        reject(new Error(`timed out connecting to ${bridgeUrl.href}`));
      }, BRIDGE_CONNECT_TIMEOUT_MS);

      socket.addEventListener("open", () => {
        this.socket = socket;
        this.transport = "Bridge";
      });
      socket.addEventListener("message", (event) => {
        try {
          this.#handleBridgeState(JSON.parse(event.data));
          if (!settled) {
            settled = true;
            window.clearTimeout(timeout);
            resolve();
          }
        } catch (error) {
          this.lastError = `bad bridge packet: ${error.message}`;
          this.#emitStatus();
        }
      });
      socket.addEventListener("error", () => {
        if (settled) return;
        settled = true;
        window.clearTimeout(timeout);
        reject(new Error("connection failed"));
      });
      socket.addEventListener("close", () => {
        if (this.socket === socket) {
          this.socket = null;
        }
        if (settled) {
          this.lastError = "local bridge disconnected";
          this.#emitStatus();
          return;
        }
        settled = true;
        window.clearTimeout(timeout);
        reject(new Error("connection closed"));
      });
    });

    this.lastError = "";
    this.#emitStatus();
  }

  async #connectWebHid() {
    const filters = [{ vendorId: NINTENDO_VENDOR_ID, productId: WII_U_GC_ADAPTER_PRODUCT_ID }];
    const granted = await navigator.hid.getDevices();
    let device = granted.find(
      (candidate) =>
        candidate.vendorId === NINTENDO_VENDOR_ID &&
        candidate.productId === WII_U_GC_ADAPTER_PRODUCT_ID
    );

    if (!device) {
      let selected = [];
      try {
        selected = await navigator.hid.requestDevice({ filters });
      } catch (error) {
        if (error.name === "NotFoundError") {
          throw new Error(
            "No compatible HID devices found. On Linux, make sure the adapter has a hidraw node; run tools/webplay/linux_webhid_setup.sh to diagnose."
          );
        }
        throw error;
      }
      device = selected[0] || null;
    }
    if (!device) {
      throw new Error(
        "No GameCube adapter selected. On Linux, make sure the adapter has a hidraw node; run tools/webplay/linux_webhid_setup.sh to diagnose."
      );
    }

    if (this.device && this.device !== device) {
      this.device.removeEventListener("inputreport", this.boundInputReport);
    }

    this.device = device;
    this.transport = "WebHID";
    this.device.addEventListener("inputreport", this.boundInputReport);
    if (!this.device.opened) {
      await this.device.open();
    }

    await this.#startAdapter();
    this.lastError = "";
    this.#emitStatus();
  }

  async #startAdapter() {
    // Official/Wii U mode adapters start interrupt input reports after report 0x13.
    // The HID descriptor exposes report 0x13 as a one-byte output report.
    await this.device.sendReport(START_REPORT_ID, new Uint8Array([0]));
  }

  #handleInputReport(event) {
    if (event.reportId !== INPUT_REPORT_ID) {
      return;
    }
    this.#handleReportData(event.data);
  }

  #pollGamepads() {
    const pads = Array.from(navigator.getGamepads?.() || []).filter(Boolean);
    if (this.transport === "Gamepad") {
      const selected = pads[this.selectedGamepadIndex] || null;
      if (!selected || !this.#gamepadHasActivity(selected)) {
        const activeIndex = pads.findIndex((pad) => this.#gamepadHasActivity(pad));
        if (activeIndex >= 0) {
          this.selectedGamepadIndex = activeIndex;
        } else if (!selected && pads.length > 0) {
          this.selectedGamepadIndex = 0;
        }
      }
    }
    for (let port = 0; port < PORT_COUNT; port += 1) {
      const padIndex = this.transport === "Gamepad" && port === this.port
        ? this.selectedGamepadIndex
        : port;
      const pad = pads[padIndex] || null;
      this.connectedPorts[port] = Boolean(pad);
      if (pad) {
        this.#parseGamepad(this.controllers[port], pad);
        if (port === this.port) {
          const pressed = pad.buttons
            .map((button, index) => (button.pressed || button.value > 0.25 ? `${index}:${button.value.toFixed(2)}` : null))
            .filter(Boolean)
            .join(",");
          const axes = Array.from(pad.axes, (axis) => axis.toFixed(2)).join(",");
          this.lastGamepadSummary = `axes=[${axes}] buttons=[${pressed || "none"}]`;
        }
      } else {
        clearController(this.controllers[port]);
        if (port === this.port) {
          this.lastGamepadSummary = "";
        }
      }
    }
    this.packetCount += 1;
    this.lastPacketAt = performance.now();
    if ((this.packetCount % 60) === 1) {
      this.#emitStatus();
    }
    return pads.length > 0;
  }

  #handleBridgeState(state) {
    if (state.error) {
      this.lastError = state.error;
      this.#emitStatus();
      return;
    }
    if (!Array.isArray(state.ports)) {
      throw new Error("missing ports array");
    }
    for (let port = 0; port < PORT_COUNT; port += 1) {
      const source = state.ports[port];
      this.connectedPorts[port] = Boolean(source?.connected);
      if (source?.connected) {
        const controller = this.controllers[port];
        controller.buttons = source.buttons & 0xffff;
        controller.mainX = centeredByteToStick(source.mainX);
        controller.mainY = centeredByteToStick(source.mainY);
        controller.cX = centeredByteToStick(source.cX);
        controller.cY = centeredByteToStick(source.cY);
        controller.l = byteToTrigger(source.l);
        controller.r = byteToTrigger(source.r);
      } else {
        clearController(this.controllers[port]);
      }
    }
    this.packetCount = state.seq ?? (this.packetCount + 1);
    this.lastPacketAt = performance.now();
    if ((this.packetCount % 60) === 1) {
      this.#emitStatus();
    }
  }

  #handleReportData(data) {
    const firstPortOffset = data.byteLength >= PORT_COUNT * PORT_BYTES + 1 ? 1 : 0;
    if (data.byteLength < firstPortOffset + PORT_COUNT * PORT_BYTES) {
      this.lastError = `short adapter report (${data.byteLength} bytes)`;
      this.#emitStatus();
      return;
    }

    for (let port = 0; port < PORT_COUNT; port += 1) {
      const off = firstPortOffset + port * PORT_BYTES;
      const status = data.getUint8(off);
      const buttonLo = data.getUint8(off + 1);
      const buttonHi = data.getUint8(off + 2);
      const connected = (status & 0xf0) !== 0;
      this.connectedPorts[port] = connected;
      if (connected) {
        this.#parseController(this.controllers[port], buttonLo, buttonHi, data, off);
      } else {
        clearController(this.controllers[port]);
      }
    }

    this.packetCount += 1;
    this.lastPacketAt = performance.now();
    if ((this.packetCount % 60) === 1) {
      this.#emitStatus();
    }
  }

  #parseController(controller, buttonLo, buttonHi, data, off) {
    let buttons = 0;
    if (buttonLo & 0x01) buttons |= BUTTONS.A;
    if (buttonLo & 0x02) buttons |= BUTTONS.B;
    if (buttonLo & 0x04) buttons |= BUTTONS.X;
    if (buttonLo & 0x08) buttons |= BUTTONS.Y;
    if (buttonLo & 0x10) buttons |= BUTTONS.D_LEFT;
    if (buttonLo & 0x20) buttons |= BUTTONS.D_RIGHT;
    if (buttonLo & 0x40) buttons |= BUTTONS.D_DOWN;
    if (buttonLo & 0x80) buttons |= BUTTONS.D_UP;
    if (buttonHi & 0x01) buttons |= BUTTONS.START;
    if (buttonHi & 0x02) buttons |= BUTTONS.Z;
    if (buttonHi & 0x04) buttons |= BUTTONS.R;
    if (buttonHi & 0x08) buttons |= BUTTONS.L;

    controller.buttons = buttons;
    controller.mainX = centeredByteToStick(data.getUint8(off + 3));
    controller.mainY = centeredByteToStick(data.getUint8(off + 4));
    controller.cX = centeredByteToStick(data.getUint8(off + 5));
    controller.cY = centeredByteToStick(data.getUint8(off + 6));
    controller.l = byteToTrigger(data.getUint8(off + 7));
    controller.r = byteToTrigger(data.getUint8(off + 8));
  }

  #parseGamepad(controller, gamepad) {
    let buttons = 0;
    const pressed = (index) => Boolean(gamepad.buttons[index]?.pressed);
    const value = (index) => gamepad.buttons[index]?.value || 0;

    if (pressed(0)) buttons |= BUTTONS.A;
    if (pressed(1)) buttons |= BUTTONS.B;
    if (pressed(2)) buttons |= BUTTONS.X;
    if (pressed(3)) buttons |= BUTTONS.Y;
    if (pressed(4) || value(6) > 0.25) buttons |= BUTTONS.L;
    if (value(7) > 0.25) buttons |= BUTTONS.R;
    if (pressed(5) || pressed(8)) buttons |= BUTTONS.Z;
    if (pressed(9)) buttons |= BUTTONS.START;
    if (pressed(12)) buttons |= BUTTONS.D_UP;
    if (pressed(13)) buttons |= BUTTONS.D_DOWN;
    if (pressed(14)) buttons |= BUTTONS.D_LEFT;
    if (pressed(15)) buttons |= BUTTONS.D_RIGHT;

    controller.buttons = buttons;
    controller.mainX = clamp(gamepad.axes[0] || 0, -1, 1);
    controller.mainY = clamp(-(gamepad.axes[1] || 0), -1, 1);
    controller.cX = clamp(gamepad.axes[2] || 0, -1, 1);
    controller.cY = clamp(-(gamepad.axes[3] || 0), -1, 1);
    controller.l = clamp(Math.max(value(6), pressed(4) ? 1 : 0), 0, 1);
    controller.r = clamp(value(7), 0, 1);
  }

  #emitStatus() {
    if (!this.supported) {
      this.onStatus("GameCube adapter: Gamepad/WebHID unavailable.");
      return;
    }
    if (this.transport === "Bridge") {
      const portLabel = `P${this.port + 1}`;
      const packetText = this.packetCount > 0 ? `${this.packetCount} packets` : "waiting for bridge";
      const controllerText = this.connectedPorts[this.port] ? "controller present" : "no controller";
      const errorText = this.lastError ? ` (${this.lastError})` : "";
      this.onStatus(`GameCube adapter: local bridge, ${portLabel}, ${controllerText}, ${packetText}.${errorText}`);
      return;
    }
    if (this.transport === "Gamepad") {
      const pads = Array.from(navigator.getGamepads?.() || []).filter(Boolean);
      const selected = pads[this.selectedGamepadIndex];
      const portLabel = `P${this.port + 1}`;
      const controllerText = selected
        ? `${selected.id} [gamepad ${this.selectedGamepadIndex}]`
        : "no browser gamepad; for XInput mode, press any controller button";
      const packetText = this.packetCount > 0 ? `${this.packetCount} polls` : "waiting for gamepad";
      const errorText = this.lastError ? ` (${this.lastError})` : "";
      const inputText = selected ? ` ${this.inputSummary()} raw ${this.lastGamepadSummary}.` : "";
      this.onStatus(`GameCube adapter: Gamepad API, ${portLabel}, ${controllerText}, ${packetText}.${inputText}${errorText}`);
      return;
    }
    if (!this.device) {
      this.onStatus("GameCube adapter: not connected.");
      return;
    }

    const portLabel = `P${this.port + 1}`;
    const packetText = this.packetCount > 0 ? `${this.packetCount} reports` : "waiting for reports";
    const controllerText = this.connectedPorts[this.port] ? "controller present" : "no controller";
    const errorText = this.lastError ? ` (${this.lastError})` : "";
    this.onStatus(
      `GameCube adapter: ${this.transport || "connected"}, ${deviceName(this.device)}, ${portLabel}, ${controllerText}, ${packetText}.${errorText}`
    );
  }
}
