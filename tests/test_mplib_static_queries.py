from __future__ import annotations

import struct
from pathlib import Path

import pytest

from tools.extraction.known_data_artifacts import read_mslstg01_v7

CHECK_FLOOR = 1
CHECK_CEILING = 2
CHECK_LEFT_WALL = 4
CHECK_RIGHT_WALL = 8


def _line_mid(seg) -> tuple[float, float]:
    return ((float(seg.x0) + float(seg.x1)) * 0.5, (float(seg.y0) + float(seg.y1)) * 0.5)


def _kind_lines(stage_bin: str, kind_id: int):
    stage = read_mslstg01_v7(Path("data/stages/bin") / stage_bin)
    return [seg for seg in stage.segments if int(seg.kind_id) == kind_id and bool(seg.fighter_solid)]


def _f32_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def test_static_floor_queries_cover_legal_stage_static_lines() -> None:
    import msl_binding

    cases = {
        32: ("grnla.bin", 1),  # FD main floor
        31: ("grnba.bin", 2),  # Battlefield static platform
        2: ("griz.bin", 2),  # FoD static top platform; height platforms are excluded below
        3: ("grps.bin", 35),  # frozen Stadium static platform top
        8: ("grst.bin", 2),  # Yoshi left sloped ledge floor
        28: ("grop.bin", 0),  # Dream Land static platform
    }
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        for stage_id, (bin_name, line_id) in cases.items():
            seg = next(s for s in read_mslstg01_v7(Path("data/stages/bin") / bin_name).segments if int(s.line_id) == line_id)
            x, y = _line_mid(seg)
            hit = msl_binding.stage_static_query(stage_id, CHECK_FLOOR, x, y + 10.0, x, y - 10.0, 65535, -1, -1)
            assert hit is not None, (stage_id, line_id)
            assert int(hit["segment_i"]) == line_id
            assert int(hit["flags"]) == int(seg.lo_flags)
            assert int(hit["joint_id"]) == int(seg.joint_id)
    finally:
        msl_binding.destroy(handle)


def test_static_floor_queries_publish_psvecnormalize_bits() -> None:
    # mpCheckFloor publishes a literal normal for its horizontal branch and calls the SDK
    # PSVECNormalize sequence for sloped lines. These bits distinguish both source paths from a
    # generic host sqrt/division implementation.
    # refs/melee/src/melee/mp/mplib.c::mpCheckFloor
    # refs/melee/build/GALE01/asm/dolphin/mtx/vec.s::PSVECNormalize
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        fd = msl_binding.stage_static_query(
            32, CHECK_FLOOR, 0.0, 10.0, 0.0, -10.0, 65535, -1, -1
        )
        yoshi = msl_binding.stage_static_query(
            8, CHECK_FLOOR, -48.0, 10.0, -48.0, -10.0, 65535, -1, -1
        )
        assert fd is not None
        assert yoshi is not None
        assert (_f32_bits(fd["normal_x"]), _f32_bits(fd["normal_y"])) == (
            0x00000000,
            0x3F800000,
        )
        assert (_f32_bits(yoshi["normal_x"]), _f32_bits(yoshi["normal_y"])) == (
            0xBE50D961,
            0x3F7A9E77,
        )
    finally:
        msl_binding.destroy(handle)


