from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD = 0x00B3

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32


def _common_attr(name: str) -> float:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _seed_4p_wait() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.array([CHAR_FOX, CHAR_FALCO, CHAR_FOX, CHAR_FALCO], dtype=np.uint8)
    seed["team_id"][0, :4] = np.array([0, 1, 2, 3], dtype=np.uint8)
    seed["attack_ratio"][0, :4] = np.float32(1.0)
    seed["defense_ratio"][0, :4] = np.float32(1.0)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["facing"][0, :4] = np.array([1, 0, 1, 0], dtype=np.uint8)
    seed["on_ground"][0, :4] = np.uint8(1)
    seed["ground_id"][0, :4] = np.uint16(0)
    seed["pos_x"][0, :4] = np.array([-12.0, -4.0, 4.0, 12.0], dtype=np.float32)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :4] = np.int16(0)
    seed["anim_frame_f32"][0, :4] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT1_0)
    seed["shield_hp"][0, :4] = np.float32(_common_attr("start_shield_health"))
    seed["instance_id"][0, :4] = np.array([101, 102, 103, 104], dtype=np.uint16)
    return seed


def test_four_player_active_refreshes_include_slot_three_geometry_and_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    seed = _seed_4p_wait()
    seed["action_id"][0, 3] = np.uint16(ACT_GUARD)
    seed["animation_index"][0, 3] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 3] = np.float32(-1.0)
    seed["state_flags"][0, 3, 2] = np.uint8(0x80)  # fp+0x221B isShieldActive

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        held_shield = np.zeros((1, input_stride), dtype=np.uint8)
        held_view = held_shield.view(INPUT_DTYPE).reshape((1,))
        held_view["p"]["buttons"][0, 3] = np.uint16(BUTTON_L)
        held_view["p"]["l"][0, 3] = np.uint8(140)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_set_hitbox_world(handle, 0, 3, 0, 123.0, 456.0, 0.0, 9.0, 7.0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 3, 0, 100.0, 100.0, 0.0, 101.0, 100.0, 0.0, 8.0)

        before_hitboxes, before_hitbox_count = msl_binding.hitboxes_world(handle, 0, 3)
        before_hurtcaps, before_hurtcap_count = msl_binding.hurtcaps_world(handle, 0, 3)
        assert before_hitbox_count == 1
        assert before_hurtcap_count == 1
        assert float(before_hitboxes[0, 0]) == np.float32(123.0)
        assert float(before_hurtcaps[0, 0]) == np.float32(100.0)

        msl_binding.step_input(handle, held_shield, held_shield)

        hitboxes, hitbox_count = msl_binding.hitboxes_world(handle, 0, 3)
        hurtcaps, hurtcap_count = msl_binding.hurtcaps_world(handle, 0, 3)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
    finally:
        msl_binding.destroy(handle)

    assert hitbox_count == 0
    assert not np.any(hitboxes[:, 9] != np.float32(0.0))
    assert hurtcap_count > 0
    assert not np.any(
        (hurtcaps[:, 0] == np.float32(100.0)) & (hurtcaps[:, 6] == np.float32(8.0))
    )
    assert float(bubbles[3, 3]) > 0.0
