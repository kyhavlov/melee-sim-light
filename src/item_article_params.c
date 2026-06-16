#include "item_article_params.h"
#include "data_dir.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  MSLITAR1_VERSION = 7,
  MSLITAR1_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID = 1,
  MSLITAR1_VALUE_U16 = 1,
  MSLITAR1_VALUE_U32 = 2,
  MSLITAR1_VALUE_F32 = 3,
  MSLITAR1_FIELD_BLASTER_SHOT_ITKIND = 1,
  MSLITAR1_FIELD_BLASTER_GUN_ITKIND = 2,
  MSLITAR1_FIELD_LASER_SPAWN_JOINT_PART_ID = 3,
  MSLITAR1_FIELD_LASER_LIFETIME_FRAMES = 4,
  MSLITAR1_FIELD_LASER_DAMAGE = 5,
  MSLITAR1_FIELD_LASER_SIZE = 6,
  MSLITAR1_FIELD_ILLUSION_ITEM_STATE0_DAMAGE = 9,
  MSLITAR1_FIELD_ILLUSION_ITEM_STATE1_DAMAGE = 10,
  MSLITAR1_FIELD_SHIELD_BOUNCE_EXTRA_DEGREES = 11,
  MSLITAR1_FIELD_SIDE_SPECIAL_ILLUSION_ITKIND = 12,
  MSLITAR1_FIELD_NEEDLE_THROW_ITKIND = 13,
  MSLITAR1_FIELD_NEEDLE_HELD_ITKIND = 14,
  MSLITAR1_FIELD_NEEDLE_LIFETIME_FRAMES = 15,
  MSLITAR1_FIELD_NEEDLE_BOUNCE_LIFETIME_FRAMES = 16,
  MSLITAR1_FIELD_NEEDLE_LAUNCH_SPEED = 17,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_COUNT = 18,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_BONE_ID = 19,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_X = 20,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_Y = 21,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_Z = 22,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_X = 23,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_Y = 24,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_Z = 25,
  MSLITAR1_FIELD_NEEDLE_HURTBOX_SCALE = 26,
  MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE = 27,
  MSLITAR1_FIELD_SHEIK_CHAIN_ITKIND = 28,
  MSLITAR1_FIELD_SHEIK_CHAIN_LIFETIME_FRAMES = 29,
  MSLITAR1_FIELD_SHEIK_VANISH_ITKIND = 30,
  MSLITAR1_FIELD_SHEIK_VANISH_LIFETIME_FRAMES = 31,
  MSLITAR1_FIELD_NEEDLE_HITBOX_COUNT = 32,
  MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE_0 = 33,
  MSLITAR1_FIELD_NEEDLE_HITBOX_SIZE_0 = 37,
  MSLITAR1_FIELD_NEEDLE_HITBOX_X_OFFSET_0 = 41,
  MSLITAR1_FIELD_NEEDLE_HITBOX_Y_OFFSET_0 = 45,
  MSLITAR1_FIELD_NEEDLE_HITBOX_Z_OFFSET_0 = 49,
  MSLITAR1_FIELD_NEEDLE_HITBOX_ANGLE_0 = 53,
  MSLITAR1_FIELD_NEEDLE_HITBOX_KBG_0 = 57,
  MSLITAR1_FIELD_NEEDLE_HITBOX_WSK_0 = 61,
  MSLITAR1_FIELD_NEEDLE_HITBOX_BKB_0 = 65,
  MSLITAR1_FIELD_NEEDLE_HITBOX_ELEMENT_0 = 69,
  MSLITAR1_FIELD_NEEDLE_HITBOX_SHIELD_DAMAGE_0 = 73,
  MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_0 = 77,
  MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_3 = 80,
  MSLITAR1_FIELD_VANISH_HITBOX_COUNT = 81,
  MSLITAR1_FIELD_VANISH_HITBOX_DAMAGE = 82,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE = 83,
  MSLITAR1_FIELD_VANISH_HITBOX_X_OFFSET = 84,
  MSLITAR1_FIELD_VANISH_HITBOX_Y_OFFSET = 85,
  MSLITAR1_FIELD_VANISH_HITBOX_Z_OFFSET = 86,
  MSLITAR1_FIELD_VANISH_HITBOX_ANGLE = 87,
  MSLITAR1_FIELD_VANISH_HITBOX_KBG = 88,
  MSLITAR1_FIELD_VANISH_HITBOX_WSK = 89,
  MSLITAR1_FIELD_VANISH_HITBOX_BKB = 90,
  MSLITAR1_FIELD_VANISH_HITBOX_ELEMENT = 91,
  MSLITAR1_FIELD_VANISH_HITBOX_SHIELD_DAMAGE = 92,
  MSLITAR1_FIELD_VANISH_HITBOX_FLAGS = 93,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_COUNT = 94,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_FRAME_0 = 95,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_VALUE_0 = 96,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_FRAME_1 = 97,
  MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_VALUE_1 = 98,
  MSLITAR1_FIELD_VANISH_HITBOX_REMOVE_FRAME = 99,
  MSLITAR1_FIELD_NEEDLE_FIRST = MSLITAR1_FIELD_NEEDLE_THROW_ITKIND,
  MSLITAR1_FIELD_SHEIK_SPECIAL_FIRST = MSLITAR1_FIELD_SHEIK_CHAIN_ITKIND,
  MSLITAR1_FIELD_SHEIK_SPECIAL_LAST = MSLITAR1_FIELD_SHEIK_VANISH_LIFETIME_FRAMES,
  MSLITAR1_FIELD_SHEIK_SPECIAL_REQUIRED_MASK =
      (1u << (MSLITAR1_FIELD_SHEIK_SPECIAL_LAST - MSLITAR1_FIELD_SHEIK_SPECIAL_FIRST + 1u)) - 1u,
  MSLITAR1_FIELD_VANISH_HITBOX_FIRST = MSLITAR1_FIELD_VANISH_HITBOX_COUNT,
  MSLITAR1_FIELD_VANISH_HITBOX_LAST = MSLITAR1_FIELD_VANISH_HITBOX_REMOVE_FRAME,
  MSLITAR1_FIELD_VANISH_HITBOX_REQUIRED_MASK =
      (1u << (MSLITAR1_FIELD_VANISH_HITBOX_LAST - MSLITAR1_FIELD_VANISH_HITBOX_FIRST + 1u)) - 1u,
};

