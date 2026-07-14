from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD_SET_OFF = 0x00B5

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
X42_FIGHTER_INTERACTION = 1 << 0
X42_ITEM_INTERACTION = 1 << 1
X42_INTERACTION_VALID = 1 << 2

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2

CHAR_FOX = 1
STAGE_FD = 32

TRIGGER_FULL = np.uint8(255)

def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _read_compare(handle) -> np.void:
    import msl_binding

    compare_stride = int(msl_binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0]


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["shield_hp"][0, :2] = np.float32(_common_attr("start_shield_health"))
    return seed


def test_debug_combat_contacts_classified_shield_overlap_reports_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = _mk_input_bytes(1, input_stride)
        shield = _mk_input_bytes(1, input_stride)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        # Defender P1 holds shield via analog trigger (avoid the GuardReflect digital-press path).
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

        # Step once to compute shield bubble world geometry.
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        assert bubbles.shape == (4, 4)
        shx, shy, shz, shr = (float(bubbles[1, 0]), float(bubbles[1, 1]), float(bubbles[1, 2]), float(bubbles[1, 3]))
        assert shr > 0.0

        # Overlap: attacker hitbox centered on defender shield bubble.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)

        raw, count = msl_binding.debug_combat_contacts_classified(handle, 0, 16)
        assert count == 1

        contact_dtype = np.dtype(
            [
                ("attacker", "u1"),
                ("defender", "u1"),
                ("hitbox_id", "u1"),
                ("contact_kind", "u1"),
                ("hurtcap_id", "u1"),
                ("_pad0", "u1", (3,)),
                ("attacker_msid", "<u2"),
                ("attacker_action_frame", "<i2"),
                ("hitbox_x", "<f4"),
                ("hitbox_y", "<f4"),
                ("hitbox_z", "<f4"),
                ("hitbox_radius", "<f4"),
                ("hitbox_damage", "<f4"),
                ("hurtcap_ax", "<f4"),
                ("hurtcap_ay", "<f4"),
                ("hurtcap_az", "<f4"),
                ("hurtcap_bx", "<f4"),
                ("hurtcap_by", "<f4"),
                ("hurtcap_bz", "<f4"),
                ("hurtcap_radius", "<f4"),
                ("shield_x", "<f4"),
                ("shield_y", "<f4"),
                ("shield_z", "<f4"),
                ("shield_radius", "<f4"),
            ],
            align=False,
        )
        assert raw.shape[1] == contact_dtype.itemsize
        contacts = raw.reshape(-1).view(contact_dtype)[:count]
        assert int(contacts["attacker"][0]) == 0
        assert int(contacts["defender"][0]) == 1
        assert int(contacts["hitbox_id"][0]) == 0
        assert int(contacts["contact_kind"][0]) == 1
        assert int(contacts["hurtcap_id"][0]) == 0xFF

        # No overlap: move hitbox beyond (shield_r + hitbox_r).
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx + shr + 10.0 + 1.0, shy, shz, 1.0, 5.0, 1)
        raw2, count2 = msl_binding.debug_combat_contacts_classified(handle, 0, 16)
        assert count2 == 0

        # Precedence: even if a hurtcap overlaps, shield contact suppresses BODY records.
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, shx - 0.5, shy, shz, shx + 0.5, shy, shz, 0.5)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        raw3, count3 = msl_binding.debug_combat_contacts_classified(handle, 0, 16)
        assert count3 == 1
        contacts3 = raw3.reshape(-1).view(contact_dtype)[:count3]
        assert int(contacts3["contact_kind"][0]) == 1
    finally:
        msl_binding.destroy(handle)



@pytest.mark.parametrize(
    ("interaction_flags", "expect_shield_hit"),
    [
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION, False),
        (X42_INTERACTION_VALID | X42_ITEM_INTERACTION | X42_FIGHTER_INTERACTION, True),
    ],
)
def test_shield_x42_b5_controls_runtime_mutation(
    interaction_flags: int, expect_shield_hit: bool
) -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        neutral = _mk_input_bytes(1, input_stride)
        shield = _mk_input_bytes(1, input_stride)
        shield.view(INPUT_DTYPE).reshape((1,))["p"]["l"][0, 1] = TRIGGER_FULL
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (float(v) for v in bubbles[1])
        assert shr > 0.0

        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(
            handle, 0, 0, 0, int(HIT_GROUNDED | interaction_flags)
        )

        before = _read_compare(handle).copy()
        msl_binding.debug_combat_resolve(handle)
        after = _read_compare(handle)

        if expect_shield_hit:
            assert float(after["shield_hp"][1]) < float(before["shield_hp"][1])
            assert int(after["hitlag"][0]) > 0
            assert int(after["hitlag"][1]) > 0
            assert int(after["action_id"][1]) == ACT_GUARD_SET_OFF
        else:
            assert float(after["shield_hp"][1]) == pytest.approx(float(before["shield_hp"][1]))
            assert int(after["hitlag"][0]) == int(before["hitlag"][0]) == 0
            assert int(after["hitlag"][1]) == int(before["hitlag"][1]) == 0
            assert int(after["action_id"][1]) == int(before["action_id"][1])
    finally:
        msl_binding.destroy(handle)
