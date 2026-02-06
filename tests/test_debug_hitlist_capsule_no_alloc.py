from __future__ import annotations

import pytest


def test_debug_hitlist_fighter_capsule_no_alloc_after_init() -> None:
    binding = pytest.importorskip("msl_binding")

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.alloc_reset()
        _capsule = binding.debug_hitlist_fighter_capsule(handle, 0, 0, 0)
        stats = binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
    finally:
        binding.destroy(handle)

