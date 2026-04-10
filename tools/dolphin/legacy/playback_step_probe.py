#!/usr/bin/env python3
from __future__ import annotations

"""
Playback-step probe (live Dolphin memory reads while replay playback runs).

WARNING:
- Run from an isolated terminal session (e.g. `tmux`) and prefer a hard timeout.
- If Dolphin wedges (or the hook blocks), this script can appear "hung" while spinning.

Recommended usage pattern:
  timeout 60s uv run python scripts/playback_step_probe.py ...
Set `SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING=1` to suppress the runtime warning banner.
"""

import argparse
import atexit
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import enet
import numpy as np
import peppi_bytes
from melee_sim.metrics import MASK_F32, canonicalize_slippi_sample_last
from melee_sim.replay_io import read_replay_bytes


def _u32_to_i32(u: int) -> int:
    u &= 0xFFFF_FFFF
    return struct.unpack("<i", struct.pack("<I", u))[0]


def _f32_at(dme, addr: int) -> float:
    bits = int(dme.read_word(addr)) & 0xFFFF_FFFF
    return struct.unpack(">f", struct.pack(">I", bits))[0]


def _u32_at(dme, addr: int) -> int:
    return int(dme.read_word(addr)) & 0xFFFF_FFFF


def _u8_at(dme, addr: int) -> int:
    return int(dme.read_byte(addr)) & 0xFF


def _f32_bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def _as_unit_f32(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, -1.0, 1.0).astype(np.float32, copy=False)


def _as_trigger(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, 0.0, 1.0).astype(np.float32, copy=False)


def _write_dolphin_ini(user_dir: Path, *, spectator_port: int, emu_speed: float | None) -> None:
    cfg_dir = user_dir / "Config"
    cfg_dir.mkdir(parents=True, exist_ok=True)
    ini_path = cfg_dir / "Dolphin.ini"
    lines = []
    if ini_path.exists():
        lines = ini_path.read_text().splitlines()
    # Minimal INI with Core section
    if not lines:
        lines = ["[Core]"]
    if "[Core]" not in lines:
        lines.append("[Core]")
    # Ensure Core section has needed keys.
    out = []
    in_core = False
    seen = set()
    for ln in lines:
        if ln.strip().startswith("["):
            if in_core:
                for k, v in (
                    ("SlippiEnableSpectator", "True"),
                    ("SlippiSpectatorLocalPort", str(spectator_port)),
                    ("SlippiOnlineDelay", "0"),
                ):
                    if k not in seen:
                        out.append(f"{k} = {v}")
                in_core = False
                seen.clear()
            out.append(ln)
            in_core = ln.strip() == "[Core]"
            continue
        if in_core and "=" in ln:
            k = ln.split("=")[0].strip()
            if k in (
                "SlippiEnableSpectator",
                "SlippiSpectatorLocalPort",
                "SlippiOnlineDelay",
                "GFXBackend",
                "EmulationSpeed",
            ):
                seen.add(k)
                if k == "SlippiEnableSpectator":
                    out.append("SlippiEnableSpectator = True")
                elif k == "SlippiSpectatorLocalPort":
                    out.append(f"SlippiSpectatorLocalPort = {spectator_port}")
                elif k == "SlippiOnlineDelay":
                    out.append("SlippiOnlineDelay = 0")
                elif k == "GFXBackend":
                    out.append("GFXBackend = Null")
                elif k == "EmulationSpeed":
                    if emu_speed is not None:
                        out.append(f"EmulationSpeed = {emu_speed:.3f}")
                    else:
                        out.append(ln)
                continue
        out.append(ln)
    if in_core:
        for k, v in (
            ("SlippiEnableSpectator", "True"),
            ("SlippiSpectatorLocalPort", str(spectator_port)),
            ("SlippiOnlineDelay", "0"),
            ("GFXBackend", "Null"),
        ):
            if k not in seen:
                out.append(f"{k} = {v}")
        if emu_speed is not None and "EmulationSpeed" not in seen:
            out.append(f"EmulationSpeed = {emu_speed:.3f}")
    # Ensure DSP section exists with NullSound backend to avoid audio init in headless mode.
    if "[DSP]" not in out:
        out.append("[DSP]")
        out.append("Backend = NullSound")
    else:
        dsp_out = []
        in_dsp = False
        dsp_seen = False
        for ln in out:
            if ln.strip().startswith("["):
                if in_dsp and not dsp_seen:
                    dsp_out.append("Backend = NullSound")
                in_dsp = ln.strip() == "[DSP]"
                dsp_out.append(ln)
                continue
            if in_dsp and "=" in ln:
                k = ln.split("=")[0].strip()
                if k == "Backend":
                    dsp_seen = True
                    dsp_out.append("Backend = NullSound")
                    continue
            dsp_out.append(ln)
        if in_dsp and not dsp_seen:
            dsp_out.append("Backend = NullSound")
        out = dsp_out
    ini_path.write_text("\n".join(out) + "\n")


def _write_playback_txt(
    user_dir: Path,
    *,
    replay: Path,
    start_frame: int,
    end_frame: int,
    real_time: bool,
    block_on_frame: bool,
    should_resync: bool,
) -> Path:
    slippi_dir = user_dir / "Slippi"
    slippi_dir.mkdir(parents=True, exist_ok=True)
    playback_path = slippi_dir / "playback.txt"
    payload = {
        "mode": "normal",
        "replay": str(replay.resolve()),
        "startFrame": int(start_frame),
        "endFrame": int(end_frame),
        "commandId": str(int(time.time() * 1000)),
        "isRealTimeMode": bool(real_time),
        "shouldResync": bool(should_resync),
        "rollbackDisplayMethod": "off",
        "blockOnFrame": bool(block_on_frame),
    }
    playback_path.write_text(json.dumps(payload))
    return playback_path


def _ensure_hookable_dolphin_bin(dolphin: Path, *, user_dir: Path) -> Path:
    """Return a Dolphin binary path that dolphin_memory_engine can reliably hook.

    On Linux, dolphin_memory_engine's process detection has historically keyed off the
    executable name `dolphin-emu`. Our playback Dolphin is often named `dolphin-emu-nogui`,
    which can cause hooks to fail even though the binary is correct.

    To avoid requiring callers to keep a special symlink around, we create a local symlink
    named `dolphin-emu` under the provided user_dir and launch via that path.
    """

    dolphin = dolphin.resolve()
    if dolphin.name == "dolphin-emu":
        return dolphin

    # IMPORTANT: do *not* call `.resolve()` on the output path. If the symlink already exists,
    # `.resolve()` would follow it and point at the target binary, which is not what we want to
    # unlink/recreate.
    out = user_dir / "dolphin-emu"
    if out.exists() or out.is_symlink():
        out.unlink()
    out.symlink_to(dolphin)
    return out


class _Stepper:
    def __init__(self, address: str, port: int) -> None:
        self._addr = address
        self._port = port
        self._host: enet.Host | None = None
        self._peer: enet.Peer | None = None

    def connect(self, timeout_ms: int = 1000) -> None:
        if self._host is not None:
            return
        self._host = enet.Host(None, 1, 0, 0)
        self._peer = self._host.connect(enet.Address(bytes(self._addr, "utf-8"), self._port), 1)
        connected = False
        for _ in range(10):
            event = self._host.service(timeout_ms)
            if event.type == enet.EVENT_TYPE_CONNECT:
                connected = True
                break
        if not connected:
            raise RuntimeError(f"failed to connect to spectator server {self._addr}:{self._port}")

    def send_step(self, frames: int, timeout_ms: int = 2000, target_frame: int | None = None) -> dict | None:
        if self._host is None or self._peer is None:
            self.connect()
        payload = {"type": "playback_step", "frames": int(frames)}
        if target_frame is not None:
            payload["targetFrame"] = int(target_frame)
        self._peer.send(0, enet.Packet(json.dumps(payload).encode("utf-8")))
        self._host.flush()
        t0 = time.monotonic()
        while (time.monotonic() - t0) * 1000.0 < timeout_ms:
            event = self._host.service(50)
            if event.type == enet.EVENT_TYPE_RECEIVE:
                try:
                    msg = json.loads(event.packet.data.decode("utf-8"))
                except Exception:
                    continue
                if msg.get("type") == "playback_step_ack":
                    return msg
        return None

    def send_ack(self, frame: int) -> None:
        if self._host is None or self._peer is None:
            self.connect()
        payload = {"type": "frame_ack", "frame": int(frame)}
        self._peer.send(0, enet.Packet(json.dumps(payload).encode("utf-8")))
        self._host.flush()

    def close(self) -> None:
        if self._peer is not None:
            try:
                self._peer.disconnect()
            except Exception:
                pass
        if self._host is not None:
            try:
                self._host.flush()
            except Exception:
                pass
        self._peer = None
        self._host = None


def _jobj_preorder_ptrs(dme, root_ptr: int, max_nodes: int = 256) -> list[int]:
    """Pre-order traversal following HSD_JObj child/next pointers.

    HSD_JObj layout (decomp): `refs/melee/src/sysdolphin/baselib/jobj.h`.
    """
    if not root_ptr:
        return []
    out: list[int] = []
    stack: list[int] = [int(root_ptr)]
    while stack and len(out) < max_nodes:
        ptr = int(stack.pop())
        if not ptr:
            continue
        out.append(ptr)
        # Child pointer at +0x10, next pointer at +0x08.
        child = int(_u32_at(dme, ptr + 0x10))
        nxt = int(_u32_at(dme, ptr + 0x08))
        if nxt:
            stack.append(nxt)
        if child:
            stack.append(child)
    return out


