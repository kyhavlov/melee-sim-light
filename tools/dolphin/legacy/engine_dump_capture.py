#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import struct
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import peppi_bytes
from melee_sim.metrics import MASK_F32, canonicalize_slippi_sample_last
from melee_sim.replay_io import read_replay_bytes

LIBMELEE_ROOT_DEFAULT = Path("/media/kyle/Windows/Users/kyleh/git/libmelee")
ENGINE_DUMP_MAGIC = b"MSIMDMP\0"
ENGINE_DUMP_ENDIAN_TAG = 0x01020304
ENGINE_DUMP_VERSION = 0


def _add_libmelee_to_path(root: Path) -> None:
    root = root.resolve()
    if not root.exists():
        raise FileNotFoundError(f"libmelee path does not exist: {root}")
    import sys

    sys.path.insert(0, str(root))


def _ensure_extracted_dolphin(appimage: Path) -> Path:
    appimage = appimage.resolve()
    if not appimage.exists():
        raise FileNotFoundError(f"missing AppImage: {appimage}")

    squash = Path.cwd() / "squashfs-root"
    dolphin_bin = squash / "usr" / "bin" / "dolphin-emu"
    if not dolphin_bin.exists():
        import subprocess

        subprocess.run([str(appimage), "--appimage-extract"], check=True)
        if not dolphin_bin.exists():
            raise RuntimeError(f"expected extracted Dolphin binary at {dolphin_bin}")

    out_dir = Path.cwd() / ".local_ExiAI"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "dolphin-emu"
    if out.exists() or out.is_symlink():
        out.unlink()
    out.symlink_to(dolphin_bin)
    return out


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


@dataclass(frozen=True)
class ReplayInputs:
    frames: np.ndarray  # int32
    p1: dict[str, np.ndarray]
    p2: dict[str, np.ndarray]


def load_replay_inputs(replay: str) -> ReplayInputs:
    rb = read_replay_bytes(replay)
    sample = canonicalize_slippi_sample_last(peppi_bytes.read_slippi_bytes_sample(rb, 0, False))
    frames = sample["frame"].astype(np.int32, copy=False)

    def p(port: int) -> dict[str, np.ndarray]:
        pfx = f"p{port}"
        out: dict[str, np.ndarray] = {}
        out["stick_x"] = _as_unit_f32(sample[f"{pfx}_main_stick_x"])
        out["stick_y"] = _as_unit_f32(sample[f"{pfx}_main_stick_y"])
        out["cstick_x"] = _as_unit_f32(sample[f"{pfx}_c_stick_x"])
        out["cstick_y"] = _as_unit_f32(sample[f"{pfx}_c_stick_y"])
        out["l_shoulder"] = _as_trigger(sample[f"{pfx}_l_shoulder"]) if f"{pfx}_l_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        out["r_shoulder"] = _as_trigger(sample[f"{pfx}_r_shoulder"]) if f"{pfx}_r_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        # Digital buttons (bool -> u8) for press/release
        def _b(name: str) -> np.ndarray:
            if f"{pfx}_button_{name}" not in sample:
                return np.zeros_like(frames, dtype=np.uint8)
            return (sample[f"{pfx}_button_{name}"] != 0).astype(np.uint8, copy=False)

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

    return ReplayInputs(frames=frames, p1=p(1), p2=p(2))


BUTTON_A = 0x0100
BUTTON_B = 0x0200
BUTTON_X = 0x0400
BUTTON_Y = 0x0800
BUTTON_Z = 0x0010
BUTTON_L = 0x0040
BUTTON_R = 0x0020
BUTTON_START = 0x1000
BUTTON_D_UP = 0x0008  # D-pad up


def _buttons_mask(p: dict[str, np.ndarray], idx: int) -> int:
    mask = 0
    if int(p["a"][idx]):
        mask |= BUTTON_A
    if int(p["b"][idx]):
        mask |= BUTTON_B
    if int(p["x"][idx]):
        mask |= BUTTON_X
    if int(p["y"][idx]):
        mask |= BUTTON_Y
    if int(p["z"][idx]):
        mask |= BUTTON_Z
    if int(p["l"][idx]):
        mask |= BUTTON_L
    if int(p["r"][idx]):
        mask |= BUTTON_R
    if int(p["start"][idx]):
        mask |= BUTTON_START
    if int(p["d_up"][idx]):
        mask |= BUTTON_D_UP
    return mask


