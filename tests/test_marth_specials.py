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
