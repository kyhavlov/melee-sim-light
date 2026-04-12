from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_OFF = 0x00B4
ACT_GUARD_REFLECT = 0x00B6
ACT_KNEE_BEND = 0x0018
ACT_ATTACK_HI4 = 0x003F

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_GUARD_ON = 37
SM_GUARD = 38
SM_GUARD_OFF = 39
SM_KNEE_BEND = 11

CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["shield_hp"][0, 0] = np.float32(_common_attr("start_shield_health"))
    return seed


def test_grounded_guard_entry_hold_exit_changes_action_and_drains_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = _seed_base()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        neutral = _mk_input_bytes(1, input_stride)
        shield = _mk_input_bytes(1, input_stride)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        msl_binding.reseed_seed(handle, seed_bytes)

        # Entry: neutral -> shield.
        msl_binding.step_input(handle, neutral, shield)
        msl_binding.write_compare(handle, out)
        out0 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()

        assert int(out0["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD)
        # In our replay-derived datasets, shield states often have `animation_index == -1`.
        if int(out0["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD):
            assert int(out0["animation_index"][0]) == 0xFFFFFFFF

        # Hold: shield -> shield for a few frames; shield_hp should drain below start.
        start_hp = float(_common_attr("start_shield_health"))
        hp = float(out0["shield_hp"][0])
        for _ in range(10):
            msl_binding.step_input(handle, shield, shield)
            msl_binding.write_compare(handle, out)
            outn = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            hp = float(outn["shield_hp"][0])
            assert int(outn["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD)
        assert hp < start_hp

        # Exit: shield -> neutral should enter GuardOff.
        msl_binding.step_input(handle, shield, neutral)
        msl_binding.write_compare(handle, out)
        out_exit = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out_exit["action_id"][0]) == ACT_GUARD_OFF
        assert int(out_exit["animation_index"][0]) == SM_GUARD_OFF

        # GuardOff anim eventually returns to Wait.
        out_last = out_exit
        for _ in range(120):
            msl_binding.step_input(handle, neutral, neutral)
            msl_binding.write_compare(handle, out)
            out_last = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(out_last["action_id"][0]) == ACT_WAIT:
                break
        assert int(out_last["action_id"][0]) == ACT_WAIT
        assert int(out_last["animation_index"][0]) == SM_WAIT1_0
    finally:
        msl_binding.destroy(handle)


def test_guard_state_does_not_reenter_guard_reflect_on_lr_edge() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["x672_input_timer"][0, 0] = np.uint8(0)
    seed["guard_x10"][0, 0] = np.uint8(3)
    seed["state_flags"][0, 0, 2] = np.uint8(0x80)  # fp+0x221B isShieldActive

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        neutral = _mk_input_bytes(1, input_stride)
        shield_edge = _mk_input_bytes(1, input_stride)
        shield_edge_view = shield_edge.view(INPUT_DTYPE).reshape((1,))
        shield_edge_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        # Decomp: powershield re-entry helper ftCo_80093694 is called from GuardOn_IASA, not
        # Guard_IASA, so a Guard frame with an LR pressed-edge should remain in Guard.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_80093694}
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, shield_edge)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD
    finally:
        msl_binding.destroy(handle)


def test_active_guard_hitlag_does_not_recharge_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["hitlag"][0, 0] = np.uint8(3)
    seed["shield_hp"][0, 0] = np.float32(55.0)
    seed["state_flags"][0, 0, 2] = np.uint8(0x80)  # fp+0x221B isShieldActive

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        neutral = _mk_input_bytes(1, input_stride)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, neutral)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD
        assert int(got["hitlag"][0]) == 2
        assert float(got["shield_hp"][0]) == np.float32(55.0)
    finally:
        msl_binding.destroy(handle)


def test_seeded_kneebend_still_allows_attack_hi4_iasa() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_KNEE_BEND)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEE_BEND)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        cur_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Decomp: ftCo_KneeBend_IASA still allows AttackHi4 interrupt on a seeded KneeBend row
        # through ftCo_AttackHi4_CheckInputNoD0 / ftCo_800DF2D8.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_CheckInputNoD0
        cur_view["p"]["c_y"][0, 0] = np.int8(80)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_ATTACK_HI4
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
def test_guard_jump_oos_does_not_reconsume_kneebend_iasa_positive_revolving_hyena() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # Replay-real guard jump OoS rows:
    # - Guard_IASA can enter KneeBend through ftCo_800CB024.
    # - The same frame is still the Guard input callback; the fresh KneeBend row must not also
    #   consume KneeBend_IASA into AttackHi4 before the next frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
    for record in (282, 389):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, 1)
        assert int(ref_row["action_id"][1]) == ACT_KNEE_BEND
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]), (
            f"record={record} expected_action={int(ref_row['action_id'][1])} "
            f"got={int(out_row['action_id'][1])}"
        )
        assert int(out_row["action_frame"][1]) == int(ref_row["action_frame"][1])
        assert int(out_row["animation_index"][1]) == int(ref_row["animation_index"][1])
