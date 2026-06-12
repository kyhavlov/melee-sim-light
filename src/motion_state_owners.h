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

// Binary artifact schema version expected by this runtime.
uint32_t motion_state_owners_format_version(void);

enum {
  MSL_MOTION_FLAG_KEEP_FASTFALL = 1u << 0,
  // refs/melee/src/melee/ft/forward.h::Ft_MF_SkipHit
  // Fighter_ChangeMotionState skips ftColl_8007AFF8 when this bit is set, preserving x914
  // HitCapsule state/victim lists across the motion transition.
  MSL_MOTION_FLAG_SKIP_HIT = 1u << 3,
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
  MSL_MS_CLASS_FT80081D0C_AIR_COLL = 1u << 19,
  MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL = 1u << 20,
  MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL = 1u << 21,
  MSL_MS_CLASS_FT80083090_PLATFORM_PASS_COLL = 1u << 22,
  MSL_MS_CLASS_FT800827A0_EDGE_SNAP_COLL = 1u << 23,
  MSL_MS_CLASS_DAMAGE_AIR = 1u << 24,
  MSL_MS_CLASS_DAMAGE_GROUND = 1u << 25,
  MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS = 1u << 26,
  MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION = 1u << 27,
  MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD = 1u << 28,
  MSL_MS_CLASS_ESCAPE_AIR_COLL = 1u << 29,
  MSL_MS_CLASS_FX_SPECIALS_GROUND_B108_COLL = 1u << 30,
  MSL_MS_CLASS_FT80082B1C_BASIC_LANDING_COLL = 1u << 31,
};

enum {
  MSL_MS_CLASS2_COMMON_GROUNDED_COLL = 1u << 0,
  MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL = 1u << 1,
  MSL_MS_CLASS2_COMMON_AIRBORNE_COLL = 1u << 2,
  MSL_MS_CLASS2_COMMON_GROUNDED_B2DC_COLL = 1u << 3,
  MSL_MS_CLASS2_COMMON_GROUNDED_B4B0_COLL = 1u << 4,
};

enum {
  MSL_MS_CLASS3_PHASE4_ATTACK_AIR_COLL = 1u << 0,
  MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL = 1u << 1,
  MSL_MS_CLASS3_PHASE4_DAMAGE_COMMON_COLL = 1u << 2,
  MSL_MS_CLASS3_PHASE4_DAMAGE_FLY_COLL = 1u << 3,
  MSL_MS_CLASS3_PHASE4_DAMAGE_FALL_COLL = 1u << 4,
  // Char-special PHYS owner families for generic-engine consumers: per-(char, action)
  // MotionState callback identity in place of `is_spacie && action_id == MSL_ACT_FX_*`
  // predicate gates. A new character's same-numbered actions carry its own callbacks and
  // never set these bits.
  MSL_MS_CLASS3_FX_SPECIALHI_HOLD_AIR_PHYS = 1u << 5,
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
uint32_t msl_motion_state_class_bits(uint8_t char_id, uint16_t action_id);
uint8_t msl_motion_state_class_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit);
uint32_t msl_motion_state_class2_bits(uint8_t char_id, uint16_t action_id);
uint8_t msl_motion_state_class2_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit);
uint32_t msl_motion_state_class3_bits(uint8_t char_id, uint16_t action_id);
uint8_t msl_motion_state_class3_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit);

// Common helpers return true only when all currently supported Fox/Falco owner tables agree. This
// is for migrating common action-family predicates without introducing character-id routing.
uint8_t msl_motion_state_common_class_has(uint16_t action_id, uint32_t class_bit);
uint8_t msl_motion_state_common_class2_has(uint16_t action_id, uint32_t class_bit);
uint8_t msl_motion_state_common_class3_has(uint16_t action_id, uint32_t class_bit);
