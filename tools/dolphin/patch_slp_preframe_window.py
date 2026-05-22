from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path
from typing import Any


"""
Patch short Slippi pre-frame windows for controlled vanilla playback probes.

This intentionally edits only the replay's 0x37 pre-frame payloads in a copied
.slp file. Use the result with tools.dolphin.dolphin_engine_dump instead of live
memory pokes or Gecko hooks.

Patch spec shape:

{
  "patches": [
    {
      "note": "Falco holds an away/down-tilted shield near grounded Fox",
      "start_frame": 4644,
      "end_frame": 4685,
      "player": 1,
      "input": {
        "lTriggerDigital": true,
        "lTriggerAnalog": 1.0,
        "anyTrigger": 1.0,
        "joystickY": -1.0
      },
      "pre_state": {
        "action": 178,
        "x": 12.4,
        "y": 0.0,
        "facing": -1.0
      }
    },
    {
      "note": "Fox buffers getup attack",
      "start_frame": 4648,
      "end_frame": 4652,
      "player": 0,
      "input": {"a": true}
    }
  ]
}

Frame numbers here are raw Slippi frame indices, not parser/viewer frames with
the usual +123 display offset.
"""


EVENT_PAYLOADS = 0x35
GAME_START = 0x36
PRE_FRAME = 0x37

# Slippi 0x37 pre-frame layout. These offsets are the same offsets consumed by
# tools/viewer/slippi-viewer/src/parse/parser.ts::parsePreFrameUpdateEvent.
PRE_FRAME_OFFSETS = {
    "frame": 0x01,
    "player": 0x05,
    "is_follower": 0x06,
    "action": 0x0B,
    "x": 0x0D,
    "y": 0x11,
    "facing": 0x15,
    "joystickX": 0x19,
    "joystickY": 0x1D,
    "cStickX": 0x21,
    "cStickY": 0x25,
    "anyTrigger": 0x29,
    "processed_buttons": 0x2D,
    "physical_buttons": 0x31,
    "lTriggerAnalog": 0x33,
    "rTriggerAnalog": 0x37,
    "rawJoystickX": 0x3B,
    "percent": 0x3C,
    "rawJoystickY": 0x40,
    "rawCStickX": 0x41,
    "rawCStickY": 0x42,
}

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

HELD_LR_PROXY_PROCESSED_BIT = 0x80000000


def _load_ubjson_module():
    try:
        import ubjson  # type: ignore
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Missing Python package 'ubjson'. Install it in this uv environment with "
            "`uv pip install py-ubjson` before using this forensic helper."
        ) from exc
    return ubjson


def _u8(raw: bytearray, offset: int) -> int:
    return raw[offset]


def _i32(raw: bytearray, offset: int) -> int:
    return struct.unpack_from(">i", raw, offset)[0]


def _u16(raw: bytearray, offset: int) -> int:
    return struct.unpack_from(">H", raw, offset)[0]


def _u32(raw: bytearray, offset: int) -> int:
    return struct.unpack_from(">I", raw, offset)[0]


def _f32(raw: bytearray, offset: int) -> float:
    return struct.unpack_from(">f", raw, offset)[0]


def _write_u8(raw: bytearray, offset: int, value: int) -> None:
    raw[offset] = int(value) & 0xFF


def _write_u16(raw: bytearray, offset: int, value: int) -> None:
    struct.pack_into(">H", raw, offset, int(value) & 0xFFFF)


def _write_u32(raw: bytearray, offset: int, value: int) -> None:
    struct.pack_into(">I", raw, offset, int(value) & 0xFFFFFFFF)


def _write_f32(raw: bytearray, offset: int, value: float) -> None:
    struct.pack_into(">f", raw, offset, float(value))


def _event_has_rel_offset(payload_size: int, rel_offset: int) -> bool:
    # Slippi command payload sizes exclude the one-byte command. Relative offset
    # zero is the command byte, so the largest valid relative offset is payload_size.
    return 0 <= rel_offset <= payload_size


def _require_rel_offset(payload_size: int, rel_offset: int, field: str) -> None:
    if not _event_has_rel_offset(payload_size, rel_offset):
        raise ValueError(
            f"pre-frame payload size 0x{payload_size:x} does not contain field {field} "
            f"at relative offset 0x{rel_offset:x}"
        )


