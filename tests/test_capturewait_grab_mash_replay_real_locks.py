from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
from tests.replay_dataset_loader import load_replay_dataset as read_dataset
from tests.replay_dataset_loader import replay_dataset_available
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


def _step_one_dataset_row(dataset_path: Path, record: int, *, seed_mutator=None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return seed.reshape(-1)[0], out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], row["ref_t1"].reshape(-1)[0]


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "victim", "expected_action", "expected_anim", "expected_on_ground"),
    [
        ("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl", 7484, 0, 224, 252, 0),
        ("datasets/sheik/replays/validation/sheik/UnusedLivelyLouse.msl", 3493, 0, 224, 252, 0),
        ("datasets/sheik/replays/validation/sheik/BeautifulDistantWolverine.msl", 3787, 1, 224, 252, 0),
        ("datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl", 143, 1, 227, 255, 1),
        ("datasets/sheik/replays/validation/sheik/BeautifulDistantWolverine.msl", 362, 0, 227, 255, 1),
        ("datasets/aggregate_recent/replays/validation/marth/LoudDullGoat.msl", 5525, 0, 226, 254, 1),
        ("datasets/aggregate_recent/replays/validation/marth/MetallicUniqueGrouse.msl", 561, 0, 226, 254, 1),
        ("datasets/aggregate_recent/replays/validation/marth/MetallicUniqueGrouse.msl", 5067, 0, 226, 254, 1),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            5619,
            1,
            226,
            254,
            1,
        ),
    ],
)
def test_capturepulled_lw_phys_threshold_selects_capturewait_variant_before_owner_handoff(
    dataset_rel: str,
    record: int,
    victim: int,
    expected_action: int,
    expected_anim: int,
    expected_on_ground: int,
) -> None:
    # CapturePulledLw source ordering:
    # - The victim Phys callback applies fn_800DAD18 and compares the vertical carry against
    #   p_ftCommonData->x3C4 * fp->x34_scale.y before the owner CatchPull/CatchDashPull
    #   CatchWait callback selects CaptureWaitHi/Lw through fn_800DB6C8.
    # - Fresh same-floor CapturePulledLw rows are still action-entry-owned; they can sparse-miss a
    #   compact replay-visible floor probe but must not run the victim Phys air handoff early.
    #   Fresh dash-pull cross-floor LandingFallSpecial rows can already be floor-loss-owned because
    #   the catcher/victim floor publications disagree; ordinary Landing/DownBound fresh entries
    #   remain same-frame Lw.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledLw_Phys,fn_800DB230_inline,fn_800DB6C8}
    # data/common/ft_common_data.json::capture_pulled_lw_air_delta_y
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel

    seed, out, ref = _step_one_dataset_row(dataset_path, record)
    assert int(seed["action_id"][victim]) == 226
    assert int(ref["action_id"][victim]) == expected_action
    assert int(ref["animation_index"][victim]) == expected_anim
    assert int(ref["on_ground"][victim]) == expected_on_ground

    assert int(out["action_id"][victim]) == expected_action
    assert int(out["animation_index"][victim]) == expected_anim
    assert int(out["on_ground"][victim]) == expected_on_ground
    assert int(out["jumps_left"][victim]) == int(ref["jumps_left"][victim])
    assert float(out["pos_x"][victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=2e-5)
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=3e-6)


