from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    seed_visible_triplet: tuple[int, int, int]
    expected_out_state_flags_4: int
    expected_ref_state_flags_4: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=1920,
            p=1,
            seed_visible_triplet=(1, 1, 0),
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="F04 over-set blocker: camera-box bit stays visible on the seed row through late Rebirth/entry flow",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=2710,
            p=0,
            seed_visible_triplet=(0, 0, 1),
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="F04 under-set blocker: camera-box bit becomes visible one row after the seed in this DamageFly/Rebirth family",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=3227,
            p=0,
            seed_visible_triplet=(1, 1, 0),
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="F04 over-set blocker: late Rebirth visibility stays latched on the seed row",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=705,
            p=1,
            seed_visible_triplet=(0, 0, 1),
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="F04 under-set blocker: camera-box bit is absent on the seed row but visible on the next replay row",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=4514,
            p=0,
            seed_visible_triplet=(1, 1, 0),
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="F04 over-set blocker: camera-box bit clears on the reference row after staying set on the seed row",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            target_record=2534,
            p=0,
            seed_visible_triplet=(0, 0, 1),
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="F04 under-set blocker: camera-box bit becomes visible after the seed row in this dead-flow family",
        ),
    ],
)
def test_rebirth_camera_box_seed_lane_locks_blockers_and_adjacent_controls(case: _Case) -> None:
    # Foundational F04 blocker locks:
    # - ftLib_80086A8C exposes fp->x221F_b0 from fighter camera-subject visibility.
    # - Rebirth_Cam owns the callback path that updates that visibility during respawn flow.
    # - Slippi already exposes fp+0x221F in state_flags[...,4]; the new seed lane promotes the
    #   b0 mask as a named semantic internal for the future runtime ownership fix.
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p

    for rec, expected in zip(
        (case.target_record - 1, case.target_record, case.target_record + 1),
        case.seed_visible_triplet,
        strict=True,
    ):
        seed = samples[rec]["seed_t"]
        assert int(seed["camera_box_visible_x221f_b0"][p]) == int(expected), case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_target["state_flags"][p, 4]) == case.expected_out_state_flags_4, case.note
    assert int(ref_target["state_flags"][p, 4]) == case.expected_ref_state_flags_4, case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note
