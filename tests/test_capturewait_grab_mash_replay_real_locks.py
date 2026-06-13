from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
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


def test_capturewait_owner_tick_uses_source_recent_buttons_or_sign_change() -> None:
    # Decomp: ftCommon_GrabMash treats fp->input.x668 AB/XY/LR or x1A50/x1A51 sign-latch changes
    # as active mash input, and CatchWait callback ownership can apply one extra
    # CaptureWait victim tick on the first steady owner frame.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    # refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10_Inner1
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    seed = np.zeros((4,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
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
    cur_view["p"]["main_y"][0, victim_p] = np.int8(0)

    # Case 1: freshly pressed B counts as mash-active through input.x668.
    cur_view["p"]["buttons"][1, victim_p] = np.uint16(0x0200)
    cur_view["p"]["main_y"][1, victim_p] = np.int8(0)

    # Case 2: ftCommon_GrabMash also treats a stick sign-latch change as mash-active, even without
    # AB/XY/LR held.
    cur_view["p"]["main_x"][2, victim_p] = np.int8(127)
    cur_view["p"]["main_y"][2, victim_p] = np.int8(-99)

    # Case 3: stale held L/R without an x668 edge does not count.
    prev_view["p"]["buttons"][3, victim_p] = np.uint16(0x0020)
    cur_view["p"]["main_y"][3, victim_p] = np.int8(0)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, victim_p]) == 227
    assert int(out["action_frame"][0, victim_p]) == 2
    assert int(out["action_id"][1, victim_p]) == 227
    assert int(out["action_frame"][1, victim_p]) == 3
    assert int(out["action_id"][2, victim_p]) == 227
    assert int(out["action_frame"][2, victim_p]) == 3
    assert int(out["action_id"][3, victim_p]) == 227
    assert int(out["action_frame"][3, victim_p]) == 2


def test_capturewait_breakout_owner_path_uses_timer_gate_or_explicit_pending_signal() -> None:
    # Decomp owner boundary:
    # - ftCo_CaptureWaitHi_Anim reaches the breakout gate after timer decrement and calls
    #   ftCo_800DA698 on the owner before CatchWait IASA.
    # - ftCo_800DA698 / ftCo_CaptureCut_Enter choose CatchCut + CaptureCut/CaptureJump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_800DA698,ftCo_CaptureCut_Enter}
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchWait_IASA
    seed = np.zeros((7,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
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
    seed["seed_prev_action_id"][:, victim_p] = np.uint16(227)
    seed["seed_prev_action_frame"][:, victim_p] = np.int16(2)
    seed["animation_index"][:, victim_p] = np.uint32(255)
    seed["anim_frame_f32"][:, victim_p] = np.float32(3.0)
    seed["frame_speed_mul_f32"][:, victim_p] = np.float32(1.0)
    seed["capture_grab_timer_f32"][:, victim_p] = np.float32(10.0)
    seed["capture_wait_counter_f32"][:, victim_p] = np.float32(3.0)
    seed["capture_breakout_pending_u8"][:, victim_p] = np.uint8(1)
    seed["capture_breakout_pending_u8"][3, victim_p] = np.uint8(0)
    seed["capture_grab_timer_f32"][3, victim_p] = np.float32(1.0)
    seed["action_id"][4, owner_p] = np.uint16(217)  # CatchAttack
    seed["animation_index"][4, owner_p] = np.uint32(245)
    seed["capture_wait_jump_latch_u8"][1, victim_p] = np.uint8(1)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_input = np.zeros((7, input_stride), dtype=np.uint8)
    cur_input = np.zeros((7, input_stride), dtype=np.uint8)
    cur_view = cur_input.view(INPUT_DTYPE).reshape((7,))

    # Case 0: breakout pending with no jump input resolves to CatchCut/CaptureCut.
    # Case 1: breakout pending with a pre-existing jump latch resolves to CatchCut/CaptureJump.
    # Case 2: Even with owner A pressed, timer-expired breakout has already replaced CatchWait
    # before CatchWait IASA can pummel.
    cur_view["p"]["buttons"][2, owner_p] = np.uint16(0x0100)
    # Case 3: live CaptureWait Anim timer expiry sets the same pending breakout owner before
    # CatchWait IASA resolves it. This locks the free-running path, not only replay seed repair.
    # Case 4: repeated-pummel windows can expose the owner as CatchAttack when the victim timer
    # expires; source ftCo_800DA698 still consumes the owner gobj and cuts the grab.
    # Case 5: same-frame X/Y edge would set mv.co.capturewait.xC in CaptureWait IASA, but a prior
    # Anim-owned breakout resolves before that IASA callback, so it cannot convert this frame to
    # CaptureJump.
    cur_view["p"]["buttons"][5, victim_p] = np.uint16(0x0400)
    # Case 6: direct stick-up fn_800DC044 is read by the Anim breakout owner itself.
    cur_view["p"]["main_y"][6, victim_p] = np.int8(80)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][0, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][1, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][1, victim_p]) == 230  # CaptureJump
    assert int(out["action_id"][2, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][2, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][3, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][3, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][4, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][4, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][5, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][5, victim_p]) == 229  # CaptureCut
    assert int(out["action_id"][6, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][6, victim_p]) == 230  # CaptureJump


