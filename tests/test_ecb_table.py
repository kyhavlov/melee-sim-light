from __future__ import annotations

import math
from pathlib import Path

from tests.test_anim_pose import _pick_first_nonempty_anim, _read_header


def _pick_char_anim_from_local_anims() -> tuple[int, int, int]:
    # Use local ISO-derived anim artifacts (no replay/dataset dependency).
    buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, _joint_parts = _read_header(buf)
    msid, _frame_count, _base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    return 1, int(msid), 0


def test_ecb_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_local_anims()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        y = float(msl_binding.ecb_bottom_rel_y(ch, anim, af))
        assert math.isfinite(y)
    finally:
        msl_binding.destroy(handle)


def test_ecb_extents_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_local_anims()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        min_x, max_x, min_y, max_y = msl_binding.ecb_extents_rel(ch, anim, af)
        assert math.isfinite(float(min_x))
        assert math.isfinite(float(max_x))
        assert math.isfinite(float(min_y))
        assert math.isfinite(float(max_y))
        assert float(min_x) <= float(max_x)
        assert float(min_y) <= float(max_y)
    finally:
        msl_binding.destroy(handle)