def _read_jobj_state(dme, jobj_ptr: int) -> dict | None:
    if not jobj_ptr:
        return None
    jobj_ptr = int(jobj_ptr) & 0xFFFF_FFFF
    parent_ptr = int(_u32_at(dme, jobj_ptr + 0x0C))
    flags = int(_u32_at(dme, jobj_ptr + 0x14))
    aobj_ptr = int(_u32_at(dme, jobj_ptr + 0x7C))
    aobj_hsd_obj = 0
    aobj_flags = None
    aobj_curr_frame = None
    aobj_curr_frame_bits = None
    aobj_rewind_frame = None
    aobj_rewind_frame_bits = None
    aobj_end_frame = None
    aobj_end_frame_bits = None
    aobj_framerate = None
    aobj_framerate_bits = None
    if aobj_ptr:
        # HSD_AObj::hsd_obj is at +0x18 (see sysdolphin/baselib/aobj.h).
        aobj_hsd_obj = int(_u32_at(dme, aobj_ptr + 0x18))
        # HSD_AObj layout (sysdolphin/baselib/aobj.h):
        #   +0x00 u32 flags
        #   +0x04 f32 curr_frame
        #   +0x08 f32 rewind_frame
        #   +0x0C f32 end_frame
        #   +0x10 f32 framerate
        aobj_flags = int(_u32_at(dme, aobj_ptr + 0x00))
        aobj_curr_frame_bits = int(_u32_at(dme, aobj_ptr + 0x04))
        aobj_rewind_frame_bits = int(_u32_at(dme, aobj_ptr + 0x08))
        aobj_end_frame_bits = int(_u32_at(dme, aobj_ptr + 0x0C))
        aobj_framerate_bits = int(_u32_at(dme, aobj_ptr + 0x10))
        aobj_curr_frame = float(_f32_at(dme, aobj_ptr + 0x04))
        aobj_rewind_frame = float(_f32_at(dme, aobj_ptr + 0x08))
        aobj_end_frame = float(_f32_at(dme, aobj_ptr + 0x0C))
        aobj_framerate = float(_f32_at(dme, aobj_ptr + 0x10))
    mtx_base = jobj_ptr + 0x44
    rot_base = jobj_ptr + 0x1C
    scl_base = jobj_ptr + 0x2C
    trn_base = jobj_ptr + 0x38
    mtx_bits = [int(_u32_at(dme, mtx_base + i * 4)) for i in range(12)]
    rot_bits = [int(_u32_at(dme, rot_base + i * 4)) for i in range(3)]
    quat_bits = [int(_u32_at(dme, rot_base + i * 4)) for i in range(4)]
    scale_bits = [int(_u32_at(dme, scl_base + i * 4)) for i in range(3)]
    trn_bits = [int(_u32_at(dme, trn_base + i * 4)) for i in range(3)]
    return {
        "mtx": [float(_f32_at(dme, mtx_base + i * 4)) for i in range(12)],
        "rot": (
            float(_f32_at(dme, rot_base + 0x00)),
            float(_f32_at(dme, rot_base + 0x04)),
            float(_f32_at(dme, rot_base + 0x08)),
        ),
        "quat": (
            float(_f32_at(dme, rot_base + 0x00)),
            float(_f32_at(dme, rot_base + 0x04)),
            float(_f32_at(dme, rot_base + 0x08)),
            float(_f32_at(dme, rot_base + 0x0C)),
        ),
        "scale": (
            float(_f32_at(dme, scl_base + 0x00)),
            float(_f32_at(dme, scl_base + 0x04)),
            float(_f32_at(dme, scl_base + 0x08)),
        ),
        "translate": (
            float(_f32_at(dme, trn_base + 0x00)),
            float(_f32_at(dme, trn_base + 0x04)),
            float(_f32_at(dme, trn_base + 0x08)),
        ),
        "mtx_bits": mtx_bits,
        "rot_bits": rot_bits,
        "quat_bits": quat_bits,
        "scale_bits": scale_bits,
        "translate_bits": trn_bits,
        "flags": flags,
        "ptr": int(jobj_ptr),
        "aobj_ptr": aobj_ptr,
        "aobj_hsd_obj": aobj_hsd_obj,
        "aobj_flags": aobj_flags,
        "aobj_curr_frame": aobj_curr_frame,
        "aobj_curr_frame_bits": aobj_curr_frame_bits,
        "aobj_rewind_frame": aobj_rewind_frame,
        "aobj_rewind_frame_bits": aobj_rewind_frame_bits,
        "aobj_end_frame": aobj_end_frame,
        "aobj_end_frame_bits": aobj_end_frame_bits,
        "aobj_framerate": aobj_framerate,
        "aobj_framerate_bits": aobj_framerate_bits,
        "parent_ptr": parent_ptr,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Probe playback step mode with memory reads.")
    ap.add_argument("--replay", required=True)
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--dolphin", required=True, help="path to playback dolphin-emu")
    ap.add_argument("--user-dir", default=str(Path("/tmp/ish_playback_user")))
    ap.add_argument(
        "--max-seconds",
        "--max_seconds",
        dest="max_seconds",
        type=float,
        default=None,
        help="Hard runtime limit for this probe. Strongly recommended to avoid wedging your terminal.",
    )
    ap.add_argument(
        "--i-accept-risk",
        action="store_true",
        help="Bypass safety guardrails (still recommended to use --max-seconds or external `timeout`).",
    )
    ap.add_argument("--start-frame", type=int, default=None)
    ap.add_argument("--end-frame", type=int, default=None)
    ap.add_argument("--frames", type=int, default=5, help="number of frames to step (0 = full replay)")
    ap.add_argument("--spectator-port", type=int, default=51441)
    ap.add_argument("--out", type=str, default=None, help="optional JSON output for per-frame probe rows")
    ap.add_argument("--out-bin", type=str, default=None, help="optional binary engine-dump output path")
    ap.add_argument("--print-every", type=int, default=1, help="print every N frames (0 to disable)")
    ap.add_argument("--debug-wait", action="store_true", help="log wait loop status while reaching start frame")
    ap.add_argument("--continuous", action="store_true", help="run playback continuously (no stepping)")
    ap.add_argument(
        "--block-on-frame",
        action="store_true",
        help="block playback at each frame via EXI until acknowledged",
    )
    ap.add_argument("--real-time", action="store_true", help="use real-time playback speed")
    ap.add_argument(
        "--no-resync",
        action="store_true",
        help="set playback shouldResync=false to match rollout/modelplay comparison probes",
    )
    ap.add_argument(
        "--emu-speed",
        type=float,
        default=None,
        help="override Dolphin emulation speed (e.g., 0.2 for 20%%).",
    )
    ap.add_argument(
        "--p1-joints",
        type=str,
        default="",
        help="comma-separated part indices to dump p1 joint matrices",
    )
    ap.add_argument(
        "--p2-joints",
        type=str,
        default="",
        help="comma-separated part indices to dump p2 joint matrices",
    )
    ap.add_argument(
        "--hurtbox-bone-mtx",
        action="store_true",
        help="include HurtCapsule::bone jobj->mtx (expensive; use with small frame windows + timeout)",
    )
    ap.add_argument(
        "--p1-jobj-preorder",
        type=int,
        default=0,
        help="dump first N HSD_JObj nodes from p1 costume_joint in pre-order (0 to disable)",
    )
    ap.add_argument(
        "--p1-anim-jobj-preorder",
        type=int,
        default=0,
        help="dump first N HSD_JObj nodes from p1 anim_skeleton in pre-order (0 to disable)",
    )
    ap.add_argument(
        "--p2-anim-jobj-preorder",
        type=int,
        default=0,
        help="dump first N HSD_JObj nodes from p2 anim_skeleton in pre-order (0 to disable)",
    )
    ap.add_argument(
        "--p1-anim-joints",
        type=str,
        default="",
        help="comma-separated pre-order indices to dump p1 anim-skeleton joint states",
    )
    ap.add_argument(
        "--p2-anim-joints",
        type=str,
        default="",
        help="comma-separated pre-order indices to dump p2 anim-skeleton joint states",
    )
    ap.add_argument(
        "--dyn-bones",
        action="store_true",
        help="dump dynamic-bone runtime nodes for each fighter (subset of fields)",
    )
    ap.add_argument(
        "--poll-item-kind",
        type=int,
        default=None,
        help="(block-on-frame only) while waiting for the next frame boundary, poll for the first appearance of an item of this kind and snapshot its state",
    )
    ap.add_argument(
        "--poll-p2-joint",
        type=int,
        default=None,
        help="(block-on-frame only) snapshot p2 fp->parts[part].joint->mtx for this part index during polling",
    )
    ap.add_argument(
        "--poll-out",
        type=str,
        default=None,
        help="(block-on-frame only) write poll snapshots to this JSON file",
    )
    ap.add_argument(
        "--poll-max-snaps",
        type=int,
        default=0,
        help="(block-on-frame only) max poll snapshots to record per ack/advance (0 = unlimited)",
    )
    ap.add_argument(
        "--poll-interval-ms",
        type=float,
        default=1.0,
        help="(block-on-frame only) polling interval in milliseconds while waiting for the next frame boundary",
    )
    args = ap.parse_args()

    max_seconds: float | None = args.max_seconds
    if max_seconds is not None and max_seconds <= 0:
        max_seconds = None

    suppress_warning = os.environ.get("SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING") == "1"
    if not suppress_warning:
        print(
            "\n".join(
                [
                    "WARNING: live Dolphin probes can wedge Dolphin and may wedge/kill your terminal session.",
                    "- Run in an isolated terminal (tmux / separate window).",
                    "- Strongly recommended: pass --max-seconds and/or wrap with `timeout`.",
                    "- Keep --start-frame/--end-frame windows small.",
                    "Set SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING=1 to suppress this warning.",
                    "",
                ]
            ),
            file=sys.stderr,
            flush=True,
        )
    if max_seconds is None and not args.i_accept_risk and not suppress_warning:
        print(
            "Refusing to run without a hard timeout. Pass --max-seconds (recommended) or --i-accept-risk.",
            file=sys.stderr,
            flush=True,
        )
        return 2

    t_start = time.monotonic()
    deadline = (t_start + max_seconds) if max_seconds is not None else None

    def _check_deadline(where: str) -> None:
        if deadline is not None and time.monotonic() > deadline:
            raise TimeoutError(f"probe exceeded --max-seconds during: {where}")

    replay = Path(args.replay)

    def _parse_joint_list(raw: str) -> list[int]:
        out: list[int] = []
        for tok in (raw or "").split(","):
            tok = tok.strip()
            if not tok:
                continue
            try:
                out.append(int(tok, 0))
            except Exception:
                raise ValueError(f"invalid joint index: {tok!r}") from None
        return out

    p1_joint_list = _parse_joint_list(args.p1_joints)
    p2_joint_list = _parse_joint_list(args.p2_joints)
    p1_anim_joint_list = _parse_joint_list(args.p1_anim_joints)
    p2_anim_joint_list = _parse_joint_list(args.p2_anim_joints)
    if not replay.exists():
        raise FileNotFoundError(replay)

    sample: dict | None = None
    frames: np.ndarray | None = None
    base_frame: int | None = None
    replay_inputs: dict[int, dict[str, np.ndarray]] | None = None
    replay_parse_error: str | None = None
    try:
        rb = read_replay_bytes(str(replay))
        sample = canonicalize_slippi_sample_last(peppi_bytes.read_slippi_bytes_sample(rb, 0, False))
        frames = sample["frame"].astype(np.int32, copy=False)
        base_frame = int(frames[0])
    except Exception as exc:
        replay_parse_error = f"{type(exc).__name__}: {exc}"
        if args.out_bin:
            raise

    start_frame = (int(base_frame) if base_frame is not None else 0) if args.start_frame is None else int(args.start_frame)
    end_frame = (int(frames[-1]) if frames is not None else start_frame) if args.end_frame is None else int(args.end_frame)
    if not args.continuous and args.frames <= 0:
        args.frames = max(0, end_frame - start_frame + 1)

    def replay_inputs_for_port(port: int) -> dict[str, np.ndarray]:
        assert sample is not None
        assert frames is not None
        pfx = f"p{port}"
        out: dict[str, np.ndarray] = {}
        out["stick_x"] = _as_unit_f32(sample[f"{pfx}_main_stick_x"])
        out["stick_y"] = _as_unit_f32(sample[f"{pfx}_main_stick_y"])
        out["cstick_x"] = _as_unit_f32(sample[f"{pfx}_c_stick_x"])
        out["cstick_y"] = _as_unit_f32(sample[f"{pfx}_c_stick_y"])
        out["l_shoulder"] = (
            _as_trigger(sample[f"{pfx}_l_shoulder"])
            if f"{pfx}_l_shoulder" in sample
            else _as_trigger(sample[f"{pfx}_trigger"])
        )
        out["r_shoulder"] = (
            _as_trigger(sample[f"{pfx}_r_shoulder"])
            if f"{pfx}_r_shoulder" in sample
            else _as_trigger(sample[f"{pfx}_trigger"])
        )

        def _b(name: str) -> np.ndarray:
            key = f"{pfx}_button_{name}"
            if key not in sample:
                return np.zeros_like(frames, dtype=np.uint8)
            return (sample[key] != 0).astype(np.uint8, copy=False)

        out["a"] = _b("a")
        out["b"] = _b("b")
        out["x"] = _b("x")
        out["y"] = _b("y")
        out["z"] = _b("z")
        out["l"] = _b("l")
        out["r"] = _b("r")
        out["start"] = _b("start")
        out["d_up"] = _b("d_up")
        return out

    if sample is not None and frames is not None:
        replay_inputs = {1: replay_inputs_for_port(1), 2: replay_inputs_for_port(2)}

    user_dir = Path(args.user_dir)
    user_dir.mkdir(parents=True, exist_ok=True)
    _write_dolphin_ini(user_dir, spectator_port=args.spectator_port, emu_speed=args.emu_speed)
    use_block = bool((not args.continuous) and args.block_on_frame)
    playback_txt = _write_playback_txt(
        user_dir,
        replay=replay,
        start_frame=start_frame,
        end_frame=end_frame,
        real_time=args.real_time,
        block_on_frame=use_block,
        should_resync=not args.no_resync,
    )

    dolphin = Path(args.dolphin)
    if not dolphin.exists():
        raise FileNotFoundError(dolphin)
    dolphin = _ensure_hookable_dolphin_bin(dolphin, user_dir=user_dir)

    env = os.environ.copy()
    print("launching playback dolphin...", flush=True)
    proc_args = [
        str(dolphin),
        "-e",
        str(Path(args.iso).resolve()),
        "-u",
        str(user_dir.resolve()),
        "--slippi-input",
        str(playback_txt.resolve()),
    ]
    if not args.continuous and not use_block:
        proc_args.append("--slippi-step")
    proc = subprocess.Popen(proc_args, env=env)

    try:
        import dolphin_memory_engine as dme  # noqa: E402
    except Exception as exc:
        proc.kill()
        raise exc

    stepper: _Stepper | None = None

    def _cleanup() -> None:
        if stepper is not None:
            try:
                stepper.close()
            except Exception:
                pass
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    atexit.register(_cleanup)
    if not args.continuous:
        stepper = _Stepper("127.0.0.1", args.spectator_port)

    t0 = time.monotonic()
    print("waiting for dolphin_memory_engine hook...", flush=True)
    while True:
        _check_deadline("hook")
        if proc.poll() is not None:
            raise RuntimeError(f"dolphin exited (code={proc.returncode})")
        dme.hook()
        if dme.is_hooked():
            break
        if time.monotonic() - t0 > 30.0:
            raise RuntimeError("dolphin_memory_engine failed to hook")
        time.sleep(0.05)
    print("hooked.", flush=True)

    # Ensure spectator connection is up before stepping.
    if stepper is not None:
        t_conn = time.monotonic()
        while True:
            _check_deadline("spectator_connect")
            try:
                stepper.connect()
                break
            except Exception:
                if time.monotonic() - t_conn > 30.0:
                    raise RuntimeError("failed to connect to spectator server")
                time.sleep(0.05)

    # Frame index pointer (r13 base - 0x49AC)
    R13_BASE = 0x804DB6A0
    FRAME_INDEX_PTR = R13_BASE - 0x49AC
    PLAYER_SLOTS = 0x80453080
    STATIC_PLAYER_SIZE = 0xE90
    GOBJ_USER_DATA_OFF = 0x2C
    LB_804D63B0_PTR = 0x804D63B0
    FIGHTER_POS_X_OFF = 0xB0
    FIGHTER_POS_Y_OFF = 0xB4
    FIGHTER_POS_Z_OFF = 0xB8
    # Decomp: `refs/melee/src/melee/ft/types.h` `Fighter` struct.
    FIGHTER_PREV_POS_X_OFF = 0xBC
    FIGHTER_PREV_POS_Y_OFF = 0xC0
    FIGHTER_PREV_POS_Z_OFF = 0xC4
    FIGHTER_POS_DELTA_X_OFF = 0xC8
    FIGHTER_POS_DELTA_Y_OFF = 0xCC
    FIGHTER_POS_DELTA_Z_OFF = 0xD0
    FIGHTER_SELF_VEL_X_OFF = 0x80
    FIGHTER_SELF_VEL_Y_OFF = 0x84
    FIGHTER_GR_VEL_OFF = 0xEC
    FIGHTER_ACTION_STATE_OFF = 0x04
    FIGHTER_ACTION_OFF = 0x10
    FIGHTER_ANIM_OFF = 0x14
    FIGHTER_ACTION_FRAME_OFF = 0x894
    FIGHTER_FRAME_SPEED_MUL_OFF = 0x89C
    FIGHTER_ANIM_FRAME_OFF = 0x8A8
    # ftAnim.c: `fp->x8AC_animSkeleton` (HSD_JObj* root for the "new anim" pose).
    FIGHTER_ANIM_SKELETON_OFF = 0x8AC
    FIGHTER_GROUND_OR_AIR_OFF = 0xE0
    FIGHTER_SCALE_X38_OFF = 0x38
    FIGHTER_FACING_OFF = 0x2C
    FIGHTER_PERCENT_OFF = 0x1830
    FIGHTER_TEAM_OFF = 0x61B
    # Fighter input struct (ft/types.h: fp+0x620).
    # These are the post-clamp, post-deadzone normalized floats used by fighter logic.
    FIGHTER_LSTICK_X_OFF = 0x620
    FIGHTER_LSTICK_Y_OFF = 0x624
    FIGHTER_LSTICK1_X_OFF = 0x628
    FIGHTER_LSTICK1_Y_OFF = 0x62C
    FIGHTER_STATE_FLAGS_2218_OFF = 0x2218
    FIGHTER_STATE_FLAGS_221A_OFF = 0x221A
    FIGHTER_STATE_FLAGS_221B_OFF = 0x221B
    FIGHTER_STATE_FLAGS_221C_OFF = 0x221C
    FIGHTER_STATE_FLAGS_221F_OFF = 0x221F
    FIGHTER_STATE_FLAGS_2228_OFF = 0x2228
    # Damage/hitlag owner internals used by ftCo_Damage_OnEveryHitlag / Fighter_8006A1BC.
    # refs/melee/src/melee/ft/types.h
    FIGHTER_X670_LSTICK_TILT_X_TIMER_OFF = 0x670
    FIGHTER_X671_LSTICK_TILT_Y_TIMER_OFF = 0x671
    FIGHTER_X672_INPUT_TIMER_COUNTER_OFF = 0x672
    FIGHTER_DYN_BONES_OFF = 0x2F0
    FIGHTER_DYN_BONES_COUNT_OFF = 0x3E0
    FIGHTER_HITLAG_OFF = 0x195C
    FIGHTER_HITSTUN_OFF = 0x2340
    # ftCommon/types.h: ftCommon_MotionVars.entry.x28 is at fp+0x2368 (union overlay).
    FIGHTER_ENTRY_X28_OFF = 0x2368
    # ftCommon/types.h: ftCommon_MotionVars.guard.{x4,x8} overlays at fp+0x2344/fp+0x2348.
    FIGHTER_GUARD_X4_OFF = 0x2344
    FIGHTER_GUARD_X8_OFF = 0x2348
    FIGHTER_SHIELD_HEALTH_OFF = 0x1998
    # CollData ECB fields (decomp: refs/melee/src/melee/lb/types.h `struct CollData`).
    # - `x64_ecb`:   fp+0x754
    # - `desired_ecb`: fp+0x774
    # - `ecb`:       fp+0x794
    # - `prev_ecb`:  fp+0x7B4
    # - `xE4_ecb`:   fp+0x7D4
    #
    # Note: engine dumps capture `ecb` (fp+0x794). When investigating one-frame ECB anomalies,
    # also inspect `desired_ecb` and `prev_ecb` to determine whether the engine is interpolating
    # the ECB (`mpCollInterpolateECB`) or whether a later refresh is being skipped.
    FIGHTER_ECB_X64_TOP_OFF = 0x754
    FIGHTER_ECB_DESIRED_TOP_OFF = 0x774
    FIGHTER_ECB_TOP_OFF = 0x794
    FIGHTER_ECB_BOTTOM_OFF = 0x79C
    FIGHTER_COLL_CUR_POS_OFF = 0x6F4
    FIGHTER_ECB_RIGHT_OFF = 0x7A4
    FIGHTER_ECB_LEFT_OFF = 0x7AC
    FIGHTER_ECB_PREV_TOP_OFF = 0x7B4
    FIGHTER_ECB_XE4_TOP_OFF = 0x7D4
    FIGHTER_ECB_SOURCE_KIND_OFF = 0x7F4
    FIGHTER_ECB_SOURCE_JOINTS_OFF = 0x7FC
    FIGHTER_COLL_X34_FLAGS_OFF = 0x724
    FIGHTER_COLL_X35_FLAGS_OFF = 0x725
    FIGHTER_COLL_X130_FLAGS_OFF = 0x820
    FIGHTER_COLL_ENV_FLAGS_OFF = 0x824
    FIGHTER_COLL_PREV_ENV_FLAGS_OFF = 0x828
    FIGHTER_COLL_FLOOR_INDEX_OFF = 0x83C
    FIGHTER_COLL_FLOOR_FLAGS_OFF = 0x840
    # Fighter callback struct. These raw pointers let us see whether the damage callback family is
    # actually installed on a given frame even when the visible action/flag lanes match.
    # refs/melee/src/melee/ft/types.h
    FIGHTER_HITLAG_CB_OFF = 0x21D0
    FIGHTER_PRE_HITLAG_CB_OFF = 0x21D4
    FIGHTER_POST_HITLAG_CB_OFF = 0x21D8
    FIGHTER_HITBOX_BASE_OFF = 0x914
    HITBOX_STRIDE = 0x138
    FIGHTER_HURTBOX_BASE_OFF = 0x11A0
    HURTBOX_STRIDE = 0x4C
    FIGHTER_X166C_OFF = 0x166C
    FIGHTER_X1670_OFF = 0x1670
    ITEM_MANAGER_PTR = R13_BASE - 0x3E74
    ITEM_MANAGER_FIRST_GOBJ_OFF = 0x24
    GOBJ_NEXT_OFF = 0x08
    ITEM_DATA_OFF = 0x2C
    ITEM_KIND_OFF = 0x10
    ITEM_STATE_OFF = 0x24
    ITEM_ANIM_ID_OFF = 0x28
    ITEM_FACING_OFF = 0x2C
    ITEM_POS_OFF = 0x4C
    ITEM_VEL_OFF = 0x40
    ITEM_X58_OFF = 0x58
    ITEM_X64_OFF = 0x64
    ITEM_X70_NUDGE_OFF = 0x70
    # Decomp: `it/itCharItems.h` `itFoxLaser_ItemVars` stores `Vec3 pos` at `ip+0xDE0`,
    # and `itFoxlaser_UnkMotion1_Phys` writes `item->xDD4_itemVar.foxlaser.pos = item->pos`
    # once per frame (before the generic item velocity add in `Item_802697D4`).
    ITEM_FOX_LASER_PREV_POS_OFF = 0xDE0
    ITEM_ANIM_FRAME_OFF = 0x5CC
    ITEM_LIFETIME_OFF = 0xD44
    ITEM_HITBOX0_OFF = 0x5D4
    ITEM_OWNER_OFF = 0x518
    MAX_ITEMS = 15
    RNG_STATE_ADDR = 0x804D5F90
    GAME_TIMER_ADDR = 0x8046B6C8
    TEAMS_FLAG_ADDR = 0x804807C8
    P1_STOCK_ADDR = 0x8045310E
    P2_STOCK_ADDR = 0x80453F9E
    FIGHTER_PARTS_OFF = 0x5E8
    FIGHTER_PART_STRIDE = 0x10
    JOBJ_MTX_OFF = 0x44
    FIGHTER_COSTUME_JOINT_OFF = 0x108
    FIGHTER_FTDATA_OFF = 0x10C
    FTDATA_JOINT_OFF = 0x5C

    def build_parts_map(fp_ptr: int, max_parts: int) -> dict[int, int]:
        parts_ptr = _u32_at(dme, fp_ptr + FIGHTER_PARTS_OFF)
        if parts_ptr == 0:
            return {}
        mapping: dict[int, int] = {}
        for idx in range(max_parts):
            joint_ptr = _u32_at(dme, parts_ptr + idx * FIGHTER_PART_STRIDE)
            if joint_ptr != 0:
                mapping[joint_ptr] = idx
        return mapping

    def fighter_gobj_for_port(port: int) -> int:
        slot = port - 1
        base = PLAYER_SLOTS + slot * STATIC_PLAYER_SIZE
        transformed0 = int(_u8_at(dme, base + 0x0C)) & 0xFF
        gobj_ptr = int(_u32_at(dme, base + 0xB0 + transformed0 * 4))
        if gobj_ptr:
            return gobj_ptr
        # Playback builds sometimes have a transient/zero transformed index early; fall back to scanning.
        for i in range(4):
            gobj_ptr = int(_u32_at(dme, base + 0xB0 + i * 4))
            if gobj_ptr:
                return gobj_ptr
        return 0

    def fighter_ptr_for_port(port: int) -> int:
        gobj_ptr = fighter_gobj_for_port(port)
        return _u32_at(dme, gobj_ptr + GOBJ_USER_DATA_OFF)

    def read_joint_mtx(fp_ptr: int, part_idx: int) -> list[float] | None:
        parts_ptr = _u32_at(dme, fp_ptr + FIGHTER_PARTS_OFF)
        if parts_ptr == 0:
            return None
        joint_ptr = _u32_at(dme, parts_ptr + part_idx * FIGHTER_PART_STRIDE)
        if joint_ptr == 0:
            return None
        base = joint_ptr + JOBJ_MTX_OFF
        return [float(_f32_at(dme, base + i * 4)) for i in range(12)]

    def read_joint_state(
        fp_ptr: int, part_idx: int, parts_map: dict[int, int] | None = None
    ) -> dict | None:
        parts_ptr = _u32_at(dme, fp_ptr + FIGHTER_PARTS_OFF)
        if parts_ptr == 0:
            return None
        joint_ptr = _u32_at(dme, parts_ptr + part_idx * FIGHTER_PART_STRIDE)
        if joint_ptr == 0:
            return None
        out = _read_jobj_state(dme, joint_ptr)
        if out is None:
            return None
        parent_part = None
        parent_ptr = int(out.get("parent_ptr", 0))
        if parts_map is not None and parent_ptr:
            parent_part = parts_map.get(parent_ptr)
        out["parent_part"] = parent_part
        return out

    def read_ecb_source(fp_ptr: int, parts_map: dict[int, int]) -> dict:
        kind = int(_u32_at(dme, fp_ptr + FIGHTER_ECB_SOURCE_KIND_OFF))
        parts: list[int] = []
        ptrs: list[int] = []
        base = fp_ptr + FIGHTER_ECB_SOURCE_JOINTS_OFF
        for i in range(6):
            ptr = int(_u32_at(dme, base + i * 4))
            ptrs.append(ptr)
            parts.append(int(parts_map.get(ptr, -1)))
        return {"kind": kind, "parts": parts, "ptrs": ptrs}

    def read_ecb_box(fp_ptr: int, top_off: int) -> dict:
        bottom_off = top_off + (FIGHTER_ECB_BOTTOM_OFF - FIGHTER_ECB_TOP_OFF)
        right_off = top_off + (FIGHTER_ECB_RIGHT_OFF - FIGHTER_ECB_TOP_OFF)
        left_off = top_off + (FIGHTER_ECB_LEFT_OFF - FIGHTER_ECB_TOP_OFF)
        return {
            "top": (
                float(_f32_at(dme, fp_ptr + top_off + 0x00)),
                float(_f32_at(dme, fp_ptr + top_off + 0x04)),
            ),
            "bottom": (
                float(_f32_at(dme, fp_ptr + bottom_off + 0x00)),
                float(_f32_at(dme, fp_ptr + bottom_off + 0x04)),
            ),
            "right": (
                float(_f32_at(dme, fp_ptr + right_off + 0x00)),
                float(_f32_at(dme, fp_ptr + right_off + 0x04)),
            ),
            "left": (
                float(_f32_at(dme, fp_ptr + left_off + 0x00)),
                float(_f32_at(dme, fp_ptr + left_off + 0x04)),
            ),
        }

    def read_dyn_bones(fp_ptr: int, parts_map: dict[int, int] | None = None) -> list[dict]:
        bones: list[dict] = []
        count = int(_u32_at(dme, fp_ptr + FIGHTER_DYN_BONES_COUNT_OFF))
        if count <= 0:
            return bones
        if parts_map is None:
            parts_map = build_parts_map(fp_ptr, 128)
        base = fp_ptr + FIGHTER_DYN_BONES_OFF
        for i in range(min(count, 8)):
            desc_base = base + i * 0x18
            bone_id = int(_u32_at(dme, desc_base + 0x00))
            data_ptr = int(_u32_at(dme, desc_base + 0x04))
            node_count = int(_u32_at(dme, desc_base + 0x08))
            pos = (
                float(_f32_at(dme, desc_base + 0x0C)),
                float(_f32_at(dme, desc_base + 0x10)),
                float(_f32_at(dme, desc_base + 0x14)),
            )
            nodes: list[dict] = []
            node_ptr = data_ptr
            for _j in range(min(node_count, 8)):
                if node_ptr == 0:
                    break
                jobj_ptr = int(_u32_at(dme, node_ptr + 0x00))
                part_idx = parts_map.get(jobj_ptr) if parts_map is not None else None
                nodes.append(
                    {
                        "jobj_ptr": jobj_ptr,
                        "part": int(part_idx) if part_idx is not None else None,
                        "rot": (
                            float(_f32_at(dme, node_ptr + 0x04)),
                            float(_f32_at(dme, node_ptr + 0x08)),
                            float(_f32_at(dme, node_ptr + 0x0C)),
                        ),
                        "rot_bits": (
                            int(_u32_at(dme, node_ptr + 0x04)),
                            int(_u32_at(dme, node_ptr + 0x08)),
                            int(_u32_at(dme, node_ptr + 0x0C)),
                        ),
                        "pos": (
                            float(_f32_at(dme, node_ptr + 0x14)),
                            float(_f32_at(dme, node_ptr + 0x18)),
                            float(_f32_at(dme, node_ptr + 0x1C)),
                        ),
                        "pos_bits": (
                            int(_u32_at(dme, node_ptr + 0x14)),
                            int(_u32_at(dme, node_ptr + 0x18)),
                            int(_u32_at(dme, node_ptr + 0x1C)),
                        ),
                        "world": (
                            float(_f32_at(dme, node_ptr + 0x2C)),
                            float(_f32_at(dme, node_ptr + 0x30)),
                            float(_f32_at(dme, node_ptr + 0x34)),
                        ),
                        "world_bits": (
                            int(_u32_at(dme, node_ptr + 0x2C)),
                            int(_u32_at(dme, node_ptr + 0x30)),
                            int(_u32_at(dme, node_ptr + 0x34)),
                        ),
                        "axis": (
                            float(_f32_at(dme, node_ptr + 0x38)),
                            float(_f32_at(dme, node_ptr + 0x3C)),
                            float(_f32_at(dme, node_ptr + 0x40)),
                        ),
                        "axis_bits": (
                            int(_u32_at(dme, node_ptr + 0x38)),
                            int(_u32_at(dme, node_ptr + 0x3C)),
                            int(_u32_at(dme, node_ptr + 0x40)),
                        ),
                        "axis_angle": float(_f32_at(dme, node_ptr + 0x44)),
                        "axis_angle_bits": int(_u32_at(dme, node_ptr + 0x44)),
                        "base_rot": (
                            float(_f32_at(dme, node_ptr + 0x58)),
                            float(_f32_at(dme, node_ptr + 0x5C)),
                            float(_f32_at(dme, node_ptr + 0x60)),
                        ),
                        "base_rot_bits": (
                            int(_u32_at(dme, node_ptr + 0x58)),
                            int(_u32_at(dme, node_ptr + 0x5C)),
                            int(_u32_at(dme, node_ptr + 0x60)),
                        ),
                        "dist": float(_f32_at(dme, node_ptr + 0x48)),
                        "dist_bits": int(_u32_at(dme, node_ptr + 0x48)),
                        "p4c": float(_f32_at(dme, node_ptr + 0x4C)),
                        "p4c_bits": int(_u32_at(dme, node_ptr + 0x4C)),
                        "p50": float(_f32_at(dme, node_ptr + 0x50)),
                        "p50_bits": int(_u32_at(dme, node_ptr + 0x50)),
                        "axis_id": int(_u32_at(dme, node_ptr + 0x54)),
                        "p68": float(_f32_at(dme, node_ptr + 0x68)),
                        "p68_bits": int(_u32_at(dme, node_ptr + 0x68)),
                        "p84": float(_f32_at(dme, node_ptr + 0x84)),
                        "p84_bits": int(_u32_at(dme, node_ptr + 0x84)),
                        "p88": float(_f32_at(dme, node_ptr + 0x88)),
                        "p88_bits": int(_u32_at(dme, node_ptr + 0x88)),
                        "p8c": float(_f32_at(dme, node_ptr + 0x8C)),
                        "p8c_bits": int(_u32_at(dme, node_ptr + 0x8C)),
                    }
                )
                node_ptr = int(_u32_at(dme, node_ptr + 0x90))
            bones.append(
                {
                    "bone_id": bone_id,
                    "count": node_count,
                    "pos": pos,
                    "nodes": nodes,
                }
            )
        return bones

    def read_items() -> list[dict]:
        items = []
        owner_gobjs = {fighter_gobj_for_port(1): 1, fighter_gobj_for_port(2): 2}
        manager_ptr = _u32_at(dme, ITEM_MANAGER_PTR)
        if manager_ptr == 0:
            return items
        item_gobj = _u32_at(dme, manager_ptr + ITEM_MANAGER_FIRST_GOBJ_OFF)
        count = 0
        while item_gobj != 0 and count < MAX_ITEMS:
            item_data = _u32_at(dme, item_gobj + ITEM_DATA_OFF)
            if item_data != 0:
                owner_gobj = _u32_at(dme, item_data + ITEM_OWNER_OFF)
                owner_port = owner_gobjs.get(owner_gobj, -1)
                items.append(
                    {
                        "gobj": int(item_gobj),
                        "item_id": int(item_data),
                        "kind": int(_u32_at(dme, item_data + ITEM_KIND_OFF) & 0xFFFF),
                        "state": int(_u32_at(dme, item_data + ITEM_STATE_OFF) & 0xFFFF),
                        "owner": int(owner_port),
                        "facing": float(_f32_at(dme, item_data + ITEM_FACING_OFF)),
                        "anim_id": int(_u32_at(dme, item_data + ITEM_ANIM_ID_OFF) & 0xFFFF),
                        "anim_frame": float(_f32_at(dme, item_data + ITEM_ANIM_FRAME_OFF)),
                        "lifetime": float(_f32_at(dme, item_data + ITEM_LIFETIME_OFF)),
                        "damage": int(_u32_at(dme, item_data + ITEM_HITBOX0_OFF + 0x08)),
                        "pos": (
                            float(_f32_at(dme, item_data + ITEM_POS_OFF + 0x00)),
                            float(_f32_at(dme, item_data + ITEM_POS_OFF + 0x04)),
                            float(_f32_at(dme, item_data + ITEM_POS_OFF + 0x08)),
                        ),
                        "vel": (
                            float(_f32_at(dme, item_data + ITEM_VEL_OFF + 0x00)),
                            float(_f32_at(dme, item_data + ITEM_VEL_OFF + 0x04)),
                            float(_f32_at(dme, item_data + ITEM_VEL_OFF + 0x08)),
                        ),
                        "x58_vec_unk": (
                            float(_f32_at(dme, item_data + ITEM_X58_OFF + 0x00)),
                            float(_f32_at(dme, item_data + ITEM_X58_OFF + 0x04)),
                            float(_f32_at(dme, item_data + ITEM_X58_OFF + 0x08)),
                        ),
                        "x64_vec_unk2": (
                            float(_f32_at(dme, item_data + ITEM_X64_OFF + 0x00)),
                            float(_f32_at(dme, item_data + ITEM_X64_OFF + 0x04)),
                            float(_f32_at(dme, item_data + ITEM_X64_OFF + 0x08)),
                        ),
                        "x70_nudge": (
                            float(_f32_at(dme, item_data + ITEM_X70_NUDGE_OFF + 0x00)),
                            float(_f32_at(dme, item_data + ITEM_X70_NUDGE_OFF + 0x04)),
                            float(_f32_at(dme, item_data + ITEM_X70_NUDGE_OFF + 0x08)),
                        ),
                    }
                )
                if int(items[-1]["kind"]) in (54, 55):
                    items[-1]["foxlaser_prev_pos"] = (
                        float(_f32_at(dme, item_data + ITEM_FOX_LASER_PREV_POS_OFF + 0x00)),
                        float(_f32_at(dme, item_data + ITEM_FOX_LASER_PREV_POS_OFF + 0x04)),
                        float(_f32_at(dme, item_data + ITEM_FOX_LASER_PREV_POS_OFF + 0x08)),
                    )
            item_gobj = _u32_at(dme, item_gobj + GOBJ_NEXT_OFF)
            count += 1
        return items

    def read_hitboxes(fp_ptr: int) -> list[dict]:
        hitboxes = []
        for idx in range(4):
            base = fp_ptr + FIGHTER_HITBOX_BASE_OFF + idx * HITBOX_STRIDE
            hitboxes.append(
                {
                    "state": int(_u32_at(dme, base + 0x00)),
                    "group": int(_u32_at(dme, base + 0x04)),
                    "damage": int(_u32_at(dme, base + 0x08)),
                    "damage_stale": float(_f32_at(dme, base + 0x0C)),
                    "offset": (
                        float(_f32_at(dme, base + 0x18)),
                        float(_f32_at(dme, base + 0x14)),
                        float(_f32_at(dme, base + 0x10)),
                    ),
                    "size": float(_f32_at(dme, base + 0x1C)),
                    "angle": int(_u32_at(dme, base + 0x20)),
                    "kbg": int(_u32_at(dme, base + 0x24)),
                    "wsk": int(_u32_at(dme, base + 0x28)),
                    "bkb": int(_u32_at(dme, base + 0x2C)),
                    "element": int(_u32_at(dme, base + 0x30)),
                    "shield_damage": int(_u32_at(dme, base + 0x34)),
                    "sfx": int(_u32_at(dme, base + 0x38)),
                    "sfx_kind": int(_u32_at(dme, base + 0x3C)),
                    "flags": (
                        int(_u8_at(dme, base + 0x40)),
                        int(_u8_at(dme, base + 0x41)),
                        int(_u8_at(dme, base + 0x42)),
                        int(_u8_at(dme, base + 0x43)),
                        int(_u8_at(dme, base + 0x44)),
                        int(_u8_at(dme, base + 0x45)),
                        int(_u8_at(dme, base + 0x46)),
                        int(_u8_at(dme, base + 0x47)),
                    ),
                    "bone_ptr": int(_u32_at(dme, base + 0x48)),
                    "pos": (
                        float(_f32_at(dme, base + 0x54)),
                        float(_f32_at(dme, base + 0x50)),
                        float(_f32_at(dme, base + 0x4C)),
                    ),
                }
            )
        return hitboxes

    def read_hurtboxes(fp_ptr: int, *, include_bone_mtx: bool) -> list[dict]:
        hurtboxes = []
        for idx in range(15):
            base = fp_ptr + FIGHTER_HURTBOX_BASE_OFF + idx * HURTBOX_STRIDE
            state = int(_u32_at(dme, base + 0x00))
            bone_ptr = int(_u32_at(dme, base + 0x20))
            bone_mtx = None
            bone_mtx_bits = None
            if include_bone_mtx and bone_ptr:
                mtx_base = bone_ptr + JOBJ_MTX_OFF
                bone_mtx_bits = [int(_u32_at(dme, mtx_base + i * 4)) for i in range(12)]
                bone_mtx = [float(_f32_at(dme, mtx_base + i * 4)) for i in range(12)]
            hurtboxes.append(
                {
                    "state": state,
                    "a_offset": (
                        float(_f32_at(dme, base + 0x04)),
                        float(_f32_at(dme, base + 0x08)),
                        float(_f32_at(dme, base + 0x0C)),
                    ),
                    "b_offset": (
                        float(_f32_at(dme, base + 0x10)),
                        float(_f32_at(dme, base + 0x14)),
                        float(_f32_at(dme, base + 0x18)),
                    ),
                    "scale": float(_f32_at(dme, base + 0x1C)),
                    "bone_ptr": bone_ptr,
                    "bone_mtx": bone_mtx,
                    "bone_mtx_bits": bone_mtx_bits,
                    "flags": int(_u8_at(dme, base + 0x24)),
                    "a_pos": (
                        float(_f32_at(dme, base + 0x28)),
                        float(_f32_at(dme, base + 0x2C)),
                        float(_f32_at(dme, base + 0x30)),
                    ),
                    "b_pos": (
                        float(_f32_at(dme, base + 0x34)),
                        float(_f32_at(dme, base + 0x38)),
                        float(_f32_at(dme, base + 0x3C)),
                    ),
                    "bone_idx": _u32_to_i32(_u32_at(dme, base + 0x40)),
                    "height": int(_u32_at(dme, base + 0x44)),
                    "is_grabbable": int(_u8_at(dme, base + 0x48)),
                }
            )
        return hurtboxes

    def _try_read_frame_index() -> int | None:
        try:
            return _u32_to_i32(_u32_at(dme, FRAME_INDEX_PTR))
        except RuntimeError:
            try:
                dme.hook()
            except Exception:
                pass
            return None

    def _fighters_ready() -> bool:
        try:
            return fighter_ptr_for_port(1) != 0 and fighter_ptr_for_port(2) != 0
        except Exception:
            return False

    def _ack_frame(msg: dict | None) -> int | None:
        if not msg:
            return None
        cur = msg.get("currentFrame")
        if cur is None:
            return None
        try:
            return int(cur)
        except Exception:
            return None

    def _wait_for_frame_change(prev_frame: int, timeout_s: float = 2.0) -> int | None:
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout_s:
            cur_frame = _try_read_frame_index()
            if cur_frame is not None and cur_frame != prev_frame:
                return cur_frame
            time.sleep(0.001)
        return None

    # Wait until frame index reaches start_frame. If we can't read yet, poke playback with steps.
    print(f"waiting for frame >= {start_frame}...", flush=True)
    t_wait = time.monotonic()
    cur = None
    last_cur = None
    wait_iters = 0
    if use_block:
        while True:
            _check_deadline("wait_start_frame")
            if proc.poll() is not None:
                raise RuntimeError(f"dolphin exited (code={proc.returncode})")
            cur = _try_read_frame_index()
            if cur is not None:
                last_cur = cur
                if cur >= start_frame and _fighters_ready():
                    break
                if stepper is not None:
                    try:
                        stepper.send_ack(cur)
                        nxt = _wait_for_frame_change(cur, timeout_s=2.0)
                        if nxt is not None:
                            last_cur = nxt
                            cur = nxt
                            if cur >= start_frame and _fighters_ready():
                                break
                    except Exception:
                        pass
            wait_iters += 1
            if args.debug_wait and wait_iters % 10 == 0:
                fp1 = fp2 = None
                try:
                    fp1 = fighter_ptr_for_port(1)
                    fp2 = fighter_ptr_for_port(2)
                except Exception:
                    pass
                print(f"wait loop: last_cur={last_cur} fp1={fp1} fp2={fp2}", flush=True)
            if time.monotonic() - t_wait > 120.0:
                raise TimeoutError(f"timed out waiting for playback to reach start frame (last_cur={last_cur})")
            time.sleep(0.02)
    else:
        while True:
            _check_deadline("wait_start_frame")
            if proc.poll() is not None:
                raise RuntimeError(f"dolphin exited (code={proc.returncode})")
            cur = _try_read_frame_index()
            if cur is not None:
                last_cur = cur
                if cur >= start_frame and _fighters_ready():
                    break
            if stepper is not None:
                try:
                    target = (last_cur + 1) if last_cur is not None else None
                    ack = stepper.send_step(1, target_frame=target)
                    ack_cur = _ack_frame(ack)
                    if ack_cur is not None:
                        last_cur = ack_cur
                        if ack_cur >= start_frame and _fighters_ready():
                            break
                except Exception:
                    pass
            wait_iters += 1
            if args.debug_wait and wait_iters % 10 == 0:
                fp1 = fp2 = None
                try:
                    fp1 = fighter_ptr_for_port(1)
                    fp2 = fighter_ptr_for_port(2)
                except Exception:
                    pass
                print(f"wait loop: last_cur={last_cur} fp1={fp1} fp2={fp2}", flush=True)
            if time.monotonic() - t_wait > 120.0:
                raise TimeoutError(f"timed out waiting for playback to reach start frame (last_cur={last_cur})")
            time.sleep(0.02)

    # Initialize prev using the last known playback frame from ack.
    prev = last_cur
    if prev is None:
        t_prev = time.monotonic()
        while True:
            _check_deadline("initial_frame_index")
            prev = _try_read_frame_index()
            if prev is not None:
                break
            if time.monotonic() - t_prev > 30.0:
                raise RuntimeError("failed to read initial frame index")
            time.sleep(0.05)
    def capture_row(cur_frame: int) -> dict:
        fp1 = fighter_ptr_for_port(1)
        fp2 = fighter_ptr_for_port(2)
        p1_hurt = read_hurtboxes(fp1, include_bone_mtx=bool(args.hurtbox_bone_mtx))
        p2_hurt = read_hurtboxes(fp2, include_bone_mtx=bool(args.hurtbox_bone_mtx))
        p1_invul = 1 if any(hb["state"] != 0 for hb in p1_hurt) else 0
        p2_invul = 1 if any(hb["state"] != 0 for hb in p2_hurt) else 0
        p1_stocks = int(_u8_at(dme, P1_STOCK_ADDR))
        p2_stocks = int(_u8_at(dme, P2_STOCK_ADDR))
        p1 = {
            "pos": (
                float(_f32_at(dme, fp1 + FIGHTER_POS_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_POS_Y_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_POS_Z_OFF)),
            ),
            "prev_pos": (
                float(_f32_at(dme, fp1 + FIGHTER_PREV_POS_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_PREV_POS_Y_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_PREV_POS_Z_OFF)),
            ),
            "pos_delta": (
                float(_f32_at(dme, fp1 + FIGHTER_POS_DELTA_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_POS_DELTA_Y_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_POS_DELTA_Z_OFF)),
            ),
            "x38_scale": float(_f32_at(dme, fp1 + FIGHTER_SCALE_X38_OFF)),
            "entry_x28": float(_f32_at(dme, fp1 + FIGHTER_ENTRY_X28_OFF)),
            "entry_x28_bits": int(_u32_at(dme, fp1 + FIGHTER_ENTRY_X28_OFF)),
            "guard_x4": float(_f32_at(dme, fp1 + FIGHTER_GUARD_X4_OFF)),
            "guard_x4_bits": int(_u32_at(dme, fp1 + FIGHTER_GUARD_X4_OFF)),
            "guard_x8": float(_f32_at(dme, fp1 + FIGHTER_GUARD_X8_OFF)),
            "guard_x8_bits": int(_u32_at(dme, fp1 + FIGHTER_GUARD_X8_OFF)),
            "lstick": (
                float(_f32_at(dme, fp1 + FIGHTER_LSTICK_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_LSTICK_Y_OFF)),
            ),
            "lstick_bits": (
                int(_u32_at(dme, fp1 + FIGHTER_LSTICK_X_OFF)),
                int(_u32_at(dme, fp1 + FIGHTER_LSTICK_Y_OFF)),
            ),
            "lstick1": (
                float(_f32_at(dme, fp1 + FIGHTER_LSTICK1_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_LSTICK1_Y_OFF)),
            ),
            "lstick1_bits": (
                int(_u32_at(dme, fp1 + FIGHTER_LSTICK1_X_OFF)),
                int(_u32_at(dme, fp1 + FIGHTER_LSTICK1_Y_OFF)),
            ),
            "coll_pos": (
                float(_f32_at(dme, fp1 + FIGHTER_COLL_CUR_POS_OFF + 0x00)),
                float(_f32_at(dme, fp1 + FIGHTER_COLL_CUR_POS_OFF + 0x04)),
                float(_f32_at(dme, fp1 + FIGHTER_COLL_CUR_POS_OFF + 0x08)),
            ),
            "self_vel": (
                float(_f32_at(dme, fp1 + FIGHTER_SELF_VEL_X_OFF)),
                float(_f32_at(dme, fp1 + FIGHTER_SELF_VEL_Y_OFF)),
            ),
            "gr_vel": float(_f32_at(dme, fp1 + FIGHTER_GR_VEL_OFF)),
            "action_state": int(_u32_at(dme, fp1 + FIGHTER_ACTION_STATE_OFF)),
            "action": int(_u32_at(dme, fp1 + FIGHTER_ACTION_OFF)),
            "anim": int(_u32_at(dme, fp1 + FIGHTER_ANIM_OFF)),
            "anim_frame": float(_f32_at(dme, fp1 + FIGHTER_ANIM_FRAME_OFF)),
            "action_frame": float(_f32_at(dme, fp1 + FIGHTER_ACTION_FRAME_OFF)),
            "frame_speed_mul": float(_f32_at(dme, fp1 + FIGHTER_FRAME_SPEED_MUL_OFF)),
            "frame_speed_mul_bits": int(_u32_at(dme, fp1 + FIGHTER_FRAME_SPEED_MUL_OFF)),
            "ground_or_air": int(_u32_at(dme, fp1 + FIGHTER_GROUND_OR_AIR_OFF) & 0xFF),
            "state_xE0": int(_u32_at(dme, fp1 + FIGHTER_GROUND_OR_AIR_OFF)),
            "facing": float(_f32_at(dme, fp1 + FIGHTER_FACING_OFF)),
            "percent": float(_f32_at(dme, fp1 + FIGHTER_PERCENT_OFF)),
            "state_flags": (
                int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_2218_OFF)),
                int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_221A_OFF)),
                int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_221B_OFF)),
                int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_221C_OFF)),
                int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_221F_OFF)),
            ),
            "state_flags_2228": int(_u8_at(dme, fp1 + FIGHTER_STATE_FLAGS_2228_OFF)),
            "x670_timer_lstick_tilt_x": int(
                _u8_at(dme, fp1 + FIGHTER_X670_LSTICK_TILT_X_TIMER_OFF)
            ),
            "x671_timer_lstick_tilt_y": int(
                _u8_at(dme, fp1 + FIGHTER_X671_LSTICK_TILT_Y_TIMER_OFF)
            ),
            "x672_input_timer_counter": int(
                _u8_at(dme, fp1 + FIGHTER_X672_INPUT_TIMER_COUNTER_OFF)
            ),
            "invulnerable": p1_invul,
            "hitlag_left": float(_f32_at(dme, fp1 + FIGHTER_HITLAG_OFF)),
            # NOTE: fp+0x2340 is `misc_as` in engine dumps (not hitstun).
            "misc_as_2340": float(_f32_at(dme, fp1 + FIGHTER_HITSTUN_OFF)),
            "guard_x4": float(_f32_at(dme, fp1 + FIGHTER_GUARD_X4_OFF)),
            "guard_x8": float(_f32_at(dme, fp1 + FIGHTER_GUARD_X8_OFF)),
            "x2E8": float(_f32_at(dme, fp1 + 0x2E8)),
            "shield_health": float(_f32_at(dme, fp1 + FIGHTER_SHIELD_HEALTH_OFF)),
            "stocks": p1_stocks,
            "team": int(_u8_at(dme, fp1 + FIGHTER_TEAM_OFF)),
            "ecb": read_ecb_box(fp1, FIGHTER_ECB_TOP_OFF),
            "desired_ecb": read_ecb_box(fp1, FIGHTER_ECB_DESIRED_TOP_OFF),
            "prev_ecb": read_ecb_box(fp1, FIGHTER_ECB_PREV_TOP_OFF),
            "x64_ecb": read_ecb_box(fp1, FIGHTER_ECB_X64_TOP_OFF),
            "xE4_ecb": read_ecb_box(fp1, FIGHTER_ECB_XE4_TOP_OFF),
            "coll_flags_x34": int(_u8_at(dme, fp1 + FIGHTER_COLL_X34_FLAGS_OFF)),
            "coll_flags_x35": int(_u8_at(dme, fp1 + FIGHTER_COLL_X35_FLAGS_OFF)),
            "coll_x130_flags": int(_u32_at(dme, fp1 + FIGHTER_COLL_X130_FLAGS_OFF)),
            "coll_env_flags": int(_u32_at(dme, fp1 + FIGHTER_COLL_ENV_FLAGS_OFF)),
            "coll_prev_env_flags": int(_u32_at(dme, fp1 + FIGHTER_COLL_PREV_ENV_FLAGS_OFF)),
            "coll_floor_index": int(_u32_to_i32(_u32_at(dme, fp1 + FIGHTER_COLL_FLOOR_INDEX_OFF))),
            "coll_floor_flags": int(_u32_at(dme, fp1 + FIGHTER_COLL_FLOOR_FLAGS_OFF)),
            "hitlag_cb_ptr": int(_u32_at(dme, fp1 + FIGHTER_HITLAG_CB_OFF)),
            "pre_hitlag_cb_ptr": int(_u32_at(dme, fp1 + FIGHTER_PRE_HITLAG_CB_OFF)),
            "post_hitlag_cb_ptr": int(_u32_at(dme, fp1 + FIGHTER_POST_HITLAG_CB_OFF)),
            "hitboxes": read_hitboxes(fp1),
            "hurtboxes": p1_hurt,
        }
        if p1_joint_list:
            p1_parts_map = build_parts_map(fp1, max(128, max(p1_joint_list) + 64))
        else:
            p1_parts_map = build_parts_map(fp1, 128)
        if args.dyn_bones:
            p1["dyn_bones"] = read_dyn_bones(fp1, p1_parts_map)
        if p1_joint_list:
            p1["joints"] = {str(part): read_joint_mtx(fp1, part) for part in p1_joint_list}
            p1["joint_state"] = {
                str(part): read_joint_state(fp1, part, p1_parts_map)
                for part in p1_joint_list
            }
        p1_x166c = int(_u8_at(dme, fp1 + FIGHTER_X166C_OFF))
        if p1_x166c:
            entries = []
            for i in range(min(p1_x166c, 4)):
                base = fp1 + FIGHTER_X1670_OFF + i * 0x28
                jobj_ptr = int(_u32_at(dme, base + 0x10))
                entries.append(
                    {
                        "v1": (
                            float(_f32_at(dme, base + 0x00)),
                            float(_f32_at(dme, base + 0x04)),
                            float(_f32_at(dme, base + 0x08)),
                        ),
                        "v2": float(_f32_at(dme, base + 0x0C)),
                        "jobj": jobj_ptr,
                        "part": int(p1_parts_map.get(jobj_ptr, -1)),
                        "x14": float(_f32_at(dme, base + 0x14)),
                        "x18": (
                            float(_f32_at(dme, base + 0x18)),
                            float(_f32_at(dme, base + 0x1C)),
                            float(_f32_at(dme, base + 0x20)),
                        ),
                    }
                )
            p1["x166C"] = p1_x166c
            p1["x1670"] = entries
        else:
            p1["x166C"] = 0
        p1["ecb_source"] = read_ecb_source(fp1, p1_parts_map)
        p1["costume_joint"] = int(_u32_at(dme, fp1 + FIGHTER_COSTUME_JOINT_OFF))
        ftdata1 = int(_u32_at(dme, fp1 + FIGHTER_FTDATA_OFF))
        p1["ft_data"] = ftdata1
        p1["ft_data_joint"] = int(_u32_at(dme, ftdata1 + FTDATA_JOINT_OFF)) if ftdata1 else 0
        if args.p1_jobj_preorder:
            root = int(p1["costume_joint"])
            # Some builds/paths can leave `fp->costume_joint` pointing at a wrapper root that is
            # not part of the same JObj tree referenced by `fp->parts[]`. Prefer a root that is
            # actually an ancestor of a known `fp->parts[]` joint.
            if root == 0 or int(p1_parts_map.get(root, -1)) < 0:
                if p1_parts_map:
                    # Grab an arbitrary part joint and walk parents to the top.
                    any_jobj = int(next(iter(p1_parts_map.keys())))
                    cur = any_jobj
                    seen_up: set[int] = set()
                    while cur and cur not in seen_up:
                        seen_up.add(cur)
                        parent = int(_u32_at(dme, cur + 0x0C))
                        if not parent:
                            break
                        cur = parent
                    root = cur
            out_nodes: list[dict[str, int]] = []
            stack = [root]
            seen: set[int] = set()
            while stack and len(out_nodes) < int(args.p1_jobj_preorder):
                jobj = int(stack.pop())
                if jobj == 0 or jobj in seen:
                    continue
                seen.add(jobj)
                out_nodes.append(
                    {
                        "idx": int(len(out_nodes)),
                        "ptr": jobj,
                        "part": int(p1_parts_map.get(jobj, -1)),
                        "parent_ptr": int(_u32_at(dme, jobj + 0x0C)),
                    }
                )
                next_ptr = int(_u32_at(dme, jobj + 0x08))
                child_ptr = int(_u32_at(dme, jobj + 0x10))
                # Depth-first, child-first: push next first so child pops next.
                if next_ptr:
                    stack.append(next_ptr)
                if child_ptr:
                    stack.append(child_ptr)
            p1["jobj_preorder"] = out_nodes
        # Anim skeleton probing (ftAnim.c: fp->x8AC_animSkeleton).
        if args.p1_anim_jobj_preorder or p1_anim_joint_list:
            anim_root = int(_u32_at(dme, fp1 + FIGHTER_ANIM_SKELETON_OFF))
            p1["anim_skeleton_root"] = anim_root
            if anim_root:
                need = 0
                if args.p1_anim_jobj_preorder:
                    need = max(need, int(args.p1_anim_jobj_preorder))
                if p1_anim_joint_list:
                    need = max(need, max(p1_anim_joint_list) + 1)
                ptrs = _jobj_preorder_ptrs(dme, anim_root, max_nodes=max(need, 64))
                if args.p1_anim_jobj_preorder:
                    out_nodes = []
                    for i, ptr in enumerate(ptrs[: int(args.p1_anim_jobj_preorder)]):
                        out_nodes.append(
                            {
                                "idx": int(i),
                                "ptr": int(ptr),
                                "part": int(p1_parts_map.get(int(ptr), -1)),
                                "parent_ptr": int(_u32_at(dme, int(ptr) + 0x0C)),
                            }
                        )
                    p1["anim_jobj_preorder"] = out_nodes
                if p1_anim_joint_list:
                    out_states = {}
                    for idx in p1_anim_joint_list:
                        if 0 <= idx < len(ptrs):
                            ptr = int(ptrs[idx])
                            st = _read_jobj_state(dme, ptr)
                            if st is not None:
                                st["part"] = int(p1_parts_map.get(ptr, -1))
                            out_states[str(idx)] = st
                        else:
                            out_states[str(idx)] = None
                    p1["anim_joint_state"] = out_states
            else:
                p1["anim_jobj_preorder"] = []
                p1["anim_joint_state"] = {}
        p2 = {
            "pos": (
                float(_f32_at(dme, fp2 + FIGHTER_POS_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_POS_Y_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_POS_Z_OFF)),
            ),
            "prev_pos": (
                float(_f32_at(dme, fp2 + FIGHTER_PREV_POS_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_PREV_POS_Y_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_PREV_POS_Z_OFF)),
            ),
            "pos_delta": (
                float(_f32_at(dme, fp2 + FIGHTER_POS_DELTA_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_POS_DELTA_Y_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_POS_DELTA_Z_OFF)),
            ),
            "x38_scale": float(_f32_at(dme, fp2 + FIGHTER_SCALE_X38_OFF)),
            "entry_x28": float(_f32_at(dme, fp2 + FIGHTER_ENTRY_X28_OFF)),
            "entry_x28_bits": int(_u32_at(dme, fp2 + FIGHTER_ENTRY_X28_OFF)),
            "lstick": (
                float(_f32_at(dme, fp2 + FIGHTER_LSTICK_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_LSTICK_Y_OFF)),
            ),
            "lstick_bits": (
                int(_u32_at(dme, fp2 + FIGHTER_LSTICK_X_OFF)),
                int(_u32_at(dme, fp2 + FIGHTER_LSTICK_Y_OFF)),
            ),
            "lstick1": (
                float(_f32_at(dme, fp2 + FIGHTER_LSTICK1_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_LSTICK1_Y_OFF)),
            ),
            "lstick1_bits": (
                int(_u32_at(dme, fp2 + FIGHTER_LSTICK1_X_OFF)),
                int(_u32_at(dme, fp2 + FIGHTER_LSTICK1_Y_OFF)),
            ),
            "coll_pos": (
                float(_f32_at(dme, fp2 + FIGHTER_COLL_CUR_POS_OFF + 0x00)),
                float(_f32_at(dme, fp2 + FIGHTER_COLL_CUR_POS_OFF + 0x04)),
                float(_f32_at(dme, fp2 + FIGHTER_COLL_CUR_POS_OFF + 0x08)),
            ),
            "self_vel": (
                float(_f32_at(dme, fp2 + FIGHTER_SELF_VEL_X_OFF)),
                float(_f32_at(dme, fp2 + FIGHTER_SELF_VEL_Y_OFF)),
            ),
            "gr_vel": float(_f32_at(dme, fp2 + FIGHTER_GR_VEL_OFF)),
            "action_state": int(_u32_at(dme, fp2 + FIGHTER_ACTION_STATE_OFF)),
            "action": int(_u32_at(dme, fp2 + FIGHTER_ACTION_OFF)),
            "anim": int(_u32_at(dme, fp2 + FIGHTER_ANIM_OFF)),
            "anim_frame": float(_f32_at(dme, fp2 + FIGHTER_ANIM_FRAME_OFF)),
            "action_frame": float(_f32_at(dme, fp2 + FIGHTER_ACTION_FRAME_OFF)),
            "frame_speed_mul": float(_f32_at(dme, fp2 + FIGHTER_FRAME_SPEED_MUL_OFF)),
            "frame_speed_mul_bits": int(_u32_at(dme, fp2 + FIGHTER_FRAME_SPEED_MUL_OFF)),
            "ground_or_air": int(_u32_at(dme, fp2 + FIGHTER_GROUND_OR_AIR_OFF) & 0xFF),
            "state_xE0": int(_u32_at(dme, fp2 + FIGHTER_GROUND_OR_AIR_OFF)),
            "facing": float(_f32_at(dme, fp2 + FIGHTER_FACING_OFF)),
            "percent": float(_f32_at(dme, fp2 + FIGHTER_PERCENT_OFF)),
            "state_flags": (
                int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_2218_OFF)),
                int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_221A_OFF)),
                int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_221B_OFF)),
                int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_221C_OFF)),
                int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_221F_OFF)),
            ),
            "state_flags_2228": int(_u8_at(dme, fp2 + FIGHTER_STATE_FLAGS_2228_OFF)),
            "x670_timer_lstick_tilt_x": int(
                _u8_at(dme, fp2 + FIGHTER_X670_LSTICK_TILT_X_TIMER_OFF)
            ),
            "x671_timer_lstick_tilt_y": int(
                _u8_at(dme, fp2 + FIGHTER_X671_LSTICK_TILT_Y_TIMER_OFF)
            ),
            "x672_input_timer_counter": int(
                _u8_at(dme, fp2 + FIGHTER_X672_INPUT_TIMER_COUNTER_OFF)
            ),
            "invulnerable": p2_invul,
            "hitlag_left": float(_f32_at(dme, fp2 + FIGHTER_HITLAG_OFF)),
            # NOTE: fp+0x2340 is `misc_as` in engine dumps (not hitstun).
            "misc_as_2340": float(_f32_at(dme, fp2 + FIGHTER_HITSTUN_OFF)),
            "guard_x4": float(_f32_at(dme, fp2 + FIGHTER_GUARD_X4_OFF)),
            "guard_x8": float(_f32_at(dme, fp2 + FIGHTER_GUARD_X8_OFF)),
            "x2E8": float(_f32_at(dme, fp2 + 0x2E8)),
            "shield_health": float(_f32_at(dme, fp2 + FIGHTER_SHIELD_HEALTH_OFF)),
            "stocks": p2_stocks,
            "team": int(_u8_at(dme, fp2 + FIGHTER_TEAM_OFF)),
            "ecb": read_ecb_box(fp2, FIGHTER_ECB_TOP_OFF),
            "desired_ecb": read_ecb_box(fp2, FIGHTER_ECB_DESIRED_TOP_OFF),
            "prev_ecb": read_ecb_box(fp2, FIGHTER_ECB_PREV_TOP_OFF),
            "x64_ecb": read_ecb_box(fp2, FIGHTER_ECB_X64_TOP_OFF),
            "xE4_ecb": read_ecb_box(fp2, FIGHTER_ECB_XE4_TOP_OFF),
            "coll_flags_x34": int(_u8_at(dme, fp2 + FIGHTER_COLL_X34_FLAGS_OFF)),
            "coll_flags_x35": int(_u8_at(dme, fp2 + FIGHTER_COLL_X35_FLAGS_OFF)),
            "coll_x130_flags": int(_u32_at(dme, fp2 + FIGHTER_COLL_X130_FLAGS_OFF)),
            "coll_env_flags": int(_u32_at(dme, fp2 + FIGHTER_COLL_ENV_FLAGS_OFF)),
            "coll_prev_env_flags": int(_u32_at(dme, fp2 + FIGHTER_COLL_PREV_ENV_FLAGS_OFF)),
            "coll_floor_index": int(_u32_to_i32(_u32_at(dme, fp2 + FIGHTER_COLL_FLOOR_INDEX_OFF))),
            "coll_floor_flags": int(_u32_at(dme, fp2 + FIGHTER_COLL_FLOOR_FLAGS_OFF)),
            "hitlag_cb_ptr": int(_u32_at(dme, fp2 + FIGHTER_HITLAG_CB_OFF)),
            "pre_hitlag_cb_ptr": int(_u32_at(dme, fp2 + FIGHTER_PRE_HITLAG_CB_OFF)),
            "post_hitlag_cb_ptr": int(_u32_at(dme, fp2 + FIGHTER_POST_HITLAG_CB_OFF)),
            "hitboxes": read_hitboxes(fp2),
            "hurtboxes": p2_hurt,
        }
        if p2_joint_list:
            p2_parts_map = build_parts_map(fp2, max(128, max(p2_joint_list) + 64))
        else:
            p2_parts_map = build_parts_map(fp2, 128)
        if args.dyn_bones:
            p2["dyn_bones"] = read_dyn_bones(fp2, p2_parts_map)
        if p2_joint_list:
            p2["joints"] = {str(part): read_joint_mtx(fp2, part) for part in p2_joint_list}
            p2["joint_state"] = {
                str(part): read_joint_state(fp2, part, p2_parts_map)
                for part in p2_joint_list
            }
        p2_x166c = int(_u8_at(dme, fp2 + FIGHTER_X166C_OFF))
        if p2_x166c:
            entries = []
            for i in range(min(p2_x166c, 4)):
                base = fp2 + FIGHTER_X1670_OFF + i * 0x28
                jobj_ptr = int(_u32_at(dme, base + 0x10))
                entries.append(
                    {
                        "v1": (
                            float(_f32_at(dme, base + 0x00)),
                            float(_f32_at(dme, base + 0x04)),
                            float(_f32_at(dme, base + 0x08)),
                        ),
                        "v2": float(_f32_at(dme, base + 0x0C)),
                        "jobj": jobj_ptr,
                        "part": int(p2_parts_map.get(jobj_ptr, -1)),
                        "x14": float(_f32_at(dme, base + 0x14)),
                        "x18": (
                            float(_f32_at(dme, base + 0x18)),
                            float(_f32_at(dme, base + 0x1C)),
                            float(_f32_at(dme, base + 0x20)),
                        ),
                    }
                )
            p2["x166C"] = p2_x166c
            p2["x1670"] = entries
        else:
            p2["x166C"] = 0
        p2["ecb_source"] = read_ecb_source(fp2, p2_parts_map)
        p2["costume_joint"] = int(_u32_at(dme, fp2 + FIGHTER_COSTUME_JOINT_OFF))
        ftdata2 = int(_u32_at(dme, fp2 + FIGHTER_FTDATA_OFF))
        p2["ft_data"] = ftdata2
        p2["ft_data_joint"] = int(_u32_at(dme, ftdata2 + FTDATA_JOINT_OFF)) if ftdata2 else 0
        # Anim skeleton probing (ftAnim.c: fp->x8AC_animSkeleton).
        if args.p2_anim_jobj_preorder or p2_anim_joint_list:
            anim_root = int(_u32_at(dme, fp2 + FIGHTER_ANIM_SKELETON_OFF))
            p2["anim_skeleton_root"] = anim_root
            if anim_root:
                need = 0
                if args.p2_anim_jobj_preorder:
                    need = max(need, int(args.p2_anim_jobj_preorder))
                if p2_anim_joint_list:
                    need = max(need, max(p2_anim_joint_list) + 1)
                ptrs = _jobj_preorder_ptrs(dme, anim_root, max_nodes=max(need, 64))
                if args.p2_anim_jobj_preorder:
                    out_nodes = []
                    for i, ptr in enumerate(ptrs[: int(args.p2_anim_jobj_preorder)]):
                        out_nodes.append(
                            {
                                "idx": int(i),
                                "ptr": int(ptr),
                                "part": int(p2_parts_map.get(int(ptr), -1)),
                                "parent_ptr": int(_u32_at(dme, int(ptr) + 0x0C)),
                            }
                        )
                    p2["anim_jobj_preorder"] = out_nodes
                if p2_anim_joint_list:
                    out_states = {}
                    for idx in p2_anim_joint_list:
                        if 0 <= idx < len(ptrs):
                            ptr = int(ptrs[idx])
                            st = _read_jobj_state(dme, ptr)
                            if st is not None:
                                st["part"] = int(p2_parts_map.get(ptr, -1))
                            out_states[str(idx)] = st
                        else:
                            out_states[str(idx)] = None
                    p2["anim_joint_state"] = out_states
            else:
                p2["anim_jobj_preorder"] = []
                p2["anim_joint_state"] = {}

        row = {
            "frame": int(cur_frame),
            "frame_index": int(_u32_at(dme, FRAME_INDEX_PTR)),
            "rng_state": int(_u32_at(dme, RNG_STATE_ADDR)),
            "game_timer": int(_u32_at(dme, GAME_TIMER_ADDR)),
            "is_teams": int(_u8_at(dme, TEAMS_FLAG_ADDR)),
            "randall": None,
            "fountain": None,
            "p1": p1,
            "p2": p2,
            "items": read_items(),
        }
        if args.dyn_bones:
            row["lb_804D63B0"] = int(_u32_at(dme, LB_804D63B0_PTR))
        # Add replay fields if we have the frame in range.
        if sample is not None and frames is not None and base_frame is not None and cur_frame >= start_frame and cur_frame <= end_frame:
            idx = cur_frame - base_frame
            if 0 <= idx < len(frames) and int(frames[idx]) == cur_frame:
                row["replay"] = {
                    "p1_pos": (
                        float(sample["p1_position_x"][idx]),
                        float(sample["p1_position_y"][idx]),
                    ),
                    "p2_pos": (
                        float(sample["p2_position_x"][idx]),
                        float(sample["p2_position_y"][idx]),
                    ),
                    "p1_action": int(sample["p1_action"][idx]),
                    "p2_action": int(sample["p2_action"][idx]),
                    "p1_percent": float(sample["p1_percent"][idx]) if "p1_percent" in sample else None,
                    "p2_percent": float(sample["p2_percent"][idx]) if "p2_percent" in sample else None,
                    "p1_on_ground": bool(int(sample["p1_on_ground"][idx]) != 0) if "p1_on_ground" in sample else None,
                    "p2_on_ground": bool(int(sample["p2_on_ground"][idx]) != 0) if "p2_on_ground" in sample else None,
                }
                if "p1_animation_index" in sample:
                    row["replay"]["p1_anim"] = int(sample["p1_animation_index"][idx])
                if "p2_animation_index" in sample:
                    row["replay"]["p2_anim"] = int(sample["p2_animation_index"][idx])
                # Replay items for same frame (exists/type/state/pos).
                replay_items = []
                for j in range(MAX_ITEMS):
                    if f"item_{j}_exists" in sample and int(sample[f"item_{j}_exists"][idx]) != 0:
                        replay_items.append(
                            {
                                "slot": int(j),
                                "kind": int(sample[f"item_{j}_type"][idx]),
                                "state": int(sample[f"item_{j}_state"][idx]),
                                "pos": (
                                    float(sample[f"item_{j}_x"][idx]),
                                    float(sample[f"item_{j}_y"][idx]),
                                ),
                            }
                        )
                row["replay"]["items"] = replay_items
        return row

    out_rows = []

    if args.continuous:
        prev = None
        t_wait = time.monotonic()
        while True:
            _check_deadline("continuous_wait_start_frame")
            cur = _try_read_frame_index()
            if cur is not None and cur >= start_frame:
                break
            if time.monotonic() - t_wait > 120.0:
                raise TimeoutError("timed out waiting for playback to reach start frame (continuous)")
            time.sleep(0.01)
        frame_map: dict[int, dict] = {}
        while cur is not None and cur <= end_frame:
            _check_deadline("continuous_capture")
            if cur != prev:
                if cur >= start_frame:
                    row = capture_row(cur)
                    frame_map[int(cur)] = row
                    if args.print_every > 0 and (len(frame_map) % args.print_every == 0):
                        print("frame", cur, "p1_pos", row["p1"]["pos"][0], row["p1"]["pos"][1], flush=True)
                prev = cur
            cur = _try_read_frame_index()
            if cur is None:
                _check_deadline("continuous_capture_wait")
                time.sleep(0.001)
                continue
        out_rows = [frame_map[k] for k in sorted(frame_map.keys())]
        if args.out:
            payload = {"replay": args.replay, "rows": out_rows}
            if replay_parse_error is not None:
                payload["replay_parse_error"] = replay_parse_error
            Path(args.out).write_text(json.dumps(payload, indent=2))
            print(f"wrote {len(out_rows)} rows to {args.out}")
        if args.out_bin:
            assert sample is not None
            assert frames is not None
            assert base_frame is not None
            assert replay_inputs is not None
            _write_engine_dump(
                Path(args.out_bin),
                out_rows,
                sample,
                frames,
                base_frame,
                stage_id=int(sample["stage"][0]) if "stage" in sample else 0,
                replay_inputs=replay_inputs,
            )
            print(f"wrote binary dump to {args.out_bin}")
        if stepper is not None:
            stepper.close()
        return 0

    if use_block:
        cur = prev
        poll_snaps: list[dict] = []
        for i in range(args.frames):
            _check_deadline("block_step")
            if cur is None:
                raise RuntimeError("failed to read frame index in block-on-frame mode")
            if cur > end_frame:
                break
            row = capture_row(cur)
            out_rows.append(row)
            if args.print_every > 0 and (len(out_rows) % args.print_every == 0):
                print("frame", cur, "p1_pos", row["p1"]["pos"][0], row["p1"]["pos"][1], flush=True)
            if i == args.frames - 1:
                break
            if stepper is not None:
                poll_kind = args.poll_item_kind
                poll_joint = args.poll_p2_joint
                poll_out = args.poll_out
                poll_interval_ms = float(args.poll_interval_ms or 0.0)
                poll_max_snaps = int(args.poll_max_snaps or 0)
                poll_enabled = poll_out is not None and (poll_kind is not None or poll_joint is not None)
                poll_seen_item_ids: set[int] = set()
                if poll_enabled and poll_kind is not None:
                    try:
                        for it in read_items():
                            poll_seen_item_ids.add(int(it.get("item_id", 0)))
                    except Exception:
                        poll_seen_item_ids = set()
                stepper.send_ack(cur)
            if poll_enabled:
                t_poll0 = time.monotonic()
                snaps_this_ack = 0
                record_enabled = True
                # Poll while Dolphin runs the next frame; `FRAME_INDEX_PTR` stays at `cur` until the
                # frame completes and the hook blocks again.
                while True:
                    _check_deadline("block_poll_wait")
                    if proc.poll() is not None:
                        raise RuntimeError(f"dolphin exited (code={proc.returncode})")
                    cur_frame = _try_read_frame_index()
                    if cur_frame is not None and cur_frame != cur:
                        break
                    try:
                        if not record_enabled:
                            raise RuntimeError("poll_record_disabled")
                        fp1_now = fighter_ptr_for_port(1)
                        fp2_now = fighter_ptr_for_port(2)

                        snap: dict = {
                            "prev_frame": int(cur),
                            "t_since_ack_s": float(time.monotonic() - t_poll0),
                            "p1": {
                                "pos_bits": (
                                    int(_u32_at(dme, fp1_now + FIGHTER_POS_X_OFF)),
                                    int(_u32_at(dme, fp1_now + FIGHTER_POS_Y_OFF)),
                                    int(_u32_at(dme, fp1_now + FIGHTER_POS_Z_OFF)),
                                ),
                                "prev_pos_bits": (
                                    int(_u32_at(dme, fp1_now + FIGHTER_PREV_POS_X_OFF)),
                                    int(_u32_at(dme, fp1_now + FIGHTER_PREV_POS_Y_OFF)),
                                    int(_u32_at(dme, fp1_now + FIGHTER_PREV_POS_Z_OFF)),
                                ),
                                "action_frame_bits": int(_u32_at(dme, fp1_now + FIGHTER_ACTION_FRAME_OFF)),
                            },
                            "p2": {
                                "pos_bits": (
                                    int(_u32_at(dme, fp2_now + FIGHTER_POS_X_OFF)),
                                    int(_u32_at(dme, fp2_now + FIGHTER_POS_Y_OFF)),
                                    int(_u32_at(dme, fp2_now + FIGHTER_POS_Z_OFF)),
                                ),
                                "prev_pos_bits": (
                                    int(_u32_at(dme, fp2_now + FIGHTER_PREV_POS_X_OFF)),
                                    int(_u32_at(dme, fp2_now + FIGHTER_PREV_POS_Y_OFF)),
                                    int(_u32_at(dme, fp2_now + FIGHTER_PREV_POS_Z_OFF)),
                                ),
                                "action_frame_bits": int(_u32_at(dme, fp2_now + FIGHTER_ACTION_FRAME_OFF)),
                            },
                        }

                        if poll_joint is not None:
                            try:
                                snap["p2_joint_mtx"] = {
                                    str(int(poll_joint)): read_joint_mtx(fp2_now, int(poll_joint))
                                }
                            except Exception:
                                pass

                        if poll_kind is not None:
                            try:
                                items_now = read_items()
                                hit = None
                                hit_new = False
                                for it in items_now:
                                    if int(it.get("kind", -1)) != int(poll_kind):
                                        continue
                                    iid = int(it.get("item_id", 0))
                                    hit = it
                                    if iid != 0 and iid not in poll_seen_item_ids:
                                        hit_new = True
                                        poll_seen_item_ids.add(iid)
                                    break
                                if hit is not None:
                                    snap["item"] = hit
                                    snap["item_new"] = hit_new
                            except Exception:
                                pass

                        poll_snaps.append(snap)
                        snaps_this_ack += 1
                        if poll_max_snaps > 0 and snaps_this_ack >= poll_max_snaps:
                            record_enabled = False
                    except Exception:
                        pass
                    if poll_interval_ms > 0:
                        time.sleep(poll_interval_ms / 1000.0)
                    else:
                        time.sleep(0.001)
                nxt = cur_frame
            else:
                nxt = _wait_for_frame_change(cur, timeout_s=2.0)
            if nxt is None:
                raise TimeoutError(f"frame did not advance after ack (prev={cur}, rows={len(out_rows)})")
            prev = cur
            cur = nxt
        if args.poll_out and poll_snaps:
            Path(args.poll_out).write_text(json.dumps({"replay": args.replay, "poll": poll_snaps}, indent=2))
            print(f"wrote {len(poll_snaps)} poll snapshots to {args.poll_out}")
    else:
        for _ in range(args.frames):
            _check_deadline("step")
            advanced = False
            for _try in range(5):
                target = (prev + 1) if prev is not None else None
                ack = stepper.send_step(1, target_frame=target)
                ack_cur = _ack_frame(ack)
                # wait for frame index to advance
                t1 = time.monotonic()
                while True:
                    _check_deadline("step_wait_frame")
                    if proc.poll() is not None:
                        raise RuntimeError(f"dolphin exited (code={proc.returncode})")
                    cur = _try_read_frame_index()
                    if cur is not None and cur != prev:
                        advanced = True
                        break
                    if time.monotonic() - t1 > 1.0:
                        break
                    time.sleep(0.001)
                if not advanced and ack_cur is not None and ack_cur != prev:
                    cur = ack_cur
                    advanced = True
                if advanced:
                    break
                time.sleep(0.05)
            if not advanced:
                raise TimeoutError(f"frame did not advance after retries (prev={prev}, rows={len(out_rows)})")
            prev = cur
            row = capture_row(cur)
            out_rows.append(row)

            if args.print_every > 0 and (len(out_rows) % args.print_every == 0):
                print("frame", cur, "p1_pos", row["p1"]["pos"][0], row["p1"]["pos"][1], flush=True)

    if args.out:
        payload = {"replay": args.replay, "rows": out_rows}
        if replay_parse_error is not None:
            payload["replay_parse_error"] = replay_parse_error
        Path(args.out).write_text(json.dumps(payload, indent=2))
        print(f"wrote {len(out_rows)} rows to {args.out}")
    if args.out_bin:
        assert sample is not None
        assert frames is not None
        assert base_frame is not None
        assert replay_inputs is not None
        _write_engine_dump(
            Path(args.out_bin),
            out_rows,
            sample,
            frames,
            base_frame,
            stage_id=int(sample["stage"][0]) if "stage" in sample else 0,
            replay_inputs=replay_inputs,
        )
        print(f"wrote binary dump to {args.out_bin}")

    if stepper is not None:
        stepper.close()
    return 0