def test_grab_breakout_cut_actions_end_through_common_source_paths() -> None:
    # Decomp:
    # - CatchCut_Anim / CaptureCut_Anim end through ftCommon_8007D92C, producing Wait on ground.
    # - CaptureJump_Anim ends through ftCo_Fall_Enter.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CatchCut_Anim,ftCo_CaptureJump_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_CaptureCut_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
    seed = np.zeros((3,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
    seed["num_players"] = np.uint8(2)
    seed["char_id"][:, :2] = np.uint8([1, 2])
    seed["stocks"][:, :2] = np.uint8([4, 4])
    seed["facing"][:, :2] = np.uint8([1, 1])
    seed["jumps_left"][:, :2] = np.uint8([2, 2])
    seed["frame_speed_mul_f32"][:, :2] = np.float32(1.0)

    p = 0
    seed["action_id"][:, p] = np.uint16([218, 229, 230])  # CatchCut/CaptureCut/CaptureJump
    seed["animation_index"][:, p] = np.uint32([246, 257, 258])
    seed["action_frame"][:, p] = np.int16(999)
    seed["anim_frame_f32"][:, p] = np.float32(999.0)
    seed["on_ground"][:2, p] = np.uint8(1)
    seed["ground_id"][:2, p] = np.uint16(1)
    seed["on_ground"][2, p] = np.uint8(0)
    seed["ground_id"][2, p] = np.uint16(0xFFFF)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_input = np.zeros((3, input_stride), dtype=np.uint8)
    cur_input = np.zeros((3, input_stride), dtype=np.uint8)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, p]) == 14  # Wait
    assert int(out["action_id"][1, p]) == 14  # Wait
    assert int(out["action_id"][2, p]) == 29  # Fall


@pytest.mark.integration
def test_doubles_capturewait_seed_lanes_prevent_zero_timer_false_breakout() -> None:
    # Regression lock for 4-player preprocessing:
    # - Capture hidden lanes are per victim slot, not singles-only state.
    # - A zeroed timer/counter seed is unknown state, not source evidence that the timer expired.
    # Building the doubles fixture from the committed replay must populate the live CaptureWait
    # timer/counter lanes so one-step rows do not falsely cut the grab.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/doubles_recent/Game_20260509T152622.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2, 3, 4],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    records = np.array([360, 390, 5129], dtype=np.int64)
    rows = samples[records]
    assert int(ds.header["num_players"]) == 4

    victim_ports = np.array([2, 2, 1], dtype=np.int64)
    for i, victim_p in enumerate(victim_ports):
        assert int(rows["seed_t"]["action_id"][i, victim_p]) == 227  # CaptureWaitLw
        assert float(rows["seed_t"]["capture_grab_timer_f32"][i, victim_p]) > 0.0
        assert float(rows["seed_t"]["capture_wait_counter_f32"][i, victim_p]) > 0.0

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    out = _step(
        rows["seed_t"],
        _input_bytes(rows, "prev_input_t", stride=input_stride),
        _input_bytes(rows, "input_t", stride=input_stride),
        num_players=4,
    )
    for i, victim_p in enumerate(victim_ports):
        owner_p = int(rows["seed_t"]["grab_owner_port"][i, victim_p])
        assert int(out["action_id"][i, victim_p]) == int(rows["ref_t1"]["action_id"][i, victim_p])
        assert int(out["action_id"][i, owner_p]) == int(rows["ref_t1"]["action_id"][i, owner_p])


