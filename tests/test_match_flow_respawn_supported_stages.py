from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


CHAR_FOX = 1
ACT_WAIT = 14
ACT_DEAD_LEFT = 1
ACT_REBIRTH = 12
SM_WAIT = 2
LEGAL_STAGE_IDS = (2, 3, 8, 28, 31, 32)


def _step_once(seed: np.ndarray, *, num_players: int) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(num_players))
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _seed_dead_left(stage_id: int, port: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(CHAR_FOX)
    seed["facing"][0, :4] = np.uint8(1)
    seed["source_port0"][0, :4] = np.arange(4, dtype=np.uint8)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["anim_frame_f32"][0, :4] = np.float32(0.0)
    seed["ground_id"][0, :4] = np.uint16(0xFFFF)

    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT)
    seed["action_id"][0, port] = np.uint16(ACT_DEAD_LEFT)
    seed["animation_index"][0, port] = np.uint32(0xFFFFFFFF)
    seed["match_flow_timer"][0, port] = np.uint8(1)
    return seed


def test_sustained_rebirth_preserves_carried_spawn_platform_target() -> None:
    # Sustained Rebirth does not reload the static MSLSTG01 respawn X every frame. Vanilla carries
    # the spawn-platform target in mv.co.common.x4 and recomputes self_vel after Rebirth_Anim
    # decrements x0. A mid-Rebirth replay seed can recover that target from visible pos/vel/timer.
    # refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_Rebirth_Anim,ftCo_Rebirth_Phys}
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(3)  # Pokemon Stadium static role has respawn x=0.
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(CHAR_FOX)
    seed["source_port0"][0, :4] = np.arange(4, dtype=np.uint8)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["anim_frame_f32"][0, :4] = np.float32(58.0)
    seed["ground_id"][0, :4] = np.uint16(0xFFFF)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT)

    seed["action_id"][0, 0] = np.uint16(ACT_REBIRTH)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT)
    seed["action_frame"][0, 0] = np.int16(58)
    seed["match_flow_timer"][0, 0] = np.uint8(2)
    seed["pos_x"][0, 0] = np.float32(16.0)
    seed["pos_y"][0, 0] = np.float32(61.0)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    out = _step_once(seed, num_players=4)

    assert int(out["action_id"][0]) == ACT_REBIRTH
    assert float(out["speed_air_x_self"][0]) == pytest.approx(0.0)
    assert float(out["speed_y_self"][0]) == pytest.approx(-1.0)
    assert float(out["pos_x"][0]) == pytest.approx(16.0)
    assert float(out["pos_y"][0]) == pytest.approx(60.0)


def test_rebirth_expiry_enters_rebirthwait_at_current_platform_pose() -> None:
    # ftCo_800D5600 changes Rebirth -> RebirthWait at the current cur_pos, then RebirthWait_Phys
    # owns self_vel.{x,y}. The sim does not preserve arbitrary seeded horizontal velocity through
    # that physics owner.
    # refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D5600,ftCo_RebirthWait_Phys}
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(3)  # Pokemon Stadium static role has respawn x=0.
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(CHAR_FOX)
    seed["source_port0"][0, :4] = np.arange(4, dtype=np.uint8)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["anim_frame_f32"][0, :4] = np.float32(59.0)
    seed["ground_id"][0, :4] = np.uint16(0xFFFF)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT)

    seed["action_id"][0, 0] = np.uint16(ACT_REBIRTH)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT)
    seed["action_frame"][0, 0] = np.int16(59)
    seed["match_flow_timer"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(16.0)
    seed["pos_y"][0, 0] = np.float32(60.0)
    seed["speed_air_x_self"][0, 0] = np.float32(1.25)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.5)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    out = _step_once(seed, num_players=4)

    assert int(out["action_id"][0]) == 13
    assert float(out["speed_air_x_self"][0]) == pytest.approx(0.0)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0)
    assert float(out["speed_y_self"][0]) == pytest.approx(0.0)
    assert float(out["pos_x"][0]) == pytest.approx(16.0)
    assert float(out["pos_y"][0]) == pytest.approx(60.0)


@pytest.mark.parametrize("stage_id", LEGAL_STAGE_IDS)
@pytest.mark.parametrize("port", range(4))
def test_dead_to_rebirth_uses_mslstg01_respawn_and_world_zero_facing(stage_id: int, port: int) -> None:
    # Source lock for the full supported legal-stage aggregate:
    # - gm_1601.c::fn_8016719C stores Player spawn-platform coords from stage respawn roles,
    #   then sets facing to -1.0 when respawn.x >= 0.0f, else +1.0f.
    # - Fighter_UnkInitReset_80067C98 reloads Player_GetSpawnPlatformPos /
    #   Player_GetFacingDirection on Rebirth reset. Its ftCommon_800804EC offset is zero for the
    #   supported normal Fox/Falco reset path because Fighter init clears fp->x40.
    # refs/melee/src/melee/gm/gm_1601.c::fn_8016719C
    # refs/melee/src/melee/pl/player.c::{Player_GetSpawnPlatformPos,Player_GetFacingDirection}
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804EC
    # data/stages/bin/*.bin::MSLSTG01 respawn_points/cam_bounds_world
    msl_binding = pytest.importorskip("msl_binding")
    roles = msl_binding.stage_match_flow_roles(int(stage_id))
    respawn_x, respawn_y = roles["respawn_points"][port]
    cam_top = roles["cam_bounds"][2]

    out = _step_once(_seed_dead_left(stage_id, port), num_players=4)
    expected_facing = 1 if float(respawn_x) < 0.0 else 0

    assert int(out["action_id"][port]) == ACT_REBIRTH
    assert int(out["facing"][port]) == expected_facing
    assert float(out["pos_x"][port]) == pytest.approx(float(respawn_x), abs=1e-5)
    assert float(out["speed_y_self"][port]) < 0.0
    # Pokemon Stadium's Rebirth start coordinate is the Player_80032768 source start Y (120),
    # while its camera/death bounds remain at the broader MSLSTG01 camera top.
    # refs/melee/src/melee/gm/gm_1601.c::{fn_8016719C,fn_80167638}
    # refs/melee/src/melee/pl/player.c::{Player_80032768,Player_LoadPlayerCoords}
    rebirth_start_y = 120.0 if int(stage_id) == 3 else float(cam_top)
    assert float(out["pos_y"][port]) == pytest.approx(
        rebirth_start_y + float(out["speed_y_self"][port]),
        abs=1e-5,
    )
    assert float(respawn_y) < float(cam_top)
