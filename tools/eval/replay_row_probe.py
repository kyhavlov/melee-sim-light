from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

import numpy as np

from tools.eval.dataset import MAX_ITEMS, MAX_PLAYERS, read_dataset_window
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


_PLAYER_CORE_FIELDS = (
    "action_id",
    "action_frame",
    "animation_index",
    "on_ground",
    "ground_id",
    "facing",
    "facing_dir1",
    "stocks",
    "is_dead",
)
_PLAYER_POSITION_FIELDS = ("pos_x", "pos_y", "pos_z")
_PLAYER_VELOCITY_FIELDS = (
    "speed_air_x_self",
    "speed_ground_x_self",
    "speed_y_self",
    "speed_x_attack",
    "speed_y_attack",
)
_PLAYER_COMBAT_FIELDS = ("percent", "shield_hp", "hitlag", "hitstun", "hurtbox_state")
_PLAYER_PROVENANCE_FIELDS = (
    "source_port0",
    "last_hit_by",
    "instance_hit_by",
    "instance_id",
    "last_attack_landed",
    "combo_count",
    "damage_post_hitlag_cb_kind",
    "source_clear_timer_x18c8",
    "fighter_8006cda4_pre_gate_consume_count",
    "throw_pulse_consumed",
    "throw_pulse_crossed_prev_frame",
    "throw_command_pending_pulse_frame",
)
_ITEM_FIELDS = (
    "exists",
    "type",
    "state",
    "owner",
    "instance_id",
    "attack_id",
    "attack_instance",
    "direction",
    "pos_x",
    "pos_y",
    "vel_x",
    "vel_y",
    "damage",
    "timer",
    "spawn_id",
    "misc0",
    "misc1",
    "misc2",
    "misc3",
)


def _fmt_value(value: Any) -> str:
    if isinstance(value, np.generic):
        value = value.item()
    if isinstance(value, float):
        return f"{value:.6g}"
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _field(entity: np.void, name: str, index: int | None = None) -> str:
    if entity.dtype.names is None or name not in entity.dtype.names:
        return "<missing>"
    value = entity[name]
    if index is not None:
        try:
            value = value[index]
        except IndexError:
            return "<missing>"
    return _fmt_value(value)


def _fields(entity: np.void, fields: tuple[str, ...], player: int) -> str:
    return " ".join(f"{name}={_field(entity, name, player)}" for name in fields)


def _item_fields(entity: np.void, item_slot: int) -> str:
    if entity.dtype.names is None or "items" not in entity.dtype.names:
        return "<missing>"
    items = entity["items"]
    if item_slot < 0 or item_slot >= int(items.shape[0]):
        return "<missing>"
    item = items[item_slot]
    return " ".join(f"{name}={_field(item, name)}" for name in _ITEM_FIELDS)


def _dataset_candidates(suite_path: Path, datasets_dir: str | Path) -> list[tuple[str, Path]]:
    suite = load_suite(suite_path)
    return [
        (
            entry.replay,
            dataset_path_for_suite_replay(
                suite_name=suite.name,
                replay_rel_path=entry.replay,
                datasets_dir=datasets_dir,
            ),
        )
        for entry in suite.replays
    ]


def _resolve_dataset_path(suite_path: Path, datasets_dir: str | Path, replay: str | None) -> Path:
    if replay is not None:
        replay_path = Path(replay)
        if replay_path.suffix == ".msl" and replay_path.exists():
            return replay_path.resolve()

    candidates = _dataset_candidates(suite_path, datasets_dir)
    if replay is None:
        if len(candidates) != 1:
            names = ", ".join(Path(name).name for name, _path in candidates[:8])
            raise ValueError(
                f"--replay is required for suite with {len(candidates)} replays"
                + (f" (examples: {names})" if names else "")
            )
        return candidates[0][1]

    matches: list[Path] = []
    for replay_rel, dataset_path in candidates:
        replay_name = Path(replay_rel).name
        dataset_name = dataset_path.name
        if replay in {replay_rel, replay_name, dataset_name, str(dataset_path)}:
            matches.append(dataset_path)
        elif replay_name.startswith(replay) or dataset_name.startswith(replay):
            matches.append(dataset_path)
    unique = sorted(set(matches))
    if len(unique) == 1:
        return unique[0]
    if not unique:
        raise FileNotFoundError(f"no suite replay matched --replay {replay!r}")
    raise ValueError(f"ambiguous --replay {replay!r}: {', '.join(str(p) for p in unique[:8])}")


