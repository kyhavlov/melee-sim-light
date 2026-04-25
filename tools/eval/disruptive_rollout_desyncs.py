from __future__ import annotations

"""Rank fixed-horizon rollout desyncs by weighted gameplay divergence.

For each replay record t, this tool reseeds from seed_t, advances with replay inputs
without reseeding, and scores the sim-vs-replay compare row at configured horizons.
It also records the first mismatching frame/field within the rollout window.
"""

import argparse
import csv
import importlib
import json
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, MAX_ITEMS, read_dataset
from tools.eval.facing_residual_blocker_report import load_action_id_names
from tools.eval.mismatch_taxonomy import PlayerRow, _action_name, _classify_player_row
from tools.eval.run_longest_rollout_streaks import _parse_csv, _parse_players, _validate_discrete_fields
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


@dataclass(frozen=True)
class FirstMismatch:
    offset: int
    player: int
    field: str
    subindex: int
    out: str
    ref: str


@dataclass(frozen=True)
class ScoreBreakdown:
    total: float
    discrete: float
    floats: float
    items: float
    player_scores: tuple[float, ...]
    item_score: float


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


@dataclass(frozen=True)
class ClusterSummary:
    cluster_key: str
    suite: str
    horizon: int
    family_id: str
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


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def _float_text(value: Any) -> str:
    return f"{float(np.asarray(value).item()):.6g}"


def _int_text(value: Any) -> str:
    return str(int(np.asarray(value).item()))


def _float_weight(field: str) -> tuple[float, float]:
    if field in {"pos_x", "pos_y"}:
        return 12.0, 20.0
    if field == "percent":
        return 40.0, 30.0
    if field == "shield_hp":
        return 20.0, 15.0
    return 4.0, 20.0


def _discrete_field_score(field: str, out_v: int, ref_v: int) -> float:
    if out_v == ref_v:
        return 0.0
    if field == "action_frame":
        return 6.0 * _clamp(float(abs(out_v - ref_v)), 0.0, 10.0)
    if field == "hitlag":
        return 25.0 * _clamp(float(abs(out_v - ref_v)), 0.0, 10.0)
    if field == "hitstun":
        return 12.0 * _clamp(float(abs(out_v - ref_v)), 0.0, 20.0)
    if field == "stocks":
        return 1000.0 * float(abs(out_v - ref_v))
    return float(SCORE_FORMULA["discrete"].get(field, 10.0))


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


def _compare_first_mismatch(
    *,
    out_row: np.void,
    ref_row: np.void,
    players: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    offset: int,
    float_epsilon: float,
) -> FirstMismatch | None:
    for field in discrete_fields:
        out = out_row[field]
        ref = ref_row[field]
        if not hasattr(out, "ndim") or out.ndim == 0:
            if int(np.asarray(out).item()) != int(np.asarray(ref).item()):
                return FirstMismatch(offset, -1, field, -1, _int_text(out), _int_text(ref))
            continue
        if out.ndim == 1:
            for p in players:
                if int(out[p]) != int(ref[p]):
                    return FirstMismatch(offset, int(p), field, -1, _int_text(out[p]), _int_text(ref[p]))
            continue
        for p in players:
            out_slice = np.asarray(out[p])
            ref_slice = np.asarray(ref[p])
            for sub in range(int(out_slice.size)):
                if int(out_slice.flat[sub]) != int(ref_slice.flat[sub]):
                    return FirstMismatch(
                        offset,
                        int(p),
                        f"{field}[{sub}]",
                        int(sub),
                        _int_text(out_slice.flat[sub]),
                        _int_text(ref_slice.flat[sub]),
                    )

    for field in float_fields:
        out = out_row[field]
        ref = ref_row[field]
        for p in players:
            if abs(float(out[p]) - float(ref[p])) > float_epsilon:
                return FirstMismatch(offset, int(p), field, -1, _float_text(out[p]), _float_text(ref[p]))

    out_items = out_row["items"]
    ref_items = ref_row["items"]
    for slot in range(MAX_ITEMS):
        out_item = out_items[slot]
        ref_item = ref_items[slot]
        for subfield in ("exists", "state", "type", "owner", "instance_id"):
            if int(out_item[subfield]) != int(ref_item[subfield]):
                return FirstMismatch(
                    offset,
                    -1,
                    f"item_{subfield}",
                    slot,
                    _int_text(out_item[subfield]),
                    _int_text(ref_item[subfield]),
                )
        if int(out_item["exists"]) or int(ref_item["exists"]):
            for subfield in ("pos_x", "pos_y", "vel_x", "vel_y"):
                if abs(float(out_item[subfield]) - float(ref_item[subfield])) > float_epsilon:
                    return FirstMismatch(
                        offset,
                        -1,
                        f"item_{subfield}",
                        slot,
                        _float_text(out_item[subfield]),
                        _float_text(ref_item[subfield]),
                    )
    return None


