from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _parse_field(field: str) -> tuple[str, int | None]:
    if "[" in field and field.endswith("]"):
        base, rest = field.split("[", 1)
        return base, int(rest[:-1])
    return field, None


def _scalar_seed_or_ref(row_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(row_obj[base][p])
    return int(row_obj[base][p, sub])


def _scalar_out(row_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(row_obj[base][p])
    return int(row_obj[base][p, sub])


def _run_one_step_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "fields"),
    [
        # Commit 2a3719d representative rows (KB lane ownership: int HitCapsule damage for KB path).
        (
            "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            6342,
            1,
            ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[3]"),
        ),
        (
            "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            10080,
            0,
            ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[3]"),
        ),
        (
            "replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            1283,
            1,
            ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[3]"),
        ),
        (
            "replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            5349,
            0,
            ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[3]"),
        ),
        # Commit 0cda0f9 representative rows (guard no-submotion body lane restore).
        (
            "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            1616,
            0,
            ("action_id", "hitlag", "state_flags[1]"),
        ),
        (
            "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            1616,
            1,
            ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[2]", "state_flags[3]"),
        ),
    ],
)
def test_followup_lock_rows_targeted_fields_match_ref(
    dataset_rel: str, record: int, p: int, fields: tuple[str, ...]
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record)

    # These rows should remain transition-meaningful (not seed==ref on all targeted lanes).
    seed_ref_diff = any(_scalar_seed_or_ref(seed, p=p, field=f) != _scalar_seed_or_ref(ref, p=p, field=f) for f in fields)
    assert seed_ref_diff, f"lock row lost transition signature: rec={record} p={p} fields={fields}"

    for field in fields:
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_followup_context_control_adjacent_rows_kb_lane() -> None:
    # Adjacent context controls around AGN:6342:p1 family (commit 2a3719d).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    p = 1
    # Pre-entry neighbor: stays neutral/unchanged.
    seed, ref, out = _run_one_step_row(dataset_path, 6341)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p]) == int(out["action_id"][p])
    assert int(ref["hitlag"][p]) == int(out["hitlag"][p]) == 0
    assert int(ref["hitstun"][p]) == int(out["hitstun"][p]) == 0

    # Post-entry neighbor: continuation frame keeps replay-real transition state.
    seed, ref, out = _run_one_step_row(dataset_path, 6343)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p])
    assert int(ref["hitlag"][p]) > 0
    assert int(ref["hitstun"][p]) > 0
    for field in ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[3]"):
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"context rec=6343 p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_followup_context_control_adjacent_rows_guard_nosubmotion_lane() -> None:
    # Adjacent context controls around AGN:1616 family (commit 0cda0f9).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    p = 1
    # Pre-transition neighbor: remains in prior state with neutral damage lanes.
    seed, ref, out = _run_one_step_row(dataset_path, 1615)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p]) == int(out["action_id"][p])
    assert int(ref["hitlag"][p]) == int(out["hitlag"][p]) == 0
    assert int(ref["hitstun"][p]) == int(out["hitstun"][p]) == 0

    # Post-transition neighbor: damage continuation shape is preserved.
    seed, ref, out = _run_one_step_row(dataset_path, 1617)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p])
    assert int(ref["hitlag"][p]) > 0
    assert int(ref["hitstun"][p]) > 0
    for field in ("action_id", "hitlag", "hitstun", "state_flags[1]", "state_flags[2]", "state_flags[3]"):
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"context rec=1617 p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_followup_lock_row_guard_reflect_snapshot_exit_gat_6315_p0() -> None:
    # Focus lock for reflect-cluster row family fixed by the GuardReflect frozen-snapshot lane split.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 6315
    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)

    # Exact replay-real preconditions for this no-submotion/frozen GuardReflect row.
    assert int(seed["action_id"][p]) == 182  # GuardReflect
    assert int(seed["action_frame"][p]) == -1
    assert int(seed["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["guard_reflect_timer_x14"][p]) == 1
    assert int(seed["guard_reflect_timer_x18"][p]) == 3

    # Expected replay transition target at t+1.
    assert int(ref["action_id"][p]) == 181  # GuardSetOff
    assert int(ref["action_frame"][p]) == 0
    assert int(ref["animation_index"][p]) == 40
    assert int(ref["hitlag"][p]) == 3
    assert int(ref["state_flags"][p, 1]) == 33
    assert int(ref["instance_id"][p]) == 1443

    # Strict parity on the lanes this slice targets for this row family.
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "state_flags[1]", "instance_id"):
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_followup_context_control_adjacent_guard_reflect_snapshot_gat_6314_p0() -> None:
    # Adjacent context control (pre-transition neighbor) for the rec=6315 reflect lock family.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 6314
    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)

    # Exact replay-real context lane: still GuardReflect snapshot with active timers.
    assert int(seed["action_id"][p]) == 182
    assert int(seed["action_frame"][p]) == -1
    assert int(seed["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed["guard_reflect_timer_x14"][p]) == 2
    assert int(seed["guard_reflect_timer_x18"][p]) == 4
    assert int(seed["state_flags"][p, 3]) == 112
    assert int(ref["state_flags"][p, 3]) == 96

    # Strict t+1 parity for adjacent control lanes.
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "state_flags[1]",
        "state_flags[3]",
        "instance_id",
    ):
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"context rec={record} p={p} field={field} expected={exp} got={got}"
