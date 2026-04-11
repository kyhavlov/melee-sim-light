from __future__ import annotations

import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from tools.dolphin.dolphin_engine_dump import capture_engine_dump
from tools.dolphin.engine_dump_io import f32_from_bits, read_engine_dump
from tools.dolphin.patch_slp_preframe_window import (
    PRE_FRAME,
    PRE_FRAME_OFFSETS,
    _apply_patches,
    _iter_events,
    _load_ubjson_module,
    _parse_event_payload_sizes,
    _u8,
)
from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset
from tools.modelplay.sim_env import (
    CHAR_FALCO,
    CHAR_FOX,
    SIM_INIT_OPENING_FRAME_ID,
    build_match_config_array,
)


DEFAULT_CARRIER_SLP = Path("replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp")
DEFAULT_DOLPHIN = Path("refs/Ishiiruka/build/Binaries/dolphin-emu-nogui")
DEFAULT_ISO = Path("SSBM.iso")

BOOL_FIELDS = ("action_id", "on_ground", "stocks")
FLOAT_FIELDS = (
    "action_frame",
    "pos_x",
    "pos_y",
    "step_dx",
    "step_dy",
    "facing",
    "percent",
    "shield_hp",
    "hitlag",
)
FIELD_ORDER = BOOL_FIELDS + FLOAT_FIELDS

TRACE_INTERNAL_CHAR_NAME = {
    1: "fox",
    22: "falco",
}

CARRIER_START_CHAR_NAME = {
    2: "fox",
    20: "falco",
}


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


@dataclass(frozen=True)
class DiffRow:
    frame: int
    raw_frame: int
    player: int
    field: str
    sim: str
    vanilla: str
    delta: str
    sim_action: int
    vanilla_action: int
    sim_context: str
    vanilla_context: str


def _load_trace(path: Path) -> dict[int, dict[str, Any]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    frames = payload.get("frames")
    if not isinstance(frames, list):
        raise ValueError(f"trace has no frames list: {path}")
    out: dict[int, dict[str, Any]] = {}
    for frame in frames:
        frame_i = int(frame["frameNumber"])
        out[frame_i] = frame
    return out


def _carrier_occupied_ports(path: Path) -> list[int]:
    ubjson = _load_ubjson_module()
    with path.open("rb") as f:
        obj = ubjson.load(f)
    if not isinstance(obj, dict) or "raw" not in obj:
        raise ValueError(f"carrier does not look like a Slippi UBJSON raw stream: {path}")
    raw_obj = obj["raw"]
    if not isinstance(raw_obj, (bytes, bytearray)):
        raise ValueError("carrier Slippi raw field is not bytes")
    raw = bytearray(raw_obj)
    sizes = _parse_event_payload_sizes(raw)
    occupied: list[int] = []
    seen: set[int] = set()
    for offset, command, _payload_size in _iter_events(raw, sizes):
        if command != PRE_FRAME:
            continue
        player = _u8(raw, offset + PRE_FRAME_OFFSETS["player"])
        is_follower = bool(_u8(raw, offset + PRE_FRAME_OFFSETS["is_follower"]))
        if is_follower or player in seen:
            continue
        seen.add(player)
        occupied.append(int(player))
    return occupied


def _carrier_char_ids(path: Path, occupied_ports: list[int]) -> list[int]:
    try:
        from peppi_py import _read_slippi
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Missing Python package 'peppi_py'. Install project dependencies with `uv sync`."
        ) from exc

    game = _read_slippi(str(path))
    players = game.start.get("players")
    if not isinstance(players, list):
        raise ValueError(f"carrier replay has no start.players list: {path}")
    if occupied_ports:
        max_port = max(int(port) for port in occupied_ports)
        if max_port < len(players):
            out: list[int] = []
            for port in occupied_ports:
                player = players[int(port)]
                if not isinstance(player, dict):
                    raise ValueError(f"carrier replay missing start.players[{port}] metadata: {path}")
                out.append(int(player.get("character", 0)))
            return out
    out: list[int] = []
    for player in players:
        if not isinstance(player, dict):
            continue
        if player.get("character") is None:
            continue
        out.append(int(player.get("character", 0)))
    return out


