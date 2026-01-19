from __future__ import annotations

import numpy as np


def finalized_frame_indices(frame_ids: np.ndarray) -> np.ndarray:
    """
    Slippi rollback can include multiple snapshots for the same frame number.
    For lite-sim evaluation we want the finalized frame sequence: keep the
    *last* occurrence of each frame id.
    """
    frame_ids = np.asarray(frame_ids)
    seen: set[int] = set()
    keep_rev: list[int] = []
    for i in range(len(frame_ids) - 1, -1, -1):
        fid = int(frame_ids[i])
        if fid in seen:
            continue
        seen.add(fid)
        keep_rev.append(i)
    keep_rev.reverse()
    return np.asarray(keep_rev, dtype=np.int32)

