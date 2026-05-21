from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


def _load_laser_non_flinch_by_item_type(root: Path, *, item_type: int) -> int:
    # MSLLASR1 v4..v6 layout owned by tools/extraction/extract_lasers.py.
    # Docs: agent_docs/DATA_CONTRACT.md
    path = root / "data" / "items" / "lasers.bin"
    buf = path.read_bytes()
    assert buf[:8] == b"MSLLASR1"
    version = struct.unpack_from("<I", buf, 8)[0]
    assert version in (4, 5, 6)
    count = struct.unpack_from("<H", buf, 12)[0]
    off = 16
    rec_bytes = 218 if version >= 6 else 254
    state0_off = 42 if version >= 6 else 78
    for _ in range(int(count)):
        shot_itkind = struct.unpack_from("<H", buf, off + 2)[0]
        if int(shot_itkind) == int(item_type):
            # `laser_non_flinch` is +18 within the state0 hitbox block.
            return int(buf[off + state0_off + 18])
        off += rec_bytes
    raise AssertionError(f"item_type={item_type} not found in data/items/lasers.bin")


@pytest.mark.integration
def test_fox_non_flinch_laser_body_hit_row_matches_replay_real() -> None:
    # Replay-real lock for extracted non-flinch proxy lane:
    # - fox laser state0 has extracted `laser_non_flinch=1` in MSLLASR1 (currently KB-triplet-derived),
    # - victim percent increases at t+1, but hitlag/hitstun/action stay non-flinch.
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 197
    victim = 1
    seed, out, ref = _step_one_row(dataset_path, record)

    assert int(seed["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(seed["hitlag"][victim]) == 0
    assert int(ref["hitlag"][victim]) == 0
    assert int(seed["hitstun"][victim]) == 0
    assert int(ref["hitstun"][victim]) == 0
    assert float(ref["percent"][victim]) > float(seed["percent"][victim])

    fox_slot = -1
    for i in range(15):
        it = seed["items"][i]
        if int(it["exists"]) and int(it["type"]) == 54 and int(it["owner"]) == 0:
            fox_slot = i
            break
    assert fox_slot >= 0
    assert _load_laser_non_flinch_by_item_type(root, item_type=54) == 1

    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 0
    got_percent = np.float32(out["percent"][victim])
    exp_percent = np.float32(ref["percent"][victim])
    assert int(got_percent.view(np.uint32)) == int(exp_percent.view(np.uint32))
