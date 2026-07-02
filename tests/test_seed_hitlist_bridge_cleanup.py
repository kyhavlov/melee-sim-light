from __future__ import annotations

import numpy as np

from tools.slippi.validation_buffer_seed import (
    _seed_bridge_owner_matches_attacker,
    _seed_bridge_trim_indefinite_lanes,
)


def test_seed_bridge_fallback_triggers_only_for_2p_unmapped_owner_nonlive_iid() -> None:
    act_attack_lw4 = 0x0040
    live_iids = np.array([100, 200], dtype=np.uint16)

    assert _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=1,
        defender=0,
        defender_action=0x0058,  # DamageFlyN (> AttackLw4)
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,  # unmapped
        owner_iid=999,  # not live
        live_instance_ids=live_iids,
    )

    # Not 2p: fallback must stay off.
    assert not _seed_bridge_owner_matches_attacker(
        num_players=4,
        attacker=1,
        defender=0,
        defender_action=0x0058,
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,
        owner_iid=999,
        live_instance_ids=np.array([100, 200, 300, 400], dtype=np.uint16),
    )
    # owner iid maps to live fighter: fallback must stay off.
    assert not _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=1,
        defender=0,
        defender_action=0x0058,
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,
        owner_iid=200,
        live_instance_ids=live_iids,
    )
    # mapped ownership is always accepted even without fallback.
    assert _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=1,
        defender=0,
        defender_action=0x0019,
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=1,
        owner_iid=200,
        live_instance_ids=live_iids,
    )


def test_seed_bridge_fallback_not_in_early_common_grounded_space() -> None:
    act_attack_lw4 = 0x0040
    live_iids = np.array([100, 200], dtype=np.uint16)

    # defender_action <= AttackLw4: fallback must not trigger.
    assert not _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=1,
        defender=0,
        defender_action=act_attack_lw4,
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,
        owner_iid=999,
        live_instance_ids=live_iids,
    )
    assert not _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=1,
        defender=0,
        defender_action=0x0019,  # JumpF
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,
        owner_iid=999,
        live_instance_ids=live_iids,
    )


def test_seed_bridge_fallback_not_when_attacker_equals_defender() -> None:
    act_attack_lw4 = 0x0040
    live_iids = np.array([100, 200], dtype=np.uint16)

    assert not _seed_bridge_owner_matches_attacker(
        num_players=2,
        attacker=0,
        defender=0,
        defender_action=0x0058,  # DamageFlyN (> AttackLw4)
        act_attack_lw4=act_attack_lw4,
        last_hit_by_owner=6,  # unmapped
        owner_iid=999,  # not live
        live_instance_ids=live_iids,
    )


def test_seed_bridge_trim_indefinite_lanes_only() -> None:
    hitlist_cd = np.zeros((1, 2, 8, 2), dtype=np.uint16)
    hitlist_iid = np.zeros((1, 2, 8, 2), dtype=np.uint16)

    # attacker=1, defender=0 lanes: mix indefinite and finite cooldown values.
    hitlist_cd[0, 1, 0, 0] = np.uint16(0xFFFF)
    hitlist_iid[0, 1, 0, 0] = np.uint16(301)
    hitlist_cd[0, 1, 1, 0] = np.uint16(3)
    hitlist_iid[0, 1, 1, 0] = np.uint16(302)
    hitlist_cd[0, 1, 2, 0] = np.uint16(0xFFFF)
    hitlist_iid[0, 1, 2, 0] = np.uint16(303)

    changed = _seed_bridge_trim_indefinite_lanes(
        hitlist_cd=hitlist_cd,
        hitlist_iid=hitlist_iid,
        fi=0,
        attacker=1,
        defender=0,
    )
    assert changed

    # Indefinite lanes are cleared.
    assert int(hitlist_cd[0, 1, 0, 0]) == 0
    assert int(hitlist_iid[0, 1, 0, 0]) == 0
    assert int(hitlist_cd[0, 1, 2, 0]) == 0
    assert int(hitlist_iid[0, 1, 2, 0]) == 0

    # Finite cooldown lane is untouched.
    assert int(hitlist_cd[0, 1, 1, 0]) == 3
    assert int(hitlist_iid[0, 1, 1, 0]) == 302

    # No indefinite lanes left: helper returns False and no further mutation.
    changed_again = _seed_bridge_trim_indefinite_lanes(
        hitlist_cd=hitlist_cd,
        hitlist_iid=hitlist_iid,
        fi=0,
        attacker=1,
        defender=0,
    )
    assert not changed_again
