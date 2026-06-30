from dataclasses import asdict
from pathlib import Path

import numpy as np
import pytest

import msl_binding
from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.run_longest_rollout_streaks import (
    _AttemptResult,
    DatasetStreaks,
    _STANDARD_ROLLOUT_FIELDS,
    _scan_dataset_streaks,
    _scan_rollout_streaks,
)
from tools.slippi.suite_io import load_suite, repo_root
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


def _suite_entry(*, suite_rel: str, replay_name: str):
    root = repo_root()
    suite = load_suite(root / suite_rel)
    for entry in suite.replays:
        if Path(entry.replay).name == replay_name:
            return suite, entry
    raise AssertionError(f"{replay_name} not found in {suite_rel}")


def _build_suite_dataset(*, suite_rel: str, replay_name: str):
    root = repo_root()
    suite, entry = _suite_entry(suite_rel=suite_rel, replay_name=replay_name)
    replay_path = root / entry.replay
    ds = build_dataset_from_slp(
        slp_path=str(replay_path),
        ports=[int(p) for p in entry.ports],
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
    )
    return suite, entry, replay_path, ds


def _python_standard_scan(
    ds, *, max_records: int, players: tuple[int, ...], profile_name: str
):
    samples = ds.samples
    n = int(samples.shape[0])
    if int(max_records) > 0:
        n = min(int(max_records), n)
    num_players = int(ds.header["num_players"])
    players_u8 = np.asarray(players, dtype=np.uint8)
    profile_rl1 = int(profile_name == "rl1_gameplay")
    binding = msl_binding
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(batch_size=1, num_players=num_players, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(samples.shape[0], sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    ref_off = int(samples.dtype.fields["ref_t1"][1])

    def reseed_at(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)

    def attempt(j: int) -> _AttemptResult:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        ref_compare_bytes = samples_u8[j : j + 1, ref_off : ref_off + compare_stride]
        code = int(
            binding.standard_rollout_compare(
                out_compare_bytes, ref_compare_bytes, players_u8, profile_rl1
            )
        )
        scored_code = code & 0xFF
        scored = (
            None
            if scored_code == 0
            else (
                "action_id",
                "animation_index",
                "on_ground",
                "hitlag",
                "hitstun",
                "state_flags",
            )[scored_code - 1]
        )
        ignored = "state_flags[4]&0x80" if (code & 0x100) != 0 else None
        return _AttemptResult(scored_field=scored, ignored_first_field=ignored)

    try:
        return _scan_rollout_streaks(
            n=n,
            reseed_at=reseed_at,
            attempt_from_current=attempt,
            attempt_seeded_at_record=lambda j: (reseed_at(j), attempt(j))[1],
        )
    finally:
        binding.destroy(handle)


def _python_dataset_streaks(
    *,
    dataset_path: Path,
    ds,
    max_records: int,
    players: tuple[int, ...],
    profile_name: str,
) -> DatasetStreaks:
    scan = _python_standard_scan(
        ds, max_records=max_records, players=players, profile_name=profile_name
    )
    samples = ds.samples
    seed = samples["seed_t"]
    ref = samples["ref_t1"]
    n = int(samples.shape[0])
    if int(max_records) > 0:
        n = min(int(max_records), n)
    start_seed_frame = None
    end_ref_frame_incl = None
    if scan.best_len > 0:
        start_seed_frame = int(seed["frame_id"][scan.best_start_record])
        end_ref_frame_incl = int(ref["frame_id"][scan.best_end_record_excl - 1])
    return DatasetStreaks(
        dataset=str(dataset_path),
        num_records=int(samples.shape[0]),
        max_records_used=n,
        players=players,
        fields=_STANDARD_ROLLOUT_FIELDS,
        best_len=int(scan.best_len),
        best_start_record=int(scan.best_start_record),
        best_end_record_excl=int(scan.best_end_record_excl),
        best_start_seed_frame_id=start_seed_frame,
        best_end_ref_frame_id_inclusive=end_ref_frame_incl,
        streak_histogram=dict(sorted(scan.streak_histogram.items())),
        first_mismatch_field_counts=dict(sorted(scan.first_mismatch_field_counts.items())),
        first_mismatch_field_counts_seeded=dict(sorted(scan.first_mismatch_field_counts_seeded.items())),
        ignored_first_mismatch_field_counts=dict(sorted(scan.ignored_first_mismatch_field_counts.items())),
        ignored_first_mismatch_field_counts_seeded=dict(
            sorted(scan.ignored_first_mismatch_field_counts_seeded.items())
        ),
        profile_name=profile_name,
    )


@pytest.mark.parametrize(
    ("label", "suite_rel", "replay_name", "max_records", "profile_name", "players", "expect"),
    [
        (
            "clean_prefix",
            "replays/suites/aggregate_recent.json",
            "AttachedGoodNaturedGuanaco.slpz",
            64,
            "rl1_gameplay",
            (0, 1),
            {"scored": False, "ignored": False, "seeded": False},
        ),
        (
            "rl1_ignored_lane",
            "replays/suites/aggregate_recent.json",
            "AttachedGoodNaturedGuanaco.slpz",
            0,
            "rl1_gameplay",
            (0, 1),
            {"scored": False, "ignored": True, "seeded": False},
        ),
        (
            "strict_scores_rl1_high_bit",
            "replays/suites/aggregate_recent.json",
            "AttachedGoodNaturedGuanaco.slpz",
            0,
            "strict",
            (0, 1),
            {"scored": True, "ignored": False, "seeded": True},
        ),
        (
            "non_default_player_subset",
            "replays/suites/aggregate_recent.json",
            "StiffLustrousZebra.slpz",
            0,
            "rl1_gameplay",
            (1,),
            {"scored": True, "ignored": True, "seeded": True},
        ),
        (
            "sheik_scored_and_seeded_mismatch",
            "replays/suites/aggregate_recent.json",
            "StiffLustrousZebra.slpz",
            0,
            "rl1_gameplay",
            (0, 1),
            {"scored": True, "ignored": True, "seeded": True},
        ),
        (
            "dream_land_scored_and_seeded_mismatch",
            "replays/suites/aggregate_recent.json",
            "WellWornSmallGoshawk.slpz",
            0,
            "rl1_gameplay",
            (0, 1),
            {"scored": True, "ignored": True, "seeded": True},
        ),
        (
            "dream_land_max_records",
            "replays/suites/aggregate_recent.json",
            "WellWornSmallGoshawk.slpz",
            1024,
            "rl1_gameplay",
            (0, 1),
            {"scored": True, "ignored": True, "seeded": False, "max_records": 1024},
        ),
    ],
)
def test_native_standard_rollout_scan_matches_python_orchestration(
    label: str,
    suite_rel: str,
    replay_name: str,
    max_records: int,
    profile_name: str,
    players: tuple[int, ...],
    expect: dict[str, object],
) -> None:
    suite, _entry, replay_path, ds = _build_suite_dataset(suite_rel=suite_rel, replay_name=replay_name)
    native = _scan_dataset_streaks(
        dataset_path=replay_path,
        ds=ds,
        fields=_STANDARD_ROLLOUT_FIELDS,
        players=players,
        max_records=max_records,
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        profile=profile_name,
    )
    py = _python_dataset_streaks(
        dataset_path=replay_path,
        ds=ds,
        max_records=max_records,
        players=players,
        profile_name=profile_name,
    )

    assert asdict(native) == asdict(py), label
    assert bool(native.first_mismatch_field_counts) is bool(expect["scored"])
    assert bool(native.ignored_first_mismatch_field_counts) is bool(expect["ignored"])
    assert bool(native.first_mismatch_field_counts_seeded) is bool(expect["seeded"])
    if "max_records" in expect:
        assert native.max_records_used == expect["max_records"]