def test_static_ceiling_and_wall_direction_gates() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        for stage_id, bin_name in ((32, "grnla.bin"), (31, "grnba.bin"), (3, "grps.bin")):
            ceiling = next(
                seg for seg in _kind_lines(bin_name, 1) if abs(float(seg.y0) - float(seg.y1)) <= 0.0001
            )
            x, y = _line_mid(ceiling)
            assert msl_binding.stage_static_query(stage_id, CHECK_CEILING, x, y - 8.0, x, y + 8.0, 65535, -1, -1) is not None
            assert msl_binding.stage_static_query(stage_id, CHECK_CEILING, x, y + 8.0, x, y - 8.0, 65535, -1, -1) is None

        for stage_id, bin_name in ((32, "grnla.bin"), (3, "grps.bin"), (8, "grst.bin")):
            right_wall = next(
                seg for seg in _kind_lines(bin_name, 2) if abs(float(seg.x0) - float(seg.x1)) <= 0.0001
            )
            x, y = _line_mid(right_wall)
            assert msl_binding.stage_static_query(stage_id, CHECK_RIGHT_WALL, x + 8.0, y, x - 8.0, y, 65535, -1, -1) is not None
            assert msl_binding.stage_static_query(stage_id, CHECK_RIGHT_WALL, x - 8.0, y, x + 8.0, y, 65535, -1, -1) is None

            left_wall = next(
                seg for seg in _kind_lines(bin_name, 3) if abs(float(seg.x0) - float(seg.x1)) <= 0.0001
            )
            x, y = _line_mid(left_wall)
            assert msl_binding.stage_static_query(stage_id, CHECK_LEFT_WALL, x - 8.0, y, x + 8.0, y, 65535, -1, -1) is not None
            assert msl_binding.stage_static_query(stage_id, CHECK_LEFT_WALL, x + 8.0, y, x - 8.0, y, 65535, -1, -1) is None
    finally:
        msl_binding.destroy(handle)


def test_static_floor_strict_bounds_and_source_order_tie() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        assert msl_binding.stage_static_query(32, CHECK_FLOOR, 0.0, 10.0, 0.0, -10.0, 65535, -1, -1)["segment_i"] == 1
        assert msl_binding.stage_static_query(32, CHECK_FLOOR, -75.0, 10.0, -75.0, -10.0, 65535, -1, -1)["segment_i"] == 0
        assert msl_binding.stage_static_query(32, CHECK_FLOOR, 1000.0, 10.0, 1000.0, -10.0, 65535, -1, -1) is None
    finally:
        msl_binding.destroy(handle)


def test_static_query_rejects_degenerate_axis_sweeps() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # mpLineIntersectionH/V reject sweeps whose crossing-axis delta is near zero, even if the
        # sweep lies along the line.
        assert msl_binding.stage_static_query(32, CHECK_FLOOR, -10.0, 0.0, 10.0, 0.0, 65535, -1, -1) is None
        assert (
            msl_binding.stage_static_query(
                32, CHECK_RIGHT_WALL, 85.5656967, -2.0, 85.5656967, -8.0, 65535, -1, -1
            )
            is None
        )
    finally:
        msl_binding.destroy(handle)


def test_static_sloped_floor_direction_uses_source_side_tests() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        hit = msl_binding.stage_static_query(8, CHECK_FLOOR, -48.0, 10.0, -48.0, -10.0, 65535, -1, -1)
        assert hit is not None
        assert int(hit["segment_i"]) == 2
        assert (
            msl_binding.stage_static_query(8, CHECK_FLOOR, -48.0, -10.0, -48.0, 10.0, 65535, -1, -1)
            is None
        )
    finally:
        msl_binding.destroy(handle)


def test_static_horizontal_endpoint_tolerance_clamps_after_source_broadphase() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        hit = msl_binding.stage_static_query(31, CHECK_FLOOR, -57.5, 35.0, -57.7, 20.0, 65535, -1, -1)
        assert hit is not None
        assert int(hit["segment_i"]) == 2
        assert float(hit["x"]) == pytest.approx(-57.6000023)
        assert msl_binding.stage_static_query(31, CHECK_FLOOR, -57.5, 35.0, -58.0, 20.0, 65535, -1, -1) is None
    finally:
        msl_binding.destroy(handle)


def test_static_floor_adjacency_extension_is_link_gated() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # FD line 0 has a raw neighbor at its left endpoint, so mpLib-style endpoint extension admits
        # a near-endpoint floor sweep just outside the source segment span.
        assert (
            msl_binding.stage_static_query(
                32, CHECK_FLOOR, -86.0, 10.0, -86.0, -10.0, 65535, -1, -1
            )["segment_i"]
            == 0
        )

        # Battlefield left platform has no raw prev/next links, so the same outside-span sweep is
        # rejected rather than extended.
        assert (
            msl_binding.stage_static_query(
                31, CHECK_FLOOR, -58.0, 35.0, -58.0, 20.0, 65535, -1, -1
            )
            is None
        )
    finally:
        msl_binding.destroy(handle)


