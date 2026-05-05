from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


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
    assert float(out["pos_y"][port]) == pytest.approx(
        float(cam_top) + float(out["speed_y_self"][port]),
        abs=1e-5,
    )
    assert float(respawn_y) < float(cam_top)
