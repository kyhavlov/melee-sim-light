from __future__ import annotations

"""Rank fixed-horizon rollout desyncs by weighted gameplay divergence.

For each replay record t, this tool reseeds from seed_t, advances with replay inputs
without reseeding, and scores the sim-vs-replay compare row at configured horizons.
It also records the first mismatching frame/field within the rollout window.
"""

import argparse
import concurrent.futures
import csv
import importlib
import json
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.facing_residual_blocker_report import load_action_id_names
from tools.eval.mismatch_taxonomy import (
    PlayerRow,
    _action_name,
    _classify_player_row,
    family_scope_reason,
    family_scope_tier,
)
from tools.eval.run_longest_rollout_streaks import _parse_csv, _parse_players, _validate_discrete_fields
from tools.eval.validation_profile import ValidationProfile, get_validation_profile, validation_profile_names
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


DEFAULT_DISCRETE_FIELDS = (
    "action_id",
    "animation_index",
    "action_frame",
    "on_ground",
    "hitlag",
    "hitstun",
    "state_flags",
    "jumps_left",
    "stocks",
    "is_dead",
    "hurtbox_state",
    "ground_id",
    "instance_id",
    "instance_hit_by",
    "last_attack_landed",
    "combo_count",
    "last_hit_by",
)

DEFAULT_FLOAT_FIELDS = (
    "pos_x",
    "pos_y",
    "speed_air_x_self",
    "speed_ground_x_self",
    "speed_y_self",
    "speed_x_attack",
    "speed_y_attack",
    "percent",
    "shield_hp",
)

ROW_COLUMNS = (
    "suite",
    "dataset",
    "record",
    "seed_frame",
    "horizon",
    "ref_frame",
    "player",
    "family_id",
    "scope_tier",
    "scope_reason",
    "action_state",
    "field_cluster",
    "first_mismatch_offset",
    "first_mismatch_field",
    "first_mismatch_subindex",
    "first_mismatch_player",
    "first_out",
    "first_ref",
    "score_total",
    "score_discrete",
    "score_float",
    "score_item",
    "seed_action_id",
    "out_action_id",
    "ref_action_id",
    "seed_action_frame",
    "out_action_frame",
    "ref_action_frame",
    "on_ground",
    "hitlag",
    "hitstun",
    "cluster_key",
)

CLUSTER_COLUMNS = (
    "cluster_key",
    "suite",
    "horizon",
    "family_id",
    "scope_tier",
    "scope_reason",
    "action_state",
    "dataset",
    "player",
    "field_cluster",
    "frequency",
    "score_sum",
    "score_avg",
    "score_max",
    "first_mismatch_min_offset",
    "example_record",
    "example_seed_frame",
    "example_ref_frame",
    "example_first_field",
    "example_first_player",
)

SCORE_FORMULA = {
    "discrete": {
        "action_id": 140.0,
        "animation_index": 120.0,
        "on_ground": 90.0,
        "stocks": 1000.0,
        "is_dead": 300.0,
        "jumps_left": 30.0,
        "l_cancel": 20.0,
        "hurtbox_state": 30.0,
        "ground_id": 20.0,
        "instance_id": 20.0,
        "instance_hit_by": 40.0,
        "last_attack_landed": 30.0,
        "combo_count": 25.0,
        "last_hit_by": 30.0,
        "state_flags": 15.0,
    },
    "integer_delta": {
        "action_frame": "6.0 * min(abs(delta), 10)",
        "hitlag": "25.0 * min(abs(delta), 10)",
        "hitstun": "12.0 * min(abs(delta), 20)",
    },
    "float": {
        "pos_x,pos_y": "12.0 * min(abs(delta), 20), epsilon 0.05",
        "speed_*": "4.0 * min(abs(delta), 20), epsilon 0.05",
        "percent": "40.0 * min(abs(delta), 30), epsilon 0.05",
        "shield_hp": "20.0 * min(abs(delta), 15), epsilon 0.05",
    },
    "items": {
        "exists": 100.0,
        "state,type": 60.0,
        "owner": 50.0,
        "position": "8.0 * min(abs(delta), 20), epsilon 0.05",
    },
}

