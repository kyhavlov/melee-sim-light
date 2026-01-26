#include "anim_pose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

// SSANIM01 v3 is written by tools/extraction/extract_fighter_anims.py.
enum {
  ANIM_MAGIC_LEN = 8,
  ANIM_HDR_BASE_BYTES = 16,  // magic[8] + ver[u32] + joint_count[u16] + anim_count[u16]
  ANIM_VERSION_V3 = 3,
  MAT_BYTES = 12 * 4,             // float32[12] (3x4)
  TRANSN_BYTES_PER_FRAME = 3 * 4  // float32[3] v3 tail (TransN/root translation)
};

static const uint8_t k_anim_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', '0', '1'};

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct {
  uint8_t* buf;
  size_t sz;

  // Header-derived.
  uint16_t joint_count;
  uint16_t anim_count;

  // O(1) lookup tables (bounded, init-time allocated):
  // - msid -> (have, frame_count, base_offset)
  // - part_id -> joint_index (0xFFFF if missing)
  uint8_t* have_msid;             // [65536]
  uint16_t* frame_count_by_msid;  // [65536]
  uint32_t* base_off_by_msid;     // [65536] byte offset to frame0/joint0 matrices
  uint16_t* part_to_joint_index;  // [65536]

  uint8_t have;
} MslAnimPoseTable;

static MslAnimPoseTable g_table_by_char[256];
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

static void free_table(MslAnimPoseTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->have_msid);
  alloc_free(t->frame_count_by_msid);
  alloc_free(t->base_off_by_msid);
  alloc_free(t->part_to_joint_index);
  alloc_free(t->buf);
  *t = (MslAnimPoseTable){0};
}

static int load_pose_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
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

  if (sz < ANIM_HDR_BASE_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_anim_magic, ANIM_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)ANIM_VERSION_V3) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t joint_count = read_u16_le(buf + 12);
  const uint16_t anim_count = read_u16_le(buf + 14);
  if (joint_count == 0) {
    alloc_free(buf);
    return -1;
  }
  if (sz < ANIM_HDR_BASE_BYTES + (size_t)joint_count) {
    alloc_free(buf);
    return -1;
  }

  uint8_t* have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint16_t* frame_count_by_msid = (uint16_t*)alloc_calloc(65536, sizeof(uint16_t));
  uint32_t* base_off_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  uint16_t* part_to_joint_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  if (have_msid == NULL || frame_count_by_msid == NULL || base_off_by_msid == NULL ||
      part_to_joint_index == NULL) {
    alloc_free(have_msid);
    alloc_free(frame_count_by_msid);
    alloc_free(base_off_by_msid);
    alloc_free(part_to_joint_index);
    alloc_free(buf);
    return -1;
  }
  memset(part_to_joint_index, 0xFF, 65536 * sizeof(uint16_t));  // 0xFFFF sentinel

  // Build part_id -> joint index mapping from joint_parts bytes in the header (like the extractors).
  for (uint16_t ji = 0; ji < joint_count; ji++) {
    const uint8_t part_id = buf[ANIM_HDR_BASE_BYTES + (size_t)ji];
    part_to_joint_index[(uint16_t)part_id] = ji;  // overwrite on duplicates (Python dict behavior)
  }

  // Walk payload (tools/extraction/extract_ecb_extents.py::_extract_ecb_extents_for_anim_file).
  size_t off = ANIM_HDR_BASE_BYTES + (size_t)joint_count;
  const uint64_t joint_count_u = (uint64_t)joint_count;
  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if (off + 4u > sz) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint16_t frame_count = read_u16_le(buf + off + 2);
    off += 4u;

    if (have_msid[msid]) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }

    // base_offset points to first frame's first joint matrix record (payload matrices start).
    have_msid[msid] = 1;
    frame_count_by_msid[msid] = frame_count;
    if (off > 0xFFFFFFFFu) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    base_off_by_msid[msid] = (uint32_t)off;

    const uint64_t frame_count_u = (uint64_t)frame_count;
    const uint64_t mats_bytes = frame_count_u * joint_count_u * (uint64_t)MAT_BYTES;
    const uint64_t transn_bytes = frame_count_u * (uint64_t)TRANSN_BYTES_PER_FRAME;
    const uint64_t need_u = mats_bytes + transn_bytes;
    if (need_u > (uint64_t)(sz - off)) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    off += (size_t)need_u;
  }

  // Fail loudly on unexpected extension bytes (extractors enforce this today).
  if (off != sz) {
    alloc_free(have_msid);
    alloc_free(frame_count_by_msid);
    alloc_free(base_off_by_msid);
    alloc_free(part_to_joint_index);
    alloc_free(buf);
    return -1;
  }

  // Replace any existing table for this character.
  if (g_table_by_char[char_id].buf) {
    free_table(&g_table_by_char[char_id]);
  }
  g_table_by_char[char_id] = (MslAnimPoseTable){
      .buf = buf,
      .sz = sz,
      .joint_count = joint_count,
      .anim_count = anim_count,
      .have_msid = have_msid,
      .frame_count_by_msid = frame_count_by_msid,
      .base_off_by_msid = base_off_by_msid,
      .part_to_joint_index = part_to_joint_index,
      .have = 1,
  };
  return 0;
}

