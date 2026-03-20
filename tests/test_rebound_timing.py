from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

ACT_WAIT = 0x000E
ACT_REBOUND_STOP = 0x00ED
ACT_REBOUND = 0x00EE
SM_WAIT1_0 = 2
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


@pytest.mark.parametrize(
    ("p0_pos_x", "p1_pos_x", "p0_facing", "p1_facing"),
    [
        (14.0, 10.0, 1, 0),  # crossed up / back-to-back
        (14.0, 10.0, 1, 1),  # crossed up / both facing right
    ],
)
def test_rebound_clank_entry_uses_each_fighters_own_facing_not_relative_pos_x(
    p0_pos_x: float, p1_pos_x: float, p0_facing: int, p1_facing: int
) -> None:
    seed = _seed_base()
    seed["pos_x"][0, 0] = np.float32(p0_pos_x)
    seed["pos_x"][0, 1] = np.float32(p1_pos_x)
    seed["facing"][0, 0] = np.uint8(p0_facing)
    seed["facing"][0, 1] = np.uint8(p1_facing)

    out = _resolve_rebound_clank(seed, damage=4.0)

    p0_facing_dir = 1.0 if p0_facing else -1.0
    p1_facing_dir = 1.0 if p1_facing else -1.0
    assert int(out["action_id"][0]) == ACT_REBOUND_STOP
    assert int(out["action_id"][1]) == ACT_REBOUND_STOP
    assert int(out["animation_index"][0]) == 0xFFFFFFFF
    assert int(out["animation_index"][1]) == 0xFFFFFFFF
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        _rebound_speed(4, p0_facing_dir), abs=2e-6
    )
    assert float(out["speed_ground_x_self"][1]) == pytest.approx(
        _rebound_speed(4, p1_facing_dir), abs=2e-6
    )
