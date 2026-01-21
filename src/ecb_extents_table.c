#include "ecb_extents_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

enum {
  ECB_MAGIC_LEN = 8,
  ECB_HDR_BYTES = 8 + 4 + 2 + 2 + 4 + 4,
  ECB_TOC_ENTRY_BYTES = 2 + 2 + 4 + 4,
  ECB_EXTENTS_STRIDE_BYTES = 16,  // float32[4]
};

static const uint8_t k_ecb_magic[ECB_MAGIC_LEN] = {'M', 'S', 'L', 'E', 'C', 'B', '0', '1'};
static const uint32_t k_ecb_format_version = 2;

typedef struct {
  uint16_t msid;
  uint16_t frame_count;
  uint32_t values_off;  // byte offset from start of file to float32[frame_count][4]
} MslEcbExtentsEntry;

typedef struct {
  uint8_t* buf;
  size_t sz;
  MslEcbExtentsEntry* entries;
  uint16_t anim_count;
  uint8_t have;
} MslEcbExtentsTable;

static MslEcbExtentsTable g_table_by_char[256];
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

static int load_table_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
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

  if (sz < ECB_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_ecb_magic, ECB_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != k_ecb_format_version) {
    alloc_free(buf);
    return -1;
  }

  const uint16_t anim_count = read_u16_le(buf + 12);
  const uint16_t stride = read_u16_le(buf + 14);
  if (stride != ECB_EXTENTS_STRIDE_BYTES) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t toc_off = read_u32_le(buf + 16);
  const uint32_t file_bytes = read_u32_le(buf + 20);
  if (file_bytes != (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }
  if (toc_off < ECB_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  const size_t toc_bytes = (size_t)anim_count * (size_t)ECB_TOC_ENTRY_BYTES;
  if (toc_off > sz || toc_bytes > (sz - toc_off)) {
    alloc_free(buf);
    return -1;
  }

  MslEcbExtentsEntry* entries =
      (MslEcbExtentsEntry*)alloc_malloc(sizeof(MslEcbExtentsEntry) * (size_t)anim_count);
  if (entries == NULL) {
    alloc_free(buf);
    return -1;
  }

  const uint8_t* p = buf + toc_off;
  uint16_t prev_msid = 0;
  uint8_t have_prev = 0;
  for (uint16_t i = 0; i < anim_count; i++) {
    const uint16_t msid = read_u16_le(p + 0);
    const uint16_t frame_count = read_u16_le(p + 2);
    const uint32_t values_off = read_u32_le(p + 4);
    const uint32_t values_bytes = read_u32_le(p + 8);
    p += ECB_TOC_ENTRY_BYTES;

    if (values_bytes != (uint32_t)frame_count * (uint32_t)ECB_EXTENTS_STRIDE_BYTES) {
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    if (values_off > sz || values_bytes > (uint32_t)(sz - (size_t)values_off)) {
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    if ((values_off & 3u) != 0u) {
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    if (have_prev && msid < prev_msid) {
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    prev_msid = msid;
    have_prev = 1;

    entries[i] = (MslEcbExtentsEntry){
        .msid = msid,
        .frame_count = frame_count,
        .values_off = values_off,
    };
  }

  if (g_table_by_char[char_id].buf) {
    alloc_free(g_table_by_char[char_id].entries);
    alloc_free(g_table_by_char[char_id].buf);
  }
  g_table_by_char[char_id] = (MslEcbExtentsTable){
      .buf = buf,
      .sz = sz,
      .entries = entries,
      .anim_count = anim_count,
      .have = 1,
  };
  return 0;
}

int ecb_extents_table_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_table_for_char(data_dir, "ecb/fox_extents.bin", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_table_for_char(data_dir, "ecb/falco_extents.bin", MSL_CHAR_FALCO) != 0) {
    return -1;
  }

  g_loaded = 1;
  return 0;
}

static const MslEcbExtentsTable* table_for_char(uint8_t char_id) {
  if (!g_loaded) {
    return NULL;
  }
  const MslEcbExtentsTable* t = &g_table_by_char[char_id];
  if (!t->have || t->buf == NULL || t->entries == NULL || t->anim_count == 0) {
    return NULL;
  }
  return t;
}

static int find_entry_index(const MslEcbExtentsTable* t, uint16_t msid) {
  if (t == NULL || t->entries == NULL) {
    return -1;
  }
  int lo = 0;
  int hi = (int)t->anim_count - 1;
  while (lo <= hi) {
    const int mid = lo + ((hi - lo) >> 1);
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

MslEcbExtentsRel msl_ecb_extents_rel(uint8_t char_id, uint32_t animation_index, int action_frame) {
  if (animation_index == 0xFFFFFFFFu) {
    return (MslEcbExtentsRel){0};
  }
  if (animation_index > 0xFFFFu) {
    return (MslEcbExtentsRel){0};
  }

  const MslEcbExtentsTable* t = table_for_char(char_id);
  if (t == NULL) {
    return (MslEcbExtentsRel){0};
  }

  const uint16_t msid = (uint16_t)animation_index;
  const int ei = find_entry_index(t, msid);
  if (ei < 0) {
    return (MslEcbExtentsRel){0};
  }
  const MslEcbExtentsEntry* e = &t->entries[ei];
  if (e->frame_count == 0) {
    return (MslEcbExtentsRel){0};
  }

  int f = action_frame;
  if (f < 0) {
    f = 0;
  }
  if (f >= (int)e->frame_count) {
    f = (int)e->frame_count - 1;
  }

  const uint8_t* base = t->buf + e->values_off + (size_t)f * (size_t)ECB_EXTENTS_STRIDE_BYTES;
  const float min_x = read_f32_le(base + 0);
  const float max_x = read_f32_le(base + 4);
  const float min_y = read_f32_le(base + 8);
  const float max_y = read_f32_le(base + 12);

  // If corrupted, fall back to 0s to avoid NaNs in hot-path physics/collision.
  if (!(min_x == min_x) || !(max_x == max_x) || !(min_y == min_y) || !(max_y == max_y)) {
    return (MslEcbExtentsRel){0};
  }

  return (MslEcbExtentsRel){
      .min_x = min_x,
      .max_x = max_x,
      .min_y = min_y,
      .max_y = max_y,
  };
}

float msl_ecb_left_rel_x(uint8_t char_id, uint32_t animation_index, int action_frame) {
  return msl_ecb_extents_rel(char_id, animation_index, action_frame).min_x;
}

float msl_ecb_right_rel_x(uint8_t char_id, uint32_t animation_index, int action_frame) {
  return msl_ecb_extents_rel(char_id, animation_index, action_frame).max_x;
}

float msl_ecb_bottom_rel_y_ext(uint8_t char_id, uint32_t animation_index, int action_frame) {
  return msl_ecb_extents_rel(char_id, animation_index, action_frame).min_y;
}

float msl_ecb_top_rel_y(uint8_t char_id, uint32_t animation_index, int action_frame) {
  return msl_ecb_extents_rel(char_id, animation_index, action_frame).max_y;
}
