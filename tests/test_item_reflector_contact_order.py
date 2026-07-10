from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids: refs/melee/src/melee/ft/chara/ftFox/forward.h
ACT_WAIT = 0x000E
ACT_REBIRTH_WAIT = 0x000D
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_FX_SPECIAL_LW_HIT = 0x016A
ACT_GUARD_ON = 0x00B2

# Submotion ids: data/moves/fox.json specials_by_msid
SM_WAIT1_0 = 2
SM_FX_SPECIAL_LW_LOOP = 314
SM_FX_SPECIAL_LW_HIT = 315

# Item kind: data/characters/fox.json::blaster_shot_itkind
ITEM_FOX_LASER = 54

BUTTON_B = 0x0200
CHAR_FOX = 1
STAGE_FD = 32

HITBOX_FLAG_X42_B5_FIGHTER_INTERACTION = 1 << 0
HITBOX_FLAG_X42_B7_ITEM_INTERACTION = 1 << 1
HITBOX_FLAG_X42_INTERACTION_VALID = 1 << 2
HITBOX_FLAG_ITEM_HIT_INTERACTION = 1 << 11
HITBOX_FLAG_CLANK = 1 << 14


def _seed_shine_laser_overlap(*, item_owner: int = 0, p0_respawn_skip: bool = False) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["pos_x"][0, 0] = np.float32(-30.0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["action_id"][0, 0] = np.uint16(ACT_REBIRTH_WAIT if p0_respawn_skip else ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, 1] = np.uint16(ACT_FX_SPECIAL_LW_LOOP)
    seed["animation_index"][0, 1] = np.uint32(SM_FX_SPECIAL_LW_LOOP)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["shine_release_lag"][0, 1] = np.uint8(18)
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    # Source ftColl_8007925C checks ReflectDesc before the later x1988/x198C BODY status gate.
    seed["hurtbox_state"][0, 1] = np.uint8(2)

    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(0)
    item["type"] = np.uint16(ITEM_FOX_LASER)
    item["owner"] = np.int8(item_owner)
    item["instance_id"] = np.uint16(333)
    item["attack_id"] = np.uint16(ITEM_FOX_LASER)
    item["attack_instance"] = np.uint16(444)
    item["direction"] = np.float32(1.0)
    item["vel_x"] = np.float32(1.0)
    item["vel_y"] = np.float32(0.0)
    # Overlaps the reflector origin/ReflectDesc bubble after item motion.
    item["pos_x"] = np.float32(-4.0)
    item["pos_y"] = np.float32(6.5)
    item["timer"] = np.float32(10.0)
    item["spawn_id"] = np.uint32(1)
    seed["item_reflect_damage_mul"][0, :] = np.float32(1.0)
    return seed


def _step_seed(
    seed: np.ndarray,
    *,
    p1_hitbox_clank: bool = False,
    p1_hitbox_flags: int = HITBOX_FLAG_CLANK | HITBOX_FLAG_ITEM_HIT_INTERACTION,
    item_collision_only: bool = False,
) -> tuple[np.void, int]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    prev_in = np.zeros((1, input_stride), dtype=np.uint8)
    cur_in = np.zeros((1, input_stride), dtype=np.uint8)
    prev_view = prev_in.view(INPUT_DTYPE).reshape((1,))
    cur_view = cur_in.view(INPUT_DTYPE).reshape((1,))
    # Keep SpecialLwLoop active until item collision reaches ftColl_8007925C-equivalent ordering.
    prev_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_B)
    cur_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_B)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        if p1_hitbox_clank:
            # DEBUG-ONLY fixture geometry: source ftColl_8007925C orders fighter HitCapsule vs
            # item HitCapsule before item ShieldDesc/BODY branches. Keep the hitbox/body spheres
            # overlapping so only ordering distinguishes the result.
            # MSLHITB1 flag bit source: src/hitboxes_tables.h::MSL_HITBOX_FLAG_CLANK.
            msl_binding.debug_set_hitbox_world(handle, 0, 1, 0, -3.0, 6.5, 0.0, 6.0, 3.0, 1)
            msl_binding.debug_set_hitbox_flags(handle, 0, 1, 0, int(p1_hitbox_flags))
            msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -3.0, 6.5, 0.0, -3.0, 6.5, 0.0, 8.0)
            msl_binding.debug_set_hurtcap_enabled(handle, 0, 1, 0, 1)
        if item_collision_only:
            msl_binding.debug_run_item_collision_phase(handle)
        else:
            msl_binding.step_input(handle, prev_in, cur_in)
        item_hitlist_contains = int(msl_binding.debug_hitlist_item_contains(handle, 0, 0, 0, 1))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy(), item_hitlist_contains
    finally:
        msl_binding.destroy(handle)