int anim_pose_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_pose_for_char(data_dir, "anims/fox.bin", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_pose_for_char(data_dir, "anims/falco.bin", MSL_CHAR_FALCO) != 0) {
    free_table(&g_table_by_char[MSL_CHAR_FOX]);
    return -1;
  }

  g_loaded = 1;
  return 0;
}

static const MslAnimPoseTable* table_for_char(uint8_t char_id) {
  if (!g_loaded) {
    return NULL;
  }
  const MslAnimPoseTable* t = &g_table_by_char[char_id];
  if (!t->have || t->buf == NULL || t->have_msid == NULL || t->frame_count_by_msid == NULL ||
      t->base_off_by_msid == NULL || t->part_to_joint_index == NULL || t->joint_count == 0) {
    return NULL;
  }
  return t;
}

void anim_pose_reset_for_tests(void) {
  for (int i = 0; i < 256; i++) {
    free_table(&g_table_by_char[i]);
  }
  g_loaded = 0;
}

int anim_pose_get_matrix(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t part_id,
                         float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  if (!t->have_msid[msid]) {
    return -1;
  }

  const uint16_t frame_count = t->frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }

  const uint16_t joint_index = t->part_to_joint_index[part_id];
  if (joint_index == 0xFFFFu || joint_index >= t->joint_count) {
    return -1;
  }

  const uint32_t base_off = t->base_off_by_msid[msid];
  const uint64_t joint_count_u = (uint64_t)t->joint_count;
  const uint64_t frame_u = (uint64_t)frame;
  const uint64_t joint_u = (uint64_t)joint_index;

  const uint64_t mat_off_u = (uint64_t)base_off + frame_u * joint_count_u * (uint64_t)MAT_BYTES +
                             joint_u * (uint64_t)MAT_BYTES;
  if (mat_off_u + (uint64_t)MAT_BYTES > (uint64_t)t->sz) {
    return -1;
  }

  memcpy(out_3x4, t->buf + (size_t)mat_off_u, (size_t)MAT_BYTES);
  return 0;
}

int anim_pose_get_transn(uint8_t char_id, uint16_t msid, uint16_t frame, float out_xyz[3]) {
  if (out_xyz == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  if (!t->have_msid[msid]) {
    return -1;
  }

  const uint16_t frame_count = t->frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }

  const uint32_t base_off = t->base_off_by_msid[msid];
  const uint64_t joint_count_u = (uint64_t)t->joint_count;
  const uint64_t frame_count_u = (uint64_t)frame_count;
  const uint64_t frame_u = (uint64_t)frame;

  const uint64_t mats_bytes = frame_count_u * joint_count_u * (uint64_t)MAT_BYTES;
  const uint64_t transn_off_u = (uint64_t)base_off + mats_bytes + frame_u * (uint64_t)TRANSN_BYTES_PER_FRAME;
  if (transn_off_u + (uint64_t)TRANSN_BYTES_PER_FRAME > (uint64_t)t->sz) {
    return -1;
  }

  memcpy(out_xyz, t->buf + (size_t)transn_off_u, (size_t)TRANSN_BYTES_PER_FRAME);
  return 0;
}