def _score_horizon_row(
    *,
    out_row: np.void,
    ref_row: np.void,
    players: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    float_epsilon: float,
) -> ScoreBreakdown:
    player_scores = [0.0 for _ in range(max(players) + 1 if players else 0)]
    discrete_score = 0.0
    float_score = 0.0
    item_score = 0.0

    for field in discrete_fields:
        out = out_row[field]
        ref = ref_row[field]
        if not hasattr(out, "ndim") or out.ndim == 0:
            discrete_score += _discrete_field_score(field, int(np.asarray(out).item()), int(np.asarray(ref).item()))
            continue
        if out.ndim == 1:
            for p in players:
                score = _discrete_field_score(field, int(out[p]), int(ref[p]))
                player_scores[p] += score
                discrete_score += score
            continue
        for p in players:
            out_slice = np.asarray(out[p])
            ref_slice = np.asarray(ref[p])
            for sub in range(int(out_slice.size)):
                if int(out_slice.flat[sub]) != int(ref_slice.flat[sub]):
                    player_scores[p] += 15.0
                    discrete_score += 15.0

    for field in float_fields:
        out = out_row[field]
        ref = ref_row[field]
        weight, cap = _float_weight(field)
        for p in players:
            delta = abs(float(out[p]) - float(ref[p]))
            if delta > float_epsilon:
                score = weight * _clamp(delta, 0.0, cap)
                player_scores[p] += score
                float_score += score

    out_items = out_row["items"]
    ref_items = ref_row["items"]
    for slot in range(MAX_ITEMS):
        out_item = out_items[slot]
        ref_item = ref_items[slot]
        out_exists = int(out_item["exists"])
        ref_exists = int(ref_item["exists"])
        if out_exists != ref_exists:
            item_score += 100.0
        for subfield, weight in (("state", 60.0), ("type", 60.0), ("owner", 50.0)):
            if int(out_item[subfield]) != int(ref_item[subfield]):
                item_score += weight
        if out_exists or ref_exists:
            for subfield in ("pos_x", "pos_y"):
                delta = abs(float(out_item[subfield]) - float(ref_item[subfield]))
                if delta > float_epsilon:
                    item_score += 8.0 * _clamp(delta, 0.0, 20.0)

    total = discrete_score + float_score + item_score
    return ScoreBreakdown(
        total=total,
        discrete=discrete_score,
        floats=float_score,
        items=item_score,
        player_scores=tuple(player_scores),
        item_score=item_score,
    )


def _mismatched_player_fields(
    *,
    out_row: np.void,
    ref_row: np.void,
    player: int,
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    float_epsilon: float,
) -> tuple[str, ...]:
    fields: list[str] = []
    for field in discrete_fields:
        out = out_row[field]
        ref = ref_row[field]
        if not hasattr(out, "ndim") or out.ndim == 0:
            continue
        if out.ndim == 1:
            if int(out[player]) != int(ref[player]):
                fields.append(field)
            continue
        out_slice = np.asarray(out[player])
        ref_slice = np.asarray(ref[player])
        for sub in range(int(out_slice.size)):
            if int(out_slice.flat[sub]) != int(ref_slice.flat[sub]):
                fields.append(f"{field}[{sub}]")
    for field in float_fields:
        if abs(float(out_row[field][player]) - float(ref_row[field][player])) > float_epsilon:
            fields.append(field)
    return tuple(fields)


