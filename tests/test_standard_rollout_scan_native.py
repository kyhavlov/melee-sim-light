import heapq
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
import pytest

import msl_binding
from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_field
from tools.eval.run_longest_rollout_streaks import (
    _AttemptResult,
    DatasetStreaks,
    _STANDARD_ROLLOUT_FIELDS,
    _scan_dataset_streaks,
    _scan_dataset_streaks_with_native_float_rows,
    _scan_rollout_streaks,
)
from tools.eval.run_rollout_suite_eval import _float_compare_fields
from tools.eval.validation_profile import get_validation_profile
from tools.slippi.suite_io import load_suite, repo_root
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


@dataclass(frozen=True)
class _OracleFloatOffender:
    field: str
    abs_err: float
    dataset: str
    record: int
    p: int
    seed_frame: int
    ref_frame: int
    seed: float
    out: float
    ref: float
    seed_action_id: int
    out_action_id: int
    ref_action_id: int
    seed_action_frame: int
    out_action_frame: int
    ref_action_frame: int
    attempt: str
    seeded_retry: bool
    discrete_state_matches: bool
    streak_start_record: int
    streak_len: int


_OracleHeap = list[
    tuple[
        float,
        str,
        int,
        int,
        str,
        int,
        float,
        float,
        float,
        _OracleFloatOffender,
    ]
]


def _oracle_would_enter_top(heap: _OracleHeap, *, top: int, abs_err: float) -> bool:
    if abs_err <= 0.0:
        return False
    if len(heap) < top:
        return True
    return abs_err > heap[0][0]


def _oracle_push_top(heap: _OracleHeap, *, top: int, row: _OracleFloatOffender) -> None:
    item = (
        float(row.abs_err),
        str(row.dataset),
        int(row.record),
        int(row.p),
        str(row.attempt),
        int(bool(row.seeded_retry)),
        float(row.seed),
        float(row.out),
        float(row.ref),
        row,
    )
    if len(heap) < top:
        heapq.heappush(heap, item)
        return
    if float(row.abs_err) > heap[0][0]:
        heapq.heapreplace(heap, item)


def _oracle_sorted_rows(heaps: dict[str, _OracleHeap]) -> dict[str, list[dict]]:
    out: dict[str, list[dict]] = {}
    for field, heap in heaps.items():
        rows = [row for (_err, _ds, _rec, _p, _attempt, _seeded, _seed, _out, _ref, row) in heap]
        rows.sort(key=lambda r: (-r.abs_err, r.dataset, r.record, r.p))
        out[field] = [asdict(row) for row in rows]
    return out


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


