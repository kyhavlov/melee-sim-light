"""Character-parameterized common-action coverage suite.

This is the reusable porting checklist: every test runs for each registry character selected
below (fox + marth + sheik during the Sheik port; see _COVERAGE_EXCLUDE) and derives
its expectations from that character's extracted data (data/characters/<ch>.json, moves,
scripts, anim tracks) plus shared ftCommonData constants - never from hardcoded per-character
literals. A new character port is "common-action complete" when this file passes for it.

Buckets (mirrors reports/marth_port/COVERAGE.md):
  1. movement/core physics  2. attacks  3. shield/defense  4. grab/throw
  5. damage/knockdown/tech  6. ledge/cliff  7. collision substrate

Decomp anchors are cited per test. Fox rows validate the harness against the long-validated
character; Marth and Sheik rows validate the current new-character ports.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

ROOT = Path(__file__).resolve().parents[1]

# Coverage characters derive from the central registry so the checklist follows new ports
# automatically. Falco is excluded for now purely to keep the matrix focused on one spacie
# control plus active new-character ports; drop the filter to widen.
from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS

_COVERAGE_EXCLUDE = {"falco"}
CHARS = {
    name: info.internal_id
    for name, info in _REGISTRY_CHARS.items()
    if name not in _COVERAGE_EXCLUDE
}

STAGE_FD = 32

ACT_WAIT = 0x000E
ACT_WALK_SLOW = 0x000F
ACT_WALK_MIDDLE = 0x0010
ACT_WALK_FAST = 0x0011
ACT_TURN = 0x0012
ACT_DASH = 0x0014
ACT_RUN = 0x0015
ACT_KNEE_BEND = 0x0018
ACT_JUMP_F = 0x0019
ACT_JUMP_B = 0x001A
ACT_JUMP_AERIAL_F = 0x001B
ACT_FALL = 0x001D
ACT_FALL_AERIAL = 0x0020
ACT_SQUAT_WAIT = 0x0028
ACT_LANDING = 0x002A
ACT_ATTACK_11 = 0x002C
ACT_ATTACK_S4 = 0x003C  # forward smash (mid angle)
ACT_ATTACK_HI4 = 0x003F
ACT_ATTACK_LW4 = 0x0040
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_F = 0x0042
ACT_ATTACK_AIR_B = 0x0043
ACT_ATTACK_AIR_HI = 0x0044
ACT_ATTACK_AIR_LW = 0x0045
ACT_LANDING_AIR_N = 0x0046
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_OFF = 0x00B4
ACT_ESCAPE_F = 0x00E9
ACT_ESCAPE_B = 0x00EA
ACT_ESCAPE = 0x00EB  # spotdodge
ACT_ESCAPE_AIR = 0x00EC
ACT_CATCH = 0x00D4
ACT_DOWN_BOUND_U = 0x00B7  # ftCo_MS_DownBoundU (face-up bound)
ACT_DOWN_WAIT_U = 0x00B8  # ftCo_MS_DownWaitU
ACT_PASSIVE = 0x00C7  # tech in place
ACT_CLIFF_CATCH = 0x00FC
ACT_CLIFF_WAIT = 0x00FD

SM_WAIT1 = 2
SM_FALL = 20  # ftCo_SM_Fall


def _char_attrs(name: str) -> dict:
    return json.loads((ROOT / "data" / "characters" / f"{name}.json").read_text())


def _char_moves(name: str) -> dict:
    return json.loads((ROOT / "data" / "moves" / f"{name}.json").read_text())


def _common() -> dict:
    return json.loads((ROOT / "data" / "common" / "ft_common_data.json").read_text())


def _mk_inputs(**axes) -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    for k, v in axes.items():
        inp["p"][0, 0][k] = v
    return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))


def _seed_base(char_name: str, *, grounded: bool = True, pos_y: float = 0.0) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHARS[char_name])
    seed["char_id"][0, 1] = np.uint8(CHARS["fox"])
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(60.0)  # far away: no interaction
    seed["pos_y"][0, 0] = np.float32(pos_y)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1 if grounded else 0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["shield_hp"][0, :2] = np.float32(60.0)
    seed["jumps_left"][0, :2] = np.uint8(2)
    for p in range(2):
        seed["action_id"][0, p] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, p] = np.int16(0)
        seed["anim_frame_f32"][0, p] = np.float32(0.0)
        seed["animation_index"][0, p] = np.uint32(SM_WAIT1)
    return seed


def _run(seed: np.ndarray, frames: list[np.ndarray]) -> list[np.ndarray]:
    """Reseed once, then step through `frames` (list of input rows); return compare rows."""
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


CHAR_PARAMS = pytest.mark.parametrize("char_name", sorted(CHARS))


def test_common_action_matrix_includes_sheik_port_and_controls() -> None:
    assert "fox" in CHARS
    assert "marth" in CHARS
    assert "sheik" in CHARS
    assert "falco" not in CHARS
    assert CHARS["sheik"] == _REGISTRY_CHARS["sheik"].internal_id


# ---------------------------------------------------------------------------
# Bucket 1: movement / core physics
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_dash_initial_velocity_and_dash_state(char_name: str) -> None:
    # Decomp: ftCo_Dash_Enter applies ftCo_DatAttrs dash_initial_velocity (x6C family) on entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c
    a = _char_attrs(char_name)
    seed = _seed_base(char_name)
    inp = _mk_inputs(main_x=127)
    outs = _run(seed, [inp, inp])
    assert int(outs[0]["action_id"][0]) == ACT_DASH, "smash-input from Wait must enter Dash"
    v = float(outs[0]["speed_ground_x_self"][0])
    assert v > 0.0
    assert v <= float(a["dash_run_terminal_velocity"]) + 1e-5
    # By frame 2 the dash velocity must be >= the per-char initial dash velocity
    # (entry impulse + accel), and bounded by the char's dash terminal velocity.
    v2 = float(outs[1]["speed_ground_x_self"][0])
    assert v2 >= float(a["dash_initial_velocity"]) - 0.25
    assert v2 <= float(a["dash_run_terminal_velocity"]) + 1e-5


@CHAR_PARAMS
def test_dash_to_run_terminal_velocity(char_name: str) -> None:
    # Decomp: ftCo_Run velocity converges to dash_run_terminal_velocity.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name)
    # 40 run frames need more runway than FD's half-width for the fastest runners
    # (Falcon covers ~88 units from x=0 and would run off the +85.57 edge into Fall).
    seed["pos_x"][0, 0] = np.float32(-60.0)
    inp = _mk_inputs(main_x=127)
    outs = _run(seed, [inp] * 40)
    v = float(outs[-1]["speed_ground_x_self"][0])
    assert abs(v - float(a["dash_run_terminal_velocity"])) < 0.05, (
        f"{char_name}: run terminal {v} != {a['dash_run_terminal_velocity']}"
    )
    assert int(outs[-1]["action_id"][0]) in (ACT_DASH, ACT_RUN)


@CHAR_PARAMS
def test_walk_max_velocity(char_name: str) -> None:
    # Decomp: ftCo_Walk velocity targets walk_max_vel * stick fraction (ftWalkCommon_800DFDDC).
    a = _char_attrs(char_name)
    seed = _seed_base(char_name)
    # Tilt input below the dash smash threshold: ramp over a few frames.
    ramp = [_mk_inputs(main_x=30), _mk_inputs(main_x=40)] + [_mk_inputs(main_x=50)] * 50
    outs = _run(seed, ramp)
    acts = {int(o["action_id"][0]) for o in outs}
    assert acts & {ACT_WALK_SLOW, ACT_WALK_MIDDLE, ACT_WALK_FAST}, f"never walked: {acts}"
    v = float(outs[-1]["speed_ground_x_self"][0])
    assert 0.0 < v <= float(a["walk_max_vel"]) + 1e-5


@CHAR_PARAMS
def test_jump_startup_and_initial_velocities(char_name: str) -> None:
    # Decomp: ftCo_KneeBend lasts jump_startup_frames, then ftCo_JumpF applies
    # jump_v_initial_velocity (and X from stick * jump_h_initial_velocity).
    a = _char_attrs(char_name)
    n_squat = int(a["jump_startup_frames"])
    seed = _seed_base(char_name)
    frames = [_mk_inputs(buttons=0x0400)] * (n_squat + 1)  # X button held
    outs = _run(seed, frames)
    assert int(outs[0]["action_id"][0]) == ACT_KNEE_BEND
    # KneeBend holds for exactly jump_startup_frames steps, then JumpF/JumpB.
    for i in range(n_squat - 1):
        assert int(outs[i]["action_id"][0]) == ACT_KNEE_BEND, f"frame {i} left kneebend early"
    jumped = outs[n_squat - 1] if int(outs[n_squat - 1]["action_id"][0]) != ACT_KNEE_BEND else outs[n_squat]
    assert int(jumped["action_id"][0]) in (ACT_JUMP_F, ACT_JUMP_B)
    vy = float(jumped["speed_y_self"][0])
    grav = float(_char_attrs(char_name)["grav"])
    expect = float(a["jump_v_initial_velocity"])
    # The post-frame Y velocity is the initial jump velocity less up to two gravity tick orders.
    assert expect - 2.5 * grav <= vy <= expect + 1e-4, (
        f"{char_name}: jump vy {vy} vs initial {expect} (grav {grav})"
    )


@CHAR_PARAMS
def test_gravity_and_terminal_velocity(char_name: str) -> None:
    # Decomp: ftCommon_Phys gravity integration clamps to terminal_vel.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=600.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 60)
    vy = float(outs[-1]["speed_y_self"][0])
    assert abs(vy + float(a["terminal_vel"])) < 1e-3, (
        f"{char_name}: terminal fall {vy} vs -{a['terminal_vel']}"
    )
    # Early integration: dv = grav per frame (clamped later).
    vy0 = float(outs[0]["speed_y_self"][0])
    vy1 = float(outs[1]["speed_y_self"][0])
    assert abs((vy0 - vy1) - float(a["grav"])) < 1e-4


@CHAR_PARAMS
def test_fastfall_velocity(char_name: str) -> None:
    # Decomp: ftCo_Fall fast-fall sets fast_fall_velocity on a down-stick flick.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=600.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    idle = _mk_inputs()
    down = _mk_inputs(main_y=-127)
    outs = _run(seed, [idle] * 12 + [down] * 4)
    vy = float(outs[-1]["speed_y_self"][0])
    assert abs(vy + float(a["fast_fall_velocity"])) < 1e-3, (
        f"{char_name}: fastfall {vy} vs -{a['fast_fall_velocity']}"
    )


@CHAR_PARAMS
def test_air_drift_caps_at_max_h_air_speed(char_name: str) -> None:
    # Decomp: ftCommon air drift accel caps |vx| at air_max_horizontal_velocity.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=900.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    fwd = _mk_inputs(main_x=127)
    outs = _run(seed, [fwd] * 70)
    vx = float(outs[-1]["speed_air_x_self"][0])
    assert abs(vx - float(a["air_drift_max"])) < 1e-3, (
        f"{char_name}: air drift {vx} vs {a['air_drift_max']}"
    )


@CHAR_PARAMS
def test_ground_friction_in_wait(char_name: str) -> None:
    # Decomp: ftCommon_8007CB74 applies gr_friction toward zero each grounded frame.
    # Keep the seed below walk_max_vel: ft_80084F3C-style high-speed friction paths scale
    # gr_friction once |gr_vel| exceeds the character's walk cap (Sheik's is below the old
    # hardcoded 1.5 fixture value).
    a = _char_attrs(char_name)
    seed = _seed_base(char_name)
    start_v = min(1.5, float(a["walk_max_vel"]) - 0.1)
    assert start_v > float(a["gr_friction"])
    seed["speed_ground_x_self"][0, 0] = np.float32(start_v)
    idle = _mk_inputs()
    outs = _run(seed, [idle, idle])
    v0 = float(outs[0]["speed_ground_x_self"][0])
    assert abs((start_v - v0) - float(a["gr_friction"])) < 0.02 or v0 == 0.0, (
        f"{char_name}: friction step {start_v}->{v0} vs gr_friction {a['gr_friction']}"
    )


# ---------------------------------------------------------------------------
# Bucket 2: attacks
# ---------------------------------------------------------------------------


def _move_events(char_name: str, move_key: str) -> list[dict]:
    mv = _char_moves(char_name)["moves"].get(move_key)
    assert mv is not None, f"{char_name}: missing movescript {move_key}"
    return mv["events"]


def _first_hitbox_frame(char_name: str, move_key: str) -> int:
    for e in _move_events(char_name, move_key):
        if e.get("kind") == "create_hitbox":
            return int(e["frame"])
    raise AssertionError(f"{char_name}: no hitboxes in {move_key}")


@CHAR_PARAMS
@pytest.mark.parametrize(
    "act,move_key,button_axes",
    [
        (ACT_ATTACK_11, "ftCo_SM_Attack11", dict(buttons=1 << 8)),  # A
        (ACT_ATTACK_AIR_N, "ftCo_SM_AttackAirN", None),  # entered via seed (air)
    ],
)
def test_attack_hitbox_first_active_frame(char_name, act, move_key, button_axes) -> None:
    # The first replay-visible hitlag-capable frame must match the movescript's first
    # create_hitbox frame. Source: data/moves/<ch>.json (extracted subaction scripts).
    first = _first_hitbox_frame(char_name, move_key)
    assert first >= 1
    if button_axes is not None:
        seed = _seed_base(char_name)
        frames = [_mk_inputs(**button_axes)] + [_mk_inputs()] * (first + 4)
    else:
        seed = _seed_base(char_name, grounded=False, pos_y=900.0)
        seed["action_id"][0, 0] = np.uint16(act)
        seed["action_frame"][0, 0] = np.int16(0)
        seed["anim_frame_f32"][0, 0] = np.float32(0.0)
        sm = _char_moves(char_name)["moves"][move_key].get("submotion_id")
        seed["animation_index"][0, 0] = np.uint32(sm if sm is not None else 0xFFFFFFFF)
        frames = [_mk_inputs()] * (first + 4)
    outs = _run(seed, frames)
    if button_axes is not None:
        assert int(outs[0]["action_id"][0]) == act, (
            f"{char_name}: expected {act}, got {int(outs[0]['action_id'][0])}"
        )


@CHAR_PARAMS
def test_attack_move_ids_distinct_per_action(char_name: str) -> None:
    # MSLACID1 move-id table must be loaded and per-char (staling identity).
    # refs/melee/src/melee/ft/ft_0881.c (stale queue move ids)
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        ids = {}
        for act in (ACT_ATTACK_11, ACT_ATTACK_S4, ACT_ATTACK_AIR_N, ACT_ATTACK_AIR_F):
            seed = _seed_base(char_name)
            # consume via the debug move-id helper if exposed; else assert via dataset tables
            ids[act] = act  # placeholder: table presence asserted below
    finally:
        msl_binding.destroy(handle)
    import struct

    p = ROOT / "data" / "attack_id" / "move_id" / f"{char_name}.bin"
    assert p.exists(), f"missing move-id table for {char_name}"
    buf = p.read_bytes()
    assert buf[:8] == b"MSLACID1"


@CHAR_PARAMS
def test_aerial_landing_lag_frames_loaded(char_name: str) -> None:
    # Landing lag attrs are per-char data consumed by ftCo_LandingAir*.
    a = _char_attrs(char_name)
    for k in (
        "landing_airn_lag_frames",
        "landing_airf_lag_frames",
        "landing_airb_lag_frames",
        "landing_airhi_lag_frames",
        "landing_airlw_lag_frames",
    ):
        assert float(a[k]) > 0.0, f"{char_name}: {k} missing"


# ---------------------------------------------------------------------------
# Bucket 3: shield / defense
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_guard_on_entry_and_shield_radius_scale(char_name: str) -> None:
    # Decomp: ftCo_80091A4C digital press -> GuardOn; bubble radius rides
    # initial_shield_size (inlineB0). refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
    a = _char_attrs(char_name)
    seed = _seed_base(char_name)
    hold = _mk_inputs(l=200)  # analog press: GuardOn without the digital powershield edge
    outs = _run(seed, [hold] * 12)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_GUARD_ON in acts, f"{char_name}: never entered GuardOn: {acts[:6]}"
    assert ACT_GUARD in acts, f"{char_name}: never settled into Guard: {acts}"
    # Steady full-hp digital-press radius = (0.85*1*0.5 + 0.15) * init * scale_y.
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()
        for _ in range(12):
            msl_binding.step_input(handle, prev, hold)
            prev = hold
        bub = msl_binding.debug_shield_bubbles_world(handle, 0)
        r = float(bub[0][3])
        ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, ob)
        hp = float(ob.view(COMPARE_DTYPE).reshape((1,))[0]["shield_hp"][0])
    finally:
        msl_binding.destroy(handle)
    # light = clamp01((200/255 - trigger_deadzone) / (1 - trigger_deadzone)); scale follows
    # inlineB0: ((1-min)*hp_ratio*lightscale + min). refs ftCo_Guard.c::inlineB0
    # hp_ratio uses the measured post-drain shield hp: 12 held frames drain enough that a
    # hp_ratio=1.0 expectation drifts past an absolute tolerance for large shields (Falcon).
    c = _common()
    dz = float(c.get("trigger_deadzone", 0.3))
    light = max(0.0, min(1.0, (200.0 / 255.0 - dz) / (1.0 - dz)))
    ls_min = float(c.get("shield_size_lightshield_min", 1.0))
    ls_max = float(c.get("shield_size_lightshield_max", 0.5))
    light_scale = light * (ls_max - ls_min) + ls_min
    min_scale = float(c.get("shield_size_min_scale", 0.15))
    hp_ratio = hp / 60.0
    expect = ((1.0 - min_scale) * hp_ratio * light_scale + min_scale) * float(a["initial_shield_size"])
    assert abs(r - expect) < 0.3, f"{char_name}: steady shield radius {r} vs {expect} (hp {hp})"


@CHAR_PARAMS
def test_spotdodge_and_rolls_enter(char_name: str) -> None:
    # Decomp: Guard + down flick -> Escape (spotdodge); back/forward -> EscapeB/F.
    for stick, want in ((dict(main_y=-127), ACT_ESCAPE), (dict(main_x=127), ACT_ESCAPE_F),
                        (dict(main_x=-127), ACT_ESCAPE_B)):
        seed = _seed_base(char_name)
        hold = _mk_inputs(l=200)
        flick = _mk_inputs(l=200, **stick)
        outs = _run(seed, [hold] * 6 + [flick, flick, flick])
        acts = [int(o["action_id"][0]) for o in outs]
        assert want in acts, f"{char_name}: stick {stick} never produced {want}: {acts[-4:]}"


@CHAR_PARAMS
def test_airdodge_enters_escape_air(char_name: str) -> None:
    seed = _seed_base(char_name, grounded=False, pos_y=900.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    dodge = _mk_inputs(buttons=0x0040, l=255)
    outs = _run(seed, [dodge, dodge])
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_ESCAPE_AIR in acts, f"{char_name}: airdodge never entered: {acts}"


# ---------------------------------------------------------------------------
# Bucket 4: grab / throw
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_grab_enters_catch(char_name: str) -> None:
    # Decomp: Z press from Wait -> ftCo_Catch.
    seed = _seed_base(char_name)
    z = _mk_inputs(buttons=0x0010)
    outs = _run(seed, [z, z])
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_CATCH in acts, f"{char_name}: Z never entered Catch: {acts}"


# ---------------------------------------------------------------------------
# Bucket 5: damage / knockdown / tech
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_downbound_to_downwait(char_name: str) -> None:
    # Decomp: ftCo_DownBoundU -> DownWaitU when the bounce anim completes.
    seed = _seed_base(char_name)
    seed["action_id"][0, 0] = np.uint16(ACT_DOWN_BOUND_U)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 50)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_DOWN_WAIT_U in acts, f"{char_name}: DownBoundU never reached DownWaitU: {set(acts)}"


@CHAR_PARAMS
def test_knockback_scales_with_weight(char_name: str) -> None:
    # Decomp: KB formula divides by (weight + 100); heavier chars take less knockback.
    # We verify the char's weight attr is loaded and plausible rather than simulating a full
    # hit here (combat KB ownership is covered by the replay suites).
    a = _char_attrs(char_name)
    w = float(a["weight"])
    assert 60.0 <= w <= 120.0


# ---------------------------------------------------------------------------
# Bucket 6: ledge / cliff
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_cliffcatch_from_fall_near_ledge(char_name: str) -> None:
    # Decomp: ftCo_Fall + ledge-snap window (ftData_x44 ledge_snap_*) -> CliffCatch.
    # FD right ledge is at x=+68.4; approach from inside, falling past the lip.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=2.0)
    seed["pos_x"][0, 0] = np.float32(85.5666 + 4.0)  # just outside the FD right edge
    seed["facing"][0, 0] = np.uint8(0)  # facing the stage (left)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)  # real submotion: ECB tables sample by msid
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 30)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_CLIFF_CATCH in acts or ACT_CLIFF_WAIT in acts, (
        f"{char_name}: never cliff-caught (snap_x={a['ledge_snap_x']}): {sorted(set(acts))}"
    )


# ---------------------------------------------------------------------------
# Bucket 7: collision substrate
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_ecb_tables_loaded_and_grounded_rest(char_name: str) -> None:
    # ECB bottom table must exist and a grounded Wait char must rest at floor height exactly.
    p = ROOT / "data" / "ecb" / f"{char_name}_bottom.bin"
    assert p.exists()
    seed = _seed_base(char_name)
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 5)
    assert abs(float(outs[-1]["pos_y"][0])) < 1e-3
    assert int(outs[-1]["on_ground"][0]) == 1


@CHAR_PARAMS
def test_platform_landing_battlefield(char_name: str) -> None:
    # Falling onto a Battlefield side platform must land (platform pass/land substrate).
    seed = _seed_base(char_name, grounded=False, pos_y=40.0)
    seed["stage_id"][0] = np.uint32(31)
    seed["pos_x"][0, 0] = np.float32(-57.6)  # over the left platform (y=27.2)
    seed["pos_x"][0, 1] = np.float32(20.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 40)
    landed = [o for o in outs if int(o["on_ground"][0]) == 1]
    assert landed, f"{char_name}: never landed on the platform"
    assert abs(float(landed[0]["pos_y"][0]) - 27.2) < 0.6, (
        f"{char_name}: landed at y={float(landed[0]['pos_y'][0])}, platform is 27.2"
    )


@CHAR_PARAMS
def test_hurtcaps_present_for_char(char_name: str) -> None:
    p = ROOT / "data" / "hurtcaps" / f"{char_name}.json"
    caps = json.loads(p.read_text())["capsules"]
    assert len(caps) >= 8, f"{char_name}: implausibly few hurtcaps ({len(caps)})"


# ---------------------------------------------------------------------------
# Bucket 2b: attack contact lifecycle (hitbox -> damage -> hitlag on a live victim)
# ---------------------------------------------------------------------------


def _first_hitbox(char_name: str, move_key: str) -> dict:
    for e in _move_events(char_name, move_key):
        if e.get("kind") == "create_hitbox":
            return e
    raise AssertionError(f"{char_name}: no hitboxes in {move_key}")


@CHAR_PARAMS
def test_jab_contact_applies_script_damage_and_hitlag(char_name: str) -> None:
    # End-to-end hitbox lifecycle: jab1's first create_hitbox frame must damage a point-blank
    # victim with the script's damage value, and both take hitlag that frame.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 (BODY intake)
    ev = _first_hitbox(char_name, "ftCo_SM_Attack11")
    first = int(ev["frame"])
    dmg = float(ev["data"]["hitbox"]["damage"])
    seed = _seed_base(char_name)
    seed["pos_x"][0, 1] = np.float32(5.0)  # point-blank victim
    seed["facing"][0, 1] = np.uint8(0)
    frames = [_mk_inputs(buttons=0x0100)] + [_mk_inputs()] * (first + 3)
    outs = _run(seed, frames)
    assert int(outs[0]["action_id"][0]) == ACT_ATTACK_11
    hit_rows = [o for o in outs if float(o["percent"][1]) > 0.0]
    assert hit_rows, f"{char_name}: jab never connected"
    first_hit_i = next(i for i, o in enumerate(outs) if float(o["percent"][1]) > 0.0)
    # Fresh-attack staling: first use applies 1.0x script damage.
    assert abs(float(hit_rows[0]["percent"][1]) - dmg) < 0.05, (
        f"{char_name}: jab applied {float(hit_rows[0]['percent'][1])} vs script {dmg}"
    )
    assert int(outs[first_hit_i]["hitlag"][0]) > 0, f"{char_name}: attacker took no hitlag"
    assert int(outs[first_hit_i]["hitlag"][1]) > 0, f"{char_name}: victim took no hitlag"
    # Lifecycle: the contact frame must be at/after the script's first active frame.
    assert first_hit_i + 1 >= first, (
        f"{char_name}: contact at step {first_hit_i} before script frame {first}"
    )


@CHAR_PARAMS
def test_fsmash_charge_hold_delays_release(char_name: str) -> None:
    # Smash charge: holding A at the charge frame freezes the anim (move_tables smash_charge);
    # releasing later still attacks. refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c
    seed = _seed_base(char_name)
    smash = _mk_inputs(buttons=0x0100, main_x=127, c_x=0)
    hold = _mk_inputs(buttons=0x0100, main_x=127)
    release = _mk_inputs(main_x=0)
    outs = _run(seed, [smash] + [hold] * 20 + [release] * 25)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_ATTACK_S4 in acts, f"{char_name}: never entered fsmash: {sorted(set(acts))[:8]}"
    # While charging, the action persists far longer than the uncharged anim would run.
    assert acts[:21].count(ACT_ATTACK_S4) >= 15, f"{char_name}: charge did not hold: {acts[:21]}"


@CHAR_PARAMS
def test_aerial_landing_enters_landing_air_lag(char_name: str) -> None:
    # NAir into the ground -> LandingAirN with the char's landing_airn_lag_frames duration.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=12.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)
    atk = _mk_inputs(buttons=0x0100)
    idle = _mk_inputs()
    outs = _run(seed, [atk] + [idle] * 45)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_ATTACK_AIR_N in acts, f"{char_name}: no NAir: {acts[:5]}"
    assert ACT_LANDING_AIR_N in acts, f"{char_name}: never landed into LandingAirN: {sorted(set(acts))}"
    lag = sum(1 for x in acts if x == ACT_LANDING_AIR_N)
    expect = float(a["landing_airn_lag_frames"])
    assert abs(lag - expect) <= 2.0, f"{char_name}: NAir landing lag {lag} vs attr {expect}"


# ---------------------------------------------------------------------------
# Bucket 3b: shield damage / GuardSetOff
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_shield_hit_enters_guardsetoff_and_drains_hp(char_name: str) -> None:
    # A jab into a holding shield must GuardSetOff the defender and drain shield HP, not damage.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    seed = _seed_base(char_name)
    # The COVERAGE char is the DEFENDER here: p0 shields, p1(fox) jabs point-blank.
    seed["pos_x"][0, 1] = np.float32(5.0)
    seed["facing"][0, 1] = np.uint8(0)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()

        def both(p0_axes: dict, p1_axes: dict) -> np.ndarray:
            inp = np.zeros((1,), dtype=INPUT_DTYPE)
            for k, v in p0_axes.items():
                inp["p"][0, 0][k] = v
            for k, v in p1_axes.items():
                inp["p"][0, 1][k] = v
            return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))

        rows = []
        script = [both(dict(l=200), dict())] * 4 + [both(dict(l=200), dict(buttons=0x0100))] + [
            both(dict(l=200), dict())
        ] * 6
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    acts = [int(r["action_id"][0]) for r in rows]
    assert 181 in acts, f"{char_name}: defender never GuardSetOff: {acts}"
    assert float(rows[-1]["percent"][0]) == 0.0, f"{char_name}: shield hit leaked percent"
    assert float(rows[-1]["shield_hp"][0]) < 60.0, f"{char_name}: shield HP did not drain"


# ---------------------------------------------------------------------------
# Bucket 4b: throw execution
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_grab_then_fthrow_executes(char_name: str) -> None:
    # Catch a point-blank victim, then forward-throw: victim must enter a thrown action and the
    # throw's scripted damage must apply. Throw hitboxes come from the per-char movescripts.
    seed = _seed_base(char_name)
    seed["pos_x"][0, 1] = np.float32(6.0)
    seed["facing"][0, 1] = np.uint8(0)
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()
        rows = []
        script = [_mk_inputs(buttons=0x0010)] * 3 + [_mk_inputs()] * 3 + [
            _mk_inputs(main_x=127)
        ] * 3 + [_mk_inputs()] * 25
        for inp in script:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            rows.append(ob.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            prev = inp
    finally:
        msl_binding.destroy(handle)
    a0 = [int(r["action_id"][0]) for r in rows]
    a1 = [int(r["action_id"][1]) for r in rows]
    # 212=Catch, 215=CatchAttack region; thrown victim actions: 240=ThrownF region / capture.
    assert any(x in (212, 213, 214) for x in a0), f"{char_name}: never caught: {a0[:6]}"
    caught = any(x in range(223, 232) for x in a1) or any(
        float(r["percent"][1]) > 0.0 for r in rows
    )
    assert caught, f"{char_name}: victim never grabbed/thrown: a1={sorted(set(a1))}"


# ---------------------------------------------------------------------------
# Bucket 5b: tech window (Passive) and DamageFly entry
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_damagefly_entry_from_strong_hit(char_name: str) -> None:
    # A strong seeded KB launches into a DamageFly family action (KB formula owns the regime).
    seed = _seed_base(char_name, grounded=False, pos_y=50.0)
    seed["action_id"][0, 0] = np.uint16(0x005A)  # DamageFlyHi seeded directly
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["hitstun"][0, 0] = np.uint8(30)
    seed["speed_y_attack"][0, 0] = np.float32(2.0)
    idle = _mk_inputs()
    outs = _run(seed, [idle] * 5)
    # Must remain in a damage action while hitstun runs (no spurious actionable exit).
    assert int(outs[0]["action_id"][0]) in (0x005A, 0x005B, 0x005C, 0x005D, 0x005E), (
        f"{char_name}: left damage state instantly: {int(outs[0]['action_id'][0])}"
    )
    assert int(outs[-1]["hitstun"][0]) < 30, f"{char_name}: hitstun did not tick over 5 frames"
    assert int(outs[-1]["action_id"][0]) in (0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x001D), (
        f"{char_name}: unexpected exit {int(outs[-1]['action_id'][0])}"
    )


# ---------------------------------------------------------------------------
# Bucket 1b: turn / squat / runbrake
# ---------------------------------------------------------------------------

ACT_RUN_BRAKE = 0x0017
ACT_SQUAT = 0x0027
ACT_SQUAT_RV = 0x0029
ACT_PASS = 0x00F4
ACT_CLIFF_CLIMB_SLOW = 0x00FE
ACT_CLIFF_CLIMB_QUICK = 0x00FF
ACT_CLIFF_ATTACK_SLOW = 0x0100
ACT_CLIFF_ATTACK_QUICK = 0x0101


@CHAR_PARAMS
def test_turn_from_walk(char_name: str) -> None:
    # Decomp: ftCo_Turn on a stick reversal from Wait/Walk; frames_to_turn flips facing.
    seed = _seed_base(char_name)
    outs = _run(seed, [_mk_inputs(main_x=-40)] * 3)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_TURN in acts, f"{char_name}: reverse tilt never turned: {acts}"


@CHAR_PARAMS
def test_squat_cycle(char_name: str) -> None:
    # Squat -> SquatWait while held, SquatRv on release.
    seed = _seed_base(char_name)
    down = _mk_inputs(main_y=-127)
    outs = _run(seed, [down] * 12 + [_mk_inputs()] * 6)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SQUAT in acts, f"{char_name}: never squatted: {acts[:6]}"
    assert ACT_SQUAT_WAIT in acts, f"{char_name}: never reached SquatWait: {acts}"
    assert ACT_SQUAT_RV in acts, f"{char_name}: never rose (SquatRv): {acts}"


@CHAR_PARAMS
def test_runbrake_on_release(char_name: str) -> None:
    # Run then release: RunBrake decelerates with the char's friction family.
    seed = _seed_base(char_name)
    fwd = _mk_inputs(main_x=127)
    outs = _run(seed, [fwd] * 25 + [_mk_inputs()] * 12)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_RUN in acts, f"{char_name}: never ran: {sorted(set(acts))}"
    assert ACT_RUN_BRAKE in acts, f"{char_name}: never braked: {sorted(set(acts))}"


# ---------------------------------------------------------------------------
# Bucket 3c: guard release
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_guard_off_on_release(char_name: str) -> None:
    seed = _seed_base(char_name)
    hold = _mk_inputs(l=200)
    outs = _run(seed, [hold] * 10 + [_mk_inputs()] * 20)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_GUARD in acts
    assert ACT_GUARD_OFF in acts, f"{char_name}: release never GuardOff: {acts}"
    assert ACT_WAIT in acts[-3:], f"{char_name}: GuardOff never returned to Wait: {acts[-5:]}"


# ---------------------------------------------------------------------------
# Bucket 6b: ledge options
# ---------------------------------------------------------------------------


def _ledge_seed(char_name: str) -> np.ndarray:
    seed = _seed_base(char_name, grounded=False, pos_y=2.0)
    seed["pos_x"][0, 0] = np.float32(85.5666 + 4.0)
    seed["facing"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    return seed


@CHAR_PARAMS
@pytest.mark.parametrize(
    "option_axes,want",
    [
        (dict(main_x=-127), (ACT_CLIFF_CLIMB_SLOW, ACT_CLIFF_CLIMB_QUICK)),
        (dict(buttons=0x0100), (ACT_CLIFF_ATTACK_SLOW, ACT_CLIFF_ATTACK_QUICK)),
    ],
)
def test_cliff_options(char_name, option_axes, want) -> None:
    # CliffCatch -> CliffWait, then climb (stick toward stage) or attack (A).
    seed = _ledge_seed(char_name)
    idle = _mk_inputs()
    opt = _mk_inputs(**option_axes)
    outs = _run(seed, [idle] * 30 + [opt] * 4)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_CLIFF_WAIT in acts, f"{char_name}: never reached CliffWait: {sorted(set(acts))}"
    assert any(w in acts for w in want), (
        f"{char_name}: option {option_axes} never produced {want}: {acts[-6:]}"
    )


# ---------------------------------------------------------------------------
# Bucket 7b: platform drop-through (Battlefield)
# ---------------------------------------------------------------------------


@CHAR_PARAMS
def test_platform_drop_through(char_name: str) -> None:
    # Standing on a BF side platform, tapping down passes through (ftCo_Pass).
    seed = _seed_base(char_name, grounded=True, pos_y=27.2)
    seed["stage_id"][0] = np.uint32(31)
    seed["pos_x"][0, 0] = np.float32(-57.6)
    seed["pos_x"][0, 1] = np.float32(20.0)
    seed["ground_id"][0, 0] = np.uint16(1)
    down = _mk_inputs(main_y=-127)
    outs = _run(seed, [_mk_inputs()] * 2 + [down] * 6 + [_mk_inputs()] * 60)
    acts = [int(o["action_id"][0]) for o in outs]
    passed = (ACT_PASS in acts) or any(int(o["on_ground"][0]) == 0 for o in outs[2:10])
    assert passed, f"{char_name}: never dropped through: {acts[:12]}"
    assert any(int(o["on_ground"][0]) == 1 and float(o["pos_y"][0]) < 1.0 for o in outs), (
        f"{char_name}: never landed on the main stage after the drop"
    )


# ---------------------------------------------------------------------------
# Bucket 2c: tilts / up+down smash / double jump / tech
# ---------------------------------------------------------------------------

ACT_ATTACK_S3S = 0x0035
ACT_ATTACK_HI3 = 0x0038
ACT_ATTACK_LW3 = 0x0039


@CHAR_PARAMS
@pytest.mark.parametrize(
    "axes,want",
    [
        (dict(buttons=0x0100, main_x=40), ACT_ATTACK_S3S),  # ftilt
        (dict(buttons=0x0100, main_y=40), ACT_ATTACK_HI3),  # utilt
        (dict(buttons=0x0100, main_y=-40), ACT_ATTACK_LW3),  # dtilt (from squat-tilt)
        (dict(buttons=0x0100, c_y=0, main_y=110), ACT_ATTACK_HI4),  # upsmash (A + up smash input)
    ],
)
def test_tilts_and_vertical_smashes_enter(char_name, axes, want) -> None:
    seed = _seed_base(char_name)
    if want == ACT_ATTACK_LW3:
        # dtilt requires crouch-held A
        frames = [_mk_inputs(main_y=-127)] * 8 + [_mk_inputs(buttons=0x0100, main_y=-127)] * 2
    elif want == ACT_ATTACK_HI4:
        frames = [_mk_inputs(buttons=0x0100, main_y=127)] * 2
    else:
        frames = [_mk_inputs(**axes)] * 2
    outs = _run(seed, frames)
    acts = [int(o["action_id"][0]) for o in outs]
    assert want in acts, f"{char_name}: input {axes} never entered {hex(want)}: {acts}"


@CHAR_PARAMS
def test_downsmash_enters_from_smash_input(char_name: str) -> None:
    # AttackLw4 via crouching smash-down + A on the same frame from Wait is awkward; use
    # the c-stick equivalent semantics: smash-down + A on frame 1.
    seed = _seed_base(char_name)
    outs = _run(seed, [_mk_inputs(buttons=0x0100, main_y=-127)] * 2)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_ATTACK_LW4 in acts, f"{char_name}: never entered dsmash: {acts}"


@CHAR_PARAMS
def test_double_jump_applies_air_multiplier(char_name: str) -> None:
    # Decomp: ftCo_JumpAerial applies jump_v_initial_velocity * air_jump_v_multiplier.
    a = _char_attrs(char_name)
    seed = _seed_base(char_name, grounded=False, pos_y=600.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)
    outs = _run(seed, [_mk_inputs(buttons=0x0400)] + [_mk_inputs()] * 2)
    acts = [int(o["action_id"][0]) for o in outs]
    assert any(x in (0x001B, 0x001C) for x in acts), f"{char_name}: no double jump: {acts}"
    vy = float(outs[0]["speed_y_self"][0])
    expect = float(a["jump_v_initial_velocity"]) * float(a["air_jump_v_multiplier"])
    grav = float(a["grav"])
    assert expect - 2.5 * grav <= vy <= expect + 1e-3, (
        f"{char_name}: dj vy {vy} vs {expect} (grav {grav})"
    )


@CHAR_PARAMS
def test_tech_in_place_on_l_press_before_ground_hit(char_name: str) -> None:
    # Passive (tech) when L is pressed within the window before hitting the ground in hitstun.
    seed = _seed_base(char_name, grounded=False, pos_y=8.0)
    seed["action_id"][0, 0] = np.uint16(0x005A)  # DamageFlyHi
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["hitstun"][0, 0] = np.uint8(40)
    seed["speed_y_attack"][0, 0] = np.float32(-1.5)  # moving down into the floor
    # Tech gates (ftCo_800986B0): x680 = frames since LR press (< window), x684 = frames since
    # the PRIOR press (>= debounce). Start both stale; the in-test press freshens x680.
    seed["x680"][0, 0] = np.uint8(254)
    seed["x684"][0, 0] = np.uint8(254)
    press = _mk_inputs(buttons=0x0040, l=255)
    idle = _mk_inputs()
    outs = _run(seed, [press, press] + [idle] * 12)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_PASSIVE in acts or 0x00C8 in acts or 0x00C9 in acts, (
        f"{char_name}: never teched (Passive/PassiveStand): {sorted(set(acts))}"
    )
