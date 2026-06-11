"""Marth character specials: decomp-anchored unit tests.

The replay (VictoriousSpitefulAlpaca) covers only air Shield Breaker fragments, the first air
side-B swing, and air Dolphin Slash - so every family here is verified against decomp/source
constants and the extracted MarsAttributes/movescripts instead.

Decomp sources:
- refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c  (Shield Breaker)
- refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c  (Dancing Blade)
- refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c (Dolphin Slash)
- refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c (Counter)
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base  # noqa: E402

B = 0x0200
SM_FALL = 20

# ftMs_MotionState action ids (341 base; see src/action_ids.h MSL_ACT_MS_*).
ACT_SB_START = 341
ACT_SB_LOOP = 342
ACT_SB_END0 = 343
ACT_SB_END1 = 344
ACT_SB_AIR_START = 345
ACT_SB_AIR_LOOP = 346
ACT_SB_AIR_END0 = 347
ACT_SB_AIR_END1 = 348
ACT_DB_S1 = 349
ACT_DB_S2_HI = 350
ACT_DB_S2_LW = 351
ACT_DB_S3_HI = 352
ACT_DB_S3_S = 353
ACT_DB_S3_LW = 354
ACT_DB_S4_HI = 355
ACT_DB_S4_S = 356
ACT_DB_S4_LW = 357
ACT_DB_AIR_S1 = 358
ACT_DS_GROUND = 367
ACT_DS_AIR = 368
ACT_COUNTER = 369
ACT_COUNTER_HIT = 370
ACT_COUNTER_AIR = 371
ACT_COUNTER_AIR_HIT = 372
ACT_FALL_SPECIAL = 0x0023
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_WAIT = 0x000E
ACT_FALL = 0x001D


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "marth.json").read_text())


def _specials() -> dict:
    return json.loads((ROOT / "data" / "moves" / "marth.json").read_text())["specials_by_msid"]


def _air_seed():
    seed = _seed_base("marth", grounded=False, pos_y=600.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    return seed


# ---------------------------------------------------------------------------
# Dolphin Slash
# ---------------------------------------------------------------------------


def test_ds_ground_enters_launches_and_fallspecials() -> None:
    # Ground up-B: enter 367, the script launch (cmd0 at frame 6) goes airborne with upward
    # velocity, then the anim end hands off to FallSpecial (ftCo_80096900 with x28/x2C).
    seed = _seed_base("marth")
    press = _mk_inputs(buttons=B, main_y=127)
    outs = _run(seed, [press] + [_mk_inputs()] * 60)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_DS_GROUND, acts[:4]
    # Rising frames must exist (the anim root motion drives a sharp upward launch).
    vys = [float(o["speed_y_self"][0]) for o in outs]
    assert max(vys[:20]) > 1.0, f"never launched upward: {vys[:12]}"
    airborne = [int(o["on_ground"][0]) for o in outs]
    assert 0 in airborne[:12], "never left the ground"
    assert ACT_FALL_SPECIAL in acts, f"no FallSpecial handoff: {sorted(set(acts))}"


def test_ds_air_launch_and_landing_lag() -> None:
    # Air up-B from a fall: enter 368, launch upward, descend in special-fall, land into
    # LandingFallSpecial whose duration tracks specialhi_landing_lag_frames (34).
    a = _attrs()
    seed = _seed_base("marth", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    press = _mk_inputs(buttons=B, main_y=127)
    outs = _run(seed, [press] + [_mk_inputs()] * 120)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_DS_AIR, acts[:4]
    vys = [float(o["speed_y_self"][0]) for o in outs]
    assert max(vys[:20]) > 1.0, f"air DS never rose: {vys[:12]}"
    assert ACT_LANDING_FALL_SPECIAL in acts, f"never landed into FallSpecial lag: {sorted(set(acts))}"
    lag = sum(1 for x in acts if x == ACT_LANDING_FALL_SPECIAL)
    expect = float(a["specialhi_landing_lag_frames"])
    assert abs(lag - expect) <= 3.0, f"DS landing lag {lag} vs attr {expect}"


def test_ds_air_entry_velocity_mul() -> None:
    # ftMs_SpecialAirHi_Enter: vel.y zeroed, vel.x *= specialhi_air_entry_vel_x_mul.
    a = _attrs()
    seed = _air_seed()
    seed["speed_air_x_self"][0, 0] = np.float32(1.0)
    press = _mk_inputs(buttons=B, main_y=127)
    outs = _run(seed, [press])
    vx = float(outs[0]["speed_air_x_self"][0])
    # The entry multiplier applies before the first frame's physics.
    assert abs(vx) <= abs(1.0 * float(a["specialhi_air_entry_vel_x_mul"])) + 0.2, (
        f"air DS entry vx {vx} vs mul {a['specialhi_air_entry_vel_x_mul']}"
    )


# ---------------------------------------------------------------------------
# Shield Breaker
# ---------------------------------------------------------------------------


def test_sb_ground_uncharged_quick_release() -> None:
    # B tap: Start -> Loop -> End0 (B released) -> Wait. End0's hitboxes carry the charge
    # formula base damage (7 at zero seconds).
    seed = _seed_base("marth")
    tap = _mk_inputs(buttons=B)
    outs = _run(seed, [tap, tap] + [_mk_inputs()] * 80)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_SB_START
    assert ACT_SB_END0 in acts, f"no normal release: {sorted(set(acts))}"
    assert ACT_SB_END1 not in acts
    assert ACT_WAIT in acts, "never returned to Wait"


def test_sb_full_charge_forces_end1() -> None:
    # Holding B past specialn_charge_max_seconds * 30 loop frames forces the End1
    # (shield-breaker) release (doLoopAnim: cur_frame > x0*30 -> cmd0=1).
    a = _attrs()
    hold_frames = int(a["specialn_charge_max_seconds"]) * 30 + 40
    seed = _seed_base("marth")
    hold = _mk_inputs(buttons=B)
    outs = _run(seed, [hold] * hold_frames + [_mk_inputs()] * 40)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SB_LOOP in acts
    assert ACT_SB_END1 in acts, f"full charge never forced End1: {sorted(set(acts))}"
    assert ACT_SB_END0 not in acts


def test_sb_charge_scaled_damage_applies_to_victim() -> None:
    # Charge ~2 seconds, release into a point-blank victim: applied damage tracks
    # base + seconds * per_second (ftColl_8007ABD0 override on End0 hitboxes).
    a = _attrs()
    charge_frames = 65  # past the Start anim; ~2s of Loop
    seed = _seed_base("marth")
    seed["pos_x"][0, 1] = np.float32(8.0)
    seed["facing"][0, 1] = np.uint8(0)
    hold = _mk_inputs(buttons=B)
    outs = _run(seed, [hold] * charge_frames + [_mk_inputs()] * 50)
    hit = [o for o in outs if float(o["percent"][1]) > 0.0]
    assert hit, "charged SB never connected"
    dmg = float(hit[0]["percent"][1])
    base = float(a["specialn_release_damage_base"])
    per = float(a["specialn_release_damage_per_second"])
    # Loop time = charge_frames minus the Start anim duration; allow a +-1s window on the
    # second-quantized scaling, but require strictly more than the uncharged base.
    assert dmg > base - 0.01, f"SB damage {dmg} below base {base}"
    assert dmg <= base + 4 * per + 0.01, f"SB damage {dmg} beyond max"
    assert (dmg - base) % per < 0.011 or per - ((dmg - base) % per) < 0.011, (
        f"SB damage {dmg} not on the base+{per}/s ladder"
    )


def test_sb_air_family_states() -> None:
    seed = _air_seed()
    tap = _mk_inputs(buttons=B)
    outs = _run(seed, [tap, tap] + [_mk_inputs()] * 70)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_SB_AIR_START
    assert ACT_SB_AIR_END0 in acts, f"air SB no release: {sorted(set(acts))}"
    assert ACT_FALL in acts, "air SB never exited to Fall"


# ---------------------------------------------------------------------------
# Dancing Blade
# ---------------------------------------------------------------------------


def _db_window(stage_msid: int) -> tuple[int, int]:
    evs = _specials()[str(stage_msid)]["events"]
    on = next(e["frame"] for e in evs if e["kind"] == "set_cmd_var" and e["data"]["idx"] == 0 and e["data"]["value"] == 1)
    offs = [e["frame"] for e in evs if e["kind"] == "set_cmd_var" and e["data"]["idx"] == 0 and e["data"]["value"] == 0 and e["frame"] > 0]
    return on, (offs[0] if offs else 999)


def test_db_chain_all_four_stages_neutral() -> None:
    # Ground side-B, then B inside each stage's cmd0 window (per-stage data: S1 msid 303,
    # S2Lw 305, S3S 307): S1 -> S2Lw -> S3S -> S4S.
    on1, _ = _db_window(303)
    on2, _ = _db_window(305)
    on3, _ = _db_window(307)
    seed = _seed_base("marth")
    script = [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * on1 + [_mk_inputs(buttons=B)]
    script += [_mk_inputs()] * on2 + [_mk_inputs(buttons=B)]
    script += [_mk_inputs()] * on3 + [_mk_inputs(buttons=B)]
    script += [_mk_inputs()] * 90
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_DB_S1
    assert ACT_DB_S2_LW in acts, f"stage 2 never chained: {sorted(set(acts))}"
    assert ACT_DB_S3_S in acts, f"stage 3 never chained: {sorted(set(acts))}"
    assert ACT_DB_S4_S in acts, f"stage 4 never chained: {sorted(set(acts))}"
    assert ACT_WAIT in acts, "never returned to Wait after stage 4"


def test_db_stage2_up_variant() -> None:
    on1, _ = _db_window(303)
    seed = _seed_base("marth")
    script = [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * on1 + [
        _mk_inputs(buttons=B, main_y=127)
    ] + [_mk_inputs()] * 30
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DB_S2_HI in acts, f"up variant never selected: {sorted(set(acts))}"


def test_db_stage3_down_variant() -> None:
    on1, _ = _db_window(303)
    seed = _seed_base("marth")
    on2, _ = _db_window(305)
    script = [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * on1 + [
        _mk_inputs(buttons=B)
    ] + [_mk_inputs()] * on2 + [_mk_inputs(buttons=B, main_y=-127)] + [_mk_inputs()] * 30
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DB_S3_LW in acts, f"down variant never selected: {sorted(set(acts))}"


def test_db_no_chain_without_press() -> None:
    # Without follow-up presses, S1 plays out and returns to Wait; no later stages.
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * 60)
    acts = set(int(o["action_id"][0]) for o in outs)
    assert ACT_DB_S1 in acts
    for st in (ACT_DB_S2_HI, ACT_DB_S2_LW, ACT_DB_S3_S, ACT_DB_S4_S):
        assert st not in acts, f"stage {st} reached without input"
    assert ACT_WAIT in acts


def test_db_air_first_swing_has_hop_once() -> None:
    # First air side-B of an airtime pops vel.y to specials_air_entry_vel_y (x222C gate).
    a = _attrs()
    seed = _air_seed()
    outs = _run(seed, [_mk_inputs(buttons=B, main_x=127)])
    vy = float(outs[0]["speed_y_self"][0])
    expect = float(a["specials_air_entry_vel_y"])
    grav = float(a["specials_fall_accel"])
    assert expect - 2.5 * grav <= vy <= expect + 1e-3, f"air DB hop vy {vy} vs {expect}"


# ---------------------------------------------------------------------------
# Counter (window state only here; combat trigger covered in the counter combat tests)
# ---------------------------------------------------------------------------


def test_counter_window_opens_and_closes_with_script() -> None:
    # The Counter intercept window follows the script cmd1 pulses ([5, 30) on msid 323):
    # outside the window a point-blank jab hits Marth normally.
    evs = _specials()["323"]["events"]
    on = next(e["frame"] for e in evs if e["kind"] == "set_cmd_var" and e["data"]["idx"] == 1 and e["data"]["value"] == 1)
    off = next(e["frame"] for e in evs if e["kind"] == "set_cmd_var" and e["data"]["idx"] == 1 and e["data"]["value"] == 0)
    assert 0 < on < off


def test_counter_states_enter_and_exit() -> None:
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-127)] + [_mk_inputs()] * 70)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_COUNTER, acts[:4]
    assert ACT_WAIT in acts, f"counter never returned to Wait: {sorted(set(acts))}"


# ---------------------------------------------------------------------------
# Counter combat integration
# ---------------------------------------------------------------------------


def _counter_combat_run(press_offset: int, n_tail: int = 40):
    """Marth down-Bs; fox (p1) jabs so first active frame lands press_offset frames after entry."""
    seed = _seed_base("marth")
    seed["pos_x"][0, 1] = np.float32(5.0)
    seed["facing"][0, 1] = np.uint8(0)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=_mk_inputs.__globals__["INPUT_DTYPE"])
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, inp.dtype.itemsize))

        rows = []
        script = [both(dict(buttons=B, main_y=-127), dict())]
        script += [both(dict(), dict())] * press_offset
        script += [both(dict(), dict(buttons=0x0100))]
        script += [both(dict(), dict())] * n_tail
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(_mk_inputs.__globals__["COMPARE_DTYPE"]).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    return rows


def test_counter_triggers_inside_window() -> None:
    # Fox jab active frame ~3 after press; counter window [5,30). Jab pressed at entry+4 ->
    # contact ~entry+7, inside the window: Marth takes NO damage and enters SpecialLwHit (370),
    # then the scripted counterattack damages fox.
    rows = _counter_combat_run(press_offset=4)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT in a0, f"counter never triggered: {sorted(set(a0))}"
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "marth took damage through counter"
    assert any(float(r["percent"][1]) > 0.0 for r in rows), "counterattack never hit fox"


def test_counter_whiffs_outside_window() -> None:
    # Jab pressed late enough that contact lands after the window closes (30): Marth gets hit.
    rows = _counter_combat_run(press_offset=33)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT not in a0, "counter triggered outside window"
    assert any(float(r["percent"][0]) > 0.0 for r in rows), "marth was never hit post-window"


def test_counter_return_damage_is_scripted_not_scaled() -> None:
    # Marth's counterattack damage = the LwHit movescript's authored value (the x5C scaling is
    # the Roy/Emblem branch in ftMs_SpecialLwHit_Anim).
    evs = _specials()["324"]["events"]
    script_dmg = next(
        float(e["data"]["hitbox"]["damage"]) for e in evs if e["kind"] == "create_hitbox"
    )
    rows = _counter_combat_run(press_offset=4)
    hits = [float(r["percent"][1]) for r in rows if float(r["percent"][1]) > 0.0]
    assert hits, "counterattack never connected"
    assert abs(hits[0] - script_dmg) < 0.05, f"counter dmg {hits[0]} vs script {script_dmg}"


def test_counter_triggers_on_back_hit() -> None:
    # The intercept descriptor is a bone-anchored sphere (AbsorbDesc bone 3, r=8.5), not a
    # facing-gated shield: an attack from behind during the window must also be countered.
    seed = _seed_base("marth")
    seed["pos_x"][0, 1] = np.float32(-5.0)  # attacker BEHIND marth (marth faces +x)
    seed["facing"][0, 1] = np.uint8(1)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=_mk_inputs.__globals__["INPUT_DTYPE"])
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, inp.dtype.itemsize))

        rows = []
        script = [both(dict(buttons=B, main_y=-127), dict())] + [both(dict(), dict())] * 4
        script += [both(dict(), dict(buttons=0x0100))] + [both(dict(), dict())] * 30
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(_mk_inputs.__globals__["COMPARE_DTYPE"]).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT in a0, f"back hit not countered: {sorted(set(a0))}"
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "marth took damage through counter"


def test_counter_fails_closed_without_poseable_descriptor() -> None:
    # FAIL-CLOSED proof: seed Marth directly into SpecialLw with the Slippi no-submotion
    # sentinel anim (0xFFFFFFFF). The descriptor sphere cannot be posed, so a point-blank jab
    # must NOT be countered through any legacy body-contact admission - Marth takes the hit.
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(ACT_COUNTER)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["pos_x"][0, 1] = np.float32(5.0)
    seed["facing"][0, 1] = np.uint8(0)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=_mk_inputs.__globals__["INPUT_DTYPE"])
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, inp.dtype.itemsize))

        rows = []
        script = [both(dict(), dict(buttons=0x0100))] + [both(dict(), dict())] * 15
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(_mk_inputs.__globals__["COMPARE_DTYPE"]).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    a0 = [int(r["action_id"][0]) for r in rows]
    # Fail-closed: with an unposeable descriptor the intercept must never fire. (The sentinel
    # anim also yields no hurtcaps, so there is no body contact either - the row is inert, which
    # is exactly the closed behavior: no counter through any legacy body-contact admission.)
    assert ACT_COUNTER_HIT not in a0, f"countered without a poseable descriptor: {sorted(set(a0))}"
    assert ACT_COUNTER_AIR_HIT not in a0


# ---------------------------------------------------------------------------
# Dancing Blade collision behavior (completeness pass)
# ---------------------------------------------------------------------------

ACT_DB_AIR_S2_LW = 360


def test_db_ground_stops_at_ledge() -> None:
    # ftMs_Special*_Coll grounded branch routes through ft_800827A0 (StopAtLedge): sliding into
    # the FD edge during a ground Dancing Blade clamps at the ledge and never walks off.
    # Data-driven via the MSLMSO01 FT800827A0_EDGE_SNAP_COLL class on the ftMs stage callbacks.
    seed = _seed_base("marth")
    seed["pos_x"][0, 0] = np.float32(80.0)
    outs = _run(seed, [_mk_inputs(main_x=127)] * 3 + [_mk_inputs(buttons=B, main_x=127)] + [
        _mk_inputs()
    ] * 45)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DB_S1 in acts, f"never entered ground DB: {sorted(set(acts))}"
    i = acts.index(ACT_DB_S1)
    xs = [float(o["pos_x"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    db_frames = [j for j in range(i, len(acts)) if acts[j] == ACT_DB_S1]
    assert max(xs[j] for j in db_frames) <= 85.5667, f"walked past the ledge: {max(xs):.3f}"
    assert all(og[j] == 1 for j in db_frames), "ground DB became airborne at the edge"


def test_db_air_stage_landing_swaps_to_ground_stage_preserving_frame() -> None:
    # ftMs_SpecialS2_Coll air branch: ft_80081D0C ground contact swaps the air stage to the
    # grounded variant at the preserved animation frame (no Landing action in between).
    seed = _seed_base("marth", grounded=False, pos_y=14.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    on1, _ = _db_window(312)
    script = [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * on1 + [_mk_inputs(buttons=B)]
    script += [_mk_inputs()] * 50
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DB_AIR_S2_LW in acts, f"never chained to air S2: {sorted(set(acts))}"
    assert ACT_DB_S2_LW in acts, f"landing never swapped to ground S2: {sorted(set(acts))}"
    j = acts.index(ACT_DB_S2_LW)
    # Frame preservation: the swap must not restart the stage; the grounded continuation is
    # short (the stage was mid-anim), never routing through Landing (0x2A).
    assert 0x002A not in acts, "generic Landing interposed in the stage swap"
    k = j
    while k < len(acts) and acts[k] == ACT_DB_S2_LW:
        k += 1
    assert k - j < 25, f"ground stage restarted instead of preserving frame ({k - j} frames)"


# ---------------------------------------------------------------------------
# Counter projectile/item intercept (completeness pass)
# ---------------------------------------------------------------------------


def _laser_counter_run(counter_delay: int):
    """Falco (p1) lasers from x=25; Marth (p0) down-Bs `counter_delay` frames later."""
    seed = _seed_base("marth")
    seed["char_id"][0, 1] = np.uint8(22)  # falco
    seed["pos_x"][0, 1] = np.float32(25.0)
    seed["facing"][0, 1] = np.uint8(0)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=_mk_inputs.__globals__["INPUT_DTYPE"])
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, inp.dtype.itemsize))

        rows = []
        script = [both(dict(), dict(buttons=B))]
        script += [both(dict(), dict())] * counter_delay
        script += [both(dict(buttons=B, main_y=-127), dict())]
        script += [both(dict(), dict())] * 45
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(_mk_inputs.__globals__["COMPARE_DTYPE"]).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    return rows


def test_counter_intercepts_projectile() -> None:
    # A Falco laser arriving inside the counter window is consumed by the descriptor: Marth
    # takes no damage and enters SpecialLwHit. The intercept rides combat_apply_item_hit, so it
    # is generic over projectile owners (no item-type special-casing).
    rows = _laser_counter_run(counter_delay=1)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT in a0, f"laser never countered: {sorted(set(a0))}"
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "marth took laser damage"


def test_counter_projectile_whiffs_after_window() -> None:
    # Counter pressed so late the laser arrives before the window arms: Marth takes the laser.
    rows = _laser_counter_run(counter_delay=30)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT not in a0, "countered outside the live window"
    assert any(float(r["percent"][0]) > 0.0 for r in rows), "laser never connected at all"


def test_counter_hitlag_floor_from_shield_strength() -> None:
    # AbsorbDesc shield strength (x60 = 11) floors counter hitlag on BOTH fighters:
    # a weak jab (CalcHitlag ~4f) frozen for 11 frames on the counter contact.
    # refs/melee/src/melee/ft/fighter.c (x1964 hitlag floor), ftcoll.c shield_unk0 tail
    rows = _counter_combat_run(press_offset=4)
    hl0 = max(int(r["hitlag"][0]) for r in rows)
    hl1 = max(int(r["hitlag"][1]) for r in rows)
    a = _attrs()
    floor = int(a["speciallw_counter_shield_strength"])
    assert hl0 >= floor, f"marth counter hitlag {hl0} below x60 floor {floor}"
    assert hl1 >= floor, f"attacker counter hitlag {hl1} below x60 floor {floor}"


# ---------------------------------------------------------------------------
# Dynamic-chain suppression proof (completeness pass)
# ---------------------------------------------------------------------------


def test_marth_hurtcaps_do_not_ride_suppressed_dynamic_chains() -> None:
    # The SSDYNN01 suppression for Marth (3 cape/hair chains, 12 nodes) is source-correct, not
    # an approximation: (1) every hurtcap bone's ancestor lineage is fully static (never passes
    # through a chain node), and (2) the chains define zero collision capsules (unlike Fox's
    # tail chain). Encoded as a data assertion so a future costume/extraction change that
    # violates either premise fails loudly.
    sys.path.insert(0, str(ROOT))
    import tools.extraction.extract_fighter_anims as ea

    _, _, _, parent, _ = ea._read_rest_srt_and_parents("marth")
    dyn = ea._read_fighter_dynamics("marth")
    assert len(dyn) == 3, f"marth chain count changed: {len(dyn)}"
    chain_nodes = set()
    for d in dyn:
        assert d["colliders"] == [], f"marth chain gained colliders: {d['colliders']}"
        chain_nodes.update(
            ea._dynamic_first_child_chain(int(d["root_part"]), int(d["chain_count"]), parent)
        )
    hurt = json.loads((ROOT / "data" / "hurtcaps" / "marth.json").read_text())
    for cap in hurt["capsules"]:
        cur = int(cap["bone_idx"])
        while cur >= 0:
            assert cur not in chain_nodes, (
                f"hurtcap bone {cap['bone_idx']} rides dynamic chain node {cur}"
            )
            cur = parent[cur]


def _bf_laser_counter_run(marth_high: bool):
    """Battlefield: the laser passes a platform level away from Marth's counter descriptor."""
    seed = _seed_base("marth")
    seed["stage_id"][0] = np.uint32(31)
    seed["char_id"][0, 1] = np.uint8(22)
    if marth_high:
        # Marth counters on the left platform; falco fires along the ground below.
        seed["pos_x"][0, 0] = np.float32(-57.6)
        seed["pos_y"][0, 0] = np.float32(27.2)
        seed["ground_id"][0, 0] = np.uint16(1)
        seed["pos_x"][0, 1] = np.float32(-30.0)
        seed["facing"][0, 1] = np.uint8(0)
    else:
        # Falco fires from the left platform; the laser passes above grounded Marth.
        seed["pos_x"][0, 0] = np.float32(-40.0)
        seed["pos_x"][0, 1] = np.float32(-70.0)
        seed["pos_y"][0, 1] = np.float32(27.2)
        seed["ground_id"][0, 1] = np.uint16(1)
        seed["facing"][0, 1] = np.uint8(1)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=_mk_inputs.__globals__["INPUT_DTYPE"])
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, inp.dtype.itemsize))

        rows = []
        script = [both(dict(), dict(buttons=B))]
        script += [both(dict(buttons=B, main_y=-127), dict())]
        script += [both(dict(), dict())] * 45
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(_mk_inputs.__globals__["COMPARE_DTYPE"]).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    return rows


