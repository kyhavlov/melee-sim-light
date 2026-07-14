from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.stage_metadata_helpers import fd_stage_segments

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_DAMAGE_FALL = 0x0026
ACT_CLIFF_CATCH = 0x00FC
ACT_CLIFF_WAIT = 0x00FD
ACT_FX_SPECIAL_HI_FALL = 0x0166
ACT_PASS = 0x00F4

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_FALL = 20
SM_DAMAGE_FALL = 29
SM_FX_SPECIAL_HI_FALL = 311
SM_PASS = 209

STAGE_FD = 32
CHAR_FOX = 1


def _fd_ledge_points() -> tuple[tuple[float, float], tuple[float, float]]:
    left: tuple[float, float] | None = None
    right: tuple[float, float] | None = None
    best_left_x = float("inf")
    best_right_x = float("-inf")

    for seg in fd_stage_segments():
        if seg.get("kind") != "floor" or bool(seg.get("platform")):
            continue
        if not bool(seg.get("ledge")):
            continue
        x0 = float(seg["x0"])
        y0 = float(seg["y0"])
        x1 = float(seg["x1"])
        y1 = float(seg["y1"])
        if x0 < best_left_x:
            best_left_x = x0
            left = (x0, y0)
        if x1 < best_left_x:
            best_left_x = x1
            left = (x1, y1)
        if x0 > best_right_x:
            best_right_x = x0
            right = (x0, y0)
        if x1 > best_right_x:
            best_right_x = x1
            right = (x1, y1)

    assert left is not None and right is not None
    return left, right


def _fox_ledge_params() -> tuple[float, float, float]:
    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox["ledge_snap_x"]), float(fox["ledge_snap_y"]), float(fox["ledge_snap_height"])


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    # P2 stable grounded idle.
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(0)

    return seed


def _step_once(seed: np.ndarray) -> np.ndarray:
    return _step_once_with_inputs(seed)


def _step_once_with_inputs(seed: np.ndarray, *, prev_inp: np.ndarray | None = None, inp: np.ndarray | None = None) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        if prev_inp is None:
            prev_inp = np.zeros((1,), dtype=INPUT_DTYPE)
        if inp is None:
            inp = np.zeros((1,), dtype=INPUT_DTYPE)
        prev_inp_bytes = prev_inp.view(np.uint8).reshape((1, input_stride))
        inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp_bytes, inp_bytes)
        msl_binding.write_compare(handle, out)

        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

def test_ledge_catch_requires_facing_and_outside() -> None:
    (lx, ly), _ = _fd_ledge_points()
    _snap_x, snap_y, _snap_h = _fox_ledge_params()

    # Choose a point just offstage to the left of the ledge.
    x_offstage = float(lx - 1.0)
    # Deterministic: mpColl_80044164 uses (pos_y + snap_y) in its vertical AABB.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
    y0 = float(ly - snap_y)

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["pos_y"][0, 0] = np.float32(y0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    # Facing into stage: should catch.
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x_offstage)
    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_CLIFF_CATCH

    # Facing away: should not catch (mpColl side check is facing-dependent).
    seed["facing"][0, 0] = np.uint8(0)
    out = _step_once(seed)
    assert int(out["action_id"][0]) != ACT_CLIFF_CATCH

    # Inside/onstage side (x > ledge_x): should not catch.
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(lx + 1.0)
    out = _step_once(seed)
    assert int(out["action_id"][0]) != ACT_CLIFF_CATCH


def test_damagefall_8370c_uses_full_ledge_snap_height() -> None:
    # DamageFall_Coll uses ft_8008370C -> mpColl_800473CC directly. Unlike the DamageFly family's
    # ft_80081DD4 wrapper, it does not temporarily multiply ledge_snap_height by x1CC. Place Fox in
    # the narrow vertical band admitted by the full initialized height but rejected by the scaled
    # DamageFly height, then retain an adjacent just-above rejection.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_8008370C,ft_80081DD4}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
    (lx, ly), _ = _fd_ledge_points()
    _, snap_y, _ = _fox_ledge_params()
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FALL)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(lx - 1.0)
    seed["speed_y_self"][0, 0] = np.float32(-3.1)

    # The terminal self velocity moves the root by -3.1 before map collision. An offset of 4.0
    # remains inside the full-height AABB; 4.6 is immediately outside it.
    seed["pos_y"][0, 0] = np.float32(ly - snap_y + 4.0 + 3.1)
    assert int(_step_once(seed)["action_id"][0]) == ACT_CLIFF_CATCH

    seed["pos_y"][0, 0] = np.float32(ly - snap_y + 4.6 + 3.1)
    assert int(_step_once(seed)["action_id"][0]) != ACT_CLIFF_CATCH


