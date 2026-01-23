from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2

CHAR_FOX = 1
STAGE_FD = 32


def _common_attr(name: str) -> float:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])

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
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    return seed


def _read_compare(handle) -> np.ndarray:
    import msl_binding

    compare_stride = int(msl_binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0]


def _read_selected_body_hits(handle, *, batch_index: int = 0, max_contacts: int = 16):
    import msl_binding

    raw, count = msl_binding.debug_combat_select_body_hits(handle, batch_index, max_contacts)
    assert raw.shape[1] > 0

    contact_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("defender", "u1"),
            ("hitbox_id", "u1"),
            ("hurtcap_id", "u1"),
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
        ],
        align=False,
    )
    assert raw.shape[1] == contact_dtype.itemsize
    contacts = raw.reshape(-1).view(contact_dtype)[:count]
    return contacts, int(count)


def test_combat_resolve_body_overlap_is_non_mutating() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # Selection should exist (debug). combat_resolve remains non-mutating while the selection
        # scaffolding is iterated against the teacher-forced one-step suite.
        contacts, count = _read_selected_body_hits(handle)
        assert count == 1
        assert int(contacts["attacker"][0]) == 0
        assert int(contacts["defender"][0]) == 1
        assert int(contacts["hitbox_id"][0]) == 0
        assert int(contacts["hurtcap_id"][0]) == 0

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert int(out["hitlag"][0]) == 0
        assert int(out["hitlag"][1]) == 0
        assert int(out["instance_hit_by"][1]) == 0
        assert int(out["last_hit_by"][1]) == 0
        assert int(out["last_attack_landed"][0]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_shield_precedence_blocks_body_selection() -> None:
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

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Reset hitlag (step_input may have decremented timers; keep this test surgical).
        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)

        # Precedence: even if a hurtcap overlaps, shield contact suppresses BODY selection.
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, shx - 0.5, shy, shz, shx + 0.5, shy, shz, 0.5)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        _, count = _read_selected_body_hits(handle)
        assert count == 0

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert int(out["hitlag"][0]) == 0
        assert int(out["hitlag"][1]) == 0
        assert int(out["last_attack_landed"][0]) == 0
        assert int(out["instance_hit_by"][1]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_disabled_hurtcap_does_not_select_body() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        contacts, count = _read_selected_body_hits(handle)
        assert count == 1

        # Disable the only overlapping capsule: contact must no longer be selected.
        msl_binding.debug_set_hurtcap_enabled(handle, 0, 1, 0, 0)
        contacts, count = _read_selected_body_hits(handle)
        assert count == 0
        assert contacts.size == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_rehit_suppression_blocks_repeat_until_clear() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force stable overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # First call selects a hit.
        _, c1 = _read_selected_body_hits(handle)
        assert c1 == 1

        # Second call (no clear/change): suppressed by rehit latch.
        _, c2 = _read_selected_body_hits(handle)
        assert c2 == 0

        # Clear hitboxes (approximates ClearHitboxes), then re-enable: hit can apply again.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        _, c3 = _read_selected_body_hits(handle)
        assert c3 == 0
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        _, c4 = _read_selected_body_hits(handle)
        assert c4 == 1
    finally:
        msl_binding.destroy(handle)
        del handle
