from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _i32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=True)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def main() -> None:
    # Note: `data/common/ft_common_data.json` is tracked. Re-run this extractor after any changes
    # here (or when switching ISO) so the committed constants stay in sync.
    ap = argparse.ArgumentParser(description="Extract ftCommonData constants from PlCo.dat (decomp-first).")
    ap.add_argument(
        "--plco",
        type=Path,
        default=Path("_iso/PlCo.dat"),
        help="path to PlCo.dat (HSD archive)",
    )
    ap.add_argument("--out", type=Path, default=Path("data/common/ft_common_data.json"))
    args = ap.parse_args()

    buf = args.plco.read_bytes()
    arc = parse_hsd_archive(buf)

    ft = arc.get_public_offset("ftLoadCommonData")
    if ft is None:
        raise SystemExit("ftLoadCommonData not found in public symbols")

    # Fighter_LoadCommonData expects 23 pointers.
    n_ptr = 23
    raw = buf[ft : ft + n_ptr * 4]
    if len(raw) != n_ptr * 4:
        raise SystemExit("ftLoadCommonData out of bounds")

    ptrs = [_u32_be(raw, i * 4) for i in range(n_ptr)]
    ft_common_abs = arc.data_base + ptrs[0]

    out = {
        # Input processing thresholds
        "lstick_deadzone_x": float(_f32_be(buf, ft_common_abs + 0x00)),
        "lstick_deadzone_y": float(_f32_be(buf, ft_common_abs + 0x04)),
        "lstick_tilt_x_thresh": float(_f32_be(buf, ft_common_abs + 0x08)),
        "lstick_tilt_y_thresh": float(_f32_be(buf, ft_common_abs + 0x0C)),
        "trigger_deadzone": float(_f32_be(buf, ft_common_abs + 0x10)),
        # Walk / turn / run thresholds (ftwalkcommon.c / ftCo_Turn.c / ftCo_Run.c).
        # These are `p_ftCommonData->x24`, `x28`, `x2C`, `x34`, `x58` in doldecomp naming.
        "walk_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x24)),
        "walk_mid_vel_mul": float(_f32_be(buf, ft_common_abs + 0x28)),
        "walk_fast_vel_mul": float(_f32_be(buf, ft_common_abs + 0x2C)),
        "turn_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x34)),
        "run_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x58)),
        # C-stick/L-stick thresholds for aerial attack direction (ftCo_AttackAir.c / ft_0DF1.c):
        # - Neutral-air selection: ABS(stick_x) < xDC && ABS(stick_y) < xE0.
        # - C-stick aerial edge: (ABS(cstick1.x) < xDC && ABS(cstick.x) >= xDC) ||
        #                        (ABS(cstick1.y) < xE0 && ABS(cstick.y) >= xE0).
        "attackair_stick_deadzone_x": float(_f32_be(buf, ft_common_abs + 0xDC)),
        "attackair_stick_deadzone_y": float(_f32_be(buf, ft_common_abs + 0xE0)),
        # Decomp mapping: p_ftCommonData->x18 (fighter.c trigger-press timer threshold for `x672`).
        "powershield_reflect_trigger_min": float(_f32_be(buf, ft_common_abs + 0x18)),
        # Tech / passive windows (ftCo_DownAttack.c / ftCo_PassiveStand.c):
        # - `fp->x680 < x250` gates whether L/R was pressed recently enough to tech.
        # - `fp->x684 >= x1C` is a simple debounce (prevents immediate re-trigger).
        # - `ABS(lstick.x) >= x254` chooses tech-in-place vs tech-roll (PassiveStandF/B).
        "tech_lr_debounce_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x1C))),
        "tech_window_frames": float(_f32_be(buf, ft_common_abs + 0x250)),
        "tech_roll_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x254)),
        # Downed / knockdown input thresholds (ftCo_Down / ft_0DF1.c).
        # - Downed rolls: ABS(stick_x) >= x248 and stick angle < x20_radians.
        # - Buffered getup attack (DownBound/DownWait): x67C/x67D < x24C, or c-stick up edge at x7F4.
        "down_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x248)),
        "down_attack_button_window_frames": float(_f32_be(buf, ft_common_abs + 0x24C)),
        "down_attack_cstick_up_threshold": float(_f32_be(buf, ft_common_abs + 0x7F4)),
        # Shield / guard (ftCo_Guard.c, fighter.c).
        # - Guard hold drain: shield_health -= x278 * (lightshield_amount*(x2F0-x2EC)+x2EC)
        # - Shield hit depletion: shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
        # - Shield stun (GuardSetOff duration): f = x28C * (int_dmg * (1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290
        # - Shield size scaling (ftCo_Guard.c inlineB0):
        #     n1 = (shield_health/x260) * (light*(x2D8-x2D4)+x2D4)
        #     n3 = (1-x264) * n1 + x264
        #     radius = n3 * fp->co_attrs.initial_shield_size
        "start_shield_health": float(_f32_be(buf, ft_common_abs + 0x260)),
        "shield_size_min_scale": float(_f32_be(buf, ft_common_abs + 0x264)),
        "shield_recharge_per_frame": float(_f32_be(buf, ft_common_abs + 0x27C)),
        "shield_hold_drain_mul": float(_f32_be(buf, ft_common_abs + 0x278)),
        "shield_hold_drain_base": float(_f32_be(buf, ft_common_abs + 0x2EC)),
        "shield_hold_drain_max": float(_f32_be(buf, ft_common_abs + 0x2F0)),
        "shield_size_lightshield_min": float(_f32_be(buf, ft_common_abs + 0x2D4)),
        "shield_size_lightshield_max": float(_f32_be(buf, ft_common_abs + 0x2D8)),
        "shield_hit_damage_mul": float(_f32_be(buf, ft_common_abs + 0x284)),
        "shield_hit_damage_base": float(_f32_be(buf, ft_common_abs + 0x288)),
        "shield_hit_lightshield_min": float(_f32_be(buf, ft_common_abs + 0x2DC)),
        "shield_hit_lightshield_max": float(_f32_be(buf, ft_common_abs + 0x2E0)),
        "shield_stun_mul": float(_f32_be(buf, ft_common_abs + 0x28C)),
        "shield_stun_base": float(_f32_be(buf, ft_common_abs + 0x290)),
        "shield_stun_lightshield_min": float(_f32_be(buf, ft_common_abs + 0x2E4)),
        "shield_stun_lightshield_max": float(_f32_be(buf, ft_common_abs + 0x2E8)),
        # Powershield / GuardReflect (ftCo_Guard.c, fighter.c).
        # - `ftCo_80093694`: compares `mv.co.guard.x0` and `fp->x672_input_timer_counter` against x2A0.
        # - `ftCo_8009388C`: initializes `mv.co.guard.x14 = x2A4` and `mv.co.guard.x18 = x2B4`.
        # - `ftCo_8009370C`: reflect desc uses x2A8 size, x2AC damage mul, x2B0 speed mul.
        "powershield_reflect_window_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x2A0))),
        "powershield_reflect_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x2A4)))),
        "powershield_reflect_damage_mul": float(_f32_be(buf, ft_common_abs + 0x2AC)),
        "powershield_reflect_speed_mul": float(_f32_be(buf, ft_common_abs + 0x2B0)),
        "powershield_reflect_total_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x2B4)))),
        # GuardSetOff pushback (ftCo_80092F2C): var_f2 = f * x294; if !x221C_b2 then *= x2BC; clamp to x298.
        "shield_setoff_push_mul": float(_f32_be(buf, ft_common_abs + 0x294)),
        "shield_setoff_push_max": float(_f32_be(buf, ft_common_abs + 0x298)),
        "shield_setoff_push_mul_non_yoshi": float(_f32_be(buf, ft_common_abs + 0x2BC)),
        # Smash stick / flick gating (ftCommon_8008031C; types.h: +0x7B8/+0x7BC/+0x7C0)
        "smash_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x7B8)),
        "cstick_smash_threshold": float(_f32_be(buf, ft_common_abs + 0x7BC)),
        "smash_flick_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x7C0)))),
        # Ground A-attacks input checks (ftCo_Attack*; ftCommonData offsets).
        "attack_angle_threshold_radians": float(_f32_be(buf, ft_common_abs + 0x20)),
        "attack_s3_stick_threshold_x": float(_f32_be(buf, ft_common_abs + 0x98)),
        "attack_hi3_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xAC)),
        "attack_lw3_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xB0)),
        # Up smash (ftCo_AttackHi4_CheckInput): y >= xCC and tilt_y_timer < xD0 (float).
        "attack_hi4_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xCC)),
        "attack_hi4_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0xD0)))),
        # Down smash (ftCo_AttackLw4_CheckInput): y <= xD4 and tilt_y_timer < xD8 (float).
        "attack_lw4_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xD4)),
        "attack_lw4_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0xD8)))),
        # L-cancel (ftCo_LandingAir.c): if cmd_vars[0] and x67F < xE4 then lag /= xE8.
        "lcancel_window_frames": int(max(0, _i32_be(buf, ft_common_abs + 0xE4))),
        "lcancel_lag_div": float(_f32_be(buf, ft_common_abs + 0xE8)),
        # Dash flick (ftCo_Dash_CheckInput)
        "dash_flick_abs": float(_f32_be(buf, ft_common_abs + 0x3C)),
        "dash_flick_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x40)),
        # Roll / spotdodge (ftCo_Escape.c)
        # - Roll: ABS(lstick.x) >= x31C and x670_timer_lstick_tilt_x < x320
        # - Spotdodge: lstick.y <= x314 and x671_timer_lstick_tilt_y < x318
        "spotdodge_stick_y_threshold": float(_f32_be(buf, ft_common_abs + 0x314)),
        "spotdodge_flick_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x318)),
        "escape_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x31C)),
        "escape_flick_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x320)),
        # Air dodge (EscapeAir) initial force + decay
        "escapeair_deadzone_x": float(_f32_be(buf, ft_common_abs + 0x32C)),
        "escapeair_deadzone_y": float(_f32_be(buf, ft_common_abs + 0x330)),
        "escapeair_timer_frames": int(_i32_be(buf, ft_common_abs + 0x334)),
        "escapeair_force": float(_f32_be(buf, ft_common_abs + 0x338)),
        "escapeair_decay": float(_f32_be(buf, ft_common_abs + 0x33C)),
        # FallSpecial mobility scalar (ftCo_FallSpecial / EscapeAir -> FallSpecial).
        # Decomp:
        # - EscapeAir_Anim calls ftCo_80096900(..., p_ftCommonData->x340, p_ftCommonData->x344).
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
        # - ftCo_80096900 stores `mv.co.fallspecial.mobility = ca->air_drift_max * mobility`.
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
        "fall_special_mobility_scalar": float(_f32_be(buf, ft_common_abs + 0x340)),
        # Landing lag for LandingFallSpecial when landing out of EscapeAir (airdodge).
        # Decomp: EscapeAir_Coll -> callback -> ftCo_LandingFallSpecial_Enter(..., p_ftCommonData->x344).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c:117
        "landing_fall_special_lag_frames": float(_f32_be(buf, ft_common_abs + 0x344)),
        # Crouch (ftCo_Squat / SquatWait)
        "crouch_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x90)),
        # Gameplay thresholds
        "tap_jump_threshold": float(_f32_be(buf, ft_common_abs + 0x70)),
        "tap_jump_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x74)),
        # Jump direction (ftCo_Jump_Enter / ftCo_JumpAerial_Enter_Basic):
        # (lstick.x * facing_dir) > -x78 ? JumpF : JumpB
        "jump_back_x_threshold": float(_f32_be(buf, ft_common_abs + 0x78)),
        "tap_jump_release_threshold": float(_f32_be(buf, ft_common_abs + 0x7C)),
        "fastfall_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x88)),
        "fastfall_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x8C)),
        # Pass-through platforms (ftCo_80099F1C / mpUpdateFloorSkip)
        "pass_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x464)),
        "pass_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x468)))),
        "floor_skip_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x470)))),
        "pass_vel_y": float(_f32_be(buf, ft_common_abs + 0x46C)),
        # Match start (ft_0C31.c): EntryStart/EntryEnd timers.
        "entry_start_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x6BC))),
        "entry_end_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x6C0))),
        "entry_scale_y": float(_f32_be(buf, ft_common_abs + 0x6C4)),
        # Movement scaling
        "walk_accel_scale_mul": float(_f32_be(buf, ft_common_abs + 0x30)),
        # Dash IASA velocity decay multiplier (ftCo_Dash.c): gr_vel += -gr_vel * x54 * traction
        "dash_iasa_vel_mul": float(_f32_be(buf, ft_common_abs + 0x54)),
        # Dash IASA frame windows (ftCo_Dash.c): thresholds against `fp->cur_anim_frame`.
        # These are floats in ftCommonData but represent whole-frame cutoffs in practice.
        "dash_iasa_x44": float(_f32_be(buf, ft_common_abs + 0x44)),
        "dash_iasa_x48": float(_f32_be(buf, ft_common_abs + 0x48)),
        "dash_iasa_x4c": float(_f32_be(buf, ft_common_abs + 0x4C)),
        "run_accel_scale_mul": float(_f32_be(buf, ft_common_abs + 0x5C)),
        "run_friction_mul": float(_f32_be(buf, ft_common_abs + 0x60)),
        # Catch / CatchDash ground friction multiplier (ftCo_Attack100.s: ftCo_Catch_Phys / ftCo_CatchDash_Phys):
        #   ftCommon_ApplyFrictionGround(fp, x64 * fp->co_attrs.gr_friction)
        "catch_friction_mul": float(_f32_be(buf, ft_common_abs + 0x64)),
        # ft_80084F3C scales ground friction by p_ftCommonData->x6C when |gr_vel| > walk_max_vel.
        "high_speed_friction_mul": float(_f32_be(buf, ft_common_abs + 0x6C)),
        "walk_anim_vel_mul": float(_f32_be(buf, ft_common_abs + 0x440)),
        # Guard pose update smoothing (ftCo_Guard.c `ftCo_80091BC4`):
        # - `mv.co.guard.x8 = 10 + normalizeAngle0(normalizeAngle180(deg-offset) * x44C + offset)`
        # - `mv.co.guard.x4 = x44C * (stick_mag - x4) + x4`
        "guard_stick_lerp_x44c": float(_f32_be(buf, ft_common_abs + 0x44C)),
        # Knockback + hitlag constants (ftCo_Damage / fighter.c / ftCommon_CalcHitlag)
        #
        # Collision knockback magnitude constants (ftColl_80079EA8):
        # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8
        # refs/melee/src/melee/ft/ftcoll.h::ftColl_80079EA8
        "kb_weight_mul": float(_f32_be(buf, ft_common_abs + 0xF4)),
        "kb_weight_mul2": float(_f32_be(buf, ft_common_abs + 0xF8)),
        "kb_applied_max": float(_f32_be(buf, ft_common_abs + 0x108)),
        "kb_base_term": float(_f32_be(buf, ft_common_abs + 0x110)),
        "kb_dmg_mul": float(_f32_be(buf, ft_common_abs + 0x114)),
        "kb_wsk_mul": float(_f32_be(buf, ft_common_abs + 0x118)),
        "kb_growth_mul": float(_f32_be(buf, ft_common_abs + 0x11C)),
        "kb_base_add": float(_f32_be(buf, ft_common_abs + 0x120)),
        # - ftCo_Damage_CalcVel merges new kb_vel with existing kb_vel when `time_since_hit >= xFC`.
        "kb_vel_merge_since_hit_frames": int(_i32_be(buf, ft_common_abs + 0xFC)),
        "kb_vel_mul": float(_f32_be(buf, ft_common_abs + 0x100)),
        "kb_min": float(_f32_be(buf, ft_common_abs + 0x104)),
        "kb_squat_mul": float(_f32_be(buf, ft_common_abs + 0x124)),
        # Hitstun scaling + damage severity thresholds (ftCo_Damage.c):
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_ScaleBy154
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008D8E8
        "damage_hitstun_mul": float(_f32_be(buf, ft_common_abs + 0x154)),
        "damage_severity_x158": float(_f32_be(buf, ft_common_abs + 0x158)),
        "damage_severity_x15c": float(_f32_be(buf, ft_common_abs + 0x15C)),
        "damage_severity_x160": float(_f32_be(buf, ft_common_abs + 0x160)),
        # DamageFly landings (ftCo_DamageFly_Coll): thresholds on |kb_vel| for DownBound vs Landing.
        "damagefly_downbound_kb_vel_threshold": float(_f32_be(buf, ft_common_abs + 0x1E0)),
        "damagefly_landing_kb_vel_threshold": float(_f32_be(buf, ft_common_abs + 0x1E4)),
        # Damage jump-buffer window (ftCo_Damage.c): when a jump input is detected during hitstun,
        # `mv.co.damage.x14` is set to the current timer `x0`, and subsequent IASA frames inject
        # `input.x668 |= HSD_PAD_XY` while `x14 <= x1D0`.
        "damage_jump_buffer_window_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x1D0)))),
        # DamageFlyTop angle window (radians).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
        "damagefly_top_angle_min_radians": float(_f32_be(buf, ft_common_abs + 0x234)),
        "damagefly_top_angle_max_radians": float(_f32_be(buf, ft_common_abs + 0x238)),
        # Sakurai angle constants (ftCo_Damage_CalcAngle; for hitbox angle=361)
        "sakurai_air_radians": float(_f32_be(buf, ft_common_abs + 0x144)),
        "sakurai_ground_deg_max": float(_f32_be(buf, ft_common_abs + 0x148)),
        "sakurai_kb_threshold": float(_f32_be(buf, ft_common_abs + 0x14C)),
        "sakurai_kb_max": float(_f32_be(buf, ft_common_abs + 0x150)),
        # Air motion KB multiplier (ftCo_Damage_CheckAirMotion): applied when victim in jump/fall states
        "air_motion_kb_mul": float(_f32_be(buf, ft_common_abs + 0x190)),
        # Frame conditions for air motion check (ftCo_Damage_CheckAirMotion)
        "air_motion_max_frames": int(_i32_be(buf, ft_common_abs + 0x18C)),
        "hitlag_dmg_mul": float(_f32_be(buf, ft_common_abs + 0x198)),
        "hitlag_base": float(_f32_be(buf, ft_common_abs + 0x19C)),
        "hitlag_squat_mul": float(_f32_be(buf, ft_common_abs + 0x1A0)),
        # Grounded knockback friction multiplier (fighter.c): effective friction = gr_friction * x200.
        "ground_kb_friction_mul": float(_f32_be(buf, ft_common_abs + 0x200)),
        "knockback_frame_decay": float(_f32_be(buf, ft_common_abs + 0x204)),
        # Ledge regrab cooldown (fighter.c / ftCo_Cliff*)
        "ledge_cooldown_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x498))),
        # SDI / ASDI / DI (ftCo_Damage.c)
        # - SDI: x4B0 (radius), x4B4 (tilt timer max), x4B8 (step mul) via ftCo_Damage_OnEveryHitlag
        # - ASDI: x4BC (step mul) via ftCo_Damage_OnExitHitlag
        # - Shield SDI/ASDI: x4C0 multiplier via ftCo_80093240 / ftCo_800932DC (ftCo_Guard.c)
        # - DI: x1A8 (max degrees multiplier) via ftCo_8008E5A4
        "sdi_radius": float(_f32_be(buf, ft_common_abs + 0x4B0)),
        "sdi_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x4B4)),
        "sdi_step_mul": float(_f32_be(buf, ft_common_abs + 0x4B8)),
        "asdi_step_mul": float(_f32_be(buf, ft_common_abs + 0x4BC)),
        "shield_sdi_mul": float(_f32_be(buf, ft_common_abs + 0x4C0)),
        "di_max_deg": float(_f32_be(buf, ft_common_abs + 0x1A8)),
        # LSI (Launch Speed Influence) - kb magnitude multiplier when L/R held at hitlag exit (ftCo_Damage_OnExitHitlag)
        "lsi_lr_held_mul": float(_f32_be(buf, ft_common_abs + 0x1AC)),
        # Cliff / ledge common behavior (ftCo_Cliff*)
        "cliff_drop_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x480)),
        "cliff_wait_percent_threshold": float(max(0, _i32_be(buf, ft_common_abs + 0x488))),
        "cliff_wait_frames_low_percent": float(_f32_be(buf, ft_common_abs + 0x48C)),
        "cliff_wait_frames_high_percent": float(_f32_be(buf, ft_common_abs + 0x490)),
        "cliff_option_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x494)),
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
