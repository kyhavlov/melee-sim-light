from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.dataset import read_dataset


ACT_GUARD = 179
ACT_GUARD_ON = 178
ACT_GUARD_OFF = 180
ACT_GUARD_SET_OFF = 181
ACT_GUARD_REFLECT = 182
ACT_KNEE_BEND = 24
ACT_LANDING = 42
ACT_FALL = 20
ACT_ATTACK_12 = 45
ACT_ATTACK_DASH = 50
ACT_ATTACK_HI3 = 56
ACT_ATTACK_LW4 = 64
ACT_CATCH = 212
ACT_FX_SPECIAL_HI_LANDING = 357
ACT_FX_SPECIAL_LW_END = 363


def _dataset(rel_path: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    return dataset_path


def _run_rollout_to_record(dataset_path: Path, *, start_record: int, target_record: int):
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert start_record <= target_record
    assert int(samples.shape[0]) > target_record

    binding = importlib.import_module("msl_binding")
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
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(
            samples[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, seed_stride)
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[:] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes[:] = np.frombuffer(
                samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)

        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        ref = samples[target_record]["ref_t1"]
        return samples[target_record], out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardsetoff_anim_enter_guard_then_same_frame_guardoff_dcc_replay_lock() -> None:
    # GuardSetOff_Anim runs in Fighter_8006A360 before Fighter_procUpdate input dispatch. When the
    # GuardDamage anim finishes into Guard, destination Guard_IASA can immediately latch release and
    # enter GuardOff.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_Guard_IASA,inlineC0}
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    record = 4241
    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(seed["guard_release_latched_xc"][p]) == 0

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_OFF


@pytest.mark.integration
def test_guardsetoff_same_frame_release_does_not_exit_before_anim_finishes() -> None:
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    record = 4238
    p = 1
    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_SET_OFF


@pytest.mark.integration
def test_guardreflect_terminal_snapshot_enters_guard_before_release_gat_replay_lock() -> None:
    # Terminal no-submotion GuardReflect snapshots with expired reflect timers expose the
    # GuardReflect_Anim -> GuardOn_Anim -> Guard handoff before the following GuardOff frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800928CC}
    dataset_path = _dataset(
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    record = 4834
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["guard_reflect_timer_x14"][p]) == 0
    assert int(seed["guard_reflect_timer_x18"][p]) == 0
    assert int(seed["guard_x10"][p]) == 1

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD
    assert int(out_t1["action_id"][p]) == ACT_GUARD


@pytest.mark.integration
def test_guardreflect_terminal_snapshot_gate_does_not_mask_active_timer_contact() -> None:
    dataset_path = _dataset(
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    record = 9479
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["guard_reflect_timer_x14"][p]) == 1
    assert int(seed["guard_reflect_timer_x18"][p]) == 3

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["action_id"][p]) != ACT_GUARD


@pytest.mark.integration
def test_same_frame_guardon_setoff_keeps_raw_x10_through_rollout_gat_859() -> None:
    # GAT 847 enters GuardSetOff from a same-frame GuardOn shield contact. Source initializes
    # mv.co.guard.x10 from the raw p_ftCommonData->x268 value before shieldstun; carrying the
    # replay-visible no-submotion GuardOn x10 shape exits to GuardOff one rollout frame early.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800924C0,ftCo_800921DC,ftCo_80092F2C,ftCo_800925A4}
    dataset_path = _dataset(
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    start_record = 847
    target_record = 859
    p = 0
    ds = read_dataset(str(dataset_path))
    seed_start = ds.samples[start_record]["seed_t"]
    target = ds.samples[target_record]["seed_t"]
    assert int(seed_start["action_id"][p]) not in {
        ACT_GUARD_ON,
        ACT_GUARD,
        ACT_GUARD_SET_OFF,
        ACT_GUARD_REFLECT,
    }
    assert int(ds.samples[start_record]["ref_t1"]["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(target["action_id"][p]) == ACT_GUARD
    assert int(target["guard_release_latched_xc"][p]) == 1
    assert int(target["guard_x10"][p]) == 1

    _, out_t1, ref_t1 = _run_rollout_to_record(
        dataset_path, start_record=start_record, target_record=target_record
    )
    assert int(ref_t1["action_id"][p]) == ACT_GUARD
    assert int(out_t1["action_id"][p]) == ACT_GUARD


@pytest.mark.integration
@pytest.mark.parametrize(
    ("start_record", "target_record"),
    [
        (4898, 4906),
        (6318, 6326),
    ],
)
def test_carried_guardsetoff_rows_still_consume_x10_before_guardoff_gat(
    start_record: int, target_record: int
) -> None:
    # GuardSetOff rows already seeded inside the shieldstun sequence do not carry the fresh
    # same-frame shield-entry owner. Their GuardSetOff_Anim -> Guard handoff exposes the ordinary
    # decremented x10, so released shield exits to GuardOff on the same rows vanilla does.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_800925A4,inlineC0}
    dataset_path = _dataset(
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    p = 0
    ds = read_dataset(str(dataset_path))
    seed_start = ds.samples[start_record]["seed_t"]
    target = ds.samples[target_record]["seed_t"]
    assert int(seed_start["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(seed_start["seed_prev_action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(target["action_id"][p]) == ACT_GUARD
    assert int(target["guard_x10"][p]) == 0
    assert int(target["guard_release_latched_xc"][p]) == 1

    _, out_t1, ref_t1 = _run_rollout_to_record(
        dataset_path, start_record=start_record, target_record=target_record
    )
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_OFF


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "start_record", "target_record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "MediumVirtualPig.msl",
            6175,
            6190,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "SweatyThisMallard.msl",
            2440,
            2454,
            1,
        ),
    ],
)
def test_powershield_guardsetoff_terminal_release_keeps_ftco_80094138_x10_clear(
    rel_path: str, start_record: int, target_record: int, p: int
) -> None:
    # Fighter shield contact with x221C_b2 live calls ftCo_80094138 before entering GuardSetOff;
    # that helper clears mv.co.guard.x10. The later GuardSetOff_Anim -> Guard handoff must therefore
    # let destination Guard_IASA exit to GuardOff on release, even if the replay-derived carry lane
    # still shows the pre-clear x10 value.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80094138,ftCo_80092F2C,ftCo_GuardSetOff_Anim,ftCo_Guard_IASA}
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[target_record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(seed["guard_x10"][p]) > 0
    assert any(
        int(ds.samples[r]["seed_t"]["state_flags"][p, 3]) & 0x20
        for r in range(start_record, target_record)
    )

    _, out_t1, ref_t1 = _run_rollout_to_record(
        dataset_path, start_record=start_record, target_record=target_record
    )
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_OFF


@pytest.mark.integration
def test_guardreflect_no_submotion_x18_does_not_consume_guard_catch_cdo_replay_lock() -> None:
    # GuardReflect no-submotion snapshots whose x14 ReflectDesc timer is still live do not expose a
    # fresh Catch_CheckInput A+LR consume on the same frozen row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "CornyDelayedOkapi.msl"
    )
    record = 1328
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    cur_input = ds.samples[record]["input_t"]
    prev_input = ds.samples[record]["prev_input_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["action_frame"][p]) <= -2
    assert int(seed["guard_reflect_timer_x14"][p]) > 0
    assert int(seed["guard_reflect_timer_x18"][p]) > 0
    assert (int(cur_input["p"][p]["buttons"]) & 0x0100) != 0
    assert (int(prev_input["p"][p]["buttons"]) & 0x0100) == 0

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(out_t1["action_id"][p]) == ACT_GUARD_REFLECT


@pytest.mark.integration
def test_guardreflect_no_submotion_x18_only_allows_guard_catch_tch_replay_lock() -> None:
    # Once x14 has expired, x18 alone keeps the powershield-active state bits but does not block the
    # normal GuardReflect IASA Catch_CheckInput path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    record = 11792
    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    cur_input = ds.samples[record]["input_t"]
    prev_input = ds.samples[record]["prev_input_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["action_frame"][p]) <= -2
    assert int(seed["guard_reflect_timer_x14"][p]) == 0
    assert int(seed["guard_reflect_timer_x18"][p]) > 0
    assert (int(cur_input["p"][p]["buttons"]) & 0x0100) != 0
    assert (int(prev_input["p"][p]["buttons"]) & 0x0100) == 0

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_CATCH
    assert int(out_t1["action_id"][p]) == ACT_CATCH


@pytest.mark.integration
def test_steady_guard_command_pose_keeps_birth_laser_out_of_hitshield_dsg_replay_lock() -> None:
    # Raw fp+0x2218_b1 marks the live command-pose shield center for a newborn SpecialN article.
    # Steady Guard uses that pose for the birth-frame item pass, not the settled Guard bubble that
    # would falsely route this laser through Item_80269DC8 / GuardSetOff one frame early.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091E78,ftCo_80092450}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"
    )
    record = 7430
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == ACT_GUARD
    assert int(seed["action_frame"][p]) < 0
    assert int(seed["state_flags"][p][0]) & 0x40
    assert int(ref["items"][1]["exists"]) == 1

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD
    assert int(out_t1["action_id"][p]) == ACT_GUARD
    assert int(out_t1["hitlag"][p]) == int(ref_t1["hitlag"][p]) == 0
    assert int(out_t1["items"][1]["exists"]) == int(ref_t1["items"][1]["exists"]) == 1


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            5588,
            1,
            ACT_FX_SPECIAL_HI_LANDING,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            6201,
            1,
            ACT_FX_SPECIAL_HI_LANDING,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            10583,
            1,
            ACT_FX_SPECIAL_LW_END,
        ),
    ],
)
def test_spacie_special_end_destination_wait_admits_guard_before_locomotion(
    rel_path: str, record: int, p: int, seed_action: int
) -> None:
    # Fox/Falco grounded special end Anim callbacks can enter Wait before this frame's input
    # callback dispatch. The destination Wait_IASA then reaches ftCo_80091A4C before jump/dash/
    # turn/walk, so held shield must be able to enter GuardOn on that same source frame.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwEnd_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == seed_action

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) == ACT_GUARD_ON


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            2487,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            8576,
            1,
        ),
    ],
)
def test_attacklw4_wait_iasa_admits_guard_without_locomotion_input(
    rel_path: str, record: int, p: int
) -> None:
    # AttackLw4_IASA delegates directly to ftCo_Wait_IASA when the command script has set
    # fp->allow_interrupt. Guard entry is owned by that destination IASA ordering even when no
    # jump/dash/walk input is present on the row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_ATTACK_LW4

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) == ACT_GUARD_ON


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_x672"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            10422,
            0,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PutridJoyousOryx.msl",
            6451,
            1,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "TubbyCurlyHerring.msl",
            11674,
            0,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            7503,
            0,
            0,
        ),
    ],
)
def test_guardon_iasa_powershield_uses_frame_start_x672_replay_locks(
    rel_path: str, record: int, p: int, seed_x672: int
) -> None:
    # GuardOn_IASA's ftCo_80093694 gate reads the LR edge and x672 trigger timer from the same
    # callback-visible input-history phase. Replay-real no-submotion GuardOn rows with a digital
    # L/R edge while the analog trigger is already held expose x672 <= 1 at the boundary;
    # incrementing the trigger timer before this gate misses the source GuardReflect handoff.
    # The TVR LandingFallSpecial case is the same ftCo_80091A4C owner after Landing_IASA's source
    # selectors miss; steady shield-owned GuardOn rows stay covered by the negative below.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardOn_IASA,ftCo_80093694,ftCo_8009388C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    seed = row["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_ON
    assert int(seed["action_frame"][p]) < 0
    assert int(seed["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed["seed_prev_action_id"][p]) not in {
        ACT_GUARD_ON,
        ACT_GUARD,
        ACT_GUARD_SET_OFF,
        ACT_GUARD_REFLECT,
    }
    assert int(seed["x672_input_timer"][p]) == seed_x672

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(out_t1["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(out_t1["action_frame"][p]) == int(ref_t1["action_frame"][p]) == -2


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_prev", "seed_x672"),
    [
        (
            "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            273,
            1,
            ACT_GUARD_ON,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PositiveRevolvingHyena.msl",
            5877,
            0,
            ACT_GUARD_ON,
            2,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "FavorableSuperficialPig.msl",
            869,
            0,
            ACT_ATTACK_12,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.msl",
            8414,
            0,
            ACT_ATTACK_DASH,
            1,
        ),
    ],
)
def test_guardon_iasa_powershield_x672_window_negative_replay_lock(
    rel_path: str, record: int, p: int, seed_prev: int, seed_x672: int
) -> None:
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_ON
    assert int(seed["seed_prev_action_id"][p]) == seed_prev
    assert int(seed["x672_input_timer"][p]) == seed_x672

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) != ACT_GUARD_REFLECT


@pytest.mark.integration
def test_guardon_no_submotion_steady_shield_does_not_reenter_guardreflect_tvr() -> None:
    # TVR:4031 is a steady no-submotion GuardOn snapshot: seed_prev is already shield-owned, and
    # source has already advanced mv.co.guard.x0 beyond the ftCo_80093694 powershield-reflect entry
    # window. A fresh digital R edge in the replay row must not make every hidden no-submotion
    # GuardOn snapshot look like a new ftCo_80091A4C entry. Fresh non-shield no-submotion entries
    # stay covered by test_guardon_iasa_powershield_uses_frame_start_x672_replay_locks.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_80093694}
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    record = 4031
    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_ON
    assert int(seed["seed_prev_action_id"][p]) == ACT_GUARD_ON
    assert int(seed["action_frame"][p]) < 0
    assert int(seed["animation_index"][p]) == 0xFFFFFFFF
    assert int(ds.samples[record]["input_t"]["p"]["buttons"][p]) & 0x0020  # R edge.

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["hitlag"][p]) == int(ref_t1["hitlag"][p]) == 0