def _trace_char_ids(trace_frames: dict[int, dict[str, Any]]) -> list[int]:
    if not trace_frames:
        return []
    first_frame_i = min(trace_frames)
    frame = trace_frames[first_frame_i]
    players = frame.get("players")
    if not isinstance(players, list):
        raise ValueError(f"trace frame {first_frame_i} has no players list")
    out: list[int] = []
    for player in players:
        state = player.get("state", {})
        out.append(int(state.get("internalCharacterId", 0)))
    return out


def _trace_char_names(trace_char_ids: list[int]) -> list[str]:
    return [TRACE_INTERNAL_CHAR_NAME.get(int(char_id), f"trace:{int(char_id)}") for char_id in trace_char_ids]


def _carrier_char_names(carrier_char_ids: list[int]) -> list[str]:
    return [
        CARRIER_START_CHAR_NAME.get(int(char_id), f"carrier:{int(char_id)}")
        for char_id in carrier_char_ids
    ]


def _input_patch_from_processed(processed: dict[str, Any]) -> dict[str, Any]:
    any_trigger = float(processed.get("anyTrigger", 0.0) or 0.0)
    l_digital = bool(processed.get("lTriggerDigital", False))
    r_digital = bool(processed.get("rTriggerDigital", False))

    # Modelplay exposes one analog shoulder lane. The sim adapter feeds that lane into L, so use L
    # as the default analog owner unless the only digital owner is R.
    l_analog = any_trigger
    r_analog = 0.0
    if r_digital and not l_digital:
        l_analog = 0.0
        r_analog = any_trigger

    return {
        "a": bool(processed.get("a", False)),
        "b": bool(processed.get("b", False)),
        "x": bool(processed.get("x", False)),
        "y": bool(processed.get("y", False)),
        "z": bool(processed.get("z", False)),
        "start": bool(processed.get("start", False)),
        "dPadLeft": bool(processed.get("dPadLeft", False)),
        "dPadRight": bool(processed.get("dPadRight", False)),
        "dPadDown": bool(processed.get("dPadDown", False)),
        "dPadUp": bool(processed.get("dPadUp", False)),
        "lTriggerDigital": l_digital,
        "rTriggerDigital": r_digital,
        "joystickX": float(processed.get("joystickX", 0.0) or 0.0),
        "joystickY": float(processed.get("joystickY", 0.0) or 0.0),
        "cStickX": float(processed.get("cStickX", 0.0) or 0.0),
        "cStickY": float(processed.get("cStickY", 0.0) or 0.0),
        "anyTrigger": any_trigger,
        "lTriggerAnalog": l_analog,
        "rTriggerAnalog": r_analog,
    }


def _build_patch_spec(
    trace_frames: dict[int, dict[str, Any]],
    *,
    start_frame: int,
    end_frame: int,
    input_raw_frame_offset: int,
    carrier_player_map: list[int],
) -> list[dict[str, Any]]:
    patches: list[dict[str, Any]] = []
    for frame_i in range(start_frame, end_frame + 1):
        if frame_i not in trace_frames:
            raise ValueError(f"trace missing frame {frame_i}")
        frame = trace_frames[frame_i]
        for player, player_payload in enumerate(frame["players"]):
            if player >= len(carrier_player_map):
                raise ValueError(
                    f"trace player {player} has no carrier port mapping; carrier map={carrier_player_map}"
                )
            processed = player_payload["inputs"]["processed"]
            patches.append(
                {
                    "note": f"modelplay frame {frame_i} p{player}",
                    "frame": int(frame_i + input_raw_frame_offset),
                    "player": int(carrier_player_map[player]),
                    "input": _input_patch_from_processed(processed),
                }
            )
    return patches


