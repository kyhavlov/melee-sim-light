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
        assert np.array_equal(pref.attack_id, full.attack_id[:k])
        assert np.array_equal(pref.attack_instance, full.attack_instance[:k])
        assert np.array_equal(pref.stale_queue_index, full.stale_queue_index[:k])
        assert np.array_equal(pref.stale_move_id, full.stale_move_id[:k])
        assert np.array_equal(pref.stale_attack_instance, full.stale_attack_instance[:k])


def test_staling_history_accounts_hidden_guardon_guard_guardoff_default_bump() -> None:
    # GAT frames 5139->5140 expose a frozen GuardOn row followed by visible GuardOff. Vanilla can
    # run GuardOn_Anim's hidden Guard entry before GuardOn_IASA enters GuardOff, so the global
    # attack-instance counter consumes both default-move motion-state bundles before Falco's next
    # SpecialN item-spawn identity copy.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardOn_Anim,ftCo_800928CC,ftCo_80092908,ftCo_GuardOn_IASA,ftCo_80092C54}
    path = "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slp"
    game = _read_slippi(path, False)
    frames_all = game.frames
    assert frames_all is not None

    keep = finalized_frame_indices(frames_all.field("id").to_numpy(zero_copy_only=False))
    frames = frames_all.take(pa.array(keep))
    frame_ids = frames.field("id").to_numpy(zero_copy_only=False)
    idx_by_frame = {int(frame_id): i for i, frame_id in enumerate(frame_ids)}

    hist = derive_staling_history(frames, src_ports=[1, 2])
    assert int(hist.attack_instance[idx_by_frame[5139], 0]) == 1127
    assert int(hist.attack_instance[idx_by_frame[5140], 0]) == 1132
    assert int(hist.attack_instance[idx_by_frame[5142], 1]) == 1133