def _parse_event_payload_sizes(raw: bytearray) -> dict[int, int]:
    if not raw or raw[0] != EVENT_PAYLOADS:
        raise ValueError("raw Slippi stream does not begin with event payload sizes command 0x35")

    event_payloads_payload_size = raw[1]
    sizes = {EVENT_PAYLOADS: event_payloads_payload_size}
    list_offset = 0x02
    stop = event_payloads_payload_size + list_offset - 0x01
    for offset in range(list_offset, stop, 0x03):
        command = raw[offset]
        sizes[command] = _u16(raw, offset + 0x01)
    return sizes


def _first_event_offset(sizes: dict[int, int]) -> int:
    if GAME_START not in sizes:
        raise ValueError("event payload sizes do not include game-start command 0x36")
    return sizes[EVENT_PAYLOADS] + 0x01 + sizes[GAME_START] + 0x01


def _iter_events(raw: bytearray, sizes: dict[int, int]):
    offset = _first_event_offset(sizes)
    raw_len = len(raw)
    while offset < raw_len:
        command = raw[offset]
        if command not in sizes:
            raise ValueError(f"unknown command 0x{command:02x} at raw offset {offset}")
        payload_size = sizes[command]
        next_offset = offset + payload_size + 0x01
        if next_offset > raw_len:
            raise ValueError(
                f"command 0x{command:02x} at raw offset {offset} extends past end of raw stream"
            )
        yield offset, command, payload_size
        offset = next_offset


