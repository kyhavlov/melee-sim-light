#pragma once

#include <stdint.h>

// Init-time loader for a small subset of per-character attributes needed for locomotion/physics.
//
// Source of truth: ISO-extracted character attributes under `data/characters/*.json`.
// Extractor: `tools/extraction/extract_character_attrs.py` (decomp-first).
//
// IMPORTANT: char_params_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslCharParams {
  // Combat (subset).
  // Decomp: ftCo_Damage knockback uses `fp->co_attrs.weight` (ftCo_Damage / ftColl).
  // Source of truth: ISO-extracted `data/characters/*.json` `weight`.
  float weight;
  // Throw anim-speed mask (per-throw bitfield).
  //
  // Decomp: ftCo_800DD4B0 skips the weight-based anim-speed formula when
  // `fp->ft_data->x0->weight_independent_throws_mask & (1 << throw_index)` is set.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
  // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x180)
  //
  // Source of truth: ISO-extracted `data/characters/*.json` `weight_independent_throws_mask`.
  uint8_t weight_independent_throws_mask;
  uint8_t _pad_u8_weight_independent_throws_mask[3];

  // Match start entry height scalar (ft_0C31.c::ftCo_800C6408).
  // Source of truth: ISO-extracted `data/characters/*.json` `trophy_scale`.
  float trophy_scale;

  // Ground locomotion
  float walk_init_vel;
  float walk_accel;
  float walk_max_vel;
  float slow_walk_max;
  float mid_walk_point;
  float fast_walk_min;
  float gr_friction;
  float ground_max_horizontal_velocity;

  float dash_initial_velocity;
  float dash_run_acceleration_a;
  float dash_run_acceleration_b;
  float dash_run_terminal_velocity;

  float run_animation_scaling;
  // Hidden RunBrake lifetime timer (`mv.co.runbrake.frames`).
  //
  // Decomp: ftCo_RunBrake_Enter copies `fp->co_attrs.max_run_brake_frames`, and
  // ftCo_RunBrake_Anim decrements it alongside the AObj remaining-frame gate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
  //   ftCo_RunBrake_Enter,ftCo_RunBrake_Anim}
  float max_run_brake_frames;

  // Turn / jump
  uint8_t turn_frames;
  uint8_t jump_startup_frames;
  uint8_t max_jumps;
  uint8_t landing_lag_frames;
  uint8_t landing_airn_lag_frames;
  uint8_t landing_airf_lag_frames;
  uint8_t landing_airb_lag_frames;
  uint8_t landing_airhi_lag_frames;
  uint8_t landing_airlw_lag_frames;
  uint8_t _pad_u8_0[1];

  // Rebound anim-speed numerator from ftCo_80099D9C / ftCo_80099E44.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
  float rebound_anim_numerator_frames;  // fp->co_attrs.x9C
  // Rapid-jab mash threshold.
  //
  // Decomp: ftCo_Attack_800D6A50 increments fp->x1A54 while A is pressed/released, then enters
  // Attack100Start when `x1A54 >= fp->co_attrs.rapid_jab_window` and x2218_b2 is set.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
  // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x98)
  //
  // Source of truth: ISO-extracted `data/characters/*.json` `rapid_jab_window`.
  uint8_t rapid_jab_window;
  uint8_t _pad_u8_rapid_jab_window[3];

  // Wait idle sub-animation roulette (`ftCo_8008A7A8` / `getAnimID`).
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys
  // `wait_anim_choice_msids` and `wait_anim_choice_weights`, copied from the character's
  // WaitStruct table.
  // refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
  uint8_t wait_anim_choice_count;
  uint8_t _pad_u8_wait_anim_choice[3];
  uint16_t wait_anim_choice_msids[4];
  uint16_t wait_anim_choice_weights[4];

  float jump_h_initial_velocity;
  float jump_v_initial_velocity;
  float hop_v_initial_velocity;
  float ground_to_air_jump_momentum_multiplier;
  float jump_h_max_velocity;
  // Grounded Side-B pre-entry gr_vel damping (decomp: ftCo_SpecialS.c::doEnter, co_attrs.xB8).
  float side_special_ground_entry_vel_mul;

  // Air physics
  float grav;
  float terminal_vel;
  float fast_fall_velocity;
  float air_max_horizontal_velocity;
  float air_drift_stick_mul;
  float aerial_drift_base;
  float air_drift_max;
  float aerial_friction;
  // Fighter camera subject (`fp->x890_cameraBox`) metadata.
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - camera_zoom_target_bone_part_id, camera_zoom_target_offset, camera_box_radius
  // Decomp:
  // - refs/melee/src/melee/ft/ftcamera.c::ftCamera_UpdateCameraBox
  // - refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
  uint16_t camera_zoom_target_bone_part_id;
  uint16_t _pad_u16_camera_0;
  float camera_zoom_target_offset_x;
  float camera_zoom_target_offset_y;
  float camera_zoom_target_offset_z;
  float camera_box_radius;
  float air_jump_v_multiplier;
  float air_jump_h_multiplier;

  // Shield (refs/melee/src/melee/ft/types.h::ftCo_DatAttrs)
  float initial_shield_size;
  float shield_break_initial_velocity;

  // Model scaling (refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::model_scaling).
  // Source of truth: ISO-extracted `data/characters/*.json` `model_scaling`.
  float model_scaling;

  // Grounded player-overlap pushbox extents (`fp->x2C4`).
  //
  // Decomp:
  // - ftCommon_8007DD7C / ftCommon_8007E0E4 use `fp->x2C4.{x,y}` to test grounded fighter overlap
  //   before accumulating `xF8_playerNudgeVel`.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys `pushbox_x` / `pushbox_y`.
  float pushbox_x;
  float pushbox_y;

  // Blaster shot scale cap (Fox/Falco laser article special attr `scale`).
  //
  // Decomp: refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
  // Source of truth: ISO-extracted `data/characters/*.json` `laser_scale_max`.
  float laser_scale_max;
  // Fox/Falco blaster shot spawn joint index (fp->parts[] domain).
  //
  // Decomp:
  // - SpecialN spawn uses ftParts_GetBoneIndex(fp, FtPart_RThumbNb), then lb_8000B1CC on that joint.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  //   refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
  //
  // Source of truth:
  // - ISO-extracted `data/characters/*.json` key `laser_spawn_joint_part_id`,
  //   derived from `_iso/PlCo.dat` ftPartsTable[ftkind].part_to_joint[FtPart_RThumbNb].
  uint16_t laser_spawn_joint_part_id;
  uint16_t _pad_u16_laser_spawn_0;

  // Grab/capture attachment anchor bone (bone index / fp->parts[] index domain; GALE01).
  //
  // Decomp:
  // - refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8 stores
  //   fp->mv.co.capturedamage.x18 (fp+0x2358) by indexing `fp->parts[]` with `fp->ft_data->x8->x11`.
  // - refs/melee/src/melee/ft/types.h::ftData.x8->x11 (u8 bone index)
  //
  // Source of truth: ISO-extracted `data/characters/*.json` `grab_capture_anchor_part_id`.
  uint16_t grab_capture_anchor_part_id;
  // Explicit overlay for the ftCo_800DDDE4 -> mpColl_800471F8 release-local floor publication
  // subset. Bit order matches throw action order: ThrowF, ThrowB, ThrowHi, ThrowLw.
  //
  // This is probe-backed rather than inferred from the anchor part id: Marth ThrowF/ThrowLw publish
  // the floor-hit substep root, Marth ThrowB and Fox/Falco controls do not.
  // Source/probe: refs/Ishiiruka engine-dump-v12-probes ftCo_800DDDE4 probe on IPW/PFZ/FSP rows.
  uint8_t throw_release_mpcoll_floor_publication_mask;
  uint8_t _pad_u8_grab_0[3];

  // Explicit overlay for source callsites that enter common FallSpecial through
  // ftCo_80096900 with xC=0. Bits are keyed by MslMsFxSpecialKind: 1u << fx_kind.
  //
  // This is a callsite owner, not a destination FallSpecial property. Marth SpecialHi/SpecialAirHi
  // use ftMs_SpecialHi_80138884 -> ftCo_80096900(..., arg1=0, ...); Fox/Falco SpecialHi fall
  // callsites pass arg1=1.
  // Source: refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_80138884
  // Source controls: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c
  uint32_t fallspecial_xc0_source_fx_kind_mask;

  // Seed-only CommonFall CollData ECB reconstruction mask.
  // Bit order: Fall, FallAerial, FallSpecial. This is a replay seed/provenance overlay for
  // hidden CollData ECB bottom state, not a free-running mpColl branch.
  uint8_t common_fall_blended_ecb_seed_mask;
  uint8_t _pad_u8_common_fall_ecb_seed[3];

  // Fox/Falco side special (Illusion/Phantasm) start/end-state velocities + friction.
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - illusion_ground_vel_x
  // - illusion_gravity_delay_start_frames / illusion_air_friction_start / illusion_fall_accel_start
  // - illusion_ground_end_vel_x / illusion_ground_friction
  // - illusion_air_end_vel_x / illusion_air_friction
  // - illusion_landing_lag_frames / illusion_gravity_delay_end_frames / illusion_fall_accel_end
  //
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/types.h
  //   (ftFox_DatAttrs x24/x28/x2C/x30/x34/x38/x3C/x40/x44/x48/x50)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //     ftFx_SpecialSStart_Enter,ftFx_SpecialAirSStart_Enter,
  //     ftFx_SpecialSStart_Phys,ftFx_SpecialAirSStart_Phys,
  //     ftFx_SpecialSEnd_Enter,ftFx_SpecialSEnd_Phys
  //   }
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSEnd_Enter,ftFx_SpecialAirSEnd_Phys}
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
  uint8_t illusion_gravity_delay_start_frames;
  uint8_t _pad_u8_illusion_start_0[3];
  float illusion_air_friction_start;
  float illusion_fall_accel_start;
  float illusion_ground_vel_x;
  float illusion_ground_end_vel_x;
  float illusion_ground_friction;
  float illusion_air_end_vel_x;
  float illusion_air_friction;
  uint8_t illusion_landing_lag_frames;
  uint8_t illusion_gravity_delay_end_frames;
  uint8_t _pad_u8_illusion_0[2];
  float illusion_fall_accel_end;

  // Fox/Falco side special ghost item (Illusion/Phantasm article) collision attrs.
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - illusion_item_hitbox_size
  // - illusion_item_lifetime_state01_frames / illusion_item_lifetime_state2_frames
  // - illusion_item_state{0,1}_{damage,shield_damage,angle,kbg,wsk,bkb,element,hitbox_y_offset}
  //
  // Decomp anchors:
  // - refs/melee/src/melee/it/items/itfoxillusion.c
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c
  float illusion_item_hitbox_size;
  uint8_t illusion_item_lifetime_state01_frames;
  uint8_t illusion_item_lifetime_state2_frames;
  int8_t illusion_item_state0_shield_damage;
  int8_t illusion_item_state1_shield_damage;
  float illusion_item_state0_damage;
  float illusion_item_state1_damage;
  float illusion_item_state0_hitbox_y_offset;
  float illusion_item_state1_hitbox_y_offset;
  uint16_t illusion_item_state0_angle;
  uint16_t illusion_item_state0_kbg;
  uint16_t illusion_item_state0_wsk;
  uint16_t illusion_item_state0_bkb;
  uint16_t illusion_item_state1_angle;
  uint16_t illusion_item_state1_kbg;
  uint16_t illusion_item_state1_wsk;
  uint16_t illusion_item_state1_bkb;
  uint8_t illusion_item_state0_element;
  uint8_t illusion_item_state1_element;
  uint8_t _pad_u8_illusion_item_0[2];

  // Fox/Falco up special HoldAir/Launch (Firefox/Firebird) attrs.
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - firefox_hold_gravity_delay_frames
  // - firefox_hold_vel_x
  // - firefox_hold_air_friction
  // - firefox_hold_air_fall_accel
  // - firefox_direction_stick_range_min
  // - firefox_launch_duration_frames
  // - firefox_bound_delay_frames
  // - firefox_launch_reverse_accel_start_frames
  // - firefox_launch_speed
  // - firefox_launch_reverse_accel
  // - firefox_ground_momentum_end
  // - firefox_bound_vel_x
  // - firefox_facing_stick_range_min
  // - firefox_freefall_mobility
  // - firefox_landing_lag_frames
  // - firefox_bound_angle_degrees
  //
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/types.h
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //     ftFx_SpecialHiHoldAir_Phys,ftFx_SpecialAirHi_Enter,ftFx_SpecialHi_Anim,
  //     ftFx_SpecialAirHi_Anim,ftFx_SpecialAirHi_Phys,ftFx_SpecialHiLanding_Phys,
  //     ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Enter
  //   }
  uint8_t firefox_hold_gravity_delay_frames;  // ftFox_DatAttrs.x54
  uint8_t _pad_u8_firefox_hold_0[3];
  float firefox_hold_vel_x;                           // ftFox_DatAttrs.x58
  float firefox_hold_air_friction;                    // ftFox_DatAttrs.x5C
  float firefox_hold_air_fall_accel;                  // ftFox_DatAttrs.x60
  float firefox_direction_stick_range_min;            // ftFox_DatAttrs.x64
  uint8_t firefox_launch_duration_frames;             // ftFox_DatAttrs.x68
  uint8_t firefox_bound_delay_frames;                 // ftFox_DatAttrs.x6C
  uint8_t firefox_launch_reverse_accel_start_frames;  // ftFox_DatAttrs.x70
  uint8_t firefox_landing_lag_frames;                 // ftFox_DatAttrs.x90
  float firefox_launch_speed;                         // ftFox_DatAttrs.x74
  float firefox_launch_reverse_accel;                 // ftFox_DatAttrs.x78
  float firefox_ground_momentum_end;                  // ftFox_DatAttrs.x7C
  float firefox_bound_vel_x;                          // ftFox_DatAttrs.x84
  float firefox_facing_stick_range_min;               // ftFox_DatAttrs.x88
  float firefox_freefall_mobility;                    // ftFox_DatAttrs.x8C
  float firefox_bound_angle_degrees;                  // ftFox_DatAttrs.x94

  // Cliff / ledge (ftCo_Cliff*).
  //
  // Source of truth: ISO-extracted `data/characters/*.json` (ftData_x44_t and co attrs).
  // Extractor: tools/extraction/extract_character_attrs.py
  float ledge_jump_horizontal_velocity;    // fp->co_attrs.ledge_jump_horizontal_velocity
  float ledge_jump_vertical_velocity;      // fp->co_attrs.ledge_jump_vertical_velocity
  float passivewall_vel_x;                 // fp->co_attrs.passivewall_vel_x
  float wall_jump_horizontal_velocity;     // fp->co_attrs.wall_jump_horizontal_velocity
  float wall_jump_vertical_velocity;       // fp->co_attrs.wall_jump_vertical_velocity
  float walljump_setup_x_delta_threshold;  // fp->co_attrs.x148
  // ECB side-point Y offset (added to midpoint between ECB bottom/top).
  // Decomp: ftData_x44_t.unkC is added when building desired_ecb.{left,right}.y in mpColl_LoadECB_JObj.
  // refs/melee/src/melee/ft/types.h::ftData_x44_t
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  float ecb_side_y_offset;
  float ledge_snap_x;       // ftData_x44_t.x10 (used as transNPos.z snap distance)
  float ledge_snap_y;       // ftData_x44_t.x14 (used as transNPos.y snap distance)
  float ledge_snap_height;  // ftData_x44_t.x18 (catch height threshold)
  // ECB source joints consumed by mpColl_LoadECB_JObj.
  //
  // Decomp:
  // - refs/melee/src/melee/ft/types.h::ftData_x44_t
  // - refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  //
  // Source of truth: ISO-extracted `data/characters/*.json` key `ecb_joints`.
  uint8_t ecb_joint_count;
  uint8_t _pad_u8_ecb_joints[3];
  uint16_t ecb_joints[6];

  // Fox/Falco down special (Reflector / Shine).
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - reflector_release_lag_frames / reflector_turn_frames / reflector_gravity_delay_frames
  // - reflector_momentum_preserve_x / reflector_fall_accel
  // - reflector_bone_id / reflector_offset / reflector_size / reflector_damage_mul / reflector_speed_mul
  uint8_t reflector_release_lag_frames;    // ftFox_DatAttrs.x98 (rounded frames)
  uint8_t reflector_turn_frames;           // ftFox_DatAttrs.x9C (rounded frames)
  uint8_t reflector_gravity_delay_frames;  // ftFox_DatAttrs.xA4 (clamped)
  uint8_t _pad_u8_reflector_0[1];
  float reflector_momentum_preserve_x;  // ftFox_DatAttrs.xA8
  float reflector_fall_accel;           // ftFox_DatAttrs.xAC
  uint16_t reflector_bone_part_id;      // ReflectDesc.x0_bone_id (FtPart id domain)
  uint16_t _pad_u16_reflector_0;
  int32_t reflector_max_damage;  // ReflectDesc.x4_max_damage
  float reflector_offset_x;      // ReflectDesc.x8_offset.x
  float reflector_offset_y;      // ReflectDesc.x8_offset.y
  float reflector_offset_z;      // ReflectDesc.x8_offset.z
  float reflector_size;          // ReflectDesc.x14_size
  float reflector_damage_mul;    // ReflectDesc.x18_damage_mul
  float reflector_speed_mul;     // ReflectDesc.x1C_speed_mul
  uint8_t reflector_behavior;    // ReflectDesc.x20_behavior
  uint8_t _pad_u8_reflector_1[3];
  // Marth-family sword special attributes (MarsAttributes ext block; mechanic-position names
  // shared with clones). Zero for characters without the mars_sword ext-attr layout.
  // Source: ISO-extracted data/characters/<ch>.json special* keys
  // refs/melee/src/melee/ft/chara/ftMars/types.h::MarsAttributes
  int32_t specialn_charge_max_seconds;
  int32_t specialn_release_damage_base;
  int32_t specialn_release_damage_per_second;
  float specialn_entry_vel_divisor;
  float specialn_start_friction;
  float specials_air_entry_vel_x_divisor;
  float specials_air_friction;
  float specials_air_entry_vel_y;
  float specials_fall_accel;
  float specials_terminal_vel;
  float specialhi_freefall_mobility_mul;
  float specialhi_landing_lag_frames;
  float specialhi_breverse_stick_threshold;
  float specialhi_angle_stick_threshold;
  float specialhi_angle_max_degrees;
  float specialhi_air_entry_vel_x_mul;
  float specialhi_launch_decay_mul;
  float specialhi_fall_accel;
  float specialhi_terminal_vel;
  float speciallw_air_entry_vel_x_divisor;
  float speciallw_air_friction;
  float speciallw_fall_accel;
  float speciallw_terminal_vel;
  float speciallw_counter_damage_mul;
  float speciallw_counter_shield_strength;
  int32_t speciallw_counter_desc_bone;
  float speciallw_counter_desc_offset_x;
  float speciallw_counter_desc_offset_y;
  float speciallw_counter_desc_offset_z;
  float speciallw_counter_desc_size;
  // Sheik special attributes (ftSeakAttributes ext block). Zero for characters without the
  // seak_special ext-attr layout.
  // Source: ISO-extracted data/characters/sheik.json sheik_* keys
  // refs/melee/src/melee/ft/chara/ftSeak/types.h::ftSeakAttributes
  float sheik_needle_ground_spawn_x_offset;
  float sheik_needle_ground_spawn_y_offset;
  float sheik_needle_air_spawn_x_offset;
  float sheik_needle_air_spawn_y_offset;
  float sheik_needle_air_end_fallspecial_lag_frames;
  float sheik_chain_release_min_frames;
  float sheik_chain_extension_frames;
  float sheik_chain_spawn_frame;
  float sheik_chain_start_end_frame;
  float sheik_chain_retract_frame;
  float sheik_chain_destroy_frame;
  float sheik_vanish_air_entry_vel_y;
  float sheik_vanish_start_air_gravity;
  float sheik_vanish_start_air_terminal_vel;
  int32_t sheik_vanish_travel_frames;
  float sheik_vanish_ground_contact_min_frames;
  float sheik_vanish_stick_mag_min;
  float sheik_vanish_travel_speed_stick_mul;
  float sheik_vanish_travel_speed_base;
  float sheik_vanish_air_end_drift_mul;
  int32_t sheik_vanish_wall_bounce_degrees;
  float sheik_vanish_end_vel_mul;
  float sheik_vanish_fallspecial_mobility_mul;
  float sheik_vanish_landing_lag_frames;
  float sheik_transform_vel_x_divisor;
  float sheik_transform_vel_y_divisor;
  float sheik_transform_air_gravity;
  float sheik_transform_air_terminal_vel;
  float sheik_transform_finish_start_frame;
} MslCharParams;

int char_params_init(void);
extern const MslCharParams* msl_char_params_ptr_by_char[256];

// Runtime fast path after char_params_init(). Before init, all slots are NULL.
static inline const MslCharParams* msl_char_params_fast(uint8_t char_id) {
  return msl_char_params_ptr_by_char[char_id];
}

const MslCharParams* msl_char_params(uint8_t char_id);
