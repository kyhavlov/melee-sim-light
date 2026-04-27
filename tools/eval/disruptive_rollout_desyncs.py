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
class BatchScoreBreakdown:
    total: np.ndarray
    discrete: np.ndarray
    floats: np.ndarray
    items: np.ndarray
    player_scores: np.ndarray
    item_score: np.ndarray


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


def _compare_first_mismatches_batch(
    *,
    out_rows: np.ndarray,
    ref_rows: np.ndarray,
    first_mismatches: list[FirstMismatch | None],
    players: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    offset: int,
    float_epsilon: float,
) -> None:
    unresolved = np.fromiter((m is None for m in first_mismatches), dtype=bool, count=len(first_mismatches))
    if not bool(unresolved.any()):
        return

    def assign_int(mask: np.ndarray, *, player: int, field: str, subindex: int, out_values, ref_values) -> bool:
        lanes = np.flatnonzero(mask & unresolved)
        for lane in lanes:
            first_mismatches[int(lane)] = FirstMismatch(
                offset,
                player,
                field,
                subindex,
                _int_text(out_values[int(lane)]),
                _int_text(ref_values[int(lane)]),
            )
        unresolved[lanes] = False
        return not bool(unresolved.any())

    def assign_float(mask: np.ndarray, *, player: int, field: str, subindex: int, out_values, ref_values) -> bool:
        lanes = np.flatnonzero(mask & unresolved)
        for lane in lanes:
            first_mismatches[int(lane)] = FirstMismatch(
                offset,
                player,
                field,
                subindex,
                _float_text(out_values[int(lane)]),
                _float_text(ref_values[int(lane)]),
            )
        unresolved[lanes] = False
        return not bool(unresolved.any())

    for field in discrete_fields:
        field_shape = COMPARE_DTYPE.fields[field][0].shape
        out = out_rows[field]
        ref = ref_rows[field]
        if field_shape == ():
            if assign_int(out != ref, player=-1, field=field, subindex=-1, out_values=out, ref_values=ref):
                return
            continue
        if len(field_shape) == 1:
            for p in players:
                out_values = out[:, p]
                ref_values = ref[:, p]
                if assign_int(
                    out_values != ref_values,
                    player=int(p),
                    field=field,
                    subindex=-1,
                    out_values=out_values,
                    ref_values=ref_values,
                ):
                    return
            continue
        for p in players:
            for sub in range(int(np.prod(field_shape[1:]))):
                out_values = out[:, p].reshape(out.shape[0], -1)[:, sub]
                ref_values = ref[:, p].reshape(ref.shape[0], -1)[:, sub]
                if assign_int(
                    out_values != ref_values,
                    player=int(p),
                    field=f"{field}[{sub}]",
                    subindex=int(sub),
                    out_values=out_values,
                    ref_values=ref_values,
                ):
                    return

    for field in float_fields:
        out = out_rows[field]
        ref = ref_rows[field]
        for p in players:
            out_values = out[:, p]
            ref_values = ref[:, p]
            if assign_float(
                np.abs(out_values - ref_values) > float_epsilon,
                player=int(p),
                field=field,
                subindex=-1,
                out_values=out_values,
                ref_values=ref_values,
            ):
                return

    out_items = out_rows["items"]
    ref_items = ref_rows["items"]
    for slot in range(MAX_ITEMS):
        out_item = out_items[:, slot]
        ref_item = ref_items[:, slot]
        for subfield in ("exists", "state", "type", "owner", "instance_id"):
            out_values = out_item[subfield]
            ref_values = ref_item[subfield]
            if assign_int(
                out_values != ref_values,
                player=-1,
                field=f"item_{subfield}",
                subindex=slot,
                out_values=out_values,
                ref_values=ref_values,
            ):
                return
        exists = (out_item["exists"] != 0) | (ref_item["exists"] != 0)
        for subfield in ("pos_x", "pos_y", "vel_x", "vel_y"):
            out_values = out_item[subfield]
            ref_values = ref_item[subfield]
            if assign_float(
                exists & (np.abs(out_values - ref_values) > float_epsilon),
                player=-1,
                field=f"item_{subfield}",
                subindex=slot,
                out_values=out_values,
                ref_values=ref_values,
            ):
                return


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


