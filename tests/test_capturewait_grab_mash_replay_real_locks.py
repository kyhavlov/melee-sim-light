from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row


def _step(seed: np.ndarray, prev_input: np.ndarray, cur_input: np.ndarray, *, num_players: int) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(len(seed), seed_stride)
    prev_input_bytes = prev_input.reshape(len(seed), input_stride)
    input_bytes = cur_input.reshape(len(seed), input_stride)
    out_compare_bytes = np.empty((len(seed), compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=len(seed), num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)


def _input_bytes(rows: np.ndarray, field: str, *, stride: int) -> np.ndarray:
    return np.frombuffer(rows[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(len(rows), stride)


def test_capturewait_owner_tick_uses_mash_buttons_sign_change_or_prev_button_carry() -> None:
    # Decomp: ftCommon_GrabMash treats held AB/XY/LR or x1A50/x1A51 sign-latch changes as active
    # mash input, and CatchWait callback ownership can apply one extra CaptureWait victim tick on
    # the first steady owner frame.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    seed = np.zeros((4,), dtype=SEED_DTYPE)
    seed["num_players"] = np.uint8(2)
    seed["char_id"][:, :2] = np.uint8([1, 2])
    seed["stocks"][:, :2] = np.uint8([4, 4])
    seed["on_ground"][:, :2] = np.uint8([1, 1])
    seed["facing"][:, :2] = np.uint8([1, 1])
    seed["jumps_left"][:, :2] = np.uint8([2, 2])

    owner_p = 1
    victim_p = 0
    seed["grab_owner_port"][:, victim_p] = np.uint8(owner_p)

    seed["action_id"][:, owner_p] = np.uint16(216)  # CatchWait
    seed["action_frame"][:, owner_p] = np.int16(1)
    seed["animation_index"][:, owner_p] = np.uint32(244)
    seed["anim_frame_f32"][:, owner_p] = np.float32(1.0)
    seed["frame_speed_mul_f32"][:, owner_p] = np.float32(1.0)

    seed["action_id"][:, victim_p] = np.uint16(227)  # CaptureWaitLw
    seed["action_frame"][:, victim_p] = np.int16(1)
    seed["animation_index"][:, victim_p] = np.uint32(255)
    seed["anim_frame_f32"][:, victim_p] = np.float32(1.0)
    seed["frame_speed_mul_f32"][:, victim_p] = np.float32(1.0)
    seed["seed_prev_action_id"][:, victim_p] = np.uint16(227)
    seed["seed_prev_action_frame"][:, victim_p] = np.int16(0)
    seed["capture_grab_timer_f32"][:, victim_p] = np.float32(100.0)
    seed["capture_wait_counter_f32"][:, victim_p] = np.float32(0.0)
    seed["capture_wait_anim_rate_timer_f32"][:, victim_p] = np.float32(0.0)
    seed["grab_mash_stick_x_sign"][:, victim_p] = np.int8(-1)
    seed["grab_mash_stick_y_sign"][:, victim_p] = np.int8(-1)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_input = np.zeros((4, input_stride), dtype=np.uint8)
    cur_input = np.zeros((4, input_stride), dtype=np.uint8)
    prev_view = prev_input.view(INPUT_DTYPE).reshape((4,))
    cur_view = cur_input.view(INPUT_DTYPE).reshape((4,))

    # Case 0: no mash-active input -> normal one-tick advance.
    cur_view["p"]["main_y"][0, victim_p] = np.int8(-99)

    # Case 1: continuously held B counts as mash-active even with no sign change.
    prev_view["p"]["buttons"][1, victim_p] = np.uint16(0x0200)
    cur_view["p"]["buttons"][1, victim_p] = np.uint16(0x0200)
    cur_view["p"]["main_y"][1, victim_p] = np.int8(-99)

    # Case 2: ftCommon_GrabMash also treats a stick sign-latch change as mash-active, even without
    # AB/XY/LR held.
    cur_view["p"]["main_x"][2, victim_p] = np.int8(127)
    cur_view["p"]["main_y"][2, victim_p] = np.int8(-99)

    # Case 3: first steady CaptureWait frame can still consume prior-frame held mash buttons.
    prev_view["p"]["buttons"][3, victim_p] = np.uint16(0x0200)
    cur_view["p"]["main_y"][3, victim_p] = np.int8(-99)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, victim_p]) == 227
    assert int(out["action_frame"][0, victim_p]) == 2
    assert int(out["action_id"][1, victim_p]) == 227
    assert int(out["action_frame"][1, victim_p]) == 3
    assert int(out["action_id"][2, victim_p]) == 227
    assert int(out["action_frame"][2, victim_p]) == 3
    assert int(out["action_id"][3, victim_p]) == 227
    assert int(out["action_frame"][3, victim_p]) == 3


def test_capturewait_breakout_owner_path_uses_explicit_pending_signal() -> None:
    # Decomp owner boundary:
    # - ftCo_CaptureWaitHi_Anim reaches the breakout gate after timer decrement.
    # - ftCo_CatchWait_IASA owns pummel -> throw -> breakout adjacency on the owner.
    # - ftCo_800DA698 / ftCo_CaptureCut_Enter choose CatchCut + CaptureCut/CaptureJump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_800DA698,ftCo_CaptureCut_Enter}
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchWait_IASA
    seed = np.zeros((3,), dtype=SEED_DTYPE)
    seed["num_players"] = np.uint8(2)
    seed["char_id"][:, :2] = np.uint8([1, 2])
    seed["stocks"][:, :2] = np.uint8([4, 4])
    seed["on_ground"][:, :2] = np.uint8([1, 1])
    seed["facing"][:, :2] = np.uint8([1, 1])
    seed["jumps_left"][:, :2] = np.uint8([2, 2])

    owner_p = 1
    victim_p = 0
    seed["grab_owner_port"][:, victim_p] = np.uint8(owner_p)

    seed["action_id"][:, owner_p] = np.uint16(216)  # CatchWait
    seed["action_frame"][:, owner_p] = np.int16(1)
    seed["animation_index"][:, owner_p] = np.uint32(244)
    seed["anim_frame_f32"][:, owner_p] = np.float32(1.0)
    seed["frame_speed_mul_f32"][:, owner_p] = np.float32(1.0)

    seed["action_id"][:, victim_p] = np.uint16(227)  # CaptureWaitLw
    seed["action_frame"][:, victim_p] = np.int16(3)
    seed["animation_index"][:, victim_p] = np.uint32(255)
    seed["anim_frame_f32"][:, victim_p] = np.float32(3.0)
    seed["frame_speed_mul_f32"][:, victim_p] = np.float32(1.0)
    seed["capture_grab_timer_f32"][:, victim_p] = np.float32(10.0)
    seed["capture_wait_counter_f32"][:, victim_p] = np.float32(3.0)
    seed["capture_breakout_pending_u8"][:, victim_p] = np.uint8(1)
    seed["capture_wait_jump_latch_u8"][1, victim_p] = np.uint8(1)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_input = np.zeros((3, input_stride), dtype=np.uint8)
    cur_input = np.zeros((3, input_stride), dtype=np.uint8)
    cur_view = cur_input.view(INPUT_DTYPE).reshape((3,))

    # Case 0: breakout pending with no jump input resolves to CatchCut/CaptureCut.
    # Case 1: breakout pending with jump latch resolves to CatchCut/CaptureJump.
    cur_view["p"]["main_y"][1, victim_p] = np.int8(80)
    # Case 2: CatchWait pummel check beats breakout on the same frame.
    cur_view["p"]["buttons"][2, owner_p] = np.uint16(0x0100)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][0, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][1, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][1, victim_p]) == 230  # CaptureJump
    assert int(out["action_id"][2, owner_p]) == 217  # CatchAttack
    assert int(out["action_id"][2, victim_p]) == 227  # CaptureWaitLw


