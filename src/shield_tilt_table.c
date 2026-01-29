#include "shield_tilt_table.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

enum {
  SHIELD_MAGIC_LEN = 8,
  SHIELD_HDR_BYTES = 16,  // magic[8] + ver[u32] + frame_count[u16] + neutral_frame[u16]
  SHIELD_VERSION_V1 = 1,
};

static const uint8_t k_magic[SHIELD_MAGIC_LEN] = {'M', 'S', 'L', 'S', 'H', 'L', 'D', '1'};

typedef struct {
  uint8_t* buf;
  size_t sz;
  const float* xyz;
  uint16_t frame_count;
  uint16_t neutral_frame;
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
  uint8_t* buf = (uint8_t*)alloc_malloc(sz);
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

  if (sz < (size_t)SHIELD_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, SHIELD_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)SHIELD_VERSION_V1) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t frame_count = read_u16_le(buf + 12);
  const uint16_t neutral_frame = read_u16_le(buf + 14);
  if (frame_count == 0 || neutral_frame >= frame_count) {
    alloc_free(buf);
    return -1;
  }

  const size_t need = (size_t)SHIELD_HDR_BYTES + (size_t)frame_count * 3u * 4u;
  if (need != sz) {
    alloc_free(buf);
    return -1;
  }

  // Replace existing.
  if (g_by_char[char_id].buf != NULL) {
    free_table(&g_by_char[char_id]);
  }

  g_by_char[char_id] = (MslShieldTiltTable){
      .buf = buf,
      .sz = sz,
      .xyz = (const float*)(buf + SHIELD_HDR_BYTES),
      .frame_count = frame_count,
      .neutral_frame = neutral_frame,
      .have = 1,
  };
  return 0;
}

int shield_tilt_table_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_for_char(data_dir, "shields/fox.bin", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_for_char(data_dir, "shields/falco.bin", MSL_CHAR_FALCO) != 0) {
    free_table(&g_by_char[MSL_CHAR_FOX]);
    return -1;
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
  *out = (MslShieldTiltTableView){
      .xyz = t->xyz,
      .frame_count = t->frame_count,
      .neutral_frame = t->neutral_frame,
  };
  return 0;
}