NATIVE_FIELD_CODE_TO_NAME = {
    0: "none",
    1: "action_id",
    2: "animation_index",
    3: "action_frame",
    4: "on_ground",
    5: "hitlag",
    6: "hitstun",
    7: "state_flags",
    8: "jumps_left",
    9: "stocks",
    10: "is_dead",
    11: "hurtbox_state",
    12: "ground_id",
    13: "instance_id",
    14: "instance_hit_by",
    15: "last_attack_landed",
    16: "combo_count",
    17: "last_hit_by",
    18: "l_cancel",
    19: "facing",
    20: "pos_x",
    21: "pos_y",
    22: "speed_air_x_self",
    23: "speed_ground_x_self",
    24: "speed_y_self",
    25: "speed_x_attack",
    26: "speed_y_attack",
    27: "percent",
    28: "shield_hp",
    100: "state_flags[0]",
    101: "state_flags[1]",
    102: "state_flags[2]",
    103: "state_flags[3]",
    104: "state_flags[4]",
    200: "item_exists",
    201: "item_state",
    202: "item_type",
    203: "item_owner",
    204: "item_instance_id",
    205: "item_pos_x",
    206: "item_pos_y",
    207: "item_vel_x",
    208: "item_vel_y",
}

NATIVE_MASK_FIELDS = (
    "action_id",
    "animation_index",
    "action_frame",
    "on_ground",
    "hitlag",
    "hitstun",
    "state_flags[0]",
    "state_flags[1]",
    "state_flags[2]",
    "state_flags[3]",
    "state_flags[4]",
    "jumps_left",
    "stocks",
    "is_dead",
    "hurtbox_state",
    "ground_id",
    "instance_id",
    "instance_hit_by",
    "last_attack_landed",
    "combo_count",
    "last_hit_by",
    "l_cancel",
    "facing",
    "pos_x",
    "pos_y",
    "speed_air_x_self",
    "speed_ground_x_self",
    "speed_y_self",
    "speed_x_attack",
    "speed_y_attack",
    "percent",
    "shield_hp",
)

NATIVE_INT_RECORD = 0
NATIVE_INT_SEED_FRAME = 1
NATIVE_INT_HORIZON = 2
NATIVE_INT_REF_FRAME = 3
NATIVE_INT_PLAYER = 4
NATIVE_INT_FIRST_OFFSET = 5
NATIVE_INT_FIRST_FIELD_CODE = 6
NATIVE_INT_FIRST_SUBINDEX = 7
NATIVE_INT_FIRST_PLAYER = 8
NATIVE_INT_FIRST_KIND = 9
NATIVE_INT_FIRST_OUT_I = 10
NATIVE_INT_FIRST_REF_I = 11
NATIVE_INT_SEED_ACTION_ID = 12
NATIVE_INT_OUT_ACTION_ID = 13
NATIVE_INT_REF_ACTION_ID = 14
NATIVE_INT_SEED_ACTION_FRAME = 15
NATIVE_INT_OUT_ACTION_FRAME = 16
NATIVE_INT_REF_ACTION_FRAME = 17
NATIVE_INT_ON_GROUND = 18
NATIVE_INT_HITLAG = 19
NATIVE_INT_HITSTUN = 20
NATIVE_INT_FAMILY_FIELD_MASK = 21

NATIVE_FLOAT_FIRST_OUT_F = 0
NATIVE_FLOAT_FIRST_REF_F = 1
NATIVE_FLOAT_SCORE_TOTAL = 2
NATIVE_FLOAT_SCORE_DISCRETE = 3
NATIVE_FLOAT_SCORE_FLOAT = 4
NATIVE_FLOAT_SCORE_ITEM = 5


@dataclass(frozen=True)
class DisruptiveRow:
    suite: str
    dataset: str
    record: int
    seed_frame: int
    horizon: int
    ref_frame: int
    player: int
    family_id: str
    action_state: str
    field_cluster: str
    first_mismatch_offset: int
    first_mismatch_field: str
    first_mismatch_subindex: int
    first_mismatch_player: int
    first_out: str
    first_ref: str
    score_total: float
    score_discrete: float
    score_float: float
    score_item: float
    seed_action_id: int
    out_action_id: int
    ref_action_id: int
    seed_action_frame: int
    out_action_frame: int
    ref_action_frame: int
    on_ground: int
    hitlag: int
    hitstun: int
    cluster_key: str
    scope_tier: str = ""
    scope_reason: str = ""