def _patch_slp(*, carrier_slp: Path, patch_spec: list[dict[str, Any]], out_slp: Path) -> list[int]:
    ubjson = _load_ubjson_module()
    normalized = []
    for idx, patch in enumerate(patch_spec):
        normalized.append(
            {
                "idx": idx,
                "note": str(patch.get("note", "")),
                "start_frame": int(patch["frame"]),
                "end_frame": int(patch["frame"]),
                "player": int(patch["player"]),
                "is_follower": False,
                "input": dict(patch["input"]),
                "pre_state": {},
            }
        )

    with carrier_slp.open("rb") as f:
        obj = ubjson.load(f)
    if not isinstance(obj, dict) or "raw" not in obj:
        raise ValueError(f"carrier does not look like a Slippi UBJSON raw stream: {carrier_slp}")
    raw_obj = obj["raw"]
    if not isinstance(raw_obj, (bytes, bytearray)):
        raise ValueError("carrier Slippi raw field is not bytes")

    raw = bytearray(raw_obj)
    matched = _apply_patches(raw, normalized)
    missing = [idx for idx, count in enumerate(matched) if int(count) == 0]
    if missing:
        examples = ", ".join(str(i) for i in missing[:10])
        raise ValueError(f"{len(missing)} generated input patches matched no pre-frame event, examples: {examples}")

    out_slp.parent.mkdir(parents=True, exist_ok=True)
    obj["raw"] = bytes(raw)
    with out_slp.open("wb") as f:
        ubjson.dump(obj, f)
    return matched


def _engine_rows_by_frame(
    dump_path: Path, *, compare_raw_frame_offset: int, carrier_player_map: list[int]
) -> dict[tuple[int, int], dict[str, Any]]:
    dump = read_engine_dump(dump_path)
    port_count = int(dump.header["port_count"])
    rows: dict[tuple[int, int], dict[str, Any]] = {}
    for frame_slot, frame in enumerate(dump.frames):
        raw_frame = int(frame["frame_index"])
        model_frame = raw_frame - compare_raw_frame_offset
        for player, carrier_player in enumerate(carrier_player_map):
            if carrier_player < 0 or carrier_player >= port_count:
                raise ValueError(
                    f"carrier port {carrier_player} is outside engine dump port_count={port_count}"
                )
            idx = frame_slot * port_count + carrier_player
            fighter = dump.fighters[idx]
            rows[(model_frame, player)] = {
                "raw_frame": raw_frame,
                "action_id": int(fighter["action_state"]),
                "animation_id": int(fighter["anim_id"]),
                "action_frame": f32_from_bits(int(fighter["action_frame_bits"])),
                "anim_frame": f32_from_bits(int(fighter["anim_frame_bits"])),
                "pos_x": f32_from_bits(int(fighter["pos_x_bits"])),
                "pos_y": f32_from_bits(int(fighter["pos_y_bits"])),
                "facing": f32_from_bits(int(fighter["facing_bits"])),
                "percent": f32_from_bits(int(fighter["percent_bits"])),
                "shield_hp": f32_from_bits(int(fighter["shield_health_bits"])),
                "hitlag": f32_from_bits(int(fighter["hitlag_left_bits"])),
                # Decomp GA_Ground=0, GA_Air=1.
                "on_ground": 1 if int(fighter["ground_or_air"]) == 0 else 0,
                "stocks": int(fighter["stocks"]),
                "state_flags": [
                    int(fighter["state_flags_2218"]),
                    int(fighter["state_flags_221a"]),
                    int(fighter["state_flags_221b"]),
                    int(fighter["state_flags_221c"]),
                    int(fighter["state_flags_221f"]),
                ],
            }
    return rows