def _load_patch_spec(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
        root = json.load(f)

    if isinstance(root, list):
        patches = root
    elif isinstance(root, dict) and isinstance(root.get("patches"), list):
        patches = root["patches"]
    else:
        raise ValueError("patch spec must be a list or an object with a 'patches' list")

    out: list[dict[str, Any]] = []
    for idx, patch in enumerate(patches):
        if not isinstance(patch, dict):
            raise ValueError(f"patch {idx} must be an object")
        out.append(_normalize_patch(patch, idx))
    return out


def _normalize_patch(patch: dict[str, Any], idx: int) -> dict[str, Any]:
    if "frame" in patch:
        start_frame = end_frame = int(patch["frame"])
    else:
        if "start_frame" not in patch or "end_frame" not in patch:
            raise ValueError(f"patch {idx} must provide either frame or start_frame/end_frame")
        start_frame = int(patch["start_frame"])
        end_frame = int(patch["end_frame"])
    if start_frame > end_frame:
        raise ValueError(f"patch {idx} has start_frame > end_frame")

    if "player" not in patch:
        raise ValueError(f"patch {idx} must provide zero-based player index")
    player = int(patch["player"])
    if not 0 <= player <= 3:
        raise ValueError(f"patch {idx} player must be in 0..3")

    input_patch = patch.get("input", {})
    pre_state_patch = patch.get("pre_state", {})
    if not isinstance(input_patch, dict):
        raise ValueError(f"patch {idx} input must be an object")
    if not isinstance(pre_state_patch, dict):
        raise ValueError(f"patch {idx} pre_state must be an object")
    if not input_patch and not pre_state_patch:
        raise ValueError(f"patch {idx} must patch input and/or pre_state")

    return {
        "idx": idx,
        "note": str(patch.get("note", "")),
        "start_frame": start_frame,
        "end_frame": end_frame,
        "player": player,
        "is_follower": bool(patch.get("is_follower", False)),
        "input": input_patch,
        "pre_state": pre_state_patch,
    }


def _patch_matches(raw: bytearray, offset: int, patch: dict[str, Any]) -> bool:
    frame = _i32(raw, offset + PRE_FRAME_OFFSETS["frame"])
    player = _u8(raw, offset + PRE_FRAME_OFFSETS["player"])
    is_follower = bool(_u8(raw, offset + PRE_FRAME_OFFSETS["is_follower"]))
    return (
        patch["start_frame"] <= frame <= patch["end_frame"]
        and player == patch["player"]
        and is_follower == patch["is_follower"]
    )


def _patch_pre_state(raw: bytearray, offset: int, payload_size: int, patch: dict[str, Any]) -> None:
    for key, value in patch.items():
        if key == "action":
            rel_offset = PRE_FRAME_OFFSETS["action"]
            _require_rel_offset(payload_size, rel_offset + 1, key)
            _write_u16(raw, offset + rel_offset, int(value))
        elif key in ("x", "y", "facing", "percent"):
            rel_offset = PRE_FRAME_OFFSETS[key]
            _require_rel_offset(payload_size, rel_offset + 3, key)
            _write_f32(raw, offset + rel_offset, float(value))
        else:
            raise ValueError(f"unsupported pre_state field {key!r}")


def _patch_button_bits(raw: bytearray, offset: int, input_patch: dict[str, Any]) -> bool:
    touched = False
    processed = _u32(raw, offset + PRE_FRAME_OFFSETS["processed_buttons"])
    physical = _u16(raw, offset + PRE_FRAME_OFFSETS["physical_buttons"])

    for key, bit in BUTTON_BITS.items():
        if key not in input_patch:
            continue
        touched = True
        if bool(input_patch[key]):
            processed |= bit
            physical |= bit
        else:
            processed &= ~bit
            physical &= ~bit

    if touched:
        _write_u32(raw, offset + PRE_FRAME_OFFSETS["processed_buttons"], processed)
        _write_u16(raw, offset + PRE_FRAME_OFFSETS["physical_buttons"], physical)
    return touched


def _axis_to_raw_byte(value: float) -> int:
    if not math.isfinite(float(value)):
        raise ValueError(f"axis value must be finite, got {value!r}")
    clamped = max(-1.0, min(1.0, float(value)))
    return int(round(clamped * 80.0)) & 0xFF


def _recompute_processed_lr_proxy(raw: bytearray, offset: int) -> None:
    processed_offset = offset + PRE_FRAME_OFFSETS["processed_buttons"]
    processed = _u32(raw, processed_offset)
    physical = _u16(raw, offset + PRE_FRAME_OFFSETS["physical_buttons"])
    l_analog = _f32(raw, offset + PRE_FRAME_OFFSETS["lTriggerAnalog"])
    r_analog = _f32(raw, offset + PRE_FRAME_OFFSETS["rTriggerAnalog"])
    any_trigger = _f32(raw, offset + PRE_FRAME_OFFSETS["anyTrigger"])

    lr_held = bool(physical & (BUTTON_BITS["lTriggerDigital"] | BUTTON_BITS["rTriggerDigital"]))
    lr_held = lr_held or l_analog > 0.0 or r_analog > 0.0 or any_trigger > 0.0
    if lr_held:
        processed |= HELD_LR_PROXY_PROCESSED_BIT
    else:
        processed &= ~HELD_LR_PROXY_PROCESSED_BIT
    _write_u32(raw, processed_offset, processed)


def _patch_input(raw: bytearray, offset: int, payload_size: int, input_patch: dict[str, Any]) -> None:
    axis_to_raw = {
        "joystickX": "rawJoystickX",
        "joystickY": "rawJoystickY",
        "cStickX": "rawCStickX",
        "cStickY": "rawCStickY",
    }

    trigger_touched = False
    for key, value in input_patch.items():
        if key in BUTTON_BITS:
            continue
        if key in ("joystickX", "joystickY", "cStickX", "cStickY", "anyTrigger"):
            rel_offset = PRE_FRAME_OFFSETS[key]
            _require_rel_offset(payload_size, rel_offset + 3, key)
            _write_f32(raw, offset + rel_offset, float(value))

            raw_key = axis_to_raw.get(key)
            if raw_key is not None:
                raw_rel_offset = PRE_FRAME_OFFSETS[raw_key]
                if _event_has_rel_offset(payload_size, raw_rel_offset):
                    _write_u8(raw, offset + raw_rel_offset, _axis_to_raw_byte(float(value)))
            if key == "anyTrigger":
                trigger_touched = True
        elif key in ("lTriggerAnalog", "rTriggerAnalog"):
            rel_offset = PRE_FRAME_OFFSETS[key]
            _require_rel_offset(payload_size, rel_offset + 3, key)
            _write_f32(raw, offset + rel_offset, float(value))
            trigger_touched = True
        else:
            raise ValueError(f"unsupported input field {key!r}")

    buttons_touched = _patch_button_bits(raw, offset, input_patch)
    if trigger_touched or "lTriggerDigital" in input_patch or "rTriggerDigital" in input_patch:
        _recompute_processed_lr_proxy(raw, offset)
    elif buttons_touched:
        # Keep any existing high processed bits intact for non-trigger button patches.
        pass


def _apply_patches(raw: bytearray, patches: list[dict[str, Any]]) -> list[int]:
    sizes = _parse_event_payload_sizes(raw)
    if PRE_FRAME not in sizes:
        raise ValueError("event payload sizes do not include pre-frame command 0x37")

    matched = [0 for _ in patches]
    pre_frame_events = 0
    for offset, command, payload_size in _iter_events(raw, sizes):
        if command != PRE_FRAME:
            continue
        pre_frame_events += 1
        for patch in patches:
            if not _patch_matches(raw, offset, patch):
                continue
            _patch_pre_state(raw, offset, payload_size, patch["pre_state"])
            _patch_input(raw, offset, payload_size, patch["input"])
            matched[patch["idx"]] += 1

    if pre_frame_events == 0:
        raise ValueError("no pre-frame events found in replay")
    return matched


def _same_path(a: Path, b: Path) -> bool:
    try:
        return a.resolve(strict=False) == b.resolve(strict=False)
    except OSError:
        return a.absolute() == b.absolute()


def _print_summary(patches: list[dict[str, Any]], matched: list[int], *, dry_run: bool, out: Path) -> None:
    action = "would write" if dry_run else "wrote"
    print(f"{action}: {out}")
    for patch, count in zip(patches, matched, strict=True):
        frame_desc = (
            str(patch["start_frame"])
            if patch["start_frame"] == patch["end_frame"]
            else f"{patch['start_frame']}..{patch['end_frame']}"
        )
        note = f" ({patch['note']})" if patch["note"] else ""
        print(
            f"patch {patch['idx']}: matched {count} pre-frame events "
            f"for p{patch['player']} frames {frame_desc}{note}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Patch short raw Slippi pre-frame windows in a copied .slp for controlled "
            "vanilla playback probes."
        )
    )
    parser.add_argument("--slp", required=True, type=Path, help="source .slp replay")
    parser.add_argument("--patch-spec", required=True, type=Path, help="JSON patch spec")
    parser.add_argument("--out", required=True, type=Path, help="patched .slp output path")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="allow --out to refer to the same file as --slp",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="parse and count patch matches without writing --out",
    )
    parser.add_argument(
        "--allow-empty-patches",
        action="store_true",
        help="do not fail if a patch matches zero pre-frame events",
    )
    args = parser.parse_args()

    ubjson = _load_ubjson_module()
    patches = _load_patch_spec(args.patch_spec)
    if not args.dry_run and _same_path(args.slp, args.out) and not args.in_place:
        raise SystemExit(
            "--out resolves to the same path as --slp. Refusing to overwrite the source replay; "
            "write to a copied path under reports/triage/ or pass --in-place intentionally."
        )

    with args.slp.open("rb") as f:
        obj = ubjson.load(f)
    if not isinstance(obj, dict) or "raw" not in obj:
        raise SystemExit("input does not look like a Slippi UBJSON object with a raw stream")
    raw_obj = obj["raw"]
    if not isinstance(raw_obj, (bytes, bytearray)):
        raise SystemExit("Slippi UBJSON raw field is not bytes")

    raw = bytearray(raw_obj)
    matched = _apply_patches(raw, patches)
    empty = [idx for idx, count in enumerate(matched) if count == 0]
    if empty and not args.allow_empty_patches:
        raise SystemExit(
            "patches matched zero pre-frame events: "
            + ", ".join(str(idx) for idx in empty)
            + ". Check raw Slippi frame numbers and zero-based player indices."
        )

    if not args.dry_run:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        obj["raw"] = bytes(raw)
        with args.out.open("wb") as f:
            ubjson.dump(obj, f)
    _print_summary(patches, matched, dry_run=args.dry_run, out=args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