@pytest.mark.integration
def test_landing_guard_entry_uses_frame_start_x672_before_current_l_press_tvr() -> None:
    # TVR:5891 rolls into Landing with replay-visible x672 already stale in the seed row, then the
    # current row presses digital L while the shield hit lands. The source trigger-history update
    # has made the live x672 byte powershield-eligible by the time Landing_IASA delegates to
    # ftCo_80091A4C, so the callback first enters GuardReflect/ReflectDesc and the same collision
    # pass immediately converts it to GuardSetOff. Grounded overlap Z-depth and the Landing-entry
    # ShieldDesc.size sweep then let the AttackHi3 hb1 shield contact publish GuardSetOff while
    # preserving x221C_b1/b2 from the powershield timer owner.
    # Dash controls stay separate because Dash_IASA has its own early/mid/late guard-entry owner.
    # refs/melee/src/melee/ft/fighter.c (x672 trigger-history update)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    target_record = 5891
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[target_record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_LANDING
    assert int(seed["x672_input_timer"][p]) == 254
    assert int(ds.samples[target_record]["input_t"]["p"]["buttons"][p]) & 0x0040  # L edge.

    _, out_t1, ref_t1 = _run_rollout_to_record(
        dataset_path, start_record=5768, target_record=target_record
    )
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["hitlag"][p]) == int(ref_t1["hitlag"][p]) == 6
    assert int(out_t1["state_flags"][p][3]) == int(ref_t1["state_flags"][p][3]) == 0x60


