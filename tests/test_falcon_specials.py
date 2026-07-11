"""Captain Falcon character-mechanic decomp-anchored unit tests.

Anchors: refs/melee/src/melee/ft/chara/ftCaptain and data/moves/falcon.json. Replay verification:
the Falcon suite covers the steady callback rows; these tests cover full input-to-outcome chains
and source-owner boundaries that replay reseeds cannot exercise live.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
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
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

DEBUG_INTERNALS_DTYPE = np.dtype(
    [
        ("tilt_timer_x", ("u1", (4,))),
        ("turn_frames_to_turn", ("u1", (4,))),
        ("turn_has_turned", ("u1", (4,))),
        ("guard_reflect_timer_x14", ("u1", (4,))),
        ("entry_end_fall_lock", ("u1", (4,))),
        ("attack_id", ("<u2", (4,))),
        ("attack_instance", ("<u2", (4,))),
        ("attack_identity_last_action_id", ("<u2", (4,))),
        ("instance_id", ("<u2", (4,))),
        ("instance_id_x2073", ("u1", (4,))),
        ("instance_identity_last_action_id", ("<u2", (4,))),
        ("instance_id_counter", "<u2"),
        ("item_spawn_id_counter", "<u4"),
        ("throw_pulse_consumed", ("u1", (4,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (4,))),
        ("throw_pending_victim_port", ("u1", (4,))),
        ("throw_pending_hit_idx", ("u1", (4,))),
        ("attached_victim_port", ("u1", (4,))),
        ("grab_owner_port", ("u1", (4,))),
        ("catch_kind_x1a68", ("<u2", (4,))),
        ("catch_target_mask_x1a6a", ("<u2", (4,))),
        ("grab_constraint_x2226_b2", ("u1", (4,))),
        ("falcon_specialhi_x221b_b7", ("u1", (4,))),
        ("ecb_lock_timer", ("u1", (4,))),
        ("ecb_lock_owner", ("u1", (4,))),
        ("fall_fast", ("u1", (4,))),
        ("prev_pos_x", ("<f4", (4,))),
        ("prev_pos_y", ("<f4", (4,))),
        ("floor_sweep_prev_pos_x", ("<f4", (4,))),
        ("floor_sweep_prev_pos_y", ("<f4", (4,))),
        ("coll_last_pos_x", ("<f4", (4,))),
        ("coll_last_pos_y", ("<f4", (4,))),
    ],
    align=False,
)


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "falcon.json").read_text())


def test_common_capture_release_hit_x380_is_extracted_and_required() -> None:
    common = json.loads((ROOT / "data" / "common" / "ft_common_data.json").read_text())
    assert {
        key: common[key]
        for key in (
            "capture_release_hit_state_x380",
            "capture_release_hit_damage_x384",
            "capture_release_hit_angle_x388",
            "capture_release_hit_kbg_x38c",
            "capture_release_hit_wsk_x390",
            "capture_release_hit_bkb_x394",
            "capture_release_hit_element_x398",
            "capture_release_hit_sfx_severity_x39c",
            "capture_release_hit_sfx_kind_x3a0",
        )
    } == {
        "capture_release_hit_state_x380": 0,
        "capture_release_hit_damage_x384": 0,
        "capture_release_hit_angle_x388": 361,
        "capture_release_hit_kbg_x38c": 0,
        "capture_release_hit_wsk_x390": 0,
        "capture_release_hit_bkb_x394": 20,
        "capture_release_hit_element_x398": 0,
        "capture_release_hit_sfx_severity_x39c": 1,
        "capture_release_hit_sfx_kind_x3a0": 0,
    }
    loader = (ROOT / "src" / "common_params.c").read_text(encoding="utf-8")
    extractor = (ROOT / "tools" / "extraction" / "extract_ftcommon_data.py").read_text(
        encoding="utf-8"
    )
    for key in common:
        if key.startswith("capture_release_hit_"):
            assert f'"{key}"' in loader
            assert f'"{key}"' in extractor


def test_falcon_special_attrs_are_required_for_runtime_load(tmp_path: Path) -> None:
    """Missing ftCaptain_DatAttrs keys must fail init instead of zeroing a special."""

    data_dir = tmp_path / "data"
    data_dir.mkdir()
    root_data = ROOT / "data"
    for child in root_data.iterdir():
        if child.name != "characters":
            (data_dir / child.name).symlink_to(child, target_is_directory=child.is_dir())

    chars_dir = data_dir / "characters"
    chars_dir.mkdir()
    for src in (root_data / "characters").glob("*.json"):
        dst = chars_dir / src.name
        if src.name != "falcon.json":
            dst.write_bytes(src.read_bytes())
            continue
        attrs = json.loads(src.read_text(encoding="utf-8"))
        attrs.pop("falcon_specialhi_catch_grav")
        dst.write_text(json.dumps(attrs, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    code = "import msl_binding\nmsl_binding.init(batch_size=1, num_players=2)\n"
    env = os.environ.copy()
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode != 0
    combined = proc.stderr + proc.stdout
    assert "char params Falcon special attr parse failed" in combined
    assert "msl_batch_create failed" in combined


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
    handle = msl_binding.init(batch_size=1, num_players=int(seed["num_players"][0]))
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


def _run_with_internals(
    seed: np.ndarray, frames: list[np.ndarray]
) -> list[tuple[np.void, np.void]]:
    import msl_binding

    sizes = msl_binding.sizes()
    assert int(sizes["internals"]) == DEBUG_INTERNALS_DTYPE.itemsize
    handle = msl_binding.init(batch_size=1, num_players=int(seed["num_players"][0]))
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        outs = []
        prev = _mk_inputs()
        for inp in frames:
            msl_binding.step_input(handle, prev, inp)
            compare_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            internals_u8 = np.zeros((1, int(sizes["internals"])), dtype=np.uint8)
            msl_binding.write_compare(handle, compare_u8)
            msl_binding.debug_write_internals(handle, internals_u8)
            outs.append(
                (
                    compare_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
                    internals_u8.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy(),
                )
            )
            prev = inp
        return outs
    finally:
        msl_binding.destroy(handle)


def _internals_immediately_after_reseed(seed: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=int(seed["num_players"][0]))
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        internals_u8 = np.zeros((1, int(sizes["internals"])), dtype=np.uint8)
        msl_binding.debug_write_internals(handle, internals_u8)
        return internals_u8.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _debug_dive_fighter_damage(
    *,
    grounded_victim_mode: bool,
    damaged_players: tuple[int, ...],
    damage: float = 8.0,
    near_floor: bool = False,
    victim_x221c_b0: bool = False,
    victim_x198c: int = 0,
    force_fallback_y: float | None = None,
    force_constrained_grounded: bool | None = None,
    follow_collision: bool = False,
    constrained_facing: int | None = None,
    seed_override: np.ndarray | None = None,
) -> tuple[np.void, np.void, np.ndarray]:
    import msl_binding

    seed = (
        _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
        if seed_override is None
        else seed_override.copy()
    )
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, 2] = np.uint8(4)
    seed["char_id"][0, 2] = np.uint8(FOX)
    seed["action_id"][0, 2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 2] = np.uint32(SM_WAIT1)
    seed["frame_speed_mul_f32"][0, 2] = np.float32(1.0)
    seed["instance_id"][0, 2] = np.uint16(700)
    if victim_x221c_b0:
        seed["state_flags"][0, 1, 3] |= np.uint8(0x80)
    seed["colanim_hit_status_x198c"][0, 1] = np.uint8(victim_x198c)
    if constrained_facing is not None:
        constrained = 0 if grounded_victim_mode else 1
        seed["facing"][0, constrained] = np.uint8(constrained_facing)
    if near_floor:
        seed["pos_x"][0, :2] = np.float32(0.0)
        seed["pos_y"][0, :2] = np.float32(0.25)
        seed["on_ground"][0, :2] = np.uint8(1)
        seed["ground_id"][0, :2] = np.uint16(1)
    if force_fallback_y is not None:
        constrained = 0 if grounded_victim_mode else 1
        sample = 1 - constrained
        seed["pos_x"][0, :2] = np.float32(0.0)
        seed["pos_y"][0, :2] = np.float32(force_fallback_y)
        seed["ground_id"][0, sample] = np.uint16(0xFFFF)
        if force_constrained_grounded is not None:
            seed["on_ground"][0, constrained] = np.uint8(force_constrained_grounded)

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.debug_clear_hitboxes_world(handle, 0, 2)
        for p in (0, 1):
            msl_binding.debug_clear_hurtcaps_world(handle, 0, p)
        for p in damaged_players:
            msl_binding.debug_set_hurtcap_world(
                handle, 0, p, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5
            )
            msl_binding.debug_set_hurtcap_height(handle, 0, p, 0, 1)
        msl_binding.debug_set_hitbox_world(handle, 0, 2, 0, 0.0, 0.0, 0.0, 1.0, damage, 1)
        msl_binding.debug_set_hitbox_flags(
            handle, 0, 2, 0, int(HIT_GROUNDED | HIT_AERIAL)
        )
        msl_binding.debug_set_hitbox_group(handle, 0, 2, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 2, 0, 0, 100, 0, 70)
        msl_binding.debug_combat_resolve(handle)
        if follow_collision:
            neutral = _mk_inputs()
            msl_binding.step_input(handle, neutral, neutral)

        compare_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        internals_u8 = np.zeros((1, int(sizes["internals"])), dtype=np.uint8)
        msl_binding.write_compare(handle, compare_u8)
        msl_binding.debug_write_internals(handle, internals_u8)
        return (
            compare_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
            internals_u8.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy(),
            seed,
        )
    finally:
        msl_binding.destroy(handle)


def _debug_dive_two_fighter_producers(
    *, grounded_victim_mode: bool, holder_damage: float, victim_damage: float, reverse_order: bool
) -> tuple[np.void, np.void, np.ndarray]:
    import msl_binding

    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, 2:4] = np.uint8(4)
    seed["char_id"][0, 2:4] = np.uint8(FOX)
    seed["action_id"][0, 2:4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 2:4] = np.uint32(SM_WAIT1)
    seed["frame_speed_mul_f32"][0, 2:4] = np.float32(1.0)
    seed["instance_id"][0, 2:4] = np.uint16([700, 701])

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        for p in (2, 3):
            msl_binding.debug_clear_hitboxes_world(handle, 0, p)
        for p in (0, 1):
            msl_binding.debug_clear_hurtcaps_world(handle, 0, p)
        msl_binding.debug_set_hurtcap_world(handle, 0, 0, 0, -4.5, 0.0, 0.0, -3.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 3.5, 0.0, 0.0, 4.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 0, 0, 1)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        holder_attacker, victim_attacker = ((3, 2) if reverse_order else (2, 3))
        for attacker, x, damage in (
            (holder_attacker, -4.0, holder_damage),
            (victim_attacker, 4.0, victim_damage),
        ):
            msl_binding.debug_set_hitbox_world(handle, 0, attacker, 0, x, 0.0, 0.0, 0.75, damage, 1)
            msl_binding.debug_set_hitbox_flags(handle, 0, attacker, 0, int(HIT_GROUNDED | HIT_AERIAL))
            msl_binding.debug_set_hitbox_group(handle, 0, attacker, 0, 0)
            msl_binding.debug_set_hitbox_kb_params(handle, 0, attacker, 0, 0, 100, 0, 70)
        msl_binding.debug_combat_resolve(handle)

        compare_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        internals_u8 = np.zeros((1, int(sizes["internals"])), dtype=np.uint8)
        msl_binding.write_compare(handle, compare_u8)
        msl_binding.debug_write_internals(handle, internals_u8)
        return (
            compare_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
            internals_u8.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy(),
            seed,
        )
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


def test_air_specialn_procedural_angle_uses_processed_stick_deadzone() -> None:
    # Fighter_8006A360 deadzones raw pad input before SpecialAirN_IASA consumes cmd_vars[0]. A raw
    # Y value inside the common deadzone therefore produces the neutral horizontal impulse.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::{
    #   ftCa_SpecialAirN_IASA,ftCaptain_SpecialN_GetAngleVel}
    seed = _seed(False, pos_y=80.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_SPECIAL_AIR_N)
    seed["animation_index"][0, 0] = np.uint32(302)
    seed["action_frame"][0, 0] = np.int16(49)
    seed["anim_frame_f32"][0, 0] = np.float32(49.0)

    out = _run(seed, [_mk_inputs(main_y=16)])[0]

    assert float(out["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-7)
    assert float(out["speed_air_x_self"][0]) == pytest.approx(
        float(_attrs()["falcon_specialn_vel_x"]) * float(_attrs()["falcon_specialn_vel_mul"]),
        abs=2e-6,
    )


def test_air_specialn_command_pulse_waits_for_hitlag_thaw() -> None:
    seed = _seed(False, pos_y=80.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_SPECIAL_AIR_N)
    seed["animation_index"][0, 0] = np.uint32(302)
    seed["action_frame"][0, 0] = np.int16(49)
    seed["anim_frame_f32"][0, 0] = np.float32(49.0)
    seed["hitlag"][0, 0] = np.uint16(3)

    up = _mk_inputs(main_y=80)
    outs = _run(seed, [up, up, up])

    assert [int(out["hitlag"][0]) for out in outs] == [2, 1, 0]
    assert all(float(out["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-7) for out in outs[:2])
    assert float(outs[2]["speed_y_self"][0]) > 0.0


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


# ---------------------------------------------------------------------------
# Falcon Kick (ftCa_SpecialLw; actions 357..362, rebound 363)
# ---------------------------------------------------------------------------

ACT_FC_LW = 357
ACT_FC_LW_END = 358
ACT_FC_AIR_LW = 359
ACT_FC_AIR_LW_END = 360
ACT_FC_AIR_LW_END_AIR = 361
ACT_FC_LW_END_AIR = 362
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


def test_falcon_kick_four_player_processhit_handles_multi_victim_team_attack() -> None:
    # Three BODY contacts across the travel each reach Fighter_ProcessHit's deal_dmg_cb owner. The
    # supported teams path models team attack enabled, so the same-team p1 remains a legal victim
    # rather than being silently filtered by team id.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialHi_800E400C
    multi = _seed_far(True)
    multi["num_players"][0] = np.uint8(4)
    multi["stocks"][0, :4] = np.uint8(4)
    multi["char_id"][0, :4] = np.array([FALCON, FOX, FOX, FOX], dtype=np.uint8)
    multi["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    multi["shield_hp"][0, :4] = np.float32(60.0)
    multi["jumps_left"][0, :4] = np.uint8(2)
    multi["x676_x"][0, :4] = np.uint8(0xFE)
    multi["on_ground"][0, :4] = np.uint8(1)
    multi["facing"][0, :4] = np.uint8(1)
    multi["action_id"][0, :4] = np.uint16(ACT_WAIT)
    multi["animation_index"][0, :4] = np.uint32(SM_WAIT1)
    multi["pos_x"][0, :4] = np.array([-70.0, -35.0, -35.0, -35.0], dtype=np.float32)
    multi["is_teams"][0] = np.uint8(1)
    multi["team_id"][0, :4] = np.array([0, 0, 1, 1], dtype=np.uint8)

    single = multi.copy()
    single["pos_x"][0, 2:] = np.float32(70.0)
    frames = [_mk_inputs(**DOWN_B)] + [_mk_inputs()] * 70
    multi_out = _run(multi, frames)
    single_out = _run(single, frames)

    for defender in (1, 2, 3):
        assert float(multi_out[-1]["percent"][defender]) > 0.0
    hit_frame = next(i for i, out in enumerate(multi_out) if float(out["percent"][1]) > 0.0)
    compare_frame = hit_frame + 8
    multi_dx = float(multi_out[compare_frame + 4]["pos_x"][0]) - float(
        multi_out[compare_frame]["pos_x"][0]
    )
    single_dx = float(single_out[compare_frame + 4]["pos_x"][0]) - float(
        single_out[compare_frame]["pos_x"][0]
    )
    assert multi_dx < single_dx


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


def _step_once_with_walljump_state(
    seed: np.ndarray, prev: np.ndarray, cur: np.ndarray
) -> tuple[np.void, tuple[int, int]]:
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(
            handle, seed.view(np.uint8).reshape((1, int(sizes["seed"])))
        )
        msl_binding.step_input(handle, prev, cur)
        out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, out_u8)
        out = out_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        walljump = tuple(int(v) for v in msl_binding.debug_walljump_state(handle, 0, 0))
        return out, walljump
    finally:
        msl_binding.destroy(handle)


def test_raptor_boost_ground_entry_applies_full_d7fc_state_reset() -> None:
    # resetCmdVarsGround runs ftCommon_8007D7FC before SpecialSStart's flags=0 motion change.
    # Deliberately stale grounded lanes must not survive entry.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{resetCmdVarsGround,
    #   ftCa_SpecialS_Enter}
    seed = _seed_vs(200.0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["walljump_used_count"][0, 0] = np.uint8(3)
    seed["fall_fast"][0, 0] = np.uint8(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ecb_lock_bottom_owner_u8"][0, 0] = np.uint8(4)

    out, walljump = _step_once_with_walljump_state(
        seed, _mk_inputs(), _mk_inputs(**SIDE_B)
    )
    out_internal, internal = _run_with_internals(seed, [_mk_inputs(**SIDE_B)])[0]

    assert int(out["action_id"][0]) == ACT_FC_S_START
    assert int(out_internal["action_id"][0]) == ACT_FC_S_START
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"])
    # The newly entered root-motion state runs Phys later in the same frame; both public velocity
    # lanes must reflect that extracted callback-local result rather than either stale seed lane.
    assert float(out["speed_air_x_self"][0]) == pytest.approx(
        float(out["speed_ground_x_self"][0]), abs=1e-7
    )
    assert walljump[0] == 0
    assert int(internal["fall_fast"][0]) == 0
    assert int(internal["ecb_lock_timer"][0]) == 0
    assert int(internal["ecb_lock_owner"][0]) == 0


@pytest.mark.parametrize(
    ("x", "target_x", "facing", "ground_id", "expected_self_bits"),
    [
        (-40.0, -50.0, 0, 2, 0xC0581068),
        (40.0, 50.0, 1, 6, 0x40581068),
        (20.0, 30.0, 1, 3, 0x40581068),
    ],
)
def test_raptor_boost_ground_detect_applies_full_d7fc_state_reset(
    x: float, target_x: float, facing: int, ground_id: int, expected_self_bits: int
) -> None:
    # onDetectGround independently calls D7FC before entering the hit state. Seed directly inside
    # the inert-contact window so this locks that second source callsite rather than entry setup.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::onDetectGround
    seed = _seed_vs(10.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(20.0)
    seed["stage_id"][0] = np.uint32(8)  # Yoshi's generated left/right slopes + flat center
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_x"][0, 1] = np.float32(target_x)
    seed["facing"][0, 0] = np.uint8(facing)
    seed["facing_dir1"][0, 0] = np.int8(1 if facing else -1)
    seed["ground_id"][0, 0] = np.uint16(ground_id)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["walljump_used_count"][0, 0] = np.uint8(3)
    seed["fall_fast"][0, 0] = np.uint8(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ecb_lock_bottom_owner_u8"][0, 0] = np.uint8(4)

    out, walljump = _step_once_with_walljump_state(seed, _mk_inputs(), _mk_inputs())
    out_internal, internal = _run_with_internals(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FC_S
    assert int(out_internal["action_id"][0]) == ACT_FC_S
    actual_self = np.float32(out["speed_air_x_self"][0])
    assert int(actual_self.view(np.uint32)) == expected_self_bits
    expected_self_x = float(actual_self)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        expected_self_x * float(_attrs()["falcon_specials_gr_vel_x"]), abs=1e-7
    )
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"])
    assert walljump[0] == 0
    assert int(internal["fall_fast"][0]) == 0
    assert int(internal["ecb_lock_timer"][0]) == 0
    assert int(internal["ecb_lock_owner"][0]) == 0


def test_raptor_wall_stop_defers_wait_iasa_until_next_fighter_update() -> None:
    import msl_binding

    seed = _seed_vs(200.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(20.0)
    seed["facing"][0, 0] = np.uint8(1)
    crouch = _mk_inputs(main_y=-127)
    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(
            handle, seed.view(np.uint8).reshape((1, int(sizes["seed"])))
        )
        # Run the normal IASA/input phase first, then isolate the Coll callback with a real
        # left-wall push bit. This is the source ordering of SpecialSStart_Coll.
        msl_binding.step_input(handle, crouch, crouch)
        msl_binding.debug_set_coll_env_flags(handle, 0, 0, 0x00000001)
        msl_binding.debug_run_locomotion_post_collision(handle)
        out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, out_u8)
        stopped = out_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(stopped["action_id"][0]) == ACT_WAIT

        msl_binding.step_input(handle, crouch, crouch)
        msl_binding.write_compare(handle, out_u8)
        next_frame = out_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(next_frame["action_id"][0]) == 0x0027  # Squat
    finally:
        msl_binding.destroy(handle)


def test_raptor_boost_four_player_detect_respects_team_attack_on_path() -> None:
    # The shared BODY/inert pass stays four-player safe and does not treat team id as a friendly-
    # fire-off proxy; the supported teams configuration has team attack enabled.
    seed = _seed_vs(25.0)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.array([FALCON, FOX, FOX, FOX], dtype=np.uint8)
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["shield_hp"][0, :4] = np.float32(60.0)
    seed["jumps_left"][0, :4] = np.uint8(2)
    seed["x676_x"][0, :4] = np.uint8(0xFE)
    seed["on_ground"][0, :4] = np.uint8(1)
    seed["facing"][0, :4] = np.uint8(1)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT1)
    seed["pos_x"][0, :4] = np.array([0.0, 25.0, 70.0, -70.0], dtype=np.float32)
    seed["is_teams"][0] = np.uint8(1)
    seed["team_id"][0, :4] = np.array([0, 0, 1, 1], dtype=np.uint8)

    outs = _run(seed, [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 90)

    assert ACT_FC_S in [int(out["action_id"][0]) for out in outs]
    assert float(outs[-1]["percent"][1]) > 0.0
    assert float(outs[-1]["percent"][2]) == 0.0
    assert float(outs[-1]["percent"][3]) == 0.0


def test_raptor_boost_detects_eligible_item_hurtbox_without_damaging_item() -> None:
    # The inert Raptor Boost HitCapsule also writes unk_gobj when it overlaps an eligible item
    # hurtbox. Heiho is in the source Old_Kuri..Arwing_Laser ItemKind family; unlike a damaging
    # HitCapsule, this path must not damage, hitlag, or register the Shy Guy as a victim.
    # refs/melee/src/melee/it/itcoll.c::it_802703E8
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialS_OnDetect
    seed_hit = _seed_vs(200.0)
    seed_hit["stage_id"][0] = np.uint32(8)
    seed_hit["pos_x"][0, 0] = np.float32(-40.0)
    seed_hit["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)
    seed_hit["stage_yoshi_shyguy_timer_u16"][0] = np.uint16(120)
    shyguy = seed_hit["items"][0, 0]
    shyguy["exists"] = np.uint8(1)
    shyguy["type"] = np.uint16(210)  # It_Kind_Heiho
    shyguy["state"] = np.uint8(1)
    shyguy["owner"] = np.int8(-1)
    shyguy["spawn_id"] = np.uint32(88)
    shyguy["pos_x"] = np.float32(-25.0)
    shyguy["pos_y"] = np.float32(0.0)
    shyguy["direction"] = np.float32(1.0)

    seed_control = seed_hit.copy()
    seed_control["items"][0, 0]["exists"] = np.uint8(0)
    frames = [_mk_inputs(**SIDE_B)] + [_mk_inputs()] * 75
    hit = _run(seed_hit, frames)
    control = _run(seed_control, frames)
    hit_actions = [int(out["action_id"][0]) for out in hit]
    control_actions = [int(out["action_id"][0]) for out in control]

    assert ACT_FC_S in hit_actions
    assert ACT_FC_S not in control_actions
    transition = hit_actions.index(ACT_FC_S)
    assert all(int(out["items"][0]["damage"]) == 0 for out in hit[: transition + 1])
    # The subsequent SpecialS punch is an ordinary damaging HitCapsule and may hit the same item.
    assert next(i for i, out in enumerate(hit) if int(out["items"][0]["damage"]) > 0) > transition


@pytest.mark.parametrize(("falcon_p", "fox_p"), [(0, 1), (1, 0)])
def test_raptor_inert_item_contact_keeps_later_fighter_item_damage_order(
    falcon_p: int, fox_p: int
) -> None:
    import msl_binding

    seed = _seed(True)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(FOX)
    seed["char_id"][0, falcon_p] = np.uint8(FALCON)
    seed["pos_x"][0, :4] = np.float32(100.0)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT1)

    # Keep fighter bodies separated from the item. The contacts below are injected as independent
    # HitCapsules so Falcon cannot satisfy OnDetect through Fox's hurtboxes.
    seed["pos_x"][0, falcon_p] = np.float32(60.0)
    seed["action_id"][0, falcon_p] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, falcon_p] = np.uint32(303)
    seed["action_frame"][0, falcon_p] = np.int16(20)
    seed["anim_frame_f32"][0, falcon_p] = np.float32(20.0)

    seed["pos_x"][0, fox_p] = np.float32(-60.0)

    seed["stage_id"][0] = np.uint32(8)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)
    seed["stage_yoshi_shyguy_timer_u16"][0] = np.uint16(120)
    shyguy = seed["items"][0, 0]
    shyguy["exists"] = np.uint8(1)
    shyguy["type"] = np.uint16(210)
    shyguy["state"] = np.uint8(1)
    shyguy["owner"] = np.int8(-1)
    shyguy["spawn_id"] = np.uint32(88)
    shyguy["pos_x"] = np.float32(0.0)
    shyguy["pos_y"] = np.float32(0.0)
    shyguy["direction"] = np.float32(1.0)

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        msl_binding.reseed_seed(
            handle, seed.view(np.uint8).reshape((1, int(sizes["seed"])))
        )
        neutral = _mk_inputs()
        msl_binding.debug_step_input_pre_combat(handle, neutral, neutral)
        for p in (falcon_p, fox_p):
            msl_binding.debug_clear_hitboxes_world(handle, 0, p)
        item_flags = int(HIT_AERIAL | (1 << 11))
        msl_binding.debug_set_hitbox_world(handle, 0, falcon_p, 0, 0.0, 0.0, 0.0, 20.0, 0.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, falcon_p, 0, item_flags)
        msl_binding.debug_set_hitbox_element(handle, 0, falcon_p, 0, 11)
        msl_binding.debug_set_hitbox_world(handle, 0, fox_p, 0, 0.0, 0.0, 0.0, 20.0, 4.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, fox_p, 0, item_flags)
        msl_binding.debug_set_hitbox_element(handle, 0, fox_p, 0, 0)
        msl_binding.debug_set_hitbox_group(handle, 0, fox_p, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, fox_p, 0, 45, 100, 0, 20)
        msl_binding.debug_run_item_collision_phase(handle)
        msl_binding.debug_combat_resolve(handle)
        out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, out_u8)
        out = out_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy()

        # The inert and damaging contacts both run regardless of port traversal order.
        assert int(out["action_id"][falcon_p]) == ACT_FC_S
        assert int(out["items"][0]["damage"]) == 4
        assert int(out["items"][0]["state"]) == 3
        assert int(out["hitlag"][fox_p]) == 4

        victim_entry = np.dtype(
            [("id32", "<u4"), ("id16", "<u2"), ("kind_slot", "u1"), ("cd", "u1")]
        )
        capsule_dtype = np.dtype(
            [
                ("ring_1", "u1"),
                ("ring_2", "u1"),
                ("_pad", "V2"),
                ("victims_1", victim_entry, (12,)),
                ("victims_2", victim_entry, (12,)),
            ]
        )
        fox_capsule = (
            msl_binding.debug_hitlist_fighter_capsule(handle, 0, fox_p, 0)
            .reshape(-1)
            .view(capsule_dtype)[0]
        )
        assert any(
            int(entry["id32"]) == 88 and int(entry["kind_slot"]) == 0x40
            for entry in fox_capsule["victims_1"]
        )
        falcon_capsule = (
            msl_binding.debug_hitlist_fighter_capsule(handle, 0, falcon_p, 0)
            .reshape(-1)
            .view(capsule_dtype)[0]
        )
        assert not any(
            int(entry["kind_slot"]) == 0x40 for entry in falcon_capsule["victims_1"]
        )
    finally:
        msl_binding.destroy(handle)


def test_shyguy_accumulates_all_ordinary_attackers_before_single_damage_callback() -> None:
    import msl_binding

    snapshots: list[tuple[float, float, float]] = []
    for damage_by_port in ((4.0, 7.0), (7.0, 4.0)):
        seed = _seed(True)
        seed["num_players"][0] = np.uint8(4)
        seed["stocks"][0, :4] = np.uint8(4)
        seed["char_id"][0, :4] = np.uint8(FOX)
        seed["pos_x"][0, :4] = np.float32(60.0)
        seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
        seed["animation_index"][0, :4] = np.uint32(SM_WAIT1)
        seed["stage_id"][0] = np.uint32(8)
        seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)
        seed["stage_yoshi_shyguy_timer_u16"][0] = np.uint16(120)
        shyguy = seed["items"][0, 0]
        shyguy["exists"] = np.uint8(1)
        shyguy["type"] = np.uint16(210)
        shyguy["state"] = np.uint8(1)
        shyguy["owner"] = np.int8(-1)
        shyguy["spawn_id"] = np.uint32(88)
        shyguy["pos_x"] = np.float32(0.0)
        shyguy["pos_y"] = np.float32(0.0)
        shyguy["direction"] = np.float32(1.0)

        sizes = msl_binding.sizes()
        handle = msl_binding.init(batch_size=1, num_players=4)
        try:
            msl_binding.reseed_seed(
                handle, seed.view(np.uint8).reshape((1, int(sizes["seed"])))
            )
            neutral = _mk_inputs()
            msl_binding.debug_step_input_pre_combat(handle, neutral, neutral)
            item_flags = int(HIT_AERIAL | (1 << 11))
            for p, damage in enumerate(damage_by_port):
                msl_binding.debug_clear_hitboxes_world(handle, 0, p)
                msl_binding.debug_set_hitbox_world(
                    handle, 0, p, 0, 0.0, 0.0, 0.0, 20.0, damage, 1
                )
                msl_binding.debug_set_hitbox_flags(handle, 0, p, 0, item_flags)
                msl_binding.debug_set_hitbox_element(handle, 0, p, 0, 0)
                msl_binding.debug_set_hitbox_group(handle, 0, p, 0, 0)
                msl_binding.debug_set_hitbox_kb_params(handle, 0, p, 0, 45, 100, 0, 20)
            msl_binding.debug_run_item_collision_phase(handle)

            out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, out_u8)
            out = out_u8.view(COMPARE_DTYPE).reshape((1,))[0]
            assert int(out["items"][0]["damage"]) == 11
            assert [int(out["hitlag"][p]) for p in range(2)] == [
                4 if damage == 4.0 else 5 for damage in damage_by_port
            ]
            victim_entry = np.dtype(
                [("id32", "<u4"), ("id16", "<u2"), ("kind_slot", "u1"), ("cd", "u1")]
            )
            capsule_dtype = np.dtype(
                [
                    ("ring_1", "u1"),
                    ("ring_2", "u1"),
                    ("_pad", "V2"),
                    ("victims_1", victim_entry, (12,)),
                    ("victims_2", victim_entry, (12,)),
                ]
            )
            for p in range(2):
                capsule = (
                    msl_binding.debug_hitlist_fighter_capsule(handle, 0, p, 0)
                    .reshape(-1)
                    .view(capsule_dtype)[0]
                )
                assert any(
                    int(entry["id32"]) == 88 and int(entry["kind_slot"]) == 0x40
                    for entry in capsule["victims_1"]
                )
            snapshots.append(
                (
                    float(out["items"][0]["vel_x"]),
                    float(out["items"][0]["vel_y"]),
                    float(out["items"][0]["direction"]),
                )
            )
        finally:
            msl_binding.destroy(handle)
    assert snapshots[0] == pytest.approx(snapshots[1])


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
    # A missing carried floor models the bounded `ft_800827A0 == false` callback result without
    # depending on a particular legal-stage ledge's edge-snap admission.
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(17)
    seed["anim_frame_f32"][0, 0] = np.float32(17.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(5.2283)
    seed["speed_air_x_self"][0, 0] = np.float32(5.2283)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 0
    assert int(out["jumps_left"][0]) == 0
    assert float(out["speed_air_x_self"][0]) == pytest.approx(float(_attrs()["air_drift_max"]), abs=1e-6)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


@pytest.mark.parametrize("action_frame", [10, 35])
def test_raptor_boost_ground_floor_loss_outside_cmd2_window_enters_ordinary_fall(
    action_frame: int,
) -> None:
    # SpecialSStart_Coll uses ft_80084104 while script cmd2 is clear (before frame 15 and after
    # frame 34). Its floor-loss destination is ordinary Fall with ftCommon_8007D5D4, not the
    # live-window FallSpecial/miss-lag bundle.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Coll
    # data/moves/falcon.json::specials_by_msid.303 set_cmd_var(idx=2)@15/35
    seed = _seed_vs(200.0)
    # A missing carried floor models the bounded `ft_800827A0 == false` callback result without
    # depending on a particular legal-stage ledge's edge-snap admission.
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_S_START)
    seed["animation_index"][0, 0] = np.uint32(303)
    seed["action_frame"][0, 0] = np.int16(action_frame)
    seed["anim_frame_f32"][0, 0] = np.float32(action_frame)
    seed["speed_ground_x_self"][0, 0] = np.float32(5.2283)
    seed["speed_air_x_self"][0, 0] = np.float32(5.2283)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["jumps_left"][0]) == int(_attrs()["max_jumps"]) - 1
    assert int(out["ground_id"][0]) == 0xFFFF
    assert abs(float(out["speed_air_x_self"][0])) <= float(_attrs()["air_drift_max"])
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_raptor_boost_reseed_is_idempotent_at_gravity_window_boundary() -> None:
    # Frame 29 is immediately before the script gives SpecialAirSStart's Phys callback gravity
    # ownership. Reusing a batch lane must reconstruct the hidden accumulator from this seed, not
    # retain the value produced by the preceding attempt.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialAirSStart_Phys
    import msl_binding

    seed = _seed_vs(200.0, grounded=False, y=25.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_AIR_S_START)
    seed["animation_index"][0, 0] = np.uint32(305)
    seed["action_frame"][0, 0] = np.int16(29)
    seed["anim_frame_f32"][0, 0] = np.float32(29.0)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    outputs: list[np.void] = []
    try:
        seed_u8 = seed.view(np.uint8).reshape((1, int(sizes["seed"])))
        neutral = _mk_inputs()
        for _ in range(4):
            msl_binding.reseed_seed(handle, seed_u8)
            msl_binding.step_input(handle, neutral, neutral)
            out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, out_u8)
            outputs.append(out_u8.view(COMPARE_DTYPE).reshape((1,))[0].copy())
    finally:
        msl_binding.destroy(handle)

    first = outputs[0]
    assert all(out.tobytes() == first.tobytes() for out in outputs[1:])
    assert float(first["speed_y_self"][0]) == pytest.approx(-0.05, abs=1e-7)


# ---------------------------------------------------------------------------
# Falcon Dive (ftCa_SpecialHi; actions 353..356, victim CaptureCaptain 275)
# ---------------------------------------------------------------------------

ACT_FC_HI = 353
ACT_FC_AIR_HI = 354
ACT_FC_HI_CATCH = 355
ACT_FC_HI_THROW = 356
ACT_CAPTURE_CAPTAIN = 275

UP_B = dict(buttons=BTN_B, main_y=127)


@pytest.mark.parametrize(
    ("action_id", "animation_index", "grounded"),
    [
        (ACT_FC_SPECIAL_N, 301, True),
        (ACT_FC_S_START, 303, True),
        (ACT_FC_HI, 307, True),
        (ACT_FC_HI_CATCH, 309, False),
        (ACT_FC_LW_END, 314, True),
    ],
)
def test_falcon_special_anim_callbacks_freeze_until_hitlag_exit(
    action_id: int, animation_index: int, grounded: bool
) -> None:
    # Fighter_8006A360 skips procUpdate/procInput/procPhysics while x2219_b0 hitlag is set;
    # procMap is owned by the later collision phase. A terminal action frame must therefore
    # remain in its source MotionState until the hitlag countdown reaches zero.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    seed = _seed(grounded, pos_y=0.0 if grounded else 80.0)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(animation_index)
    seed["action_frame"][0, 0] = np.int16(100)
    seed["anim_frame_f32"][0, 0] = np.float32(100.0)
    seed["hitlag"][0, 0] = np.uint16(3)

    outs = _run(seed, [_mk_inputs(), _mk_inputs(), _mk_inputs()])

    assert [int(out["hitlag"][0]) for out in outs] == [2, 1, 0]
    assert [int(out["action_id"][0]) for out in outs[:2]] == [action_id, action_id]
    assert [int(out["action_frame"][0]) for out in outs[:2]] == [100, 100]
    assert int(outs[2]["action_id"][0]) != action_id


def test_falcon_dive_b_reverse_uses_processed_stick_deadzone() -> None:
    # doAirIASA consumes the Fighter_8006A360-processed stick. Raw X=20 is inside the common
    # deadzone even though its unprocessed magnitude exceeds Falcon's specialhi_input_var;
    # raw X=24 is outside both gates and reverses normally.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::doAirIASA
    seed = _seed(True)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_HI)
    seed["animation_index"][0, 0] = np.uint32(307)
    seed["action_frame"][0, 0] = np.int16(12)
    seed["anim_frame_f32"][0, 0] = np.float32(12.0)

    inside = _run(seed.copy(), [_mk_inputs(main_x=-20)])[0]
    outside = _run(seed.copy(), [_mk_inputs(main_x=-24)])[0]

    assert int(inside["facing"][0]) == 1
    assert int(outside["facing"][0]) == 0


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


def _seed_falcon_dive_hold(*, grounded_victim_mode: bool, constrained_hitlag: int = 0) -> np.ndarray:
    seed = _seed(False, pos_y=35.0)
    seed["action_id"][0, :2] = np.array([ACT_FC_HI_CATCH, ACT_CAPTURE_CAPTAIN], dtype=np.uint16)
    seed["animation_index"][0, :2] = np.array([309, 276], dtype=np.uint32)
    seed["action_frame"][0, :2] = np.int16(1)
    seed["anim_frame_f32"][0, :2] = np.float32(1.0)
    seed["grab_owner_port"][0, 1] = np.uint8(0)
    seed["pos_x"][0, :2] = np.array([12.0, -20.0], dtype=np.float32)
    if grounded_victim_mode:
        # x221B_b7 is a serialized connect-time source bit. Make the victim's current state
        # airborne to prove the hold does not switch to the airborne-victim constraint later.
        seed["state_flags"][0, 0, 2] |= np.uint8(0x01)
        seed["pos_y"][0, 1] = np.float32(25.0)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["hitlag"][0, 0] = np.uint16(constrained_hitlag)
    else:
        # Conversely, a grounded current victim does not change an airborne-victim connect into
        # accessory4 owner snapping. Its constrained CaptureCaptain map callback remains skipped.
        seed["state_flags"][0, 0, 2] &= np.uint8(0xFE)
        seed["pos_y"][0, 1] = np.float32(25.0)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["hitlag"][0, 1] = np.uint16(constrained_hitlag)
    return seed


def test_falcon_dive_hold_keeps_connect_time_constraint_mode_across_live_floor_state() -> None:
    grounded_seed = _seed_falcon_dive_hold(grounded_victim_mode=True)
    grounded_seed["on_ground"][0, 0] = np.uint8(1)
    grounded_seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    grounded_mode, grounded_internal = _run_with_internals(grounded_seed, [_mk_inputs()])[0]
    # Grounded-victim mode constrains Falcon and accessory4 snaps its root to the victim even
    # though the victim's live on_ground bit is now clear.
    assert float(grounded_mode["pos_x"][0]) == pytest.approx(
        float(grounded_mode["pos_x"][1]), abs=1e-6
    )
    assert float(grounded_mode["pos_y"][0]) == pytest.approx(
        float(grounded_mode["pos_y"][1]), abs=1e-6
    )
    assert int(grounded_mode["on_ground"][0]) == 1
    assert int(grounded_mode["ground_id"][0]) == 0xFFFF
    assert list(grounded_internal["attached_victim_port"][:2]) == [1, 0xFF]
    assert list(grounded_internal["grab_owner_port"][:2]) == [0xFF, 0]
    assert list(grounded_internal["grab_constraint_x2226_b2"][:2]) == [1, 0]

    airborne_seed = _seed_falcon_dive_hold(grounded_victim_mode=False)
    airborne_seed["ground_id"][0, 1] = np.uint16(0xFFFF)
    airborne_mode, airborne_internal = _run_with_internals(airborne_seed, [_mk_inputs()])[0]
    # Airborne-victim mode constrains CaptureCaptain and accessory1 hangs it from Falcon. The
    # victim's source map callback stays disabled, preserving its deliberately contradictory
    # current floor bit rather than using that bit to choose a new constraint mode.
    assert int(airborne_mode["on_ground"][1]) == 1
    assert int(airborne_mode["ground_id"][1]) == 0xFFFF
    assert float(airborne_mode["pos_x"][0]) == pytest.approx(12.0, abs=1e-5)
    assert float(airborne_mode["pos_x"][1]) != pytest.approx(-20.0, abs=1e-4)
    assert list(airborne_internal["attached_victim_port"][:2]) == [1, 0xFF]
    assert list(airborne_internal["grab_owner_port"][:2]) == [0xFF, 0]
    assert list(airborne_internal["grab_constraint_x2226_b2"][:2]) == [0, 1]


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_falcon_dive_constraint_suppresses_only_map_body_not_ecb_countdown(
    grounded_victim_mode: bool,
) -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    constrained = 0 if grounded_victim_mode else 1
    seed["on_ground"][0, constrained] = np.uint8(0)
    seed["ground_id"][0, constrained] = np.uint16(0xFFFF)
    seed["ecb_lock_timer"][0, constrained] = np.uint8(4)
    seed["ecb_lock_bottom_owner_u8"][0, constrained] = np.uint8(4)

    outs = _run_with_internals(seed, [_mk_inputs()] * 4)
    assert [int(internal["ecb_lock_timer"][constrained]) for _, internal in outs] == [3, 2, 1, 0]
    assert int(outs[-1][1]["ecb_lock_owner"][constrained]) == 0
    assert all(int(out["action_id"][constrained]) in {ACT_FC_HI_CATCH, ACT_CAPTURE_CAPTAIN}
               for out, _ in outs)
    assert all(int(internal["grab_constraint_x2226_b2"][constrained]) == 1
               for _, internal in outs)


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_falcon_dive_live_connect_and_reseed_reconstruct_same_link_packet(
    grounded_victim_mode: bool,
) -> None:
    live_seed = _seed_active_catch_vs_target(catch_kind=2, target_action=ACT_WAIT)
    live_seed["on_ground"][0, 1] = np.uint8(1 if grounded_victim_mode else 0)
    if not grounded_victim_mode:
        live_seed["pos_y"][0, 1] = np.float32(0.5)
    live_out, live = _run_with_internals(live_seed, [_mk_inputs()])[0]

    # Serialize the actual free-running connect result back through the public seed contract. The
    # victim-side x1A5C owner is the only serialized link; reseed must reconstruct its reciprocal
    # owner pointer, masks, connect-time mode, and selected constraint from this live packet.
    roundtrip = live_seed.copy()
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "pos_x",
        "pos_y",
        "state_flags",
    ):
        roundtrip[field][0] = live_out[field]
    roundtrip["anim_frame_f32"][0] = live_out["action_frame"].astype(np.float32)
    roundtrip["grab_owner_port"][0] = live["grab_owner_port"]
    reseeded = _internals_immediately_after_reseed(roundtrip)

    fields = (
        "attached_victim_port",
        "grab_owner_port",
        "catch_target_mask_x1a6a",
        "grab_constraint_x2226_b2",
        "falcon_specialhi_x221b_b7",
    )
    for field in fields:
        assert list(live[field]) == list(reseeded[field]), field


def test_falcon_dive_reseed_sanitizes_duplicate_and_incompatible_capture_links() -> None:
    duplicate = _seed_falcon_dive_hold(grounded_victim_mode=False)
    duplicate["num_players"][0] = np.uint8(4)
    duplicate["stocks"][0, :4] = np.uint8(4)
    duplicate["char_id"][0, 2:4] = np.uint8(FOX)
    duplicate["action_id"][0, 2] = np.uint16(ACT_CAPTURE_CAPTAIN)
    duplicate["animation_index"][0, 2] = np.uint32(276)
    duplicate["grab_owner_port"][0, 2] = np.uint8(0)

    rebuilt = _internals_immediately_after_reseed(duplicate)
    assert list(rebuilt["attached_victim_port"]) == [1, 0xFF, 0xFF, 0xFF]
    assert list(rebuilt["grab_owner_port"]) == [0xFF, 0, 0xFF, 0xFF]
    assert list(rebuilt["grab_constraint_x2226_b2"]) == [0, 1, 0, 0]

    incompatible = duplicate.copy()
    incompatible["char_id"][0, 0] = np.uint8(FOX)
    incompatible["action_id"][0, 0] = np.uint16(ACT_WAIT)
    incompatible["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    rejected = _internals_immediately_after_reseed(incompatible)
    assert list(rejected["attached_victim_port"]) == [0xFF] * 4
    assert list(rejected["grab_owner_port"]) == [0xFF] * 4
    assert list(rejected["grab_constraint_x2226_b2"]) == [0] * 4


def test_airborne_falcon_dive_release_runs_d5d4_and_release_local_floor_probe() -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=False)
    # SpecialHiCatch ends after frame 15. Put Falcon's sample-owner root below FD so the
    # constrained CaptureCaptain x1A70 target crosses the hard floor during ftCo_800DDDE4's
    # immediate mpColl_800471F8 substeps.
    seed["action_frame"][0, :2] = np.int16(15)
    seed["anim_frame_f32"][0, :2] = np.float32(15.0)
    seed["pos_y"][0, :2] = np.array([-5.0, -4.7], dtype=np.float32)
    seed["on_ground"][0, :2] = np.uint8(0)

    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][0]) == ACT_FC_HI_THROW
    assert 0x004B <= int(out["action_id"][1]) <= 0x005B
    assert int(out["jumps_left"][1]) == 1  # ftCommon_8007D5D4: jumpsUsed = 1
    assert int(out["ground_id"][1]) == 1
    assert float(out["pos_y"][1]) >= 0.0


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_falcon_dive_release_explicitly_unlocks_constrained_ecb(
    grounded_victim_mode: bool,
) -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    constrained = 0 if grounded_victim_mode else 1
    seed["action_frame"][0, :2] = np.int16(15)
    seed["anim_frame_f32"][0, :2] = np.float32(15.0)
    seed["ecb_lock_timer"][0, constrained] = np.uint8(4)
    seed["ecb_lock_bottom_owner_u8"][0, constrained] = np.uint8(4)

    out, internal = _run_with_internals(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_FC_HI_THROW
    assert int(internal["ecb_lock_timer"][constrained]) == 0
    assert int(internal["ecb_lock_owner"][constrained]) == 0
    assert int(internal["grab_constraint_x2226_b2"][constrained]) == 0


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize("floor_hit", [False, True])
def test_falcon_dive_normal_release_replaces_stale_collision_roots_on_hit_and_miss(
    grounded_victim_mode: bool, floor_hit: bool
) -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    constrained = 0 if grounded_victim_mode else 1
    seed["action_frame"][0, :2] = np.int16(15)
    seed["anim_frame_f32"][0, :2] = np.float32(15.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, constrained] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, constrained] = np.float32(123.0)
    seed["floor_sweep_prev_pos_y_f32"][0, constrained] = np.float32(456.0)
    if floor_hit:
        seed["pos_y"][0, :2] = np.float32([-5.0, -4.7])
        seed["on_ground"][0, :2] = np.uint8(0)

    runs = _run_with_internals(seed, [_mk_inputs(), _mk_inputs()])
    for out, internal in runs:
        assert float(internal["floor_sweep_prev_pos_x"][constrained]) != pytest.approx(123.0)
        assert float(internal["floor_sweep_prev_pos_y"][constrained]) != pytest.approx(456.0)
        assert np.isfinite(float(internal["coll_last_pos_x"][constrained]))
        assert np.isfinite(float(internal["coll_last_pos_y"][constrained]))
        if floor_hit:
            assert int(out["ground_id"][constrained]) == 1


def _seed_four_player_falcon_dive_and_third_party_catch(
    *, third_party_dive: bool,
) -> np.ndarray:
    seed = _seed(True)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.array(
        [FALCON, FOX, FALCON if third_party_dive else FOX, FOX], dtype=np.uint8
    )
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["shield_hp"][0, :4] = np.float32(60.0)
    seed["jumps_left"][0, :4] = np.uint8(2)
    seed["x676_x"][0, :4] = np.uint8(0xFE)
    seed["on_ground"][0, :4] = np.uint8(1)
    seed["facing"][0, :4] = np.uint8(1)
    seed["pos_x"][0, :4] = np.array([0.0, 1.0, -4.0, 100.0], dtype=np.float32)
    seed["pos_y"][0, :4] = np.float32(0.0)
    seed["action_id"][0, :4] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :4] = np.uint32(SM_WAIT1)

    # p0's frame-13 Dive catch resolves first and selects the nearest victim p1. p2's later
    # attacker pass overlaps the now-carrying p0 and must observe p0.x1A6A=0x1FF immediately.
    seed["action_id"][0, 0] = np.uint16(ACT_FC_HI)
    seed["animation_index"][0, 0] = np.uint32(307)
    seed["action_frame"][0, 0] = np.int16(12)
    seed["anim_frame_f32"][0, 0] = np.float32(12.0)
    if third_party_dive:
        seed["action_id"][0, 2] = np.uint16(ACT_FC_HI)
        seed["animation_index"][0, 2] = np.uint32(307)
        seed["action_frame"][0, 2] = np.int16(12)
        seed["anim_frame_f32"][0, 2] = np.float32(12.0)
    else:
        seed["action_id"][0, 2] = np.uint16(0x00D4)  # Catch
        seed["animation_index"][0, 2] = np.uint32(242)
        seed["action_frame"][0, 2] = np.int16(5)
        seed["anim_frame_f32"][0, 2] = np.float32(5.0)
    return seed


def _seed_active_catch_vs_target(
    *, catch_kind: int, target_action: int, mirrored: bool = False
) -> np.ndarray:
    seed = _seed(True)
    direction = -1.0 if mirrored else 1.0
    seed["pos_x"][0, :2] = np.array([0.0, direction], dtype=np.float32)
    seed["facing"][0, 0] = np.uint8(0 if mirrored else 1)
    seed["facing"][0, 1] = np.uint8(1 if mirrored else 0)
    if catch_kind == 1:
        seed["char_id"][0, 0] = np.uint8(FOX)
        seed["action_id"][0, 0] = np.uint16(0x00D4)
        seed["animation_index"][0, 0] = np.uint32(242)
        seed["action_frame"][0, 0] = np.int16(5)
        seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    else:
        seed["char_id"][0, 0] = np.uint8(FALCON)
        seed["action_id"][0, 0] = np.uint16(ACT_FC_HI)
        seed["animation_index"][0, 0] = np.uint32(307)
        seed["action_frame"][0, 0] = np.int16(12)
        seed["anim_frame_f32"][0, 0] = np.float32(12.0)
    seed["action_id"][0, 1] = np.uint16(target_action)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1 if target_action == ACT_WAIT else target_action)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    return seed


@pytest.mark.parametrize("catch_kind", [1, 2])
@pytest.mark.parametrize("target_action", [0x00BE, 0x00C6])
@pytest.mark.parametrize("mirrored", [False, True])
def test_downspot_entry_mask_rejects_ordinary_catch_and_falcon_dive(
    catch_kind: int, target_action: int, mirrored: bool
) -> None:
    seed = _seed_active_catch_vs_target(
        catch_kind=catch_kind, target_action=target_action, mirrored=mirrored
    )
    out, internal = _run_with_internals(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][1]) == target_action
    assert int(internal["catch_target_mask_x1a6a"][1]) == 0x1FF
    assert int(internal["attached_victim_port"][0]) == 0xFF
    assert int(internal["grab_owner_port"][1]) == 0xFF


@pytest.mark.parametrize("catch_kind", [1, 2])
def test_adjacent_downstand_remains_catchable_positive(catch_kind: int) -> None:
    seed = _seed_active_catch_vs_target(catch_kind=catch_kind, target_action=0x00BA)
    seed["action_frame"][0, 1] = np.int16(25)
    seed["anim_frame_f32"][0, 1] = np.float32(25.0)
    out, internal = _run_with_internals(seed, [_mk_inputs()])[0]
    expected = ACT_CAPTURE_CAPTAIN if catch_kind == 2 else 0x00E2
    assert int(out["action_id"][1]) == expected
    assert int(internal["attached_victim_port"][0]) == 1
    assert int(internal["grab_owner_port"][1]) == 0


@pytest.mark.parametrize("catch_kind", [1, 2])
def test_catch_selection_rejects_dmg_x2224_b2_without_mutating_packet(catch_kind: int) -> None:
    import msl_binding

    eligible = _seed_active_catch_vs_target(catch_kind=catch_kind, target_action=ACT_WAIT)
    blocked = eligible.copy()
    blocked["dmg_x2224_b2"][0, 1] = np.uint8(1)

    eligible_out, eligible_internal = _run_with_internals(eligible, [_mk_inputs()])[0]
    blocked_out, blocked_internal = _run_with_internals(blocked, [_mk_inputs()])[0]
    assert int(eligible_internal["attached_victim_port"][0]) == 1
    assert int(eligible_internal["grab_owner_port"][1]) == 0
    assert int(blocked_out["action_id"][1]) == ACT_WAIT
    assert int(blocked_internal["attached_victim_port"][0]) == 0xFF
    assert int(blocked_internal["grab_owner_port"][1]) == 0xFF
    assert int(blocked_internal["catch_target_mask_x1a6a"][1]) == 0
    assert int(blocked_internal["grab_constraint_x2226_b2"][1]) == 0
    assert float(blocked_out["percent"][1]) == pytest.approx(0.0)
    assert int(blocked_out["action_id"][0]) == (0x00D4 if catch_kind == 1 else ACT_FC_HI)
    assert int(eligible_out["action_id"][1]) != ACT_WAIT

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(
            handle, blocked.view(np.uint8).reshape((1, int(sizes["seed"])))
        )
        neutral = _mk_inputs()
        msl_binding.step_input(handle, neutral, neutral)
        assert int(msl_binding.debug_hitlist_fighter_contains(handle, 0, 0, 0, 1)) == 0
    finally:
        msl_binding.destroy(handle)


def test_two_whiffing_catch_capsules_never_clank() -> None:
    seed = _seed(True)
    seed["pos_x"][0, :2] = np.array([0.0, 1.0], dtype=np.float32)
    seed["facing"][0, :2] = np.array([1, 0], dtype=np.uint8)
    seed["action_id"][0, :2] = np.uint16(0x00D4)
    seed["animation_index"][0, :2] = np.uint32(242)
    seed["action_frame"][0, :2] = np.int16(5)
    seed["anim_frame_f32"][0, :2] = np.float32(5.0)
    seed["dmg_x2224_b2"][0, :2] = np.uint8(1)

    out = _run(seed, [_mk_inputs()])[0]
    assert list(int(v) for v in out["action_id"][:2]) == [0x00D4, 0x00D4]


@pytest.mark.parametrize("catch_kind", [1, 2])
def test_catch_connect_clears_owner_and_victim_outgoing_primitives(catch_kind: int) -> None:
    import msl_binding

    seed = _seed_active_catch_vs_target(catch_kind=catch_kind, target_action=ACT_WAIT)
    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        neutral = _mk_inputs()
        msl_binding.debug_step_input_pre_combat(handle, neutral, neutral)
        # Give the otherwise-catchable victim a live, remote attack capsule with prior-sweep state.
        # Catch connection must clear the full HitCapsule, not only its world-enabled bit.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 1)
        msl_binding.debug_set_hitbox_world(handle, 0, 1, 0, 50.0, 50.0, 0.0, 2.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 1, 0, HIT_GROUNDED | HIT_AERIAL)
        _, active_count = msl_binding.hitboxes_world_full(handle, 0, 1)
        assert int(active_count) == 1

        msl_binding.debug_combat_resolve(handle)
        _, owner_count = msl_binding.hitboxes_world(handle, 0, 0)
        _, victim_count = msl_binding.hitboxes_world(handle, 0, 1)
        msl_binding.debug_refresh_combat_geometry(handle)
        _, victim_refresh_count = msl_binding.hitboxes_world_full(handle, 0, 1)
    finally:
        msl_binding.destroy(handle)
    assert int(owner_count) == 0
    assert int(victim_count) == 0
    assert int(victim_refresh_count) == 0


@pytest.mark.parametrize("third_party_dive", [False, True])
def test_falcon_dive_carrier_mask_blocks_later_four_player_catch_kinds(
    third_party_dive: bool,
) -> None:
    out, internal = _run_with_internals(
        _seed_four_player_falcon_dive_and_third_party_catch(
            third_party_dive=third_party_dive
        ),
        [_mk_inputs()],
    )[0]

    assert int(out["action_id"][1]) == ACT_CAPTURE_CAPTAIN
    assert list(internal["attached_victim_port"][:4]) == [1, 0xFF, 0xFF, 0xFF]
    assert list(internal["grab_owner_port"][:4]) == [0xFF, 0, 0xFF, 0xFF]
    assert list(internal["catch_target_mask_x1a6a"][:2]) == [0x1FF, 0x1FF]
    assert list(internal["grab_constraint_x2226_b2"][:2]) == [1, 0]
    if third_party_dive:
        # HitElement_Catch is excluded from ftColl_8007699C clank candidates. The first Falcon
        # keeps the protected carrier link and the later Dive neither replaces it nor rebounds.
        # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007699C}
        assert int(out["action_id"][0]) == ACT_FC_HI_CATCH
        assert int(out["action_id"][2]) == ACT_FC_HI
    else:
        assert int(out["action_id"][0]) == ACT_FC_HI_CATCH
        assert int(out["action_id"][2]) == 0x00D4


def test_falcon_dive_carrier_mask_clears_after_release_for_later_grab() -> None:
    seed = _seed_four_player_falcon_dive_and_third_party_catch(third_party_dive=False)
    seed["action_id"][0, 0] = np.uint16(ACT_FC_HI_THROW)
    seed["animation_index"][0, 0] = np.uint32(310)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(100.0)
    seed["pos_y"][0, 0] = np.float32(-10.0)
    seed["on_ground"][0, 0] = np.uint8(0)

    out = _run(seed, [_mk_inputs()])[0]

    assert int(out["action_id"][2]) == 0x00D5  # CatchPull
    assert int(out["action_id"][0]) in {0x00DF, 0x00E2}  # CapturePulledHi/Lw


@pytest.mark.parametrize(
    ("p1_x", "p3_x", "expected_victim"),
    [(1.5, 1.0, 3), (1.0, -1.0, 1)],
)
def test_falcon_dive_four_player_selects_nearest_then_port_order(
    p1_x: float, p3_x: float, expected_victim: int
) -> None:
    # ftColl_80078A2C retains the smallest ftGrabDist X distance and resolves exact ties in stable
    # fighter/GObj order. This locks both selection layers without relying on two-player ordering.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    seed = _seed_four_player_falcon_dive_and_third_party_catch(third_party_dive=False)
    seed["action_id"][0, 2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 2] = np.uint32(SM_WAIT1)
    seed["action_frame"][0, 2] = np.int16(0)
    seed["anim_frame_f32"][0, 2] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(p1_x)
    seed["pos_x"][0, 2] = np.float32(50.0)
    seed["pos_x"][0, 3] = np.float32(p3_x)

    out, internal = _run_with_internals(seed, [_mk_inputs()])[0]

    expected_actions = [ACT_FC_HI_CATCH, ACT_WAIT, ACT_WAIT, ACT_WAIT]
    expected_actions[expected_victim] = ACT_CAPTURE_CAPTAIN
    assert list(int(v) for v in out["action_id"][:4]) == expected_actions
    expected_attached = [expected_victim, 0xFF, 0xFF, 0xFF]
    expected_owner = [0xFF, 0xFF, 0xFF, 0xFF]
    expected_owner[expected_victim] = 0
    expected_masks = [0, 0, 0, 0]
    expected_masks[0] = 0x1FF
    expected_masks[expected_victim] = 0x1FF
    assert list(internal["attached_victim_port"][:4]) == expected_attached
    assert list(internal["grab_owner_port"][:4]) == expected_owner
    assert list(internal["catch_kind_x1a68"][:4]) == [0, 0, 0, 0]
    assert list(internal["catch_target_mask_x1a6a"][:4]) == expected_masks
    assert list(internal["grab_constraint_x2226_b2"][:4]) == [1, 0, 0, 0]
    assert list(internal["falcon_specialhi_x221b_b7"][:4]) == [1, 0, 0, 0]


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_falcon_dive_accessory_position_owner_freezes_under_its_own_hitlag(
    grounded_victim_mode: bool,
) -> None:
    active = _run(
        _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode), [_mk_inputs()]
    )[0]
    frozen_seed = _seed_falcon_dive_hold(
        grounded_victim_mode=grounded_victim_mode, constrained_hitlag=2
    )
    frozen = _run(frozen_seed, [_mk_inputs()])[0]

    constrained_p = 0 if grounded_victim_mode else 1
    initial_x = float(frozen_seed["pos_x"][0, constrained_p])
    assert float(frozen["pos_x"][constrained_p]) == pytest.approx(initial_x, abs=1e-6)
    assert float(active["pos_x"][constrained_p]) != pytest.approx(initial_x, abs=1e-4)


def test_airborne_falcon_dive_accessory_freezes_recursively_with_owner_hitlag() -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=False)
    seed["hitlag"][0, 0] = np.uint16(2)
    seed["hitlag"][0, 1] = np.uint16(0)
    initial_x = float(seed["pos_x"][0, 1])
    initial_frame = int(seed["action_frame"][0, 1])

    outs = _run(seed, [_mk_inputs()] * 3)
    assert float(outs[0]["pos_x"][1]) == pytest.approx(initial_x, abs=1e-6)
    assert int(outs[0]["action_frame"][1]) == initial_frame
    assert float(outs[1]["pos_x"][1]) != pytest.approx(initial_x, abs=1e-4)
    assert int(outs[1]["action_frame"][1]) > initial_frame


def test_grounded_falcon_dive_accessory_freezes_recursively_with_victim_hitlag() -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=True)
    seed["hitlag"][0, 0] = np.uint16(0)
    seed["hitlag"][0, 1] = np.uint16(2)
    initial_x = float(seed["pos_x"][0, 0])
    initial_frame = int(seed["action_frame"][0, 0])

    outs = _run(seed, [_mk_inputs()] * 3)
    assert float(outs[0]["pos_x"][0]) == pytest.approx(initial_x, abs=1e-6)
    assert int(outs[0]["action_frame"][0]) == initial_frame
    assert float(outs[1]["pos_x"][0]) != pytest.approx(initial_x, abs=1e-4)
    assert int(outs[1]["action_frame"][0]) > initial_frame


@pytest.mark.parametrize("hitlag_source", [0, 1], ids=["holder", "victim"])
def test_falcon_dive_recursive_hitlag_rejects_one_sided_links(hitlag_source: int) -> None:
    import msl_binding

    seed = _seed_falcon_dive_hold(grounded_victim_mode=bool(hitlag_source))
    seed["hitlag"][0, hitlag_source] = np.uint16(2)
    other = 1 - hitlag_source
    initial_other_frame = int(seed["action_frame"][0, other])
    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        # Leave Falcon's reconstructed owner-side pointer live but sever the victim-side x1A5C.
        # Recursive freeze must validate both directions before following either actor.
        msl_binding.debug_set_grab_owner_port(handle, 0, 1, 0xFF)
        neutral = _mk_inputs()
        msl_binding.step_input(handle, neutral, neutral)
        out_u8 = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, out_u8)
        out = out_u8.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)
    assert int(out["action_frame"][other]) > initial_other_frame


def _seed_dive_hold_with_third_party_damage(
    *, grounded_victim_mode: bool, item_damage: bool
) -> np.ndarray:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, 2:4] = np.array([7 if item_damage else FOX, FOX], dtype=np.uint8)
    seed["frame_speed_mul_f32"][0, 2:4] = np.float32(1.0)
    seed["shield_hp"][0, 2:4] = np.float32(60.0)
    seed["jumps_left"][0, 2:4] = np.uint8(2)
    seed["x676_x"][0, 2:4] = np.uint8(0xFE)
    seed["action_id"][0, 3] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 3] = np.uint32(SM_WAIT1)
    seed["pos_x"][0, 3] = np.float32(100.0)
    seed["on_ground"][0, 3] = np.uint8(1)
    constrained = 0 if grounded_victim_mode else 1
    seed["ecb_lock_timer"][0, constrained] = np.uint8(4)
    seed["ecb_lock_bottom_owner_u8"][0, constrained] = np.uint8(4)

    target_x = -20.0 if grounded_victim_mode else 12.0
    target_y = 25.0 if grounded_victim_mode else 35.0
    if item_damage:
        seed["action_id"][0, 2] = np.uint16(ACT_WAIT)
        seed["animation_index"][0, 2] = np.uint32(SM_WAIT1)
        seed["pos_x"][0, 2] = np.float32(100.0)
        seed["on_ground"][0, 2] = np.uint8(1)
        smoke = seed["items"][0, 0]
        smoke["exists"] = np.uint8(1)
        smoke["type"] = np.uint16(85)  # Sheik Vanish article, extracted MSLITAR1 kind.
        smoke["state"] = np.uint8(0)
        smoke["owner"] = np.int8(2)
        smoke["instance_id"] = np.uint16(701)
        smoke["spawn_id"] = np.uint32(71)
        smoke["direction"] = np.float32(1.0)
        smoke["timer"] = np.float32(80.0)
        smoke["pos_x"] = np.float32(target_x + 2.0)
        smoke["pos_y"] = np.float32(target_y + 4.0)
    else:
        seed["action_id"][0, 2] = np.uint16(0x003F)  # AttackHi4
        seed["animation_index"][0, 2] = np.uint32(66)
        seed["action_frame"][0, 2] = np.int16(6)
        seed["anim_frame_f32"][0, 2] = np.float32(6.0)
        seed["pos_x"][0, 2] = np.float32(target_x)
        seed["pos_y"][0, 2] = np.float32(target_y)
        seed["on_ground"][0, 2] = np.uint8(0)
    return seed


def _is_damage_action(action: int) -> bool:
    return 0x004B <= action <= 0x005B


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize("damage", [1.0, 5.0, 6.0, 8.0])
@pytest.mark.parametrize(
    "damaged_players", [(0,), (1,), (0, 1)], ids=["holder", "victim", "simultaneous"]
)
def test_dive_processhit_source_branch_matrix(
    grounded_victim_mode: bool, damage: float, damaged_players: tuple[int, ...]
) -> None:
    out, internal, seed = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode,
        damaged_players=damaged_players,
        damage=damage,
    )
    deltas = [float(out["percent"][p]) - float(seed["percent"][0, p]) for p in range(2)]
    victim_low = damage < 6.0

    if damaged_players == (0,):
        # Holder-only always runs DCFD4 before DC920; the holder threshold is irrelevant.
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert deltas == pytest.approx([damage, 6.0])
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) == 0  # DCFD4 does not assign hitlag.
    elif damaged_players == (1,) and victim_low:
        assert list(map(int, out["action_id"][:2])) == [ACT_FC_HI_CATCH, ACT_CAPTURE_CAPTAIN]
        assert deltas == pytest.approx([0.0, damage])
        assert list(map(int, out["hitstun"][:2])) == [0, 0]
        assert int(out["hitlag"][0]) == int(out["hitlag"][1]) > 0
        assert list(map(int, internal["attached_victim_port"][:2])) == [1, 0xFF]
        assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0]
        return
    elif damaged_players == (1,):
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert deltas == pytest.approx([0.0, damage])
        assert int(out["hitlag"][0]) == 0  # DE2F0 does not assign hitlag.
        assert int(out["hitstun"][0]) > 0
        assert int(out["last_hit_by"][0]) == 6
    elif victim_low:
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert deltas == pytest.approx([damage, damage + 6.0])
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
    else:
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert deltas == pytest.approx([damage, damage])

    assert list(map(int, internal["attached_victim_port"][:2])) == [0xFF, 0xFF]
    assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0xFF]
    assert list(map(int, internal["grab_constraint_x2226_b2"][:2])) == [0, 0]
    assert list(map(int, internal["catch_target_mask_x1a6a"][:2])) == [0, 0]


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize(("holder_damage", "victim_damage"), [(8.0, 1.0), (1.0, 8.0)])
def test_dive_simultaneous_mixed_hits_are_producer_order_independent(
    grounded_victim_mode: bool, holder_damage: float, victim_damage: float
) -> None:
    runs = [
        _debug_dive_two_fighter_producers(
            grounded_victim_mode=grounded_victim_mode,
            holder_damage=holder_damage,
            victim_damage=victim_damage,
            reverse_order=reverse,
        )
        for reverse in (False, True)
    ]
    expected_victim_delta = victim_damage + (6.0 if victim_damage < 6.0 else 0.0)
    for out, internal, seed in runs:
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert [
            float(out["percent"][0]) - float(seed["percent"][0, 0]),
            float(out["percent"][1]) - float(seed["percent"][0, 1]),
        ] == pytest.approx([holder_damage, expected_victim_delta])
        assert list(map(int, internal["attached_victim_port"][:2])) == [0xFF, 0xFF]
        assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0xFF]

    a, b = runs[0][0], runs[1][0]
    for field in (
        "action_id",
        "percent",
        "hitlag",
        "hitstun",
        "speed_x_attack",
        "speed_y_attack",
        "pos_x",
        "pos_y",
        "on_ground",
        "ground_id",
    ):
        assert list(a[field][:2]) == pytest.approx(list(b[field][:2])), field


@pytest.mark.parametrize(
    ("grounded_victim_mode", "smoke_x", "smoke_y"),
    [(False, 0.0, 35.0), (True, -30.0, 20.0)],
)
def test_dive_holder_only_item_hit_runs_dcfd4_independent_of_owner_port(
    grounded_victim_mode: bool, smoke_x: float, smoke_y: float
) -> None:
    control = _run(
        _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode), [_mk_inputs()]
    )[0]
    runs = []
    for owner in (2, 3):
        seed = _seed_dive_hold_with_third_party_damage(
            grounded_victim_mode=grounded_victim_mode, item_damage=True
        )
        seed["char_id"][0, 2:4] = np.uint8(FOX)
        seed["char_id"][0, owner] = np.uint8(7)  # Sheik owns the extracted Vanish article.
        seed["instance_id"][0, owner] = np.uint16(701)
        seed["items"][0, 0]["owner"] = np.int8(owner)
        seed["items"][0, 0]["pos_x"] = np.float32(smoke_x)
        seed["items"][0, 0]["pos_y"] = np.float32(smoke_y)
        runs.append(_run_with_internals(seed, [_mk_inputs()])[0])

    # The item producer hits only Falcon for 12. DCFD4 then contributes exactly the stored six to
    # the victim over the control hold step, with no victim hitlag. Swapping the unrelated source
    # fighter between ports 2 and 3 must not alter the linked-pair ProcessHit packet.
    for out, internal in runs:
        assert all(_is_damage_action(int(a)) for a in out["action_id"][:2])
        assert [
            float(out["percent"][0]) - float(control["percent"][0]),
            float(out["percent"][1]) - float(control["percent"][1]),
        ] == pytest.approx([12.0, 6.0])
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) == 0
        assert list(map(int, internal["attached_victim_port"][:2])) == [0xFF, 0xFF]
        assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0xFF]

    for field in (
        "action_id",
        "percent",
        "hitlag",
        "hitstun",
        "speed_x_attack",
        "speed_y_attack",
        "pos_x",
        "pos_y",
        "on_ground",
        "ground_id",
    ):
        assert list(runs[0][0][field][:2]) == pytest.approx(list(runs[1][0][field][:2])), field


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_dive_holder_break_ignores_x198c_and_bitlocks_predamage_stored_kb(
    grounded_victim_mode: bool,
) -> None:
    import struct

    out, _, seed = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode,
        damaged_players=(0,),
        damage=8.0,
        victim_x198c=1,
    )
    assert float(out["percent"][1]) - float(seed["percent"][0, 1]) == pytest.approx(6.0)
    assert int(out["hitlag"][1]) == 0
    assert struct.unpack("<I", struct.pack("<f", float(out["speed_x_attack"][1])))[0] == 0xC017AE14
    assert struct.unpack("<I", struct.pack("<f", float(out["speed_y_attack"][1])))[0] == 0


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize("damaged_players", [(0,), (0, 1)], ids=["dcfd4", "de854"])
def test_dive_stored_hit_uses_creation_stale_damage_and_live_attack_identity(
    grounded_victim_mode: bool, damaged_players: tuple[int, ...]
) -> None:
    import struct

    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    seed["attack_id"][0, 0] = np.uint16(20)
    seed["attack_instance"][0, 0] = np.uint16(777)
    seed["stale_queue_index"][0, 0] = np.uint8(1)
    seed["stale_move_id"][0, 0, 0] = np.uint16(20)
    seed["stale_attack_instance"][0, 0, 0] = np.uint16(111)
    seed["combo_victim_port"][0, 0] = np.uint8(1)
    seed["last_attack_landed"][0, 0] = np.uint8(20)
    seed["combo_count"][0, 0] = np.uint8(3)

    out, _, original = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode,
        damaged_players=damaged_players,
        damage=1.0,
        seed_override=seed,
    )
    direct = 1.0 if damaged_players == (0, 1) else 0.0
    # Latest stale weight is 0.09: xDF4[1].damage is 6 * 0.91, while KB keeps raw unk_count 6.
    assert float(out["percent"][1]) - float(original["percent"][0, 1]) == pytest.approx(
        direct + 5.46
    )
    expected_x = 0xC0184B5E if damaged_players == (0, 1) else 0xC017AE14
    assert struct.unpack("<I", struct.pack("<f", float(out["speed_x_attack"][1])))[0] == expected_x
    assert int(out["last_attack_landed"][0]) == 20
    assert int(out["combo_count"][0]) == 4


def test_dive_stored_hit_creation_scalar_excludes_current_attack_instance() -> None:
    seed = _seed_falcon_dive_hold(grounded_victim_mode=False)
    seed["attack_id"][0, 0] = np.uint16(20)
    seed["attack_instance"][0, 0] = np.uint16(777)
    seed["stale_queue_index"][0, 0] = np.uint8(2)
    seed["stale_move_id"][0, 0, :2] = np.uint16([20, 20])
    seed["stale_attack_instance"][0, 0, :2] = np.uint16([111, 777])

    out, _, original = _debug_dive_fighter_damage(
        grounded_victim_mode=False,
        damaged_players=(0,),
        damage=1.0,
        seed_override=seed,
    )
    # The latest current-instance entry is excluded; only the previous instance's 0.08 weight
    # contributes to the creation-time scalar.
    assert float(out["percent"][1]) - float(original["percent"][0, 1]) == pytest.approx(5.52)


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_dive_victim_x221c_b0_retains_pair_above_damage_threshold(
    grounded_victim_mode: bool,
) -> None:
    out, internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode,
        damaged_players=(1,),
        damage=8.0,
        victim_x221c_b0=True,
    )
    assert list(map(int, out["action_id"][:2])) == [ACT_FC_HI_CATCH, ACT_CAPTURE_CAPTAIN]
    assert list(map(int, internal["attached_victim_port"][:2])) == [1, 0xFF]
    assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0]


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
def test_dive_victim_high_applies_extracted_de2f0_without_hitlag(
    grounded_victim_mode: bool,
) -> None:
    import struct

    out, _, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode, damaged_players=(1,), damage=8.0
    )
    assert int(out["hitlag"][0]) == 0
    assert int(out["hitstun"][0]) == 8
    assert int(out["last_hit_by"][0]) == 6
    assert struct.unpack("<I", struct.pack("<f", float(out["speed_x_attack"][0])))[0] == 0xBED93923
    assert struct.unpack("<I", struct.pack("<f", float(out["speed_y_attack"][0])))[0] == 0x3ED93923


def test_dc920_airborne_477e0_floor_contact_stays_airborne_and_restores_target() -> None:
    out, internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=False,
        damaged_players=(0,),
        damage=8.0,
        force_fallback_y=-5.0,
    )
    constrained = 1
    assert int(out["on_ground"][constrained]) == 0
    assert float(out["pos_x"][constrained]) == pytest.approx(0.0)
    assert float(out["pos_y"][constrained]) == pytest.approx(-5.0)
    assert int(internal["ecb_lock_timer"][constrained]) == 0
    assert float(internal["prev_pos_y"][constrained]) == pytest.approx(-5.0)
    assert float(internal["floor_sweep_prev_pos_y"][constrained]) == pytest.approx(-5.0)
    assert float(internal["coll_last_pos_y"][constrained]) != pytest.approx(-5.0)
    assert list(map(int, internal["attached_victim_port"][:2])) == [0xFF, 0xFF]
    assert list(map(int, internal["grab_owner_port"][:2])) == [0xFF, 0xFF]


@pytest.mark.parametrize(
    ("root_y", "expect_floor"), [(-5.0, True), (35.0, False)], ids=["true", "false"]
)
def test_dc920_grounded_48654_fallback_keeps_distinct_true_false_packets(
    root_y: float, expect_floor: bool
) -> None:
    out, internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=True,
        damaged_players=(0,),
        damage=8.0,
        force_fallback_y=root_y,
        force_constrained_grounded=True,
    )
    constrained = 0
    if expect_floor:
        assert int(out["ground_id"][constrained]) == 1
        assert float(out["pos_y"][constrained]) == pytest.approx(0.0001, abs=2e-6)
        assert float(internal["prev_pos_y"][constrained]) < 0.0
    else:
        assert int(out["ground_id"][constrained]) != 1
        assert float(out["pos_y"][constrained]) == pytest.approx(38.658023834228516)
        assert float(internal["prev_pos_y"][constrained]) == pytest.approx(
            float(out["pos_y"][constrained])
        )
    assert float(internal["coll_last_pos_y"][constrained]) != pytest.approx(
        float(internal["prev_pos_y"][constrained])
    )


def test_dc920_connected_success_rebases_all_represented_roots_and_survives_next_collision() -> None:
    immediate, immediate_internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=False, damaged_players=(0,), damage=8.0, near_floor=True
    )
    constrained = 1
    root = (float(immediate["pos_x"][constrained]), float(immediate["pos_y"][constrained]))
    assert int(immediate["ground_id"][constrained]) == 1
    for field, expected in (
        ("prev_pos_x", root[0]),
        ("prev_pos_y", root[1]),
        ("floor_sweep_prev_pos_x", root[0]),
        ("floor_sweep_prev_pos_y", root[1]),
        ("coll_last_pos_x", root[0]),
        ("coll_last_pos_y", root[1]),
    ):
        assert float(immediate_internal[field][constrained]) == pytest.approx(expected)

    following, following_internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=False,
        damaged_players=(0,),
        damage=8.0,
        near_floor=True,
        follow_collision=True,
    )
    assert int(following["ground_id"][constrained]) == 1
    assert float(following_internal["prev_pos_x"][constrained]) == pytest.approx(root[0])
    assert float(following_internal["prev_pos_y"][constrained]) == pytest.approx(root[1])
    assert float(following_internal["coll_last_pos_x"][constrained]) == pytest.approx(root[0])
    assert float(following_internal["coll_last_pos_y"][constrained]) == pytest.approx(root[1])


def test_dc920_tolerance_failure_preserves_candidate_floor_before_fallback() -> None:
    out, internal, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=True,
        damaged_players=(0,),
        damage=8.0,
        near_floor=True,
    )
    constrained = 0
    assert int(out["ground_id"][constrained]) == 1
    assert float(out["pos_y"][constrained]) > 3.0
    assert float(internal["coll_last_pos_y"][constrained]) != pytest.approx(
        float(internal["prev_pos_y"][constrained])
    )


@pytest.mark.parametrize(
    ("damaged_players", "expected_root"),
    [((0,), -20.0), ((1,), -17.376113891601562), ((0, 1), -17.376113891601562)],
)
def test_dc920_branch_order_samples_pre_or_post_damage_pose_away_from_floors(
    damaged_players: tuple[int, ...], expected_root: float
) -> None:
    out, _, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=False, damaged_players=damaged_players, damage=8.0
    )
    assert float(out["pos_x"][1]) == pytest.approx(expected_root, abs=1e-6)


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize("facing", [0, 1])
def test_dc920_x1a70_xy_fmadds_are_bit_exact_for_both_constraint_modes_and_facings(
    grounded_victim_mode: bool, facing: int
) -> None:
    import struct

    out, _, _ = _debug_dive_fighter_damage(
        grounded_victim_mode=grounded_victim_mode,
        damaged_players=(0,),
        damage=8.0,
        constrained_facing=facing,
    )
    constrained = 0 if grounded_victim_mode else 1
    bits = tuple(
        struct.unpack("<I", struct.pack("<f", float(out[field][constrained])))[0]
        for field in ("pos_x", "pos_y")
    )
    assert bits == ((0x419BCB92, 0x421AA1D1) if grounded_victim_mode else (0xC1A00000, 0x41C80001))


@pytest.mark.parametrize("grounded_victim_mode", [False, True])
@pytest.mark.parametrize("facing", [0, 1])
def test_ddde4_x1a70_xy_fmadds_are_bit_exact_for_both_constraint_modes_and_facings(
    grounded_victim_mode: bool, facing: int
) -> None:
    import struct

    seed = _seed_falcon_dive_hold(grounded_victim_mode=grounded_victim_mode)
    constrained = 0 if grounded_victim_mode else 1
    seed["action_frame"][0, :2] = np.int16(15)
    seed["anim_frame_f32"][0, :2] = np.float32(15.0)
    seed["facing"][0, constrained] = np.uint8(facing)
    out = _run(seed, [_mk_inputs()])[0]
    bits = tuple(
        struct.unpack("<I", struct.pack("<f", float(out[field][constrained])))[0]
        for field in ("pos_x", "pos_y")
    )
    assert bits == ((0xC10EA867, 0x41DB1BD8) if grounded_victim_mode else (0x4174EBBC, 0x4211B1BC))


def test_non_capture_damage_does_not_create_capture_aftermath_negative() -> None:
    seed = _seed_dive_hold_with_third_party_damage(
        grounded_victim_mode=False, item_damage=False
    )
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1)
    seed["grab_owner_port"][0, 1] = np.uint8(0xFF)

    out, internal = _run_with_internals(seed, [_mk_inputs()])[0]
    assert ACT_CAPTURE_CAPTAIN not in set(int(v) for v in out["action_id"][:2])
    assert int(internal["attached_victim_port"][0]) == 0xFF
    assert int(internal["grab_owner_port"][1]) == 0xFF
