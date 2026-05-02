#include "stage_item_params.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  MSLSTIO1_VERSION = 1,
  MSLSTIO1_HEADER_BYTES = 52,
};

static MslYoshiShyguyParams g_yoshi_shyguy;
static uint8_t g_loaded;

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

static int load_yoshi_shyguy(const uint8_t* buf, size_t sz) {
  if (sz < (size_t)MSLSTIO1_HEADER_BYTES || memcmp(buf, "MSLSTIO1", 8) != 0) {
    return -1;
  }
  const uint32_t version = read_u32_le(buf + 8);
  if (version != (uint32_t)MSLSTIO1_VERSION) {
    return -1;
  }
  const uint16_t stage_id = read_u16_le(buf + 12);
  const uint16_t item_kind = read_u16_le(buf + 14);
  const uint16_t vpos_count = read_u16_le(buf + 16);
  const uint16_t speed_count = read_u16_le(buf + 18);
  const uint16_t dyn_y_count = read_u16_le(buf + 20);
  const size_t expected = (size_t)MSLSTIO1_HEADER_BYTES + (size_t)vpos_count * sizeof(float) +
                          (size_t)speed_count * sizeof(float) + (size_t)dyn_y_count * sizeof(float);
  if (sz != expected || vpos_count != (uint16_t)MSL_YOSHI_SHYGUY_VPOS_COUNT ||
      speed_count != (uint16_t)MSL_YOSHI_SHYGUY_SPEED_COUNT ||
      dyn_y_count != (uint16_t)MSL_YOSHI_SHYGUY_DYN_Y_COUNT) {
    return -1;
  }

  memset(&g_yoshi_shyguy, 0, sizeof(g_yoshi_shyguy));
  g_yoshi_shyguy.loaded = 1u;
  g_yoshi_shyguy.stage_id = stage_id;
  g_yoshi_shyguy.item_kind = item_kind;
  g_yoshi_shyguy.timer_min = read_u16_le(buf + 22);
  g_yoshi_shyguy.timer_rand = read_u16_le(buf + 24);
  g_yoshi_shyguy.timer_reset = read_u16_le(buf + 26);
  g_yoshi_shyguy.spawnmany_rarity = read_u16_le(buf + 28);
  g_yoshi_shyguy.spawn_delay_step = read_u16_le(buf + 30);
  g_yoshi_shyguy.fall_accel = read_f32_le(buf + 32);
  g_yoshi_shyguy.spawn_left_x = read_f32_le(buf + 36);
  g_yoshi_shyguy.spawn_right_x = read_f32_le(buf + 40);
  g_yoshi_shyguy.state4_speed_mul = read_f32_le(buf + 44);
  g_yoshi_shyguy.jitter_y_amp = read_f32_le(buf + 48);
  size_t off = MSLSTIO1_HEADER_BYTES;
  for (int i = 0; i < MSL_YOSHI_SHYGUY_VPOS_COUNT; i++, off += sizeof(float)) {
    g_yoshi_shyguy.vpos[i] = read_f32_le(buf + off);
  }
  for (int i = 0; i < MSL_YOSHI_SHYGUY_SPEED_COUNT; i++, off += sizeof(float)) {
    g_yoshi_shyguy.speed[i] = read_f32_le(buf + off);
  }
  for (int i = 0; i < MSL_YOSHI_SHYGUY_DYN_Y_COUNT; i++, off += sizeof(float)) {
    g_yoshi_shyguy.dyn_y_vel[i] = read_f32_le(buf + off);
  }
  if (g_yoshi_shyguy.stage_id == 0u || g_yoshi_shyguy.item_kind == 0u ||
      g_yoshi_shyguy.timer_reset == 0u || g_yoshi_shyguy.spawn_delay_step == 0u) {
    return -1;
  }
  return 0;
}

int stage_item_params_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/stage_items/yoshi_shyguy.bin", data_dir);
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
  const int ok = (got == (size_t)sz && load_yoshi_shyguy(buf, (size_t)sz) == 0);
  alloc_free(buf);
  if (!ok) {
    return -1;
  }
  g_loaded = 1u;
  return 0;
}

const MslYoshiShyguyParams* stage_item_params_yoshi_shyguy(void) {
  return g_yoshi_shyguy.loaded ? &g_yoshi_shyguy : NULL;
}