@pytest.mark.integration
def test_fall_guard_entry_uses_post_deadzone_x650_for_x672_tvr() -> None:
    # Fighter input maintenance zeroes fp->input.x650 when it is <= p_ftCommonData->x10 before the
    # x672 trigger-history block compares against the lower powershield threshold x18. TVR:9685 has
    # a raw previous analog trigger in (x18, x10] plus a current digital R edge; source resets x672
    # to 0 and the following shield-entry helper admits GuardReflect. Treating the raw analog
    # trigger as live would stale-carry x672=2 and fall through to GuardOn.
    # refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    record = 9685
    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    seed = row["seed_t"]
    assert int(seed["action_id"][p]) == ACT_FALL
    assert int(seed["x672_input_timer"][p]) == 2
    assert int(row["prev_input_t"]["p"]["buttons"][p]) & 0x0020 == 0
    assert int(row["input_t"]["p"]["buttons"][p]) & 0x0020  # R edge.
    prev_trigger = max(int(row["prev_input_t"]["p"]["l"][p]), int(row["prev_input_t"]["p"]["r"][p])) / 255.0
    assert 0.25 < prev_trigger <= 0.30000001192092896

    _, ref_t1, out_t1 = _run_one_step_row(
        dataset_path, record, p, ucf_enabled=True, ucf_cardinals_1_0_enabled=True
    )
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(out_t1["action_id"][p]) == ACT_GUARD_REFLECT


