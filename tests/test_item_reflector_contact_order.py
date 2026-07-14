from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids: refs/melee/src/melee/ft/chara/ftFox/forward.h
ACT_WAIT = 0x000E
ACT_REBIRTH_WAIT = 0x000D
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_FX_SPECIAL_LW_HIT = 0x016A

# Submotion ids: data/moves/fox.json specials_by_msid
SM_WAIT1_0 = 2
SM_FX_SPECIAL_LW_LOOP = 314
SM_FX_SPECIAL_LW_HIT = 315

# Item kind: data/characters/fox.json::blaster_shot_itkind
ITEM_FOX_LASER = 54

BUTTON_B = 0x0200
CHAR_FOX = 1
STAGE_FD = 32

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


def _step_seed(seed: np.ndarray) -> np.void:
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
        msl_binding.step_input(handle, prev_in, cur_in)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_shine_reflectdesc_precedes_item_body_hurt_status_gate() -> None:
    # Source order in ftColl_8007925C: reflect, absorb, fighter HitCapsules, shield, then BODY.
    # The reflector overlap must still transfer the item and enter SpecialLwHit even when the later
    # BODY hurt-status gate would reject ordinary item BODY contact.
    out = _step_seed(_seed_shine_laser_overlap(item_owner=0))

    assert int(out["action_id"][1]) == ACT_FX_SPECIAL_LW_HIT
    assert int(out["animation_index"][1]) == SM_FX_SPECIAL_LW_HIT
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["owner"]) == 1
    assert int(out["items"][0]["instance_id"]) == 222
    # Item_8026A294 consumes the reflect packet at item priority 14; the laser callback rotates its
    # stored angle by pi, so the public velocity is already reversed when the frame is published.
    assert float(out["items"][0]["vel_x"]) == -1.0
    assert float(out["items"][0]["direction"]) == -1.0


def test_shine_reflectdesc_does_not_rehit_own_reflected_item() -> None:
    # ftColl_80077464 / Item_80269F14 owner transfer makes the projectile reflector-owned; later
    # overlap ticks should not re-enter SpecialLwHit or flip ownership/velocity again.
    out = _step_seed(_seed_shine_laser_overlap(item_owner=1, p0_respawn_skip=True))

    assert int(out["action_id"][1]) == ACT_FX_SPECIAL_LW_LOOP
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["owner"]) == 1
    assert int(out["items"][0]["instance_id"]) == 333
    assert float(out["items"][0]["vel_x"]) == 1.0
    assert float(out["items"][0]["direction"]) == 1.0
