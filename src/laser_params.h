#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MSL_LASER_MAX_SHOOT_FRAMES = 8 };
enum { MSL_LASER_MAX_HITBOX_OFFS_X = 16 };

typedef struct MslLaserParams {
  uint8_t loaded;

  // Item kinds (Slippi `item.type` / GALE01 ItemKind).
  uint16_t shot_itkind;
  uint16_t gun_itkind;

  // Spawn attachment (Fighter_Part id domain, compatible with anim_pose_get_matrix part_id).
  uint16_t spawn_bone_part_id;
  float spawn_off_xyz[3];

  // Fighter blaster attrs (ftFox_DatAttrs): used to compute initial projectile velocity.
  float blaster_angle;
  float blaster_speed;

  // SpecialN loop submotion ids (Slippi `animation_index` lower 16 bits).
  //
  // Decomp source-of-truth (GALE01):
  // - refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion (Start/Loop/End)
  // Note: these ids are game-code enums (not DAT attrs); they are stored in lasers.bin for
  // table-driven gameplay and are documented as decomp-backed constants.
  uint16_t ground_start_msid;
  uint16_t ground_loop_msid;
  uint16_t ground_end_msid;
  uint16_t air_start_msid;
  uint16_t air_loop_msid;
  uint16_t air_end_msid;

  // Frames (integer) in the loop scripts where movescript sets cmd_var[2] nonzero (shoot trigger).
  uint8_t shoot_frame_count_ground;
  uint8_t shoot_frame_count_air;
  uint16_t shoot_frames_ground[MSL_LASER_MAX_SHOOT_FRAMES];
  uint16_t shoot_frames_air[MSL_LASER_MAX_SHOOT_FRAMES];

  // Laser lifetime (frames) and hitbox params (article state script).
  uint16_t lifetime_frames;
  float damage;
  float size;  // radius
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  int8_t shield_damage;

  uint8_t hitbox_offsets_x_count;
  float hitbox_offsets_x[MSL_LASER_MAX_HITBOX_OFFS_X];
} MslLaserParams;

// IMPORTANT: laser_params_init() may do IO/allocations; call only during batch init.
int laser_params_init(void);

const MslLaserParams* laser_params_get(uint8_t char_id);

#ifdef __cplusplus
}  // extern "C"
#endif
