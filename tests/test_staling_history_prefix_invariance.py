from __future__ import annotations

import pyarrow as pa
import numpy as np
from peppi_py import _read_slippi

from tools.slippi.rollback import finalized_frame_indices
from tools.slippi.staling_history import derive_staling_history


def test_staling_history_prefix_invariance() -> None:
    # Strict prefix invariance: derive(prefix) == derive(full)[:k] for many k.
    path = "replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp"
    game = _read_slippi(path, False)
    frames_all = game.frames
    assert frames_all is not None

    keep = finalized_frame_indices(frames_all.field("id").to_numpy(zero_copy_only=False))
    frames = frames_all.take(pa.array(keep))
    n = int(len(frames))
    assert n > 10

    full = derive_staling_history(frames, src_ports=[1, 2])

    ks = [1, 2, 3, 10, 37, 128, n // 2, n - 1]
    ks = [k for k in ks if 1 <= k <= n]
    for k in ks:
        pref = derive_staling_history(frames.slice(0, k), src_ports=[1, 2])
        assert np.array_equal(pref.attack_instance, full.attack_instance[:k])
        assert np.array_equal(pref.stale_queue_index, full.stale_queue_index[:k])
        assert np.array_equal(pref.stale_move_id, full.stale_move_id[:k])
        assert np.array_equal(pref.stale_attack_instance, full.stale_attack_instance[:k])

