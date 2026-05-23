from __future__ import annotations

import importlib

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


STAGE_FD = 32
CHAR_FOX = 1
ACT_ATTACK_S4_S = 60
ACT_DAMAGE_N_2 = 79
ACT_DAMAGE_AIR_2 = 85
ACT_DAMAGE_AIR_3 = 86
SM_ATTACK_S4_S = 62
SM_DAMAGE_N_2 = 169
SM_DAMAGE_AIR_2 = 175
SM_DAMAGE_AIR_3 = 176


def _stick_i8(v: float) -> np.int8:
    return np.int8(int(np.clip(np.rint(((float(v) + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _base_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["stocks"][0, :2] = np.uint8(1)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["team_id"][0, :2] = [np.uint8(0), np.uint8(2)]
    seed["facing"][0, :2] = [np.uint8(0), np.uint8(1)]
    seed["facing_dir1"][0, :2] = seed["facing"][0, :2]
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["ground_friction_mul"][0, :2] = np.float32(1.0)
    seed["jumps_left"][0, :2] = np.uint8(2)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, :2] = np.uint16(ACT_ATTACK_S4_S)
    seed["animation_index"][0, :2] = np.uint32(SM_ATTACK_S4_S)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["pos_y"][0, :2] = np.float32(0.0001)
    return seed


def _damage_n2_hitlag_seed(*, player: int, pos_x: float, hitlag: int, hitstun: int) -> np.ndarray:
    seed = _base_seed()
    p = int(player)
    seed["action_id"][0, p] = np.uint16(ACT_DAMAGE_N_2)
    seed["animation_index"][0, p] = np.uint32(SM_DAMAGE_N_2)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DAMAGE_N_2)
    seed["seed_prev_action_frame"][0, p] = np.int16(1)
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0)
    seed["pos_x"][0, p] = np.float32(pos_x)
    seed["pos_y"][0, p] = np.float32(0.0001)
    seed["speed_x_attack"][0, p] = np.float32(-0.713 if p == 0 else -1.089)
    seed["speed_y_attack"][0, p] = np.float32(0.689 if p == 0 else 1.051)
    seed["hitlag"][0, p] = np.uint16(hitlag)
    seed["hitstun"][0, p] = np.uint16(hitstun)
    return seed


def _damage_air2_hitlag_seed(
    *,
    player: int,
    pos_x: float,
    pos_y: float,
    hitlag: int,
    hitstun: int,
) -> np.ndarray:
    seed = _base_seed()
    p = int(player)
    seed["action_id"][0, p] = np.uint16(ACT_DAMAGE_AIR_2)
    seed["animation_index"][0, p] = np.uint32(SM_DAMAGE_AIR_2)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DAMAGE_AIR_2)
    seed["seed_prev_action_frame"][0, p] = np.int16(1)
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0)
    seed["pos_x"][0, p] = np.float32(pos_x)
    seed["pos_y"][0, p] = np.float32(pos_y)
    seed["speed_x_attack"][0, p] = np.float32(0.19980676)
    seed["speed_y_attack"][0, p] = np.float32(0.9400171)
    seed["hitlag"][0, p] = np.uint16(hitlag)
    seed["hitstun"][0, p] = np.uint16(hitstun)
    seed["tilt_timer_x"][0, p] = np.uint8(0)
    seed["tilt_timer_y"][0, p] = np.uint8(0)
    seed["state_flags"][0, p, 1] = np.uint8(0x10)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(pos_x)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(0.0001)
    return seed


def _damage_air3_hitlag_seed(
    *,
    player: int,
    pos_x: float,
    pos_y: float,
    hitlag: int,
    hitstun: int,
) -> np.ndarray:
    seed = _base_seed()
    p = int(player)
    seed["action_id"][0, p] = np.uint16(ACT_DAMAGE_AIR_3)
    seed["animation_index"][0, p] = np.uint32(SM_DAMAGE_AIR_3)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DAMAGE_AIR_3)
    seed["seed_prev_action_frame"][0, p] = np.int16(1)
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0)
    seed["pos_x"][0, p] = np.float32(pos_x)
    seed["pos_y"][0, p] = np.float32(pos_y)
    seed["speed_x_attack"][0, p] = np.float32(0.0)
    seed["speed_y_attack"][0, p] = np.float32(0.0)
    seed["hitlag"][0, p] = np.uint16(hitlag)
    seed["hitstun"][0, p] = np.uint16(hitstun)
    seed["tilt_timer_x"][0, p] = np.uint8(0)
    seed["tilt_timer_y"][0, p] = np.uint8(0)
    seed["state_flags"][0, p, 1] = np.uint8(0x10)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(pos_x)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(0.0001)
    return seed


def _input_for_player(player: int, *, main_x: float, main_y: float, c_x: float = 0.0,
                      c_y: float = 0.0, trigger: float = 0.0) -> np.ndarray:
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    input_t["p"]["main_x"][0, player] = _stick_i8(main_x)
    input_t["p"]["main_y"][0, player] = _stick_i8(main_y)
    input_t["p"]["c_x"][0, player] = _stick_i8(c_x)
    input_t["p"]["c_y"][0, player] = _stick_i8(c_y)
    input_t["p"]["l"][0, player] = np.uint8(np.clip(np.rint(trigger * 140.0), 0, 140))
    return input_t


