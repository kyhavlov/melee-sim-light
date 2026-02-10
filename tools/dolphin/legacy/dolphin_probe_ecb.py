#!/usr/bin/env python3
from __future__ import annotations

"""
ECB probe via ExiAI Dolphin (libmelee Console) + live memory reads.

WARNING:
- Run from an isolated terminal session (e.g. `tmux`) and prefer a hard timeout (`timeout 60s ...`).
- If Dolphin wedges, this script can appear "hung" while spinning.

Set `SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING=1` to suppress the runtime warning banner.
"""

import argparse
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import peppi_bytes
from melee_sim.metrics import MASK_F32, MASK_U8, canonicalize_slippi_sample_last
from melee_sim.replay_io import read_replay_bytes


LIBMELEE_ROOT_DEFAULT = Path("/media/kyle/Windows/Users/kyleh/git/libmelee")


def _add_libmelee_to_path(root: Path) -> None:
    root = root.resolve()
    if not root.exists():
        raise FileNotFoundError(f"libmelee path does not exist: {root}")
    sys.path.insert(0, str(root))


def _as_unit_f32(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    # peppi_bytes uses MASK_F32 for missing/shifted tail values
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, -1.0, 1.0).astype(np.float32, copy=False)

def _as_f32(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    # peppi_bytes uses MASK_F32 for missing/shifted tail values
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return x


def _as_u8_bool(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.uint8)
    # peppi_bytes masks missing as 255
    return np.where(x == MASK_U8, 0, x).astype(np.uint8, copy=False)


def _as_trigger(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, 0.0, 1.0).astype(np.float32, copy=False)


@dataclass(frozen=True)
class ReplayInputs:
    frames: np.ndarray  # int32, post-frame ids (canonicalized, contiguous)
    # shifted pre-frame inputs (index i drives transition frame[i] -> frame[i+1])
    p1: dict[str, np.ndarray]
    p2: dict[str, np.ndarray]
    # post-frame state (for quick alignment sanity checks)
    post: dict[str, np.ndarray]


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
        out["l"] = _as_u8_bool(sample[f"{pfx}_button_l"]) if f"{pfx}_button_l" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["r"] = _as_u8_bool(sample[f"{pfx}_button_r"]) if f"{pfx}_button_r" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["a"] = _as_u8_bool(sample[f"{pfx}_button_a"]) if f"{pfx}_button_a" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["b"] = _as_u8_bool(sample[f"{pfx}_button_b"]) if f"{pfx}_button_b" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["x"] = _as_u8_bool(sample[f"{pfx}_button_x"]) if f"{pfx}_button_x" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["y"] = _as_u8_bool(sample[f"{pfx}_button_y"]) if f"{pfx}_button_y" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["z"] = _as_u8_bool(sample[f"{pfx}_button_z"]) if f"{pfx}_button_z" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["start"] = _as_u8_bool(sample[f"{pfx}_button_start"]) if f"{pfx}_button_start" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["d_up"] = _as_u8_bool(sample[f"{pfx}_button_d_up"]) if f"{pfx}_button_d_up" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["l_shoulder"] = _as_trigger(sample[f"{pfx}_l_shoulder"]) if f"{pfx}_l_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        out["r_shoulder"] = _as_trigger(sample[f"{pfx}_r_shoulder"]) if f"{pfx}_r_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        return out

    post = {
        "p1_x": _as_f32(sample["p1_position_x"]),
        "p1_y": _as_f32(sample["p1_position_y"]),
        "p2_x": _as_f32(sample["p2_position_x"]),
        "p2_y": _as_f32(sample["p2_position_y"]),
        "p1_action": sample["p1_action"].astype(np.uint16, copy=False),
        "p2_action": sample["p2_action"].astype(np.uint16, copy=False),
        "p1_og": _as_u8_bool(sample["p1_on_ground"]),
        "p2_og": _as_u8_bool(sample["p2_on_ground"]),
        "p1_char": sample["p1_character"].astype(np.uint8, copy=False),
        "p2_char": sample["p2_character"].astype(np.uint8, copy=False),
    }
    if "p1_animation_index" in sample:
        post["p1_anim"] = sample["p1_animation_index"].astype(np.uint32, copy=False)
    if "p2_animation_index" in sample:
        post["p2_anim"] = sample["p2_animation_index"].astype(np.uint32, copy=False)

    return ReplayInputs(frames=frames, p1=p(1), p2=p(2), post=post)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run ExiAI Dolphin and log ECB values while replaying inputs.")
    ap.add_argument("--replay", required=True, type=str)
    ap.add_argument("--dolphin", default=str(Path.cwd() / "Slippi_Online-x86_64-ExiAI.AppImage"))
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--libmelee", default=str(LIBMELEE_ROOT_DEFAULT))
    ap.add_argument("--start_frame", type=int, default=10)
    ap.add_argument("--end_frame", type=int, default=25)
    ap.add_argument("--out", type=str, default="/tmp/dolphin_ecb_probe.json")
    ap.add_argument("--max_seconds", type=float, default=240.0)
    ap.add_argument("--max_menu_steps", type=int, default=6000)
    args = ap.parse_args()
    if os.environ.get("SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING") != "1":
        print(
            "WARNING: live Dolphin memory probes can wedge Dolphin and may take down your terminal.\n"
            "Run in tmux/separate terminal and prefer `timeout` + small `--max_seconds` + small frame windows.",
            file=sys.stderr,
            flush=True,
        )

    _add_libmelee_to_path(Path(args.libmelee))
    import melee  # noqa: E402
    from melee import enums  # noqa: E402

    r = load_replay_inputs(args.replay)
    min_frame = int(r.frames[0])
    max_frame = int(r.frames[-1])
    if args.start_frame < min_frame or args.end_frame > max_frame:
        raise ValueError(f"frame window [{args.start_frame},{args.end_frame}] outside replay [{min_frame},{max_frame}]")

    # Infer characters from post-frame ids (these match libmelee's enums).
    p1_char = int(r.post["p1_char"][0])
    p2_char = int(r.post["p2_char"][0])
    char1 = enums.Character(p1_char)
    char2 = enums.Character(p2_char)

    t0 = time.monotonic()

    console = None
    controllers = None
    try:
        console = melee.Console(
            path=args.dolphin,
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

        print("Launching Dolphin...")
        console.run(iso_path=args.iso)
        print("Connecting to console...")
        if not console.connect():
            raise RuntimeError("failed to connect to dolphin slippstream")
        for c in controllers.values():
            if not c.connect():
                raise RuntimeError(f"failed to connect controller port {c.port}")

        # Menu to a local VS match with requested characters/stage.
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
                character_selected=char1,
                stage_selected=stage,
                autostart=True,
                costume=0,
                connect_code="",
            )
            menu.menu_helper_simple(
                gamestate,
                controllers[2],
                character_selected=char2,
                stage_selected=stage,
                autostart=True,
                costume=1,
                connect_code="",
            )

        assert gamestate is not None
        print(f"In game at frame={int(gamestate.frame)} (replay starts at {min_frame})")

        # Advance until we reach the replay's frame timeline, then feed recorded inputs.
        out_rows: list[dict] = []

        def apply_inputs(port: int, idx: int) -> None:
            ctrl = controllers[port]
            pi = r.p1 if port == 1 else r.p2
            ctrl.tilt_analog_unit(
                enums.Button.BUTTON_MAIN,
                float(pi["stick_x"][idx]),
                float(pi["stick_y"][idx]),
            )
            ctrl.tilt_analog_unit(
                enums.Button.BUTTON_C,
                float(pi["cstick_x"][idx]),
                float(pi["cstick_y"][idx]),
            )
            ctrl.press_shoulder(enums.Button.BUTTON_L, float(pi["l_shoulder"][idx]))
            ctrl.press_shoulder(enums.Button.BUTTON_R, float(pi["r_shoulder"][idx]))

            def set_btn(name: str, b: enums.Button) -> None:
                pressed = int(pi[name][idx]) != 0
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

        # Align to replay's earliest frame.
        while int(gamestate.frame) < min_frame:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while aligning to replay start frame")
            gamestate = console.step()
            if gamestate is None:
                continue
        if int(gamestate.frame) != min_frame:
            raise RuntimeError(
                f"cannot align: entered IN_GAME at frame={int(gamestate.frame)} but replay starts at {min_frame}"
            )

        while True:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while running replay input loop")

            frame = int(gamestate.frame)
            idx = frame - min_frame
            if idx < 0 or idx >= len(r.frames):
                break
            if int(r.frames[idx]) != frame:
                raise RuntimeError(
                    f"frame/index mismatch: replay.frames[{idx}]={int(r.frames[idx])} but game.frame={frame}"
                )

            # Capture ECB + a minimal set of state for comparison.
            row: dict = {"frame": frame}
            for port in (1, 2):
                ps = gamestate.players.get(port)
                if ps is None:
                    continue
                row[f"p{port}_x"] = float(ps.position.x)
                row[f"p{port}_y"] = float(ps.position.y)
                act = getattr(ps.action, "value", None)
                if act is None:
                    # UnknownAnimation in libmelee stores the raw value in `_value`
                    act = getattr(ps.action, "_value", 0)
                row[f"p{port}_action"] = int(act)
                row[f"p{port}_action_frame"] = int(getattr(ps, "action_frame", 0))
                row[f"p{port}_on_ground"] = bool(ps.on_ground)
                # ECB points (libmelee gecko codes).
                row[f"p{port}_ecb_bottom"] = (float(ps.ecb.bottom.x), float(ps.ecb.bottom.y))
                row[f"p{port}_ecb_top"] = (float(ps.ecb.top.x), float(ps.ecb.top.y))
                row[f"p{port}_ecb_left"] = (float(ps.ecb.left.x), float(ps.ecb.left.y))
                row[f"p{port}_ecb_right"] = (float(ps.ecb.right.x), float(ps.ecb.right.y))
                row[f"p{port}_ecb_bottom_rel_y"] = float(ps.ecb.bottom.y - ps.position.y)

            # Replay post-frame snapshot at this same frame index (sanity check).
            row["replay_p1_x"] = float(r.post["p1_x"][idx])
            row["replay_p1_y"] = float(r.post["p1_y"][idx])
            row["replay_p1_action"] = int(r.post["p1_action"][idx])
            row["replay_p1_og"] = bool(int(r.post["p1_og"][idx]) != 0)
            if "p1_anim" in r.post:
                row["replay_p1_anim"] = int(r.post["p1_anim"][idx])
            row["replay_p2_x"] = float(r.post["p2_x"][idx])
            row["replay_p2_y"] = float(r.post["p2_y"][idx])
            row["replay_p2_action"] = int(r.post["p2_action"][idx])
            row["replay_p2_og"] = bool(int(r.post["p2_og"][idx]) != 0)
            if "p2_anim" in r.post:
                row["replay_p2_anim"] = int(r.post["p2_anim"][idx])

            if args.start_frame <= frame <= args.end_frame:
                out_rows.append(row)

            # Apply inputs for the *next* frame by using shifted replay input at this frame index.
            apply_inputs(1, idx)
            apply_inputs(2, idx)

            gamestate = console.step()
            if gamestate is None:
                continue
            if int(gamestate.frame) > args.end_frame:
                break

        Path(args.out).write_text(json.dumps({"replay": args.replay, "rows": out_rows}, indent=2))
        print(f"wrote {len(out_rows)} rows to {args.out}")
        return 0
    finally:
        if controllers is not None:
            for c in controllers.values():
                try:
                    c.disconnect()
                except Exception:
                    pass
        if console is not None:
            try:
                console.stop()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
