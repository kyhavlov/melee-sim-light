from __future__ import annotations

import numpy as np

from tools.slippi.rollback import finalized_frame_indices


def test_finalized_frame_indices_keeps_last_occurrence() -> None:
    # Two rollbacks on frame 11, and two rollbacks on frame 12.
    frame_ids = np.asarray([10, 11, 11, 12, 12, 12, 13], dtype=np.int32)
    keep = finalized_frame_indices(frame_ids)
    assert keep.tolist() == [0, 2, 5, 6]
    assert frame_ids[keep].tolist() == [10, 11, 12, 13]


def test_finalized_frame_indices_handles_negative_ids() -> None:
    frame_ids = np.asarray([-123, -122, -122, -121, -121], dtype=np.int32)
    keep = finalized_frame_indices(frame_ids)
    assert frame_ids[keep].tolist() == [-123, -122, -121]

