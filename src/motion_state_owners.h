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

enum { MSL_MOTION_STATE_COMMON_ACTION_CAP = 1024 };

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
  MSL_MS_CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA = 1u << 5,
  MSL_MS_CLASS2_WALK_ACTION = 1u << 6,
  MSL_MS_CLASS2_FALL_LIKE_ACTION = 1u << 7,
  MSL_MS_CLASS2_GUARD_STATE = 1u << 8,
  MSL_MS_CLASS2_CATCH_START_FLOOR_LOSS = 1u << 9,
  MSL_MS_CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS = 1u << 10,
  MSL_MS_CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE = 1u << 11,
  MSL_MS_CLASS2_LANDING_ROOT_FLOOR_SNAP = 1u << 12,
  MSL_MS_CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST = 1u << 13,
};

enum {
  MSL_MS_CLASS3_PHASE4_ATTACK_AIR_COLL = 1u << 0,
  MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL = 1u << 1,
  MSL_MS_CLASS3_PHASE4_DAMAGE_COMMON_COLL = 1u << 2,
  MSL_MS_CLASS3_PHASE4_DAMAGE_FLY_COLL = 1u << 3,
  MSL_MS_CLASS3_PHASE4_DAMAGE_FALL_COLL = 1u << 4,
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

// fx_special_kind (MSLMSO01 v19): per-(char, action) identity of the Fox/Falco bespoke
// special MotionState rows, generated 1:1 from each row's ANIM callback symbol by
// tools/extraction/extract_motion_state_owners.py (FX_SPECIAL_KIND_BY_ANIM_CB). This is
// the migration target for `msl_char_id_is_spacie(c) && action_id == MSL_ACT_FX_*`
// predicate gates in generic engine code: ownership comes from extracted MotionState
// callback data, and a new character's same-numbered actions stay MSL_FX_KIND_NONE.
// Values are parity-locked to the extractor table and callback_symbols.json by
// tests/test_motion_state_owners_table.py.
typedef enum MslMsFxSpecialKind {
  MSL_FX_KIND_NONE = 0,
  MSL_FX_KIND_SPECIAL_N_START = 1,
  MSL_FX_KIND_SPECIAL_N_LOOP = 2,
  MSL_FX_KIND_SPECIAL_N_END = 3,
  MSL_FX_KIND_SPECIAL_AIR_N_START = 4,
  MSL_FX_KIND_SPECIAL_AIR_N_LOOP = 5,
  MSL_FX_KIND_SPECIAL_AIR_N_END = 6,
  MSL_FX_KIND_SPECIAL_S_START = 7,
  MSL_FX_KIND_SPECIAL_S = 8,
  MSL_FX_KIND_SPECIAL_S_END = 9,
  MSL_FX_KIND_SPECIAL_AIR_S_START = 10,
  MSL_FX_KIND_SPECIAL_AIR_S = 11,
  MSL_FX_KIND_SPECIAL_AIR_S_END = 12,
  MSL_FX_KIND_SPECIAL_HI_HOLD = 13,
  MSL_FX_KIND_SPECIAL_HI_HOLD_AIR = 14,
  MSL_FX_KIND_SPECIAL_HI = 15,
  MSL_FX_KIND_SPECIAL_AIR_HI = 16,
  MSL_FX_KIND_SPECIAL_HI_LANDING = 17,
  MSL_FX_KIND_SPECIAL_HI_FALL = 18,
  MSL_FX_KIND_SPECIAL_HI_BOUND = 19,
  MSL_FX_KIND_SPECIAL_LW_START = 20,
  MSL_FX_KIND_SPECIAL_LW_LOOP = 21,
  MSL_FX_KIND_SPECIAL_LW_HIT = 22,
  MSL_FX_KIND_SPECIAL_LW_END = 23,
  MSL_FX_KIND_SPECIAL_LW_TURN = 24,
  MSL_FX_KIND_SPECIAL_AIR_LW_START = 25,
  MSL_FX_KIND_SPECIAL_AIR_LW_LOOP = 26,
  MSL_FX_KIND_SPECIAL_AIR_LW_HIT = 27,
  MSL_FX_KIND_SPECIAL_AIR_LW_END = 28,
  MSL_FX_KIND_SPECIAL_AIR_LW_TURN = 29,
  MSL_FX_KIND_COUNT = 30,
} MslMsFxSpecialKind;

uint8_t msl_motion_state_fx_special_kind(uint8_t char_id, uint16_t action_id);
// Reverse identity: the char's action id owning the kind row (0xFFFF when the char has no
// row for the kind). The kind lane is 1:1 per char (pinned by the owners parity tests), so
// special-machine writers can be kind-keyed instead of raw-action-id-keyed.
uint16_t msl_motion_state_action_for_fx_kind(uint8_t char_id, uint8_t fx_kind);
uint8_t msl_motion_state_class3_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit);