def _f32_bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def main() -> int:
    ap = argparse.ArgumentParser(description="Capture a binary engine-dump replay from live Dolphin state.")
    ap.add_argument("--replay", required=True, type=str, help="path to .slp replay for inputs")
    ap.add_argument("--out", required=True, type=str, help="output .bin path")
    ap.add_argument("--dolphin", default=None, type=str, help="path to dolphin-emu (hookable by dolphin_memory_engine)")
    ap.add_argument("--appimage", default=str(Path.cwd() / "Slippi_Online-x86_64-ExiAI.AppImage"), type=str)
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--libmelee", default=str(LIBMELEE_ROOT_DEFAULT))
    ap.add_argument("--start_frame", type=int, default=None)
    ap.add_argument("--end_frame", type=int, default=None)
    ap.add_argument("--max_seconds", type=float, default=240.0)
    ap.add_argument("--max_menu_steps", type=int, default=6000)
    args = ap.parse_args()

    _add_libmelee_to_path(Path(args.libmelee))
    import melee  # noqa: E402
    from melee import enums  # noqa: E402
    import dolphin_memory_engine as dme  # noqa: E402

    r = load_replay_inputs(args.replay)
    base_frame = int(r.frames[0])
    capture_start = base_frame if args.start_frame is None else int(args.start_frame)
    capture_end = int(r.frames[-1]) if args.end_frame is None else int(args.end_frame)
    if capture_start < base_frame or capture_end > int(r.frames[-1]):
        raise ValueError("frame window outside replay frames")

    dolphin_path = Path(args.dolphin) if args.dolphin is not None else _ensure_extracted_dolphin(Path(args.appimage))
    iso_path = Path(args.iso).resolve()
    if not iso_path.exists():
        raise FileNotFoundError(f"missing ISO: {iso_path}")

    t0 = time.monotonic()
    console = None
    controllers = None
    try:
        console = melee.Console(
            path=str(dolphin_path),
            slippi_address="127.0.0.1",
            online_delay=0,
            blocking_input=True,
            copy_home_directory=False,
            setup_gecko_codes=True,
            save_replays=False,
            use_exi_inputs=True,
            enable_ffw=True,
            gfx_backend="Null",
            disable_audio=True,
            infinite_time=False,
            fullscreen=False,
        )
        controllers = {
            1: melee.Controller(console=console, port=1, type=enums.ControllerType.STANDARD),
            2: melee.Controller(console=console, port=2, type=enums.ControllerType.STANDARD),
        }
        menu = melee.MenuHelper(is_singles=True)

        console.run(iso_path=str(iso_path))
        if not console.connect():
            raise RuntimeError("failed to connect to dolphin slippstream")
        for c in controllers.values():
            if not c.connect():
                raise RuntimeError(f"failed to connect controller port {c.port}")

        # Hook dolphin_memory_engine.
        while True:
            dme.hook()
            if dme.is_hooked():
                break
            if time.monotonic() - t0 > min(args.max_seconds, 30.0):
                raise RuntimeError(f"dolphin_memory_engine failed to hook (status={dme.get_status()})")
            time.sleep(0.05)

        # Menu to game.
        p1_char = enums.Character.FOX
        p2_char = enums.Character.FALCO
        stage = enums.Stage.FINAL_DESTINATION
        gamestate = None
        menu_steps = 0
        while True:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while menuing")
            if menu_steps > args.max_menu_steps:
                raise TimeoutError("exceeded max menu steps")

            gamestate = console.step()
            menu_steps += 1
            if gamestate is None:
                continue
            if gamestate.menu_state in [enums.Menu.IN_GAME, enums.Menu.SUDDEN_DEATH]:
                break
            menu.menu_helper_simple(
                gamestate,
                controllers[1],
                character_selected=p1_char,
                stage_selected=stage,
                autostart=True,
                costume=0,
                connect_code="",
            )
            menu.menu_helper_simple(
                gamestate,
                controllers[2],
                character_selected=p2_char,
                stage_selected=stage,
                autostart=True,
                costume=1,
                connect_code="",
            )

        assert gamestate is not None

        # Memory helpers (big-endian PPC).
        def f32_at(addr: int) -> float:
            bits = int(dme.read_word(addr)) & 0xFFFF_FFFF
            return struct.unpack(">f", struct.pack(">I", bits))[0]

        def u8_at(addr: int) -> int:
            return int(dme.read_byte(addr)) & 0xFF

        def u32_at(addr: int) -> int:
            return int(dme.read_word(addr)) & 0xFFFF_FFFF

        PLAYER_SLOTS = 0x80453080
        STATIC_PLAYER_SIZE = 0xE90
        GOBJ_USER_DATA_OFF = 0x2C
        # Fighter struct fields.
        FIGHTER_POS_X_OFF = 0xB0
        FIGHTER_POS_Y_OFF = 0xB4
        FIGHTER_POS_Z_OFF = 0xB8
        FIGHTER_SELF_VEL_X_OFF = 0x80
        FIGHTER_SELF_VEL_Y_OFF = 0x84
        FIGHTER_SELF_VEL_Y_OFF = 0x84
        FIGHTER_GR_VEL_OFF = 0xEC
        FIGHTER_MOTION_ID_OFF = 0x10
        FIGHTER_ANIM_ID_OFF = 0x14
        FIGHTER_CUR_ANIM_FRAME_OFF = 0x894
        FIGHTER_GROUND_OR_AIR_OFF = 0xE0
        FIGHTER_FACING_OFF = 0x2C
        FIGHTER_PERCENT_OFF = 0x1830

        # Item manager
        R13_BASE = 0x804DB6A0
        ITEM_MANAGER_PTR = R13_BASE - 0x3E74
        ITEM_MANAGER_FIRST_GOBJ_OFF = 0x24
        GOBJ_NEXT_OFF = 0x08
        ITEM_GOBJ_USERDATA_OFF = 0x2C
        ITEM_POS_OFF = 0x50
        ITEM_VEL_OFF = 0x5C
        ITEM_KIND_OFF = 0x10
        ITEM_STATE_OFF = 0x14
        ITEM_OWNER_OFF = 0x3C
        MAX_ITEMS = 15

        def fighter_ptr_for_port(port: int) -> int:
            slot = port - 1
            base = PLAYER_SLOTS + slot * STATIC_PLAYER_SIZE
            transformed0 = u8_at(base + 0x0C)
            gobj_ptr = u32_at(base + 0xB0 + transformed0 * 4)
            return u32_at(gobj_ptr + GOBJ_USER_DATA_OFF)

        def read_items() -> list[dict]:
            items: list[dict] = []
            mgr = u32_at(ITEM_MANAGER_PTR)
            if mgr == 0:
                return items
            gobj = u32_at(mgr + ITEM_MANAGER_FIRST_GOBJ_OFF)
            seen = 0
            while gobj != 0 and seen < MAX_ITEMS:
                seen += 1
                user = u32_at(gobj + ITEM_GOBJ_USERDATA_OFF)
                if user != 0:
                    pos_x = f32_at(user + ITEM_POS_OFF + 0x00)
                    pos_y = f32_at(user + ITEM_POS_OFF + 0x04)
                    pos_z = f32_at(user + ITEM_POS_OFF + 0x08)
                    vel_x = f32_at(user + ITEM_VEL_OFF + 0x00)
                    vel_y = f32_at(user + ITEM_VEL_OFF + 0x04)
                    vel_z = f32_at(user + ITEM_VEL_OFF + 0x08)
                    kind = u32_at(user + ITEM_KIND_OFF) & 0xFFFF
                    state = u32_at(user + ITEM_STATE_OFF) & 0xFFFF
                    owner = int(struct.unpack(">b", bytes([u8_at(user + ITEM_OWNER_OFF)]))[0])
                    items.append(
                        {
                            "item_id": user,
                            "kind": kind,
                            "state": state,
                            "owner": owner,
                            "pos": (pos_x, pos_y, pos_z),
                            "vel": (vel_x, vel_y, vel_z),
                        }
                    )
                gobj = u32_at(gobj + GOBJ_NEXT_OFF)
            return items

        # Align to replay start frame.
        while int(gamestate.frame) < base_frame:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while aligning to replay start frame")
            gamestate = console.step()
            if gamestate is None:
                continue
        if int(gamestate.frame) != base_frame:
            raise RuntimeError(f"cannot align: game.frame={int(gamestate.frame)} but replay starts at {base_frame}")

        frames = []
        fighters = []
        inputs = []
        items = []

        while True:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while running input loop")
            frame = int(gamestate.frame)
            if frame > capture_end:
                break
            idx = frame - base_frame
            if idx < 0 or idx >= len(r.frames):
                break
            if int(r.frames[idx]) != frame:
                raise RuntimeError(f"frame/index mismatch: replay.frames[{idx}]={int(r.frames[idx])} but game.frame={frame}")

            do_capture = capture_start <= frame <= capture_end
            if do_capture:
                frame_rec = {
                    "frame_index": frame,
                    "item_offset": len(items),
                }

            if do_capture:
                # Fighters.
                for port in (1, 2):
                    fp = fighter_ptr_for_port(port)
                    if fp == 0:
                        fighters.append(
                            {
                                "flags": 0,
                                "pos": (0.0, 0.0, 0.0),
                                "self_vel": (0.0, 0.0),
                                "gr_vel": 0.0,
                                "action": 0,
                                "anim": 0,
                                "anim_frame": 0.0,
                                "ground": 0,
                                "facing": 0.0,
                                "percent": 0.0,
                            }
                        )
                        continue
                    pos_x = f32_at(fp + FIGHTER_POS_X_OFF)
                    pos_y = f32_at(fp + FIGHTER_POS_Y_OFF)
                    pos_z = f32_at(fp + FIGHTER_POS_Z_OFF)
                    self_vx = f32_at(fp + FIGHTER_SELF_VEL_X_OFF)
                    self_vy = f32_at(fp + FIGHTER_SELF_VEL_Y_OFF)
                    gr_v = f32_at(fp + FIGHTER_GR_VEL_OFF)
                    motion = u32_at(fp + FIGHTER_MOTION_ID_OFF) & 0xFFFF
                    anim = u32_at(fp + FIGHTER_ANIM_ID_OFF) & 0xFFFF
                    anim_frame = f32_at(fp + FIGHTER_CUR_ANIM_FRAME_OFF)
                    ground = u8_at(fp + FIGHTER_GROUND_OR_AIR_OFF)
                    facing = f32_at(fp + FIGHTER_FACING_OFF)
                    percent = f32_at(fp + FIGHTER_PERCENT_OFF)
                    fighters.append(
                        {
                            "flags": 0,
                            "pos": (pos_x, pos_y, pos_z),
                            "self_vel": (self_vx, self_vy),
                            "gr_vel": gr_v,
                            "action": motion,
                            "anim": anim,
                            "anim_frame": anim_frame,
                            "ground": ground,
                            "facing": facing,
                            "percent": percent,
                        }
                    )

                # Items.
                frame_items = read_items()
                frame_rec["item_count"] = len(frame_items)
                for it in frame_items:
                    items.append(it)

                frames.append(frame_rec)

            # Inputs for next frame.
            if do_capture:
                for port, pdata in ((1, r.p1), (2, r.p2)):
                    mask = _buttons_mask(pdata, idx)
                    inputs.append(
                        {
                            "buttons": mask,
                            "stick_x": float(pdata["stick_x"][idx]),
                            "stick_y": float(pdata["stick_y"][idx]),
                            "cstick_x": float(pdata["cstick_x"][idx]),
                            "cstick_y": float(pdata["cstick_y"][idx]),
                            "l_shoulder": float(pdata["l_shoulder"][idx]),
                            "r_shoulder": float(pdata["r_shoulder"][idx]),
                        }
                    )

            # Apply inputs for the next frame and step.
            for port, ctrl in controllers.items():
                pdata = r.p1 if port == 1 else r.p2
                ctrl.tilt_analog_unit(enums.Button.BUTTON_MAIN, float(pdata["stick_x"][idx]), float(pdata["stick_y"][idx]))
                ctrl.tilt_analog_unit(enums.Button.BUTTON_C, float(pdata["cstick_x"][idx]), float(pdata["cstick_y"][idx]))
                ctrl.press_shoulder(enums.Button.BUTTON_L, float(pdata["l_shoulder"][idx]))
                ctrl.press_shoulder(enums.Button.BUTTON_R, float(pdata["r_shoulder"][idx]))
                # Buttons.
                def set_btn(name: str, b: enums.Button) -> None:
                    pressed = int(pdata[name][idx]) != 0
                    if pressed:
                        ctrl.press_button(b)
                    else:
                        ctrl.release_button(b)

                set_btn("a", enums.Button.BUTTON_A)
                set_btn("b", enums.Button.BUTTON_B)
                set_btn("x", enums.Button.BUTTON_X)
                set_btn("y", enums.Button.BUTTON_Y)
                set_btn("z", enums.Button.BUTTON_Z)
                set_btn("l", enums.Button.BUTTON_L)
                set_btn("r", enums.Button.BUTTON_R)
                set_btn("start", enums.Button.BUTTON_START)
                set_btn("d_up", enums.Button.BUTTON_D_UP)

            gamestate = console.step()
            if gamestate is None:
                break

        # Write binary dump.
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        frame_count = len(frames)
        port_count = 2

        frame_rec_size = 16
        input_rec_size = 28
        fighter_rec_size = 48
        item_rec_size = 34

        frames_offset = 64
        inputs_offset = frames_offset + frame_count * frame_rec_size
        fighters_offset = inputs_offset + frame_count * port_count * input_rec_size
        items_offset = fighters_offset + frame_count * port_count * fighter_rec_size

        with out_path.open("wb") as f:
            # Header (packed explicitly).
            f.write(ENGINE_DUMP_MAGIC)
            f.write(struct.pack("<I", ENGINE_DUMP_VERSION))
            f.write(struct.pack("<I", ENGINE_DUMP_ENDIAN_TAG))
            f.write(struct.pack("<I", frame_count))
            f.write(struct.pack("<B", port_count))
            f.write(struct.pack("<H", int(stage.value)))
            f.write(b"\x00" * 5)
            f.write(struct.pack("<I", frames_offset))
            f.write(struct.pack("<I", inputs_offset))
            f.write(struct.pack("<I", fighters_offset))
            f.write(struct.pack("<I", items_offset))
            f.write(struct.pack("<I", len(items)))
            # Pad header to frames_offset.
            pad = frames_offset - f.tell()
            if pad < 0:
                raise RuntimeError("header layout exceeded frame offset")
            f.write(b"\x00" * pad)

            # Frame records.
            for fr in frames:
                f.write(struct.pack("<i", int(fr["frame_index"])))
                f.write(struct.pack("<I", 0))
                f.write(struct.pack("<H", int(fr["item_count"])))
                f.write(struct.pack("<I", int(fr["item_offset"])))
                f.write(struct.pack("<H", 0))

            # Input records.
            for inp in inputs:
                f.write(struct.pack("<I", int(inp["buttons"])))
                f.write(struct.pack("<I", _f32_bits(inp["stick_x"])))
                f.write(struct.pack("<I", _f32_bits(inp["stick_y"])))
                f.write(struct.pack("<I", _f32_bits(inp["cstick_x"])))
                f.write(struct.pack("<I", _f32_bits(inp["cstick_y"])))
                f.write(struct.pack("<I", _f32_bits(inp["l_shoulder"])))
                f.write(struct.pack("<I", _f32_bits(inp["r_shoulder"])))

            # Fighter records.
            for ft in fighters:
                f.write(struct.pack("<I", int(ft["flags"])))
                f.write(struct.pack("<I", _f32_bits(ft["pos"][0])))
                f.write(struct.pack("<I", _f32_bits(ft["pos"][1])))
                f.write(struct.pack("<I", _f32_bits(ft["pos"][2])))
                f.write(struct.pack("<I", _f32_bits(ft["self_vel"][0])))
                f.write(struct.pack("<I", _f32_bits(ft["self_vel"][1])))
                f.write(struct.pack("<I", _f32_bits(ft["gr_vel"])))
                f.write(struct.pack("<H", int(ft["action"])))
                f.write(struct.pack("<H", int(ft["anim"])))
                f.write(struct.pack("<I", _f32_bits(ft["anim_frame"])))
                f.write(struct.pack("<B", int(ft["ground"]) & 0xFF))
                f.write(struct.pack("<B", 0))
                f.write(struct.pack("<H", 0))
                f.write(struct.pack("<I", _f32_bits(ft["facing"])))
                f.write(struct.pack("<I", _f32_bits(ft["percent"])))

            # Item records.
            for it in items:
                f.write(struct.pack("<I", int(it["item_id"])))
                f.write(struct.pack("<H", int(it["kind"]) & 0xFFFF))
                f.write(struct.pack("<H", int(it["state"]) & 0xFFFF))
                f.write(struct.pack("<b", int(it["owner"])) )
                f.write(struct.pack("<B", 0))
                f.write(struct.pack("<I", _f32_bits(it["pos"][0])))
                f.write(struct.pack("<I", _f32_bits(it["pos"][1])))
                f.write(struct.pack("<I", _f32_bits(it["pos"][2])))
                f.write(struct.pack("<I", _f32_bits(it["vel"][0])))
                f.write(struct.pack("<I", _f32_bits(it["vel"][1])))
                f.write(struct.pack("<I", _f32_bits(it["vel"][2])))

        print(f"wrote {frame_count} frames to {out_path}")
        return 0

    finally:
        if console is not None:
            console.stop()


if __name__ == "__main__":
    raise SystemExit(main())
