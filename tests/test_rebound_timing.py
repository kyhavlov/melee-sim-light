from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_ATTACK_HI4 = 0x003F
ACT_REBOUND_STOP = 0x00ED
ACT_REBOUND = 0x00EE
SM_WAIT1_0 = 2
SM_FALL = 20
SM_ATTACK_HI4 = 66
SM_REBOUND = 45

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_CLANK = 1 << 14
HIT_REBOUND = 1 << 15


def _common() -> dict[str, float]:
    return json.loads(Path("data/common/ft_common_data.json").read_text())


def _fox() -> dict[str, float]:
    return json.loads(Path("data/characters/fox.json").read_text())


def _step_once(seed: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _resolve_rebound_clank(seed: np.ndarray, *, damage: float = 4.0) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)

        for p in range(2):
            msl_binding.debug_clear_hitboxes_world(handle, 0, p)
            # Force one overlapping clank/rebound hitbox per fighter so combat_resolve executes the
            # grounded hitbox-vs-hitbox ReboundStop lane directly.
            # refs/melee/src/melee/ft/ftcoll.c::ftColl_80079AB0
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
            msl_binding.debug_set_hitbox_world(handle, 0, p, 0, 12.0, 0.0, 0.0, 3.0, damage, 1)
            msl_binding.debug_set_hitbox_flags(
                handle, 0, p, 0, int(HIT_GROUNDED | HIT_CLANK | HIT_REBOUND)
            )

        msl_binding.debug_combat_resolve(handle)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _resolve_rebound_clank_to_first_rebound(
    seed: np.ndarray,
    *,
    damage_p0: float = 4.0,
    damage_p1: float = 4.0,
    p0_smash_release: tuple[int, int] | None = None,
) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        if p0_smash_release is not None:
            frames, hold_frames_max = p0_smash_release
            msl_binding.debug_set_smash_charge_state(handle, 0, 0, 3, frames, hold_frames_max)

        for p, damage in ((0, damage_p0), (1, damage_p1)):
            msl_binding.debug_clear_hitboxes_world(handle, 0, p)
            msl_binding.debug_set_hitbox_world(handle, 0, p, 0, 12.0, 0.0, 0.0, 3.0, damage, 1)
            msl_binding.debug_set_hitbox_flags(
                handle, 0, p, 0, int(HIT_GROUNDED | HIT_CLANK | HIT_REBOUND)
            )

        msl_binding.debug_combat_resolve(handle)
        for _ in range(16):
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(row["action_id"][0]) == ACT_REBOUND and int(row["action_id"][1]) == ACT_REBOUND:
                return row
        raise AssertionError("clank did not reach first Rebound frame")
    finally:
        msl_binding.destroy(handle)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)
    seed["char_id"][0, 1] = np.uint8(22)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(10.0)
    seed["pos_x"][0, 1] = np.float32(14.0)
    return seed


def _rebound_x191c(int_dmg: int) -> float:
    common = _common()
    return (
        float(int_dmg) * float(common["rebound_damage_x191c_mul"])
        + float(common["rebound_damage_x191c_base"])
    )


def _rebound_speed(int_dmg: int, facing_dir: float) -> float:
    common = _common()
    x191c = _rebound_x191c(int_dmg)
    return -facing_dir * (
        x191c * float(common["rebound_ground_x0_mul"])
        + float(common["rebound_ground_x0_base"])
    )


