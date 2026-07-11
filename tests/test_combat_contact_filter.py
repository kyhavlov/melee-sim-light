from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10
HIT_CLANK = 1 << 14
X42_FIGHTER_INTERACTION = 1 << 0
X42_ITEM_INTERACTION = 1 << 1
X42_INTERACTION_VALID = 1 << 2

ACT_WAIT = 0x000E
SM_WAIT1_0 = 2
CHAR_FOX = 1


def _make_handle(*, defender_on_ground: int):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )

    seed = np.zeros((1, seed_stride), dtype=np.uint8)
    seed_view = seed.view(SEED_DTYPE).reshape(-1)
    seed_view["stage_id"][0] = np.uint32(32)
    seed_view["num_players"][0] = np.uint8(2)
    seed_view["stocks"][0, :2] = np.uint8(4)
    seed_view["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed_view["instance_id"][0, 0] = np.uint16(111)
    seed_view["instance_id"][0, 1] = np.uint16(222)
    seed_view["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed_view["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed_view["attack_ratio"][0, :2] = np.float32(1.0)
    seed_view["defense_ratio"][0, :2] = np.float32(1.0)
    seed_view["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed_view["on_ground"][0, 0] = np.uint8(1)
    seed_view["on_ground"][0, 1] = np.uint8(1 if defender_on_ground else 0)

    msl_binding.reseed_seed(handle, seed)
    return handle


def _read_compare(handle) -> np.void:
    import msl_binding

    compare_stride = int(msl_binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0]


def _force_single_overlap(*, handle, hitbox_flags: int):
    import msl_binding

    # Attacker: P0 hitbox 0 at origin.
    msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
    msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
    msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(hitbox_flags))

    # Defender: P1 single capsule centered at origin.
    msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
    msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)


@pytest.mark.parametrize(
    ("defender_on_ground", "hitbox_flags"),
    [
        (1, HIT_AERIAL),  # grounded victim requires HIT_GROUNDED
        (0, HIT_GROUNDED),  # airborne victim requires HIT_AERIAL
    ],
)
def test_debug_combat_contacts_filtered_skips_ground_air_mismatch(
    defender_on_ground: int, hitbox_flags: int
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=defender_on_ground)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=hitbox_flags)

        _, raw_count = msl_binding.debug_combat_contacts(handle, 0, 16)
        assert raw_count == 1

        _, filtered_count = msl_binding.debug_combat_contacts_filtered(handle, 0, 16)
        assert filtered_count == 0
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("defender_on_ground", "hitbox_flags"),
    [
        (1, HIT_GROUNDED),
        (0, HIT_AERIAL),
    ],
)
def test_debug_combat_contacts_filtered_keeps_ground_air_match(
    defender_on_ground: int, hitbox_flags: int
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=defender_on_ground)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=hitbox_flags)

        _, raw_count = msl_binding.debug_combat_contacts(handle, 0, 16)
        assert raw_count == 1

        _, filtered_count = msl_binding.debug_combat_contacts_filtered(handle, 0, 16)
        assert filtered_count == 1
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("interaction_flags", "expected_count"),
    [
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION, 0),
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION | X42_FIGHTER_INTERACTION, 1),
    ],
)
def test_debug_combat_contacts_filtered_respects_hitcapsule_x42_b5(
    interaction_flags: int, expected_count: int
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=0)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=HIT_AERIAL | interaction_flags)
        _, raw_count = msl_binding.debug_combat_contacts(handle, 0, 16)
        _, filtered_count = msl_binding.debug_combat_contacts_filtered(handle, 0, 16)
        assert raw_count == 1
        assert filtered_count == expected_count
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("interaction_flags", "expect_hit"),
    [
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION, False),
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION | X42_FIGHTER_INTERACTION, True),
    ],
)
def test_combat_resolve_body_respects_hitcapsule_x42_b5(
    interaction_flags: int, expect_hit: bool
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=1)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=HIT_GROUNDED | interaction_flags)
        before = _read_compare(handle).copy()

        msl_binding.debug_combat_resolve(handle)
        after = _read_compare(handle)

        if expect_hit:
            assert float(after["percent"][1]) > float(before["percent"][1])
            assert int(after["hitlag"][0]) > 0
            assert int(after["hitlag"][1]) > 0
        else:
            assert float(after["percent"][1]) == pytest.approx(float(before["percent"][1]))
            assert int(after["hitlag"][0]) == int(before["hitlag"][0]) == 0
            assert int(after["hitlag"][1]) == int(before["hitlag"][1]) == 0
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("interaction_flags", "expect_clank"),
    [
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION, False),
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION | X42_FIGHTER_INTERACTION, True),
    ],
)
def test_combat_resolve_clank_respects_hitcapsule_x42_b5(
    interaction_flags: int, expect_clank: bool
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=1)
    try:
        for player in (0, 1):
            msl_binding.debug_clear_hurtcaps_world(handle, 0, player)
            msl_binding.debug_clear_hitboxes_world(handle, 0, player)
            msl_binding.debug_set_hitbox_world(handle, 0, player, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)

        enabled_flags = (
            HIT_GROUNDED
            | HIT_CLANK
            | X42_INTERACTION_VALID
            | X42_ITEM_INTERACTION
            | X42_FIGHTER_INTERACTION
        )
        msl_binding.debug_set_hitbox_flags(
            handle, 0, 0, 0, int(HIT_GROUNDED | HIT_CLANK | interaction_flags)
        )
        msl_binding.debug_set_hitbox_flags(handle, 0, 1, 0, int(enabled_flags))

        before = _read_compare(handle).copy()
        msl_binding.debug_combat_resolve(handle)
        after = _read_compare(handle)

        assert float(after["percent"][0]) == pytest.approx(float(before["percent"][0]))
        assert float(after["percent"][1]) == pytest.approx(float(before["percent"][1]))
        if expect_clank:
            assert int(after["hitlag"][0]) > 0
            assert int(after["hitlag"][1]) > 0
        else:
            assert int(after["hitlag"][0]) == int(before["hitlag"][0]) == 0
            assert int(after["hitlag"][1]) == int(before["hitlag"][1]) == 0
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize("attached", [False, True])
def test_third_party_body_hit_preserves_attached_thrown_state(attached: bool) -> None:
    import msl_binding

    handle = msl_binding.init(
        batch_size=1,
        num_players=4,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed = np.zeros(1, dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(32)
    seed["num_players"] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(CHAR_FOX)
    seed["instance_id"][0, :4] = np.array([100, 200, 300, 400], dtype=np.uint16)
    seed["action_id"][0, :4] = np.array([222, 242, ACT_WAIT, ACT_WAIT], dtype=np.uint16)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT1_0)
    seed["attack_ratio"][0, :4] = np.float32(1.0)
    seed["defense_ratio"][0, :4] = np.float32(1.0)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["on_ground"][0, :4] = np.uint8(1)
    seed["grab_owner_port"] = np.uint8(0xFF)
    if attached:
        seed["grab_owner_port"][0, 1] = np.uint8(0)

    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, SEED_DTYPE.itemsize)))
        msl_binding.debug_clear_hitboxes_world(handle, 0, 2)
        msl_binding.debug_set_hitbox_world(handle, 0, 2, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(
            handle,
            0,
            2,
            0,
            HIT_GROUNDED | X42_INTERACTION_VALID | X42_FIGHTER_INTERACTION,
        )
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 2, 0, 45, 100, 0, 20)
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(
            handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5
        )

        msl_binding.debug_combat_resolve(handle)
        after = _read_compare(handle)

        assert float(after["percent"][1]) > 0.0
        assert int(after["hitlag"][1]) > 0
        assert (int(after["action_id"][1]) == 242) is attached
    finally:
        msl_binding.destroy(handle)