def test_counter_does_not_intercept_projectile_above_descriptor() -> None:
    # Descriptor-geometry negative: a laser passing a platform height ABOVE Marth's armed
    # counter must not intercept (it misses the bone-3 sphere entirely). This fails if the
    # intercept uses a proxy Y instead of the real projectile Y.
    rows = _bf_laser_counter_run(marth_high=False)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT not in a0, "counter intercepted a projectile far above the descriptor"
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "overhead laser somehow hit marth"


def test_counter_does_not_intercept_projectile_below_descriptor() -> None:
    rows = _bf_laser_counter_run(marth_high=True)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT not in a0, "counter intercepted a projectile far below the descriptor"
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "underpass laser somehow hit marth"


# ---------------------------------------------------------------------------
# Ground <-> air swap family coverage (completeness pass)
# ---------------------------------------------------------------------------

ACT_SB_AIR_LOOP_ = 346


def _seed_into_special(action: int, grounded: bool, pos_y: float, frame: float = 0.0):
    seed = _seed_base("marth", grounded=grounded, pos_y=pos_y)
    seed["action_id"][0, 0] = np.uint16(action)
    seed["animation_index"][0, 0] = np.uint32(295 + (action - 341))
    if frame:
        seed["action_frame"][0, 0] = np.int16(int(frame))
    return seed