def _discrete_field_score_array(field: str, out: np.ndarray, ref: np.ndarray) -> np.ndarray:
    if field == "action_frame":
        return 6.0 * np.clip(np.abs(out.astype(np.float64) - ref.astype(np.float64)), 0.0, 10.0)
    if field == "hitlag":
        return 25.0 * np.clip(np.abs(out.astype(np.float64) - ref.astype(np.float64)), 0.0, 10.0)
    if field == "hitstun":
        return 12.0 * np.clip(np.abs(out.astype(np.float64) - ref.astype(np.float64)), 0.0, 20.0)
    if field == "stocks":
        return 1000.0 * np.abs(out.astype(np.float64) - ref.astype(np.float64))
    weight = float(SCORE_FORMULA["discrete"].get(field, 10.0))
    return (out != ref).astype(np.float64) * weight


def _score_horizon_rows_batch(
    *,
    out_rows: np.ndarray,
    ref_rows: np.ndarray,
    players: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    float_epsilon: float,
) -> BatchScoreBreakdown:
    n = int(out_rows.shape[0])
    player_scores = np.zeros((n, max(players) + 1 if players else 0), dtype=np.float64)
    discrete_score = np.zeros(n, dtype=np.float64)
    float_score = np.zeros(n, dtype=np.float64)
    item_score = np.zeros(n, dtype=np.float64)

    for field in discrete_fields:
        field_shape = COMPARE_DTYPE.fields[field][0].shape
        out = out_rows[field]
        ref = ref_rows[field]
        if field_shape == ():
            discrete_score += _discrete_field_score_array(field, out, ref)
            continue
        if len(field_shape) == 1:
            for p in players:
                score = _discrete_field_score_array(field, out[:, p], ref[:, p])
                player_scores[:, p] += score
                discrete_score += score
            continue
        for p in players:
            diff_count = np.count_nonzero(
                out[:, p].reshape(n, -1) != ref[:, p].reshape(n, -1),
                axis=1,
            ).astype(np.float64)
            score = diff_count * 15.0
            player_scores[:, p] += score
            discrete_score += score

    for field in float_fields:
        out = out_rows[field]
        ref = ref_rows[field]
        weight, cap = _float_weight(field)
        for p in players:
            delta = np.abs(out[:, p].astype(np.float64) - ref[:, p].astype(np.float64))
            score = np.where(delta > float_epsilon, weight * np.clip(delta, 0.0, cap), 0.0)
            player_scores[:, p] += score
            float_score += score

    out_items = out_rows["items"]
    ref_items = ref_rows["items"]
    for slot in range(MAX_ITEMS):
        out_item = out_items[:, slot]
        ref_item = ref_items[:, slot]
        item_score += (out_item["exists"] != ref_item["exists"]).astype(np.float64) * 100.0
        for subfield, weight in (("state", 60.0), ("type", 60.0), ("owner", 50.0)):
            item_score += (out_item[subfield] != ref_item[subfield]).astype(np.float64) * weight
        exists = (out_item["exists"] != 0) | (ref_item["exists"] != 0)
        for subfield in ("pos_x", "pos_y"):
            delta = np.abs(out_item[subfield].astype(np.float64) - ref_item[subfield].astype(np.float64))
            item_score += np.where(exists & (delta > float_epsilon), 8.0 * np.clip(delta, 0.0, 20.0), 0.0)

    total = discrete_score + float_score + item_score
    return BatchScoreBreakdown(
        total=total,
        discrete=discrete_score,
        floats=float_score,
        items=item_score,
        player_scores=player_scores,
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
        missing = [column for column in ROW_COLUMNS if column not in reader.fieldnames]
        if missing:
            raise SystemExit(f"error: {path} is missing columns: {', '.join(missing)}")
        rows: list[DisruptiveRow] = []
        for raw in reader:
            values: dict[str, Any] = {}
            for column in ROW_COLUMNS:
                value = raw[column]
                if column in _DISRUPTIVE_INT_COLUMNS:
                    values[column] = int(value)
                elif column in _DISRUPTIVE_FLOAT_COLUMNS:
                    values[column] = float(value)
                else:
                    values[column] = value
            rows.append(DisruptiveRow(**values))
    return rows


def _row_for_scored_horizon(
    *,
    suite_name: str,
    dataset_label: str,
    record: int,
    horizon: int,
    seed_row: np.void,
    out_row: np.void,
    ref_row: np.void,
    score: ScoreBreakdown,
    first_mismatch: FirstMismatch | None,
    players: tuple[int, ...],
    discrete_fields: tuple[str, ...],
    float_fields: tuple[str, ...],
    float_epsilon: float,
    action_names: dict[int, str],
) -> DisruptiveRow:
    player = max(players, key=lambda p: (score.player_scores[p], -p))
    if score.player_scores[player] <= 0.0 and score.item_score > 0.0:
        player = -1
    family_fields = (
        (first_mismatch.field,) if player < 0 else _mismatched_player_fields(
            out_row=out_row,
            ref_row=ref_row,
            player=player,
            discrete_fields=discrete_fields,
            float_fields=float_fields,
            float_epsilon=float_epsilon,
        )
    )
    if not family_fields and first_mismatch is not None:
        family_fields = (first_mismatch.field,)
    first = first_mismatch or FirstMismatch(horizon, player, "none", -1, "", "")
    field_cluster = _field_cluster(first.field if first.field != "none" else family_fields[0])
    family_id = (
        "F00_rollout_items"
        if player < 0
        else _taxonomy_family(
            dataset=dataset_label,
            record=record,
            seed_row=seed_row,
            out_row=out_row,
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
            seed_row=seed_row,
            out_row=out_row,
            ref_row=ref_row,
            player=player,
            action_names=action_names,
        )
    )
    cluster_key = _cluster_key(
        horizon=horizon,
        family_id=family_id,
        action_state=action_state,
        dataset=dataset_label,
        player=player,
        field_cluster=field_cluster,
    )
    action_player = 0 if player < 0 else player
    return DisruptiveRow(
        suite=suite_name,
        dataset=dataset_label,
        record=record,
        seed_frame=int(seed_row["frame_id"]),
        horizon=horizon,
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
        out_action_id=int(out_row["action_id"][action_player]),
        ref_action_id=int(ref_row["action_id"][action_player]),
        seed_action_frame=int(seed_row["action_frame"][action_player]),
        out_action_frame=int(out_row["action_frame"][action_player]),
        ref_action_frame=int(ref_row["action_frame"][action_player]),
        on_ground=int(ref_row["on_ground"][action_player]),
        hitlag=int(ref_row["hitlag"][action_player]),
        hitstun=int(ref_row["hitstun"][action_player]),
        cluster_key=cluster_key,
    )


def _scan_dataset_scalar(
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
    start_record: int = 0,
    stop_record: int = 0,
) -> list[DisruptiveRow]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    total_records = int(samples.shape[0])
    max_horizon = max(horizons)
    usable_records = max(0, total_records - max_horizon + 1)
    if max_records > 0:
        usable_records = min(usable_records, int(max_records))
    record_start = max(0, int(start_record))
    record_stop = usable_records if stop_record <= 0 else min(usable_records, int(stop_record))

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
        for start in range(record_start, record_stop, max(1, int(stride))):
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

                seed_row = samples["seed_t"][start]
                rows.append(
                    _row_for_scored_horizon(
                        suite_name=suite_name,
                        dataset_label=dataset_label,
                        record=start,
                        horizon=offset,
                        seed_row=seed_row,
                        out_row=out_view[0],
                        ref_row=ref_row,
                        score=score,
                        first_mismatch=first_mismatch,
                        players=players,
                        discrete_fields=discrete_fields,
                        float_fields=float_fields,
                        float_epsilon=float_epsilon,
                        action_names=action_names,
                    )
                )
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass
    return rows


def _fill_inactive_lanes(arr: np.ndarray, active_count: int) -> None:
    if 0 < active_count < int(arr.shape[0]):
        arr[active_count:, :] = arr[active_count - 1, :]


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
    start_record: int = 0,
    stop_record: int = 0,
) -> list[DisruptiveRow]:
    if batch_size <= 1:
        return _scan_dataset_scalar(
            suite_name=suite_name,
            dataset_path=dataset_path,
            dataset_label=dataset_label,
            horizons=horizons,
            discrete_fields=discrete_fields,
            float_fields=float_fields,
            players=players,
            max_records=max_records,
            stride=stride,
            float_epsilon=float_epsilon,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
            action_names=action_names,
            start_record=start_record,
            stop_record=stop_record,
        )

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    total_records = int(samples.shape[0])
    max_horizon = max(horizons)
    usable_records = max(0, total_records - max_horizon + 1)
    if max_records > 0:
        usable_records = min(usable_records, int(max_records))
    record_start = max(0, int(start_record))
    record_stop = usable_records if stop_record <= 0 else min(usable_records, int(stop_record))

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    batch_size = max(1, int(batch_size))

    init_kwargs = {"batch_size": batch_size, "num_players": int(ds.header["num_players"])}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    handle = binding.init(**init_kwargs)

    seed_bytes = np.empty((batch_size, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((batch_size, input_stride), dtype=np.uint8)
    input_bytes = np.empty((batch_size, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((batch_size, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(batch_size)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(total_records, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    starts_all = np.arange(record_start, record_stop, max(1, int(stride)), dtype=np.int64)
    horizon_set = set(horizons)
    rows: list[DisruptiveRow] = []
    try:
        for chunk_lo in range(0, int(starts_all.shape[0]), batch_size):
            starts = starts_all[chunk_lo : chunk_lo + batch_size]
            active_count = int(starts.shape[0])
            if active_count <= 0:
                continue

            seed_bytes[:active_count, :] = samples_u8[starts, seed_off : seed_off + seed_stride]
            _fill_inactive_lanes(seed_bytes, active_count)
            binding.reseed_seed(handle, seed_bytes)

            first_mismatches: list[FirstMismatch | None] = [None for _ in range(active_count)]
            chunk_rows: list[list[DisruptiveRow]] = [[] for _ in range(active_count)]
            for offset in range(1, max_horizon + 1):
                ref_indices = starts + offset - 1
                prev_input_bytes[:active_count, :] = samples_u8[
                    ref_indices, prev_input_off : prev_input_off + input_stride
                ]
                input_bytes[:active_count, :] = samples_u8[ref_indices, input_off : input_off + input_stride]
                _fill_inactive_lanes(prev_input_bytes, active_count)
                _fill_inactive_lanes(input_bytes, active_count)

                binding.step_input(handle, prev_input_bytes, input_bytes)
                binding.write_compare(handle, out_compare_bytes)
                ref_rows = samples["ref_t1"][ref_indices]

                _compare_first_mismatches_batch(
                    out_rows=out_view[:active_count],
                    ref_rows=ref_rows,
                    first_mismatches=first_mismatches,
                    players=players,
                    discrete_fields=discrete_fields,
                    float_fields=float_fields,
                    offset=offset,
                    float_epsilon=float_epsilon,
                )

                if offset not in horizon_set:
                    continue

                batch_score = _score_horizon_rows_batch(
                    out_rows=out_view[:active_count],
                    ref_rows=ref_rows,
                    players=players,
                    discrete_fields=discrete_fields,
                    float_fields=float_fields,
                    float_epsilon=float_epsilon,
                )
                for lane in np.flatnonzero(batch_score.total > 0.0):
                    lane_i = int(lane)
                    score = ScoreBreakdown(
                        total=float(batch_score.total[lane_i]),
                        discrete=float(batch_score.discrete[lane_i]),
                        floats=float(batch_score.floats[lane_i]),
                        items=float(batch_score.items[lane_i]),
                        player_scores=tuple(float(v) for v in batch_score.player_scores[lane_i]),
                        item_score=float(batch_score.item_score[lane_i]),
                    )
                    start = int(starts[lane_i])
                    chunk_rows[lane_i].append(
                        _row_for_scored_horizon(
                            suite_name=suite_name,
                            dataset_label=dataset_label,
                            record=start,
                            horizon=offset,
                            seed_row=samples["seed_t"][start],
                            out_row=out_view[lane_i],
                            ref_row=ref_rows[lane_i],
                            score=score,
                            first_mismatch=first_mismatches[lane_i],
                            players=players,
                            discrete_fields=discrete_fields,
                            float_fields=float_fields,
                            float_epsilon=float_epsilon,
                            action_names=action_names,
                        )
                    )

            for lane_rows in chunk_rows:
                rows.extend(lane_rows)
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass
    return rows


def _scan_dataset_task(task: dict[str, Any]) -> list[DisruptiveRow]:
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
) -> dict[str, Any]:
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
    ap.add_argument("--batch-size", type=int, default=512, help="Rollout start rows to scan per sim batch.")
    ap.add_argument("--workers", type=int, default=1, help="Parallel dataset/chunk worker processes.")
    ap.add_argument("--chunk-records", type=int, default=2048, help="Start rows per worker task.")
    ap.add_argument("--top", type=int, default=20, help="Top-N clusters to print and include in summary.")
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
        task_count = max(1, (int(starts.shape[0]) + chunk_records - 1) // chunk_records)
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
