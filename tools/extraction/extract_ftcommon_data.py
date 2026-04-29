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
        # Walk / turn / run thresholds (ftwalkcommon.c / ftCo_Turn.c / ftCo_Run.c / ftCo_TurnRun.c).
        # These are `p_ftCommonData->x24`, `x28`, `x2C`, `x34`, `x38`, `x58`, `x474` in
        # doldecomp naming.
        "walk_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x24)),
        "walk_mid_vel_mul": float(_f32_be(buf, ft_common_abs + 0x28)),
        "walk_fast_vel_mul": float(_f32_be(buf, ft_common_abs + 0x2C)),
        "turn_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x34)),
        "turn_run_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x38)),
        "run_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x58)),
        "ottotto_walk_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x474)),
        # Run IASA lockout init (fp->mv.co.run.x0) used for specific Run entries (notably TurnRun->Run).
        # Decomp:
        # - fn_800CA644 passes p_ftCommonData->x430 as arg0 to ftCo_Run_Enter (stores into mv.co.run.x0).
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
        # - TurnRun_Anim uses fn_800CA644 when the TurnRun anim finishes.
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
        "run_x0_init_x430": float(_f32_be(buf, ft_common_abs + 0x430)),
        # C-stick/L-stick thresholds for aerial attack direction (ftCo_AttackAir.c / ft_0DF1.c):
        # - Neutral-air selection: ABS(stick_x) < xDC && ABS(stick_y) < xE0.
        # - C-stick aerial edge: (ABS(cstick1.x) < xDC && ABS(cstick.x) >= xDC) ||
        #                        (ABS(cstick1.y) < xE0 && ABS(cstick.y) >= xE0).
        "attackair_stick_deadzone_x": float(_f32_be(buf, ft_common_abs + 0xDC)),
        "attackair_stick_deadzone_y": float(_f32_be(buf, ft_common_abs + 0xE0)),
        # Special move direction selection (ftCo_SpecialAir.c / ftCo_SpecialS.c).
        #
        # Decomp:
        # - Up/Down special: stick.y >= x21C / stick.y <= -x21C
        # - Side special: ABS(stick.x) >= x218
        # - Side-special facing update (B-reverse): stick.x * facing_dir < -x220
        # - Neutral-special facing update (B-turn): fp->x676_x < x224 and fp->x2228_b7 parity
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c
        "special_stick_x_threshold_side": float(_f32_be(buf, ft_common_abs + 0x218)),
        "special_stick_y_threshold": float(_f32_be(buf, ft_common_abs + 0x21C)),
        "special_side_reverse_threshold": float(_f32_be(buf, ft_common_abs + 0x220)),
        # DamageFall IASA -> Fall gate (ftCo_DamageFall_IASA):
        # - ABS(lstick.x) >= x210
        # - x670_timer_lstick_tilt_x < x214
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
        "damagefall_fall_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x210)),
        "damagefall_fall_tilt_max_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x214))),
        # Note: ftCommonData.x224 is an int (refs/melee/src/melee/ft/types.h), but many decomp
        # callsites compare it against a u8 timer (fp->x676_x). Store as an int count.
        "special_neutral_reverse_threshold": int(max(0, _i32_be(buf, ft_common_abs + 0x224))),
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
        # - DownStand input: stick.y >= x244 and stick angle >= x20_radians.
        # - Buffered getup attack (DownBound/DownWait): x67C/x67D < x24C, or c-stick up edge at x7F4.
        # - DownWait timer init: mv.co.downwait.x0 = x424.
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
        "down_stand_stick_y_threshold": float(_f32_be(buf, ft_common_abs + 0x244)),
        "down_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x248)),
        "down_attack_button_window_frames": float(_f32_be(buf, ft_common_abs + 0x24C)),
        "down_attack_cstick_up_threshold": float(_f32_be(buf, ft_common_abs + 0x7F4)),
        "down_wait_frames": float(_f32_be(buf, ft_common_abs + 0x424)),
        # Downed low-damage contact gate:
        # - ftCo_8009F0F0 routes DownBound/DownWait/DownDamage into DownDamage when
        #   `fp->x2224_b2 || fp->dmg.x1838_percentTemp < p_ftCommonData->x428`.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F0F0
        "down_damage_percent_threshold": int(max(0, _i32_be(buf, ft_common_abs + 0x428))),
        # Combo timer window after hitstun ends (GALE01 fp->x2098 reset).
        #
        # Decomp trail:
        # - set when hitstun ends:
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744 (fp->x2098 = p_ftCommonData->x4CC)
        # - decremented and used for clearing attacker combo victim (fp->x2094):
        #   refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
        "combo_timer_post_hitstun_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x4CC))),
        # PassiveWall / PassiveWallJump startup timer (`fp->mv.co.passivewall.timer`).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}
        "passivewall_timer_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x760))),
        # PassiveWall / PassiveWallJump entry hurt-status ownership (ftCo_PassiveWall.c).
        # - ftCo_800C1E64 calls ftColl_8007B760(..., x764) on entry.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
        "colanim_passivewall_x1990_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x764))),
        # Generic wall-jump interrupt constants (ftWallJump_8008169C).
        # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
        "walljump_input_window_frames": float(_f32_be(buf, ft_common_abs + 0x768)),
        "walljump_stick_x_threshold": float(_f32_be(buf, ft_common_abs + 0x76C)),
        "walljump_tilt_x_max_frames": float(_f32_be(buf, ft_common_abs + 0x770)),
        "walljump_startup_timer_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x774))),
        # Shield / guard (ftCo_Guard.c, fighter.c).
        # - Guard hold drain: shield_health -= x278 * (lightshield_amount*(x2F0-x2EC)+x2EC)
        # - Shield hit depletion: shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
        # - Shield stun (GuardSetOff duration): f = x28C * (int_dmg * (1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290
        # - Shield size scaling (ftCo_Guard.c inlineB0):
        #     n1 = (shield_health/x260) * (light*(x2D8-x2D4)+x2D4)
        #     n3 = (1-x264) * n1 + x264
        #     radius = n3 * fp->co_attrs.initial_shield_size
        "start_shield_health": float(_f32_be(buf, ft_common_abs + 0x260)),
        "shield_break_reset_health": float(_f32_be(buf, ft_common_abs + 0x280)),
        # Shield-break dizzy (`Furafura`) timer.
        # Decomp:
        # - ftCo_80099010 initializes fp->grab_timer =
        #     MAX(p_ftCommonData->x2F8 - fp->dmg.x1830_percent, 0) + p_ftCommonData->x2FC.
        # - ftCo_Furafura_Anim subtracts x300 each callback, applies ftCommon_GrabMash(..., x304),
        #   and exits through ft_8008A2BC when the timer reaches zero.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c
        "furafura_timer_percent_base": float(_f32_be(buf, ft_common_abs + 0x2F8)),
        "furafura_timer_base": float(_f32_be(buf, ft_common_abs + 0x2FC)),
        "furafura_timer_decrement": float(_f32_be(buf, ft_common_abs + 0x300)),
        "furafura_mash_decrement": float(_f32_be(buf, ft_common_abs + 0x304)),
        "shield_size_min_scale": float(_f32_be(buf, ft_common_abs + 0x264)),
        # Guard release lockout timer init (mv.co.guard.x10).
        #
        # Decomp:
        # - fp->mv.co.guard.x10 = p_ftCommonData->x268 on GuardOn entry,
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
        # - decremented while shielding in ftCo_800925A4,
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
        "guard_x10_init_frames": float(_f32_be(buf, ft_common_abs + 0x268)),
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
        "powershield_reflect_size": float(_f32_be(buf, ft_common_abs + 0x2A8)),
        "powershield_reflect_damage_mul": float(_f32_be(buf, ft_common_abs + 0x2AC)),
        "powershield_reflect_speed_mul": float(_f32_be(buf, ft_common_abs + 0x2B0)),
        "powershield_reflect_total_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x2B4)))),
        # GuardSetOff pushback (ftCo_80092F2C): var_f2 = f * x294; if !x221C_b2 then *= x2BC; clamp to x298.
        "shield_setoff_push_mul": float(_f32_be(buf, ft_common_abs + 0x294)),
        "shield_setoff_push_max": float(_f32_be(buf, ft_common_abs + 0x298)),
        "shield_setoff_push_mul_non_yoshi": float(_f32_be(buf, ft_common_abs + 0x2BC)),
        # Phantom-hit overlap cap (ftColl_80076ED8).
        # - `inlineB1(hit)` checks `hit->coll_distance < p_ftCommonData->x7A8`.
        # - Datasheet labels x7A8 as the max overlap amount that still counts as a phantom hit.
        # refs/melee/src/melee/ft/ftcoll.c::{inlineB1,ftColl_80076ED8}
        # refs/datasheet/plco_offsets.txt
        "phantom_overlap_max_x7a8": float(_f32_be(buf, ft_common_abs + 0x7A8)),
        # Smash stick / flick gating.
        # - Grounded dash / side-smash checks consume p_ftCommonData->x3C/x40.
        # - Grounded c-stick smash edges in ft_0DF1.c::{ftCo_800DF1C8,ftCo_800DF2D8,ftCo_800DF3A8}
        #   also compare against the same x3C / xCC / xD4 thresholds.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_CheckInput
        # refs/melee/src/melee/ft/ft_0DF1.c::{ftCo_800DF1C8,ftCo_800DF2D8,ftCo_800DF3A8}
        "smash_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x7B8)),
        "cstick_smash_threshold": float(_f32_be(buf, ft_common_abs + 0x3C)),
        "smash_flick_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0x7C0)))),
        # Throw entry anim-speed weight scalar.
        #
        # Decomp:
        # - ftCo_800DD4B0 computes weight-based throw anim speed as:
        #     1.0f / (victim->ft_data->x0->weight * p_ftCommonData->x37C)
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
        "throw_anim_speed_weight_mul": float(_f32_be(buf, ft_common_abs + 0x37C)),
        # Ground A-attacks input checks (ftCo_Attack*; ftCommonData offsets).
        "attack_angle_threshold_radians": float(_f32_be(buf, ft_common_abs + 0x20)),
        "attack_s3_stick_threshold_x": float(_f32_be(buf, ft_common_abs + 0x98)),
        # Side-tilt angle splits used by ftCo_AttackS3.c::decideAngle after the initial
        # ftCo_AttackS3_CheckInput gate succeeds.
        "attack_s3_hi_angle_radians": float(_f32_be(buf, ft_common_abs + 0x9C)),
        "attack_s3_hi_s_angle_radians": float(_f32_be(buf, ft_common_abs + 0xA0)),
        "attack_s3_lw_s_angle_radians": float(_f32_be(buf, ft_common_abs + 0xA4)),
        "attack_s3_lw_angle_radians": float(_f32_be(buf, ft_common_abs + 0xA8)),
        "attack_hi3_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xAC)),
        "attack_lw3_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xB0)),
        # Up smash (ftCo_AttackHi4_CheckInput): y >= xCC and tilt_y_timer < xD0 (float).
        "attack_hi4_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xCC)),
        "attack_hi4_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0xD0)))),
        # Down smash (ftCo_AttackLw4_CheckInput): y <= xD4 and tilt_y_timer < xD8 (float).
        "attack_lw4_stick_threshold_y": float(_f32_be(buf, ft_common_abs + 0xD4)),
        "attack_lw4_tilt_max_frames": int(round(float(_f32_be(buf, ft_common_abs + 0xD8)))),
        # DamageFlyRoll gating params (ftCo_Damage.c):
        # - Percent threshold: compare against fp->dmg.x1838_percentTemp.
        # - Probability: compared against HSD_Randf() to decide DamageFlyRoll entry.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c (search for M2C_FIELD(..., 0x23C/0x240))
        "damagefly_roll_percent_threshold": int(_i32_be(buf, ft_common_abs + 0x23C)),
        "damagefly_roll_prob": float(_f32_be(buf, ft_common_abs + 0x240)),
        # L-cancel (ftCo_LandingAir.c): if cmd_vars[0] and x67F < xE4 then lag /= xE8.
        "lcancel_window_frames": int(max(0, _i32_be(buf, ft_common_abs + 0xE4))),
        "lcancel_lag_div": float(_f32_be(buf, ft_common_abs + 0xE8)),
        # Dash flick (ftCo_Dash_CheckInput)
        "dash_flick_abs": float(_f32_be(buf, ft_common_abs + 0x3C)),
        "dash_flick_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x40)),
        # AttackDash Phys friction multiplier.
        # Decomp: ftCo_AttackDash_Phys -> ft_80085030(..., p_ftCommonData->x50 * traction, ...).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Phys
        "attackdash_friction_mul": float(_f32_be(buf, ft_common_abs + 0x50)),
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
        # SquatRv exit threshold from SquatWait.
        # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_CheckInput
        #         (fp->input.lstick.y > -p_ftCommonData->x94)
        "crouch_release_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x94)),
        # Gameplay thresholds
        "tap_jump_threshold": float(_f32_be(buf, ft_common_abs + 0x70)),
        "tap_jump_tilt_max_frames": int(_i32_be(buf, ft_common_abs + 0x74)),
        # Jump direction (ftCo_Jump_Enter / ftCo_JumpAerial_Enter_Basic):
        # (lstick.x * facing_dir) > -x78 ? JumpF : JumpB
        "jump_back_x_threshold": float(_f32_be(buf, ft_common_abs + 0x78)),
        "tap_jump_release_threshold": float(_f32_be(buf, ft_common_abs + 0x7C)),
        # Dash/Run/RunBrake/TurnRun jump gate uses fn_800CAF78, which compares against x80
        # instead of ftCo_Jump_GetInput's tap_jump_threshold (x70).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
        "dash_run_jump_stick_y_threshold": float(_f32_be(buf, ft_common_abs + 0x80)),
        # Grab mash updates x1A50/x1A51 when lstick.{x,y} crosses +/-x308.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
        "grab_mash_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x308)),
        # CaptureWait anim-rate ownership (ftCo_CaptureWaitHi/Lw_Anim).
        # - x3A4: per-frame grab timer decrement while captured.
        # - x3A8: per-mash extra grab_timer damage passed into ftCommon_GrabMash.
        # - x3AC: XY jump-latch window (`mv.co.capturewait.x0 < x3AC` in fn_800DC014).
        # - x3B0/x3B4: mash-rate hold timer and boosted anim rate.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DC014
        "capture_wait_grab_timer_decrement": float(_f32_be(buf, ft_common_abs + 0x3A4)),
        "capture_wait_grab_mash_damage": float(_f32_be(buf, ft_common_abs + 0x3A8)),
        "capture_wait_jump_latch_window_frames": float(_f32_be(buf, ft_common_abs + 0x3AC)),
        "capture_wait_anim_rate_hold_frames": float(_f32_be(buf, ft_common_abs + 0x3B0)),
        "capture_wait_anim_rate": float(_f32_be(buf, ft_common_abs + 0x3B4)),
        # Common grab/capture breakout timer formula + exit velocities.
        # Decomp:
        # - ftCo_800DA824 computes the initial grab timer from x354/x358/x35C/x360/x364/x368.
        # - ftCo_800DA698 / fn_800DC070 / ftCo_CaptureCut_Enter consume x370/x374/x378.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DA824,ftCo_800DA698,fn_800DC070}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_CaptureCut_Enter
        "capture_grab_timer_base": float(_f32_be(buf, ft_common_abs + 0x354)),
        "capture_grab_timer_handicap_mul": float(_f32_be(buf, ft_common_abs + 0x358)),
        "capture_grab_timer_handicap_base": float(_f32_be(buf, ft_common_abs + 0x35C)),
        "capture_grab_timer_slot_mul": float(_f32_be(buf, ft_common_abs + 0x360)),
        "capture_grab_timer_slot_base": float(_f32_be(buf, ft_common_abs + 0x364)),
        "capture_grab_timer_percent_mul": float(_f32_be(buf, ft_common_abs + 0x368)),
        "capture_cut_escape_speed": float(_f32_be(buf, ft_common_abs + 0x370)),
        "capture_jump_escape_speed_x": float(_f32_be(buf, ft_common_abs + 0x374)),
        "capture_jump_escape_speed_y": float(_f32_be(buf, ft_common_abs + 0x378)),
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
        # Offscreen death / match-flow timers (ft_0D31.c).
        # - Top blastzone DeadUpStar gate: fp->x8c_kb_vel.y > p_ftCommonData->x4F0
        #   refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
        # - DeadDown/Left/Right timer: fp->mv.co.unk_800D3680.x40 = p_ftCommonData->x500
        #   refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3680
        # - DeadUpStar initial timer:
        #   fp->mv.co.unk_deadup.x40 = p_ftCommonData->x504
        #   refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D40B8
        # - DeadUpStar phase timers:
        #   ftCo_DeadUpStar_Anim uses p_ftCommonData fields at 0x508 and 0x50C as per-phase timers:
        #     - on phase enter: fp->x2340 = *(p_ftCommonData + 0x508); fp->x2344 = 1
        #     - on stock loss:  fp->x2340 = *(p_ftCommonData + 0x50C); fp->x2344 = 2
        #   The same phase-enter branch writes:
        #     - fp->self_vel.z = *(p_ftCommonData + 0x510) / x508
        #     - fp->self_vel.y = (*(p_ftCommonData + 0x514) * Stage_GetCamBoundsTopOffset()
        #                         - fp->cur_pos.y) / x508
        #   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
        # - Rebirth/RebirthWait timers:
        #   - Rebirth:     fp->x2340 = *(p_ftCommonData + 0x5D0) right before ChangeMotionState(Rebirth)
        #     refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (see callsite around 800D5640)
        #   - RebirthWait: fp->x2340 = *(p_ftCommonData + 0x5D4) on enter
        #     refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (see ftCo_800D5600 block around 800D5A08)
        # - RebirthWait -> Fall collision status:
        #   - ftCo_RebirthWait_{Anim,IASA} call ftColl_8007B7A4(gobj, p_ftCommonData->x5D8) before Fall enter.
        #     refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::{ftCo_RebirthWait_Anim,ftCo_RebirthWait_IASA}
        #   - fn_800D5A30 (RebirthWait_Coll helper) calls ftColl_8007B7A4(gobj, p_ftCommonData->x5D8) before
        #     ft_8008A2BC (usually Fall enter).
        #     refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::fn_800D5A30
        "dead_up_kb_vel_threshold": float(_f32_be(buf, ft_common_abs + 0x4F0)),
        "dead_timer_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x500))),
        "dead_up_star_initial_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x504))),
        "dead_up_star_phase1_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x508))),
        "dead_up_star_phase2_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x50C))),
        "dead_up_star_phase1_z_vel_total": float(_f32_be(buf, ft_common_abs + 0x510)),
        "dead_up_star_phase1_cam_top_mul": float(_f32_be(buf, ft_common_abs + 0x514)),
        "rebirth_timer_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x5D0))),
        "rebirth_wait_timer_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x5D4))),
        "colanim_rebirth_fall_x1994_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x5D8))),
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
        # Grounded fighter-overlap nudge (ftCommon_8007DD7C / ftCommon_8007E0E4):
        # - x450 contributes to fp->xF8_playerNudgeVel.x on horizontal pushbox overlap.
        # - x454 contributes to fp->xF8_playerNudgeVel.y (engine-space Z lane).
        # - x458 clamps the normal (non-x221F_b4) depth lane in ftCommon_8007E0E4.
        "player_nudge_x": float(_f32_be(buf, ft_common_abs + 0x450)),
        "player_nudge_z": float(_f32_be(buf, ft_common_abs + 0x454)),
        "player_nudge_z_max": float(_f32_be(buf, ft_common_abs + 0x458)),
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
        # Throw release uses x10C as ftColl_80079AB0's `weight` argument.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
        "throw_kb_weight_x10c": float(_f32_be(buf, ft_common_abs + 0x10C)),
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
        # Damage scalar used by ftColl on a specific "victim_gobj != NULL and != attacker" branch.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_800765F0
        # refs/melee/src/melee/ft/ftcoll.c::inlineB3
        "ftcoll_damage_mul_x128": float(_f32_be(buf, ft_common_abs + 0x128)),
        # ftCo_Damage_CalcKnockback additional modifiers (GALE01):
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
        # Offsets (relative to ftCommonData base) are from decomp struct:
        # refs/melee/src/melee/ft/types.h (ftCommonData fields at +0x6F0, +0x718, +0x7C4)
        "metal_armor": float(_f32_be(buf, ft_common_abs + 0x6F0)),
        "kb_ice_mul": float(_f32_be(buf, ft_common_abs + 0x718)),
        "kb_smashcharge_mul": float(_f32_be(buf, ft_common_abs + 0x7C4)),
        # Hitstun scaling + damage severity thresholds (ftCo_Damage.c):
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_ScaleBy154
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008D8E8
        "damage_hitstun_mul": float(_f32_be(buf, ft_common_abs + 0x154)),
        "damage_severity_x158": float(_f32_be(buf, ft_common_abs + 0x158)),
        "damage_severity_x15c": float(_f32_be(buf, ft_common_abs + 0x15C)),
        "damage_severity_x160": float(_f32_be(buf, ft_common_abs + 0x160)),
        # Grounded tumble-only meteor rebound branch in ftCo_8008DCE0.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        "grounded_tumble_bounce_angle_extra_radians": float(_f32_be(buf, ft_common_abs + 0x1E8)),
        "grounded_tumble_bounce_y_mul": float(_f32_be(buf, ft_common_abs + 0x1EC)),
        # DamageFly landings (ftCo_DamageFly_Coll): thresholds on |kb_vel| for DownBound vs Landing.
        "damagefly_downbound_kb_vel_threshold": float(_f32_be(buf, ft_common_abs + 0x1E0)),
        "damagefly_landing_kb_vel_threshold": float(_f32_be(buf, ft_common_abs + 0x1E4)),
        # DamageFly no-tech wall/ceiling reflect.
        #
        # Decomp:
        # - ftCo_800C17CC uses x1B0 as the wall/ceiling KB threshold.
        # - ftCo_800C18A8 scales mirrored velocity by x1BC, sets x18 from x1C0, and calls
        #   ftColl_8007B760(gobj, x1B8).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{
        #   ftCo_800C15F4,ftCo_800C1718,ftCo_800C17CC,ftCo_800C18A8}
        "damagefly_reflect_speed_threshold": float(_f32_be(buf, ft_common_abs + 0x1B0)),
        "colanim_flyreflect_x1990_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x1B8))),
        "damagefly_reflect_speed_mul": float(_f32_be(buf, ft_common_abs + 0x1BC)),
        "damagefly_reflect_lockout_frames": int(
            max(0, round(float(_f32_be(buf, ft_common_abs + 0x1C0))))
        ),
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
        # Hitlag multiplier (fp->x1960_vibrateMult) for electric hits.
        #
        # Decomp/ASM evidence (GALE01):
        # - ftColl_8007A06C sets `fp->x1960_vibrateMult = p_ftCommonData->x1A4` when the hit element is 2.
        #   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (search for `stfs f0, 0x1960`)
        # - ftCommon_CalcHitlag consumes the multiplier argument as `mul`.
        #   refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
        "hitlag_electric_mul": float(_f32_be(buf, ft_common_abs + 0x1A4)),
        # Clank damage-delta threshold consumed by ftColl_8007699C:
        # - if ((int)dmg_other - x3CC < (int)dmg_self) the side-specific clank branch runs,
        # - full clank-confirm requires the reciprocal check too.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
        # refs/melee/src/melee/ft/types.h (ftCommonData +0x3CC is s32)
        "clank_damage_diff_threshold": int(_i32_be(buf, ft_common_abs + 0x3CC)),
        # Rebound clank-response constants:
        # - ftColl inlineA0/inlineA1 derive `fp->dmg.x191C = int_dmg * x3D0 + x3D4` when the clanking
        #   hitbox requests rebound and the fighter is grounded.
        # - ftCo_80099D9C then derives rebound ground velocity from `x191C * x3D8 + x3DC`.
        # refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
        "rebound_damage_x191c_mul": float(_f32_be(buf, ft_common_abs + 0x3D0)),
        "rebound_damage_x191c_base": float(_f32_be(buf, ft_common_abs + 0x3D4)),
        "rebound_ground_x0_mul": float(_f32_be(buf, ft_common_abs + 0x3D8)),
        "rebound_ground_x0_base": float(_f32_be(buf, ft_common_abs + 0x3DC)),
        # Air drift overspeed friction (ftCommon_8007CF58 / ftCommon_8007D050): when
        # ABS(self_vel.x) > co_attrs.air_drift_max, the engine uses p_ftCommonData->x1FC as the
        # friction magnitude for the "clamp back toward max drift" step.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58
        "air_drift_overmax_friction": float(_f32_be(buf, ft_common_abs + 0x1FC)),
        # Grounded knockback friction multiplier (fighter.c): effective friction = gr_friction * x200.
        "ground_kb_friction_mul": float(_f32_be(buf, ft_common_abs + 0x200)),
        "knockback_frame_decay": float(_f32_be(buf, ft_common_abs + 0x204)),
        # Ledge regrab cooldown (fighter.c / ftCo_Cliff*)
        "ledge_cooldown_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x498))),
        # Collision hit-status timers (x198C path).
        #
        # Decomp:
        # - Damage hitlag-exit hook calls ftColl_8007B7A4(gobj, p_ftCommonData->x130).
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
        # - Throw entry calls ftColl_8007B7A4(gobj, p_ftCommonData->x348), which sets x1994 and x198C.
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
        # - Cliff wait path calls ftColl_8007B760(gobj, p_ftCommonData->x49C), which sets x1990 and x198C.
        #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C
        "colanim_damage_x1994_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x130))),
        "colanim_throw_x1994_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x348))),
        "colanim_cliff_x1990_frames": int(max(0, _i32_be(buf, ft_common_abs + 0x49C))),
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
        # Grounded attacker-on-shield pushback.
        #
        # Decomp:
        # - Shield-hit apply stores `fp->dmg.x1928 = defender.lightshield_amount * int_dmg`.
        # - Fighter post-hit processing shapes grounded attacker shield KB as:
        #     eval = x1928 * x3E0 + x3E4
        #     xF4_ground_attacker_shield_kb_vel = +/-eval
        # - Grounded decay uses gr_friction * x3EC.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_procUpdate}
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CE4C
        "shield_attacker_ground_kb_mul": float(_f32_be(buf, ft_common_abs + 0x3E0)),
        "shield_attacker_ground_kb_base": float(_f32_be(buf, ft_common_abs + 0x3E4)),
        "shield_attacker_ground_friction_mul": float(_f32_be(buf, ft_common_abs + 0x3EC)),
        # Cliff / ledge common behavior (ftCo_Cliff*)
        "cliff_drop_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x480)),
        "cliff_wait_percent_threshold": float(max(0, _i32_be(buf, ft_common_abs + 0x488))),
        "cliff_wait_frames_low_percent": float(_f32_be(buf, ft_common_abs + 0x48C)),
        "cliff_wait_frames_high_percent": float(_f32_be(buf, ft_common_abs + 0x490)),
        "cliff_option_stick_threshold": float(_f32_be(buf, ft_common_abs + 0x494)),
        # ftColl_80079AB0 percent-term overrides (used when fp+0x2225 bit0 is set; see the
        # non-WSK else-branch in GALE01 asm):
        # - refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
        #   - 0x80079B80: lwz r0, 0x6d8(p_ftCommonData)
        #   - 0x80079B88: lwz r0, 0x6d4(p_ftCommonData)
        #
        # Extract as signed ints (the asm uses the signed-int -> float conversion sequence).
        "ftcoll_percent_base_x6d4": int(_i32_be(buf, ft_common_abs + 0x6D4)),
        "ftcoll_percent_base_x6d8": int(_i32_be(buf, ft_common_abs + 0x6D8)),
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