@dataclass(frozen=True)
class ClusterSummary:
    cluster_key: str
    suite: str
    horizon: int
    family_id: str
    scope_tier: str
    scope_reason: str
    action_state: str
    dataset: str
    player: int
    field_cluster: str
    frequency: int
    score_sum: float
    score_avg: float
    score_max: float
    first_mismatch_min_offset: int
    example_record: int
    example_seed_frame: int
    example_ref_frame: int
    example_first_field: str
    example_first_player: int


def _load_binding():
    return importlib.import_module("msl_binding")


def _float_text(value: Any) -> str:
    return f"{float(np.asarray(value).item()):.6g}"


def _base_field(field: str) -> str:
    if field.startswith("state_flags["):
        return field
    if field.startswith("item_"):
        return field
    return field


def _field_cluster(field: str) -> str:
    base = _base_field(field)
    if base in {"action_id", "animation_index", "action_frame", "instance_id"}:
        return "action"
    if base in {"on_ground", "ground_id", "jumps_left", "pos_x", "pos_y"}:
        return "position_collision"
    if base.startswith("speed_"):
        return "velocity"
    if base in {
        "hitlag",
        "hitstun",
        "percent",
        "shield_hp",
        "stocks",
        "is_dead",
        "instance_hit_by",
        "last_attack_landed",
        "combo_count",
        "last_hit_by",
    }:
        return "combat"
    if base.startswith("state_flags[") or base in {"state_flags", "hurtbox_state", "l_cancel"}:
        return "state_flags"
    if base.startswith("item_"):
        return "items"
    return "other"


def _cluster_key(
    *,
    horizon: int,
    family_id: str,
    action_state: str,
    dataset: str,
    player: int,
    field_cluster: str,
) -> str:
    replay = Path(dataset).name
    return (
        f"v1|h={int(horizon)}|family={family_id}|action={action_state}|"
        f"replay={replay}|player={int(player)}|cluster={field_cluster}"
    )


def _row_to_values(row: DisruptiveRow) -> tuple[object, ...]:
    return tuple(getattr(row, name) for name in ROW_COLUMNS)


def _cluster_to_values(row: ClusterSummary) -> tuple[object, ...]:
    return tuple(getattr(row, name) for name in CLUSTER_COLUMNS)