def test_shine_reflectdesc_precedes_item_body_hurt_status_gate() -> None:
    # Source order in ftColl_8007925C: reflect, absorb, fighter HitCapsules, shield, then BODY.
    # The reflector overlap must still transfer the item and enter SpecialLwHit even when the later
    # BODY hurt-status gate would reject ordinary item BODY contact.
    out, _ = _step_seed(_seed_shine_laser_overlap(item_owner=0))

    assert int(out["action_id"][1]) == ACT_FX_SPECIAL_LW_HIT
    assert int(out["animation_index"][1]) == SM_FX_SPECIAL_LW_HIT
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["owner"]) == 1
    assert int(out["items"][0]["instance_id"]) == 222
    # Hidden BODY-before-reflect-callback rows flip public direction immediately but defer the
    # reflected speed lane until the item callback consumes the BODY latch.
    assert float(out["items"][0]["vel_x"]) == 1.0
    assert float(out["items"][0]["direction"]) == -1.0


def test_shine_reflectdesc_does_not_rehit_own_reflected_item() -> None:
    # ftColl_80077464 / Item_80269F14 owner transfer makes the projectile reflector-owned; later
    # overlap ticks should not re-enter SpecialLwHit or flip ownership/velocity again.
    out, _ = _step_seed(_seed_shine_laser_overlap(item_owner=1, p0_respawn_skip=True))

    assert int(out["action_id"][1]) == ACT_FX_SPECIAL_LW_LOOP
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["owner"]) == 1
    assert int(out["items"][0]["instance_id"]) == 333
    assert float(out["items"][0]["vel_x"]) == 1.0
    assert float(out["items"][0]["direction"]) == 1.0


def test_item_hitcapsule_clank_precedes_shield_and_body_contact() -> None:
    # Source order in ftColl_8007925C after ReflectDesc/AbsorbDesc: item HitCapsule vs fighter
    # HitCapsule, then ShieldDesc, then BODY hurtcaps. A clankable attack bubble overlapping a
    # projectile must suppress later item-shield and item-BODY side effects for that fighter.
    seed = _seed_shine_laser_overlap(item_owner=0, p0_respawn_skip=True)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_ON)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["hurtbox_state"][0, 1] = np.uint8(0)
    seed["shield_hp"][0, 1] = np.float32(60.0)
    seed["items"][0, 0]["pos_x"] = np.float32(-8.0)

    out, item_hitlist_contains = _step_seed(
        seed, p1_hitbox_clank=True, item_collision_only=True
    )

    assert item_hitlist_contains == 1
    assert float(out["shield_hp"][1]) == 60.0
    assert float(out["percent"][1]) == 0.0
    assert int(out["hitlag"][1]) == 0
    assert int(out["items"][0]["exists"]) == 1


def test_item_hitcapsule_clank_uses_fighter_x42_b5_not_item_hurtbox_x42_b7() -> None:
    # ftColl_8007925C builds the fighter HitCapsule candidate list with x42_b5. x42_b7 belongs to
    # the separate fighter-HitCapsule vs item-hurtbox path in it_8026D564. Toggle the two extracted
    # interaction bits independently around the same overlapping item-HitCapsule setup.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/it/itcoll.c::it_8026D564
    seed = _seed_shine_laser_overlap(item_owner=0, p0_respawn_skip=True)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_ON)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["hurtbox_state"][0, 1] = np.uint8(0)
    seed["shield_hp"][0, 1] = np.float32(60.0)
    seed["items"][0, 0]["pos_x"] = np.float32(-8.0)

    b7_only, b7_item_hitlist_contains = _step_seed(
        seed.copy(),
        p1_hitbox_clank=True,
        item_collision_only=True,
        p1_hitbox_flags=(
            HITBOX_FLAG_CLANK
            | HITBOX_FLAG_ITEM_HIT_INTERACTION
            | HITBOX_FLAG_X42_INTERACTION_VALID
            | HITBOX_FLAG_X42_B7_ITEM_INTERACTION
        ),
    )
    b5_only, b5_item_hitlist_contains = _step_seed(
        seed.copy(),
        p1_hitbox_clank=True,
        item_collision_only=True,
        p1_hitbox_flags=(
            HITBOX_FLAG_CLANK
            | HITBOX_FLAG_ITEM_HIT_INTERACTION
            | HITBOX_FLAG_X42_INTERACTION_VALID
            | HITBOX_FLAG_X42_B5_FIGHTER_INTERACTION
        ),
    )

    assert b7_item_hitlist_contains == 0
    assert b5_item_hitlist_contains == 1
    assert int(b7_only["items"][0]["exists"]) == 0
    assert int(b5_only["items"][0]["exists"]) == 1