def format_probe(
    *,
    dataset_path: Path,
    record: int,
    player: int,
    window: int,
    item_slot: int | None,
) -> str:
    if record < 0:
        raise IndexError(f"record must be nonnegative, got {record}")
    if item_slot is not None and (item_slot < 0 or item_slot >= MAX_ITEMS):
        raise IndexError(f"item slot {item_slot} out of range for {MAX_ITEMS} items")

    start = max(0, record - max(0, window))
    stop_exclusive = record + max(0, window) + 1
    ds = read_dataset_window(str(dataset_path), start, stop_exclusive)
    num_records = int(ds.header["num_records"])
    num_players = int(ds.header["num_players"])
    if record < 0 or record >= num_records:
        raise IndexError(f"record {record} out of range for {num_records} records")
    if player < 0 or player >= min(num_players, MAX_PLAYERS):
        raise IndexError(f"player {player} out of range for {num_players} players")
    stop = start + int(ds.samples.shape[0]) - 1
    rows = ds.samples

    out = [
        f"dataset: {dataset_path}",
        f"access: direct-window rows={start}..{stop}",
        f"records: {start}..{stop} target={record} player={player}"
        + (f" item={item_slot}" if item_slot is not None else ""),
    ]
    for offset, sample in enumerate(rows):
        row_index = start + offset
        marker = "*" if row_index == record else " "
        seed = sample["seed_t"]
        ref = sample["ref_t1"]
        out.append(f"{marker} record={row_index} seed.frame_id={_field(seed, 'frame_id')}")
        out.append(f"  seed.p{player}.core: {_fields(seed, _PLAYER_CORE_FIELDS, player)}")
        out.append(f"  seed.p{player}.pos: {_fields(seed, _PLAYER_POSITION_FIELDS, player)}")
        out.append(f"  seed.p{player}.vel: {_fields(seed, _PLAYER_VELOCITY_FIELDS, player)}")
        out.append(f"  seed.p{player}.combat: {_fields(seed, _PLAYER_COMBAT_FIELDS, player)}")
        out.append(f"  seed.p{player}.prov: {_fields(seed, _PLAYER_PROVENANCE_FIELDS, player)}")
        out.append(f"  ref.p{player}.core: {_fields(ref, _PLAYER_CORE_FIELDS, player)}")
        out.append(f"  ref.p{player}.pos: {_fields(ref, _PLAYER_POSITION_FIELDS, player)}")
        out.append(f"  ref.p{player}.vel: {_fields(ref, _PLAYER_VELOCITY_FIELDS, player)}")
        out.append(f"  ref.p{player}.combat: {_fields(ref, _PLAYER_COMBAT_FIELDS, player)}")
        out.append(f"  ref.p{player}.prov: {_fields(ref, _PLAYER_PROVENANCE_FIELDS, player)}")
        if item_slot is not None:
            out.append(f"  seed.item{item_slot}: {_item_fields(seed, item_slot)}")
            out.append(f"  ref.item{item_slot}: {_item_fields(ref, item_slot)}")
    return "\n".join(out)


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="Print a compact direct-index .msl row probe.")
    ap.add_argument("--suite", required=True, type=Path, help="Suite JSON path.")
    ap.add_argument("--datasets-dir", default="datasets", help="Dataset cache root.")
    ap.add_argument("--replay", help="Replay name, suite replay path, dataset filename, or .msl path.")
    ap.add_argument("--record", required=True, type=int, help="Target record index.")
    ap.add_argument("--player", required=True, type=int, help="Player slot index.")
    ap.add_argument("--window", type=int, default=0, help="Rows before/after target to print.")
    ap.add_argument("--item", type=int, help="Optional item slot index to print.")
    args = ap.parse_args(argv)

    root = repo_root()
    suite_path = args.suite if args.suite.is_absolute() else root / args.suite
    dataset_path = _resolve_dataset_path(suite_path, args.datasets_dir, args.replay)
    print(
        format_probe(
            dataset_path=dataset_path,
            record=int(args.record),
            player=int(args.player),
            window=max(0, int(args.window)),
            item_slot=args.item,
        )
    )


if __name__ == "__main__":
    main()
