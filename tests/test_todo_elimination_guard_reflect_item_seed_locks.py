from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_GUARD_REFLECT = 0x00B6
STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20


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


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
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
    return seed, out, ref


@dataclass(frozen=True)
class _FixedRow:
    dataset_rel: str
    record: int
    slot: int
    expected_instance_id: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _FixedRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=4045,
            slot=0,
            expected_instance_id=861,
            note="fixed row A (AGG)",
        ),
        _FixedRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=4046,
            slot=0,
            expected_instance_id=861,
            note="fixed row B (AGG)",
        ),
        _FixedRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            record=9480,
            slot=0,
            expected_instance_id=2116,
            note="fixed row C (GAT)",
        ),
        _FixedRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            record=3494,
            slot=0,
            expected_instance_id=730,
            note="fixed row D (TBK)",
        ),
        _FixedRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            record=3495,
            slot=0,
            expected_instance_id=730,
            note="fixed row E (TBK)",
        ),
    ],
)
def test_guard_reflect_non_guardreflect_powershield_rows_lock_item_identity(case: _FixedRow) -> None:
    # Replay-real lock for powershield-active rows where action has already advanced out of
    # GuardReflect. Reflect ownership/identity must remain stable through item collision handling.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, case.record)
    slot = case.slot

    assert int(seed["items"][slot]["exists"]) == 1, case.note
    assert int(ref["items"][slot]["exists"]) == 1, case.note
    assert int(ref["items"][slot]["instance_id"]) == int(case.expected_instance_id), case.note

    # Lane precondition: powershield-active can persist while action is no longer GuardReflect.
    powershield_active_players = [
        p
        for p in range(int(seed["num_players"]))
        if (int(seed["state_flags"][p, 3]) & STATE_FLAG_221C_POWERSHIELD_ACTIVE) != 0
    ]
    assert powershield_active_players, case.note
    assert any(int(seed["action_id"][p]) != ACT_GUARD_REFLECT for p in powershield_active_players), case.note

    for fld in ("exists", "type", "instance_id"):
        got = int(out["items"][slot][fld])
        exp = int(ref["items"][slot][fld])
        assert got == exp, f"{case.note}: field={fld} expected={exp} got={got}"
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=case.record, p=p)


@dataclass(frozen=True)
class _ControlRow:
    dataset_rel: str
    record: int
    slot: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ControlRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=4043,
            slot=0,
            note="adjacent control A (pre-lane)",
        ),
        _ControlRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            record=9478,
            slot=0,
            note="adjacent control B (still GuardReflect)",
        ),
        _ControlRow(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            record=3492,
            slot=0,
            note="adjacent control C (pre-lane)",
        ),
    ],
)
def test_guard_reflect_item_identity_adjacent_controls(case: _ControlRow) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, case.record)
    slot = case.slot

    has_powershield_non_guardreflect = any(
        (int(seed["state_flags"][p, 3]) & STATE_FLAG_221C_POWERSHIELD_ACTIVE) != 0
        and int(seed["action_id"][p]) != ACT_GUARD_REFLECT
        for p in range(int(seed["num_players"]))
    )
    assert not has_powershield_non_guardreflect, case.note

    for fld in ("exists", "type", "instance_id"):
        got = int(out["items"][slot][fld])
        exp = int(ref["items"][slot][fld])
        assert got == exp, f"{case.note}: field={fld} expected={exp} got={got}"
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=case.record, p=p)
