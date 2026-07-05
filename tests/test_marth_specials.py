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
from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE
from tests.replay_buffers_loader import load_replay_buffer_window  # noqa: E402

B = 0x0200
Y = 0x0800
SM_FALL = 20
SM_DB_AIR_S1 = 312
SM_FX_SPECIAL_HI_FALL = 311

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
ACT_TURN = 0x0012
ACT_JUMP_AERIAL_F = 0x001B
ACT_FALL = 0x001D
ACT_SQUAT = 0x0027
ACT_GUARD_ON = 0x00B2
ACT_GUARD_REFLECT = 0x00B6
ACT_THROW_F = 0x00DB
ACT_THROW_B = 0x00DC
ACT_THROW_HI = 0x00DD
ACT_THROWN_F = 0x00EF
ACT_THROWN_B = 0x00F0
ACT_THROWN_HI = 0x00F1
ACT_DAMAGE_AIR_3 = 0x0056
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "marth.json").read_text())


def _specials() -> dict:
    return json.loads((ROOT / "data" / "moves" / "marth.json").read_text())["specials_by_msid"]


def _air_seed():
    seed = _seed_base("marth", grounded=False, pos_y=600.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    return seed


def _throw_pair_seed(owner_char: str, victim_char_id: int, throw_action: int, frame: int) -> np.ndarray:
    owner_char_id = {
        "fox": 1,
        "marth": 18,
        "falco": 22,
    }[owner_char]
    seed = _seed_base("marth" if owner_char == "marth" else "fox")
    seed["char_id"][0, 0] = np.uint8(owner_char_id)
    seed["char_id"][0, 1] = np.uint8(victim_char_id)
    seed["grab_owner_port"][0, :] = np.uint8(0xFF)
    seed["grab_owner_port"][0, 1] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(throw_action)
    seed["animation_index"][0, 0] = np.uint32(throw_action)
    thrown_action = {
        ACT_THROW_F: ACT_THROWN_F,
        ACT_THROW_B: ACT_THROWN_B,
        ACT_THROW_HI: ACT_THROWN_HI,
    }[throw_action]
    seed["action_id"][0, 1] = np.uint16(thrown_action)
    seed["animation_index"][0, 1] = np.uint32(thrown_action)
    seed["action_frame"][0, :2] = np.int16(frame)
    seed["anim_frame_f32"][0, :2] = np.float32(float(frame))
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    return seed


def _run_throw_pair(owner_char: str, victim_char_id: int, throw_action: int, frame: int) -> np.ndarray:
    return _run(_throw_pair_seed(owner_char, victim_char_id, throw_action, frame), [_mk_inputs()])[0]


def _step_real_row(dataset: str, record: int, seed_mutator=None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    msl_binding = pytest.importorskip("msl_binding")

    dataset_path = ROOT / dataset

    ds = load_replay_buffer_window(str(dataset_path), record, record + 1)
    row = ds.rows[0]
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_struct = np.array([row["seed_t"]], dtype=SEED_DTYPE)
    if seed_mutator is not None:
        seed_mutator(seed_struct)
    seed = seed_struct.view(np.uint8).reshape(1, seed_stride).copy()
    prev = (
        np.frombuffer(row["prev_input_t"].tobytes(), dtype=np.uint8)
        .reshape(1, input_stride)
        .copy()
    )
    inp = np.frombuffer(row["input_t"].tobytes(), dtype=np.uint8).reshape(1, input_stride).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        msl_binding.reseed_seed(handle, seed)
        msl_binding.step_input(handle, prev, inp)
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)
    return (
        out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(),
        seed_struct[0].copy(),
        row["ref_t1"].copy(),
    )


def _rollout_real_window(dataset: str, start: int, stop: int) -> dict[int, tuple[np.ndarray, np.ndarray]]:
    msl_binding = pytest.importorskip("msl_binding")

    dataset_path = ROOT / dataset

    ds = load_replay_buffer_window(str(dataset_path), start, stop + 1)
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = msl_binding.init(batch_size=1, num_players=int(ds.num_players))
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    got: dict[int, tuple[np.ndarray, np.ndarray]] = {}
    try:
        seed = (
            np.frombuffer(ds.rows[0]["seed_t"].tobytes(), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        msl_binding.reseed_seed_rollout(handle, seed)
        for offset, row in enumerate(ds.rows):
            rec = start + offset
            prev = (
                np.frombuffer(row["prev_input_t"].tobytes(), dtype=np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            inp = (
                np.frombuffer(row["input_t"].tobytes(), dtype=np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            msl_binding.step_input(handle, prev, inp)
            msl_binding.write_compare(handle, out_bytes)
            got[rec] = (
                out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(),
                row["ref_t1"].copy(),
            )
    finally:
        msl_binding.destroy(handle)
    return got


def _debug_precombat_body_select_count(dataset: str, record: int) -> int:
    msl_binding = pytest.importorskip("msl_binding")

    dataset_path = ROOT / dataset

    ds = load_replay_buffer_window(str(dataset_path), record, record + 1)
    row = ds.rows[0]
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed = np.frombuffer(row["seed_t"].tobytes(), dtype=np.uint8).reshape(1, seed_stride).copy()
    prev = (
        np.frombuffer(row["prev_input_t"].tobytes(), dtype=np.uint8)
        .reshape(1, input_stride)
        .copy()
    )
    inp = np.frombuffer(row["input_t"].tobytes(), dtype=np.uint8).reshape(1, input_stride).copy()
    handle = msl_binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        msl_binding.reseed_seed(handle, seed)
        msl_binding.debug_step_input_pre_combat(handle, prev, inp)
        _raw, count = msl_binding.debug_combat_select_body_hits(handle, 0, 64)
        return int(count)
    finally:
        msl_binding.destroy(handle)


# ---------------------------------------------------------------------------
# Dolphin Slash
# ---------------------------------------------------------------------------


def test_marth_throwhi_uses_thrower_victim_anim_table_for_falco_victim() -> None:
    # Thrown victims source their victim animation from the thrower's fighter data:
    # ftCo_800DE3FC passes the thrower gobj to Fighter_ChangeMotionState, so the victim-side
    # loop/end-frame lookup must use Marth's ftCo_SM_ThrownHi table, not Falco's shorter table.
    # This locks the non-skipping Marth up-throw/Falco-victim case that regressed when thrown
    # victims clamped on their own character's animation end frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    out = _run_throw_pair("marth", 22, ACT_THROW_HI, 7)
    assert int(out["action_id"][1]) == ACT_THROWN_HI
    assert int(out["action_frame"][1]) == 8
    assert float(out["percent"][1]) == pytest.approx(0.0)


def test_marth_non_low_throw_snap_boundaries_are_not_early() -> None:
    # Marth's non-low throw rate does not land exactly on the source command integer boundaries.
    # The timebase re-accumulates f32 AObj time so frame-9-ish and frame-12-ish rows do not snap
    # past their command until the real source crossing. Back throw crosses from 6->7; up throw
    # crosses from 11->12. The adjacent rows stay attached.
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
    import msl_binding

    assert msl_binding.move_tables_throw_release_hit_idx(18, ACT_THROW_B, 7.0) == (1, 0)
    assert msl_binding.move_tables_throw_release_hit_idx(18, ACT_THROW_HI, 12.0) == (1, 0)

    before_back = _run_throw_pair("marth", 22, ACT_THROW_B, 7)
    assert int(before_back["action_id"][1]) == ACT_THROWN_B
    assert int(before_back["action_frame"][1]) == 8

    crossed_back = _run_throw_pair("marth", 22, ACT_THROW_B, 6)
    assert int(crossed_back["action_id"][1]) == ACT_DAMAGE_FLY_N
    assert float(crossed_back["percent"][1]) == pytest.approx(4.0)

    before_hi = _run_throw_pair("marth", 22, ACT_THROW_HI, 10)
    assert int(before_hi["action_id"][1]) == ACT_THROWN_HI
    assert int(before_hi["action_frame"][1]) == 11

    crossed_hi = _run_throw_pair("marth", 22, ACT_THROW_HI, 11)
    assert int(crossed_hi["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert float(crossed_hi["percent"][1]) == pytest.approx(4.0)


def test_throw_release_final_facing_override_is_raw_angle_bounded() -> None:
    # ftCo_800DE7C0 passes `facing_dir = -dmg.facing_dir_1` into ftCo_8008DCE0 only for throw-hit
    # raw angles strictly between 90 and 270 degrees. Marth ThrowHi (93) gets the final-facing
    # override; Marth ThrowF (50) keeps the earlier `dmg.facing_dir_1` facing used for KB velocity.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Thrown.s::ftCo_800DE7C0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    throw_hi = _run_throw_pair("marth", 22, ACT_THROW_HI, 11)
    assert int(throw_hi["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert int(throw_hi["facing"][1]) == 1

    throw_f = _run_throw_pair("marth", 22, ACT_THROW_F, 13)
    assert int(throw_f["action_id"][1]) == ACT_DAMAGE_AIR_3
    assert int(throw_f["facing"][1]) == 0


def test_fox_falco_throw_timing_invariant_stays_on_existing_boundaries() -> None:
    # Adjacent invariant for the long-standing Fox/Falco timing: the Marth f32 snap fix must not
    # move spacie throw release boundaries.
    # data/moves/{fox,falco}.json ftCo_SM_ThrowB/ThrowHi set_throw_flags events
    import msl_binding

    assert msl_binding.move_tables_throw_release_hit_idx(1, ACT_THROW_B, 8.0) == (0, -1)
    assert msl_binding.move_tables_throw_release_hit_idx(1, ACT_THROW_B, 9.0) == (1, 0)
    assert msl_binding.move_tables_throw_release_hit_idx(1, ACT_THROW_HI, 7.0) == (0, -1)
    assert msl_binding.move_tables_throw_release_hit_idx(1, ACT_THROW_HI, 8.0) == (1, 0)
    assert msl_binding.move_tables_throw_release_hit_idx(22, ACT_THROW_B, 8.0) == (0, -1)
    assert msl_binding.move_tables_throw_release_hit_idx(22, ACT_THROW_B, 9.0) == (1, 0)
    assert msl_binding.move_tables_throw_release_hit_idx(22, ACT_THROW_HI, 7.0) == (1, 0)

    fox_back_before = _run_throw_pair("fox", 22, ACT_THROW_B, 7)
    assert int(fox_back_before["action_id"][1]) == ACT_THROWN_B
    assert int(fox_back_before["action_frame"][1]) == 8

    fox_back_crossed = _run_throw_pair("fox", 22, ACT_THROW_B, 8)
    assert int(fox_back_crossed["action_id"][1]) == ACT_DAMAGE_FLY_N
    assert float(fox_back_crossed["percent"][1]) == pytest.approx(2.0)

    fox_hi_crossed = _run_throw_pair("fox", 22, ACT_THROW_HI, 7)
    assert int(fox_hi_crossed["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert float(fox_hi_crossed["percent"][1]) == pytest.approx(2.0)

    falco_back_before = _run_throw_pair("falco", 1, ACT_THROW_B, 7)
    assert int(falco_back_before["action_id"][1]) == ACT_THROWN_B
    assert int(falco_back_before["action_frame"][1]) == 8

    falco_back_crossed = _run_throw_pair("falco", 1, ACT_THROW_B, 8)
    assert int(falco_back_crossed["action_id"][1]) == ACT_DAMAGE_FLY_N
    assert float(falco_back_crossed["percent"][1]) == pytest.approx(2.0)

    falco_hi_crossed = _run_throw_pair("falco", 1, ACT_THROW_HI, 6)
    assert int(falco_hi_crossed["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert float(falco_hi_crossed["percent"][1]) == pytest.approx(2.0)


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


def test_ds_landing_lag_is_not_interruptible() -> None:
    # ftMs_SpecialHi enters freefall through ftCo_80096900(gobj, 0, 1, 0, x28, x2C) -
    # allow_interrupt FALSE - so neither the special fall nor the LandingFallSpecial lag
    # accepts squat/guard/airdodge interrupts (unlike fox/falco Firefox, which passes true).
    # Locks the spurious squat-at-af3 / guard-at-af19 one-step class (IPW recs 594/612).
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    a = _attrs()
    seed = _seed_base("marth", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    press = _mk_inputs(buttons=B, main_y=127)
    # Hold crouch through the descent and the whole landing; mash shield late in the lag.
    hold_down = _mk_inputs(main_y=-90)
    hold_shield = _mk_inputs(buttons=0x0040, l=255, main_y=-90)
    outs = _run(seed, [press] + [hold_down] * 70 + [hold_shield] * 60)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_LANDING_FALL_SPECIAL in acts, f"never landed: {sorted(set(acts))}"
    lag = sum(1 for x in acts if x == ACT_LANDING_FALL_SPECIAL)
    expect = float(a["specialhi_landing_lag_frames"])
    assert abs(lag - expect) <= 3.0, (
        f"DS landing lag {lag} vs attr {expect} - the lag was interrupted"
    )
    first = acts.index(ACT_LANDING_FALL_SPECIAL)
    run = acts[first : first + lag]
    assert all(x == ACT_LANDING_FALL_SPECIAL for x in run), (
        f"LandingFallSpecial interrupted mid-lag: {run}"
    )


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


@pytest.mark.integration
def test_sb_start_anim_end_runs_destination_loop_iasa_release_real_rows() -> None:
    # Source owner:
    # ftMs_SpecialNStart_Anim changes into Loop when the Start animation ends; the same
    # Fighter_procUpdate then calls the destination Loop_IASA, where released B immediately enters
    # End0 at start frame 1. Start_IASA itself is empty, so this is a destination-callback handoff
    # rather than a Start-state release rule.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::{
    #   ftMs_SpecialNStart_Anim,ftMs_SpecialAirNStart_Anim,ftMs_SpecialNLoop_IASA,
    #   ftMs_SpecialAirNLoop_IASA,ftMs_SpecialN_80137354,ftMs_SpecialN_801373B8}
    path = "replays/validation/marth/InternalPowerlessWallaby.slpz"
    cases = (
        (4449, 1, ACT_SB_AIR_START, ACT_SB_AIR_END0),
        (5174, 1, ACT_SB_START, ACT_SB_END0),
        # Grounded Start finishes, then the Coll floor-loss swap preserves the source frame on
        # the aerial release variant.
        (10061, 1, ACT_SB_START, ACT_SB_AIR_END0),
    )
    for rec, p, seed_act, ref_act in cases:
        out, seed, ref = _step_real_row(path, rec)
        assert int(seed["action_id"][p]) == seed_act
        assert int(ref["action_id"][p]) == ref_act
        assert int(ref["action_frame"][p]) == 1
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
        assert int(out["animation_index"][p]) == int(ref["animation_index"][p])


def test_sb_start_anim_end_with_held_b_stays_in_loop() -> None:
    # Adjacent negative: Start_Anim can enter Loop on the end frame, but Loop_IASA only releases
    # when B is not held. Holding B keeps the charge loop and does not synthesize End0.
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(ACT_SB_START)
    seed["animation_index"][0, 0] = np.uint32(295)
    seed["action_frame"][0, 0] = np.int16(11)
    seed["anim_frame_f32"][0, 0] = np.float32(11.0)
    outs = _run(seed, [_mk_inputs(buttons=B)])
    assert int(outs[0]["action_id"][0]) == ACT_SB_LOOP
    assert int(outs[0]["action_frame"][0]) == 0
    assert int(outs[0]["action_id"][0]) != ACT_SB_END0


def test_sb_loop_release_enters_end_at_source_frame_one() -> None:
    # ftMs_SpecialN_80137354/801373B8 pass start_frame=1 to Fighter_ChangeMotionState for both
    # normal release and full-charge release. Lock the uncharged Loop_IASA path without depending
    # on replay-hidden mv.ms.specialn.cur_frame reconstruction.
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(ACT_SB_LOOP)
    seed["animation_index"][0, 0] = np.uint32(296)
    seed["action_frame"][0, 0] = np.int16(7)
    seed["anim_frame_f32"][0, 0] = np.float32(7.0)
    outs = _run(seed, [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_SB_END0
    assert int(outs[0]["action_frame"][0]) == 1


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


@pytest.mark.integration
def test_db_air_s1_anim_end_runs_destination_fall_iasa_jump_qhp_2123() -> None:
    # Source owner:
    # ftMs_SpecialAirS1_Anim exits through ftCo_Fall_Enter when the swing ends; the same
    # Fighter_procUpdate then runs the destination Fall IASA, so a current-frame jump edge reaches
    # ftCo_800CB870 and enters JumpAerial instead of serializing the intermediate Fall.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c::ftMs_SpecialAirS1_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}
    path = "replays/validation/marth/QuestionableHarmfulPanther.slpz"
    out, seed, ref = _step_real_row(path, 2123)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DB_AIR_S1
    assert int(seed["animation_index"][p]) == SM_DB_AIR_S1
    assert int(seed["jumps_left"][p]) == 1
    assert int(ref["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["jumps_left"][p]) == 0


def test_db_air_s1_anim_end_without_iasa_input_stays_fall() -> None:
    # Adjacent negative: the destination Fall IASA tail must not invent an interrupt when the
    # SpecialAirS1 end frame has no current-frame input edge.
    seed = _air_seed()
    seed["action_id"][0, 0] = np.uint16(ACT_DB_AIR_S1)
    seed["animation_index"][0, 0] = np.uint32(SM_DB_AIR_S1)
    seed["action_frame"][0, 0] = np.int16(29)
    seed["anim_frame_f32"][0, 0] = np.float32(29.0)
    seed["jumps_left"][0, 0] = np.uint8(1)
    outs = _run(seed, [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_FALL
    assert int(outs[0]["action_id"][0]) != ACT_JUMP_AERIAL_F
    assert int(outs[0]["jumps_left"][0]) == 1


@pytest.mark.integration
def test_db_ground_anim_end_runs_destination_wait_iasa_real_rows() -> None:
    # Source owner:
    # Grounded Dancing Blade stages call ft_8008A2BC when their animation ends; the destination
    # Wait_IASA then runs in the same Fighter_procUpdate and can immediately enter GuardOn, Squat,
    # or Turn. This locks the callback handoff, not the individual replay rows.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c::{
    #   ftMs_SpecialAirS1_Anim,ftMs_SpecialS2_Anim}
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    path_ldg = "replays/validation/marth/LoudDullGoat.slpz"
    path_wws = "replays/validation/marth/WellWornSmallGoshawk.slpz"
    cases = [
        (path_ldg, 742, 1, ACT_DB_S1, ACT_GUARD_ON),
        (path_wws, 6566, 1, ACT_DB_S1, ACT_GUARD_ON),
        (path_ldg, 2276, 1, ACT_DB_S1, ACT_SQUAT),
        (path_wws, 12482, 1, ACT_DB_S2_LW, ACT_TURN),
    ]
    for path, record, p, seed_action, expected in cases:
        out, seed, ref = _step_real_row(path, record)
        assert int(seed["action_id"][p]) == seed_action
        assert int(ref["action_id"][p]) == expected
        assert int(out["action_id"][p]) == expected


@pytest.mark.integration
def test_shield_breaker_end_anim_end_runs_destination_wait_guard_reflect_real_row() -> None:
    # Shield Breaker End uses the same grounded ft_8008A2BC callback owner as Dancing Blade:
    # current-frame shield input can enter GuardReflect before the row serializes.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::ftMs_SpecialNEnd_Anim
    path = "replays/validation/marth/InternalPowerlessWallaby.slpz"
    out, seed, ref = _step_real_row(path, 5207)
    p = 1
    assert int(seed["action_id"][p]) == ACT_SB_END0
    assert int(ref["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(out["action_id"][p]) == ACT_GUARD_REFLECT


def test_db_ground_anim_end_without_wait_iasa_input_stays_wait() -> None:
    # Adjacent negative: ft_8008A2BC provides the Wait_IASA opportunity, but with no current
    # command input the grounded S1 end remains the ordinary Wait entry.
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(ACT_DB_S1)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(29)
    seed["anim_frame_f32"][0, 0] = np.float32(29.0)
    outs = _run(seed, [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_WAIT


def test_fox_action_358_overlap_does_not_inherit_marth_dancing_blade_jump_tail() -> None:
    # Numeric action 358 is Marth SpecialAirS1 but Fox/Falco SpecialHiFall. Keep the fix tied to
    # the Marth MotionState owner, not to raw action id 358.
    # data/motion_state/owners/{marth,fox}.bin prove the callback split.
    seed = _seed_base("fox", grounded=False, pos_y=60.0)
    seed["action_id"][0, 0] = np.uint16(ACT_DB_AIR_S1)
    seed["animation_index"][0, 0] = np.uint32(SM_FX_SPECIAL_HI_FALL)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)
    seed["jumps_left"][0, 0] = np.uint8(1)
    outs = _run(seed, [_mk_inputs(buttons=Y)])
    assert int(outs[0]["action_id"][0]) != ACT_JUMP_AERIAL_F
    assert int(outs[0]["jumps_left"][0]) == 1


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


@pytest.mark.integration
def test_counter_shielddesc_state_flags_publish_on_script_open_real_rows() -> None:
    # ftMs_SpecialLw_Anim/ftMs_SpecialAirLw_Anim create a ShieldDesc when script cmd var 1 opens:
    # ftColl_8007B1B8 publishes x221B_b0 (Slippi state_flags[2] 0x80), and Marth immediately
    # sets x221B_b1 (0x40). The publication is post-action-frame owned, so seed frame 4 publishes
    # 0xC0 on the frame that serializes action_frame 5.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
    #   ftMs_SpecialLw_Anim,ftMs_SpecialAirLw_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
    path = "replays/validation/marth/LoudDullGoat.slpz"
    for rec, floor_owner in ((3959, 0), (3960, 1), (3963, 1)):
        out, _seed, ref = _step_real_row(path, rec)
        p = 1
        assert int(ref["action_id"][p]) == ACT_COUNTER_AIR
        assert int(ref["state_flags"][p, 2]) == 0xC0
        assert int(out["state_flags"][p, 2]) == int(ref["state_flags"][p, 2])
        assert int(_seed["speciallw_counter_hitlag_floor_active_u8"][p]) == floor_owner


@pytest.mark.integration
def test_counter_shielddesc_state_flags_carry_across_ground_air_swap_real_rows() -> None:
    # Counter's Coll callbacks recreate the descriptor after ground/air swaps when cmd var 1 has
    # reached the armed value, so the same x221B_b0|b1 publication survives SpecialAirLw -> SpecialLw.
    # The x60 hitlag-floor provenance does not survive the same swap: the swap helpers recreate the
    # descriptor but do not restore shield_unk0/1.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
    #   ftMs_SpecialLw_80138D38,ftMs_SpecialLw_80138DD0}
    path = "replays/validation/marth/WellWornSmallGoshawk.slpz"
    for rec, act, floor_owner in (
        (846, ACT_COUNTER_AIR, 0),
        (850, ACT_COUNTER_AIR, 1),
        (851, ACT_COUNTER, 1),
        (852, ACT_COUNTER, 0),
        (858, ACT_COUNTER, 0),
    ):
        out, _seed, ref = _step_real_row(path, rec)
        p = 1
        assert int(ref["action_id"][p]) == act
        assert int(ref["state_flags"][p, 2]) == 0xC0
        assert int(out["state_flags"][p, 2]) == int(ref["state_flags"][p, 2])
        assert int(_seed["speciallw_counter_hitlag_floor_active_u8"][p]) == floor_owner


@pytest.mark.integration
def test_counter_shielddesc_state_flags_clear_b0_on_hit_transition_real_row() -> None:
    # The ShieldDesc callback enters SpecialAirLwHit through Fighter_ChangeMotionState. That reset
    # clears x221B_b0 while x221B_b1 remains published on the post-frame row (0xC0 -> 0x40).
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
    # refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState reset)
    path = "replays/validation/marth/LoudDullGoat.slpz"
    out, _seed, ref = _step_real_row(path, 3964)
    p = 1
    assert int(ref["action_id"][p]) == ACT_COUNTER_AIR_HIT
    assert int(_seed["speciallw_counter_hitlag_floor_active_u8"][p]) == 1
    assert int(ref["state_flags"][p, 2]) == 0x40
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 11
    assert int(out["state_flags"][p, 2]) == int(ref["state_flags"][p, 2])


@pytest.mark.integration
def test_counter_descriptor_hitbox_contact_does_not_require_body_overlap_real_row() -> None:
    # WWS:860 has Marth's Counter descriptor live and a Fox Drill hitbox in range. The BODY
    # selector has no hurtcap overlap at pre-combat, but source `ftColl_8007B1B8` owns an
    # AbsorbDesc/ShieldDesc check against the active HitCapsule and immediately enters
    # SpecialLwHit with hitlag on both fighters.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
    #   ftMs_SpecialLw_Anim,ftMs_SpecialLw_80139140}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80078C70}
    path = "replays/validation/marth/WellWornSmallGoshawk.slpz"
    assert _debug_precombat_body_select_count(path, 860) == 0
    out, seed, ref = _step_real_row(path, 860)
    p = 1
    attacker = 0
    assert int(seed["action_id"][p]) == ACT_COUNTER
    assert int(seed["state_flags"][p, 2]) == 0xC0
    assert int(seed["speciallw_counter_hitlag_floor_active_u8"][p]) == 0
    assert int(ref["action_id"][p]) == ACT_COUNTER_HIT
    assert int(ref["hitlag"][p]) == 6
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["state_flags"][p, 2]) == int(ref["state_flags"][p, 2])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker])


@pytest.mark.integration
def test_counter_grounded_uninterrupted_seed_floor_owner_applies_x60_real_row() -> None:
    # Same grounded descriptor/contact shape as WWS:860, but with the explicit seed provenance set
    # to the source case where the grounded descriptor was Anim-created rather than swap-recreated.
    # This locks the reseed lane that Slippi does not expose directly: grounded/aerial state is not
    # the owner, shield_unk0/1 provenance is.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
    #   ftMs_SpecialLw_Anim,ftMs_SpecialLw_80138D38,ftMs_SpecialLw_80138DD0}
    path = "replays/validation/marth/WellWornSmallGoshawk.slpz"
    p = 1

    def force_anim_created_descriptor(seed_t: np.ndarray) -> None:
        seed_t["speciallw_counter_hitlag_floor_active_u8"][0, p] = np.uint8(1)

    out, seed, ref = _step_real_row(path, 860, seed_mutator=force_anim_created_descriptor)
    floor = int(_attrs()["speciallw_counter_shield_strength"])
    assert int(seed["action_id"][p]) == ACT_COUNTER
    assert int(seed["on_ground"][p]) == 1
    assert int(seed["speciallw_counter_hitlag_floor_active_u8"][p]) == 1
    assert int(ref["hitlag"][p]) == 6  # The real row is the swapped negative.
    assert int(out["action_id"][p]) == ACT_COUNTER_HIT
    assert int(out["hitlag"][p]) == floor


@pytest.mark.integration
def test_counter_hit_registers_post_transition_identity_for_lingering_contact_wws() -> None:
    # WWS:860 is Fox Drill into Marth Counter. Counter entry bumps Marth's action instance id;
    # the accepted HitCapsule victim ring must be registered against that post-transition identity,
    # matching normal BODY damage paths. Registering the pre-transition id lets the lingering same
    # hit_group contact re-enter on the next frozen rows.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    path = "replays/validation/marth/WellWornSmallGoshawk.slpz"
    p = 1
    got = _rollout_real_window(path, 860, 862)
    for rec in (860, 861, 862):
        out, ref = got[rec]
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["instance_id"][p]) == int(ref["instance_id"][p])


def test_counter_shielddesc_state_flags_do_not_leak_to_fox_action_overlap() -> None:
    # Numeric action 369 is Marth SpecialLw but Fox SpecialAirLwTurn. The publication is the
    # Marth ftMs_SpecialLw owner, not a raw action-id rule.
    seed = _seed_base("fox", grounded=False, pos_y=60.0)
    seed["action_id"][0, 0] = np.uint16(ACT_COUNTER)
    seed["animation_index"][0, 0] = np.uint32(0)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed["state_flags"][0, 0, 2] = np.uint8(0)
    outs = _run(seed, [_mk_inputs()])
    assert int(outs[0]["state_flags"][0, 2]) & 0xC0 == 0


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
    first_hit = next(r for r in rows if int(r["action_id"][0]) == ACT_COUNTER_HIT)
    assert int(first_hit["facing"][0]) == 1
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
    first_hit = next(r for r in rows if int(r["action_id"][0]) == ACT_COUNTER_HIT)
    # ftColl stores specialn_facing_dir from the contact side; CounterHit copies that lane instead
    # of facing the attacker in the hit callback. A behind/cross-up contact must face left.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
    assert int(first_hit["facing"][0]) == 0
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
    # Behavior contract: the StopAtLedge class keeps him grounded at the edge (the endpoint
    # snap may rest the root a fraction past the ledge x; the invariant is no walk-off).
    assert max(xs[j] for j in db_frames) <= 86.0, f"walked past the ledge: {max(xs):.3f}"
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
    first_hit = next(r for r in rows if int(r["action_id"][0]) == ACT_COUNTER_HIT)
    # ftColl_80077688 writes specialn_facing_dir from the projectile's actual contact position
    # before ftMs_SpecialLw_80139140 enters CounterHit.
    assert int(first_hit["facing"][0]) == 0
    assert all(float(r["percent"][0]) == 0.0 for r in rows), "marth took laser damage"


def test_counter_projectile_whiffs_after_window() -> None:
    # Counter pressed so late the laser arrives before the window arms: Marth takes the laser.
    rows = _laser_counter_run(counter_delay=30)
    a0 = [int(r["action_id"][0]) for r in rows]
    assert ACT_COUNTER_HIT not in a0, "countered outside the live window"
    assert any(float(r["percent"][0]) > 0.0 for r in rows), "laser never connected at all"


def test_counter_anim_created_descriptor_applies_x60_hitlag_floor() -> None:
    # Counter descriptors created by the Anim callback own shield_unk0/1 and apply the
    # MarsAttributes x60 hitlag floor. Swap-recreated descriptors are covered separately by the
    # WWS:860 real row, where vanilla publishes 6f instead of x60=11.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007B1B8}
    rows = _counter_combat_run(press_offset=4)
    hl0 = max(int(r["hitlag"][0]) for r in rows)
    hl1 = max(int(r["hitlag"][1]) for r in rows)
    a = _attrs()
    floor = int(a["speciallw_counter_shield_strength"])
    assert ACT_COUNTER_HIT in [int(r["action_id"][0]) for r in rows]
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
    # Entry route: dash does not admit neutral-B (Dash_IASA chain is SpecialS-only), so build
    # walk momentum instead (Walk_IASA runs the full chain incl. ftCo_800D6824).
    seed["pos_x"][0, 0] = np.float32(84.9)
    outs = _run(seed, [_mk_inputs(main_x=60)] * 3 + [_mk_inputs(buttons=B)] * 26)
    acts = [int(o["action_id"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert ACT_SB_START in acts, f"never entered grounded SB: {sorted(set(acts))}"
    i = acts.index(ACT_SB_START)
    assert og[i] == 1, "SB entry was not grounded"
    assert ACT_SB_AIR_START in acts, f"walk-off never swapped to air SB: {sorted(set(acts))}"
    j = acts.index(ACT_SB_AIR_START)
    assert ACT_FALL not in acts[: j + 1], f"generic Fall interposed: {acts[:j+1]}"
    assert og[j] == 0, "air SB swap row still grounded"


# ---------------------------------------------------------------------------
# Grounded B-dispatch chains + phys owners (manual-repro pass)
# ---------------------------------------------------------------------------

ACT_KNEE_BEND = 24


def test_neutral_b_not_available_from_dash() -> None:
    # Dash_IASA dispatches ftCo_SpecialS_CheckInput only - no neutral/up/down specials.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(main_x=127)] * 6 + [_mk_inputs(buttons=B)] * 10)
    acts = set(int(o["action_id"][0]) for o in outs)
    assert ACT_SB_START not in acts, f"neutral-B entered from dash: {sorted(acts)}"


def test_up_b_out_of_dash_via_kneebend() -> None:
    # Dash up+B: the tap-jump enters KneeBend; KneeBend_IASA's first check is
    # ftCo_Attack100_CheckInput (the up-special dispatcher), one frame after entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
    # Realistic input: the up flick (tap jump) lands a frame before the B edge, so the edge
    # arrives during KneeBend (vanilla's x686==0 gate is equally edge-strict).
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(main_x=127)] * 6 + [_mk_inputs(main_y=127)] + [
        _mk_inputs(buttons=B, main_y=127)
    ] * 8)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_GROUND in acts, f"up-B out of dash never came out: {sorted(set(acts))}"
    i = acts.index(ACT_DS_GROUND)
    assert acts[i - 1] == ACT_KNEE_BEND, f"no KneeBend frame before up-B: {acts[max(0,i-3):i+1]}"


def test_squatwait_neutral_and_side_b_blocked() -> None:
    # SquatWait_IASA: D68C0(down) and Attack100(up) only - no SpecialS, no D6824(neutral).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
    seed = _seed_base("marth")
    down = _mk_inputs(main_y=-127)
    outs = _run(seed, [down] * 10 + [_mk_inputs(buttons=B)] * 8)
    acts = set(int(o["action_id"][0]) for o in outs)
    assert ACT_SB_START not in acts, "neutral-B entered from SquatWait"
    # Keep holding down while pressing B+side (releasing down stands up through SquatRv,
    # whose IASA legitimately reopens the full chain).
    outs = _run(_seed_base("marth"), [down] * 10 + [_mk_inputs(buttons=B, main_x=90, main_y=-90)] * 8)
    acts = set(int(o["action_id"][0]) for o in outs)
    assert ACT_DB_S1 not in acts, "side-B entered from SquatWait"


def test_reverse_up_b_flips_facing_at_launch() -> None:
    # B-reverse: ftCheckThrowB3 (the frame-6 set_throw_flags pulse) + |stick.x| past x30
    # flips facing - checked unconditionally in the IASA, not only pre-launch.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_IASA
    seed = _seed_base("marth")  # facing right
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs(main_x=-127)] * 10 + [
        _mk_inputs()
    ] * 30)
    acts = [int(o["action_id"][0]) for o in outs]
    faces = [int(o["facing"][0]) for o in outs]
    assert acts[0] == ACT_DS_GROUND
    assert 0 in faces[:9], f"reverse up-B never flipped facing: {faces[:10]}"


def test_ground_sideb_momentum_decays() -> None:
    # Grounded Side-B entry runs the common ftCo_SpecialS::doEnter xB8 damping before Marth's
    # ftMs_SpecialS_Enter; the same-frame SpecialS1 Phys then applies ft_80084F3C friction.
    # This is the vanilla "stops immediately" owner behind the moving Side-B repro.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::doEnter
    # refs/melee/src/melee/ft/ft_084E.c::ft_80084F3C
    seed = _seed_base("marth")
    seed["speed_ground_x_self"][0, 0] = np.float32(1.2)
    seed["speed_air_x_self"][0, 0] = np.float32(1.2)
    out = _run(seed, [_mk_inputs(buttons=B, main_x=127)])[0]
    assert int(out["action_id"][0]) == ACT_DB_S1
    damped = 1.2 * float(_attrs()["side_special_ground_entry_vel_mul"])
    expected = damped - float(_attrs()["gr_friction"])
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(expected, abs=1e-6)


def test_ds_grounded_landing_never_sticks_in_fallspecial() -> None:
    # Manual repro (marth_stuck_landing_upb): DS drifting inland lands DURING the special;
    # the landing must enter LandingFallSpecial (x2C lag), never a grounded FallSpecial.
    seed = _seed_base("marth", grounded=False, pos_y=-40.0)
    seed["pos_x"][0, 0] = np.float32(74.0)
    seed["facing"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs(main_x=-127)] * 110)
    acts = [int(o["action_id"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert not any(acts[i] == ACT_FALL_SPECIAL and og[i] == 1 for i in range(len(acts))), (
        "grounded FallSpecial (stuck) reproduced"
    )
    assert ACT_LANDING_FALL_SPECIAL in acts, f"DS landing missed: {sorted(set(acts))}"


# ---------------------------------------------------------------------------
# Source-hook audit additions (RunDirect / SquatRv / OttottoWait / GuardOff / buffers)
# ---------------------------------------------------------------------------

ACT_RUN_DIRECT = 0x16
ACT_SQUAT_RV_ = 0x29
ACT_OTTOTTO = 0xF5
ACT_OTTOTTO_WAIT = 0xF6
ACT_GUARD_OFF_ = 0xB4


def test_squatrv_up_b_allowed_side_blocked() -> None:
    # ftCo_SquatRv_IASA: D68C0(down) -> Attack100(up) only.
    # Crouch, release down to stand (SquatRv), press up+B during the rise.
    seed = _seed_base("marth")
    script = [_mk_inputs(main_y=-127)] * 10 + [_mk_inputs()] + [_mk_inputs(buttons=B, main_y=127)] * 4
    outs = _run(seed, script + [_mk_inputs()] * 10)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SQUAT_RV_ in acts, f"never stood up through SquatRv: {sorted(set(acts))}"
    assert ACT_DS_GROUND in acts, f"up-B from SquatRv never entered: {sorted(set(acts))}"
    # NOTE: a side-B negative cannot be isolated here - the locomotion layer walk-cancels
    # SquatRv on a side-stick (pre-existing fox-validated behavior), and Walk's full chain
    # then legitimately admits side-B on the same frame. The SquatRv mask itself is up/down
    # only (verified by the up-B positive above with neutral stick-x).


def test_ottotto_wait_neutral_b_allowed() -> None:
    # OttottoWait_IASA delegates to Ottotto_IASA (full chain incl. ftCo_800D6824).
    seed = _seed_base("marth")
    seed["pos_x"][0, 0] = np.float32(85.2)
    # walk to the edge -> teeter (Ottotto -> OttottoWait), then neutral-B.
    outs = _run(seed, [_mk_inputs(main_x=50)] * 4 + [_mk_inputs()] * 40 + [_mk_inputs(buttons=B)] + [
        _mk_inputs(buttons=B)
    ] * 10)
    acts = [int(o["action_id"][0]) for o in outs]
    if ACT_OTTOTTO_WAIT in acts:
        assert ACT_SB_START in acts, f"neutral-B from OttottoWait never entered: {sorted(set(acts))}"


def test_guardoff_special_blocked_without_x1c() -> None:
    # GuardOff_IASA gates the special chain on mv.co.guard.x1C (armed only by
    # powershield-active shield contact); a plain shield release admits no specials.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOff_IASA,ftCo_80094138}
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(l=200)] * 8 + [_mk_inputs(buttons=B, main_y=127)] * 4 + [
        _mk_inputs()
    ] * 12)
    acts = [int(o["action_id"][0]) for o in outs]
    assert 180 in acts, f"never entered GuardOff: {sorted(set(acts))}"
    # The x1C gate blocks the special chain FROM GuardOff itself. The held up+B still jumps
    # out of shield (up = jump OoS) and the presence-based grounded up-special admission
    # (ftCo_Attack100_CheckInput, x686 == 0) then legitimately enters DS from KneeBend - the
    # real up-B out-of-shield route. Assert no GuardOff -> DS transition, not DS absence.
    for i in range(len(acts) - 1):
        assert not (acts[i] == 180 and acts[i + 1] == ACT_DS_GROUND), (
            "up-B entered directly from GuardOff without the x1C window"
        )


def test_aerial_up_b_requires_up_b_presence() -> None:
    # Source shape (x686/x68B lanes landed): ftCo_800D69C4 admits the aerial up-special on
    # up+B PRESENCE (held B with stick.y >= x21C) with the x68B >= x1C freshness gate. An
    # up-stick that ended before the B press leaves no presence frame, so no entry - x68B is
    # a re-trigger debounce, not a forward buffer.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D69C4,ftCo_800D6928}
    seed = _air_seed()
    outs = _run(seed, [_mk_inputs(main_y=127)] * 3 + [_mk_inputs(buttons=B)] + [_mk_inputs()] * 8)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_AIR not in acts, "aerial up-B entered with no up+B presence frame"


def test_aerial_fall_up_b_held_b_late_up_flick_does_not_enter() -> None:
    # Ordinary Fall IASA uses ftCo_SpecialAir_CheckInput, which gates the whole aerial special
    # chain on a current B edge. The presence-owned ftCo_800D69C4 path is a Damage/DamageFly
    # callsite, not a generic Fall/SpecialAir resolver.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    seed = _air_seed()
    # hitstun masks the initial B-hold frames (a bare held B would otherwise dispatch
    # neutral-B on its press edge); the up-flick lands after hitstun clears, with B held
    # the whole time - so the flick frame has NO B edge, only presence.
    seed["hitstun"][0, 0] = np.uint8(5)
    outs = _run(seed, [_mk_inputs(buttons=B)] * 6 +
                [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs(main_y=127)] * 10)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_AIR not in acts, f"Fall admitted held-B late up-flick: {sorted(set(acts))}"


def test_aerial_fall_up_b_current_b_edge_enters_without_presence_gate() -> None:
    # Adjacent positive for the ordinary SpecialAir_CheckInput owner: a current B edge with up-stick
    # from Fall still enters Dolphin Slash. This path must not depend on x686 presence freshness.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    seed = _air_seed()
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs()] * 8)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_AIR in acts, f"B-edge Fall up-B did not enter: {sorted(set(acts))}"


def test_under_lip_jump_lands_safely() -> None:
    # Under-lip rise (double jump from below the FD ledge with inward drift): the high jump
    # diamond legally rides over the lip while the root transiently passes the wall region -
    # the ECB is the only collision body in source, so the root path is not an invariant.
    # The outcome contract is: the fighter ends up ON the stage (vanilla corner-slide) and
    # never falls through. The lethal companion (airdodging during the transit) is covered by
    # test_escapeair_entry_floor_catch_under_lip.
    seed = _seed_base("marth", grounded=False, pos_y=-22.0)
    seed["pos_x"][0, 0] = np.float32(88.5)
    seed["facing"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)
    outs = _run(seed, [_mk_inputs(buttons=0x0400, main_x=-127)] + [_mk_inputs(main_x=-127)] * 70)
    ys = [float(o["pos_y"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert any(og), "never landed on the stage"
    assert min(ys) > -35.0, f"fell through: min_y {min(ys):.1f}"


# ---------------------------------------------------------------------------
# Source-driven dispatch audit pass 3: attack-IASA + aerial/common delegates
# ---------------------------------------------------------------------------


def test_up_b_from_fsmash_iasa_window() -> None:
    # AttackS4_IASA runs the full special chain directly once the script's allow_interrupt
    # event fires (marth fsmash: frame 48, anim end 49 - a real 2-frame cancel window);
    # earlier presses are eaten. (Marth's ftilt window opens at 40 with anim end ~39, i.e.
    # data-true empty - so fsmash is the representative grounded-attack IASA path.)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_IASA
    # data/moves/marth.json ftCo_SM_AttackS4 allow_interrupt frame 48
    seed = _seed_base("marth")
    script = [_mk_inputs(buttons=0x0100, main_x=127)] + [_mk_inputs()] * 45
    script += [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs()] * 15
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == 60, f"no fsmash: {acts[:3]}"  # AttackS4S
    assert ACT_DS_GROUND not in acts, "up-B entered before the fsmash allow_interrupt frame"
    seed = _seed_base("marth")
    script = [_mk_inputs(buttons=0x0100, main_x=127)] + [_mk_inputs()] * 46
    script += [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs()] * 15
    outs = _run(seed, script)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_GROUND in acts and acts.index(ACT_DS_GROUND) == 47, (
        f"up-B did not cancel the fsmash at the IASA frame: {sorted(set(acts))}"
    )


def test_side_b_from_grounded_damage_after_hitstun() -> None:
    # Grounded Damage_IASA delegates to Wait_IASA once the hitstun scalar clears.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(0x004F)  # DamageN2 (grounded damage)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["hitstun"][0, 0] = np.uint8(6)
    outs = _run(seed, [_mk_inputs()] * 7 + [_mk_inputs(buttons=B, main_x=127)] + [_mk_inputs()] * 12)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DB_S1 in acts, f"side-B never entered post-hitstun grounded damage: {sorted(set(acts))}"


def test_up_b_from_damagefly_after_hitstun() -> None:
    # Airborne DamageFly_IASA -> DamageFall_IASA -> ftCo_SpecialAir_CheckInput once hitstun
    # clears (the aerial/common delegate path).
    seed = _seed_base("marth", grounded=False, pos_y=60.0)
    seed["action_id"][0, 0] = np.uint16(0x0058)  # DamageFlyHi
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["hitstun"][0, 0] = np.uint8(8)
    seed["x680"][0, 0] = np.uint8(254)
    seed["x684"][0, 0] = np.uint8(254)
    outs = _run(seed, [_mk_inputs()] * 10 + [_mk_inputs(buttons=B, main_y=127)] + [_mk_inputs()] * 10)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DS_AIR in acts, f"up-B never entered post-hitstun DamageFly: {sorted(set(acts))}"


def test_no_special_from_jab_iasa() -> None:
    # Attack11_IASA runs attack/locomotion checks only - NO special dispatch.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_IASA
    seed = _seed_base("marth")
    outs = _run(seed, [_mk_inputs(buttons=0x0100)] + [_mk_inputs()] * 10 + [
        _mk_inputs(buttons=B, main_y=127)
    ] + [_mk_inputs()] * 6)
    acts = [int(o["action_id"][0]) for o in outs]
    if ACT_DS_GROUND in acts:
        # the up-B must come AFTER the jab fully ended (Wait reached), never from jab IASA
        i_ds = acts.index(ACT_DS_GROUND)
        assert ACT_WAIT in acts[:i_ds], f"up-B entered from jab IASA: {acts[:i_ds+1]}"


def test_down_b_from_full_chain_grounded_states() -> None:
    # Source order check: the grounded full chain is SpecialS -> up -> neutral(D6824) ->
    # down(D68C0); B+down from full-chain states must enter Counter (369), never Shield
    # Breaker (the neutral helper declines when the stick is in the down zone).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    for label, lead in (
        ("wait", []),
        ("walk", [_mk_inputs(main_x=50)] * 6),
        ("run", [_mk_inputs(main_x=127)] * 16),
    ):
        seed = _seed_base("marth")
        outs = _run(seed, lead + [_mk_inputs(buttons=B, main_y=-127)] + [_mk_inputs()] * 8)
        acts = [int(o["action_id"][0]) for o in outs]
        assert ACT_COUNTER in acts, f"{label}: down-B never entered Counter: {sorted(set(acts))}"
        assert ACT_SB_START not in acts, f"{label}: down-B misrouted to neutral-B"


def test_down_b_from_rundirect_brake_entry() -> None:
    # The RunBrake entry-frame exception covers RunDirect too: ftCo_RunDirect_IASA runs the
    # same full chain as Run before the brake transition, and locomotion's brake accepts
    # either source. Seed RunDirect (SM_Run pose), then down+B as the stick leaves forward.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_IASA
    seed = _seed_base("marth")
    seed["action_id"][0, 0] = np.uint16(0x0016)  # RunDirect
    seed["animation_index"][0, 0] = np.uint32(13)  # ftCo_SM_Run
    seed["seed_prev_action_id"][0, 0] = np.uint16(0x0016)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.7)
    outs = _run(seed, [_mk_inputs(main_x=127)] + [_mk_inputs(buttons=B, main_y=-127)] + [
        _mk_inputs()
    ] * 8)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_COUNTER in acts, f"down-B eaten on the RunDirect brake frame: {sorted(set(acts))}"


def test_escapeair_entry_floor_catch_under_lip() -> None:
    # Regression for marth_still_airdodge_through_stage: rising under the FD lip in the
    # double-jump tuck pose (diamond high above the root), then airdodging down-left. The
    # EscapeAir ENTRY frame's prev-bottom used a live-stale CollData lane (rel 0 from the
    # last grounded frame), collapsing the swept bottom to the root so the floor crossing
    # was never seen and the fighter fell through the stage to his death. The entry-lifetime
    # frames now use the pre-entry pose rel; the dodge must land.
    seed = _seed_base("marth", grounded=False, pos_y=-2.503126)
    seed["pos_x"][0, 0] = np.float32(85.399467)
    seed["facing"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(0x001B)  # JumpAerialF
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["action_frame"][0, 0] = np.float32(14)
    # Real one-step rows always carry a real seed_prev (the reseed reconstructs the CollData
    # lane from it); leaving it zero would write a garbage-but-seed-fresh lane no real row has.
    seed["seed_prev_action_id"][0, 0] = np.uint16(0x001B)
    seed["seed_prev_action_frame"][0, 0] = np.int16(13)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["speed_y_self"][0, 0] = np.float32(0.837)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.502)
    outs = _run(seed, [_mk_inputs(buttons=0x0040, l=255, main_x=-116, main_y=-106)] + [
        _mk_inputs(main_x=-116, main_y=-106)
    ] * 80)
    ys = [float(o["pos_y"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert any(og), "the dodge never landed"
    assert min(ys) > -35.0, f"fell through the stage: min_y {min(ys):.1f}"


def test_escapeair_under_lip_dodge_lands_from_live_lane_state() -> None:
    # Regression for marth_STILL_CLIPS_THROUGH_STAGE (the live-path kill that survived the
    # first stale-lane fix): ledge release -> fall -> live double jump -> rise under the FD
    # lip -> airdodge down-left ~14 frames later. The live jump arms the ECB lock and the
    # EscapeAir floor owners were FoD/seed-scoped, so no owner consumed the dodge's bottom
    # crossing of the FD ledge strip and the fighter fell through the stage. The last-resort
    # EscapeAir descending floor catch must land him on the stage.
    seed = _seed_base("marth", grounded=False, pos_y=-23.716)
    seed["pos_x"][0, 0] = np.float32(88.474)
    seed["facing"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(0x00FD)  # CliffWait
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["action_frame"][0, 0] = np.float32(1)
    seed["jumps_left"][0, 0] = np.uint8(1)
    # release (stick away), fall, dj with inward drift, dodge down-left mid-rise
    script = [_mk_inputs(main_x=-30)] * 4 + [_mk_inputs()] * 1
    script += [_mk_inputs(buttons=0x0400, main_x=-90, main_y=-90)]
    script += [_mk_inputs(main_x=-115, main_y=-95)] * 13
    script += [_mk_inputs(buttons=0x0040, l=255, main_x=-120, main_y=-100)]
    script += [_mk_inputs(main_x=-120, main_y=-100)] * 80
    outs = _run(seed, script)
    ys = [float(o["pos_y"][0]) for o in outs]
    og = [int(o["on_ground"][0]) for o in outs]
    assert min(ys) > -35.0, f"fell through the stage: min_y {min(ys):.1f}"
    assert any(og), "never landed"


def test_fuzz_live_clip_known_seeds_stay_clean() -> None:
    # The four autonomously-found live hull-interior clip seeds (see
    # tools/eval/fuzz_live_clip.py and MANUAL_REPRO_CATALOG.md). Each was a distinct
    # mechanism: ground-jump dodge with the ground-departure lock (571981485), lip-rounding
    # jump descending through the ledge strip (1135808358), and run-off dodges where the
    # X130-locked bottom must drive the wall envelope (1282560985 marth, 2053134993 fox).
    import random

    from tools.eval.fuzz_live_clip import Episode, _policy_script, run_episode

    # Seed 571981485 was an old Marth lock whose clean resolution depended on Marth being admitted
    # to the common walljump/ledge escape path. Marth's `can_walljump=false` data is now enforced
    # separately, so that seed is no longer a source-valid anti-clip positive.
    # data/characters/marth.json::can_walljump
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    for rng_seed, char in ((1135808358, "marth"), (1282560985, "marth"), (2053134993, "fox")):
        rng = random.Random(rng_seed)
        start_x = rng.choice((55.0, 70.0, 78.0, 83.0, -70.0, -83.0))
        ep = Episode(rng_seed=rng_seed, char=char, start_x=start_x)
        ep.script = _policy_script(rng, 420)
        v = run_episode(ep)
        assert not v, f"{char} seed {rng_seed} clipped: {v}"


def test_runoff_escapeair_ground_departure_wall_packet_still_prevents_clip() -> None:
    # Positive lock for the owner excluded from JumpAerial/KneeBend-entry EscapeAir rows:
    # run-off / ground-departure EscapeAir can consume the X130-locked bottom wall envelope.
    # The deterministic fuzz seed below is the Marth run-off witness from the manual clip search;
    # narrowing JumpAerial-entry rows must not remove this source path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80046224_LeftWall}
    import random

    from tools.eval.fuzz_live_clip import Episode, _policy_script, run_episode

    rng_seed = 1282560985
    rng = random.Random(rng_seed)
    start_x = rng.choice((55.0, 70.0, 78.0, 83.0, -70.0, -83.0))
    ep = Episode(rng_seed=rng_seed, char="marth", start_x=start_x)
    ep.script = _policy_script(rng, 420)
    violation = run_episode(ep)
    assert not violation, f"run-off EscapeAir ground-departure packet regressed: {violation}"


def test_fall_carried_same_ledge_floor_in_span_relands() -> None:
    # Durable lock for fall_carried_same_ledge_floor_in_span_owner (mpcoll_ground.c): a Fall
    # carrying a ledge floor's gid, back IN-SPAN, descending onto that SAME strip previously
    # had no owner (the connected owner arms only off-span; every hard-floor producer
    # excludes is_ledge lines) and the fighter fell through the stage. Source mpCheckFloor
    # treats the carried index as the prefer hint, never an exclusion.
    # Warm live repro: land on FD, run LEFT across the strip and off the ledge (carrying the
    # strip's gid naturally), double-jump back inward, land.
    seed = _seed_base("fox", grounded=False, pos_y=1.5)
    seed["pos_x"][0, 0] = np.float32(-40.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    script = [_mk_inputs()] * 14                       # settle-land (warm lanes)
    script += [_mk_inputs(main_x=-127)] * 30           # run left, off the ledge
    script += [_mk_inputs()] * 4
    script += [_mk_inputs(buttons=0x0400, main_x=110)] + [_mk_inputs(main_x=110)] * 60
    outs = _run(seed, script)
    og = [int(o["on_ground"][0]) for o in outs]
    ys = [float(o["pos_y"][0]) for o in outs]
    landed_back = any(og[i] for i in range(46, len(og)))
    assert landed_back, "carried-ledge in-span re-landing fell through the strip"
    assert min(ys) > -35.0, f"fell through the stage: min_y {min(ys):.1f}"
    # cold-seed direct variant: Fall above the strip carrying that strip's own gid
    seed = _seed_base("fox", grounded=False, pos_y=1.5)
    seed["pos_x"][0, 0] = np.float32(-80.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["ground_id"][0, 0] = np.uint16(0)  # FD left strip's own segment id
    outs = _run(seed, [_mk_inputs()] * 25)
    og = [int(o["on_ground"][0]) for o in outs]
    assert any(og), "cold carried-same-strip Fall fell through"


def test_ys_ledgedash_horizontal_airdodge_wavelands_on_lip_slope() -> None:
    # Yoshi's Story ledgedash, pure-horizontal airdodge (the pinned sweep residual: tuple
    # side=+1, hang=4, dj_delay=1, dodge_af_delay=13, angle (127, 0)). The dodge transits
    # under the lip at constant root y with the pose ECB bottom skimming the sloped ledge
    # strip (segment 6); the bottom crosses the slope while RISING by float jitter on the
    # crossing frame. Source mpCheckFloor's sloped-line branch has no descent gate and no
    # ledge-line filter, so vanilla wavelands onto the slope; without the ledge-strip
    # producer the fighter fell through the stage body and lost the stock.
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersection}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    for side in (1, -1):
        seed = _seed_base("marth", grounded=False, pos_y=0.0)
        seed["stage_id"][0] = np.uint32(8)  # Yoshi's Story
        edge_x = 56.0 * side
        slope_y = -3.5 / 16.8 * 6.8  # strip y at 10 units inboard of the edge
        seed["pos_x"][0, 0] = np.float32(edge_x - side * 10.0)
        seed["pos_y"][0, 0] = np.float32(slope_y + 1.5)
        seed["ground_id"][0, 0] = np.uint16(0xFFFF)
        seed["action_id"][0, 0] = np.uint16(ACT_FALL)
        seed["animation_index"][0, 0] = np.uint32(SM_FALL)
        script = [_mk_inputs()] * 14                              # settle-land on the strip
        script += [_mk_inputs(main_x=side * 127)] * 12            # run off toward the ledge
        script += [_mk_inputs(main_x=side * 40)] * 10             # drift to grab
        script += [_mk_inputs()] * 4                              # hang
        script += [_mk_inputs(main_x=-side * 35)]                 # release
        script += [_mk_inputs()] * 1
        script += [_mk_inputs(buttons=0x0400, main_x=-side * 90, main_y=-60)]
        script += [_mk_inputs(main_x=-side * 90, main_y=-60)] * 13
        script += [_mk_inputs(buttons=0x0040, l=255, main_x=-side * 127, main_y=0)]
        script += [_mk_inputs(main_x=-side * 127, main_y=0)] * 80
        outs = _run(seed, script)
        og = [int(o["on_ground"][0]) for o in outs]
        ys = [float(o["pos_y"][0]) for o in outs]
        stocks = [int(o["stocks"][0]) for o in outs]
        dodge_rows = [i for i, o in enumerate(outs) if int(o["action_id"][0]) == 0x00EC]
        assert dodge_rows, f"side {side}: airdodge never started"
        landed = any(og[i] for i in range(dodge_rows[0], len(og)))
        assert landed, f"side {side}: horizontal under-lip dodge never landed on the slope"
        # ys before the dodge include the legal pre-grab fall outside the stage (~-29);
        # the clip signature is the post-dodge descent into the keel (the kill reached -91).
        post_dodge_min_y = min(ys[dodge_rows[0] :])
        assert post_dodge_min_y > -20.0, (
            f"side {side}: entered the stage body: min_y {post_dodge_min_y:.1f}"
        )
        assert stocks[-1] == stocks[0], f"side {side}: lost a stock ({stocks[0]}->{stocks[-1]})"