def _taxonomy_family(
    *,
    dataset: str,
    record: int,
    seed_row: np.void,
    out_row: np.void,
    ref_row: np.void,
    player: int,
    fields: tuple[str, ...],
    action_names: dict[int, str],
) -> str:
    taxonomy_fields = tuple(field for field in fields if not field.startswith("item_"))
    if not taxonomy_fields:
        cluster = _field_cluster(fields[0] if fields else "other")
        return f"F00_rollout_{cluster}"
    row = PlayerRow(
        dataset=dataset,
        record=int(record),
        p=int(player),
        seed_frame=int(seed_row["frame_id"]),
        ref_frame=int(ref_row["frame_id"]),
        seed_action_id=int(seed_row["action_id"][player]),
        ref_action_id=int(ref_row["action_id"][player]),
        out_action_id=int(out_row["action_id"][player]),
        prev_action_id=int(seed_row["seed_prev_action_id"][player]),
        seed_action_frame=int(seed_row["action_frame"][player]),
        ref_action_frame=int(ref_row["action_frame"][player]),
        out_action_frame=int(out_row["action_frame"][player]),
        on_ground=int(seed_row["on_ground"][player]),
        hitlag=int(seed_row["hitlag"][player]),
        hitstun=int(seed_row["hitstun"][player]),
        fields=taxonomy_fields,
        family_id="",
    )
    return _classify_player_row(row, action_names)


