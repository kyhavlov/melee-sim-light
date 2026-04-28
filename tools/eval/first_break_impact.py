from __future__ import annotations

"""Rank rollout first-breaks by short-window gameplay impact.

This tool anchors on the unseeded rows from `locate_rollout_desyncs` and scores two
follow-up windows per first-break row:

- rollout: continue from the live rollout streak state at the break row
- reseed: reseed exactly at the break row, then score the next K frames

The reseeded window is the main local-actionability lens; the rollout window is the
blast-radius lens for live play.
"""

import argparse
import csv
import importlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.locate_rollout_desyncs import _locate_dataset_rollout_desyncs, _suite_dataset_paths
from tools.eval.rollout_locate_tsv import RolloutLocateRow, parse_rollout_locate_tsv
from tools.eval.run_longest_rollout_streaks import _validate_discrete_fields
from tools.slippi.suite_io import load_suite, repo_root


def _load_binding():
    return importlib.import_module("msl_binding")


@dataclass(frozen=True)
class ImpactMetrics:
    frames_scored: int
    state_score: float
    combat_score: float
    position_score: float
    item_score: float
    total_score: float
    action_mismatch_count: int
    on_ground_mismatch_count: int
    percent_mismatch_count: int
    item_discrete_mismatch_count: int
    major_state_frames: int
    percent_abs_sum: float
    shield_abs_sum: float
    pos_l1_sum: float
    max_pos_l1: float
    item_pos_l1_sum: float


@dataclass(frozen=True)
class FirstBreakImpactRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    player: int
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    streak_start_record: int
    streak_len: int
    cluster_key: str
    frames_scored: int
    rollout_total_score: float
    rollout_state_score: float
    rollout_combat_score: float
    rollout_position_score: float
    rollout_item_score: float
    rollout_action_mismatch_count: int
    rollout_on_ground_mismatch_count: int
    rollout_percent_mismatch_count: int
    rollout_item_discrete_mismatch_count: int
    rollout_major_state_frames: int
    rollout_percent_abs_sum: float
    rollout_shield_abs_sum: float
    rollout_pos_l1_sum: float
    rollout_max_pos_l1: float
    rollout_item_pos_l1_sum: float
    reseed_total_score: float
    reseed_state_score: float
    reseed_combat_score: float
    reseed_position_score: float
    reseed_item_score: float
    reseed_action_mismatch_count: int
    reseed_on_ground_mismatch_count: int
    reseed_percent_mismatch_count: int
    reseed_item_discrete_mismatch_count: int
    reseed_major_state_frames: int
    reseed_percent_abs_sum: float
    reseed_shield_abs_sum: float
    reseed_pos_l1_sum: float
    reseed_max_pos_l1: float
    reseed_item_pos_l1_sum: float


FIRST_BREAK_IMPACT_COLUMNS: tuple[str, ...] = (
    "dataset",
    "record",
    "seed_frame",
    "ref_frame",
    "player",
    "field",
    "subindex",
    "seed",
    "out",
    "ref",
    "streak_start_record",
    "streak_len",
    "cluster_key",
    "frames_scored",
    "rollout_total_score",
    "rollout_state_score",
    "rollout_combat_score",
    "rollout_position_score",
    "rollout_item_score",
    "rollout_action_mismatch_count",
    "rollout_on_ground_mismatch_count",
    "rollout_percent_mismatch_count",
    "rollout_item_discrete_mismatch_count",
    "rollout_major_state_frames",
    "rollout_percent_abs_sum",
    "rollout_shield_abs_sum",
    "rollout_pos_l1_sum",
    "rollout_max_pos_l1",
    "rollout_item_pos_l1_sum",
    "reseed_total_score",
    "reseed_state_score",
    "reseed_combat_score",
    "reseed_position_score",
    "reseed_item_score",
    "reseed_action_mismatch_count",
    "reseed_on_ground_mismatch_count",
    "reseed_percent_mismatch_count",
    "reseed_item_discrete_mismatch_count",
    "reseed_major_state_frames",
    "reseed_percent_abs_sum",
    "reseed_shield_abs_sum",
    "reseed_pos_l1_sum",
    "reseed_max_pos_l1",
    "reseed_item_pos_l1_sum",
)


