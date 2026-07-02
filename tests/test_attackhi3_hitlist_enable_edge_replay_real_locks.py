from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_damagefly_right_wall_projection_replay_real_locks import _rollout_rows
from tests.replay_buffers_loader import load_replay_buffers


_PPA = Path("replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz")

ACT_ATTACK_HI3 = 56
ACT_DAMAGE_FLY_LW = 89
ACT_DAMAGE_FLY_TOP = 90


@pytest.mark.integration
def test_attackhi3_enable_edge_clears_stale_dense_hitlist_for_damagefly_rehit() -> None:
    # PPA 567 starts inside Falco AttackHi3 after a no-hitbox gap. The legacy dense hitlist seed can
    # still contain an older same-group victim latch, but decomp HitCapsule ownership clears/copies
    # the concrete victims_1 list on the new hitbox enable edge. Starting rollout from 567 must
    # therefore relaunch Fox at 569 instead of suppressing the new hit. The 568 control also locks
    # that active-hitstun matrix-only BODY geometry does not relaunch one frame before the concrete
    # baseline overlap appears.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_CopyHitCapsule}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _PPA
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed_567 = ds.rows[567]["seed_t"]
    assert int(seed_567["action_id"][0]) == ACT_DAMAGE_FLY_LW
    assert int(seed_567["action_id"][1]) == ACT_ATTACK_HI3
    assert int(seed_567["combat_hitlist_cd"][1, 0, 0]) == 0

    rows = _rollout_rows(dataset_path, 567, 570)
    out_568, ref_568, _contacts_568 = rows[568]
    out_569, ref_569, _contacts = rows[569]

    assert int(ref_568["action_id"][0]) == ACT_DAMAGE_FLY_LW
    assert int(out_568["action_id"][0]) == ACT_DAMAGE_FLY_LW
    assert int(out_568["hitlag"][0]) == int(ref_568["hitlag"][0]) == 0
    assert float(out_568["percent"][0]) == pytest.approx(float(ref_568["percent"][0]), abs=1e-6)

    assert int(ref_569["action_id"][0]) == ACT_DAMAGE_FLY_TOP
    assert int(out_569["action_id"][0]) == ACT_DAMAGE_FLY_TOP
    assert int(out_569["hitlag"][0]) == int(ref_569["hitlag"][0]) == 6
    assert int(out_569["hitlag"][1]) == int(ref_569["hitlag"][1]) == 6
    assert float(out_569["percent"][0]) == pytest.approx(float(ref_569["percent"][0]), abs=1e-6)