@pytest.mark.integration
def test_capturepulled_lw_fresh_cross_floor_entry_selects_hi_before_lw_anchor() -> None:
    # Fresh dash-pull cross-floor LandingFallSpecial -> CapturePulledLw rows can already be
    # floor-loss-owned before the Lw Phys anchor. Lock only the discrete CapturePulledHi owner here:
    # this row carries an unrelated pre-existing x-position float residual in both HEAD and this
    # packet.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   fn_800DB230,fn_800DAC78,ftCo_CapturePulledLw_Phys}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/aggregate_recent/replays/validation/marth/InternalPowerlessWallaby.msl"

    seed, out, ref = _step_one_dataset_row(dataset_path, 7312)
    victim = 0
    owner = int(seed["grab_owner_port"][victim])
    assert int(seed["action_id"][victim]) == 226
    assert int(seed["seed_prev_action_id"][victim]) != 226
    assert int(seed["ground_id"][victim]) != int(seed["ground_id"][owner])

    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id", "jumps_left"):
        assert int(out[field][victim]) == int(ref[field][victim]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "victim"),
    [
        ("datasets/aggregate_recent/replays/validation/marth/DraftyHealthyHare.msl", 9358, 1),
        ("datasets/aggregate_recent/replays/validation/marth/ExtraLargeScaryHornet.msl", 9699, 0),
    ],
)
def test_capturepulled_lw_fresh_dash_pull_non_landingfallspecial_stays_lw(
    dataset_rel: str, record: int, victim: int
) -> None:
    # Adjacent negative for the dash-pull cross-floor handoff: ordinary Landing/DownBound fresh
    # CapturePulledLw entries remain entry-owned Lw. These rows carry unrelated attachment-position
    # float residuals, so lock only the discrete owner.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel

    seed, out, ref = _step_one_dataset_row(dataset_path, record)
    owner = int(seed["grab_owner_port"][victim])
    assert int(seed["action_id"][victim]) == 226
    assert int(seed["seed_prev_action_id"][victim]) != 43
    assert int(seed["action_id"][owner]) == 215

    for field in ("action_id", "animation_index", "on_ground", "ground_id", "jumps_left"):
        assert int(out[field][victim]) == int(ref[field][victim]), field


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
    seed = np.zeros((8,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
    seed["num_players"] = np.uint8(2)
    seed["char_id"][:, :2] = np.uint8([1, 2])
    seed["stocks"][:, :2] = np.uint8([4, 4])
    seed["on_ground"][:, :2] = np.uint8([1, 1])
    seed["ground_id"][:, :2] = np.uint16([4, 4])
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
    prev_input = np.zeros((8, input_stride), dtype=np.uint8)
    cur_input = np.zeros((8, input_stride), dtype=np.uint8)
    prev_view = prev_input.view(INPUT_DTYPE).reshape((8,))
    cur_view = cur_input.view(INPUT_DTYPE).reshape((8,))

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
    # Case 6: direct stick-up fn_800DC044 is read by the Anim breakout owner itself before the
    # current input callback, so the pre-input stick lane controls the branch.
    prev_view["p"]["main_y"][6, victim_p] = np.int8(80)
    # Case 7: a current-only up-stick belongs to the later input/IASA phase and must not convert an
    # already pending Anim-owned breakout to CaptureJump.
    cur_view["p"]["main_y"][7, victim_p] = np.int8(80)

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
    assert int(out["on_ground"][6, victim_p]) == 0
    assert int(out["ground_id"][6, victim_p]) == 4
    assert int(out["action_id"][7, owner_p]) == 218  # CatchCut
    assert int(out["action_id"][7, victim_p]) == 229  # CaptureCut


@pytest.mark.integration
def test_capturewait_breakout_uses_pre_input_stick_and_carries_floor_wss() -> None:
    # Replay-real lock for the deferred CaptureWait breakout scheduler split:
    # - The source branch is in CaptureWait*_Anim, before Fighter_procUpdate input handling, so the
    #   direct stick-up check (`fn_800DC044`) reads pre-input `fp->input.lstick`.
    # - `fn_800DC070` calls ftCommon_8007D5D4, which flips ground_or_air to Air for CaptureJump but
    #   does not clear the carried CollData floor id.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,fn_800DC044,fn_800DC070}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/marth/replays/validation/marth/WellWornSmallGoshawk.msl"

    ds = read_dataset(str(ds_path))
    for record, seed_action in ((5162, 227), (5303, 224)):
        row = ds.samples[record]
        seed, ref, out = _run_one_step_row(ds_path, record, 0)
        assert int(seed["action_id"][0]) == seed_action
        assert int(seed["capture_breakout_pending_u8"][0]) == 1
        assert int(seed["capture_wait_jump_latch_u8"][0]) == 0
        if record == 5162:
            assert int(row["prev_input_t"]["p"]["main_y"][0]) > int(row["input_t"]["p"]["main_y"][0])
        assert int(ref["action_id"][0]) == 230  # CaptureJump
        assert int(out["action_id"][0]) == int(ref["action_id"][0])
        assert int(out["animation_index"][0]) == int(ref["animation_index"][0])
        assert int(out["action_frame"][0]) == int(ref["action_frame"][0])
        assert int(out["on_ground"][0]) == int(ref["on_ground"][0]) == 0
        assert int(out["ground_id"][0]) == int(ref["ground_id"][0])
        assert int(out["jumps_left"][0]) == int(ref["jumps_left"][0])
        np.testing.assert_allclose(float(out["pos_x"][0]), float(ref["pos_x"][0]), atol=1e-6)
        np.testing.assert_allclose(float(out["pos_y"][0]), float(ref["pos_y"][0]), atol=1e-6)
        np.testing.assert_allclose(
            float(out["speed_air_x_self"][0]), float(ref["speed_air_x_self"][0]), atol=1e-6
        )
        np.testing.assert_allclose(float(out["speed_y_self"][0]), float(ref["speed_y_self"][0]), atol=1e-6)


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


@pytest.mark.parametrize(
    ("dataset_name", "record", "player"),
    [
        ("QuestionableHarmfulPanther.msl", 3110, 0),
        ("WellWornSmallGoshawk.msl", 5180, 0),
        ("WellWornSmallGoshawk.msl", 5322, 0),
    ],
)
def test_capturejump_floor_contact_uses_aircatchhit_basic_landing(
    dataset_name: str, record: int, player: int
) -> None:
    # CaptureJump has its own Phys/Coll owner:
    # - Phys calls ftCommon_Fall + ftCommon_8007D268, so gravity and common air drift apply before
    #   integration without inheriting ft_80084DB0's fastfall latch.
    # - Coll calls ftCo_AirCatchHit_Coll, which routes floor contact through ft_80082B1C into
    #   Landing/Wait. The MSLMSO01 FT80082B1C_BASIC_LANDING_COLL class owns this runtime predicate.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureJump_Phys,ftCo_CaptureJump_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ftCo_AirCatchHit_Coll,ft_80082B1C}
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/marth/replays/validation/marth" / dataset_name
    seed, ref, out = _run_one_step_row(ds_path, record, player)

    assert int(seed["action_id"][player]) == 230  # CaptureJump
    assert int(ref["action_id"][player]) == 42  # Landing
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["animation_index"][player]) == int(ref["animation_index"][player])
    assert int(out["action_frame"][player]) == int(ref["action_frame"][player])
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player])
    assert int(out["instance_id"][player]) == int(ref["instance_id"][player])
    np.testing.assert_allclose(float(out["pos_x"][player]), float(ref["pos_x"][player]), atol=1e-6)
    np.testing.assert_allclose(float(out["pos_y"][player]), float(ref["pos_y"][player]), atol=1e-6)
    np.testing.assert_allclose(
        float(out["speed_air_x_self"][player]), float(ref["speed_air_x_self"][player]), atol=1e-6
    )
    np.testing.assert_allclose(
        float(out["speed_ground_x_self"][player]),
        float(ref["speed_ground_x_self"][player]),
        atol=1e-6,
    )
    np.testing.assert_allclose(
        float(out["speed_y_self"][player]), float(ref["speed_y_self"][player]), atol=1e-6
    )


