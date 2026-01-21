from __future__ import annotations

import math
from pathlib import Path

from tools.eval.dataset import read_dataset


def _pick_char_anim_from_cached_dataset() -> tuple[int, int, int]:
    # Use an already-preprocessed local dataset (no ISO extraction).
    root = Path("datasets/fox_falco_fd_ucf084_recent/replays")
    paths = sorted(root.glob("**/*.msl"))
    assert paths, f"no cached .msl datasets found under {root}"

    ds = read_dataset(str(paths[0]))
    samples = ds.samples
    n = min(int(samples.shape[0]), 4096)
    for i in range(n):
        seed = samples[i]["seed_t"]
        for p in range(2):
            ch = int(seed["char_id"][p])
            anim = int(seed["animation_index"][p])
            af = int(seed["action_frame"][p])
            if ch in (1, 22) and anim != 0xFFFFFFFF:
                return ch, anim, af
    raise AssertionError("failed to find a (char_id, animation_index) pair in cached dataset samples")


def test_ecb_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_cached_dataset()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        y = float(msl_binding.ecb_bottom_rel_y(ch, anim, af))
        assert math.isfinite(y)
    finally:
        msl_binding.destroy(handle)


def test_ecb_extents_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_cached_dataset()

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
