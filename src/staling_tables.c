#include "staling_tables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

enum {
  MOVEID_MAGIC_LEN = 8,
  MOVEID_HDR_BYTES = 8 + 4 + 2 + 2 + 4 + 4,
  MOVEID_ENTRY_BYTES = 2 + 2,
};

static const uint8_t k_moveid_magic[MOVEID_MAGIC_LEN] = {'M', 'S', 'L', 'S', 'T', 'I', 'D', '1'};
static const uint32_t k_moveid_format_version = 1;

typedef struct {
  uint16_t msid;
  uint16_t move_id;
} MslStalingMoveIdEntry;

typedef struct {
  uint8_t* buf;
  size_t sz;
  MslStalingMoveIdEntry* entries;
  uint16_t entry_count;
  uint8_t have;
} MslStalingMoveIdTable;

static MslStalingMoveIdTable g_table_by_char[256];

enum {
  WEIGHTS_MAGIC_LEN = 8,
  WEIGHTS_HDR_BYTES = 8 + 4 + 2 + 2 + 4,
  WEIGHTS_COUNT = 9,
};

static const uint8_t k_weights_magic[WEIGHTS_MAGIC_LEN] = {'M', 'S', 'L', 'S', 'T', 'W', '0', '1'};
static const uint32_t k_weights_format_version = 1;

static float g_weights[WEIGHTS_COUNT];
static uint8_t g_have_weights = 0;

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

static const char* data_dir_or_default(void) {
  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }
  return data_dir;
}

static int load_file_buf(const char* path, uint8_t** out_buf, size_t* out_sz) {
  if (path == NULL || out_buf == NULL || out_sz == NULL) {
    return -1;
  }
  *out_buf = NULL;
  *out_sz = 0;

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

  *out_buf = buf;
  *out_sz = sz;
  return 0;
}

static int load_move_id_table_for_char(uint8_t char_id, const char* rel_name) {
  const char* data_dir = data_dir_or_default();
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/staling/move_id/%s.bin", data_dir, rel_name);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  uint8_t* buf = NULL;
  size_t sz = 0;
  if (load_file_buf(path, &buf, &sz) != 0) {
    return -1;
  }
  if (sz < MOVEID_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_moveid_magic, MOVEID_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }

  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != k_moveid_format_version) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t entry_count = read_u16_le(buf + 12);
  const uint32_t toc_off = read_u32_le(buf + 16);
  const uint32_t file_bytes = read_u32_le(buf + 20);
  if (file_bytes != (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }
  if (toc_off < MOVEID_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (toc_off + (uint32_t)entry_count * (uint32_t)MOVEID_ENTRY_BYTES > (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }

  // Validate monotonic msid order (deterministic binary search).
  const uint8_t* p = buf + toc_off;
  uint16_t prev = 0;
  uint8_t have_prev = 0;
  for (uint16_t i = 0; i < entry_count; i++) {
    const uint16_t msid = read_u16_le(p + (size_t)i * MOVEID_ENTRY_BYTES);
    if (have_prev && msid < prev) {
      alloc_free(buf);
      return -1;
    }
    prev = msid;
    have_prev = 1;
  }

  g_table_by_char[char_id] =
      (MslStalingMoveIdTable){.buf = buf,
                              .sz = sz,
                              .entries = (MslStalingMoveIdEntry*)(void*)(buf + toc_off),
                              .entry_count = entry_count,
                              .have = 1};
  return 0;
}

static int load_staling_weights(void) {
  const char* data_dir = data_dir_or_default();
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/staling/weights.bin", data_dir);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  uint8_t* buf = NULL;
  size_t sz = 0;
  if (load_file_buf(path, &buf, &sz) != 0) {
    return -1;
  }
  if (sz < WEIGHTS_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_weights_magic, WEIGHTS_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != k_weights_format_version) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t count = read_u16_le(buf + 12);
  const uint32_t file_bytes = read_u32_le(buf + 16);
  if (file_bytes != (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }
  if (count != (uint16_t)WEIGHTS_COUNT) {
    alloc_free(buf);
    return -1;
  }
  if (WEIGHTS_HDR_BYTES + (size_t)count * 4u > sz) {
    alloc_free(buf);
    return -1;
  }

  for (uint16_t i = 0; i < count; i++) {
    const float w = read_f32_le(buf + WEIGHTS_HDR_BYTES + (size_t)i * 4u);
    // Reject NaN/Inf: downstream staling math must be deterministic and safe.
    if (!isfinite(w)) {
      alloc_free(buf);
      g_have_weights = 0;
      return -1;
    }
    g_weights[i] = w;
  }
  g_have_weights = 1;

  alloc_free(buf);
  return 0;
}

int staling_tables_init(void) {
  if (g_loaded) {
    return 0;
  }

  // Treat missing artifacts as non-fatal (groundwork is allowed to be debug-only).
  (void)load_staling_weights();
  (void)load_move_id_table_for_char((uint8_t)MSL_CHAR_FOX, "fox");
  (void)load_move_id_table_for_char((uint8_t)MSL_CHAR_FALCO, "falco");

  g_loaded = 1;
  return 0;
}

const float* staling_weights_table(void) { return g_have_weights ? g_weights : NULL; }

static int find_entry_index(const MslStalingMoveIdTable* t, uint16_t msid) {
  if (t == NULL || !t->have || t->entries == NULL || t->entry_count == 0) {
    return -1;
  }
  int lo = 0;
  int hi = (int)t->entry_count - 1;
  while (lo <= hi) {
    const int mid = lo + ((hi - lo) / 2);
    const uint16_t m = t->entries[mid].msid;
    if (m == msid) {
      return mid;
    }
    if (m < msid) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

uint16_t staling_move_id_from_msid(uint8_t char_id, uint16_t msid) {
  const MslStalingMoveIdTable* t = &g_table_by_char[char_id];
  const int ei = find_entry_index(t, msid);
  if (ei < 0) {
    return 0xFFFFu;
  }
  return t->entries[ei].move_id;
}
