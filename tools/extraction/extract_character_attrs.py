from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive
from melee_sim.iso import extract_file, find_files, list_files
from tools.extraction.extract_fighter_moves import _parse_subaction_events


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _i32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=True)


def _s16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=True)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def _extract_fox_falco_laser(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Fox/Falco blaster shot (laser) params from fighter item article data (decomp-first).

    Source of truth:
    - ftData.x48_items[0] is the blaster shot Article* (registered via `it_8026B3F8` in ftFx_Init/ftFc_Init).
    - Article.x4_specialAttributes is FoxLaserAttr (lifetime/scale/etc).
    - Article.xC_itemStates[0].xC_script defines the hitbox (damage/size/kb params).
    """
    out: dict = {}

    # ftData.x48_items is a void** list of Article* pointers (ft/types.h +0x48).
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return out

    # items[0] => blaster shot.
    shot_article_abs = arc.ptr32(items_abs + 0x00)
    if shot_article_abs == arc.data_base:
        return out

    # struct Article { ItemAttr* x0_common_attr; void* x4_specialAttributes; ... ItemStateArray* xC_itemStates; ... }
    special_abs = arc.ptr32(shot_article_abs + 0x04)
    states_abs = arc.ptr32(shot_article_abs + 0x0C)
    if special_abs == arc.data_base or states_abs == arc.data_base:
        return out

    lifetime = float(_f32_be(pl_buf, special_abs + 0x00))
    out["laser_lifetime_frames"] = int(max(0, round(lifetime)))
    # FoxLaserAttr.scale (it/items/itfoxlaser.c): max visual stretch for the beam.
    out["laser_scale_max"] = float(_f32_be(pl_buf, special_abs + 0x04))

    def _extract_state_hitbox(state_index: int) -> tuple[dict, list[dict]] | None:
        # ItemStateDesc stride is 0x10 (it/types.h). xC_script is at offset 0x0C.
        # refs/melee/src/melee/it/types.h::ItemStateDesc
        script_abs = arc.ptr32(states_abs + 0x0C + int(state_index) * 0x10)
        if script_abs == arc.data_base:
            return None
        events = _parse_subaction_events(arc, script_abs, max_frames=8, max_steps_per_frame=500)
        hitboxes: list[dict] = []
        for ev in events:
            if ev.kind == "create_hitbox":
                hb = ev.data.get("hitbox")
                if isinstance(hb, dict):
                    hitboxes.append(hb)
        hb0 = hitboxes[0] if hitboxes else None
        if not isinstance(hb0, dict):
            return None
        return hb0, hitboxes

    # State 0 is spawned by it_8029C6A4 (msid=0).
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6A4
    st0 = _extract_state_hitbox(0)
    if st0 is None:
        return out
    hb0, hbs0 = st0

    out["laser_damage"] = float(hb0.get("damage", 0.0))
    out["laser_size"] = float(hb0.get("size", 0.0))
    out["laser_angle"] = int(hb0.get("angle", 0))
    out["laser_kbg"] = int(hb0.get("kbg", 0))
    out["laser_wsk"] = int(hb0.get("wsk", 0))
    out["laser_bkb"] = int(hb0.get("bkb", 0))
    out["laser_element"] = int(hb0.get("element", 0))
    out["laser_shield_damage"] = int(hb0.get("shield_damage", 0))

    # The blaster shot article uses multiple hitboxes spaced along the beam. Preserve the X offsets
    # so the simulator can reproduce early hits without inflating radius.
    #
    # (Decomp: Pl*.dat article state script; parsed via `_parse_subaction_events`.)
    x_offs0: list[float] = []
    for hb in hbs0:
        try:
            x_offs0.append(float(hb.get("x_offset", 0.0)))
        except Exception:
            pass
    out["laser_hitbox_offsets_x"] = x_offs0

    # State 1 is spawned by it_8029C6CC (msid=1).
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    st1 = _extract_state_hitbox(1)
    if st1 is not None:
        hb1, hbs1 = st1
        out["laser_state1_damage"] = float(hb1.get("damage", 0.0))
        out["laser_state1_size"] = float(hb1.get("size", 0.0))
        out["laser_state1_angle"] = int(hb1.get("angle", 0))
        out["laser_state1_kbg"] = int(hb1.get("kbg", 0))
        out["laser_state1_wsk"] = int(hb1.get("wsk", 0))
        out["laser_state1_bkb"] = int(hb1.get("bkb", 0))
        out["laser_state1_element"] = int(hb1.get("element", 0))
        out["laser_state1_shield_damage"] = int(hb1.get("shield_damage", 0))

        x_offs1: list[float] = []
        for hb in hbs1:
            try:
                x_offs1.append(float(hb.get("x_offset", 0.0)))
            except Exception:
                pass
        out["laser_state1_hitbox_offsets_x"] = x_offs1
    return out


def _extract_ftco_dattrs(pl_dat: Path, *, ftdata_symbol: str, extract_fox_blaster: bool = False) -> dict:
    buf = pl_dat.read_bytes()
    arc = parse_hsd_archive(buf)

    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise ValueError(f"{pl_dat.name}: missing public symbol {ftdata_symbol!r}")

    # Grab/capture victim attachment anchor (decomp-first).
    #
    # Decomp: in fn_800D9CE8, the engine sets `mv.co.capturedamage.x18` by indexing `fp->parts[]`
    # using the u8 stored at `fp->ft_data->x8->x11`.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
    #
    # Store the raw u8 as a pose bone index (index into `fp->parts[]` / SSANIM01 part id space).
    grab_capture_anchor_part_id = 0
    x8_abs = arc.ptr32(ftdata_abs + 0x08)
    if 0 <= x8_abs + 0x12 <= len(buf):
        grab_capture_anchor_part_id = int(buf[x8_abs + 0x11])

    # struct ftData { ftCo_DatAttrs* x0; ... }
    attrs_abs = arc.ptr32(ftdata_abs + 0x00)
    # struct ftData { ... Vec2* x50; } (ft/types.h +0x50) => fp->x2C4 (pushbox center offset + radius).
    pushbox_abs = arc.ptr32(ftdata_abs + 0x50)

    def f(off: int) -> float:
        return float(_f32_be(buf, attrs_abs + off))

    def i(off: int) -> int:
        return int(_i32_be(buf, attrs_abs + off))

    jump_startup = f(0x38)
    # In practice these are integer-valued floats (e.g. 3.0, 4.0).
    jump_startup_frames = int(round(jump_startup))
    # ftCo_DatAttrs.frames_to_change_direction_on_standing_turn (ft/types.h +0x84)
    turn_frames = int(round(f(0x84)))
    # ftCo_DatAttrs.normal_landing_lag (ft/types.h +0xE4)
    landing_lag_frames = int(round(f(0xE4)))
    landing_airn_lag_frames = int(round(f(0xE8)))
    landing_airf_lag_frames = int(round(f(0xEC)))
    landing_airb_lag_frames = int(round(f(0xF0)))
    landing_airhi_lag_frames = int(round(f(0xF4)))
    landing_airlw_lag_frames = int(round(f(0xF8)))

    x44_abs = arc.ptr32(ftdata_abs + 0x44)
    out = {
        "grab_capture_anchor_part_id": grab_capture_anchor_part_id,
        "walk_init_vel": f(0x00),
        "walk_accel": f(0x04),
        "walk_max_vel": f(0x08),
        "gr_friction": f(0x18),
        "dash_initial_velocity": f(0x1C),
        "dash_run_acceleration_a": f(0x20),
        "dash_run_acceleration_b": f(0x24),
        "dash_run_terminal_velocity": f(0x28),
        # Decomp: ftCo_DatAttrs.run_animation_scaling (ft/types.h +0x2C), used by `ftCo_Run_Anim`:
        #   anim_rate = ABS(vel) / fp->co_attrs.run_animation_scaling
        "run_animation_scaling": f(0x2C),
        "ground_max_horizontal_velocity": f(0x34),
        "jump_startup_frames": int(max(1, jump_startup_frames)),
        "jump_h_initial_velocity": f(0x3C),
        "jump_v_initial_velocity": f(0x40),
        "hop_v_initial_velocity": f(0x4C),
        "ground_to_air_jump_momentum_multiplier": f(0x44),
        "jump_h_max_velocity": f(0x48),
        # Note: in Melee, max_jumps counts total jumps including the grounded jump.
        "max_jumps": int(i(0x58)),
        "grav": f(0x5C),
        "terminal_vel": f(0x60),
        "air_drift_stick_mul": f(0x64),
        "aerial_drift_base": f(0x68),
        "air_drift_max": f(0x6C),
        "aerial_friction": f(0x70),
        "fast_fall_velocity": f(0x74),
        "air_max_horizontal_velocity": f(0x78),
        "air_jump_v_multiplier": f(0x50),
        "air_jump_h_multiplier": f(0x54),
        "weight": f(0x88),
        "model_scaling": f(0x8C),
        "initial_shield_size": f(0x90),
        "trophy_scale": f(0x110),
        # Decomp: fp->x2C4 = *fp->ft_data->x50 (ftchangeparam.c: ftCo_800D0FA0 / ftCo_800D105C).
        "pushbox_x": float(_f32_be(buf, pushbox_abs + 0x00)) if pushbox_abs != arc.data_base else 0.0,
        "pushbox_y": float(_f32_be(buf, pushbox_abs + 0x04)) if pushbox_abs != arc.data_base else 0.0,
        "turn_frames": int(max(1, turn_frames)),
        "landing_lag_frames": int(max(1, landing_lag_frames)),
        "landing_airn_lag_frames": int(max(1, landing_airn_lag_frames)),
        "landing_airf_lag_frames": int(max(1, landing_airf_lag_frames)),
        "landing_airb_lag_frames": int(max(1, landing_airb_lag_frames)),
        "landing_airhi_lag_frames": int(max(1, landing_airhi_lag_frames)),
        "landing_airlw_lag_frames": int(max(1, landing_airlw_lag_frames)),

        "ledge_jump_horizontal_velocity": f(0xA8),
        "ledge_jump_vertical_velocity": f(0xAC),

        # Ledge snap parameters: ftData_x44_t (ft/types.h)
        # struct ftData { ... ftData_x44_t* x44; }
        "ledge_snap_x": float(_f32_be(buf, x44_abs + 0x10)),
        "ledge_snap_y": float(_f32_be(buf, x44_abs + 0x14)),
        "ledge_snap_height": float(_f32_be(buf, x44_abs + 0x18)),
        # ECB (environment collision box) joints: ftData_x44_t.
        #
        # These are indices into `fp->parts[]` (the "bones" array in decomp).
        # Decomp: ft_80081B38 -> mpColl_SetECBSource_JObj(..., bones[temp_r29->unk*].joint, ..., temp_r29->unkC * scale_y)
        "ecb_joints": [
            int(_s16_be(buf, x44_abs + 0x00)),
            int(_s16_be(buf, x44_abs + 0x02)),
            int(_s16_be(buf, x44_abs + 0x04)),
            int(_s16_be(buf, x44_abs + 0x06)),
            int(_s16_be(buf, x44_abs + 0x08)),
            int(_s16_be(buf, x44_abs + 0x0A)),
        ],
        "ecb_side_y_offset": float(_f32_be(buf, x44_abs + 0x0C)),
    }
    if extract_fox_blaster:
        # struct ftData { ... void* ext_attr; } (ft/types.h +0x4)
        # Fox/Falco ext attrs: struct ftFox_DatAttrs (ft/chara/ftFox/types.h)
        ext_abs = arc.ptr32(ftdata_abs + 0x04)
        # Fox/Falco side special (Illusion/Phantasm) ground-velocity scaling.
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        #   `x28_FOX_ILLUSION_GROUND_VEL_X`
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSStart_Enter
        #   `fp->gr_vel /= da->x28_FOX_ILLUSION_GROUND_VEL_X;`
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
        #   divides horizontal self velocity similarly.
        out["illusion_ground_vel_x"] = float(_f32_be(buf, ext_abs + 0x28))
        # End-state velocity + friction parameters (used on main->end transition and in End Phys).
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        #   x34/x38/x3C/x40 fields
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialSEnd_Enter,ftFx_SpecialSEnd_Phys}
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSEnd_Enter,ftFx_SpecialAirSEnd_Phys}
        out["illusion_ground_end_vel_x"] = float(_f32_be(buf, ext_abs + 0x34))
        out["illusion_ground_friction"] = float(_f32_be(buf, ext_abs + 0x38))
        out["illusion_air_end_vel_x"] = float(_f32_be(buf, ext_abs + 0x3C))
        out["illusion_air_friction"] = float(_f32_be(buf, ext_abs + 0x40))
        # Fox/Falco up special HoldAir (Firefox/Firebird charge) physics attrs.
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs x54/x5C/x60)
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiHoldAir_Phys
        out["firefox_hold_gravity_delay_frames"] = int(max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x54)))))))
        out["firefox_hold_air_friction"] = float(_f32_be(buf, ext_abs + 0x5C))
        out["firefox_hold_air_fall_accel"] = float(_f32_be(buf, ext_abs + 0x60))
        out["blaster_angle"] = float(_f32_be(buf, ext_abs + 0x10))
        out["blaster_vel"] = float(_f32_be(buf, ext_abs + 0x14))
        out["blaster_shot_itkind"] = int(_u32_be(buf, ext_abs + 0x1C))
        out["blaster_gun_itkind"] = int(_u32_be(buf, ext_abs + 0x20))
        # Fox/Falco reflector (shine) attrs: ftFox_DatAttrs down-special section.
        #
        # Decomp: refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        # - x98_FOX_REFLECTOR_RELEASE_LAG (float)
        # - x9C_FOX_REFLECTOR_TURN_FRAMES (float)
        # - gravity delay is stored as an s32 but treated as a small frame count.
        try:
            out["reflector_release_lag_frames"] = int(
                max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x98))))))
            )
        except Exception:
            pass
        try:
            out["reflector_turn_frames"] = int(
                max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x9C))))))
            )
        except Exception:
            pass
        try:
            out["reflector_gravity_delay_frames"] = int(max(0, min(255, _i32_be(buf, ext_abs + 0xA4))))
        except Exception:
            pass
        out["reflector_momentum_preserve_x"] = float(_f32_be(buf, ext_abs + 0xA8))
        out["reflector_fall_accel"] = float(_f32_be(buf, ext_abs + 0xAC))

        # ReflectDesc (lb/types.h): bone id + offset + size + reflect multipliers.
        refl_abs = ext_abs + 0xB0
        out["reflector_bone_id"] = int(_u32_be(buf, refl_abs + 0x00))
        out["reflector_max_damage"] = int(_i32_be(buf, refl_abs + 0x04))
        out["reflector_offset"] = [
            float(_f32_be(buf, refl_abs + 0x08)),
            float(_f32_be(buf, refl_abs + 0x0C)),
            float(_f32_be(buf, refl_abs + 0x10)),
        ]
        out["reflector_size"] = float(_f32_be(buf, refl_abs + 0x14))
        out["reflector_damage_mul"] = float(_f32_be(buf, refl_abs + 0x18))
        out["reflector_speed_mul"] = float(_f32_be(buf, refl_abs + 0x1C))
        out["reflector_behavior"] = int(buf[refl_abs + 0x20]) if (refl_abs + 0x20) < len(buf) else 0
        out.update(_extract_fox_falco_laser(buf, arc, ftdata_abs=ftdata_abs))
    return out


def _stable_update(existing: dict, extracted: dict) -> dict:
    out: dict = {}
    # Deprecated keys from previous extractor iterations; drop them on rewrite so downstream
    # consumers don't accidentally treat them as part of the contract.
    drop_keys = {
        "ecb_bone_indices",
    }
    ordered_keys = [
        "walk_init_vel",
        "walk_accel",
        "walk_max_vel",
        "gr_friction",
        "ground_max_horizontal_velocity",
        "turn_frames",
        "jump_startup_frames",
        "jump_h_initial_velocity",
        "jump_v_initial_velocity",
        "hop_v_initial_velocity",
        "ground_to_air_jump_momentum_multiplier",
        "jump_h_max_velocity",
        "max_jumps",
        "grav",
        "terminal_vel",
        "fast_fall_velocity",
        "air_drift_stick_mul",
        "aerial_drift_base",
        "air_drift_max",
        "aerial_friction",
        "air_max_horizontal_velocity",
        "air_jump_v_multiplier",
        "air_jump_h_multiplier",
        "dash_initial_velocity",
        "dash_run_acceleration_a",
        "dash_run_acceleration_b",
        "dash_run_terminal_velocity",
        "run_animation_scaling",
        "weight",
        "model_scaling",
        "initial_shield_size",
        "trophy_scale",
        "pushbox_x",
        "pushbox_y",
        "grab_capture_anchor_part_id",
        "illusion_ground_vel_x",
        "illusion_ground_end_vel_x",
        "illusion_ground_friction",
        "illusion_air_end_vel_x",
        "illusion_air_friction",
        "firefox_hold_gravity_delay_frames",
        "firefox_hold_air_friction",
        "firefox_hold_air_fall_accel",
        "blaster_angle",
        "blaster_vel",
        "blaster_shot_itkind",
        "blaster_gun_itkind",
        "reflector_gravity_delay_frames",
        "reflector_release_lag_frames",
        "reflector_turn_frames",
        "reflector_momentum_preserve_x",
        "reflector_fall_accel",
        "reflector_bone_id",
        "reflector_max_damage",
        "reflector_offset",
        "reflector_size",
        "reflector_damage_mul",
        "reflector_speed_mul",
        "reflector_behavior",
        "laser_lifetime_frames",
        "laser_damage",
        "laser_size",
        "laser_scale_max",
        "laser_hitbox_offsets_x",
        "laser_angle",
        "laser_kbg",
        "laser_wsk",
        "laser_bkb",
        "laser_shield_damage",
        "landing_lag_frames",
        "landing_airn_lag_frames",
        "landing_airf_lag_frames",
        "landing_airb_lag_frames",
        "landing_airhi_lag_frames",
        "landing_airlw_lag_frames",
        "ledge_jump_horizontal_velocity",
        "ledge_jump_vertical_velocity",
        "ecb_joints",
        "ecb_side_y_offset",
        "ledge_snap_x",
        "ledge_snap_y",
        "ledge_snap_height",
    ]
    for k in ordered_keys:
        if k in extracted:
            out[k] = extracted[k]
        elif k in existing:
            out[k] = existing[k]
    for k, v in existing.items():
        if k in drop_keys:
            continue
        if k not in out:
            out[k] = v
    return out


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract per-character ftCo_DatAttrs from Pl*.dat (decomp-first) and update melee_sim character JSONs."
    )
    ap.add_argument("--iso", type=Path, default=None, help="optional path to SSBM.iso (used to extract missing Pl*.dat)")
    ap.add_argument("--pl-dir", type=Path, default=Path("_iso"), help="directory containing extracted Pl*.dat")
    ap.add_argument(
        "--out-dir", type=Path, default=Path("data/characters"), help="directory for character JSON outputs"
    )
    ap.add_argument(
        "--chars",
        type=str,
        default="fox,falco,sheik,peach,marth,puff,falcon",
        help="comma-separated character set to extract",
    )
    args = ap.parse_args()

    mapping = {
        "fox": ("PlFx.dat", "ftDataFox", True),
        "falco": ("PlFc.dat", "ftDataFalco", True),
        "sheik": ("PlSk.dat", "ftDataSeak", False),
        "peach": ("PlPe.dat", "ftDataPeach", False),
        "marth": ("PlMs.dat", "ftDataMars", False),
        "puff": ("PlPr.dat", "ftDataPurin", False),
        "falcon": ("PlCa.dat", "ftDataCaptain", False),
    }
    want = [c.strip() for c in args.chars.split(",") if c.strip()]
    for c in want:
        if c not in mapping:
            raise SystemExit(f"unknown character {c!r} (available: {sorted(mapping)})")
    mapping = {k: mapping[k] for k in want}

    if args.iso is not None:
        files = list_files(args.iso)
        for _, (pl, _, _) in mapping.items():
            dst = args.pl_dir / pl
            if dst.exists():
                continue
            matches = find_files(files, f"*{pl}")
            if not matches:
                raise SystemExit(f"missing {pl!r} in ISO")
            extract_file(args.iso, matches[0], dst)
            print(f"wrote {dst} ({matches[0].size} bytes)")

    args.out_dir.mkdir(parents=True, exist_ok=True)

    for name, (pl_name, sym, blaster) in mapping.items():
        pl_path = args.pl_dir / pl_name
        if not pl_path.exists():
            raise SystemExit(f"missing {pl_path} (pass --iso to extract)")

        extracted = _extract_ftco_dattrs(pl_path, ftdata_symbol=sym, extract_fox_blaster=bool(blaster))
        out_path = args.out_dir / f"{name}.json"
        try:
            existing = json.loads(out_path.read_text())
        except Exception:
            existing = {}
        merged = _stable_update(existing, extracted)
        out_path.write_text(json.dumps(merged, indent=2) + "\n")
        print(f"updated {out_path}")


if __name__ == "__main__":
    main()
