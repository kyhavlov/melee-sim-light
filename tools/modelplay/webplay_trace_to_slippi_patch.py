from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from tools.modelplay.sim_env import SIM_INIT_OPENING_FRAME_ID


BUTTON_BITS = {
    "dPadLeft": 0x0001,
    "dPadRight": 0x0002,
    "dPadDown": 0x0004,
    "dPadUp": 0x0008,
    "z": 0x0010,
    "rTriggerDigital": 0x0020,
    "lTriggerDigital": 0x0040,
    "a": 0x0100,
    "b": 0x0200,
    "x": 0x0400,
    "y": 0x0800,
    "start": 0x1000,
}

WEBPLAY_TRACE_FORMAT = "melee-sim-light-webplay-debug-trace-v1"
WEBPLAY_INPUT_FIELDS = ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]


def _load_webplay_trace(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("format") != WEBPLAY_TRACE_FORMAT:
        raise ValueError(
            f"expected {WEBPLAY_TRACE_FORMAT!r}, got {payload.get('format')!r}: {path}"
        )
    if payload.get("inputFields") != WEBPLAY_INPUT_FIELDS:
        raise ValueError(f"unsupported webplay inputFields: {payload.get('inputFields')!r}")
    for key in ("p1Inputs", "p2Inputs"):
        if not isinstance(payload.get(key), list):
            raise ValueError(f"trace missing {key} list: {path}")
    return payload


def _clamp_unit(value: Any) -> float:
    return max(-1.0, min(1.0, float(value)))


def _clamp_trigger(value: Any) -> float:
    return max(0.0, min(1.0, float(value)))


def _input_patch_from_webplay(values: list[Any]) -> dict[str, Any]:
    if len(values) != len(WEBPLAY_INPUT_FIELDS):
        raise ValueError(f"expected {len(WEBPLAY_INPUT_FIELDS)} webplay input values, got {values!r}")
    buttons = int(values[0]) & 0xFFFF
    l_analog = _clamp_trigger(values[5])
    r_analog = _clamp_trigger(values[6])
    out: dict[str, Any] = {
        key: bool(buttons & bit) for key, bit in BUTTON_BITS.items()
    }
    out.update(
        {
            "joystickX": _clamp_unit(values[1]),
            "joystickY": _clamp_unit(values[2]),
            "cStickX": _clamp_unit(values[3]),
            "cStickY": _clamp_unit(values[4]),
            "anyTrigger": max(l_analog, r_analog),
            "lTriggerAnalog": l_analog,
            "rTriggerAnalog": r_analog,
        }
    )
    return out


def _frame_inputs(trace: dict[str, Any], player: int, frame: int) -> list[Any]:
    key = f"p{player + 1}Inputs"
    rows = trace[key]
    if frame < 0 or frame >= len(rows):
        raise ValueError(f"trace missing {key}[{frame}]")
    return rows[frame]


def _parse_player_map(raw: str) -> list[int]:
    out = [int(part.strip()) for part in raw.split(",") if part.strip()]
    if not out:
        raise ValueError("--carrier-player-map must not be empty")
    if any(player < 0 or player > 3 for player in out):
        raise ValueError("--carrier-player-map entries must be zero-based player indices in 0..3")
    return out


def build_patch_spec(
    trace: dict[str, Any],
    *,
    start_frame: int,
    end_frame: int,
    input_raw_frame_offset: int,
    carrier_player_map: list[int],
    compress: bool,
) -> dict[str, Any]:
    if start_frame < 0:
        raise ValueError("--start-frame must be non-negative")
    frame_count = int(trace.get("frameCount", -1))
    if end_frame < start_frame:
        raise ValueError("--end-frame must be >= --start-frame")
    if end_frame > frame_count:
        raise ValueError(f"--end-frame {end_frame} exceeds trace frameCount {frame_count}")
    if len(carrier_player_map) < 2:
        raise ValueError("webplay traces currently require two carrier player mappings")

    patches: list[dict[str, Any]] = []
    for player in range(2):
        carrier_player = int(carrier_player_map[player])
        if compress:
            run_start = start_frame
            run_values = _frame_inputs(trace, player, start_frame)
            for frame in range(start_frame + 1, end_frame + 2):
                values = _frame_inputs(trace, player, frame) if frame <= end_frame else None
                if values == run_values:
                    continue
                patches.append(
                    {
                        "note": f"webplay frames {run_start}..{frame - 1} p{player}",
                        "start_frame": int(run_start + input_raw_frame_offset),
                        "end_frame": int(frame - 1 + input_raw_frame_offset),
                        "player": carrier_player,
                        "input": _input_patch_from_webplay(run_values),
                    }
                )
                if values is not None:
                    run_start = frame
                    run_values = values
        else:
            for frame in range(start_frame, end_frame + 1):
                patches.append(
                    {
                        "note": f"webplay frame {frame} p{player}",
                        "frame": int(frame + input_raw_frame_offset),
                        "player": carrier_player,
                        "input": _input_patch_from_webplay(_frame_inputs(trace, player, frame)),
                    }
                )

    return {
        "source_trace": str(trace.get("name") or ""),
        "webplay_format": trace.get("format"),
        "webplay_frame_count": frame_count,
        "input_raw_frame_offset": int(input_raw_frame_offset),
        "carrier_player_map": list(carrier_player_map),
        "patches": patches,
    }


def _print_summary(spec: dict[str, Any], *, out: Path) -> None:
    patches = spec["patches"]
    print(f"wrote patch spec: {out}")
    print(f"patches: {len(patches)}")
    print(f"input_raw_frame_offset: {spec['input_raw_frame_offset']}")
    print(f"carrier_player_map: {spec['carrier_player_map']}")
    if patches:
        first = patches[0]
        last = patches[-1]
        first_frame = first.get("frame", first.get("start_frame"))
        last_frame = last.get("frame", last.get("end_frame"))
        print(f"raw pre-frame span: {first_frame}..{last_frame}")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description=(
            "Convert a webplay debug trace into a patch_slp_preframe_window JSON patch spec "
            "for vanilla Dolphin playback probes."
        )
    )
    ap.add_argument("--trace", required=True, type=Path, help="webplay debug trace JSON")
    ap.add_argument("--out", required=True, type=Path, help="output patch_spec.json")
    ap.add_argument("--start-frame", type=int, default=0, help="webplay trace frame index")
    ap.add_argument("--end-frame", type=int, default=None, help="webplay trace frame index")
    ap.add_argument(
        "--input-raw-frame-offset",
        type=int,
        default=SIM_INIT_OPENING_FRAME_ID - 1,
        help=(
            "raw_slippi_pre_frame = webplay_frame + offset. This defaults to the same "
            "sim-init convention used by compare_trace_to_vanilla."
        ),
    )
    ap.add_argument(
        "--carrier-player-map",
        default="0,1",
        help="comma-separated mapping from webplay p1,p2 to zero-based Slippi players",
    )
    ap.add_argument(
        "--no-compress",
        action="store_true",
        help="emit one patch per player frame instead of ranges for repeated inputs",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    trace = _load_webplay_trace(args.trace)
    end_frame = int(trace["frameCount"]) if args.end_frame is None else int(args.end_frame)
    spec = build_patch_spec(
        trace,
        start_frame=int(args.start_frame),
        end_frame=end_frame,
        input_raw_frame_offset=int(args.input_raw_frame_offset),
        carrier_player_map=_parse_player_map(args.carrier_player_map),
        compress=not bool(args.no_compress),
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    _print_summary(spec, out=args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