def test_sb_air_loop_landing_swaps_to_ground_loop() -> None:
    # ftMs_SpecialAirNLoop_Coll: ft_80081D0C ground contact swaps to the grounded Loop at the
    # preserved frame (no Landing action).
    seed = _seed_base("marth", grounded=False, pos_y=4.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    hold = _mk_inputs(buttons=B)
    outs = _run(seed, [hold] * 40)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SB_AIR_START in acts, f"never entered air SB: {sorted(set(acts))}"
    assert ACT_SB_LOOP in acts or ACT_SB_START in acts, (
        f"air SB never swapped to the grounded family on landing: {sorted(set(acts))}"
    )
    assert 0x002A not in acts, "generic Landing interposed in the SB swap"


def test_counter_air_landing_swaps_to_ground_counter() -> None:
    # ftMs_SpecialAirLw_Coll: ground contact swaps 371 -> 369 at the preserved frame; the
    # script window keeps running on the grounded variant.
    seed = _seed_base("marth", grounded=False, pos_y=3.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-127)] + [_mk_inputs()] * 50)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_COUNTER_AIR in acts, f"never entered air counter: {sorted(set(acts))}"
    assert ACT_COUNTER in acts, f"landing never swapped to ground counter: {sorted(set(acts))}"
    assert 0x002A not in acts, "generic Landing interposed in the counter swap"