@pytest.mark.integration
def test_attacklw4_wait_iasa_catch_precedes_attack_restart_ldw_replay_lock() -> None:
    # The same AttackLw4 -> Wait_IASA delegation checks Catch before guard/locomotion and before
    # any later grounded attack restart. LDW:1150 is a Z+shield row that source routes to Catch,
    # not a side-tilt restart from the held stick.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    dataset_path = _dataset(
        "datasets/aggregate_recent/replays/validation/battlefield_recent/LoyalDishonestWren.msl"
    )
    record = 1150
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_ATTACK_LW4

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_CATCH
    assert int(out_t1["action_id"][p]) == ACT_CATCH


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            1670,
            1,
            ACT_ATTACK_LW4,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            9127,
            0,
            ACT_ATTACK_HI3,
        ),
    ],
)
def test_grounded_attack_wait_iasa_tap_jump_reaches_kneebend(
    rel_path: str, record: int, p: int, seed_action: int
) -> None:
    # Grounded AttackHi3/AttackLw4 IASA delegates to ftCo_Wait_IASA once allow_interrupt is live.
    # Wait_IASA checks jump input after catch/guard and before dash/squat/turn/walk, so a pure
    # tap-jump row reaches KneeBend instead of staying in the attack until another locomotion
    # predicate happens to enable the tail.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    dataset_path = _dataset(rel_path)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == seed_action
    assert int(ref["action_id"][p]) == ACT_KNEE_BEND

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(out_t1["action_id"][p]) == int(ref_t1["action_id"][p]) == ACT_KNEE_BEND
    assert int(out_t1["action_frame"][p]) == int(ref_t1["action_frame"][p]) == 0
