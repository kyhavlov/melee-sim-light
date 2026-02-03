from __future__ import annotations

import numpy as np

from tools.slippi.hitstun import (
    _IS_HITSTUN_MASK_221C,
    hitstun_u16_from_misc_as_and_state_flags3,
    u16_from_hitstun_misc,
)


def test_hitstun_misc_as_is_gated_by_state_flags_hitstun_bit() -> None:
    # If the hitstun bit is not set, misc_as must not be interpreted as hitstun remaining.
    misc_as = np.array([1.9, 1.9, 70000.0, -5.0, 123.4], dtype=np.float32)
    sf3 = np.array([0x00, 0x02, 0x02, 0x02, 0x00], dtype=np.uint8)

    got = hitstun_u16_from_misc_as_and_state_flags3(misc_as_f32=misc_as, state_flags3_u8=sf3, n=int(misc_as.size))
    misc_u16 = u16_from_hitstun_misc(misc_as, n=int(misc_as.size))
    expected = np.where((sf3 & _IS_HITSTUN_MASK_221C) != 0, misc_u16, np.uint16(0)).astype(np.uint16)

    assert np.array_equal(got, expected)
    # Spot-check representative behavior (floor + clamp + gating).
    assert int(got[0]) == 0  # gate off
    assert int(got[1]) == 1  # floor(1.9)
    assert int(got[2]) == 65535  # clamp(70000)
    assert int(got[3]) == 0  # clamp(-5)
    assert int(got[4]) == 0  # gate off

