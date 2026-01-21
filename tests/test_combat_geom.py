from __future__ import annotations

import pytest


def test_point_segment_dist2_midpoint() -> None:
    import msl_binding

    d2, t = msl_binding.debug_point_segment_dist2(
        5.0,
        3.0,
        0.0,  # P
        0.0,
        0.0,
        0.0,  # A
        10.0,
        0.0,
        0.0,  # B
    )
    assert t == pytest.approx(0.5)
    assert d2 == pytest.approx(9.0)


def test_point_segment_dist2_clamps_to_endpoints() -> None:
    import msl_binding

    d2_a, t_a = msl_binding.debug_point_segment_dist2(
        -5.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        10.0,
        0.0,
        0.0,
    )
    assert t_a == pytest.approx(0.0)
    assert d2_a == pytest.approx(25.0)

    d2_b, t_b = msl_binding.debug_point_segment_dist2(
        15.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        10.0,
        0.0,
        0.0,
    )
    assert t_b == pytest.approx(1.0)
    assert d2_b == pytest.approx(25.0)


def test_point_segment_dist2_degenerate_segment() -> None:
    import msl_binding

    d2, t = msl_binding.debug_point_segment_dist2(
        1.0,
        2.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
    )
    assert t == pytest.approx(0.0)
    assert d2 == pytest.approx(5.0)

