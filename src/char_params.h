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

  float jump_h_initial_velocity;
  float jump_v_initial_velocity;
  float hop_v_initial_velocity;
  float ground_to_air_jump_momentum_multiplier;
  float jump_h_max_velocity;

  // Air physics
  float grav;
  float terminal_vel;
  float fast_fall_velocity;
  float air_max_horizontal_velocity;
  float air_drift_stick_mul;
  float aerial_drift_base;
  float air_drift_max;
  float aerial_friction;
  float air_jump_v_multiplier;
  float air_jump_h_multiplier;

  // Shield (refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::initial_shield_size)
  float initial_shield_size;

  // Model scaling (refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::model_scaling).
  // Source of truth: ISO-extracted `data/characters/*.json` `model_scaling`.
  float model_scaling;

  // Blaster shot scale cap (Fox/Falco laser article special attr `scale`).
  //
  // Decomp: refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
  // Source of truth: ISO-extracted `data/characters/*.json` `laser_scale_max`.
  float laser_scale_max;

  // Grab/capture attachment anchor bone (bone index / fp->parts[] index domain; GALE01).
  //
  // Decomp:
  // - refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8 stores
  //   fp->mv.co.capturedamage.x18 (fp+0x2358) by indexing `fp->parts[]` with `fp->ft_data->x8->x11`.
  // - refs/melee/src/melee/ft/types.h::ftData.x8->x11 (u8 bone index)
  //
  // Source of truth: ISO-extracted `data/characters/*.json` `grab_capture_anchor_part_id`.
  uint16_t grab_capture_anchor_part_id;
  uint16_t _pad_u16_grab_0;

  // Fox/Falco side special (Illusion/Phantasm) end-state velocities + friction.
  //
  // Source of truth: ISO-extracted `data/characters/*.json` keys:
  // - illusion_ground_end_vel_x / illusion_ground_friction
  // - illusion_air_end_vel_x / illusion_air_friction
  //
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs x34/x38/x3C/x40)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialSEnd_Enter,ftFx_SpecialSEnd_Phys}
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSEnd_Enter,ftFx_SpecialAirSEnd_Phys}
  float illusion_ground_end_vel_x;
  float illusion_ground_friction;
  float illusion_air_end_vel_x;
  float illusion_air_friction;

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
  // - firefox_hold_air_friction
  // - firefox_hold_air_fall_accel
  // - firefox_direction_stick_range_min
  // - firefox_launch_speed
  // - firefox_facing_stick_range_min
  //
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs x54/x5C/x60/x64/x74/x88)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //     ftFx_SpecialHiHoldAir_Phys,ftFx_SpecialAirHi_Enter
  //   }
  uint8_t firefox_hold_gravity_delay_frames;  // ftFox_DatAttrs.x54
  uint8_t _pad_u8_firefox_hold_0[3];
  float firefox_hold_air_friction;          // ftFox_DatAttrs.x5C
  float firefox_hold_air_fall_accel;        // ftFox_DatAttrs.x60
  float firefox_direction_stick_range_min;  // ftFox_DatAttrs.x64
  float firefox_launch_speed;               // ftFox_DatAttrs.x74
  float firefox_facing_stick_range_min;     // ftFox_DatAttrs.x88

  // Cliff / ledge (ftCo_Cliff*).
  //
  // Source of truth: ISO-extracted `data/characters/*.json` (ftData_x44_t and co attrs).
  // Extractor: tools/extraction/extract_character_attrs.py
  float ledge_jump_horizontal_velocity;  // fp->co_attrs.ledge_jump_horizontal_velocity
  float ledge_jump_vertical_velocity;    // fp->co_attrs.ledge_jump_vertical_velocity
  // ECB side-point Y offset (added to midpoint between ECB bottom/top).
  // Decomp: ftData_x44_t.unkC is added when building desired_ecb.{left,right}.y in mpColl_LoadECB_JObj.
  // refs/melee/src/melee/ft/types.h::ftData_x44_t
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  float ecb_side_y_offset;
  float ledge_snap_x;       // ftData_x44_t.x10 (used as transNPos.z snap distance)
  float ledge_snap_y;       // ftData_x44_t.x14 (used as transNPos.y snap distance)
  float ledge_snap_height;  // ftData_x44_t.x18 (catch height threshold)

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
} MslCharParams;

int char_params_init(void);
const MslCharParams* msl_char_params(uint8_t char_id);