def _write_tsv(path: Path, columns: tuple[str, ...], rows: Iterable[tuple[object, ...]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t", lineterminator="\n")
        writer.writerow(columns)
        writer.writerows(rows)


_DISRUPTIVE_INT_COLUMNS = {
    "record",
    "seed_frame",
    "horizon",
    "ref_frame",
    "player",
    "first_mismatch_offset",
    "first_mismatch_subindex",
    "first_mismatch_player",
    "seed_action_id",
    "out_action_id",
    "ref_action_id",
    "seed_action_frame",
    "out_action_frame",
    "ref_action_frame",
    "on_ground",
    "hitlag",
    "hitstun",
}

_DISRUPTIVE_FLOAT_COLUMNS = {"score_total", "score_discrete", "score_float", "score_item"}


def _read_rows_tsv(path: Path) -> list[DisruptiveRow]:
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        if reader.fieldnames is None:
            return []
        optional_columns = {"scope_tier", "scope_reason"}
        missing = [
            column for column in ROW_COLUMNS if column not in reader.fieldnames and column not in optional_columns
        ]
        if missing:
            raise SystemExit(f"error: {path} is missing columns: {', '.join(missing)}")
        rows: list[DisruptiveRow] = []
        for raw in reader:
            values: dict[str, Any] = {}
            for column in ROW_COLUMNS:
                value = raw.get(column, "")
                if column in _DISRUPTIVE_INT_COLUMNS:
                    values[column] = int(value)
                elif column in _DISRUPTIVE_FLOAT_COLUMNS:
                    values[column] = float(value)
                else:
                    values[column] = value
            if not values["scope_tier"]:
                values["scope_tier"] = family_scope_tier(str(values["family_id"]))
            if not values["scope_reason"]:
                values["scope_reason"] = family_scope_reason(str(values["family_id"]))
            rows.append(DisruptiveRow(**values))
    return rows


def _native_mask_fields(mask: int) -> tuple[str, ...]:
    return tuple(field for bit, field in enumerate(NATIVE_MASK_FIELDS) if int(mask) & (1 << bit))


def _row_from_native(
    *,
    suite_name: str,
    dataset_label: str,
    samples: np.ndarray,
    ints: np.ndarray,
    floats: np.ndarray,
    row_i: int,
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    float_epsilon: float,
    action_names: dict[int, str],
    profile: ValidationProfile,
) -> DisruptiveRow:
    _ = (discrete_fields, float_fields, float_epsilon, profile)
    raw_i = ints[row_i]
    raw_f = floats[row_i]
    record = int(raw_i[NATIVE_INT_RECORD])
    player = int(raw_i[NATIVE_INT_PLAYER])
    first_field = NATIVE_FIELD_CODE_TO_NAME.get(int(raw_i[NATIVE_INT_FIRST_FIELD_CODE]), "none")
    first_kind = int(raw_i[NATIVE_INT_FIRST_KIND])
    first_out = _float_text(raw_f[NATIVE_FLOAT_FIRST_OUT_F]) if first_kind == 1 else str(int(raw_i[NATIVE_INT_FIRST_OUT_I]))
    first_ref = _float_text(raw_f[NATIVE_FLOAT_FIRST_REF_F]) if first_kind == 1 else str(int(raw_i[NATIVE_INT_FIRST_REF_I]))

    if player < 0:
        family_id = "F00_rollout_items"
        action_state = "item_slot"
        family_fields = (first_field,) if first_field != "none" else ()
    else:
        mask_fields = _native_mask_fields(int(raw_i[NATIVE_INT_FAMILY_FIELD_MASK]))
        family_fields = mask_fields or ((first_field,) if first_field != "none" else ())
        seed_row = samples["seed_t"][record]
        row = PlayerRow(
            dataset=dataset_label,
            record=record,
            p=player,
            seed_frame=int(raw_i[NATIVE_INT_SEED_FRAME]),
            ref_frame=int(raw_i[NATIVE_INT_REF_FRAME]),
            seed_action_id=int(raw_i[NATIVE_INT_SEED_ACTION_ID]),
            ref_action_id=int(raw_i[NATIVE_INT_REF_ACTION_ID]),
            out_action_id=int(raw_i[NATIVE_INT_OUT_ACTION_ID]),
            prev_action_id=int(seed_row["seed_prev_action_id"][player]),
            seed_action_frame=int(raw_i[NATIVE_INT_SEED_ACTION_FRAME]),
            ref_action_frame=int(raw_i[NATIVE_INT_REF_ACTION_FRAME]),
            out_action_frame=int(raw_i[NATIVE_INT_OUT_ACTION_FRAME]),
            on_ground=int(seed_row["on_ground"][player]),
            hitlag=int(seed_row["hitlag"][player]),
            hitstun=int(seed_row["hitstun"][player]),
            fields=tuple(field for field in family_fields if not field.startswith("item_")),
            family_id="",
        )
        family_id = _classify_player_row(row, action_names)
        action_state = (
            f"seed={_action_name(action_names, int(raw_i[NATIVE_INT_SEED_ACTION_ID]))}"
            f"|out={_action_name(action_names, int(raw_i[NATIVE_INT_OUT_ACTION_ID]))}"
            f"|ref={_action_name(action_names, int(raw_i[NATIVE_INT_REF_ACTION_ID]))}"
            f"|ground={int(raw_i[NATIVE_INT_ON_GROUND])}"
            f"|hitlag={int(raw_i[NATIVE_INT_HITLAG]) > 0}"
            f"|hitstun={int(raw_i[NATIVE_INT_HITSTUN]) > 0}"
        )

    field_cluster = _field_cluster(first_field if first_field != "none" else family_fields[0])
    cluster_key = _cluster_key(
        horizon=int(raw_i[NATIVE_INT_HORIZON]),
        family_id=family_id,
        action_state=action_state,
        dataset=dataset_label,
        player=player,
        field_cluster=field_cluster,
    )
    return DisruptiveRow(
        suite=suite_name,
        dataset=dataset_label,
        record=record,
        seed_frame=int(raw_i[NATIVE_INT_SEED_FRAME]),
        horizon=int(raw_i[NATIVE_INT_HORIZON]),
        ref_frame=int(raw_i[NATIVE_INT_REF_FRAME]),
        player=player,
        family_id=family_id,
        action_state=action_state,
        field_cluster=field_cluster,
        first_mismatch_offset=int(raw_i[NATIVE_INT_FIRST_OFFSET]),
        first_mismatch_field=first_field,
        first_mismatch_subindex=int(raw_i[NATIVE_INT_FIRST_SUBINDEX]),
        first_mismatch_player=int(raw_i[NATIVE_INT_FIRST_PLAYER]),
        first_out=first_out,
        first_ref=first_ref,
        score_total=float(raw_f[NATIVE_FLOAT_SCORE_TOTAL]),
        score_discrete=float(raw_f[NATIVE_FLOAT_SCORE_DISCRETE]),
        score_float=float(raw_f[NATIVE_FLOAT_SCORE_FLOAT]),
        score_item=float(raw_f[NATIVE_FLOAT_SCORE_ITEM]),
        seed_action_id=int(raw_i[NATIVE_INT_SEED_ACTION_ID]),
        out_action_id=int(raw_i[NATIVE_INT_OUT_ACTION_ID]),
        ref_action_id=int(raw_i[NATIVE_INT_REF_ACTION_ID]),
        seed_action_frame=int(raw_i[NATIVE_INT_SEED_ACTION_FRAME]),
        out_action_frame=int(raw_i[NATIVE_INT_OUT_ACTION_FRAME]),
        ref_action_frame=int(raw_i[NATIVE_INT_REF_ACTION_FRAME]),
        on_ground=int(raw_i[NATIVE_INT_ON_GROUND]),
        hitlag=int(raw_i[NATIVE_INT_HITLAG]),
        hitstun=int(raw_i[NATIVE_INT_HITSTUN]),
        cluster_key=cluster_key,
        scope_tier=family_scope_tier(family_id),
        scope_reason=family_scope_reason(family_id),
    )


def _scan_dataset(
    *,
    suite_name: str,
    dataset_path: Path,
    dataset_label: str,
    horizons: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    players: tuple[int, ...],
    max_records: int,
    stride: int,
    float_epsilon: float,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    action_names: dict[int, str],
    batch_size: int,
    profile: ValidationProfile,
    start_record: int = 0,
    stop_record: int = 0,
) -> list[DisruptiveRow]:
    binding = _load_binding()
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), int(samples.dtype.itemsize))
    ints, floats = binding.disruptive_scan(
        samples_u8,
        tuple(int(v) for v in horizons),
        tuple(str(v) for v in discrete_fields),
        tuple(str(v) for v in float_fields),
        tuple(int(v) for v in players),
        int(ds.header["num_players"]),
        int(max_records),
        int(stride),
        float(float_epsilon),
        -1 if ucf_enabled is None else int(bool(ucf_enabled)),
        -1 if ucf_cardinals_1_0_enabled is None else int(bool(ucf_cardinals_1_0_enabled)),
        int(batch_size),
        int(start_record),
        int(stop_record),
        1 if profile.name == "rl1_gameplay" else 0,
    )
    return [
        _row_from_native(
            suite_name=suite_name,
            dataset_label=dataset_label,
            samples=samples,
            ints=ints,
            floats=floats,
            row_i=i,
            discrete_fields=discrete_fields,
            float_fields=float_fields,
            float_epsilon=float_epsilon,
            action_names=action_names,
            profile=profile,
        )
        for i in range(int(ints.shape[0]))
    ]