@pytest.mark.integration
def test_capturewait_mash_trigger_rows_and_blocker_control_are_replay_exact() -> None:
    # Replay-real lock for the modeled CaptureWait extra-tick family:
    # - owner later slot, owner CatchWait 1->2, victim CaptureWaitLw 1->3
    # - victim mash is active via ftCommon_GrabMash inputs/sign-latch state
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    root = Path(__file__).resolve().parents[1]

    modeled_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    blocker_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    modeled_path = root / modeled_rel
    blocker_path = root / blocker_rel
    if not modeled_path.exists() or not blocker_path.exists():
        pytest.skip("missing local datasets for capturewait mash replay locks")

    modeled = read_dataset(str(modeled_path)).samples[[8256, 8257, 8258, 8259]]
    blocker = read_dataset(str(blocker_path)).samples[[980, 981, 982, 983]]
    victim_p = 0
    owner_p = 1

    target = modeled[1]
    assert int(target["seed_t"]["action_id"][owner_p]) == 216
    assert int(target["seed_t"]["action_frame"][owner_p]) == 1
    assert int(target["seed_t"]["action_id"][victim_p]) == 227
    assert int(target["seed_t"]["action_frame"][victim_p]) == 1
    assert int(target["seed_t"]["grab_mash_stick_x_sign"][victim_p]) == -1
    assert int(target["seed_t"]["grab_mash_stick_y_sign"][victim_p]) == -1
    assert int(target["input_t"]["p"]["buttons"][victim_p]) == 0x0200  # held B
    assert int(target["ref_t1"]["action_id"][victim_p]) == 227
    assert int(target["ref_t1"]["action_frame"][victim_p]) == 3

    out_modeled = _step(
        modeled["seed_t"],
        _input_bytes(modeled, "prev_input_t", stride=int(pytest.importorskip("msl_binding").sizes()["input"])),
        _input_bytes(modeled, "input_t", stride=int(pytest.importorskip("msl_binding").sizes()["input"])),
        num_players=2,
    )

    # Negative control / target-1 / target / target+1 on modeled family.
    assert int(out_modeled["action_id"][0, victim_p]) == int(modeled["ref_t1"]["action_id"][0, victim_p]) == 227
    assert int(out_modeled["action_frame"][0, victim_p]) == int(modeled["ref_t1"]["action_frame"][0, victim_p]) == 1
    assert int(out_modeled["action_id"][1, victim_p]) == int(modeled["ref_t1"]["action_id"][1, victim_p]) == 227
    assert int(out_modeled["action_frame"][1, victim_p]) == int(modeled["ref_t1"]["action_frame"][1, victim_p]) == 3
    assert int(out_modeled["animation_index"][1, victim_p]) == int(modeled["ref_t1"]["animation_index"][1, victim_p]) == 255
    assert int(out_modeled["instance_id"][1, victim_p]) == int(modeled["ref_t1"]["instance_id"][1, victim_p]) == 1472
    assert int(out_modeled["action_id"][2, victim_p]) == int(modeled["ref_t1"]["action_id"][2, victim_p]) == 227
    assert int(out_modeled["action_frame"][2, victim_p]) == int(modeled["ref_t1"]["action_frame"][2, victim_p]) == 5
    assert int(out_modeled["action_id"][3, victim_p]) == int(modeled["ref_t1"]["action_id"][3, victim_p]) == 227
    assert int(out_modeled["action_frame"][3, victim_p]) == int(modeled["ref_t1"]["action_frame"][3, victim_p]) == 7

    # Nearby blocker control: same owner frame shape but no mash-active input, so no extra tick.
    out_blocker = _step(
        blocker["seed_t"],
        _input_bytes(blocker, "prev_input_t", stride=int(pytest.importorskip("msl_binding").sizes()["input"])),
        _input_bytes(blocker, "input_t", stride=int(pytest.importorskip("msl_binding").sizes()["input"])),
        num_players=2,
    )
    assert int(blocker[1]["seed_t"]["action_frame"][victim_p]) == 1
    assert int(blocker[1]["ref_t1"]["action_frame"][victim_p]) == 2
    assert int(blocker[1]["input_t"]["p"]["buttons"][victim_p]) == 0
    assert int(out_blocker["action_id"][1, victim_p]) == int(blocker["ref_t1"]["action_id"][1, victim_p]) == 227
    assert int(out_blocker["action_frame"][1, victim_p]) == int(blocker["ref_t1"]["action_frame"][1, victim_p]) == 2