def test_capturewait_and_pulled_do_not_inherit_capturejump_landing_callback() -> None:
    # Negative control for the generated owner: CapturePulled*/CaptureWait* have separate captured
    # victim collision callbacks and must not become basic Landing merely because their root crosses
    # the floor. The ft_80082B1C AirCatchHit path is specific here to CaptureJump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledHi_Coll,ftCo_CaptureWaitHi_Coll,ftCo_CaptureJump_Coll}
    seed = np.zeros((3,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
    seed["num_players"] = np.uint8(2)
    seed["char_id"][:, :2] = np.uint8([1, 2])
    seed["stocks"][:, :2] = np.uint8([4, 4])
    seed["facing"][:, :2] = np.uint8([1, 1])
    seed["facing_dir1"][:, :2] = np.int8([1, 1])
    seed["jumps_left"][:, :2] = np.uint8([2, 2])
    seed["frame_speed_mul_f32"][:, :2] = np.float32(1.0)
    seed["fighter_scale_y"][:, :2] = np.float32(1.0)
    seed["grab_owner_port"][:, 0] = np.uint8(1)

    p = 0
    seed["action_id"][:, p] = np.uint16([223, 224, 227])  # PulledHi/WaitHi/WaitLw
    seed["animation_index"][:, p] = np.uint32([253, 254, 255])
    seed["action_frame"][:, p] = np.int16([5, 5, 5])
    seed["anim_frame_f32"][:, p] = np.float32([5.0, 5.0, 5.0])
    seed["on_ground"][:, p] = np.uint8(0)
    seed["ground_id"][:, p] = np.uint16(0xFFFF)
    seed["pos_x"][:, p] = np.float32(0.0)
    seed["pos_y"][:, p] = np.float32(-2.0)
    seed["speed_y_self"][:, p] = np.float32(-2.0)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_input = np.zeros((3, input_stride), dtype=np.uint8)
    cur_input = np.zeros((3, input_stride), dtype=np.uint8)

    out = _step(seed, prev_input, cur_input, num_players=2)
    assert int(out["action_id"][0, p]) != 42
    assert int(out["action_id"][1, p]) != 42
    assert int(out["action_id"][2, p]) != 42


@pytest.mark.integration
def test_fallspecial_keeps_landingfallspecial_despite_basic_landing_callback_class() -> None:
    # Regression control for the CaptureJump owner admission gate:
    # FallSpecial also carries the generated FT80082B1C_BASIC_LANDING_COLL class, but the common
    # air-locomotion fallback has a source-owned override to LandingFallSpecial. The CaptureJump
    # non-locomotion admission must not steal FallSpecial into basic Landing.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_800831CC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"

    seed, ref, out = _run_one_step_row(
        ds_path,
        2648,
        0,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    assert int(seed["action_id"][0]) == 35  # FallSpecial.
    assert int(ref["action_id"][0]) == 43  # LandingFallSpecial.
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0])
    assert int(out["on_ground"][0]) == int(ref["on_ground"][0])


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
    if not replay_dataset_available(modeled_path) or not replay_dataset_available(blocker_path):
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
        seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, victim_p)
        assert int(seed_row["action_id"][victim_p]) == 227  # CaptureWaitLw
        assert int(seed_row["action_frame"][victim_p]) == 1
        assert int(ref_row["action_id"][victim_p]) == 227
        assert int(ref_row["action_frame"][victim_p]) == want_frame
        assert int(out_row["action_id"][victim_p]) == 227
        assert int(out_row["action_frame"][victim_p]) == want_frame


