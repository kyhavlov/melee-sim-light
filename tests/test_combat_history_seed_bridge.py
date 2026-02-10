from tools.slippi.combat_history import _should_prune_guard_stale_seed_bridge


def test_should_prune_guard_stale_seed_bridge_true_on_guard_create_order_with_attribution_mismatch() -> None:
    assert _should_prune_guard_stale_seed_bridge(
        defender_action_id=0x00B3,  # Guard
        defender_hitlag=0,
        defender_last_hit_by=1,
        defender_instance_hit_by=441,
        attacker_port=1,
        attacker_instance_id=460,
        hitbox_def_frame=8,
        attacker_action_frame=8,
    )


def test_should_prune_guard_stale_seed_bridge_false_when_not_guard() -> None:
    assert not _should_prune_guard_stale_seed_bridge(
        defender_action_id=0x00B5,  # GuardSetOff
        defender_hitlag=0,
        defender_last_hit_by=1,
        defender_instance_hit_by=441,
        attacker_port=1,
        attacker_instance_id=460,
        hitbox_def_frame=8,
        attacker_action_frame=8,
    )


def test_should_prune_guard_stale_seed_bridge_false_when_not_create_order_frame() -> None:
    assert not _should_prune_guard_stale_seed_bridge(
        defender_action_id=0x00B3,  # Guard
        defender_hitlag=0,
        defender_last_hit_by=1,
        defender_instance_hit_by=441,
        attacker_port=1,
        attacker_instance_id=460,
        hitbox_def_frame=8,
        attacker_action_frame=9,
    )


def test_should_prune_guard_stale_seed_bridge_false_when_instance_attribution_matches() -> None:
    assert not _should_prune_guard_stale_seed_bridge(
        defender_action_id=0x00B3,  # Guard
        defender_hitlag=0,
        defender_last_hit_by=1,
        defender_instance_hit_by=460,
        attacker_port=1,
        attacker_instance_id=460,
        hitbox_def_frame=8,
        attacker_action_frame=8,
    )