def _scan_dataset_task(task: dict[str, Any]) -> list[DisruptiveRow]:
    profile = get_validation_profile(str(task.get("profile", "rl1_gameplay")))
    return _scan_dataset(
        suite_name=str(task["suite_name"]),
        dataset_path=Path(str(task["dataset_path"])),
        dataset_label=str(task["dataset_label"]),
        horizons=tuple(int(v) for v in task["horizons"]),
        discrete_fields=tuple(str(v) for v in task["discrete_fields"]),
        float_fields=tuple(str(v) for v in task["float_fields"]),
        players=tuple(int(v) for v in task["players"]),
        max_records=int(task["max_records"]),
        stride=int(task["stride"]),
        float_epsilon=float(task["float_epsilon"]),
        ucf_enabled=task["ucf_enabled"],
        ucf_cardinals_1_0_enabled=task["ucf_cardinals_1_0_enabled"],
        action_names=dict(task["action_names"]),
        batch_size=int(task["batch_size"]),
        profile=profile,
        start_record=int(task["start_record"]),
        stop_record=int(task["stop_record"]),
    )


def _summarize_clusters(rows: Iterable[DisruptiveRow]) -> list[ClusterSummary]:
    grouped: dict[str, list[DisruptiveRow]] = defaultdict(list)
    for row in rows:
        grouped[row.cluster_key].append(row)
    out: list[ClusterSummary] = []
    for key, group in grouped.items():
        ex = max(group, key=lambda row: row.score_total)
        score_sum = sum(row.score_total for row in group)
        out.append(
            ClusterSummary(
                cluster_key=key,
                suite=ex.suite,
                horizon=int(ex.horizon),
                family_id=ex.family_id,
                scope_tier=family_scope_tier(ex.family_id),
                scope_reason=family_scope_reason(ex.family_id),
                action_state=ex.action_state,
                dataset=Path(ex.dataset).name,
                player=int(ex.player),
                field_cluster=ex.field_cluster,
                frequency=len(group),
                score_sum=score_sum,
                score_avg=score_sum / float(len(group)),
                score_max=max(row.score_total for row in group),
                first_mismatch_min_offset=min(row.first_mismatch_offset for row in group),
                example_record=int(ex.record),
                example_seed_frame=int(ex.seed_frame),
                example_ref_frame=int(ex.ref_frame),
                example_first_field=ex.first_mismatch_field,
                example_first_player=int(ex.first_mismatch_player),
            )
        )
    return sorted(out, key=lambda row: (-row.score_sum, -row.frequency, row.cluster_key))