@pytest.mark.integration
def test_capturewait_expired_anim_rate_timer_resets_stale_replay_speed() -> None:
    # Teacher-forced CaptureWait seed reconstruction:
    # - If x2344 seeds as 0, ftCo_CaptureWaitHi_Anim has already expired the mash-rate window and
    #   reset frame_speed_mul to 1.0 on the source callback.
    # - The active-timer mutation remains rate-2.0 to guard the adjacent bridge above.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    root = Path(__file__).resolve().parents[1]
    cases = (
        (root / "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl", 2560, 1),
        (root / "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl", 5298, 1),
        (root / "datasets/sheik/replays/validation/sheik/SnarlingHelplessBeaver.msl", 859, 1),
    )
    for dataset_path, record, victim_p in cases:
        seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, victim_p)
        assert int(seed_row["action_id"][victim_p]) == 227  # CaptureWaitLw
        assert float(seed_row["frame_speed_mul_f32"][victim_p]) == pytest.approx(2.0)
        assert float(seed_row["capture_wait_anim_rate_timer_f32"][victim_p]) == pytest.approx(0.0)
        assert int(ref_row["action_frame"][victim_p]) == int(seed_row["action_frame"][victim_p]) + 1
        assert int(out_row["action_frame"][victim_p]) == int(ref_row["action_frame"][victim_p])

    dataset_path, record, victim_p = cases[0]
    rows = read_dataset(str(dataset_path)).samples[[record]].copy()
    seed = rows["seed_t"].copy()
    seed["capture_wait_anim_rate_timer_f32"][0, victim_p] = np.float32(1.0)
    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    out = _step(
        seed,
        _input_bytes(rows, "prev_input_t", stride=input_stride),
        _input_bytes(rows, "input_t", stride=input_stride),
        num_players=2,
    )
    assert int(out["action_frame"][0, victim_p]) == int(seed["action_frame"][0, victim_p]) + 2