@pytest.mark.integration
def test_capturewait_mash_trigger_rows_and_blocker_control_are_replay_exact() -> None:
    # Replay-real lock for the modeled CaptureWait extra-tick family:
    # - owner later slot, owner CatchWait 1->2, victim CaptureWaitLw 1->3
    # - victim mash is active via ftCommon_GrabMash inputs/sign-latch state
    # - a first-steady held X/Y row is not timer-rate reconstructed as a stale AB mash edge; that
    #   branch is owned by CaptureWait's xC jump-latch path instead.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    root = Path(__file__).resolve().parents[1]

    modeled_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    blocker_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    modeled_path = root / modeled_rel
    blocker_path = root / blocker_rel
    if not modeled_path.exists() or not blocker_path.exists():
        pytest.skip("missing local datasets for capturewait mash replay locks")

    modeled = read_dataset(str(modeled_path)).samples[[8256, 8257, 8258, 8259]]
    blocker = read_dataset(str(blocker_path)).samples[[980, 981, 982, 983, 3208, 3209]]
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

    # Regenerated AGNG keeps this held-Y first-steady blocker at x2344 == 0: X/Y jump-latch
    # ownership is not the CaptureWait AObj-rate bridge. Keep the lock to the adjacent no-mash row
    # above and to the modeled B-mash family.
    # bindings/msl_preprocess_native.c::msl_py_derive_capture_wait_lanes_from_series
    assert int(blocker[4]["seed_t"]["action_frame"][victim_p]) == 1
    assert int(blocker[4]["input_t"]["p"]["buttons"][victim_p]) == 0x0800  # held Y
    assert int(blocker[4]["seed_t"]["capture_wait_anim_rate_timer_f32"][victim_p]) == 0
    assert int(out_blocker["action_id"][5, victim_p]) == int(blocker["ref_t1"]["action_id"][5, victim_p]) == 227
    assert int(out_blocker["action_frame"][5, victim_p]) == int(blocker["ref_t1"]["action_frame"][5, victim_p]) == 4


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


@pytest.mark.integration
def test_capturewait_first_steady_seed_reconstruction_keeps_visible_timer_boundary() -> None:
    # Replay seed reconstruction boundary:
    # - An active x2344 animation-rate timer can still require reconstructing the prior AB mash edge.
    # - Stale X/Y/LR or already-latched stick state with x2344 active must not be treated as a new
    #   ftCommon_GrabMash edge.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    root = Path(__file__).resolve().parents[1]
    cases = (
        (
            root / "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl",
            360,
            2,
            3,
        ),
    )
    for dataset_path, record, victim_p, want_frame in cases:
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path}")
        seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, victim_p)
        assert int(seed_row["action_id"][victim_p]) == 227  # CaptureWaitLw
        assert int(seed_row["action_frame"][victim_p]) == 1
        assert int(ref_row["action_id"][victim_p]) == 227
        assert int(ref_row["action_frame"][victim_p]) == want_frame
        assert int(out_row["action_id"][victim_p]) == 227
        assert int(out_row["action_frame"][victim_p]) == want_frame
