#include "hurtcaps_tables.h"
#include "char_registry.h"
#include "ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  HURTCAPS_MAGIC_LEN = 8,
  HURTCAPS_HDR_BYTES = 16,
  HURTCAPS_VERSION_V1 = 1,
  REC_BYTES_V1 = 34
};
static const uint8_t k_magic[HURTCAPS_MAGIC_LEN] = {'M', 'S', 'L', 'H', 'U', 'R', 'T', '1'};

typedef struct {
  MslHurtCap* caps;
  uint16_t count;
  uint8_t have;
} MslHurtCapsTable;

static MslHurtCapsTable g_table_by_char[256];
static int g_loaded = 0;

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

static void free_table(MslHurtCapsTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->caps);
  *t = (MslHurtCapsTable){0};
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

  if (sz < HURTCAPS_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, HURTCAPS_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)HURTCAPS_VERSION_V1) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t count = read_u16_le(buf + 12);
  const uint16_t reserved = read_u16_le(buf + 14);
  if (reserved != 0) {
    alloc_free(buf);
    return -1;
  }

  const size_t need = (size_t)HURTCAPS_HDR_BYTES + (size_t)count * (size_t)REC_BYTES_V1;
  if (need != sz) {
    alloc_free(buf);
    return -1;
  }

  MslHurtCap* caps = NULL;
  if (count > 0) {
    caps = (MslHurtCap*)alloc_malloc((size_t)count * sizeof(MslHurtCap));
    if (caps == NULL) {
      alloc_free(buf);
      return -1;
    }
  }

  size_t off = HURTCAPS_HDR_BYTES;
  for (uint16_t i = 0; i < count; i++) {
    const uint8_t* r = buf + off;
    MslHurtCap cap = {0};
    cap.bone_part_id = read_u16_le(r + 0);
    cap.height = r[2];
    cap.is_grabbable = r[3];
    const uint16_t pad = read_u16_le(r + 4);
    if (pad != 0) {
      alloc_free(caps);
      alloc_free(buf);
      return -1;
    }
    cap.a_offset[0] = read_f32_le(r + 6);
    cap.a_offset[1] = read_f32_le(r + 10);
    cap.a_offset[2] = read_f32_le(r + 14);
    cap.b_offset[0] = read_f32_le(r + 18);
    cap.b_offset[1] = read_f32_le(r + 22);
    cap.b_offset[2] = read_f32_le(r + 26);
    cap.scale = read_f32_le(r + 30);
    if (caps != NULL) {
      caps[i] = cap;
    }
    off += (size_t)REC_BYTES_V1;
  }
  alloc_free(buf);

  if (g_table_by_char[char_id].caps) {
    free_table(&g_table_by_char[char_id]);
  }
  g_table_by_char[char_id] = (MslHurtCapsTable){
      .caps = caps,
      .count = count,
      .have = 1,
  };
  return 0;
}

int hurtcaps_tables_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    char rel[64];
    snprintf(rel, sizeof(rel), "hurtcaps/%s.bin", MSL_CHAR_REGISTRY[ci].name);
    if (load_for_char(data_dir, rel, MSL_CHAR_REGISTRY[ci].char_id) != 0) {
      for (int cj = 0; cj < ci; cj++) {
        free_table(&g_table_by_char[MSL_CHAR_REGISTRY[cj].char_id]);
      }
      return -1;
    }
  }

  g_loaded = 1;
  return 0;
}

int hurtcaps_get(uint8_t char_id, const MslHurtCap** out_caps, uint16_t* out_count) {
  if (out_caps == NULL || out_count == NULL) {
    return -1;
  }
  *out_caps = NULL;
  *out_count = 0;
  if (!g_loaded) {
    return -1;
  }
  const MslHurtCapsTable* t = &g_table_by_char[char_id];
  if (!t->have || (t->count > 0 && t->caps == NULL)) {
    return -1;
  }
  *out_caps = t->caps;
  *out_count = t->count;
  return 0;
}
