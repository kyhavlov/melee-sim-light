from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing, _step_one_row


@pytest.mark.integration
def test_cliffattackquick_hitbox_extraction_body_hit_bhh_3661() -> None:
    # CliffAttackSlow/Quick are common motion states with real HitCapsule events for both Fox and
    # Falco. BHH:3661 protects the positive body-hit side of extracting those states instead of
    # leaving ledge attacks hitbox-empty.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::{
    #   ftCo_CliffAttack_Anim,ftCo_CliffAttack_Coll}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 3661)
    defender = 0
    assert int(ref["action_id"][defender]) == 88  # DamageFlyN
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "last_hit_by", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
def test_cliffattackquick_no_damage_contact_preserves_hitcapsule_lineage_gat_8224() -> None:
    # Negative sentinel for the same owner: Falco CliffAttackQuick data must stay extracted, but an
    # invincible/no-damage BODY contact still writes HitCapsule.victims_1 in vanilla before the
    # vulnerable damage guard. Preprocessing preserves that hidden lineage causally from current-row
    # hitlag/hurtbox state; runtime still uses ordinary hitlist suppression, not replay BODY
    # admission.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB0,inlineB2}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 8224)
    for p in (0, 1):
        for field in ("action_id", "animation_index", "hitlag", "hitstun", "last_hit_by", "instance_hit_by"):
            assert int(out[field][p]) == int(ref[field][p]), f"p={p} field={field}"
        assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6)
