from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


@dataclass(frozen=True)
class _DamageGroundWaitIasaCase:
    dataset_rel: str
    record: int
    player: int
    expected_action_id: int
    expected_animation_index: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=4036,
            player=1,
            expected_action_id=15,  # WalkSlow
            expected_animation_index=7,  # ftCo_SM_WalkSlow
            note="DamageHi1 grounded Wait_IASA walk branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=1948,
            player=0,
            expected_action_id=20,  # Dash
            expected_animation_index=12,  # ftCo_SM_Dash
            note="DamageN1 grounded Wait_IASA dash branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=7260,
            player=0,
            expected_action_id=39,  # Squat
            expected_animation_index=30,  # ftCo_SM_Squat
            note="DamageHi1 grounded Wait_IASA squat branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=4436,
            player=0,
            expected_action_id=39,  # Squat
            expected_animation_index=30,  # ftCo_SM_Squat
            note="DamageHi1 grounded held-B neutral-X still falls through to Squat",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            record=8765,
            player=0,
            expected_action_id=18,  # Turn
            expected_animation_index=10,  # ftCo_SM_Turn
            note="DamageN2 grounded Wait_IASA turn branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=5068,
            player=0,
            expected_action_id=64,  # AttackLw4
            expected_animation_index=67,  # ftCo_SM_AttackLw4
            note="DamageHi3 grounded Wait_IASA keeps earlier grounded A-attack ownership before guard",
        ),
    ],
)
def test_damage_ground_wait_iasa_replay_real_transition_locks(case: _DamageGroundWaitIasaCase) -> None:
    # Replay-real locks for grounded Damage_IASA -> Wait_IASA delegation in src/knockdown.c.
    #
    # Decomp:
    # - grounded Damage_IASA delegates to Wait_IASA when fp->x221C_b6 is clear.
    # - Wait_IASA then owns locomotion interrupts including Dash/Squat/Turn/Walk.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = int(case.player)
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]

    seed = row["seed_t"]
    ref = row["ref_t1"]

    assert int(seed["on_ground"][0, p]) == 1, case.note
    assert int(seed["hitlag"][0, p]) == 0, case.note
    assert int(seed["action_id"][0, p]) in (75, 77, 78, 79), case.note  # DamageHi1 / DamageHi3 / DamageN1 / DamageN2
    if record == 4436:
        assert int(row["input_t"][0]["p"]["buttons"][p]) == 0x0200, case.note
        assert int(row["input_t"][0]["p"]["main_x"][p]) == 0, case.note
        assert int(row["input_t"][0]["p"]["main_y"][p]) == -102, case.note
    assert int(ref["action_id"][0, p]) == case.expected_action_id, case.note
    assert int(ref["animation_index"][0, p]) == case.expected_animation_index, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_grounded_damage_hitstun_jumpbuffer_rollout_enters_kneebend() -> None:
    # Rollout-real lock for grounded Damage hitstun-side doIasa ownership:
    # - ftCo_Damage_IASA calls doIasa while x221C_b6 is still set.
    # - A qualifying jump input snapshots mv.co.damage.x0 into mv.co.damage.x14.
    # - After hitstun clears, grounded Damage_IASA injects XY before the Wait_IASA delegate, so
    #   held down-stick does not incorrectly become Squat on the release frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    p = 0
    start_record = 167
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed(handle, seed_bytes)
        for record in range(start_record, 212):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out_row = out_view[0].copy()
            ref_row = samples["ref_t1"][record]
            if record < 211:
                assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), record
                assert int(out_row["action_id"][p]) != 24, record  # KneeBend not before x14 release.
            else:
                assert int(samples["seed_t"][208]["damage_jump_buffer_x14"][p]) == 4
                assert int(out_row["action_id"][p]) == 24  # KneeBend
                assert int(ref_row["action_id"][p]) == 24
                assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]) == 0
                assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]) == 0
                assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
                break
    finally:
        binding.destroy(handle)
