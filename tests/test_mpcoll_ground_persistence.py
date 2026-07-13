from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.stage_metadata_helpers import fd_stage_segments

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_DAMAGE_AIR_3 = 0x0056

STAGE_BATTLEFIELD = 2
CHAR_SHEIK = 7
CHAR_FOX = 1
SM_WAIT1_0 = 2
SM_SHEIK_DAMAGE_AIR_3 = 176


def _fd_main_floor_and_right_shared_endpoint() -> tuple[int, float, float]:
    floors: list[tuple[int, float, float, float, float]] = []
    for seg in fd_stage_segments():
        if seg.get("kind") != "floor" or bool(seg.get("platform")):
            continue
        floors.append(
            (
                int(seg["i"]),
                float(seg["x0"]),
                float(seg["y0"]),
                float(seg["x1"]),
                float(seg["y1"]),
            )
        )
    assert floors

    # Main floor is the longest floor segment by X-span.
    main_i, mx0, my0, mx1, my1 = max(floors, key=lambda s: abs(float(s[3]) - float(s[1])))
    main_right = (max(mx0, mx1), my0 if mx1 >= mx0 else my1)

    # Shared endpoints are endpoints touched by >=2 floor segments; take the rightmost shared one.
    counts: dict[tuple[float, float], int] = {}
    for _i, x0, y0, x1, y1 in floors:
        counts[(x0, y0)] = counts.get((x0, y0), 0) + 1
        counts[(x1, y1)] = counts.get((x1, y1), 0) + 1
    shared = [pt for pt, n in counts.items() if n >= 2]
    assert shared
    right_x, right_y = max(shared, key=lambda pt: float(pt[0]))
    assert (right_x, right_y) == main_right

    return int(main_i), float(right_x), float(right_y)


def test_ground_id_persists_at_shared_endpoint_across_frames() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    main_i, x_shared, y_shared = _fd_main_floor_and_right_shared_endpoint()
    seed["pos_x"][0, 0] = np.float32(x_shared)
    seed["pos_y"][0, 0] = np.float32(y_shared)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(main_i)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        o0 = out.view(COMPARE_DTYPE).reshape((1,))[0]

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        o1 = out.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(o0["on_ground"][0]) == 1
        assert int(o0["ground_id"][0]) == main_i
        assert int(o1["on_ground"][0]) == 1
        assert int(o1["ground_id"][0]) == main_i
    finally:
        msl_binding.destroy(handle)


def _damage_air3_under_battlefield_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_BATTLEFIELD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_SHEIK)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)

    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_AIR_3)
    seed["action_frame"][0, 0] = np.int16(28)
    seed["animation_index"][0, 0] = np.uint32(SM_SHEIK_DAMAGE_AIR_3)
    seed["anim_frame_f32"][0, 0] = np.float32(28.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing_dir1"][0, 0] = np.int8(1)
    seed["pos_x"][0, 0] = np.float32(43.144248962402344)
    seed["pos_y"][0, 0] = np.float32(-57.66318130493164)
    seed["speed_y_self"][0, 0] = np.float32(-2.130000114440918)
    seed["speed_x_attack"][0, 0] = np.float32(-0.9547246694564819)
    seed["speed_y_attack"][0, 0] = np.float32(0.20089276134967804)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["hitstun"][0, 0] = np.uint16(4)

    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(44.09897232055664)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-55.734073638916016)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(0)
    return seed


def _step_damage_air3_under_battlefield_seed() -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _damage_air3_under_battlefield_seed()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.step_input(handle, inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_damageair3_seed_prev_floor_sweep_does_not_project_to_carried_floor() -> None:
    out = _step_damage_air3_under_battlefield_seed()

    assert int(out["action_id"][0]) == ACT_DAMAGE_AIR_3
    assert int(out["action_frame"][0]) == 29
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 5
    assert float(out["pos_x"][0]) == np.float32(42.23943328857422)
    assert float(out["pos_y"][0]) == np.float32(-59.60279083251953)