def test_static_ceiling_endpoint_extension_and_wall_source_boundary() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # FD ceiling line 4 has raw prev/next links, so mpLib_8004ED5C extends its endpoint before
        # the horizontal intersection helper clamps the hit.
        hit = msl_binding.stage_static_query(32, CHECK_CEILING, 48.0, -65.0, 48.0, -50.0, 65535, -1, -1)
        assert hit is not None
        assert int(hit["segment_i"]) == 4
        assert float(hit["x"]) == pytest.approx(48.0)

        # mpCheckLeft/RightWall do not call mpLib_8004ED5C; vertical wall endpoint extension is a
        # later wrapper concern, not part of the static wall scan.
        assert (
            msl_binding.stage_static_query(
                32, CHECK_RIGHT_WALL, 90.0, 0.05, 80.0, 0.05, 65535, -1, -1
            )
            is None
        )
    finally:
        msl_binding.destroy(handle)


def test_static_floor_wall_ceiling_skip_and_joint_filters() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        bf_platform = msl_binding.stage_floor_segment(31, 2)
        assert bf_platform is not None
        x = (float(bf_platform["x0"]) + float(bf_platform["x1"])) * 0.5
        y = float(bf_platform["y0"])
        assert msl_binding.stage_static_query(31, CHECK_FLOOR, x, y + 8.0, x, y - 8.0, 65535, -1, -1)["segment_i"] == 2
        assert msl_binding.stage_static_query(31, CHECK_FLOOR, x, y + 8.0, x, y - 8.0, 2, -1, -1) is None

        ps_main = msl_binding.stage_floor_segment(3, 34)
        assert ps_main is not None
        joint_id = int(ps_main["joint_id"])
        assert msl_binding.stage_static_query(3, CHECK_FLOOR, 0.0, 10.0, 0.0, -10.0, 65535, -1, joint_id)["segment_i"] == 34
        assert msl_binding.stage_static_query(3, CHECK_FLOOR, 0.0, 10.0, 0.0, -10.0, 65535, joint_id, -1) is None

        ps_ceiling = msl_binding.stage_static_query(3, CHECK_CEILING, -65.0, -25.0, -65.0, -10.0, 65535, -1, 6)
        assert ps_ceiling is not None
        assert int(ps_ceiling["segment_i"]) == 71
        assert msl_binding.stage_static_query(3, CHECK_CEILING, -65.0, -25.0, -65.0, -10.0, 65535, 6, -1) is None

        ps_wall = msl_binding.stage_static_query(3, CHECK_RIGHT_WALL, 90.0, -2.0, 80.0, -2.0, 65535, -1, 6)
        assert ps_wall is not None
        assert int(ps_wall["segment_i"]) == 89
        assert msl_binding.stage_static_query(3, CHECK_RIGHT_WALL, 90.0, -2.0, 80.0, -2.0, 65535, 6, -1) is None
    finally:
        msl_binding.destroy(handle)


def test_static_queries_exclude_deferred_moving_platform_lines() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # FoD side height platforms remain metadata-only for Phase 1 static queries; the static top
        # platform at the same source-local span remains queryable.
        hit = msl_binding.stage_static_query(2, CHECK_FLOOR, 0.0, 8.0, 0.0, -8.0, 65535, -1, -1)
        assert hit is not None
        assert int(hit["segment_i"]) == 2

        randall = msl_binding.stage_floor_segment(8, 1000)
        assert randall is not None
        x = (float(randall["x0"]) + float(randall["x1"])) * 0.5
        y = float(randall["y0"])
        assert msl_binding.stage_static_query(8, CHECK_FLOOR, x, y + 5.0, x, y - 5.0, 65535, -1, -1) is None
    finally:
        msl_binding.destroy(handle)


def test_static_query_helpers_do_not_allocate_after_init() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.alloc_reset()
        assert msl_binding.stage_static_query(32, CHECK_FLOOR | CHECK_LEFT_WALL | CHECK_RIGHT_WALL, 0.0, 10.0, 0.0, -10.0, 65535, -1, -1) is not None
        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
    finally:
        msl_binding.destroy(handle)
