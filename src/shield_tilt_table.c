#include "shield_tilt_table.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "char_registry.h"
#include "data_dir.h"

enum {
  SHIELD_HEADER_BYTES = 20,
  SHIELD_RECORD_BYTES = 40,
  SHIELD_VERSION = 5,
};

static const uint8_t k_magic[8] = {'M', 'S', 'L', 'S', 'H', 'L', 'D', '1'};

typedef struct MslShieldPoseTable {
  uint8_t* buf;
  size_t size;
  uint16_t neutral_frame;
  uint16_t shield_part;
  uint16_t guard_frame_count;
  uint16_t part_count;
  int16_t record_by_part[256];
  uint8_t have;
} MslShieldPoseTable;

static MslShieldPoseTable g_by_char[256];
static uint8_t g_loaded;

static uint16_t read_u16(const uint8_t* p) {
  uint16_t value;
  memcpy(&value, p, sizeof(value));
  return value;
}

static uint32_t read_u32(const uint8_t* p) {
  uint32_t value;
  memcpy(&value, p, sizeof(value));
  return value;
}

static void free_table(MslShieldPoseTable* table) {
  if (table == NULL) {
    return;
  }
  alloc_free(table->buf);
  *table = (MslShieldPoseTable){0};
}

static int load_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
  char path[512];
  const int path_len = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (path_len <= 0 || (size_t)path_len >= sizeof(path)) {
    return -1;
  }
  FILE* file = fopen(path, "rb");
  if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
    if (file != NULL) {
      fclose(file);
    }
    return -1;
  }
  const long size_long = ftell(file);
  if (size_long < SHIELD_HEADER_BYTES || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  const size_t size = (size_t)size_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(size);
  if (buf == NULL || fread(buf, 1, size, file) != size) {
    fclose(file);
    alloc_free(buf);
    return -1;
  }
  fclose(file);
  if (memcmp(buf, k_magic, sizeof(k_magic)) != 0 || read_u32(buf + 8) != SHIELD_VERSION) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t part_count = read_u16(buf + 12);
  if (part_count == 0u ||
      (size_t)SHIELD_HEADER_BYTES + (size_t)part_count * SHIELD_RECORD_BYTES != size) {
    alloc_free(buf);
    return -1;
  }

  MslShieldPoseTable table = {
      .buf = buf,
      .size = size,
      .neutral_frame = read_u16(buf + 14),
      .shield_part = read_u16(buf + 16),
      .guard_frame_count = read_u16(buf + 18),
      .part_count = part_count,
      .have = 1u,
  };
  if (table.guard_frame_count == 0u || table.neutral_frame >= table.guard_frame_count) {
    free_table(&table);
    return -1;
  }
  for (size_t part = 0u; part < 256u; part++) {
    table.record_by_part[part] = -1;
  }
  for (uint16_t record = 0u; record < part_count; record++) {
    const uint8_t* p = buf + SHIELD_HEADER_BYTES + (size_t)record * SHIELD_RECORD_BYTES;
    const uint16_t part = read_u16(p);
    if (part >= 256u || table.record_by_part[part] >= 0) {
      free_table(&table);
      return -1;
    }
    table.record_by_part[part] = (int16_t)record;
  }
  free_table(&g_by_char[char_id]);
  g_by_char[char_id] = table;
  return 0;
}

int shield_tilt_table_init(void) {
  if (g_loaded != 0u) {
    return 0;
  }
  const char* data_dir = msl_data_dir();
  for (int i = 0; i < MSL_CHAR_REGISTRY_COUNT; i++) {
    char rel_path[64];
    (void)snprintf(rel_path, sizeof(rel_path), "shields/%s.bin", MSL_CHAR_REGISTRY[i].name);
    if (load_for_char(data_dir, rel_path, MSL_CHAR_REGISTRY[i].char_id) != 0) {
      for (int j = 0; j < i; j++) {
        free_table(&g_by_char[MSL_CHAR_REGISTRY[j].char_id]);
      }
      return -1;
    }
  }
  g_loaded = 1u;
  return 0;
}

uint16_t msl_shield_guard_neutral_frame(uint8_t char_id) {
  return g_loaded != 0u && g_by_char[char_id].have != 0u ? g_by_char[char_id].neutral_frame : 10u;
}

uint16_t msl_shield_guard_frame_count(uint8_t char_id) {
  return g_loaded != 0u && g_by_char[char_id].have != 0u ? g_by_char[char_id].guard_frame_count
                                                         : 0u;
}

uint16_t msl_shield_part_id(uint8_t char_id) {
  return g_loaded != 0u && g_by_char[char_id].have != 0u ? g_by_char[char_id].shield_part
                                                         : UINT16_MAX;
}

int msl_shield_guard_target_srt(uint8_t char_id, uint16_t part_id, float out_rot[3],
                                float out_pos[3], float out_scl[3]) {
  if (g_loaded == 0u || part_id >= 256u || out_rot == NULL || out_pos == NULL || out_scl == NULL) {
    return -1;
  }
  const MslShieldPoseTable* table = &g_by_char[char_id];
  const int record = table->record_by_part[part_id];
  if (table->have == 0u || record < 0 || (uint16_t)record >= table->part_count) {
    return -1;
  }
  const uint8_t* payload =
      table->buf + SHIELD_HEADER_BYTES + (size_t)record * SHIELD_RECORD_BYTES + 4u;
  memcpy(out_rot, payload, 3u * sizeof(float));
  memcpy(out_pos, payload + 3u * sizeof(float), 3u * sizeof(float));
  memcpy(out_scl, payload + 6u * sizeof(float), 3u * sizeof(float));
  return 0;
}
