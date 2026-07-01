from __future__ import annotations

import importlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.one_step_report import (
    Reporter,
    emit_summary_from_native,
)
from tools.eval.validation_profile import ValidationProfile, get_validation_profile
from tools.slippi.make_dataset_from_slp import ValidationReplayBuffers


_STANDARD_ROLLOUT_FIELDS = ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "state_flags")


def _load_binding():
    return importlib.import_module("msl_binding")


@dataclass(frozen=True)
class NativeOneStepRuntime:
    binding: object
    handle: object

    def close(self) -> None:
        self.binding.destroy(self.handle)


@dataclass(frozen=True)
class ValidationStreaks:
    dataset: str
    num_records: int
    max_records_used: int
    players: tuple[int, ...]
    fields: tuple[str, ...]
    best_len: int
    best_start_record: int
    best_end_record_excl: int
    best_start_seed_frame_id: int | None
    best_end_ref_frame_id_inclusive: int | None
    streak_histogram: dict[int, int]
    first_mismatch_field_counts: dict[str, int]
    first_mismatch_field_counts_seeded: dict[str, int]
    ignored_first_mismatch_field_counts: dict[str, int]
    ignored_first_mismatch_field_counts_seeded: dict[str, int]
    profile_name: str


def create_native_one_step_runtime(
    *,
    batch_size: int,
    num_players: int,
    ucf_enabled: bool | None = None,
    ucf_cardinals_1_0_enabled: bool | None = None,
) -> NativeOneStepRuntime:
    binding = _load_binding()
    init_kwargs = {"batch_size": max(1, int(batch_size)), "num_players": int(num_players)}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    return NativeOneStepRuntime(binding=binding, handle=binding.init(**init_kwargs))


def parse_players(players_csv: str | None, *, num_players: int) -> tuple[int, ...]:
    if players_csv is None or players_csv.strip() == "":
        return tuple(range(num_players))
    out: list[int] = []
    for part in (x.strip() for x in players_csv.split(",") if x.strip() != ""):
        p = int(part)
        if p < 0 or p >= num_players:
            raise SystemExit(f"error: --players includes {p}, but replay has num_players={num_players}")
        out.append(p)
    return tuple(sorted(set(out)))