extern const uint8_t* msl_motion_state_fx_special_kind_by_char[256] __attribute__((weak));
extern uint16_t msl_motion_state_action_count_by_char[256] __attribute__((weak));

static inline uint8_t msl_motion_state_fx_special_kind_fast(uint8_t char_id, uint16_t action_id) {
  if (msl_motion_state_fx_special_kind_by_char == 0 || msl_motion_state_action_count_by_char == 0) {
    return msl_motion_state_fx_special_kind(char_id, action_id);
  }
  const uint8_t* table = msl_motion_state_fx_special_kind_by_char[char_id];
  if (table == 0 || action_id >= msl_motion_state_action_count_by_char[char_id]) {
    return (uint8_t)MSL_FX_KIND_NONE;
  }
  return table[action_id];
}

// Common helpers return true only when all currently supported owner tables agree. This is for
// migrating common action-family predicates without introducing character-id routing.
uint8_t msl_motion_state_common_class_has(uint16_t action_id, uint32_t class_bit);
uint8_t msl_motion_state_common_class2_has(uint16_t action_id, uint32_t class_bit);
uint8_t msl_motion_state_common_class3_has(uint16_t action_id, uint32_t class_bit);

extern uint32_t msl_motion_state_common_class_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP]
    __attribute__((weak));
extern uint32_t msl_motion_state_common_class2_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP]
    __attribute__((weak));
extern uint32_t msl_motion_state_common_class3_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP]
    __attribute__((weak));

static inline uint8_t msl_motion_state_common_class_has_fast(uint16_t action_id,
                                                             uint32_t class_bit) {
  if (class_bit == 0u || action_id >= (uint16_t)MSL_MOTION_STATE_COMMON_ACTION_CAP) {
    return 0u;
  }
  if (msl_motion_state_common_class_bits_by_action == 0) {
    return msl_motion_state_common_class_has(action_id, class_bit);
  }
  return ((msl_motion_state_common_class_bits_by_action[action_id] & class_bit) != 0u) ? 1u : 0u;
}

static inline uint8_t msl_motion_state_common_class2_has_fast(uint16_t action_id,
                                                              uint32_t class_bit) {
  if (class_bit == 0u || action_id >= (uint16_t)MSL_MOTION_STATE_COMMON_ACTION_CAP) {
    return 0u;
  }
  if (msl_motion_state_common_class2_bits_by_action == 0) {
    return msl_motion_state_common_class2_has(action_id, class_bit);
  }
  return ((msl_motion_state_common_class2_bits_by_action[action_id] & class_bit) != 0u) ? 1u : 0u;
}

static inline uint8_t msl_motion_state_common_class3_has_fast(uint16_t action_id,
                                                              uint32_t class_bit) {
  if (class_bit == 0u || action_id >= (uint16_t)MSL_MOTION_STATE_COMMON_ACTION_CAP) {
    return 0u;
  }
  if (msl_motion_state_common_class3_bits_by_action == 0) {
    return msl_motion_state_common_class3_has(action_id, class_bit);
  }
  return ((msl_motion_state_common_class3_bits_by_action[action_id] & class_bit) != 0u) ? 1u : 0u;
}
