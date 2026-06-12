#include "item_article_params.h"
#include "data_dir.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  MSLITAR1_VERSION = 2,
  MSLITAR1_CHAR_DOMAIN_GALE01_FIGHTER_KIND = 1,
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
};

typedef struct ItemArticleTable {
  MslItemArticleParams by_char[256];
  uint8_t have_char[256];
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

static uint8_t sim_char_from_gale01_fighter_kind(uint16_t gale01_kind) {
  // MSLITAR1 stores GALE01 internal FighterKind ids:
  // - Fox   = 2
  // - Falco = 20
  // Runtime state uses Slippi/sim external character ids:
  // - Fox   = 1
  // - Falco = 22
  // agent_docs/DATA_CONTRACT.md::MSLITAR1
  switch (gale01_kind) {
    case 2:
      return 1u;
    case 20:
      return 22u;
    default:
      return 0u;
  }
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
    default:
      break;
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
    const uint16_t gale01_char = read_u16_le(p + 0);
    const uint8_t char_domain = p[2];
    const uint8_t value_type = p[3];
    const uint16_t field_id = read_u16_le(p + 4);
    const uint32_t u32_value = read_u32_le(p + 8);
    const float f32_value = read_f32_le(p + 12);
    p += record_bytes;
    if (char_domain != (uint8_t)MSLITAR1_CHAR_DOMAIN_GALE01_FIGHTER_KIND) {
      alloc_free(buf);
      return -1;
    }
    const uint8_t sim_char = sim_char_from_gale01_fighter_kind(gale01_char);
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
      g_tbl.by_char[22].side_special_illusion_itkind == 0u) {
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