def _python_collect_rollout_float_offenders_oracle(
    *,
    dataset_path: Path,
    ds,
    dataset_label: str,
    fields: tuple[str, ...],
    players: tuple[int, ...],
    top: int,
    max_records: int,
    threshold: float,
    discrete_fields: tuple[str, ...],
    profile: str,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
) -> dict[str, list[dict]]:
    samples = ds.samples
    num_records_total = int(samples.shape[0])
    num_players = int(ds.header["num_players"])
    n = num_records_total if int(max_records) <= 0 else min(num_records_total, int(max_records))

    binding = msl_binding
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=int(bool(ucf_enabled)),
        ucf_cardinals_1_0_enabled=int(bool(ucf_cardinals_1_0_enabled)),
    )

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records_total, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed = samples["seed_t"]
    ref = samples["ref_t1"]
    compare_lanes = compile_discrete_compare_lanes(
        discrete_fields,
        players,
        profile=get_validation_profile(profile),
    )
    heaps: dict[str, _OracleHeap] = {field: [] for field in fields}

    def reseed_at(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)

    def step(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

    def discrete_matches(j: int) -> bool:
        return first_mismatch_field(out_row=out_view[0], ref_row=ref[j], lanes=compare_lanes) is None

    def inspect_float_offenders(
        j: int,
        *,
        attempt: str,
        seeded_retry: bool,
        row_discrete_matches: bool,
        streak_start_record: int,
        streak_len: int,
    ) -> None:
        for field in fields:
            out_arr = out_view[0][field]
            ref_arr = ref[j][field]
            seed_arr = seed[j][field]
            for p in players:
                pp = int(p)
                err = abs(float(out_arr[pp]) - float(ref_arr[pp]))
                if err < threshold or not _oracle_would_enter_top(
                    heaps[field], top=top, abs_err=err
                ):
                    continue
                _oracle_push_top(
                    heaps[field],
                    top=top,
                    row=_OracleFloatOffender(
                        field=field,
                        abs_err=err,
                        dataset=dataset_label,
                        record=int(j),
                        p=pp,
                        seed_frame=int(seed["frame_id"][j]),
                        ref_frame=int(ref["frame_id"][j]),
                        seed=float(seed_arr[pp]),
                        out=float(out_arr[pp]),
                        ref=float(ref_arr[pp]),
                        seed_action_id=int(seed["action_id"][j, pp]),
                        out_action_id=int(out_view[0]["action_id"][pp]),
                        ref_action_id=int(ref["action_id"][j, pp]),
                        seed_action_frame=int(seed["action_frame"][j, pp]),
                        out_action_frame=int(out_view[0]["action_frame"][pp]),
                        ref_action_frame=int(ref["action_frame"][j, pp]),
                        attempt=str(attempt),
                        seeded_retry=bool(seeded_retry),
                        discrete_state_matches=bool(row_discrete_matches),
                        streak_start_record=int(streak_start_record),
                        streak_len=int(streak_len),
                    ),
                )

    try:
        cur_start = 0
        cur_len = 0
        needs_seed = True
        for j in range(n):
            if needs_seed:
                reseed_at(cur_start)
                needs_seed = False

            step(j)
            row_discrete_matches = discrete_matches(j)
            inspect_float_offenders(
                j,
                attempt="free_run",
                seeded_retry=False,
                row_discrete_matches=row_discrete_matches,
                streak_start_record=cur_start,
                streak_len=cur_len,
            )
            if row_discrete_matches:
                cur_len += 1
                continue

            cur_start = j
            cur_len = 0
            reseed_at(j)
            step(j)
            retry_discrete_matches = discrete_matches(j)
            inspect_float_offenders(
                j,
                attempt="seeded_retry",
                seeded_retry=True,
                row_discrete_matches=retry_discrete_matches,
                streak_start_record=cur_start,
                streak_len=0,
            )
            if retry_discrete_matches:
                cur_len = 1
            else:
                cur_start = j + 1
                cur_len = 0
                needs_seed = True
    finally:
        binding.destroy(handle)

    return _oracle_sorted_rows(heaps)


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


@pytest.mark.parametrize(
    ("label", "suite_rel", "replay_name", "max_records", "profile_name", "players", "top"),
    [
        (
            "clean_prefix",
            "replays/suites/aggregate_recent.json",
            "AttachedGoodNaturedGuanaco.slpz",
            512,
            "rl1_gameplay",
            (0, 1),
            4,
        ),
        (
            "strict_profile",
            "replays/suites/aggregate_recent.json",
            "AttachedGoodNaturedGuanaco.slpz",
            1024,
            "strict",
            (0, 1),
            4,
        ),
        (
            "non_default_player_subset",
            "replays/suites/aggregate_recent.json",
            "StiffLustrousZebra.slpz",
            1024,
            "rl1_gameplay",
            (1,),
            4,
        ),
        (
            "sheik_seeded_mismatch",
            "replays/suites/aggregate_recent.json",
            "StiffLustrousZebra.slpz",
            2048,
            "rl1_gameplay",
            (0, 1),
            4,
        ),
        (
            "dream_land",
            "replays/suites/aggregate_recent.json",
            "WellWornSmallGoshawk.slpz",
            2048,
            "rl1_gameplay",
            (0, 1),
            4,
        ),
    ],
)
def test_native_standard_rollout_float_rows_match_python_collector(
    label: str,
    suite_rel: str,
    replay_name: str,
    max_records: int,
    profile_name: str,
    players: tuple[int, ...],
    top: int,
) -> None:
    suite, _entry, replay_path, ds = _build_suite_dataset(suite_rel=suite_rel, replay_name=replay_name)
    dataset_label = str(replay_path.relative_to(repo_root()))
    float_fields = _float_compare_fields()

    _streaks, native_rows = _scan_dataset_streaks_with_native_float_rows(
        dataset_path=replay_path,
        ds=ds,
        fields=_STANDARD_ROLLOUT_FIELDS,
        players=players,
        max_records=max_records,
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        profile=profile_name,
        float_fields=float_fields,
        float_top=top,
        float_threshold=0.0,
        float_dataset_label=dataset_label,
    )
    expected_rows = _python_collect_rollout_float_offenders_oracle(
        dataset_path=replay_path,
        ds=ds,
        dataset_label=dataset_label,
        fields=float_fields,
        players=players,
        top=top,
        max_records=max_records,
        threshold=0.0,
        discrete_fields=_STANDARD_ROLLOUT_FIELDS,
        profile=profile_name,
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
    )

    assert native_rows == expected_rows, label
