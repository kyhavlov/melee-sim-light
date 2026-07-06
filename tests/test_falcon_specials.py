"""Falcon Punch (ftCa_SpecialN) decomp-anchored unit tests.

Anchors: refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c and
data/moves/falcon.json::specials_by_msid.{301,302}. Replay verification: the falcon suite
(replays/suites/falcon.json) contains SpecialN/SpecialAirN rows; these tests cover the input
-> entry -> impulse -> exit chain that replay reseeds cannot exercise live.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE  # noqa: E402

STAGE_FD = 32
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_FALL_AERIAL = 0x0020
SM_WAIT1 = 2
SM_FALL = 20
ACT_FC_SPECIAL_N = 347
ACT_FC_SPECIAL_AIR_N = 348
BTN_B = 0x0200

FALCON = 2
FOX = 1


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "falcon.json").read_text())


def _mk_inputs(**axes) -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    for k, v in axes.items():
        inp["p"][0, 0][k] = v
    return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))


def _seed(grounded: bool = True, pos_y: float = 0.0, facing: int = 1) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(FALCON)
    seed["char_id"][0, 1] = np.uint8(FOX)
    seed["facing"][0, 0] = np.uint8(facing)
    seed["facing"][0, 1] = np.uint8(1)
    seed["pos_x"][0, 1] = np.float32(60.0)
    seed["pos_y"][0, 0] = np.float32(pos_y)
    seed["on_ground"][0, 0] = np.uint8(1 if grounded else 0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["shield_hp"][0, :2] = np.float32(60.0)
    seed["jumps_left"][0, :2] = np.uint8(2)
    for p in range(2):
        seed["action_id"][0, p] = np.uint16(ACT_WAIT)
        seed["animation_index"][0, p] = np.uint32(SM_WAIT1)
    if not grounded:
        seed["action_id"][0, 0] = np.uint16(ACT_FALL)
        seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    return seed


def _run(seed: np.ndarray, frames: list[np.ndarray]) -> list[np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        outs = []
        prev = _mk_inputs()
        for inp in frames:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            outs.append(ob.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            prev = inp
        return outs
    finally:
        msl_binding.destroy(handle)


def test_grounded_neutral_b_enters_specialn_and_is_not_interruptible() -> None:
    # ftCa_SpecialN_Enter via the Wait_IASA neutral-B chain; ftCa_SpecialN_IASA is empty, so
    # the punch runs to anim end (the script's frame-65 allow_interrupt has no consumer) and
    # exits through ft_8008A2BC -> Wait.
    b = _mk_inputs(buttons=BTN_B)
    jump = _mk_inputs(buttons=0x0400)
    outs = _run(_seed(True), [b] + [jump] * 90 + [_mk_inputs()] * 20)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_SPECIAL_N
    # Held jump during the punch must not interrupt it (KneeBend is 0x18).
    assert 0x0018 not in acts[:90], "grounded Falcon Punch must not be jump-interruptible"
    assert ACT_WAIT in acts[95:], f"punch must exit to Wait at anim end: {acts[90:]}"


def test_grounded_specialn_root_motion_steps_forward() -> None:
    # ftCa_SpecialN_Phys -> ft_80084FA8: the grounded punch's forward lunge is anim
    # root-motion-owned, so the fighter gains meaningful forward distance with neutral stick.
    b = _mk_inputs(buttons=BTN_B)
    outs = _run(_seed(True), [b] + [_mk_inputs()] * 99)
    assert float(outs[-1]["pos_x"][0]) > 8.0


def test_air_specialn_impulse_at_script_cmd0_pulse() -> None:
    # ftCa_SpecialAirN_IASA consumes cmd_vars[0] (script frame 50) once:
    # self_vel = specialn_vel_x * (facing*cos, sin)(angle); neutral stick -> angle 0.
    # The same frame's Phys already runs the cmd1==1 decay (script sets both at 50), so the
    # first visible velocity is vel_x * vel_mul.
    a = _attrs()
    vel_x = float(a["falcon_specialn_vel_x"])
    vel_mul = float(a["falcon_specialn_vel_mul"])
    b = _mk_inputs(buttons=BTN_B)
    outs = _run(_seed(False, pos_y=400.0), [b] + [_mk_inputs()] * 70)
    assert int(outs[10]["action_id"][0]) == ACT_FC_SPECIAL_AIR_N
    # Pre-impulse: ft_80084EEC ordinary fall (no horizontal impulse).
    assert abs(float(outs[45]["speed_air_x_self"][0])) < 1e-3
    vx49 = float(outs[49]["speed_air_x_self"][0])
    assert vx49 == pytest.approx(vel_x * vel_mul, abs=1e-3)
    assert float(outs[49]["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-3)
    # Post-impulse decay phase: *= vel_mul per frame, gravity suspended.
    vx55 = float(outs[55]["speed_air_x_self"][0])
    assert vx55 == pytest.approx(vx49 * vel_mul**6, abs=1e-3)
    assert float(outs[55]["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-3)


def test_air_specialn_stick_angle_tilts_impulse() -> None:
    # ftCaptain_SpecialN_GetAngleVel: |stick.y| in [range_y_neg, range_y_pos] maps to up to
    # angle_diff degrees, sign from stick.y. Full-up stick -> positive vy at the impulse.
    a = _attrs()
    vel_x = float(a["falcon_specialn_vel_x"])
    vel_mul = float(a["falcon_specialn_vel_mul"])
    ang = math.radians(float(a["falcon_specialn_angle_diff"]))
    b = _mk_inputs(buttons=BTN_B)
    up = _mk_inputs(main_y=80)
    outs = _run(_seed(False, pos_y=400.0), [b] + [up] * 60)
    vy49 = float(outs[49]["speed_y_self"][0])
    vx49 = float(outs[49]["speed_air_x_self"][0])
    assert vy49 == pytest.approx(vel_x * math.sin(ang) * vel_mul, abs=1e-2)
    assert vx49 == pytest.approx(vel_x * math.cos(ang) * vel_mul, abs=1e-2)
    # Down stick mirrors the angle.
    down = _mk_inputs(main_y=-80)
    outs = _run(_seed(False, pos_y=400.0), [b] + [down] * 60)
    assert float(outs[49]["speed_y_self"][0]) == pytest.approx(-vel_x * math.sin(ang) * vel_mul,
                                                               abs=1e-2)


def test_air_specialn_cmd1_tail_resumes_gravity_and_fastfall() -> None:
    # Script cmd1=2 at frame 65 hands physics to ft_80084DB0: ordinary gravity resumes and a
    # down-flick can latch fastfall (the falcon branch in msl_action_allows_fastfall).
    a = _attrs()
    b = _mk_inputs(buttons=BTN_B)
    n = _mk_inputs()
    down = _mk_inputs(main_y=-127)
    outs = _run(_seed(False, pos_y=400.0), [b] + [n] * 99)
    # Gravity resumed in the tail (vy < 0 and decreasing).
    assert float(outs[66]["speed_y_self"][0]) < -0.1
    assert float(outs[70]["speed_y_self"][0]) < float(outs[66]["speed_y_self"][0])
    # Fastfall latch in the tail.
    outs = _run(_seed(False, pos_y=400.0), [b] + [n] * 66 + [down] * 20)
    assert float(outs[70]["speed_y_self"][0]) == pytest.approx(
        -float(a["fast_fall_velocity"]), abs=1e-3)
    # No fastfall latch during the pre-impulse phase: ft_80084EEC applies ordinary gravity
    # (terminal_vel) but has no CheckFallFast, so a held down-stick must leave vy at ordinary
    # terminal, not fast_fall_velocity.
    outs = _run(_seed(False, pos_y=400.0), [b] + [down] * 40 + [n] * 30)
    assert float(outs[45]["speed_y_self"][0]) == pytest.approx(-float(a["terminal_vel"]),
                                                               abs=1e-3), (
        "down-stick during the pre-impulse phase must not latch fastfall "
        f"(got vy={float(outs[45]['speed_y_self'][0])})")


def test_air_specialn_lands_into_grounded_variant_at_preserved_frame() -> None:
    # ftCa_SpecialAirN_Coll: ground contact swaps to grounded SpecialN at the preserved
    # animation frame; the punch continues instead of restarting or landing.
    b = _mk_inputs(buttons=BTN_B)
    outs = _run(_seed(False, pos_y=80.0), [b] + [_mk_inputs()] * 120)
    acts = [int(o["action_id"][0]) for o in outs]
    ai = acts.index(ACT_FC_SPECIAL_AIR_N)
    gi = acts.index(ACT_FC_SPECIAL_N)
    assert ai < gi, "air punch must swap into the grounded variant on landing"
    assert ACT_WAIT in acts, "swapped grounded punch must still exit to Wait at anim end"
    # No Landing action between the variants (frame-preserving swap, not a landing).
    assert 0x002A not in acts[ai:gi + 1]


def test_side_b_does_not_enter_falcon_punch() -> None:
    # Raptor Boost is not ported yet: a side-B input must not fall through to SpecialN (the
    # source SpecialS_CheckInput would consume it first).
    side_b = _mk_inputs(buttons=BTN_B, main_x=127)
    outs = _run(_seed(True), [side_b] * 5)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_SPECIAL_N not in acts
    assert ACT_FC_SPECIAL_AIR_N not in acts


def test_air_neutral_b_from_jump_enters_air_punch() -> None:
    # ftCo_SpecialAir_CheckInput is reachable from Jump/Fall family IASAs.
    jump = _mk_inputs(buttons=0x0400)
    n = _mk_inputs()
    b = _mk_inputs(buttons=BTN_B)
    outs = _run(_seed(True), [jump] * 5 + [n] * 3 + [b] + [n] * 5)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_SPECIAL_AIR_N in acts, f"jump -> air punch failed: {acts}"


# ---------------------------------------------------------------------------
# Falcon Kick (ftCa_SpecialLw; actions 357..362, rebound 363)
# ---------------------------------------------------------------------------

ACT_FC_LW = 357
ACT_FC_LW_END = 358
ACT_FC_AIR_LW = 359
ACT_FC_AIR_LW_END = 360
ACT_FC_AIR_LW_END_AIR = 361
ACT_FC_LW_END_AIR = 362
ACT_FC_HI_THROW1 = 363

DOWN_B = dict(buttons=BTN_B, main_y=-127)


def _seed_far(grounded: bool = True, pos_y: float = 0.0) -> np.ndarray:
    # Opponent far away and falcon far left so the travel stays on FD.
    seed = _seed(grounded, pos_y)
    seed["pos_x"][0, 0] = np.float32(-70.0)
    seed["pos_x"][0, 1] = np.float32(70.0)
    return seed


def test_grounded_down_b_enters_falcon_kick_and_exits_through_end() -> None:
    # ftCa_SpecialLw_Enter -> travel -> ftCa_SpecialLw_Anim_inline grounded -> SpecialLwEnd ->
    # ftCommon_8007D92C -> Wait.
    outs = _run(_seed_far(True), [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_LW
    li = max(i for i, a in enumerate(acts) if a == ACT_FC_LW)
    assert acts[li + 1] == ACT_FC_LW_END, f"travel must chain into LwEnd: {acts[li:li+3]}"
    assert ACT_WAIT in acts, "kick must settle into Wait"
    # The travel is anim-root-motion-owned: meaningful forward distance.
    assert float(outs[40]["pos_x"][0]) > -40.0


def test_air_down_b_dive_lands_into_air_lw_end() -> None:
    # ftCa_SpecialAirLw dive (ft_80085134 anim-owned trajectory) -> landing doColl ->
    # SpecialAirLwEnd (frame 0, landing-lag rate) -> ft_8008A2BC Wait tail.
    outs = _run(_seed_far(False, pos_y=60.0), [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 130)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_AIR_LW
    assert ACT_FC_AIR_LW_END in acts, f"air kick must land into AirLwEnd: {sorted(set(acts))}"
    assert ACT_WAIT in acts
    # No frame-preserving grounded-variant swap for the air kick (distinct motion state).
    assert ACT_FC_LW not in acts
    # The dive descends (anim-driven; the animation hops slightly before the dive).
    assert float(outs[28]["pos_y"][0]) < 55.0


def test_falcon_kick_on_hit_slowdown_scales_travel_velocity() -> None:
    # deal_dmg_cb (ftCa_SpecialHi_800E400C): each dealt hit scales the travel velocity by
    # speciallw_on_hit_spd_modifier (0.6). Compare per-frame travel dx just after the connect
    # against a no-target control at the same action frames.
    a = _attrs()
    mod = float(a["falcon_speciallw_on_hit_spd_modifier"])
    seed_hit = _seed_far(True)
    seed_hit["pos_x"][0, 1] = np.float32(-35.0)  # fox in the kick's path
    frames = [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 70
    outs_hit = _run(seed_hit, frames)
    outs_ctl = _run(_seed_far(True), frames)
    # Find the connect (fox percent rises).
    pct = [float(o["percent"][1]) for o in outs_hit]
    hit_f = next(i for i, p in enumerate(pct) if p > 0.0)
    # Skip attacker hitlag (frozen frames), then compare travel speed.
    f = hit_f + 8
    dx_hit = float(outs_hit[f + 4]["pos_x"][0]) - float(outs_hit[f]["pos_x"][0])
    # Control at the same ACTION frame: hitlag froze the anim for the hit row, so compare
    # against the control's dx around the same anim progress (hit_f-aligned is close enough
    # given the travel's flat speed profile).
    dx_ctl = float(outs_ctl[f]["pos_x"][0]) - float(outs_ctl[f - 4]["pos_x"][0])
    assert dx_hit < dx_ctl * (mod + 0.25), (
        f"post-hit travel dx {dx_hit:.2f} must be slowed vs control {dx_ctl:.2f} "
        f"(modifier {mod})")


def test_falcon_kick_no_wall_no_rebound() -> None:
    # The SpecialHiThrow1 rebound requires a wall hug in the facing direction during the
    # script cmd0 window; open FD ground must never produce it. (The rebound positive is
    # replay-covered: the falcon suite's YS ditto contains the dump's only HiThrow1 game.)
    outs = _run(_seed_far(True), [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_HI_THROW1 not in acts