@pytest.mark.integration
def test_capturewait_owner_earlier_first_steady_rows_are_replay_exact() -> None:
    # Decomp callback ordering:
    # - CaptureWait victims can receive one extra same-frame timeline advance on the first steady
    #   owner frame when the owner family is still in the CatchPull -> CatchWait/CatchAttack chain.
    # - By pre-physics, a seeded CatchAttack af=0 row has already advanced to af=1, so the owner
    #   lane must match the first steady CatchAttack frame rather than the seed snapshot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   fn_800DA1D8,ftCo_CatchWait_IASA,ftCo_CatchAttack_Anim,ftCo_CaptureWaitHi_Anim}
    root = Path(__file__).resolve().parents[1]
    cases = (
        (
            root
            / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            10714,
            1,
            227,
            3,
        ),
        (
            root
            / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            10934,
            1,
            227,
            3,
        ),
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            4367,
            1,
            224,
            3,
        ),
    )
    for dataset_path, record, victim_p, want_action, want_frame in cases:
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path}")
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, victim_p)
        assert int(ref_row["action_id"][victim_p]) == want_action
        assert int(ref_row["action_frame"][victim_p]) == want_frame
        assert int(out_row["action_id"][victim_p]) == want_action
        assert int(out_row["action_frame"][victim_p]) == want_frame