def test_specialhi_fall_cliffcatch_both_can_grab_facing_away() -> None:
    # Fox/Falco SpecialHiFall passes CLIFFCATCH_BOTH to ft_CheckGroundAndLedge, so the same source
    # ledge box that rejects ordinary Fall while facing away admits this callback owner.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    (lx, ly), _ = _fd_ledge_points()
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_HI_FALL)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FX_SPECIAL_HI_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["facing"][0, 0] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(lx - 1.0)
    seed["pos_y"][0, 0] = np.float32(ly - 10.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_CLIFF_CATCH


def test_pass_cliffcatch_respects_down_release_gate() -> None:
    # Pass_Coll runs ft_80082F28's common cliff-catch owner before Pass animates into Fall.
    # Releasing down admits the ledge; continuing to hold down is rejected by ftCliffCommon.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082F28
    (lx, ly), _ = _fd_ledge_points()
    _, snap_y, _ = _fox_ledge_params()
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_PASS)
    seed["animation_index"][0, 0] = np.uint32(SM_PASS)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(lx - 1.0)
    seed["pos_y"][0, 0] = np.float32(ly - snap_y)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    assert int(_step_once(seed)["action_id"][0]) == ACT_CLIFF_CATCH
    held_down = np.zeros((1,), dtype=INPUT_DTYPE)
    held_down["p"]["main_y"][0, 0] = np.int8(-80)
    assert int(_step_once_with_inputs(seed, inp=held_down)["action_id"][0]) != ACT_CLIFF_CATCH

def test_ledge_catch_disabled_by_hold_down() -> None:
    (lx, ly), _ = _fd_ledge_points()
    _, snap_y, _ = _fox_ledge_params()

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["facing"][0, 0] = np.uint8(1)

    seed["pos_x"][0, 0] = np.float32(float(lx - 1.0))
    seed["pos_y"][0, 0] = np.float32(float(ly - snap_y))
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    # Hold down: ftCliffCommon_80081298 disables cliff catch when lstick.y <= -x480.
    inp["p"]["main_y"][0, 0] = np.int8(-127)

    out = _step_once_with_inputs(seed, inp=inp)
    assert int(out["action_id"][0]) != ACT_CLIFF_CATCH


def test_ledge_catch_blocked_when_ledge_occupied() -> None:
    (lx, ly), _ = _fd_ledge_points()
    _snap_x, snap_y, _snap_h = _fox_ledge_params()

    seed = _seed_base()

    # P1: falling offstage attempting to catch left ledge.
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(float(lx - 1.0))
    seed["pos_y"][0, 0] = np.float32(float(ly - snap_y))
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    # P2: already holding left ledge (occupancy blocks catch).
    seed["action_id"][0, 1] = np.uint16(ACT_CLIFF_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["on_ground"][0, 1] = np.uint8(0)
    seed["ground_id"][0, 1] = np.uint16(0)
    seed["facing"][0, 1] = np.uint8(1)
    seed["pos_x"][0, 1] = np.float32(float(lx))
    seed["pos_y"][0, 1] = np.float32(float(ly))

    out = _step_once(seed)
    assert int(out["action_id"][0]) != ACT_CLIFF_CATCH


def test_ledge_catch_hot_path_no_allocations() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    (lx, ly), _ = _fd_ledge_points()
    _snap_x, snap_y, _snap_h = _fox_ledge_params()
    x_offstage = float(lx - 1.0)
    y0 = float(ly - snap_y)

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x_offstage)
    seed["pos_y"][0, 0] = np.float32(y0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)

        msl_binding.alloc_reset()
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
    finally:
        msl_binding.destroy(handle)