def test_reboundstop_entry_uses_clank_owned_ground_speed() -> None:
    seed = _seed_base()
    rebound_speed = _rebound_speed(4, 1.0)
    fox = _fox()
    expected_speed = rebound_speed + float(fox["gr_friction"])

    seed["action_id"][0, 0] = np.uint16(ACT_REBOUND_STOP)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(rebound_speed)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_REBOUND
    assert int(out["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == SM_REBOUND
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(expected_speed, abs=2e-6)


def test_rebound_frame0_uses_callback_rate_and_ground_friction() -> None:
    seed = _seed_base()
    fox = _fox()
    rebound_speed = _rebound_speed(4, 1.0)
    expected_rate = (float(fox["rebound_anim_numerator_frames"]) + 0.1) / _rebound_x191c(4)
    expected_speed = rebound_speed + float(fox["gr_friction"])

    seed["action_id"][0, 0] = np.uint16(ACT_REBOUND)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_REBOUND)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(rebound_speed)

    out = _step_once(seed)

    assert expected_rate == pytest.approx(3.8333333, abs=1e-5)
    assert int(out["action_id"][0]) == ACT_REBOUND
    assert int(out["action_frame"][0]) == 3
    assert int(out["animation_index"][0]) == SM_REBOUND
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(expected_speed, abs=2e-6)


def test_rebound_anim_end_wait_restores_ground_from_source_carried_floor() -> None:
    seed = _seed_base()
    fox = _fox()

    seed["action_id"][0, 0] = np.uint16(ACT_REBOUND)
    seed["animation_index"][0, 0] = np.uint32(SM_REBOUND)
    seed["action_frame"][0, 0] = np.int16(50)
    seed["anim_frame_f32"][0, 0] = np.float32(1000.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["ecb_lock_timer"][0, 0] = np.uint8(3)
    seed["fall_fast"][0, 0] = np.uint8(1)
    seed["jumps_left"][0, 0] = np.uint8(0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["animation_index"][0]) == SM_WAIT1_0
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert int(out["jumps_left"][0]) == int(fox["max_jumps"])


def test_rebound_anim_end_wait_syncs_ground_velocity_from_air_self_velocity() -> None:
    seed = _seed_base()
    fox = _fox()
    common = _common()
    air_x = 5.0
    old_ground_x = -1.75
    expected_entry_ground_x = air_x
    expected_friction = float(fox["gr_friction"])
    if abs(expected_entry_ground_x) > float(fox["walk_max_vel"]):
        expected_friction *= float(common["high_speed_friction_mul"])
    expected_post_phys_ground_x = expected_entry_ground_x - expected_friction

    seed["action_id"][0, 0] = np.uint16(ACT_REBOUND)
    seed["animation_index"][0, 0] = np.uint32(SM_REBOUND)
    seed["action_frame"][0, 0] = np.int16(50)
    seed["anim_frame_f32"][0, 0] = np.float32(1000.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["speed_air_x_self"][0, 0] = np.float32(air_x)
    seed["speed_ground_x_self"][0, 0] = np.float32(old_ground_x)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["on_ground"][0]) == 1
    # ftCommon_8007D6A4 clamps the old gr_vel, then publishes gr_vel=self_vel.x. The following
    # Wait Phys owns the only reduction visible on this frame.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        expected_post_phys_ground_x, abs=2e-6
    )
    assert float(out["speed_air_x_self"][0]) == pytest.approx(expected_post_phys_ground_x, abs=2e-6)


def test_rebound_anim_end_wait_callback_falls_without_valid_carried_floor() -> None:
    seed = _seed_base()
    fox = _fox()

    seed["action_id"][0, 0] = np.uint16(ACT_REBOUND)
    seed["animation_index"][0, 0] = np.uint32(SM_REBOUND)
    seed["action_frame"][0, 0] = np.int16(50)
    seed["anim_frame_f32"][0, 0] = np.float32(1000.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["pos_y"][0, 0] = np.float32(0.0001)

    out = _step_once(seed)

    # Rebound_Anim enters Wait, then the newly installed Wait_Coll runs in the same fighter proc
    # and immediately takes ft_80084280's floor-loss handoff to Fall.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Coll
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["animation_index"][0]) == SM_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF


@pytest.mark.parametrize(
    ("p0_pos_x", "p1_pos_x", "p0_facing", "p1_facing"),
    [
        (14.0, 10.0, 1, 0),  # crossed up / back-to-back
        (14.0, 10.0, 1, 1),  # crossed up / both facing right
    ],
)
def test_rebound_clank_entry_does_not_publish_hidden_rebound_x0_on_entry(
    p0_pos_x: float, p1_pos_x: float, p0_facing: int, p1_facing: int
) -> None:
    # ReboundStop entry runs inside the collision pass after the reported ground-velocity owner for
    # the frame. Decomp ftCo_80099D9C writes the rebound x0 through ftCommon_800804A0's transient
    # xE8 lane; the visible `speed_ground_x_self` row remains unchanged until ReboundStop_Anim /
    # Rebound consumes the lane on a later procUpdate.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
    seed = _seed_base()
    seed["pos_x"][0, 0] = np.float32(p0_pos_x)
    seed["pos_x"][0, 1] = np.float32(p1_pos_x)
    seed["facing"][0, 0] = np.uint8(p0_facing)
    seed["facing"][0, 1] = np.uint8(p1_facing)

    out = _resolve_rebound_clank(seed, damage=4.0)

    assert int(out["action_id"][0]) == ACT_REBOUND_STOP
    assert int(out["action_id"][1]) == ACT_REBOUND_STOP
    assert int(out["animation_index"][0]) == 0xFFFFFFFF
    assert int(out["animation_index"][1]) == 0xFFFFFFFF
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=2e-6)
    assert float(out["speed_ground_x_self"][1]) == pytest.approx(0.0, abs=2e-6)


def test_rebound_clank_uses_collision_damage_facing_not_visible_facing_for_xe8() -> None:
    # Cross-up/back-facing Rebound clanks must queue xE8 from the clank-local `dmg.facing_dir`,
    # not from replay-visible scalar facing. Here p0 is to the right of p1 but is visibly facing
    # right; using scalar facing would push p0 left on first Rebound, while decomp pushes away from
    # the opponent.
    # refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
    seed = _seed_base()
    seed["pos_x"][0, 0] = np.float32(14.0)
    seed["pos_x"][0, 1] = np.float32(10.0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)

    out = _resolve_rebound_clank_to_first_rebound(seed, damage_p0=4.0, damage_p1=4.0)

    assert float(out["speed_ground_x_self"][0]) == pytest.approx(_rebound_speed(4, -1.0), abs=2e-6)
    assert float(out["speed_ground_x_self"][1]) == pytest.approx(_rebound_speed(4, 1.0), abs=2e-6)


def test_rebound_clank_damage_uses_smash_release_damage_before_stale_scalar() -> None:
    # ftColl_8007ABD0 writes HitCapsule.damage after ftCo_800DEEB8 applies released-smash damage.
    # The ReboundStop clank path then consumes that same HitCapsule.damage for dmg.x191C/xE8.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_8007699C}
    # refs/melee/src/melee/ft/ft_0DF0.c::ftCo_800DEEB8
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_HI4)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_HI4)
    seed["pos_x"][0, 0] = np.float32(10.0)
    seed["pos_x"][0, 1] = np.float32(14.0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)

    raw_damage = 18.0
    charged_damage = raw_damage * 1.3671875
    expected_int_damage = int(charged_damage)
    assert expected_int_damage > int(raw_damage)

    out = _resolve_rebound_clank_to_first_rebound(
        seed,
        damage_p0=raw_damage,
        damage_p1=raw_damage,
        p0_smash_release=(60, 60),
    )

    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        _rebound_speed(expected_int_damage, 1.0), abs=2e-6
    )
    assert float(out["speed_ground_x_self"][0]) != pytest.approx(
        _rebound_speed(int(raw_damage), 1.0), abs=2e-6
    )
