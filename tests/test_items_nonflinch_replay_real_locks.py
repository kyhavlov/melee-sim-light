from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


def _load_laser_state_terms_by_item_type(
    root: Path,
    *,
    item_type: int,
    state: int,
) -> tuple[int, int, int, int]:
    # MSLLASR1 v4..v7 layout owned by tools/extraction/extract_lasers.py.
    # Docs: agent_docs/DATA_CONTRACT.md
    path = root / "data" / "items" / "lasers.bin"
    buf = path.read_bytes()
    assert buf[:8] == b"MSLLASR1"
    version = struct.unpack_from("<I", buf, 8)[0]
    assert version in (4, 5, 6, 7)
    count = struct.unpack_from("<H", buf, 12)[0]
    off = 16
    rec_bytes = 226 if version >= 7 else (218 if version >= 6 else 254)
    state0_off = 42 if version >= 6 else 78
    state1_off = 138 if version >= 7 else state0_off + 24 + 16 * 4
    for _ in range(int(count)):
        shot_itkind = struct.unpack_from("<H", buf, off + 2)[0]
        if int(shot_itkind) == int(item_type):
            state_off = state0_off if state == 0 else state1_off
            kbg = struct.unpack_from("<H", buf, off + state_off + 10)[0]
            wsk = struct.unpack_from("<H", buf, off + state_off + 12)[0]
            bkb = struct.unpack_from("<H", buf, off + state_off + 14)[0]
            # `laser_zero_kb_damage_class` is +18 within each state hitbox block.
            zero_kb_damage_class = int(buf[off + state_off + 18])
            return int(kbg), int(wsk), int(bkb), int(zero_kb_damage_class)
        off += rec_bytes
    raise AssertionError(f"item_type={item_type} not found in data/items/lasers.bin")


def test_msllasr1_zero_kb_damage_class_matches_supported_laser_article_kb_terms() -> None:
    root = Path(__file__).resolve().parents[1]

    # Source owner:
    # - article scripts write kbg/wsk/bkb into HitCapsule.x24/x28/x2C;
    # - ftColl_80077C60 writes item BODY percent-temp;
    # - Fighter_ProcessHit_8006D1EC enters Damage* only when applied KB is nonzero.
    # For supported Fox/Falco laser records, the all-zero KB tuple is the data-backed
    # percent-only/no-Damage-entry class.
    for state in (0, 1):
        assert _load_laser_state_terms_by_item_type(root, item_type=54, state=state) == (0, 0, 0, 1)

    for state in (0, 1):
        kbg, wsk, bkb, zero_kb_damage_class = _load_laser_state_terms_by_item_type(
            root,
            item_type=55,
            state=state,
        )
        assert (kbg, wsk, bkb) != (0, 0, 0)
        assert zero_kb_damage_class == 0


@pytest.mark.integration
def test_fox_zero_kb_damage_class_laser_body_hit_row_matches_replay_real() -> None:
    # Replay-real lock for extracted zero applied-KB source lane:
    # - Fox laser state0 has `laser_zero_kb_damage_class=1` in MSLLASR1 from its all-zero
    #   article HitCapsule KB terms,
    # - victim percent increases at t+1, but hitlag/hitstun/action stay zero-KB.
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
    assert _load_laser_state_terms_by_item_type(root, item_type=54, state=0) == (0, 0, 0, 1)

    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 0
    got_percent = np.float32(out["percent"][victim])
    exp_percent = np.float32(ref["percent"][victim])
    assert int(got_percent.view(np.uint32)) == int(exp_percent.view(np.uint32))