typedef struct ItemArticleTable {
  MslItemArticleParams by_char[256];
  uint8_t have_char[256];
  uint64_t sheik_needle_fields_seen;
  uint32_t sheik_special_article_fields_seen;
  uint32_t sheik_vanish_hitbox_fields_seen;
  uint8_t loaded;
} ItemArticleTable;

static ItemArticleTable g_tbl;

static uint16_t read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint32_t read_u32_le(const uint8_t* p) {
  uint32_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static float read_f32_le(const uint8_t* p) {
  float v = 0.0f;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint8_t sim_char_from_slippi_external_id(uint16_t external_id) {
  // MSLITAR1 stores Slippi/CSS external character ids:
  // - Fox   = 2
  // - Sheik = 19
  // - Falco = 20
  // Runtime state uses Slippi/sim external character ids:
  // - Fox   = 1
  // - Sheik = 7
  // - Falco = 22
  // agent_docs/DATA_CONTRACT.md::MSLITAR1
  switch (external_id) {
    case 2:
      return 1u;
    case 19:
      return 7u;
    case 20:
      return 22u;
    default:
      return 0u;
  }
}

static uint8_t needle_required_bit_for_field(uint16_t field_id) {
  if (field_id >= MSLITAR1_FIELD_NEEDLE_THROW_ITKIND &&
      field_id <= MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE) {
    return (uint8_t)(field_id - MSLITAR1_FIELD_NEEDLE_THROW_ITKIND);
  }
  if (field_id >= MSLITAR1_FIELD_NEEDLE_HITBOX_COUNT &&
      field_id <= MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_3) {
    return (uint8_t)(15u + (uint8_t)(field_id - MSLITAR1_FIELD_NEEDLE_HITBOX_COUNT));
  }
  return 0xFFu;
}

static uint64_t needle_required_mask(void) {
  const uint8_t count = (uint8_t)(15u + (uint8_t)(MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_3 -
                                                  MSLITAR1_FIELD_NEEDLE_HITBOX_COUNT + 1u));
  uint64_t mask = 0ull;
  for (uint8_t i = 0u; i < count; i++) {
    mask = (mask << 1u) | 1ull;
  }
  return mask;
}

static uint8_t needle_hitbox_field_index(uint16_t field_id, uint16_t base_field_id) {
  return (field_id >= base_field_id && field_id < (uint16_t)(base_field_id + 4u))
             ? (uint8_t)(field_id - base_field_id)
             : 0xFFu;
}

static int apply_record(uint16_t char_id, uint8_t value_type, uint16_t field_id, uint32_t u32_value,
                        float f32_value) {
  MslItemArticleParams* rec = &g_tbl.by_char[char_id];
  rec->loaded = 1u;
  g_tbl.have_char[char_id] = 1u;
  switch (field_id) {
    case MSLITAR1_FIELD_BLASTER_SHOT_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->blaster_shot_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_BLASTER_GUN_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->blaster_gun_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_LASER_SPAWN_JOINT_PART_ID:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->laser_spawn_joint_part_id = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_LASER_LIFETIME_FRAMES:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->laser_lifetime_frames = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_LASER_DAMAGE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->laser_damage = f32_value;
      break;
    case MSLITAR1_FIELD_LASER_SIZE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->laser_size = f32_value;
      break;
    case MSLITAR1_FIELD_ILLUSION_ITEM_STATE0_DAMAGE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->illusion_item_state0_damage = f32_value;
      break;
    case MSLITAR1_FIELD_ILLUSION_ITEM_STATE1_DAMAGE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->illusion_item_state1_damage = f32_value;
      break;
    case MSLITAR1_FIELD_SHIELD_BOUNCE_EXTRA_DEGREES:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->shield_bounce_extra_degrees = f32_value;
      break;
    case MSLITAR1_FIELD_SIDE_SPECIAL_ILLUSION_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->side_special_illusion_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_THROW_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->needle_throw_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HELD_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->needle_held_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_LIFETIME_FRAMES:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->needle_lifetime_frames = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_BOUNCE_LIFETIME_FRAMES:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->needle_bounce_lifetime_frames = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_LAUNCH_SPEED:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_launch_speed = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_COUNT:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->needle_hurtbox_count = (uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_BONE_ID:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->needle_hurtbox_bone_id = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_X:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_a_offset[0] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_Y:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_a_offset[1] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_A_OFFSET_Z:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_a_offset[2] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_X:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_b_offset[0] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_Y:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_b_offset[1] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_B_OFFSET_Z:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_b_offset[2] = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HURTBOX_SCALE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hurtbox_scale = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->needle_hitbox_damage = f32_value;
      break;
    case MSLITAR1_FIELD_NEEDLE_HITBOX_COUNT:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->needle_hitbox_count = (uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_SHEIK_CHAIN_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->sheik_chain_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_SHEIK_CHAIN_LIFETIME_FRAMES:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->sheik_chain_lifetime_frames = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_SHEIK_VANISH_ITKIND:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->sheik_vanish_itkind = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_SHEIK_VANISH_LIFETIME_FRAMES:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->sheik_vanish_lifetime_frames = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_COUNT:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_count = (uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_DAMAGE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->vanish_hitbox_damage = f32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->vanish_hitbox_size = f32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_X_OFFSET:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->vanish_hitbox_x_offset = f32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_Y_OFFSET:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->vanish_hitbox_y_offset = f32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_Z_OFFSET:
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      rec->vanish_hitbox_z_offset = f32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_ANGLE:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_angle = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_KBG:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_kbg = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_WSK:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_wsk = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_BKB:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_bkb = (uint16_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_ELEMENT:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_element = (uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_SHIELD_DAMAGE:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_shield_damage = (int8_t)(uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_FLAGS:
      if (value_type != MSLITAR1_VALUE_U32) return -1;
      rec->vanish_hitbox_flags = u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_COUNT:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_size_keyframe_count = (uint8_t)u32_value;
      break;
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_FRAME_0:
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_FRAME_1: {
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      const uint8_t idx =
          (uint8_t)((field_id - MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_FRAME_0) / 2u);
      if (idx >= (uint8_t)MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES) return -1;
      rec->vanish_hitbox_size_keyframe_frame[idx] = (uint16_t)u32_value;
      break;
    }
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_VALUE_0:
    case MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_VALUE_1: {
      if (value_type != MSLITAR1_VALUE_F32) return -1;
      const uint8_t idx =
          (uint8_t)((field_id - MSLITAR1_FIELD_VANISH_HITBOX_SIZE_KEYFRAME_VALUE_0) / 2u);
      if (idx >= (uint8_t)MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES) return -1;
      rec->vanish_hitbox_size_keyframe_value[idx] = f32_value;
      break;
    }
    case MSLITAR1_FIELD_VANISH_HITBOX_REMOVE_FRAME:
      if (value_type != MSLITAR1_VALUE_U16) return -1;
      rec->vanish_hitbox_remove_frame = (uint16_t)u32_value;
      break;
    default:
      if (field_id >= MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE_0 &&
          field_id <= MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_3) {
        uint8_t idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_DAMAGE_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_F32) return -1;
          rec->needle_hitbox_damage_by_id[idx] = f32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_SIZE_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_F32) return -1;
          rec->needle_hitbox_size[idx] = f32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_X_OFFSET_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_F32) return -1;
          rec->needle_hitbox_x_offset[idx] = f32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_Y_OFFSET_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_F32) return -1;
          rec->needle_hitbox_y_offset[idx] = f32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_Z_OFFSET_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_F32) return -1;
          rec->needle_hitbox_z_offset[idx] = f32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_ANGLE_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_angle[idx] = (uint16_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_KBG_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_kbg[idx] = (uint16_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_WSK_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_wsk[idx] = (uint16_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_BKB_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_bkb[idx] = (uint16_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_ELEMENT_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_element[idx] = (uint8_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_SHIELD_DAMAGE_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U16) return -1;
          rec->needle_hitbox_shield_damage[idx] = (int8_t)(uint8_t)u32_value;
          break;
        }
        idx = needle_hitbox_field_index(field_id, MSLITAR1_FIELD_NEEDLE_HITBOX_FLAGS_0);
        if (idx != 0xFFu) {
          if (value_type != MSLITAR1_VALUE_U32) return -1;
          rec->needle_hitbox_flags[idx] = u32_value;
          break;
        }
      }
      break;
  }
  const uint8_t needle_bit = needle_required_bit_for_field(field_id);
  if (char_id == 7u && needle_bit != 0xFFu) {
    g_tbl.sheik_needle_fields_seen |= (uint64_t)1ull << (uint64_t)needle_bit;
  }
  if (char_id == 7u && field_id >= MSLITAR1_FIELD_SHEIK_SPECIAL_FIRST &&
      field_id <= MSLITAR1_FIELD_SHEIK_SPECIAL_LAST) {
    g_tbl.sheik_special_article_fields_seen |=
        (uint32_t)1u << (uint32_t)(field_id - MSLITAR1_FIELD_SHEIK_SPECIAL_FIRST);
  }
  if (char_id == 7u && field_id >= MSLITAR1_FIELD_VANISH_HITBOX_FIRST &&
      field_id <= MSLITAR1_FIELD_VANISH_HITBOX_LAST) {
    g_tbl.sheik_vanish_hitbox_fields_seen |=
        (uint32_t)1u << (uint32_t)(field_id - MSLITAR1_FIELD_VANISH_HITBOX_FIRST);
  }
  return 0;
}

int item_article_params_init(void) {
  if (g_tbl.loaded) {
    return 0;
  }

  const char* data_dir = msl_data_dir();

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/items/articles/fox_falco.bin", data_dir);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  uint8_t* buf = (uint8_t*)alloc_malloc((size_t)sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    alloc_free(buf);
    return -1;
  }

  const uint8_t* p = buf;
  const uint8_t* end = buf + (size_t)sz;
  if ((size_t)(end - p) < 16u || memcmp(p, "MSLITAR1", 8) != 0) {
    alloc_free(buf);
    return -1;
  }
  p += 8;
  const uint32_t version = read_u32_le(p);
  p += 4;
  if (version != MSLITAR1_VERSION) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t record_count = read_u32_le(p);
  p += 4;

  const size_t record_bytes = 24u;
  if ((size_t)(end - p) != (size_t)record_count * record_bytes) {
    alloc_free(buf);
    return -1;
  }

  for (uint32_t ri = 0; ri < record_count; ri++) {
    const uint16_t external_char = read_u16_le(p + 0);
    const uint8_t char_domain = p[2];
    const uint8_t value_type = p[3];
    const uint16_t field_id = read_u16_le(p + 4);
    const uint32_t u32_value = read_u32_le(p + 8);
    const float f32_value = read_f32_le(p + 12);
    p += record_bytes;
    if (char_domain != (uint8_t)MSLITAR1_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID) {
      alloc_free(buf);
      return -1;
    }
    const uint8_t sim_char = sim_char_from_slippi_external_id(external_char);
    if (sim_char == 0u) {
      continue;
    }
    if (apply_record((uint16_t)sim_char, value_type, field_id, u32_value, f32_value) != 0) {
      alloc_free(buf);
      return -1;
    }
  }

  alloc_free(buf);
  if (!g_tbl.have_char[1] || !g_tbl.have_char[22] || g_tbl.by_char[1].blaster_shot_itkind == 0u ||
      g_tbl.by_char[22].blaster_shot_itkind == 0u ||
      g_tbl.by_char[1].side_special_illusion_itkind == 0u ||
      g_tbl.by_char[22].side_special_illusion_itkind == 0u || !g_tbl.have_char[7] ||
      g_tbl.sheik_needle_fields_seen != needle_required_mask() ||
      g_tbl.sheik_special_article_fields_seen !=
          (uint32_t)MSLITAR1_FIELD_SHEIK_SPECIAL_REQUIRED_MASK ||
      g_tbl.sheik_vanish_hitbox_fields_seen !=
          (uint32_t)MSLITAR1_FIELD_VANISH_HITBOX_REQUIRED_MASK ||
      g_tbl.by_char[7].needle_throw_itkind == 0u || g_tbl.by_char[7].needle_hurtbox_count == 0u ||
      g_tbl.by_char[7].needle_hitbox_count == 0u ||
      g_tbl.by_char[7].needle_hitbox_count > (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES ||
      g_tbl.by_char[7].needle_bounce_lifetime_frames == 0u ||
      !(g_tbl.by_char[7].needle_hurtbox_scale > 0.0f) ||
      !(g_tbl.by_char[7].needle_hitbox_damage > 0.0f) ||
      !(g_tbl.by_char[7].needle_hitbox_size[0] > 0.0f) ||
      g_tbl.by_char[7].sheik_chain_itkind == 0u ||
      g_tbl.by_char[7].sheik_chain_lifetime_frames == 0u ||
      g_tbl.by_char[7].sheik_vanish_itkind == 0u ||
      g_tbl.by_char[7].sheik_vanish_lifetime_frames == 0u ||
      g_tbl.by_char[7].vanish_hitbox_count == 0u ||
      !(g_tbl.by_char[7].vanish_hitbox_damage > 0.0f) ||
      !(g_tbl.by_char[7].vanish_hitbox_size > 0.0f) ||
      g_tbl.by_char[7].vanish_hitbox_size_keyframe_count >
          (uint8_t)MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES ||
      g_tbl.by_char[7].vanish_hitbox_size_keyframe_count < 2u ||
      !(g_tbl.by_char[7].vanish_hitbox_size_keyframe_value[0] > 0.0f) ||
      !(g_tbl.by_char[7].vanish_hitbox_size_keyframe_value[1] > 0.0f) ||
      g_tbl.by_char[7].vanish_hitbox_remove_frame == 0u) {
    return -1;
  }
  g_tbl.loaded = 1u;
  return 0;
}

const MslItemArticleParams* item_article_params_get(uint8_t sim_char_id) {
  if (!g_tbl.loaded || !g_tbl.have_char[sim_char_id]) {
    return NULL;
  }
  return &g_tbl.by_char[sim_char_id];
}

const MslItemArticleParams* item_article_params_for_laser_item_type(uint16_t type) {
  const MslItemArticleParams* fox = item_article_params_get(1u);
  if (fox != NULL && fox->blaster_shot_itkind == type) {
    return fox;
  }
  const MslItemArticleParams* falco = item_article_params_get(22u);
  if (falco != NULL && falco->blaster_shot_itkind == type) {
    return falco;
  }
  return NULL;
}

const MslItemArticleParams* item_article_params_for_illusion_item_type(uint16_t type) {
  const MslItemArticleParams* fox = item_article_params_get(1u);
  if (fox != NULL && fox->side_special_illusion_itkind == type) {
    return fox;
  }
  const MslItemArticleParams* falco = item_article_params_get(22u);
  if (falco != NULL && falco->side_special_illusion_itkind == type) {
    return falco;
  }
  return NULL;
}

uint8_t item_article_params_is_illusion_item_type(uint16_t type) {
  return item_article_params_for_illusion_item_type(type) != NULL ? 1u : 0u;
}

const MslItemArticleParams* item_article_params_for_sheik_needle_throw_item_type(uint16_t type) {
  const MslItemArticleParams* sheik = item_article_params_get(7u);
  if (sheik != NULL && sheik->needle_throw_itkind == type) {
    return sheik;
  }
  return NULL;
}