def _write_engine_dump(
    out_path: Path,
    rows: list[dict],
    sample: dict,
    frames: np.ndarray,
    base_frame: int,
    stage_id: int,
    replay_inputs: dict[int, dict[str, np.ndarray]],
) -> None:
    # Validate contiguous frames.
    if not rows:
        raise ValueError("no rows to write")
    frame_indices = [int(r["frame"]) for r in rows]
    for i in range(1, len(frame_indices)):
        if frame_indices[i] != frame_indices[i - 1] + 1:
            raise ValueError(f"non-contiguous frames at {i}: {frame_indices[i-1]} -> {frame_indices[i]}")

    port_count = 2
    frame_count = len(rows)

    frames_out = []
    fighters_out = []
    inputs_out = []
    items_out = []
    hitboxes_out = []
    hurtboxes_out = []

    # Build per-frame records.
    for r in rows:
        frame_idx = int(r["frame"])
        idx = frame_idx - base_frame
        if idx < 0 or idx >= len(frames):
            raise ValueError(f"frame {frame_idx} out of replay range")
        if int(frames[idx]) != frame_idx:
            raise ValueError(f"replay frame mismatch at {idx}: {int(frames[idx])} != {frame_idx}")

        frame_rec = {
            "frame_index": frame_idx,
            "rng_state": int(r.get("rng_state", 0)),
            "game_timer": int(r.get("game_timer", 0)),
            "item_offset": len(items_out),
            "randall": r.get("randall"),
            "fountain": r.get("fountain"),
        }

        # Fighters (port order 1,2).
        for port in (1, 2):
            p = r[f"p{port}"]
            fighters_out.append(
                {
                    "flags": 0,
                    "pos": p["pos"],
                    "self_vel": p["self_vel"],
                    "gr_vel": p["gr_vel"],
                    "action": int(p["action"]),
                    "anim": int(p["anim"]),
                    "anim_frame": float(p["anim_frame"]),
                    "action_frame": float(p["action_frame"]),
                    "ground": int(p["ground_or_air"]),
                    "facing": float(p["facing"]),
                    "percent": float(p["percent"]),
                    "state_flags": p["state_flags"],
                    "invulnerable": int(p["invulnerable"]),
                    "hitlag_left": float(p["hitlag_left"]),
                    "hitstun_left": float(p["hitstun_left"]),
                    "shield_health": float(p["shield_health"]),
                    "stocks": int(p["stocks"]),
                    "team": int(p["team"]),
                    "ecb": p["ecb"],
                }
            )
            hitboxes_out.extend(p["hitboxes"])
            hurtboxes_out.extend(p["hurtboxes"])

        # Items.
        frame_items = r.get("items", [])
        frame_rec["item_count"] = len(frame_items)
        for it in frame_items:
            items_out.append(it)

        frames_out.append(frame_rec)

        # Inputs (same frame index).
        for port in (1, 2):
            pdata = replay_inputs[port]
            mask = 0
            if int(pdata["a"][idx]):
                mask |= 0x0100
            if int(pdata["b"][idx]):
                mask |= 0x0200
            if int(pdata["x"][idx]):
                mask |= 0x0400
            if int(pdata["y"][idx]):
                mask |= 0x0800
            if int(pdata["z"][idx]):
                mask |= 0x0010
            if int(pdata["l"][idx]):
                mask |= 0x0040
            if int(pdata["r"][idx]):
                mask |= 0x0020
            if int(pdata["start"][idx]):
                mask |= 0x1000
            if int(pdata["d_up"][idx]):
                mask |= 0x0008
            l_sh = float(pdata["l_shoulder"][idx])
            r_sh = float(pdata["r_shoulder"][idx])
            if mask & (0x0040 | 0x0020) or l_sh > 0.0 or r_sh > 0.0:
                mask |= 1 << 31
            inputs_out.append(
                {
                    "buttons": mask,
                    "stick_x": float(pdata["stick_x"][idx]),
                    "stick_y": float(pdata["stick_y"][idx]),
                    "cstick_x": float(pdata["cstick_x"][idx]),
                    "cstick_y": float(pdata["cstick_y"][idx]),
                    "l_shoulder": l_sh,
                    "r_shoulder": r_sh,
                }
            )

    frame_rec_size = 40
    input_rec_size = 28
    fighter_rec_size = 104
    item_rec_size = 54
    hitbox_rec_size = 88
    hurtbox_rec_size = 68

    frames_offset = 80
    inputs_offset = frames_offset + frame_count * frame_rec_size
    fighters_offset = inputs_offset + frame_count * port_count * input_rec_size
    items_offset = fighters_offset + frame_count * port_count * fighter_rec_size
    hitboxes_offset = items_offset + len(items_out) * item_rec_size
    hurtboxes_offset = hitboxes_offset + frame_count * port_count * 4 * hitbox_rec_size

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(b"MSIMDMP\0")
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<I", 0x01020304))
        f.write(struct.pack("<I", frame_count))
        f.write(struct.pack("<B", port_count))
        f.write(struct.pack("<H", int(stage_id) & 0xFFFF))
        f.write(struct.pack("<B", int(rows[0].get("is_teams", 0)) & 0xFF))
        f.write(b"\x00")  # reserved0
        f.write(b"\x00" * 16)  # replay_uuid (unknown)
        f.write(struct.pack("<I", frames_offset))
        f.write(struct.pack("<I", inputs_offset))
        f.write(struct.pack("<I", fighters_offset))
        f.write(struct.pack("<I", items_offset))
        f.write(struct.pack("<I", hitboxes_offset))
        f.write(struct.pack("<I", hurtboxes_offset))
        f.write(struct.pack("<I", len(items_out)))
        f.write(b"\x00" * 11)  # reserved1
        pad = frames_offset - f.tell()
        if pad < 0:
            raise RuntimeError("header layout exceeded frame offset")
        f.write(b"\x00" * pad)

        # Frame records.
        for fr in frames_out:
            f.write(struct.pack("<i", int(fr["frame_index"])))
            f.write(struct.pack("<I", int(fr.get("rng_state", 0))))
            f.write(struct.pack("<H", int(fr["item_count"])))
            f.write(struct.pack("<I", int(fr["item_offset"])))
            f.write(struct.pack("<H", 0))
            f.write(struct.pack("<I", int(fr.get("game_timer", 0))))
            randall = fr.get("randall")
            if randall is None:
                f.write(struct.pack("<B", 0))
                f.write(struct.pack("<I", 0))
                f.write(struct.pack("<I", 0))
            else:
                f.write(struct.pack("<B", 1))
                f.write(struct.pack("<I", _f32_bits(randall[0])))
                f.write(struct.pack("<I", _f32_bits(randall[1])))
            fountain = fr.get("fountain") or [None, None]
            for plat in fountain:
                if plat is None:
                    f.write(struct.pack("<B", 0))
                    f.write(struct.pack("<I", 0))
                else:
                    f.write(struct.pack("<B", 1))
                    f.write(struct.pack("<I", _f32_bits(float(plat))))
            f.write(struct.pack("<B", 0))

        # Input records.
        for inp in inputs_out:
            f.write(struct.pack("<I", int(inp["buttons"])))
            f.write(struct.pack("<I", _f32_bits(inp["stick_x"])))
            f.write(struct.pack("<I", _f32_bits(inp["stick_y"])))
            f.write(struct.pack("<I", _f32_bits(inp["cstick_x"])))
            f.write(struct.pack("<I", _f32_bits(inp["cstick_y"])))
            f.write(struct.pack("<I", _f32_bits(inp["l_shoulder"])))
            f.write(struct.pack("<I", _f32_bits(inp["r_shoulder"])))

        # Fighter records.
        for ft in fighters_out:
            f.write(struct.pack("<I", int(ft["flags"])))
            f.write(struct.pack("<I", _f32_bits(ft["pos"][0])))
            f.write(struct.pack("<I", _f32_bits(ft["pos"][1])))
            f.write(struct.pack("<I", _f32_bits(ft["pos"][2])))
            f.write(struct.pack("<I", _f32_bits(ft["self_vel"][0])))
            f.write(struct.pack("<I", _f32_bits(ft["self_vel"][1])))
            f.write(struct.pack("<I", _f32_bits(ft["gr_vel"])))
            f.write(struct.pack("<H", int(ft["action"]) & 0xFFFF))
            f.write(struct.pack("<H", int(ft["anim"]) & 0xFFFF))
            f.write(struct.pack("<I", _f32_bits(ft["anim_frame"])))
            f.write(struct.pack("<I", _f32_bits(ft["action_frame"])))
            sf = ft["state_flags"]
            f.write(struct.pack("<B", int(sf[0]) & 0xFF))
            f.write(struct.pack("<B", int(sf[1]) & 0xFF))
            f.write(struct.pack("<B", int(sf[2]) & 0xFF))
            f.write(struct.pack("<B", int(sf[3]) & 0xFF))
            f.write(struct.pack("<B", int(sf[4]) & 0xFF))
            f.write(struct.pack("<B", int(ft["invulnerable"]) & 0xFF))
            f.write(struct.pack("<B", int(ft["ground"]) & 0xFF))
            f.write(struct.pack("<B", int(ft["stocks"]) & 0xFF))
            f.write(struct.pack("<B", int(ft["team"]) & 0xFF))
            f.write(struct.pack("<B", 0))
            f.write(struct.pack("<I", _f32_bits(ft["facing"])))
            f.write(struct.pack("<I", _f32_bits(ft["percent"])))
            f.write(struct.pack("<I", _f32_bits(ft["hitlag_left"])))
            f.write(struct.pack("<I", _f32_bits(ft["hitstun_left"])))
            f.write(struct.pack("<I", _f32_bits(ft["shield_health"])))
            ecb = ft["ecb"]
            f.write(struct.pack("<I", _f32_bits(ecb["top"][0])))
            f.write(struct.pack("<I", _f32_bits(ecb["top"][1])))
            f.write(struct.pack("<I", _f32_bits(ecb["bottom"][0])))
            f.write(struct.pack("<I", _f32_bits(ecb["bottom"][1])))
            f.write(struct.pack("<I", _f32_bits(ecb["left"][0])))
            f.write(struct.pack("<I", _f32_bits(ecb["left"][1])))
            f.write(struct.pack("<I", _f32_bits(ecb["right"][0])))
            f.write(struct.pack("<I", _f32_bits(ecb["right"][1])))
            f.write(struct.pack("<H", 0))

        # Item records.
        for it in items_out:
            f.write(struct.pack("<I", int(it["item_id"])))
            f.write(struct.pack("<H", int(it["kind"]) & 0xFFFF))
            f.write(struct.pack("<H", int(it["state"]) & 0xFFFF))
            f.write(struct.pack("<b", int(it["owner"])))
            f.write(struct.pack("<B", 0))
            f.write(struct.pack("<I", _f32_bits(it["pos"][0])))
            f.write(struct.pack("<I", _f32_bits(it["pos"][1])))
            f.write(struct.pack("<I", _f32_bits(it["pos"][2])))
            f.write(struct.pack("<I", _f32_bits(it["vel"][0])))
            f.write(struct.pack("<I", _f32_bits(it["vel"][1])))
            f.write(struct.pack("<I", _f32_bits(it["vel"][2])))
            f.write(struct.pack("<I", _f32_bits(it["facing"])))
            f.write(struct.pack("<H", int(it["anim_id"]) & 0xFFFF))
            f.write(struct.pack("<H", 0))
            f.write(struct.pack("<I", _f32_bits(it["anim_frame"])))
            f.write(struct.pack("<I", _f32_bits(it["lifetime"])))
            f.write(struct.pack("<I", int(it["damage"]) & 0xFFFF_FFFF))

        # Hitbox records.
        for hb in hitboxes_out:
            f.write(struct.pack("<I", int(hb["state"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["group"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["damage"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", _f32_bits(hb["damage_stale"])))
            f.write(struct.pack("<I", _f32_bits(hb["offset"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["offset"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["offset"][2])))
            f.write(struct.pack("<I", _f32_bits(hb["size"])))
            f.write(struct.pack("<I", int(hb["angle"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["kbg"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["wsk"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["bkb"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["element"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["shield_damage"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["sfx"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", int(hb["sfx_kind"]) & 0xFFFF_FFFF))
            for flag in hb["flags"]:
                f.write(struct.pack("<B", int(flag) & 0xFF))
            f.write(struct.pack("<I", int(hb["bone_ptr"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", _f32_bits(hb["pos"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["pos"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["pos"][2])))

        # Hurtbox records.
        for hb in hurtboxes_out:
            f.write(struct.pack("<I", int(hb["state"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<I", _f32_bits(hb["a_offset"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["a_offset"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["a_offset"][2])))
            f.write(struct.pack("<I", _f32_bits(hb["b_offset"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["b_offset"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["b_offset"][2])))
            f.write(struct.pack("<I", _f32_bits(hb["scale"])))
            f.write(struct.pack("<I", _f32_bits(hb["a_pos"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["a_pos"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["a_pos"][2])))
            f.write(struct.pack("<I", _f32_bits(hb["b_pos"][0])))
            f.write(struct.pack("<I", _f32_bits(hb["b_pos"][1])))
            f.write(struct.pack("<I", _f32_bits(hb["b_pos"][2])))
            f.write(struct.pack("<i", int(hb["bone_idx"])))
            f.write(struct.pack("<I", int(hb["height"]) & 0xFFFF_FFFF))
            f.write(struct.pack("<B", int(hb["is_grabbable"]) & 0xFF))
            f.write(struct.pack("<B", int(hb["flags"]) & 0xFF))
            f.write(struct.pack("<H", 0))


if __name__ == "__main__":
    raise SystemExit(main())