def test_sb_ground_walkoff_swaps_to_air_variant_preserving_frame() -> None:
    # Ground->air floor-loss swap, exercised end-to-end through the one genuinely reachable
    # route on static stages: Shield Breaker's grounded colls use ft_80082708 (walk-off
    # ALLOWED, unlike the DB/Counter ft_800827A0 stop-at-ledge class), so dash momentum can
    # carry a grounded SB over the FD edge. The swap enters SpecialAirNStart at the preserved
    # frame, never generic Fall first.
    #
    # DB stages and ground Counter share the same swap helper/wiring but their EDGE_SNAP
    # collision class makes grounded floor loss unreachable except via Stadium transform floor
    # removal, which has no synthetic seed surface; their mappings are covered by the
    # air->ground direction tests above.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::{ftMs_SpecialNStart_Coll,
    #   ftMs_SpecialN_80136A1C}
    seed = _seed_base("marth")
    seed["pos_x"][0, 0] = np.float32(83.0)
    outs = _run(seed, [_mk_inputs(main_x=127)] + [_mk_inputs(buttons=B)] * 26)
    acts = [int(o["action_id"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert ACT_SB_START in acts, f"never entered grounded SB: {sorted(set(acts))}"
    i = acts.index(ACT_SB_START)
    assert og[i] == 1, "SB entry was not grounded"
    assert ACT_SB_AIR_START in acts, f"walk-off never swapped to air SB: {sorted(set(acts))}"
    j = acts.index(ACT_SB_AIR_START)
    assert ACT_FALL not in acts[: j + 1], f"generic Fall interposed: {acts[:j+1]}"
    assert og[j] == 0, "air SB swap row still grounded"