@dataclass(frozen=True)
class FirstBreakImpactClusterSummary:
    cluster_key: str
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    frequency: int
    datasets: tuple[str, ...]
    example: FirstBreakImpactRow
    rollout_total_sum: float
    rollout_total_avg: float
    reseed_total_sum: float
    reseed_total_avg: float
    reseed_major_state_frames: int
    reseed_percent_abs_sum: float
    reseed_pos_l1_sum: float
    reseed_item_discrete_mismatch_count: int


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def _score_compare_row(*, out_row: np.void, ref_row: np.void, players: tuple[int, ...]) -> ImpactMetrics:
    state_score = 0.0
    combat_score = 0.0
    position_score = 0.0
    item_score = 0.0
    action_mismatch_count = 0
    on_ground_mismatch_count = 0
    percent_mismatch_count = 0
    item_discrete_mismatch_count = 0
    major_state_frames = 0
    percent_abs_sum = 0.0
    shield_abs_sum = 0.0
    pos_l1_sum = 0.0
    max_pos_l1 = 0.0
    item_pos_l1_sum = 0.0

    major_state_this_frame = False

    for p in players:
        if int(out_row["action_id"][p]) != int(ref_row["action_id"][p]):
            state_score += 140.0
            action_mismatch_count += 1
            major_state_this_frame = True

        action_frame_delta = abs(int(out_row["action_frame"][p]) - int(ref_row["action_frame"][p]))
        if action_frame_delta:
            state_score += 6.0 * _clamp(float(action_frame_delta), 0.0, 10.0)

        if int(out_row["on_ground"][p]) != int(ref_row["on_ground"][p]):
            state_score += 90.0
            on_ground_mismatch_count += 1
            major_state_this_frame = True

        stocks_delta = abs(int(out_row["stocks"][p]) - int(ref_row["stocks"][p]))
        if stocks_delta:
            combat_score += 1000.0 * float(stocks_delta)
            major_state_this_frame = True

        percent_delta = abs(float(out_row["percent"][p]) - float(ref_row["percent"][p]))
        if percent_delta > 0.05:
            percent_abs_sum += percent_delta
            percent_mismatch_count += 1
            combat_score += 40.0 * _clamp(percent_delta, 0.0, 30.0)

        shield_delta = abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p]))
        if shield_delta > 0.05:
            shield_abs_sum += shield_delta
            combat_score += 20.0 * _clamp(shield_delta, 0.0, 15.0)

        hitlag_delta = abs(int(out_row["hitlag"][p]) - int(ref_row["hitlag"][p]))
        if hitlag_delta:
            combat_score += 25.0 * _clamp(float(hitlag_delta), 0.0, 10.0)

        hitstun_delta = abs(int(out_row["hitstun"][p]) - int(ref_row["hitstun"][p]))
        if hitstun_delta:
            combat_score += 12.0 * _clamp(float(hitstun_delta), 0.0, 20.0)

        dx = abs(float(out_row["pos_x"][p]) - float(ref_row["pos_x"][p]))
        dy = abs(float(out_row["pos_y"][p]) - float(ref_row["pos_y"][p]))
        pos_l1 = dx + dy
        if pos_l1 > 0.05:
            pos_l1_sum += pos_l1
            max_pos_l1 = max(max_pos_l1, pos_l1)
            position_score += 12.0 * _clamp(pos_l1, 0.0, 20.0)

    out_items = out_row["items"]
    ref_items = ref_row["items"]
    for idx in range(int(out_items.shape[0])):
        out_item = out_items[idx]
        ref_item = ref_items[idx]
        out_exists = int(out_item["exists"])
        ref_exists = int(ref_item["exists"])
        if out_exists != ref_exists:
            item_score += 100.0
            item_discrete_mismatch_count += 1
        active = bool(out_exists or ref_exists)
        if not active:
            continue
        for name, weight in (("state", 60.0), ("type", 60.0), ("owner", 50.0)):
            if int(out_item[name]) != int(ref_item[name]):
                item_score += weight
                item_discrete_mismatch_count += 1
        if out_exists and ref_exists:
            pos_l1 = abs(float(out_item["pos_x"]) - float(ref_item["pos_x"])) + abs(
                float(out_item["pos_y"]) - float(ref_item["pos_y"])
            )
            if pos_l1 > 0.05:
                item_pos_l1_sum += pos_l1
                item_score += 8.0 * _clamp(pos_l1, 0.0, 20.0)

    if major_state_this_frame:
        major_state_frames = 1

    total_score = state_score + combat_score + position_score + item_score
    return ImpactMetrics(
        frames_scored=1,
        state_score=state_score,
        combat_score=combat_score,
        position_score=position_score,
        item_score=item_score,
        total_score=total_score,
        action_mismatch_count=action_mismatch_count,
        on_ground_mismatch_count=on_ground_mismatch_count,
        percent_mismatch_count=percent_mismatch_count,
        item_discrete_mismatch_count=item_discrete_mismatch_count,
        major_state_frames=major_state_frames,
        percent_abs_sum=percent_abs_sum,
        shield_abs_sum=shield_abs_sum,
        pos_l1_sum=pos_l1_sum,
        max_pos_l1=max_pos_l1,
        item_pos_l1_sum=item_pos_l1_sum,
    )