def _summary_json(
    *,
    suite_path: Path,
    suite_name: str,
    datasets_dir: str,
    horizons: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    rows: list[DisruptiveRow],
    clusters: list[ClusterSummary],
    top: int,
    stride: int,
    max_records: int,
    batch_size: int,
    workers: int,
    chunk_records: int,
    float_epsilon: float,
    profile: ValidationProfile,
) -> dict[str, Any]:
    scope_counts: dict[str, int] = defaultdict(int)
    for row in rows:
        scope_counts[row.scope_tier or family_scope_tier(row.family_id)] += 1
    return {
        "suite": suite_name,
        "suite_path": str(suite_path),
        "datasets_dir": datasets_dir,
        "horizons": list(horizons),
        "stride": int(stride),
        "max_records": int(max_records),
        "batch_size": int(batch_size),
        "workers": int(workers),
        "chunk_records": int(chunk_records),
        "float_epsilon": float(float_epsilon),
        "profile": profile.name,
        "ignored_lanes": [lane.label for lane in profile.ignored_lanes],
        "discrete_fields": list(discrete_fields),
        "float_fields": list(float_fields),
        "scoring_formula": SCORE_FORMULA,
        "row_count": len(rows),
        "cluster_count": len(clusters),
        "scope_counts": dict(sorted(scope_counts.items())),
        "top_clusters": [asdict(row) for row in clusters[: max(1, int(top))]],
    }