def _step_once(seed: np.ndarray, cur_input: np.ndarray) -> np.void:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, prev_input, cur_input.view(np.uint8).reshape((1, input_stride)))
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize(
    ("player", "pos_x", "hitlag", "hitstun", "main_x", "main_y", "c_x", "c_y"),
    [
        # selfplay_token9_replayprior_1573m_seed25 frame 3535 -> 3536: grounded p1 is
        # hit into airborne DamageN2 and downward SDI would otherwise leave the root below FD.
        (1, -53.836, 6, 20, -0.39, -0.93, 1.0, 0.0),
        # Same trace frame 3567 -> 3568, mirrored onto p0 by the later hit.
        (0, -64.669, 4, 13, -0.39, -0.93, 0.71, -0.71),
    ],
)
def test_common_damage_active_hitlag_sdi_floorhug_does_not_fall_through_fd(
    player: int,
    pos_x: float,
    hitlag: int,
    hitstun: int,
    main_x: float,
    main_y: float,
    c_x: float,
    c_y: float,
) -> None:
    pytest.importorskip("msl_binding")
    # Source owner:
    # - ftCo_Damage_OnEveryHitlag may move the airborne DamageN root during active hitlag.
    # - ftCo_Damage_Coll then runs ft_80081DD4; when mpColl's stay-airborne floor path accepts the
    #   carried FD floor, the fighter stays airborne but floor-hugged instead of tunneling below it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    out = _step_once(
        _damage_n2_hitlag_seed(player=player, pos_x=pos_x, hitlag=hitlag, hitstun=hitstun),
        _input_for_player(player, main_x=main_x, main_y=main_y, c_x=c_x, c_y=c_y, trigger=1.0),
    )

    assert int(out["action_id"][player]) == ACT_DAMAGE_N_2
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == 0
    assert float(out["pos_y"][player]) == pytest.approx(0.0001, abs=0.001)


@pytest.mark.integration
def test_damageair2_active_hitlag_sdi_floorhug_does_not_fall_through_fd_seed26() -> None:
    pytest.importorskip("msl_binding")
    # selfplay_token9_replayprior_1573m_seed26 frame 4173 -> 4174:
    # the hidden x221A_b3/allow_sdi lane plus a current x670/x671 SDI window can move DamageAir2's
    # root far below FD during active hitlag. Because the row carries the callback-current hard
    # floor sweep, `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800477E0` keeps the fighter airborne
    # but floor-hugged instead of publishing the old modelplay fall-through.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    player = 0
    out = _step_once(
        _damage_air2_hitlag_seed(
            player=player,
            pos_x=-60.0,
            pos_y=-4.311050891876221,
            hitlag=2,
            hitstun=12,
        ),
        _input_for_player(player, main_x=0.3875, main_y=-0.925),
    )

    assert int(out["action_id"][player]) == ACT_DAMAGE_AIR_2
    assert int(out["hitlag"][player]) == 1
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == 1
    assert float(out["pos_y"][player]) == pytest.approx(0.0001, abs=0.001)


@pytest.mark.integration
def test_damageair3_same_action_active_hitlag_sdi_floorhug_does_not_clip_fd_trace66() -> None:
    pytest.importorskip("msl_binding")
    # modelplay_customv1_ar_penalties_840m trace_seed66 frames 2259..2262:
    # same-action DamageAir3 hitlag receives a fresh downward SDI input after already carrying the
    # FD main floor as CollData.floor.index. Source still routes DamageAir3 through
    # `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800477E0`; the stay-airborne floor path must
    # publish FloorPush|FloorHug instead of tunneling below the floor until hitlag ends.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    player = 0
    out = _step_once(
        _damage_air3_hitlag_seed(
            player=player,
            pos_x=-38.974609375,
            pos_y=-3.149899959564209,
            hitlag=3,
            hitstun=31,
        ),
        _input_for_player(player, main_x=-0.8375, main_y=-0.55),
    )

    assert int(out["action_id"][player]) == ACT_DAMAGE_AIR_3
    assert int(out["hitlag"][player]) == 2
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == 1
    assert float(out["pos_y"][player]) == pytest.approx(0.0001, abs=0.001)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("action_id", "player", "pos_x", "pos_y", "hitlag", "hitstun", "main_x", "main_y", "c_x", "trigger"),
    [
        # trace_seed66 frames 4212..4214: p1 DamageAir3 has the same active-hitlag
        # `ftCo_Damage_Coll -> mpColl_800477E0` floorhug owner as the p0 window above.
        (ACT_DAMAGE_AIR_3, 1, -16.293006896972656, -9.155311584472656, 3, 23, -0.6375, -0.775, 0.0, 0.0),
    ],
)
def test_damageair_active_hitlag_sdi_floorhug_covers_additional_trace66_windows(
    action_id: int,
    player: int,
    pos_x: float,
    pos_y: float,
    hitlag: int,
    hitstun: int,
    main_x: float,
    main_y: float,
    c_x: float,
    trigger: float,
) -> None:
    pytest.importorskip("msl_binding")
    # Additional modelplay_customv1_ar_penalties_840m trace_seed66 floor-clip window. This keeps
    # the same source owner as the primary lock above but guards the mirrored player slot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    if action_id == ACT_DAMAGE_AIR_3:
        seed = _damage_air3_hitlag_seed(
            player=player,
            pos_x=pos_x,
            pos_y=pos_y,
            hitlag=hitlag,
            hitstun=hitstun,
        )
        expected_action = ACT_DAMAGE_AIR_3
    else:
        seed = _damage_air2_hitlag_seed(
            player=player,
            pos_x=pos_x,
            pos_y=pos_y,
            hitlag=hitlag,
            hitstun=hitstun,
        )
        expected_action = ACT_DAMAGE_AIR_2

    out = _step_once(
        seed,
        _input_for_player(player, main_x=main_x, main_y=main_y, c_x=c_x, trigger=trigger),
    )

    assert int(out["action_id"][player]) == expected_action
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == 1
    assert float(out["pos_y"][player]) == pytest.approx(0.0001, abs=0.001)
