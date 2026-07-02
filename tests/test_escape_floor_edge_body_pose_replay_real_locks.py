from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


@pytest.mark.integration
def test_escape_roll_floor_edge_pose_feeds_body_hit_poy_4476() -> None:
    # Combat geometry / map-collision ordering lock:
    # - The defender is grounded EscapeB at the FD floor edge.
    # - Vanilla ftCo_Escape_Coll routes through ft_80084104, which uses ft_800827A0 /
    #   mpColl_8004B2DC and can keep the roll floor-owned before ftColl_80078C70 samples hurtcaps.
    # - The BODY hit must therefore select the EscapeB pose and enter DamageN3, not sample a
    #   premature Fall pose and enter DamageAir3 / wrong low hurtcap.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800827A0}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/PutridJoyousOryx.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 4476)
    attacker = 0
    defender = 1

    assert int(seed["action_id"][attacker]) == 50  # AttackDash
    assert int(seed["action_id"][defender]) == 234  # EscapeB
    assert int(ref["action_id"][defender]) == 80  # DamageN3

    for p in (attacker, defender):
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "hitlag", "hitstun"):
            assert int(out[field][p]) == int(ref[field][p]), f"p={p} field={field}"

    for field in ("instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert (out["state_flags"][defender] == ref["state_flags"][defender]).all()