def _action_state(
    *,
    seed_row: np.void,
    out_row: np.void,
    ref_row: np.void,
    player: int,
    action_names: dict[int, str],
) -> str:
    seed_id = int(seed_row["action_id"][player])
    out_id = int(out_row["action_id"][player])
    ref_id = int(ref_row["action_id"][player])
    return (
        f"seed={_action_name(action_names, seed_id)}"
        f"|out={_action_name(action_names, out_id)}"
        f"|ref={_action_name(action_names, ref_id)}"
        f"|ground={int(ref_row['on_ground'][player])}"
        f"|hitlag={int(ref_row['hitlag'][player]) > 0}"
        f"|hitstun={int(ref_row['hitstun'][player]) > 0}"
    )


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
) -> list[DisruptiveRow]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    total_records = int(samples.shape[0])
    max_horizon = max(horizons)
    usable_records = max(0, total_records - max_horizon + 1)
    if max_records > 0:
        usable_records = min(usable_records, int(max_records))

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": 1, "num_players": int(ds.header["num_players"])}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    handle = binding.init(**init_kwargs)

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(total_records, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    horizon_set = set(horizons)
    rows: list[DisruptiveRow] = []
    try:
        for start in range(0, usable_records, max(1, int(stride))):
            seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
            binding.reseed_seed(handle, seed_bytes)
            first_mismatch: FirstMismatch | None = None
            for offset in range(1, max_horizon + 1):
                j = start + offset - 1
                prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
                input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
                binding.step_input(handle, prev_input_bytes, input_bytes)
                binding.write_compare(handle, out_compare_bytes)
                ref_row = samples["ref_t1"][j]
                if first_mismatch is None:
                    first_mismatch = _compare_first_mismatch(
                        out_row=out_view[0],
                        ref_row=ref_row,
                        players=players,
                        discrete_fields=discrete_fields,
                        float_fields=float_fields,
                        offset=offset,
                        float_epsilon=float_epsilon,
                    )
                if offset not in horizon_set:
                    continue

                score = _score_horizon_row(
                    out_row=out_view[0],
                    ref_row=ref_row,
                    players=players,
                    discrete_fields=discrete_fields,
                    float_fields=float_fields,
                    float_epsilon=float_epsilon,
                )
                if score.total <= 0.0:
                    continue

                player = max(players, key=lambda p: (score.player_scores[p], -p))
                if score.player_scores[player] <= 0.0 and score.item_score > 0.0:
                    player = -1
                family_fields = (
                    (first_mismatch.field,) if player < 0 else _mismatched_player_fields(
                        out_row=out_view[0],
                        ref_row=ref_row,
                        player=player,
                        discrete_fields=discrete_fields,
                        float_fields=float_fields,
                        float_epsilon=float_epsilon,
                    )
                )
                if not family_fields and first_mismatch is not None:
                    family_fields = (first_mismatch.field,)
                first = first_mismatch or FirstMismatch(offset, player, "none", -1, "", "")
                field_cluster = _field_cluster(first.field if first.field != "none" else family_fields[0])
                family_id = (
                    "F00_rollout_items"
                    if player < 0
                    else _taxonomy_family(
                        dataset=dataset_label,
                        record=start,
                        seed_row=samples["seed_t"][start],
                        out_row=out_view[0],
                        ref_row=ref_row,
                        player=player,
                        fields=family_fields,
                        action_names=action_names,
                    )
                )
                action_state = (
                    "item_slot"
                    if player < 0
                    else _action_state(
                        seed_row=samples["seed_t"][start],
                        out_row=out_view[0],
                        ref_row=ref_row,
                        player=player,
                        action_names=action_names,
                    )
                )
                cluster_key = _cluster_key(
                    horizon=offset,
                    family_id=family_id,
                    action_state=action_state,
                    dataset=dataset_label,
                    player=player,
                    field_cluster=field_cluster,
                )
                seed_row = samples["seed_t"][start]
                action_player = 0 if player < 0 else player
                rows.append(
                    DisruptiveRow(
                        suite=suite_name,
                        dataset=dataset_label,
                        record=start,
                        seed_frame=int(seed_row["frame_id"]),
                        horizon=offset,
                        ref_frame=int(ref_row["frame_id"]),
                        player=int(player),
                        family_id=family_id,
                        action_state=action_state,
                        field_cluster=field_cluster,
                        first_mismatch_offset=int(first.offset),
                        first_mismatch_field=first.field,
                        first_mismatch_subindex=int(first.subindex),
                        first_mismatch_player=int(first.player),
                        first_out=first.out,
                        first_ref=first.ref,
                        score_total=float(score.total),
                        score_discrete=float(score.discrete),
                        score_float=float(score.floats),
                        score_item=float(score.items),
                        seed_action_id=int(seed_row["action_id"][action_player]),
                        out_action_id=int(out_view[0]["action_id"][action_player]),
                        ref_action_id=int(ref_row["action_id"][action_player]),
                        seed_action_frame=int(seed_row["action_frame"][action_player]),
                        out_action_frame=int(out_view[0]["action_frame"][action_player]),
                        ref_action_frame=int(ref_row["action_frame"][action_player]),
                        on_ground=int(ref_row["on_ground"][action_player]),
                        hitlag=int(ref_row["hitlag"][action_player]),
                        hitstun=int(ref_row["hitstun"][action_player]),
                        cluster_key=cluster_key,
                    )
                )
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass
    return rows


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
    float_epsilon: float,
) -> dict[str, Any]:
    return {
        "suite": suite_name,
        "suite_path": str(suite_path),
        "datasets_dir": datasets_dir,
        "horizons": list(horizons),
        "stride": int(stride),
        "max_records": int(max_records),
        "float_epsilon": float(float_epsilon),
        "discrete_fields": list(discrete_fields),
        "float_fields": list(float_fields),
        "scoring_formula": SCORE_FORMULA,
        "row_count": len(rows),
        "cluster_count": len(clusters),
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
    ap.add_argument("--top", type=int, default=20, help="Top-N clusters to print and include in summary.")
    ap.add_argument("--out-dir", type=Path, default=None, help="Output directory under reports/triage.")
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

    out_dir = args.out_dir or (root / "reports" / "triage" / f"disruptive_rollout_desyncs_{suite.name}")
    if not out_dir.is_absolute():
        out_dir = root / out_dir

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
    all_rows: list[DisruptiveRow] = []
    for ds_path, rel in dataset_paths:
        ds = read_dataset(str(ds_path))
        players = _parse_players(args.players, num_players=int(ds.header["num_players"]))
        print(f"scan: {rel} records={int(ds.header['num_records'])}")
        all_rows.extend(
            _scan_dataset(
                suite_name=suite.name,
                dataset_path=ds_path,
                dataset_label=rel,
                horizons=horizons,
                discrete_fields=discrete_fields,
                float_fields=float_fields,
                players=players,
                max_records=int(args.max_records),
                stride=max(1, int(args.stride)),
                float_epsilon=float(args.float_epsilon),
                ucf_enabled=suite.ucf_enabled,
                ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
                action_names=action_names,
            )
        )

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
        float_epsilon=float(args.float_epsilon),
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
            f"{row.family_id} {row.field_cluster} p={row.player} "
            f"example={row.dataset}:rec={row.example_record} first={row.example_first_field}"
        )


if __name__ == "__main__":
    main()
