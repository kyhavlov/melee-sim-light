"""Captain Falcon character-mechanic decomp-anchored unit tests.

Anchors: refs/melee/src/melee/ft/chara/ftCaptain and data/moves/falcon.json. Replay verification:
the Falcon suite covers the steady callback rows; these tests cover full input-to-outcome chains
and source-owner boundaries that replay reseeds cannot exercise live.
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
    # "No recent horizontal stick flick" (match-init value): a zero-filled x676_x reads as a
    # flick 0 frames ago and would arm the aerial neutral-B turnaround on the fresh B press.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    seed["x676_x"][0, :2] = np.uint8(0xFE)
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


def test_air_specialn_turnaround_b_flips_facing() -> None:
    # ftCo_SpecialAir_CheckInput neutral-B turnaround: a fresh horizontal flick opposite to
    # facing (x676_x < x224, x2228_b7 latch) flips facing before the SpecialAirN Enter.
    # Facing right + fresh leftward flick (x676_x=0, x2228_b7=0) -> enters facing left, and the
    # frame-50 impulse pushes -x.
    seed = _seed(False, pos_y=400.0)
    seed["x676_x"][0, 0] = np.uint8(0)
    outs = _run(seed, [_mk_inputs(buttons=BTN_B)] + [_mk_inputs()] * 55)
    assert int(outs[0]["action_id"][0]) == ACT_FC_SPECIAL_AIR_N
    assert int(outs[0]["facing"][0]) == 0
    assert float(outs[49]["speed_air_x_self"][0]) < 0.0


def test_air_specialn_no_turnaround_without_fresh_flick() -> None:
    # Same press with the flick latch stale (x676_x=0xFE): no turnaround.
    outs = _run(_seed(False, pos_y=400.0), [_mk_inputs(buttons=BTN_B)] + [_mk_inputs()] * 3)
    assert int(outs[0]["action_id"][0]) == ACT_FC_SPECIAL_AIR_N
    assert int(outs[0]["facing"][0]) == 1


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
    # The preserved-frame state swap selects grounded SpecialN's FigaTree; the destination TransN
    # slice owns the landing-frame ground velocity.
    assert abs(float(outs[gi]["speed_ground_x_self"][0])) > 1e-4
    assert float(outs[gi]["speed_ground_x_self"][0]) == pytest.approx(
        float(outs[gi]["speed_air_x_self"][0]), abs=1e-7
    )


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
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_THROW_LW = 0x00DE
ACT_THROWN_LW = 0x00F2

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
    # Fighter_ChangeMotionState clamps the outgoing root velocity to the character's run terminal
    # before SpecialLwEnd's ft_80084F3C high-speed friction step.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEnd_Phys
    attrs = _attrs()
    common = json.loads((ROOT / "data/common/ft_common_data.json").read_text())
    expected_end_speed = float(attrs["dash_run_terminal_velocity"]) - (
        float(attrs["gr_friction"]) * float(common["high_speed_friction_mul"])
    )
    assert float(outs[li + 1]["speed_ground_x_self"][0]) == pytest.approx(
        expected_end_speed, abs=1e-6
    )


def test_grounded_down_b_completion_refreshes_full_jumps() -> None:
    # ftCa_SpecialLw_Anim_inline(condition=0) calls ftCommon_8007D7FC before entering
    # SpecialLwEnd. Its D6A4 tail writes jumpsUsed=0, clears fastfall, and unlocks ECB. Seed stale
    # values deliberately so the terminal callback, rather than normal grounded invariants, owns
    # the replay-visible jump repair.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Anim_inline
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    seed = _seed_far(True)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_LW)
    seed["animation_index"][0, 0] = np.uint32(311)
    seed["action_frame"][0, 0] = np.int16(39)
    seed["anim_frame_f32"][0, 0] = np.float32(39.0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["fall_fast"][0, 0] = np.uint8(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FC_LW_END
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"])


def test_air_down_b_dive_lands_into_air_lw_end() -> None:
    # ftCa_SpecialAirLw dive (ft_80085134 anim-owned trajectory) -> landing doColl ->
    # SpecialAirLwEnd (frame 0, landing-lag rate) -> ft_8008A2BC Wait tail.
    seed = _seed_far(False, pos_y=60.0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    outs = _run(seed, [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 130)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_AIR_LW
    assert ACT_FC_AIR_LW_END in acts, f"air kick must land into AirLwEnd: {sorted(set(acts))}"
    assert ACT_WAIT in acts
    # No frame-preserving grounded-variant swap for the air kick (distinct motion state).
    assert ACT_FC_LW not in acts
    landing = acts.index(ACT_FC_AIR_LW_END)
    assert int(outs[landing]["jumps_left"][0]) == int(_attrs()["max_jumps"])
    # The dive descends (anim-driven; the animation hops slightly before the dive).
    assert float(outs[28]["pos_y"][0]) < 55.0


def test_air_down_b_completion_restores_falcons_double_jump() -> None:
    # ftCa_SpecialAirLw_Anim calls ftCommon_8007D5D4 before entering SpecialAirLwEndAir.
    # The call is intentional even though Falcon is already airborne: jumpsUsed is reset to 1,
    # so Slippi's jumps-left lane returns to max_jumps-1 after a previously spent double jump.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLw_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    seed = _seed_far(False, pos_y=500.0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    outs = _run(seed, [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    transition = acts.index(ACT_FC_AIR_LW_END_AIR)

    assert acts[transition - 1] == ACT_FC_AIR_LW
    assert int(outs[transition - 1]["jumps_left"][0]) == 0
    assert int(outs[transition]["jumps_left"][0]) == int(_attrs()["max_jumps"]) - 1
    # ftCa_SpecialAirLw_Anim changes state without ftAnim_8006EBA4, so the destination remains at
    # its frame-0 entry value until its own next animation callback.
    assert int(outs[transition]["action_frame"][0]) == 0


def test_airborne_ground_kick_completion_restores_falcons_double_jump() -> None:
    # The grounded kick can leave an edge without changing action. Its terminal Anim callback then
    # calls the same ftCommon_8007D5D4 absolute jump write before entering SpecialLwEndAir.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Anim_inline
    seed = _seed_far(True)
    seed["pos_x"][0, 0] = np.float32(82.0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    outs = _run(seed, [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 90)
    acts = [int(out["action_id"][0]) for out in outs]
    transition = acts.index(ACT_FC_LW_END_AIR)

    assert acts[transition - 1] == ACT_FC_LW
    assert int(outs[transition - 1]["on_ground"][0]) == 0
    assert int(outs[transition]["jumps_left"][0]) == int(_attrs()["max_jumps"]) - 1


def test_air_kick_landing_end_floor_loss_enters_fall_and_consumes_ground_jump() -> None:
    # SpecialAirLwEnd is grounded but its Coll callback is ftCa_SpecialAirLwEnd_Coll, which calls
    # ft_80084104. Losing the floor therefore enters Fall through ftCo_Fall_Enter and its
    # ftCommon_8007D5D4 ground-to-air bundle instead of carrying the landing-skid state airborne.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLwEnd_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084104
    seed = _seed_far(True)
    seed["pos_x"][0, 0] = np.float32(84.0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_AIR_LW_END)
    seed["animation_index"][0, 0] = np.uint32(314)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(2.0)
    seed["speed_air_x_self"][0, 0] = np.float32(2.0)
    # Model a grounded source whose current CollData floor disappeared (for example, a moving
    # stage object no longer supporting the fighter). B2DC cannot retain an absent floor.
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"]) - 1
    assert float(out["speed_air_x_self"][0]) == pytest.approx(
        float(_attrs()["air_drift_max"]), abs=1e-6
    )
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_air_kick_landing_end_keeps_ground_and_full_jumps_with_floor_support() -> None:
    seed = _seed_far(True)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_AIR_LW_END)
    seed["animation_index"][0, 0] = np.uint32(314)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)
    seed["ground_id"][0, 0] = np.uint16(0)
    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FC_AIR_LW_END
    assert int(out["on_ground"][0]) == 1
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"])


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


def test_falcon_kick_item_hurtbox_hit_runs_shared_deal_damage_callback() -> None:
    # Fighter HitCapsule -> item hurtbox writes the same attacker x1914 lane as fighter BODY
    # contact. Fighter_ProcessHit must therefore run Falcon Kick's deal_dmg_cb even when the only
    # victim is a stage item. Use an extracted Yoshi Shy Guy hurtbox as the positive owner and an
    # otherwise identical no-item rollout as the velocity control.
    # refs/melee/src/melee/it/itcoll.c::it_802703E8
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialHi_800E400C
    seed_hit = _seed_far(True)
    seed_hit["stage_id"][0] = np.uint32(8)
    seed_hit["pos_x"][0, 0] = np.float32(-40.0)
    seed_hit["pos_x"][0, 1] = np.float32(50.0)
    seed_hit["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)
    seed_hit["stage_yoshi_shyguy_timer_u16"][0] = np.uint16(120)
    shyguy = seed_hit["items"][0, 0]
    shyguy["exists"] = np.uint8(1)
    shyguy["type"] = np.uint16(0xD2)
    shyguy["state"] = np.uint8(1)
    shyguy["owner"] = np.int8(-1)
    shyguy["spawn_id"] = np.uint32(88)
    shyguy["pos_x"] = np.float32(-35.0)
    shyguy["pos_y"] = np.float32(0.0)
    shyguy["direction"] = np.float32(1.0)

    seed_control = seed_hit.copy()
    seed_control["items"][0, 0]["exists"] = np.uint8(0)
    frames = [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 70
    hit = _run(seed_hit, frames)
    control = _run(seed_control, frames)
    hit_frame = next(i for i, out in enumerate(hit) if int(out["items"][0]["damage"]) > 0)
    compare_frame = hit_frame + 8  # first frame after the eight-frame x1914 deal hitlag
    hit_dx = float(hit[compare_frame + 4]["pos_x"][0]) - float(hit[compare_frame]["pos_x"][0])
    control_dx = float(control[compare_frame]["pos_x"][0]) - float(
        control[compare_frame - 4]["pos_x"][0]
    )

    assert hit_dx == pytest.approx(
        control_dx * float(_attrs()["falcon_speciallw_on_hit_spd_modifier"]), abs=1e-4
    )


def test_falcon_kick_reseed_consumes_persisted_speciallw_friction() -> None:
    # Validation preprocessing carries mv.ca.speciallw.friction after the victim has left
    # hitlag/hitstun. A mid-kick seed must consume that explicit hidden lane rather than infer it
    # from current victim state.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    #   ftCa_SpecialHi_800E400C,ftCa_SpecialLw_Phys}
    control = _seed_far(True)
    control["action_id"][0, 0] = np.uint16(ACT_FC_LW)
    control["animation_index"][0, 0] = np.uint32(311)
    control["action_frame"][0, 0] = np.int16(20)
    control["anim_frame_f32"][0, 0] = np.float32(20.0)
    control["falcon_speciallw_friction"][0, 0] = np.float32(1.0)

    slowed = control.copy()
    slowed["falcon_speciallw_hits"][0, 0] = np.uint8(2)
    slowed["falcon_speciallw_friction"][0, 0] = np.float32(0.36)

    control_out = _run(control, [_mk_inputs()])[0]
    slowed_out = _run(slowed, [_mk_inputs()])[0]
    control_dx = float(control_out["pos_x"][0]) - float(control["pos_x"][0, 0])
    slowed_dx = float(slowed_out["pos_x"][0]) - float(slowed["pos_x"][0, 0])
    assert slowed_dx == pytest.approx(control_dx * 0.36, abs=1e-5)


def test_falcon_down_throw_publishes_release_floor_before_upward_launch() -> None:
    # Falcon ThrowLw's attached x1A70 pose finishes below the stage floor. On release,
    # ftCo_800DDDE4 runs the selected-fighter mpColl_800471F8 publication before applying the
    # extracted 65-degree throw hit. The victim therefore launches upward from floor level under
    # ftCommon_8007D5D4's ECB lock instead of colliding into DownBound on the next frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
    # data/moves/falcon.json::moves.ftCo_SM_ThrowLw
    seed = _seed(True)
    seed["pos_x"][0, 1] = np.float32(6.0)
    seed["facing"][0, 1] = np.uint8(0)
    idle = _mk_inputs()
    script = (
        [_mk_inputs(buttons=0x0010)] * 3
        + [idle] * 18
        + [_mk_inputs(main_y=-127)]
        + [idle] * 40
    )
    outs = _run(seed, script)
    release = next(
        i
        for i, out in enumerate(outs)
        if int(out["action_id"][0]) == ACT_THROW_LW
        and int(out["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    )

    assert int(outs[release - 1]["action_id"][1]) == ACT_THROWN_LW
    assert float(outs[release - 1]["pos_y"][1]) < 0.0
    launched = outs[release : release + 5]
    assert all(int(out["action_id"][1]) == ACT_DAMAGE_FLY_TOP for out in launched)
    assert all(int(out["on_ground"][1]) == 0 for out in launched)
    ys = [float(out["pos_y"][1]) for out in launched]
    assert ys[0] > 0.0
    assert all(y1 > y0 for y0, y1 in zip(ys[:-1], ys[1:], strict=True))


# ---------------------------------------------------------------------------
# Raptor Boost (ftCa_SpecialS; actions 349..352)
# ---------------------------------------------------------------------------

ACT_FC_S_START = 349
ACT_FC_S = 350
ACT_FC_AIR_S_START = 351
ACT_FC_AIR_S = 352
ACT_FALL_SPECIAL = 0x0023
ACT_LANDING_FALL_SPECIAL = 0x002B

SIDE_B = dict(buttons=BTN_B, main_x=127)


def _seed_vs(ox: float, grounded: bool = True, y: float = 0.0) -> np.ndarray:
    seed = _seed(grounded, y)
    seed["pos_x"][0, 1] = np.float32(ox)
    seed["facing"][0, 1] = np.uint8(0)
    return seed


def test_raptor_boost_ground_miss_runs_to_wait() -> None:
    # ftCa_SpecialSStart_Anim: no detect -> anim end -> ft_8008A2BC Wait tail. The inert
    # detect hitboxes deal no damage.
    outs = _run(_seed_vs(200.0), [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_S_START
    assert ACT_FC_S not in acts
    assert ACT_WAIT in acts
    assert float(outs[-1]["percent"][1]) == 0.0


def test_raptor_boost_ground_detect_transitions_to_hit_punch() -> None:
    # ftCa_SpecialS_OnDetect (inert hitbox touches the fighter during the cmd0 window) ->
    # onDetectGround -> SpecialS; the hit state's scripted hitbox deals the damage (7.0).
    outs = _run(_seed_vs(25.0), [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_S in acts, f"detect must enter the hit punch: {sorted(set(acts))}"
    assert float(outs[-1]["percent"][1]) > 0.0
    assert ACT_WAIT in acts


def test_raptor_boost_detect_ignores_coarse_dense_hitlist_seed() -> None:
    # A replay-derived hit-group seed is not an exact HitCapsule victims_1 entry. The inert BODY
    # path must still run lbColl admission and OnDetect; concrete per-hitbox entries remain live.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    seed = _seed_vs(25.0)
    seed["combat_hitlist_cd"][0, 0, 0, 1] = np.uint16(0xFFFF)
    seed["combat_hitlist_victim_iid"][0, 0, 0, 1] = seed["instance_id"][0, 1]

    outs = _run(seed, [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 40)

    assert ACT_FC_S in [int(out["action_id"][0]) for out in outs]


def test_raptor_boost_detects_shielding_opponent() -> None:
    # The inert shield-overlap branch writes the attacker's unk_gobj too, so Raptor Boost
    # connects on shield (the punch is then shielded).
    seed = _seed_vs(25.0)
    hold_shield = np.zeros((1,), dtype=INPUT_DTYPE)
    hold_shield["p"][0, 0]["buttons"] = BTN_B
    hold_shield["p"][0, 0]["main_x"] = 127
    hold_shield["p"][0, 1]["l"] = 200
    first = hold_shield.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))
    rest = np.zeros((1,), dtype=INPUT_DTYPE)
    rest["p"][0, 1]["l"] = 200
    rest_b = rest.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))
    outs = _run(seed, [first] + [rest_b] * 80)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_S in acts, f"shield contact must fire OnDetect: {sorted(set(acts))}"
    # The defender never leaves the guard family into a damage action.
    o_acts = set(int(o["action_id"][1]) for o in outs)
    assert not any(0x004B <= a <= 0x005B for a in o_acts), f"fox must shield the punch: {o_acts}"


def test_raptor_boost_detect_window_closes_at_cmd0_clear() -> None:
    # OnDetect gates on the script cmd_vars[0] window (15..35). An opponent placed past the
    # window's reach must not trigger the hit state even though the dash keeps moving.
    outs = _run(_seed_vs(70.0), [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_S not in acts, f"post-window contact must not detect: {sorted(set(acts))}"
    assert float(outs[-1]["percent"][1]) == 0.0


def test_raptor_boost_ground_floor_loss_clamps_to_air_drift() -> None:
    # ftCa_SpecialSStart_Coll floor loss: ftCommon_8007D60C clears gr_vel/burns jumps, then
    # ftCommon_ClampAirDrift clamps self_vel.x before ftCo_80096900 enters FallSpecial.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D60C,ftCommon_ClampAirDrift}
    seed = _seed_vs(200.0)
    seed["pos_x"][0, 0] = np.float32(83.0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(17)
    seed["anim_frame_f32"][0, 0] = np.float32(17.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(5.2283)
    seed["speed_air_x_self"][0, 0] = np.float32(5.2283)
    seed["ground_id"][0, 0] = np.uint16(0)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 0
    assert int(out["jumps_left"][0]) == 0
    assert float(out["speed_air_x_self"][0]) == pytest.approx(float(_attrs()["air_drift_max"]), abs=1e-6)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_raptor_boost_air_miss_ends_in_freefall_landing() -> None:
    # ftCa_SpecialAirSStart_Anim miss end: ftCo_80096900(1,1,0,1,miss_lag) freefall ->
    # LandingFallSpecial.
    outs = _run(_seed_vs(200.0, grounded=False, y=80.0), [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_AIR_S_START
    assert int(outs[0]["jumps_left"][0]) == 0
    assert float(outs[0]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-7)
    assert ACT_FALL_SPECIAL in acts or ACT_LANDING_FALL_SPECIAL in acts, (
        f"air miss must reach freefall/landing lag: {sorted(set(acts))}")
    assert ACT_FC_AIR_S not in acts


def test_raptor_boost_air_detect_enters_air_hit_and_lands_with_lag() -> None:
    # Low flat air dash over a standing opponent: detect -> SpecialAirS (uppercut, 7%) ->
    # landing -> LandingFallSpecial with hit landing lag.
    outs = _run(_seed_vs(25.0, grounded=False, y=8.0), [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_AIR_S in acts, f"air detect must enter the air hit: {sorted(set(acts))}"
    assert ACT_LANDING_FALL_SPECIAL in acts
    assert float(outs[-1]["percent"][1]) > 0.0


def test_falcon_kick_no_wall_no_rebound() -> None:
    # The SpecialHiThrow1 rebound requires a wall hug in the facing direction during the
    # script cmd0 window; open FD ground must never produce it. (The rebound positive is
    # replay-covered: the falcon suite's YS ditto contains the dump's only HiThrow1 game.)
    outs = _run(_seed_far(True), [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_FC_HI_THROW1 not in acts


# ---------------------------------------------------------------------------
# Falcon Dive (ftCa_SpecialHi; actions 353..356, victim CaptureCaptain 275)
# ---------------------------------------------------------------------------

ACT_FC_HI = 353
ACT_FC_AIR_HI = 354
ACT_FC_HI_CATCH = 355
ACT_FC_HI_THROW = 356
ACT_CAPTURE_CAPTAIN = 275

UP_B = dict(buttons=BTN_B, main_y=127)


def test_grounded_up_b_whiff_rises_and_ends_in_freefall() -> None:
    # ftCa_SpecialHi_Enter (grounded), set_airborne_state@14 launches the rise, and the whiffed
    # anim end exits through ftCo_80096900(1,1,0,...) freefall -> LandingFallSpecial.
    outs = _run(_seed_far(True), [_mk_inputs(**UP_B)] + [_mk_inputs()] * 140)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_HI
    assert max(float(o["pos_y"][0]) for o in outs) > 15.0, "dive must gain height"
    assert ACT_FALL_SPECIAL in acts or ACT_LANDING_FALL_SPECIAL in acts, (
        f"whiffed dive must end in freefall/landing lag: {sorted(set(acts))}")
    assert ACT_FC_HI_CATCH not in acts, "whiff must not enter the catch"


def test_air_up_b_enters_air_dive_and_double_jump_is_burned() -> None:
    # ftCa_SpecialAirHi_Enter via the aerial Up-B zone; the entry burns all jumps
    # (ftCa_SpecialLw_800E49FC: x1968_jumpsUsed = max_jumps), so mashing jump in the
    # post-dive freefall never produces JumpAerial (0x1B/0x1C).
    jump = _mk_inputs(buttons=0x0400)
    outs = _run(_seed_far(False, pos_y=40.0), [_mk_inputs(**UP_B)] + [jump] * 120)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_FC_AIR_HI
    assert 0x001B not in acts and 0x001C not in acts, "dive must burn the double jump"
    assert ACT_FALL_SPECIAL in acts or ACT_LANDING_FALL_SPECIAL in acts


def test_air_dive_reseed_preserves_source_overcap_velocity_branch() -> None:
    # Slippi exposes the post-Phys sum of TransN and mv.ca.specialhi.vel. Reseed inversion must not
    # clamp the recovered hidden velocity: ftCa_SpecialHi_Phys has an explicit over-cap deceleration
    # branch and carries values above specialhi_horz_vel * air_drift_max into the next frame.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHi_Phys
    seed = _seed(False, pos_y=400.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_AIR_HI)
    seed["animation_index"][0, 0] = np.uint32(308)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(20.0)
    seed["speed_air_x_self"][0, 0] = np.float32(3.0)

    out = _run(seed, [_mk_inputs()])[0]
    attrs = _attrs()
    drift_cap = float(attrs["falcon_specialhi_horz_vel"]) * float(attrs["air_drift_max"])
    assert int(out["action_id"][0]) == ACT_FC_AIR_HI
    assert float(out["speed_air_x_self"][0]) > 2.0 * drift_cap


def test_air_dive_anim_end_preserves_horizontal_velocity_into_fallspecial() -> None:
    # ftCo_80096900's already-airborne branch changes state and consumes jumps but does not rewrite
    # self_vel.x. The outgoing SpecialHi composite velocity therefore enters FallSpecial intact.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialAirHi_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
    seed = _seed_far(False, pos_y=400.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_AIR_HI)
    seed["animation_index"][0, 0] = np.uint32(308)
    seed["action_frame"][0, 0] = np.int16(64)
    seed["anim_frame_f32"][0, 0] = np.float32(64.0)
    seed["speed_air_x_self"][0, 0] = np.float32(2.5)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL
    assert float(out["speed_air_x_self"][0]) > 2.0


def test_dive_grabs_grounded_opponent_hits_and_throws() -> None:
    # Connect vs a GROUNDED victim (grab hitboxes at frame 13, before the frame-14 airborne
    # switch): grab_flow's falcon branch -> attacker SpecialHiCatch(355) with x221B_b7 (the
    # attacker snaps to the victim), victim CaptureCaptain(275). The HiCatch script's 5dmg
    # hit lands on the held victim (attached-suppressed: no state change), then doCatchAnim
    # applies Throw0's 12dmg release (ftCo_800DE2A8/ftCo_800DE7C0) into the damage aftermath.
    seed = _seed_vs(9.0)
    outs = _run(seed, [_mk_inputs(**UP_B)] + [_mk_inputs()] * 130)
    acts = [int(o["action_id"][0]) for o in outs]
    o_acts = [int(o["action_id"][1]) for o in outs]
    assert ACT_FC_HI_CATCH in acts, f"dive must connect: {sorted(set(acts))}"
    assert ACT_CAPTURE_CAPTAIN in o_acts, f"victim must be held: {sorted(set(o_acts))}"
    assert ACT_FC_HI_THROW in acts, "catch anim end must enter the throw"
    release_i = acts.index(ACT_FC_HI_THROW)
    assert int(outs[release_i]["jumps_left"][0]) == 1
    assert float(outs[release_i]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-7)
    # 5 (HiCatch scripted hit) + stale 12 (Throw0 release hitbox).
    assert float(outs[-1]["percent"][1]) == pytest.approx(15.92, abs=1e-5)
    # Victim leaves the hold only through the release damage aftermath,
    # never via Landing/Wait straight out of CaptureCaptain.
    li = max(i for i, a in enumerate(o_acts) if a == ACT_CAPTURE_CAPTAIN)
    assert 0x004B <= o_acts[li + 1] <= 0x005B or o_acts[li + 1] == ACT_FALL, (
        f"release must enter the damage aftermath: {o_acts[li:li+3]}")


def test_grounded_dive_release_anchor_mirrors_with_facing() -> None:
    right = _seed_vs(9.0)
    left = _seed_vs(9.0)
    left["pos_x"][0, :2] *= np.float32(-1.0)
    left["facing"][0, 0] = np.uint8(0)
    left["facing"][0, 1] = np.uint8(1)

    inputs = [_mk_inputs(**UP_B)] + [_mk_inputs()] * 130
    right_out = _run(right, inputs)
    left_out = _run(left, inputs)
    right_release = next(o for o in right_out if int(o["action_id"][0]) == ACT_FC_HI_THROW)
    left_release = next(o for o in left_out if int(o["action_id"][0]) == ACT_FC_HI_THROW)

    # ftCo_800DDDE4 samples the victim's mirrored TransN2 joint, then applies Falcon's x1A70.
    # A fixed-sign release correction can match only one of these two source-equivalent cases.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    assert float(left_release["pos_x"][0]) == pytest.approx(
        -float(right_release["pos_x"][0]), abs=1e-5
    )
    assert float(left_release["pos_y"][0]) == pytest.approx(
        float(right_release["pos_y"][0]), abs=1e-5
    )


def test_dive_grabs_airborne_opponent_hanging_from_attacker() -> None:
    # Connect vs an AIRBORNE victim: no attacker snap (x221B_b7=0); the victim hangs from the
    # attacker's grab anchor (ftCo_800DB368 + accessory1) and rides the rising dive until the
    # throw releases.
    seed = _seed_vs(8.0, grounded=False, y=40.0)
    # Fox seeded falling from above: by the grab window (frame 13) it has fallen to ~45,
    # level with the rising dive.
    seed["pos_y"][0, 1] = np.float32(66.0)
    seed["on_ground"][0, 1] = np.uint8(0)
    seed["action_id"][0, 1] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 1] = np.uint32(SM_FALL)
    outs = _run(seed, [_mk_inputs(**UP_B)] + [_mk_inputs()] * 130)
    acts = [int(o["action_id"][0]) for o in outs]
    o_acts = [int(o["action_id"][1]) for o in outs]
    assert ACT_FC_HI_CATCH in acts, f"air dive must connect: {sorted(set(acts))}"
    assert ACT_CAPTURE_CAPTAIN in o_acts
    # While held, the hanging victim tracks the attacker (rides the rise: same-direction dy).
    held = [i for i, a in enumerate(o_acts) if a == ACT_CAPTURE_CAPTAIN]
    if len(held) >= 3:
        f0, f1 = held[1], held[-1]
        dy_victim = float(outs[f1]["pos_y"][1]) - float(outs[f0]["pos_y"][1])
        dy_attacker = float(outs[f1]["pos_y"][0]) - float(outs[f0]["pos_y"][0])
        assert abs(dy_victim - dy_attacker) < 3.0, (
            f"hanging victim must ride the attacker: victim dy {dy_victim:.2f} vs "
            f"attacker dy {dy_attacker:.2f}")
    assert float(outs[-1]["percent"][1]) == pytest.approx(15.92, abs=1e-5)


def test_dive_whiff_beyond_reach_never_catches() -> None:
    # The grab is script-hitbox-owned (frames 13..14); an opponent far outside its reach must
    # leave both fighters untouched (no catch, no damage).
    outs = _run(_seed_vs(200.0), [_mk_inputs(**UP_B)] + [_mk_inputs()] * 130)
    acts = [int(o["action_id"][0]) for o in outs]
    o_acts = set(int(o["action_id"][1]) for o in outs)
    assert ACT_FC_HI_CATCH not in acts
    assert ACT_CAPTURE_CAPTAIN not in o_acts
    assert float(outs[-1]["percent"][1]) == 0.0