def _sum_metrics(metrics: Iterable[ImpactMetrics]) -> ImpactMetrics:
    rows = list(metrics)
    if not rows:
        return ImpactMetrics(
            frames_scored=0,
            state_score=0.0,
            combat_score=0.0,
            position_score=0.0,
            item_score=0.0,
            total_score=0.0,
            action_mismatch_count=0,
            on_ground_mismatch_count=0,
            percent_mismatch_count=0,
            item_discrete_mismatch_count=0,
            major_state_frames=0,
            percent_abs_sum=0.0,
            shield_abs_sum=0.0,
            pos_l1_sum=0.0,
            max_pos_l1=0.0,
            item_pos_l1_sum=0.0,
        )
    return ImpactMetrics(
        frames_scored=sum(row.frames_scored for row in rows),
        state_score=sum(row.state_score for row in rows),
        combat_score=sum(row.combat_score for row in rows),
        position_score=sum(row.position_score for row in rows),
        item_score=sum(row.item_score for row in rows),
        total_score=sum(row.total_score for row in rows),
        action_mismatch_count=sum(row.action_mismatch_count for row in rows),
        on_ground_mismatch_count=sum(row.on_ground_mismatch_count for row in rows),
        percent_mismatch_count=sum(row.percent_mismatch_count for row in rows),
        item_discrete_mismatch_count=sum(row.item_discrete_mismatch_count for row in rows),
        major_state_frames=sum(row.major_state_frames for row in rows),
        percent_abs_sum=sum(row.percent_abs_sum for row in rows),
        shield_abs_sum=sum(row.shield_abs_sum for row in rows),
        pos_l1_sum=sum(row.pos_l1_sum for row in rows),
        max_pos_l1=max(row.max_pos_l1 for row in rows),
        item_pos_l1_sum=sum(row.item_pos_l1_sum for row in rows),
    )


def impact_row_to_tsv_values(row: FirstBreakImpactRow) -> tuple[object, ...]:
    return tuple(getattr(row, name) for name in FIRST_BREAK_IMPACT_COLUMNS)


