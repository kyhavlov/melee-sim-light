#include "shield_tilt_table.h"
#include "data_dir.h"
#include "char_registry.h"
#include "ids.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  SHIELD_MAGIC_LEN = 8,
  SHIELD_HDR_BYTES_V1 = 16,  // magic[8] + ver[u32] + frame_count[u16] + neutral_frame[u16]
  SHIELD_HDR_BYTES_V2 = 28,  // v1 + entry_anchor_xyz[3f]
  SHIELD_HDR_BYTES_V3 = 32,  // v2 + guard_on_frame_count[u16] + pad[u16]
  SHIELD_VERSION_V1 = 1,
  SHIELD_VERSION_V2 = 2,
  SHIELD_VERSION_V3 = 3,
  SHIELD_VERSION_V4 = 4,
};

static const uint8_t k_magic[SHIELD_MAGIC_LEN] = {'M', 'S', 'L', 'S', 'H', 'L', 'D', '1'};

typedef struct {
  uint8_t* buf;
  size_t sz;
  const float* xyz;
  float guard_on_x20_xyz[3];
  const float* guard_on_xyz;
  uint16_t frame_count;
  uint16_t neutral_frame;
  uint16_t guard_on_frame_count;
  uint8_t have;
} MslShieldTiltTable;

static MslShieldTiltTable g_by_char[256];
static uint8_t g_loaded = 0;

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

static void free_table(MslShieldTiltTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->buf);
  *t = (MslShieldTiltTable){0};
}

static int load_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
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
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    alloc_free(buf);
    return -1;
  }

  if (sz < (size_t)SHIELD_HDR_BYTES_V1) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, SHIELD_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)SHIELD_VERSION_V4) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t frame_count = read_u16_le(buf + 12);
  const uint16_t neutral_frame = read_u16_le(buf + 14);
  if (frame_count == 0 || neutral_frame >= frame_count) {
    alloc_free(buf);
    return -1;
  }
  float guard_on_x20_xyz[3] = {0.0f, 0.0f, 0.0f};
  size_t hdr_bytes = (size_t)SHIELD_HDR_BYTES_V1;
  uint16_t guard_on_frame_count = 0u;
  const float* guard_on_xyz = NULL;
  if (ver == (uint32_t)SHIELD_VERSION_V4) {
    hdr_bytes = (size_t)SHIELD_HDR_BYTES_V3;
    memcpy(guard_on_x20_xyz, buf + SHIELD_HDR_BYTES_V1, sizeof(guard_on_x20_xyz));
    guard_on_frame_count = read_u16_le(buf + SHIELD_HDR_BYTES_V2);
    if (guard_on_frame_count == 0u) {
      alloc_free(buf);
      return -1;
    }
  }

  size_t need = hdr_bytes + (size_t)frame_count * 3u * 4u;
  if (ver == (uint32_t)SHIELD_VERSION_V4) {
    need += (size_t)guard_on_frame_count * 3u * 4u;
  }
  if (need != sz) {
    alloc_free(buf);
    return -1;
  }
  if (ver == (uint32_t)SHIELD_VERSION_V4) {
    guard_on_xyz = (const float*)(buf + hdr_bytes + (size_t)frame_count * 3u * 4u);
  }

  // Replace existing.
  if (g_by_char[char_id].buf != NULL) {
    free_table(&g_by_char[char_id]);
  }

  g_by_char[char_id] = (MslShieldTiltTable){
      .buf = buf,
      .sz = sz,
      .xyz = (const float*)(buf + hdr_bytes),
      .guard_on_xyz = guard_on_xyz,
      .frame_count = frame_count,
      .neutral_frame = neutral_frame,
      .guard_on_frame_count = guard_on_frame_count,
      .have = 1,
  };
  g_by_char[char_id].guard_on_x20_xyz[0] = guard_on_x20_xyz[0];
  g_by_char[char_id].guard_on_x20_xyz[1] = guard_on_x20_xyz[1];
  g_by_char[char_id].guard_on_x20_xyz[2] = guard_on_x20_xyz[2];
  return 0;
}

int shield_tilt_table_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = msl_data_dir();

  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    char rel[64];
    snprintf(rel, sizeof(rel), "shields/%s.bin", MSL_CHAR_REGISTRY[ci].name);
    if (load_for_char(data_dir, rel, MSL_CHAR_REGISTRY[ci].char_id) != 0) {
      for (int cj = 0; cj < ci; cj++) {
        free_table(&g_by_char[MSL_CHAR_REGISTRY[cj].char_id]);
      }
      return -1;
    }
  }

  g_loaded = 1;
  return 0;
}

int msl_shield_tilt_table_view(uint8_t char_id, MslShieldTiltTableView* out) {
  if (out == NULL) {
    return -1;
  }
  if (!g_loaded) {
    return -1;
  }
  const MslShieldTiltTable* t = &g_by_char[char_id];
  if (!t->have || t->xyz == NULL || t->frame_count == 0) {
    return -1;
  }
  out->xyz = t->xyz;
  out->guard_on_x20_xyz[0] = t->guard_on_x20_xyz[0];
  out->guard_on_x20_xyz[1] = t->guard_on_x20_xyz[1];
  out->guard_on_x20_xyz[2] = t->guard_on_x20_xyz[2];
  out->guard_on_xyz = t->guard_on_xyz;
  out->frame_count = t->frame_count;
  out->neutral_frame = t->neutral_frame;
  out->guard_on_frame_count = t->guard_on_frame_count;
  return 0;
}
