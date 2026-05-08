#pragma once

#include <stdint.h>

// Decomp: Fighter_ChangeMotionState copies MotionState callback pointers and raw flags into the
// live fighter state from the selected motion-state row.
// refs/melee/src/melee/ft/types.h::MotionState
// refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
//
// Artifact:
//   data/motion_state/owners/{fox,falco}.bin (MSLMSO01)
int motion_state_owners_init(void);

enum {
  MSL_MOTION_FLAG_KEEP_FASTFALL = 1u << 0,
};

enum {
  MSL_MS_CLASS_ATTACK_AIR = 1u << 0,
  MSL_MS_CLASS_ATTACK_S3 = 1u << 1,
  MSL_MS_CLASS_ATTACK_S4 = 1u << 2,
  MSL_MS_CLASS_DAMAGE_COMMON = 1u << 3,
  MSL_MS_CLASS_DAMAGE_FLY = 1u << 4,
  MSL_MS_CLASS_LANDING_AIR = 1u << 5,
  MSL_MS_CLASS_COMMON_FALL = 1u << 6,
  MSL_MS_CLASS_SPECIALHI = 1u << 7,
  MSL_MS_CLASS_COMMON_AIR_PHYS = 1u << 8,
  MSL_MS_CLASS_COMMON_AIR_COLL = 1u << 9,
  MSL_MS_CLASS_COMMON_AIR_WALLJUMP_COLL = 1u << 10,
  MSL_MS_CLASS_LANDING_COLL = 1u << 11,
  MSL_MS_CLASS_LANDING_AIR_COLL = 1u << 12,
  MSL_MS_CLASS_DAMAGE_COMMON_COLL = 1u << 13,
  MSL_MS_CLASS_DAMAGE_FLY_COLL = 1u << 14,
  MSL_MS_CLASS_DAMAGE_FALL_COLL = 1u << 15,
  MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL = 1u << 16,
  MSL_MS_CLASS_GROUNDED_ATTACK = 1u << 17,
  MSL_MS_CLASS_GUARDON_FRAME_START_X672_IASA = 1u << 18,
};

uint16_t msl_motion_state_submotion_id(uint8_t char_id, uint16_t action_id);
uint32_t msl_motion_state_x4_flags(uint8_t char_id, uint16_t action_id);
uint32_t msl_motion_state_word(uint8_t char_id, uint16_t action_id);
uint16_t msl_motion_state_anim_cb_id(uint8_t char_id, uint16_t action_id);
uint16_t msl_motion_state_iasa_cb_id(uint8_t char_id, uint16_t action_id);
uint16_t msl_motion_state_phys_cb_id(uint8_t char_id, uint16_t action_id);
uint16_t msl_motion_state_coll_cb_id(uint8_t char_id, uint16_t action_id);
uint16_t msl_motion_state_cam_cb_id(uint8_t char_id, uint16_t action_id);
uint8_t msl_motion_state_has_motion_flag(uint8_t char_id, uint16_t action_id, uint32_t flag_mask);
uint8_t msl_motion_state_class_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit);

// Common helpers return true only when all currently supported Fox/Falco owner tables agree. This
// is for migrating common action-family predicates without introducing character-proxy logic.
uint8_t msl_motion_state_common_class_has(uint16_t action_id, uint32_t class_bit);
