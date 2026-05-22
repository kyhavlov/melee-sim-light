#include "anim_table.h"
#include "ids.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

typedef struct {
  // Submotion ids in GALE01 exceed 255 (e.g. Fox/Falco specials are in the ~295+ range).
  // Keep this as a fixed array for hot-path lookup without allocations.
  float end_frame_by_smid[1024];
  uint8_t root_motion_by_smid[1024];
  uint8_t loop_by_smid[1024];
  uint8_t have_smid[1024];
  uint8_t part_under_xrotn[256];
} MslAnimTable;

static MslAnimTable g_table_by_char[256];
static uint8_t g_have_char[256];
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

static int load_tracks_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
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

  // tracks.bin format is defined in `tools/extraction/extract_fighter_anims.py` (SSANIMT1).
  const uint8_t* p = buf;
  const uint8_t* end = buf + (size_t)sz;
  if ((size_t)(end - p) < 8) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(p, "SSANIMT1", 8) != 0) {
    alloc_free(buf);
    return -1;
  }
  p += 8;
  if ((size_t)(end - p) < 4) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t format_version = read_u32_le(p);
  p += 4;
  if (format_version != 3) {
    alloc_free(buf);
    return -1;
  }

  if ((size_t)(end - p) < 4) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t local_count = read_u16_le(p);
  const uint16_t anim_count = read_u16_le(p + 2);
  p += 4;

  const size_t local_parts_bytes = (size_t)local_count;
  const size_t local_parent_bytes = (size_t)local_count * 2u;
  const size_t local_flags_bytes = (size_t)local_count * 4u;
  const size_t skip_hdr = local_parts_bytes + local_parent_bytes + local_flags_bytes;
  if ((size_t)(end - p) < skip_hdr) {
    alloc_free(buf);
    return -1;
  }

  MslAnimTable tbl = {0};
  int16_t parent_by_part[256];
  for (int i = 0; i < 256; i++) {
    parent_by_part[i] = -1;
  }
  const uint8_t* local_parts_ptr = p;
  const uint8_t* local_parent_ptr = p + local_parts_bytes;
  for (uint16_t li = 0; li < local_count; li++) {
    const uint8_t part = local_parts_ptr[li];
    parent_by_part[part] = (int16_t)read_u16_le(local_parent_ptr + (size_t)li * 2u);
  }
  for (uint16_t li = 0; li < local_count; li++) {
    const uint8_t part = local_parts_ptr[li];
    int steps = 0;
    int cur = (int)part;
    while (cur >= 0 && cur < 256 && steps < 256) {
      if (cur == 2) {  // FtPart_XRotN
        tbl.part_under_xrotn[part] = 1u;
        break;
      }
      const int next = (int)parent_by_part[cur];
      if (next == cur) {
        break;
      }
      cur = next;
      steps++;
    }
  }
  p += skip_hdr;

  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if ((size_t)(end - p) < 2 + 4 + 2u) {
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(p);
    p += 2;
    const float end_frame = read_f32_le(p);
    p += 4;
    const uint8_t aobj_loop = *(const uint8_t*)p;
    p += 1;
    const uint8_t uses_root_motion = *(const uint8_t*)p;
    p += 1;

    if (msid < 1024) {
      tbl.end_frame_by_smid[msid] = end_frame;
      tbl.root_motion_by_smid[msid] = (uint8_t)(uses_root_motion ? 1 : 0);
      tbl.loop_by_smid[msid] = (uint8_t)(aobj_loop ? 1 : 0);
      tbl.have_smid[msid] = 1;
    }

    for (uint16_t li = 0; li < local_count; li++) {
      if ((size_t)(end - p) < 2) {
        alloc_free(buf);
        return -1;
      }
      const uint8_t n_tracks = *(const uint8_t*)(p + 1);
      p += 2;
      for (uint8_t ti = 0; ti < n_tracks; ti++) {
        if ((size_t)(end - p) < 8) {
          alloc_free(buf);
          return -1;
        }
        const uint16_t len = read_u16_le(p + 6);
        p += 8;
        if ((size_t)(end - p) < (size_t)len) {
          alloc_free(buf);
          return -1;
        }
        p += (size_t)len;
      }
    }
  }

  alloc_free(buf);
  g_table_by_char[char_id] = tbl;
  g_have_char[char_id] = 1;
  return 0;
}

int anim_table_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_tracks_for_char(data_dir, "anims/fox.tracks.bin", MSL_CHAR_ID_FOX) != 0) {
    return -1;
  }
  if (load_tracks_for_char(data_dir, "anims/falco.tracks.bin", MSL_CHAR_ID_FALCO) != 0) {
    return -1;
  }

  g_loaded = 1;
  return 0;
}

float msl_anim_end_frame(uint8_t char_id, uint16_t submotion_id) {
  if (!g_loaded || !g_have_char[char_id]) {
    return 0.0f;
  }
  if (submotion_id >= 1024) {
    return 0.0f;
  }
  const MslAnimTable* t = &g_table_by_char[char_id];
  if (!t->have_smid[submotion_id]) {
    return 0.0f;
  }
  return t->end_frame_by_smid[submotion_id];
}

uint8_t msl_anim_uses_root_motion(uint8_t char_id, uint16_t submotion_id) {
  if (!g_loaded || !g_have_char[char_id]) {
    return 0;
  }
  if (submotion_id >= 1024) {
    return 0;
  }
  const MslAnimTable* t = &g_table_by_char[char_id];
  if (!t->have_smid[submotion_id]) {
    return 0;
  }
  return (uint8_t)(t->root_motion_by_smid[submotion_id] ? 1 : 0);
}

uint8_t msl_anim_is_looping(uint8_t char_id, uint16_t submotion_id) {
  if (!g_loaded || !g_have_char[char_id]) {
    return 0;
  }
  if (submotion_id >= 1024) {
    return 0;
  }
  const MslAnimTable* t = &g_table_by_char[char_id];
  if (!t->have_smid[submotion_id]) {
    return 0;
  }
  return (uint8_t)(t->loop_by_smid[submotion_id] ? 1 : 0);
}

uint8_t msl_anim_part_under_xrotn(uint8_t char_id, uint16_t part_id) {
  if (!g_loaded || !g_have_char[char_id]) {
    return 0;
  }
  if (part_id >= 256) {
    return 0;
  }
  return g_table_by_char[char_id].part_under_xrotn[part_id] ? 1u : 0u;
}