def _sim_row(trace_frames: dict[int, dict[str, Any]], frame_i: int, player: int) -> dict[str, Any]:
    state = trace_frames[frame_i]["players"][player]["state"]
    return {
        "action_id": int(state["actionStateId"]),
        "action_frame": float(state["actionStateFrameCounter"]),
        "pos_x": float(state["xPosition"]),
        "pos_y": float(state["yPosition"]),
        "facing": float(state["facingDirection"]),
        "percent": float(state["percent"]),
        "shield_hp": float(state["shieldSize"]),
        "hitlag": float(state["hitlagRemaining"]),
        "on_ground": 1 if bool(state["isGrounded"]) else 0,
        "stocks": int(state["stocksRemaining"]),
        "hitstun": int(state["hitstunRemaining"]),
        "jumps_left": int(state.get("jumpsRemaining", 0)),
    }


def _buttons_mask(processed: dict[str, Any]) -> int:
    mask = 0
    for key, bit in (
        ("a", 0x0100),
        ("b", 0x0200),
        ("x", 0x0400),
        ("y", 0x0800),
        ("z", 0x0010),
        ("lTriggerDigital", 0x0040),
        ("rTriggerDigital", 0x0020),
        ("start", 0x1000),
        ("dPadLeft", 0x0001),
        ("dPadRight", 0x0002),
        ("dPadDown", 0x0004),
        ("dPadUp", 0x0008),
    ):
        if bool(processed.get(key, False)):
            mask |= bit
    return mask


def _processed_to_stick_i8(v: float) -> int:
    vv = max(-1.0, min(1.0, float(v)))
    return int(round(((vv + 1.0) * 0.5) * 160.0 - 80.0))


def _input_array_from_trace_frame(frame: dict[str, Any], input_stride: int) -> Any:
    import numpy as np

    arr = np.zeros((1,), dtype=INPUT_DTYPE)
    for p in range(2):
        processed = frame["players"][p]["inputs"]["processed"]
        arr["p"]["buttons"][0, p] = _buttons_mask(processed)
        arr["p"]["main_x"][0, p] = _processed_to_stick_i8(processed.get("joystickX", 0.0))
        arr["p"]["main_y"][0, p] = _processed_to_stick_i8(processed.get("joystickY", 0.0))
        arr["p"]["c_x"][0, p] = _processed_to_stick_i8(processed.get("cStickX", 0.0))
        arr["p"]["c_y"][0, p] = _processed_to_stick_i8(processed.get("cStickY", 0.0))
        arr["p"]["l"][0, p] = int(round(max(0.0, min(1.0, float(processed.get("anyTrigger", 0.0)))) * 255.0))
        arr["p"]["r"][0, p] = 0
    return arr.view(np.uint8).reshape((1, input_stride)).copy()


def _compare_row_to_sim(row: Any, player: int) -> dict[str, Any]:
    return {
        "action_id": int(row["action_id"][player]),
        "action_frame": float(row["action_frame"][player]),
        "pos_x": float(row["pos_x"][player]),
        "pos_y": float(row["pos_y"][player]),
        "facing": 1.0 if int(row["facing"][player]) else -1.0,
        "percent": float(row["percent"][player]),
        "shield_hp": float(row["shield_hp"][player]),
        "hitlag": float(row["hitlag"][player]),
        "on_ground": 1 if int(row["on_ground"][player]) else 0,
        "stocks": int(row["stocks"][player]),
        "hitstun": int(row["hitstun"][player]),
        "jumps_left": int(row["jumps_left"][player]),
    }


def _load_modelplay_config(trace_path: Path) -> dict[str, Any]:
    config_path = trace_path.parent / "config.json"
    if not config_path.exists():
        raise FileNotFoundError(f"--sim-source current requires modelplay config next to trace: {config_path}")
    return json.loads(config_path.read_text(encoding="utf-8"))