def evaluate_validation_buffers(
    *,
    dataset_path: Path,
    buffers: ValidationReplayBuffers,
    chunk: int,
    profile: str | ValidationProfile | None,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    reporter: Reporter,
    print_profile: bool = False,
):
    validation_profile = get_validation_profile(profile)
    records = int(buffers.num_records)
    num_players = int(buffers.num_players)
    runtime = create_native_one_step_runtime(
        batch_size=max(1, min(int(chunk), max(1, records))),
        num_players=num_players,
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    try:
        native_summary = runtime.binding.one_step_eval_buffers(
            runtime.handle,
            buffers.seed_u8(),
            buffers.prev_input_u8(),
            buffers.input_u8(),
            buffers.ref_u8(),
            num_players,
            int(validation_profile.name == "rl1_gameplay"),
        )
    finally:
        runtime.close()
    max_items = int(buffers.seed_t.dtype["items"].shape[0])
    return emit_summary_from_native(
        native_summary=native_summary,
        reporter=reporter,
        validation_profile=validation_profile,
        print_profile=print_profile,
        num_records=records,
        total_records=records,
        total_player_frames=records * num_players,
        total_state_flags=records * num_players * 5,
        total_item_slots=records * max_items,
        num_players=num_players,
    )


def scan_validation_buffers_with_native_float_rows(
    *,
    dataset_path: Path,
    buffers: ValidationReplayBuffers,
    fields: tuple[str, ...],
    players: tuple[int, ...],
    max_records: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    profile: str | ValidationProfile | None = None,
    float_fields: tuple[str, ...] = (),
    float_top: int = 0,
    float_threshold: float = 0.0,
    float_dataset_label: str | None = None,
    first_mismatch_probe_limit: int = 0,
) -> tuple[ValidationStreaks, dict[str, list[dict]], list[dict]]:
    validation_profile = get_validation_profile(profile)
    records = int(buffers.num_records)
    num_players = int(buffers.num_players)
    n = records if int(max_records) <= 0 else min(records, int(max_records))
    if tuple(fields) != _STANDARD_ROLLOUT_FIELDS or validation_profile.name not in ("rl1_gameplay", "strict"):
        raise ValueError("streaming validation supports the standard rollout field set only")
    if int(float_top) > 0 and not tuple(float_fields):
        raise ValueError("float_top requires float_fields")

    binding = _load_binding()
    players_u8 = np.asarray(players, dtype=np.uint8)
    native = binding.standard_rollout_scan_buffers(
        buffers.seed_u8(),
        buffers.prev_input_u8(),
        buffers.input_u8(),
        buffers.ref_u8(),
        players_u8,
        num_players,
        int(max_records),
        -1 if ucf_enabled is None else int(bool(ucf_enabled)),
        -1 if ucf_cardinals_1_0_enabled is None else int(bool(ucf_cardinals_1_0_enabled)),
        int(validation_profile.name == "rl1_gameplay"),
        tuple(float_fields),
        int(float_top),
        float(float_threshold),
        int(first_mismatch_probe_limit),
    )
    field_names = _STANDARD_ROLLOUT_FIELDS
    mismatch_counts = {
        field: int(count)
        for field, count in zip(field_names, native["first_mismatch_counts"], strict=True)
        if int(count) != 0
    }
    mismatch_counts_seeded = {
        field: int(count)
        for field, count in zip(field_names, native["first_mismatch_counts_seeded"], strict=True)
        if int(count) != 0
    }
    ignored_counts = {}
    ignored = int(native["ignored_first"])
    if ignored:
        ignored_counts["state_flags[4]&0x80"] = ignored
    ignored_counts_seeded = {}
    ignored_seeded = int(native["ignored_first_seeded"])
    if ignored_seeded:
        ignored_counts_seeded["state_flags[4]&0x80"] = ignored_seeded

    best_start = int(native["best_start_record"])
    best_end = int(native["best_end_record_excl"])
    start_seed_frame = None
    end_ref_frame_incl = None
    if int(native["best_len"]) > 0:
        try:
            start_seed_frame = int(buffers.seed_t["frame_id"][best_start])
        except Exception:
            start_seed_frame = None
        try:
            end_ref_frame_incl = int(buffers.ref_t1["frame_id"][best_end - 1])
        except Exception:
            end_ref_frame_incl = None

    float_rows = {
        str(field): [
            {"dataset": float_dataset_label if float_dataset_label is not None else str(dataset_path), **dict(row)}
            for row in rows
        ]
        for field, rows in dict(native.get("float_rows", {})).items()
    }
    first_rows = [
        {"dataset": float_dataset_label if float_dataset_label is not None else str(dataset_path), **dict(row)}
        for row in native.get("first_mismatch_rows", [])
    ]
    return (
        ValidationStreaks(
            dataset=str(dataset_path),
            num_records=records,
            max_records_used=n,
            players=players,
            fields=fields,
            best_len=int(native["best_len"]),
            best_start_record=best_start,
            best_end_record_excl=best_end,
            best_start_seed_frame_id=start_seed_frame,
            best_end_ref_frame_id_inclusive=end_ref_frame_incl,
            streak_histogram=dict(sorted((int(k), int(v)) for k, v in native["streak_histogram"].items())),
            first_mismatch_field_counts=dict(sorted(mismatch_counts.items())),
            first_mismatch_field_counts_seeded=dict(sorted(mismatch_counts_seeded.items())),
            ignored_first_mismatch_field_counts=dict(sorted(ignored_counts.items())),
            ignored_first_mismatch_field_counts_seeded=dict(sorted(ignored_counts_seeded.items())),
            profile_name=validation_profile.name,
        ),
        float_rows,
        first_rows,
    )


def default_players(buffers: ValidationReplayBuffers, players_csv: str | None = None) -> tuple[int, ...]:
    return parse_players(players_csv, num_players=int(buffers.num_players))
