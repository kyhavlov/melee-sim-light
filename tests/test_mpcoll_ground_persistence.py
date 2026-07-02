from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.stage_metadata_helpers import fd_stage_segments

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E


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