def _run_current_sim_rows(
    trace_path: Path,
    trace_frames: dict[int, dict[str, Any]],
    *,
    end_frame: int,
) -> dict[tuple[int, int], dict[str, Any]]:
    import numpy as np
    import msl_binding  # type: ignore

    config = _load_modelplay_config(trace_path)
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out: dict[tuple[int, int], dict[str, Any]] = {}

    start_mode = str(config.get("start_mode", "replay"))
    if start_mode == "sim-init":
        char_map = {
            "fox": CHAR_FOX,
            "falco": CHAR_FALCO,
        }
        p1_char = str(config.get("p1_char", "falco")).lower()
        p2_char = str(config.get("p2_char", "fox")).lower()
        if p1_char not in char_map or p2_char not in char_map:
            raise ValueError(f"unsupported sim-init trace characters: p1={p1_char} p2={p2_char}")
        match_config = build_match_config_array(
            num_players=2,
            char_ids=(char_map[p1_char], char_map[p2_char]),
            facing=(1, 0),
            stocks=int(config.get("stocks", 4)),
            frame_id=SIM_INIT_OPENING_FRAME_ID,
            random_seed=int(config.get("seed", 0)),
        )
        config_bytes = match_config.view(np.uint8).reshape((1, -1))
        num_players = 2
    else:
        dataset_path = Path(str(config["dataset"]))
        start_record = int(config.get("start_record", 0))
        ds = read_dataset(str(dataset_path))
        sample = ds.samples[start_record : start_record + 1]
        seed_bytes = np.frombuffer(sample["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        num_players = int(ds.header["num_players"])

    handle = msl_binding.init(batch_size=1, num_players=num_players)
    try:
        if start_mode == "sim-init":
            msl_binding.init_match(handle, config_bytes)
            msl_binding.write_compare(handle, out_bytes)
            row = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            for p in range(2):
                out[(0, p)] = _compare_row_to_sim(row, p)
        else:
            msl_binding.reseed_seed(handle, seed_bytes)
            seed_row = sample["seed_t"][0]
            for p in range(2):
                out[(0, p)] = {
                    "action_id": int(seed_row["action_id"][p]),
                    "action_frame": float(seed_row["action_frame"][p]),
                    "pos_x": float(seed_row["pos_x"][p]),
                    "pos_y": float(seed_row["pos_y"][p]),
                    "facing": 1.0 if int(seed_row["facing"][p]) else -1.0,
                    "percent": float(seed_row["percent"][p]),
                    "shield_hp": float(seed_row["shield_hp"][p]),
                    "hitlag": float(seed_row["hitlag"][p]),
                    "on_ground": 1 if int(seed_row["on_ground"][p]) else 0,
                    "stocks": int(seed_row["stocks"][p]),
                    "hitstun": int(seed_row["hitstun"][p]),
                    "jumps_left": int(seed_row["jumps_left"][p]),
                }

        prev_input = _input_array_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, end_frame + 1):
            current_input = _input_array_from_trace_frame(trace_frames[frame_i], input_stride)
            msl_binding.step_input(handle, prev_input, current_input)
            msl_binding.write_compare(handle, out_bytes)
            row = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            for p in range(2):
                out[(frame_i, p)] = _compare_row_to_sim(row, p)
            prev_input = current_input
    finally:
        msl_binding.destroy(handle)
    return out


def _trace_sim_rows(
    trace_frames: dict[int, dict[str, Any]], *, start_frame: int, end_frame: int
) -> dict[tuple[int, int], dict[str, Any]]:
    rows: dict[tuple[int, int], dict[str, Any]] = {}
    for frame_i in range(start_frame, end_frame + 1):
        for p in range(2):
            rows[(frame_i, p)] = _sim_row(trace_frames, frame_i, p)
    return rows


def _float_mismatch(field: str, sim: float, vanilla: float) -> bool:
    if not math.isfinite(sim) or not math.isfinite(vanilla):
        return sim != vanilla
    tol = 0.001
    if field in ("percent", "shield_hp"):
        tol = 0.01
    if field in ("hitlag", "action_frame"):
        tol = 0.25
    return abs(sim - vanilla) > tol


def _context(row: dict[str, Any]) -> str:
    return (
        f"a={int(row['action_id'])} af={float(row['action_frame']):.2f} "
        f"g={int(row['on_ground'])} x={float(row['pos_x']):.3f} y={float(row['pos_y']):.3f} "
        f"pct={float(row['percent']):.2f} sh={float(row['shield_hp']):.2f} hlag={float(row['hitlag']):.2f}"
    )


def _with_step_delta(
    row: dict[str, Any],
    prev: dict[str, Any] | None,
) -> dict[str, Any]:
    out = dict(row)
    if prev is None:
        out["step_dx"] = 0.0
        out["step_dy"] = 0.0
    else:
        out["step_dx"] = float(row["pos_x"]) - float(prev["pos_x"])
        out["step_dy"] = float(row["pos_y"]) - float(prev["pos_y"])
    return out


def _compare(
    sim_rows: dict[tuple[int, int], dict[str, Any]],
    engine_rows: dict[tuple[int, int], dict[str, Any]],
    *,
    start_frame: int,
    end_frame: int,
    max_rows: int,
) -> list[DiffRow]:
    diffs: list[DiffRow] = []
    for frame_i in range(start_frame, end_frame + 1):
        for player in range(2):
            vanilla = engine_rows.get((frame_i, player))
            sim_base = sim_rows.get((frame_i, player))
            if vanilla is None or sim_base is None:
                continue
            prev_vanilla = engine_rows.get((frame_i - 1, player))
            prev_sim = sim_rows.get((frame_i - 1, player))
            sim = _with_step_delta(sim_base, prev_sim)
            vanilla = _with_step_delta(vanilla, prev_vanilla)
            for field in FIELD_ORDER:
                if field in BOOL_FIELDS:
                    mismatch = int(sim[field]) != int(vanilla[field])
                    delta = str(int(sim[field]) - int(vanilla[field]))
                else:
                    mismatch = _float_mismatch(field, float(sim[field]), float(vanilla[field]))
                    delta = f"{float(sim[field]) - float(vanilla[field]):.6g}"
                if not mismatch:
                    continue
                diffs.append(
                    DiffRow(
                        frame=frame_i,
                        raw_frame=int(vanilla["raw_frame"]),
                        player=player,
                        field=field,
                        sim=f"{sim[field]}",
                        vanilla=f"{vanilla[field]}",
                        delta=delta,
                        sim_action=int(sim["action_id"]),
                        vanilla_action=int(vanilla["action_id"]),
                        sim_context=_context(sim),
                        vanilla_context=_context(vanilla),
                    )
                )
                if len(diffs) >= max_rows:
                    return diffs
    return diffs


def _write_diff_tsv(path: Path, rows: list[DiffRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "frame",
                "raw_frame",
                "player",
                "field",
                "sim",
                "vanilla",
                "delta",
                "sim_action",
                "vanilla_action",
                "sim_context",
                "vanilla_context",
            ],
            delimiter="\t",
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def _summary_text(rows: list[DiffRow], *, start_frame: int, end_frame: int, out_dir: Path) -> str:
    lines: list[str] = []
    lines.append("# modelplay_vs_vanilla_compare")
    lines.append(f"window={start_frame}..{end_frame}")
    lines.append(f"out_dir={out_dir}")
    lines.append(f"diff_rows={len(rows)}")
    if not rows:
        lines.append("first_mismatch=none")
        return "\n".join(lines) + "\n"

    first_frame = min(row.frame for row in rows)
    lines.append(f"first_mismatch_frame={first_frame}")
    lines.append("")
    lines.append("== first frame mismatches ==")
    for row in rows:
        if row.frame != first_frame:
            continue
        lines.append(
            f"f={row.frame} raw={row.raw_frame} p={row.player} field={row.field} "
            f"sim={row.sim} vanilla={row.vanilla} delta={row.delta}"
        )
        lines.append(f"  sim:     {row.sim_context}")
        lines.append(f"  vanilla: {row.vanilla_context}")

    field_counts = Counter(row.field for row in rows)
    action_pairs = Counter((row.sim_action, row.vanilla_action) for row in rows if row.field == "action_id")
    by_frame: dict[int, Counter[str]] = defaultdict(Counter)
    for row in rows:
        by_frame[row.frame][row.field] += 1

    lines.append("")
    lines.append("== field counts ==")
    for field, count in field_counts.most_common():
        lines.append(f"{field}\t{count}")

    lines.append("")
    lines.append("== action mismatch pairs ==")
    if action_pairs:
        for (sim_action, vanilla_action), count in action_pairs.most_common(12):
            lines.append(f"sim={sim_action} vanilla={vanilla_action}\t{count}")
    else:
        lines.append("none")

    lines.append("")
    lines.append("== first mismatch frames ==")
    for frame_i in sorted(by_frame)[:20]:
        fields = ",".join(f"{k}:{v}" for k, v in by_frame[frame_i].most_common())
        lines.append(f"{frame_i}\t{fields}")
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Compare a modelplay trace against vanilla playback engine dump.")
    ap.add_argument("--trace", required=True, type=Path)
    ap.add_argument("--carrier-slp", type=Path, default=DEFAULT_CARRIER_SLP)
    ap.add_argument("--dolphin", type=Path, default=DEFAULT_DOLPHIN)
    ap.add_argument("--iso", type=Path, default=DEFAULT_ISO)
    ap.add_argument("--start-frame", type=int, default=0, help="modelplay viewer frame index")
    ap.add_argument("--end-frame", type=int, default=600, help="modelplay viewer frame index")
    ap.add_argument(
        "--input-raw-frame-offset",
        type=int,
        default=-122,
        help=(
            "raw_slippi_pre_frame = modelplay_frame + offset. Modelplay frames store the "
            "input sample that produced the same indexed post-step state, so this normally "
            "matches --compare-raw-frame-offset."
        ),
    )
    ap.add_argument(
        "--compare-raw-frame-offset",
        type=int,
        default=-122,
        help=(
            "raw_engine_dump_frame = modelplay_frame + offset. Modelplay trace frames are "
            "post-step states, so this is normally one frame after the patched pre-frame input."
        ),
    )
    ap.add_argument(
        "--raw-frame-offset",
        type=int,
        default=None,
        help="legacy shorthand: set both input and compare raw-frame offsets to this value",
    )
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument(
        "--resync",
        action="store_true",
        help="leave Slippi playback resync enabled; default is disabled for rollout comparison",
    )
    ap.add_argument(
        "--sim-source",
        choices=("trace", "current"),
        default="trace",
        help="compare vanilla against saved trace states or replay trace inputs through current sim",
    )
    ap.add_argument("--max-diff-rows", type=int, default=20000)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_modelplay_vanilla_compare",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    if args.end_frame < args.start_frame:
        raise SystemExit("--end-frame must be >= --start-frame")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    input_raw_frame_offset = int(args.input_raw_frame_offset)
    compare_raw_frame_offset = int(args.compare_raw_frame_offset)
    if args.raw_frame_offset is not None:
        input_raw_frame_offset = int(args.raw_frame_offset)
        compare_raw_frame_offset = int(args.raw_frame_offset)

    trace_frames = _load_trace(args.trace)
    trace_char_ids = _trace_char_ids(trace_frames)
    trace_char_names = _trace_char_names(trace_char_ids)
    carrier_player_map = _carrier_occupied_ports(args.carrier_slp)
    if len(carrier_player_map) < len(trace_char_ids):
        raise SystemExit(
            f"carrier replay only exposes ports {carrier_player_map}, but trace has "
            f"{len(trace_char_ids)} players"
        )
    carrier_player_map = carrier_player_map[: len(trace_char_ids)]
    carrier_char_ids = _carrier_char_ids(args.carrier_slp, carrier_player_map)
    carrier_char_names = _carrier_char_names(carrier_char_ids)
    if trace_char_names and carrier_char_names != trace_char_names:
        raise SystemExit(
            "carrier replay character mismatch: "
            f"trace chars={trace_char_names}, carrier chars={carrier_char_names}, "
            f"carrier ports={carrier_player_map}"
        )
    patch_spec = _build_patch_spec(
        trace_frames,
        start_frame=int(args.start_frame),
        end_frame=int(args.end_frame),
        input_raw_frame_offset=input_raw_frame_offset,
        carrier_player_map=carrier_player_map,
    )
    patch_spec_path = out_dir / "patch_spec.json"
    patch_spec_path.write_text(json.dumps({"patches": patch_spec}, indent=2) + "\n", encoding="utf-8")

    patched_slp = out_dir / "patched_modelplay_inputs.slp"
    matched = _patch_slp(carrier_slp=args.carrier_slp, patch_spec=patch_spec, out_slp=patched_slp)

    raw_start = min(
        int(args.start_frame) + input_raw_frame_offset,
        int(args.start_frame) + compare_raw_frame_offset,
    )
    raw_end = max(
        int(args.end_frame) + input_raw_frame_offset,
        int(args.end_frame) + compare_raw_frame_offset,
    )
    dump_path = out_dir / "vanilla_engine_dump.bin"
    user_dir = out_dir / "dolphin_user"
    rc, dump = capture_engine_dump(
        replay=patched_slp,
        dolphin=args.dolphin,
        iso=args.iso,
        user_dir=user_dir,
        out_bin=dump_path,
        start_frame=raw_start,
        end_frame=raw_end,
        timeout=float(args.timeout),
        should_resync=bool(args.resync),
    )
    if rc != 0:
        raise SystemExit(f"Dolphin engine dump failed; expected dump at {dump}")

    engine_rows = _engine_rows_by_frame(
        dump_path,
        compare_raw_frame_offset=compare_raw_frame_offset,
        carrier_player_map=carrier_player_map,
    )
    if args.sim_source == "current":
        sim_rows = _run_current_sim_rows(args.trace, trace_frames, end_frame=int(args.end_frame))
    else:
        sim_rows = _trace_sim_rows(
            trace_frames, start_frame=int(args.start_frame), end_frame=int(args.end_frame)
        )
    diff_rows = _compare(
        sim_rows,
        engine_rows,
        start_frame=int(args.start_frame),
        end_frame=int(args.end_frame),
        max_rows=int(args.max_diff_rows),
    )

    diff_tsv = out_dir / "diff.tsv"
    summary_path = out_dir / "summary.txt"
    _write_diff_tsv(diff_tsv, diff_rows)
    summary = _summary_text(
        diff_rows, start_frame=int(args.start_frame), end_frame=int(args.end_frame), out_dir=out_dir
    )
    summary_path.write_text(summary, encoding="utf-8")

    meta = {
        "trace": str(args.trace),
        "carrier_slp": str(args.carrier_slp),
        "patched_slp": str(patched_slp),
        "input_raw_frame_offset": input_raw_frame_offset,
        "compare_raw_frame_offset": compare_raw_frame_offset,
        "start_frame": int(args.start_frame),
        "end_frame": int(args.end_frame),
        "raw_start": raw_start,
        "raw_end": raw_end,
        "patches": len(patch_spec),
        "carrier_player_map": carrier_player_map,
        "patch_matches_min": min(matched) if matched else 0,
        "patch_matches_max": max(matched) if matched else 0,
        "engine_dump": str(dump_path),
        "should_resync": bool(args.resync),
        "sim_source": str(args.sim_source),
        "diff_tsv": str(diff_tsv),
        "summary": str(summary_path),
        "diff_rows": len(diff_rows),
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(summary.rstrip())
    print(f"wrote {diff_tsv}")
    print(f"wrote {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