def write_first_break_impact_tsv(path: Path, rows: Iterable[FirstBreakImpactRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t", lineterminator="\n")
        writer.writerow(FIRST_BREAK_IMPACT_COLUMNS)
        for row in rows:
            writer.writerow(impact_row_to_tsv_values(row))


def parse_first_break_impact_tsv(path: Path) -> list[FirstBreakImpactRow]:
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        if reader.fieldnames is None:
            raise ValueError(f"{path}: missing TSV header")
        missing = [name for name in FIRST_BREAK_IMPACT_COLUMNS if name not in reader.fieldnames]
        if missing:
            raise ValueError(f"{path}: missing columns: {', '.join(missing)}")
        rows: list[FirstBreakImpactRow] = []
        for raw in reader:
            kwargs: dict[str, Any] = {}
            for name in FIRST_BREAK_IMPACT_COLUMNS:
                value = raw[name]
                if name in {"dataset", "field", "cluster_key"}:
                    kwargs[name] = str(value)
                elif name.endswith("_score") or name.endswith("_sum") or name.endswith("_max_pos_l1"):
                    kwargs[name] = float(value)
                else:
                    kwargs[name] = int(value)
            rows.append(FirstBreakImpactRow(**kwargs))
    return rows


def summarize_impact_clusters(rows: Iterable[FirstBreakImpactRow]) -> list[FirstBreakImpactClusterSummary]:
    by_key: dict[str, list[FirstBreakImpactRow]] = {}
    for row in rows:
        by_key.setdefault(row.cluster_key, []).append(row)

    summaries: list[FirstBreakImpactClusterSummary] = []
    for key, group in by_key.items():
        example = group[0]
        datasets = tuple(sorted({row.dataset for row in group}))
        freq = len(group)
        reseed_total_sum = sum(row.reseed_total_score for row in group)
        rollout_total_sum = sum(row.rollout_total_score for row in group)
        summaries.append(
            FirstBreakImpactClusterSummary(
                cluster_key=key,
                field=example.field,
                subindex=int(example.subindex),
                seed=int(example.seed),
                out=int(example.out),
                ref=int(example.ref),
                frequency=freq,
                datasets=datasets,
                example=example,
                rollout_total_sum=rollout_total_sum,
                rollout_total_avg=rollout_total_sum / float(freq),
                reseed_total_sum=reseed_total_sum,
                reseed_total_avg=reseed_total_sum / float(freq),
                reseed_major_state_frames=sum(row.reseed_major_state_frames for row in group),
                reseed_percent_abs_sum=sum(row.reseed_percent_abs_sum for row in group),
                reseed_pos_l1_sum=sum(row.reseed_pos_l1_sum for row in group),
                reseed_item_discrete_mismatch_count=sum(
                    row.reseed_item_discrete_mismatch_count for row in group
                ),
            )
        )

    return sorted(
        summaries,
        key=lambda row: (
            -row.reseed_total_sum,
            -row.frequency,
            -row.rollout_total_sum,
            row.cluster_key,
        ),
    )


def _metrics_to_kwargs(prefix: str, metrics: ImpactMetrics) -> dict[str, object]:
    return {
        f"{prefix}_total_score": metrics.total_score,
        f"{prefix}_state_score": metrics.state_score,
        f"{prefix}_combat_score": metrics.combat_score,
        f"{prefix}_position_score": metrics.position_score,
        f"{prefix}_item_score": metrics.item_score,
        f"{prefix}_action_mismatch_count": metrics.action_mismatch_count,
        f"{prefix}_on_ground_mismatch_count": metrics.on_ground_mismatch_count,
        f"{prefix}_percent_mismatch_count": metrics.percent_mismatch_count,
        f"{prefix}_item_discrete_mismatch_count": metrics.item_discrete_mismatch_count,
        f"{prefix}_major_state_frames": metrics.major_state_frames,
        f"{prefix}_percent_abs_sum": metrics.percent_abs_sum,
        f"{prefix}_shield_abs_sum": metrics.shield_abs_sum,
        f"{prefix}_pos_l1_sum": metrics.pos_l1_sum,
        f"{prefix}_max_pos_l1": metrics.max_pos_l1,
        f"{prefix}_item_pos_l1_sum": metrics.item_pos_l1_sum,
    }


def _impact_row_from_locate(
    *,
    locate_row: RolloutLocateRow,
    rollout_metrics: ImpactMetrics,
    reseed_metrics: ImpactMetrics,
) -> FirstBreakImpactRow:
    kwargs: dict[str, object] = {
        "dataset": locate_row.dataset,
        "record": int(locate_row.record),
        "seed_frame": int(locate_row.seed_frame),
        "ref_frame": int(locate_row.ref_frame),
        "player": int(locate_row.player),
        "field": locate_row.field,
        "subindex": int(locate_row.subindex),
        "seed": int(locate_row.seed),
        "out": int(locate_row.out),
        "ref": int(locate_row.ref),
        "streak_start_record": int(locate_row.streak_start_record),
        "streak_len": int(locate_row.streak_len),
        "cluster_key": locate_row.cluster_key,
        "frames_scored": int(min(rollout_metrics.frames_scored, reseed_metrics.frames_scored)),
    }
    kwargs.update(_metrics_to_kwargs("rollout", rollout_metrics))
    kwargs.update(_metrics_to_kwargs("reseed", reseed_metrics))
    return FirstBreakImpactRow(**kwargs)


def _score_window(
    *,
    handle,
    binding,
    samples_u8: np.ndarray,
    samples: np.ndarray,
    seed_off: int,
    prev_input_off: int,
    input_off: int,
    seed_stride: int,
    input_stride: int,
    compare_stride: int,
    out_compare_bytes: np.ndarray,
    players: tuple[int, ...],
    start_record: int,
    start_seed_record: int,
    horizon: int,
) -> ImpactMetrics:
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    seed_bytes[0, :] = samples_u8[start_seed_record, seed_off : seed_off + seed_stride]
    binding.reseed_seed_rollout(handle, seed_bytes)

    for j in range(start_seed_record, start_record):
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input(handle, prev_input_bytes, input_bytes)

    metrics: list[ImpactMetrics] = []
    num_records = int(samples.shape[0])
    end_record = min(num_records, start_record + max(0, int(horizon)))
    for j in range(start_record, end_record):
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        metrics.append(_score_compare_row(out_row=out_view[0], ref_row=samples["ref_t1"][j], players=players))

    return _sum_metrics(metrics)


def _load_or_build_locate_rows(
    *,
    suite_path: Path,
    datasets_dir: str,
    fields: tuple[str, ...],
    dataset_filter: str,
    max_records: int,
    limit: int | None,
    locate_tsv_in: Path | None,
    locate_tsv_out: Path | None,
) -> list[RolloutLocateRow]:
    if locate_tsv_in is not None:
        return [row for row in parse_rollout_locate_tsv(locate_tsv_in) if not row.seeded_break]

    root = repo_root()
    suite = load_suite(suite_path)
    replay_rel_paths = [entry.replay for entry in suite.replays]
    dataset_paths = _suite_dataset_paths(
        suite_name=suite.name,
        replay_rel_paths=replay_rel_paths,
        datasets_dir=str(datasets_dir),
    )

    filtered: list[tuple[Path, str]] = []
    for ds_path in dataset_paths:
        rel = str(ds_path.resolve().relative_to(root))
        if dataset_filter and dataset_filter not in rel:
            continue
        if not ds_path.exists():
            raise SystemExit(f"missing dataset: {rel}")
        filtered.append((ds_path, rel))
    if not filtered:
        raise SystemExit("error: no datasets selected")

    rows: list[RolloutLocateRow] = []
    row_limit = None if limit is None or limit <= 0 else int(limit)
    for ds_path, rel in filtered:
        if row_limit is not None and len(rows) >= row_limit:
            break
        ds = read_dataset(str(ds_path))
        num_players = int(ds.header["num_players"])
        players = tuple(range(num_players))
        remaining = None if row_limit is None else max(0, row_limit - len(rows))
        rows.extend(
            _locate_dataset_rollout_desyncs(
                dataset_path=ds_path,
                dataset_label=rel,
                ds=ds,
                fields=fields,
                players=players,
                max_records=int(max_records),
                row_limit=remaining,
                ucf_enabled=suite.ucf_enabled,
                ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            )
        )

    raw_rows = [row for row in rows if not row.seeded_break]
    if locate_tsv_out is not None:
        locate_tsv_out.parent.mkdir(parents=True, exist_ok=True)
        from tools.eval.rollout_locate_tsv import write_rollout_locate_tsv

        write_rollout_locate_tsv(locate_tsv_out, rows)
    return raw_rows


def _score_dataset_rows(
    *,
    dataset_path: Path,
    rows: list[RolloutLocateRow],
    horizon: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> list[FirstBreakImpactRow]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records_total = int(samples.shape[0])
    num_players = int(ds.header["num_players"])
    players = tuple(range(num_players))

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": 1, "num_players": num_players}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    handle = binding.init(**init_kwargs)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records_total, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    out_rows: list[FirstBreakImpactRow] = []
    try:
        for row in rows:
            rollout_metrics = _score_window(
                handle=handle,
                binding=binding,
                samples_u8=samples_u8,
                samples=samples,
                seed_off=seed_off,
                prev_input_off=prev_input_off,
                input_off=input_off,
                seed_stride=seed_stride,
                input_stride=input_stride,
                compare_stride=compare_stride,
                out_compare_bytes=out_compare_bytes,
                players=players,
                start_record=int(row.record),
                start_seed_record=int(row.streak_start_record),
                horizon=horizon,
            )
            reseed_metrics = _score_window(
                handle=handle,
                binding=binding,
                samples_u8=samples_u8,
                samples=samples,
                seed_off=seed_off,
                prev_input_off=prev_input_off,
                input_off=input_off,
                seed_stride=seed_stride,
                input_stride=input_stride,
                compare_stride=compare_stride,
                out_compare_bytes=out_compare_bytes,
                players=players,
                start_record=int(row.record),
                start_seed_record=int(row.record),
                horizon=horizon,
            )
            out_rows.append(
                _impact_row_from_locate(
                    locate_row=row,
                    rollout_metrics=rollout_metrics,
                    reseed_metrics=reseed_metrics,
                )
            )
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass
    return out_rows


def _cluster_to_json(row: FirstBreakImpactClusterSummary) -> dict[str, Any]:
    example = row.example
    return {
        "cluster_key": row.cluster_key,
        "field": row.field,
        "subindex": int(row.subindex),
        "seed": int(row.seed),
        "out": int(row.out),
        "ref": int(row.ref),
        "frequency": int(row.frequency),
        "datasets": list(row.datasets),
        "rollout_total_sum": row.rollout_total_sum,
        "rollout_total_avg": row.rollout_total_avg,
        "reseed_total_sum": row.reseed_total_sum,
        "reseed_total_avg": row.reseed_total_avg,
        "reseed_major_state_frames": int(row.reseed_major_state_frames),
        "reseed_percent_abs_sum": row.reseed_percent_abs_sum,
        "reseed_pos_l1_sum": row.reseed_pos_l1_sum,
        "reseed_item_discrete_mismatch_count": int(row.reseed_item_discrete_mismatch_count),
        "example": {
            "dataset": example.dataset,
            "record": int(example.record),
            "seed_frame": int(example.seed_frame),
            "ref_frame": int(example.ref_frame),
            "player": int(example.player),
            "field": example.field,
            "subindex": int(example.subindex),
            "seed": int(example.seed),
            "out": int(example.out),
            "ref": int(example.ref),
            "streak_len": int(example.streak_len),
            "rollout_total_score": example.rollout_total_score,
            "reseed_total_score": example.reseed_total_score,
        },
    }


def build_summary(rows: list[FirstBreakImpactRow], *, top: int, horizon: int) -> dict[str, Any]:
    clusters = summarize_impact_clusters(rows)
    by_dataset: dict[str, dict[str, float]] = {}
    for row in rows:
        metrics = by_dataset.setdefault(
            row.dataset,
            {"rows": 0.0, "rollout_total_sum": 0.0, "reseed_total_sum": 0.0, "major_state_frames": 0.0},
        )
        metrics["rows"] += 1
        metrics["rollout_total_sum"] += row.rollout_total_score
        metrics["reseed_total_sum"] += row.reseed_total_score
        metrics["major_state_frames"] += row.reseed_major_state_frames

    return {
        "row_count": len(rows),
        "cluster_count": len(clusters),
        "horizon": int(horizon),
        "top": int(top),
        "top_clusters": [_cluster_to_json(row) for row in clusters[: max(1, int(top))]],
        "per_dataset": [
            {
                "dataset": dataset,
                "rows": int(values["rows"]),
                "rollout_total_sum": values["rollout_total_sum"],
                "reseed_total_sum": values["reseed_total_sum"],
                "major_state_frames": int(values["major_state_frames"]),
            }
            for dataset, values in sorted(
                by_dataset.items(),
                key=lambda item: (-item[1]["reseed_total_sum"], item[0]),
            )
        ],
    }


def print_summary(summary: dict[str, Any]) -> None:
    print(
        f"first_break_impact: rows={summary['row_count']} "
        f"clusters={summary['cluster_count']} horizon={summary['horizon']}"
    )
    print("top_clusters:")
    if not summary["top_clusters"]:
        print("  (none)")
    for i, row in enumerate(summary["top_clusters"], start=1):
        ex = row["example"]
        ds_preview = ",".join(Path(ds).name for ds in row["datasets"][:3])
        if len(row["datasets"]) > 3:
            ds_preview += f",+{len(row['datasets']) - 3}"
        print(
            f"{i:>2}. reseed_sum={row['reseed_total_sum']:<10.1f} freq={row['frequency']:<4d} "
            f"raw_sum={row['rollout_total_sum']:<10.1f} {row['field']}[{row['subindex']}] "
            f"seed/out/ref={row['seed']}/{row['out']}/{row['ref']} "
            f"reseed_pos={row['reseed_pos_l1_sum']:.2f} reseed_pct={row['reseed_percent_abs_sum']:.2f} "
            f"example={Path(ex['dataset']).name}:rec={ex['record']}:p={ex['player']} datasets={ds_preview}"
        )


def main() -> None:
    ap = argparse.ArgumentParser(description="Rank rollout first-breaks by short-window gameplay impact.")
    ap.add_argument("--suite", required=True, help="Suite JSON path under repo root.")
    ap.add_argument("--datasets-dir", default="datasets", help="Datasets directory under repo root.")
    ap.add_argument(
        "--fields",
        default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags",
        help="Discrete rollout first-break fields (same meaning as locate_rollout_desyncs).",
    )
    ap.add_argument("--dataset-filter", default="", help="Optional substring filter on dataset relative path.")
    ap.add_argument("--max-records", type=int, default=0, help="Optional per-dataset record cap.")
    ap.add_argument("--limit", type=int, default=0, help="Optional first-break row cap.")
    ap.add_argument("--horizon", type=int, default=20, help="Frames to score from each first-break row.")
    ap.add_argument("--top", type=int, default=12, help="Top-N cluster summaries to print.")
    ap.add_argument("--locate-tsv-in", type=Path, default=None, help="Optional precomputed rollout locate TSV.")
    ap.add_argument("--locate-tsv-out", type=Path, default=None, help="Optional rollout locate TSV output path.")
    ap.add_argument("--out", type=Path, required=True, help="Output TSV path for row-level impact scores.")
    ap.add_argument("--json-out", type=Path, default=None, help="Optional JSON summary output path.")
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    fields = _validate_discrete_fields(tuple(s.strip() for s in str(args.fields).split(",") if s.strip()))

    locate_rows = _load_or_build_locate_rows(
        suite_path=suite_path,
        datasets_dir=str(args.datasets_dir),
        fields=fields,
        dataset_filter=str(args.dataset_filter),
        max_records=int(args.max_records),
        limit=None if int(args.limit) <= 0 else int(args.limit),
        locate_tsv_in=args.locate_tsv_in,
        locate_tsv_out=args.locate_tsv_out,
    )

    by_dataset: dict[str, list[RolloutLocateRow]] = {}
    for row in locate_rows:
        by_dataset.setdefault(row.dataset, []).append(row)

    all_rows: list[FirstBreakImpactRow] = []
    for dataset_rel, rows in by_dataset.items():
        ds_path = (root / dataset_rel).resolve()
        all_rows.extend(
            _score_dataset_rows(
                dataset_path=ds_path,
                rows=rows,
                horizon=int(args.horizon),
                ucf_enabled=suite.ucf_enabled,
                ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            )
        )

    write_first_break_impact_tsv(args.out, all_rows)
    print(f"wrote: {args.out}")
    summary = build_summary(all_rows, top=max(1, int(args.top)), horizon=int(args.horizon))
    print_summary(summary)
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote: {args.json_out}")


if __name__ == "__main__":
    main()