def main() -> None:
    ap = argparse.ArgumentParser(description="Rank most disruptive fixed-horizon rollout desyncs.")
    ap.add_argument("--suite", required=True, help="Suite JSON path under repo root.")
    ap.add_argument("--datasets-dir", default="datasets", help="Datasets directory under repo root.")
    ap.add_argument("--horizons", default="10,20,60", help="Comma-separated rollout horizons.")
    ap.add_argument(
        "--discrete-fields",
        default=",".join(DEFAULT_DISCRETE_FIELDS),
        help="Comma-separated discrete compare fields.",
    )
    ap.add_argument(
        "--float-fields",
        default=",".join(DEFAULT_FLOAT_FIELDS),
        help="Comma-separated float compare fields.",
    )
    ap.add_argument("--float-epsilon", type=float, default=0.05, help="Float mismatch epsilon.")
    ap.add_argument("--players", default=None, help="Comma-separated player indices; default all players.")
    ap.add_argument("--dataset-filter", default="", help="Optional substring filter on dataset relative path.")
    ap.add_argument("--max-records", type=int, default=0, help="Optional per-dataset start-record cap.")
    ap.add_argument("--stride", type=int, default=1, help="Start-record stride; default scans every row.")
    ap.add_argument("--batch-size", type=int, default=512, help="Rollout start rows to scan per sim batch.")
    ap.add_argument("--workers", type=int, default=1, help="Parallel dataset/chunk worker processes.")
    ap.add_argument("--chunk-records", type=int, default=2048, help="Start rows per worker task.")
    ap.add_argument("--top", type=int, default=20, help="Top-N clusters to print and include in summary.")
    ap.add_argument(
        "--profile",
        default="rl1_gameplay",
        choices=validation_profile_names(),
        help="Validation scoring profile. strict scores every compare lane; rl1_gameplay ignores RL1-irrelevant lanes.",
    )
    ap.add_argument("--out-dir", type=Path, default=None, help="Output directory under reports/triage.")
    ap.add_argument(
        "--rows-in",
        type=Path,
        default=None,
        help="Existing disruptive rows.tsv to rerank without rerunning rollout simulation.",
    )
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    horizons = tuple(sorted({int(v) for v in _parse_csv(str(args.horizons))}))
    if not horizons or min(horizons) <= 0:
        raise SystemExit("error: horizons must be positive integers")
    discrete_fields = _validate_discrete_fields(_parse_csv(str(args.discrete_fields)))
    float_fields = tuple(_parse_csv(str(args.float_fields)))
    bad_float = [field for field in float_fields if field not in COMPARE_DTYPE.fields]
    if bad_float:
        raise SystemExit(f"error: unknown float fields: {', '.join(bad_float)}")
    validation_profile = get_validation_profile(args.profile)

    out_dir = args.out_dir or (root / "reports" / "triage" / f"disruptive_rollout_desyncs_{suite.name}")
    if not out_dir.is_absolute():
        out_dir = root / out_dir

    if args.rows_in is not None:
        rows_in = args.rows_in if args.rows_in.is_absolute() else root / args.rows_in
        all_rows = _read_rows_tsv(rows_in)
        clusters = _summarize_clusters(all_rows)
        rows_path = out_dir / "rows.tsv"
        clusters_path = out_dir / "clusters.tsv"
        summary_path = out_dir / "summary.json"
        _write_tsv(rows_path, ROW_COLUMNS, (_row_to_values(row) for row in all_rows))
        _write_tsv(clusters_path, CLUSTER_COLUMNS, (_cluster_to_values(row) for row in clusters))
        summary = _summary_json(
            suite_path=suite_path,
            suite_name=suite.name,
            datasets_dir=str(args.datasets_dir),
            horizons=tuple(sorted({row.horizon for row in all_rows})) or horizons,
            discrete_fields=discrete_fields,
            float_fields=float_fields,
            rows=all_rows,
            clusters=clusters,
            top=max(1, int(args.top)),
            stride=max(1, int(args.stride)),
            max_records=int(args.max_records),
            batch_size=max(1, int(args.batch_size)),
            workers=0,
            chunk_records=0,
            float_epsilon=float(args.float_epsilon),
            profile=validation_profile,
        )
        summary["rows_in"] = str(rows_in)
        out_dir.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"reranked: {rows_in}")
        print(f"wrote: {rows_path}")
        print(f"wrote: {clusters_path}")
        print(f"wrote: {summary_path}")
        print(f"rows={len(all_rows)} clusters={len(clusters)}")
        return

    dataset_paths: list[tuple[Path, str]] = []
    missing: list[str] = []
    for entry in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=str(args.datasets_dir),
        )
        rel = str(ds_path.resolve().relative_to(root))
        if str(args.dataset_filter) and str(args.dataset_filter) not in rel:
            continue
        if ds_path.exists():
            dataset_paths.append((ds_path, rel))
        else:
            missing.append(rel)
    if missing:
        print(f"Missing {len(missing)} preprocessed dataset files for suite {suite.name}:")
        for path in missing:
            print(f"  {path}")
        raise SystemExit(2)
    if not dataset_paths:
        raise SystemExit("error: no datasets selected")

    action_names = load_action_id_names()
    tasks: list[dict[str, Any]] = []
    chunk_records = max(1, int(args.chunk_records))
    for ds_path, rel in dataset_paths:
        ds = read_dataset(str(ds_path))
        players = _parse_players(args.players, num_players=int(ds.header["num_players"]))
        total_records = int(ds.header["num_records"])
        usable_records = max(0, int(ds.samples.shape[0]) - max(horizons) + 1)
        if int(args.max_records) > 0:
            usable_records = min(usable_records, int(args.max_records))
        starts = np.arange(0, usable_records, max(1, int(args.stride)), dtype=np.int64)
        task_count = (int(starts.shape[0]) + chunk_records - 1) // chunk_records
        print(f"scan: {rel} records={total_records} tasks={task_count}")
        for chunk_lo in range(0, int(starts.shape[0]), chunk_records):
            chunk_starts = starts[chunk_lo : chunk_lo + chunk_records]
            if int(chunk_starts.shape[0]) <= 0:
                continue
            tasks.append(
                {
                    "suite_name": suite.name,
                    "dataset_path": str(ds_path),
                    "dataset_label": rel,
                    "horizons": horizons,
                    "discrete_fields": discrete_fields,
                    "float_fields": float_fields,
                    "players": players,
                    "max_records": int(args.max_records),
                    "stride": max(1, int(args.stride)),
                    "float_epsilon": float(args.float_epsilon),
                    "ucf_enabled": suite.ucf_enabled,
                    "ucf_cardinals_1_0_enabled": suite.ucf_cardinals_1_0_enabled,
                    "action_names": action_names,
                    "batch_size": max(1, int(args.batch_size)),
                    "profile": validation_profile.name,
                    "start_record": int(chunk_starts[0]),
                    "stop_record": int(chunk_starts[-1]) + max(1, int(args.stride)),
                }
            )

    workers = max(1, int(args.workers))
    all_rows: list[DisruptiveRow] = []
    if workers == 1 or len(tasks) <= 1:
        for task in tasks:
            all_rows.extend(_scan_dataset_task(task))
    else:
        task_results: list[list[DisruptiveRow] | None] = [None for _ in tasks]
        with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as executor:
            future_to_index = {executor.submit(_scan_dataset_task, task): i for i, task in enumerate(tasks)}
            for future in concurrent.futures.as_completed(future_to_index):
                task_results[future_to_index[future]] = future.result()
        for result in task_results:
            if result:
                all_rows.extend(result)

    clusters = _summarize_clusters(all_rows)
    rows_path = out_dir / "rows.tsv"
    clusters_path = out_dir / "clusters.tsv"
    summary_path = out_dir / "summary.json"
    _write_tsv(rows_path, ROW_COLUMNS, (_row_to_values(row) for row in all_rows))
    _write_tsv(clusters_path, CLUSTER_COLUMNS, (_cluster_to_values(row) for row in clusters))
    summary = _summary_json(
        suite_path=suite_path,
        suite_name=suite.name,
        datasets_dir=str(args.datasets_dir),
        horizons=horizons,
        discrete_fields=discrete_fields,
        float_fields=float_fields,
        rows=all_rows,
        clusters=clusters,
        top=max(1, int(args.top)),
        stride=max(1, int(args.stride)),
        max_records=int(args.max_records),
        batch_size=max(1, int(args.batch_size)),
        workers=workers,
        chunk_records=chunk_records,
        float_epsilon=float(args.float_epsilon),
        profile=validation_profile,
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"wrote: {rows_path}")
    print(f"wrote: {clusters_path}")
    print(f"wrote: {summary_path}")
    print(f"rows={len(all_rows)} clusters={len(clusters)} horizons={','.join(str(h) for h in horizons)}")
    print("top_clusters:")
    for i, row in enumerate(clusters[: max(1, int(args.top))], start=1):
        print(
            f"{i:>2}. score={row.score_sum:.1f} freq={row.frequency:<5d} h={row.horizon:<2d} "
            f"{row.family_id} scope={row.scope_tier} {row.field_cluster} p={row.player} "
            f"example={row.dataset}:rec={row.example_record} first={row.example_first_field}"
        )


if __name__ == "__main__":
    main()
