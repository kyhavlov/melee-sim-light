from __future__ import annotations


def test_hitlist_ring_insertion_eviction_is_deterministic() -> None:
    import numpy as np

    import msl_binding

    # Decomp: HitCapsule.victims_1 has fixed capacity 12 and uses a ring overwrite pointer (x44)
    # when full.
    # refs/melee/src/melee/lb/types.h::HitCapsule (victims_1[12], x44)
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (ring overwrite + increment)
    ring12, ids12 = msl_binding.hitlist_ring_demo(12)
    assert int(ring12) == 0
    assert np.array_equal(ids12.astype(np.uint32), np.arange(1, 13, dtype=np.uint32))

    ring13, ids13 = msl_binding.hitlist_ring_demo(13)
    assert int(ring13) == 1
    expected13 = np.array([13, *range(2, 13)], dtype=np.uint32)
    assert np.array_equal(ids13.astype(np.uint32), expected13)

    ring14, ids14 = msl_binding.hitlist_ring_demo(14)
    assert int(ring14) == 2
    expected14 = np.array([13, 14, *range(3, 13)], dtype=np.uint32)
    assert np.array_equal(ids14.astype(np.uint32), expected14)

