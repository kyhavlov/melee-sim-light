from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.stage_metadata_helpers import fd_stage_segments

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E


def _fd_floor_lines() -> list[dict]:
    out: list[dict] = []
    for seg in fd_stage_segments():
        if seg.get("kind") != "floor" or bool(seg.get("platform")):
            continue
        x0 = float(seg["x0"])
        y0 = float(seg["y0"])
        x1 = float(seg["x1"])
        y1 = float(seg["y1"])
        out.append(
            {
                "segment_i": int(seg["i"]),
                "x0": x0,
                "y0": y0,
                "x1": x1,
                "y1": y1,
                "min_x": min(x0, x1),
                "max_x": max(x0, x1),
                "span": abs(x1 - x0),
            }
        )
    assert out
    return out


def _fd_floor_pick_line_at_x(x: float) -> dict:
    lines = _fd_floor_lines()
    # Prefer the longest floor segment that contains x (decomp-shaped stable selection).
    cand = [l for l in lines if l["min_x"] <= x <= l["max_x"]]
    assert cand
    return max(cand, key=lambda l: float(l["span"]))


def _fd_floor_main_and_right_lip() -> tuple[dict, dict, float, float]:
    lines = _fd_floor_lines()
    main = max(lines, key=lambda l: float(l["span"]))
    # Shared endpoints are endpoints touched by >=2 floor segments.
    endpoints: dict[tuple[float, float], int] = {}
    for l in lines:
        for pt in ((l["x0"], l["y0"]), (l["x1"], l["y1"])):
            endpoints[pt] = endpoints.get(pt, 0) + 1
    shared = [pt for pt, n in endpoints.items() if n >= 2]
    assert shared
    # For FD, pick the right shared endpoint (largest X) and find the segment to its right.
    right_pt = max(shared, key=lambda pt: float(pt[0]))
    right_x, right_y = right_pt
    incident = [l for l in lines if (l["x0"], l["y0"]) == right_pt or (l["x1"], l["y1"]) == right_pt]
    assert len(incident) >= 2
    # Right lip is the incident segment whose span is smaller than main and lies to the right.
    right_lip = None
    for l in incident:
        if l["segment_i"] == main["segment_i"]:
            continue
        if l["max_x"] >= right_x:
            right_lip = l
            break
    assert right_lip is not None
    return main, right_lip, float(right_x), float(right_y)


def _step_once(seed: np.ndarray) -> np.ndarray:
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
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        # Avoid leaks in long-running test sessions.
        msl_binding.destroy(handle)
        del handle


def _seed_base(*, stage_id: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    # Avoid accidentally starting in a match-flow action (action_id=0 is DeadDown).
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    return seed


def test_fd_grounded_stays_grounded_on_same_line() -> None:
    seg0 = _fd_floor_pick_line_at_x(0.0)

    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.05)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(seg0["segment_i"])

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == int(seg0["segment_i"])


def test_fd_shared_endpoint_does_not_flip_ground_id() -> None:
    main, right_lip, x_boundary, y_boundary = _fd_floor_main_and_right_lip()

    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(x_boundary)
    seed["pos_y"][0, 0] = np.float32(y_boundary)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)

    seed["ground_id"][0, 0] = np.uint16(main["segment_i"])
    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == int(main["segment_i"])

    seed["ground_id"][0, 0] = np.uint16(right_lip["segment_i"])
    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == int(right_lip["segment_i"])


def test_vy_positive_never_grounds() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(1234)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 1234


def test_vy_zero_can_ground_when_on_surface() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1


def test_no_snap_from_far_below_stage() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(-10.0)
    seed["speed_y_self"][0, 0] = np.float32(-0.1)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(2222)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 2222


def test_ground_id_unchanged_when_airborne() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(5.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(3333)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 3333


def test_unsupported_stage_reseed_is_rejected() -> None:
    seed = _seed_base(stage_id=1)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4444)

    try:
        _step_once(seed)
    except RuntimeError:
        return
    raise AssertionError("unsupported stage reseed unexpectedly succeeded")
