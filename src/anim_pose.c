#include "anim_pose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "anim_frame.h"
#include "batch_internal.h"
#include "msl_math.h"

// SSANIM01 v3 is written by tools/extraction/extract_fighter_anims.py.
enum {
  ANIM_MAGIC_LEN = 8,
  ANIM_HDR_BASE_BYTES = 16,  // magic[8] + ver[u32] + joint_count[u16] + anim_count[u16]
  ANIM_VERSION_V3 = 3,
  MAT_BYTES = 12 * 4,              // float32[12] (3x4)
  TRANSN_BYTES_PER_FRAME = 3 * 4,  // float32[3] v3 tail (TransN/root translation)

  // RL1.0 target data contract:
  // - Fox ftData.x2C has exactly one dynamic bone set rooted at part 17.
  // - Falco ftData.x2C has zero dynamic bone sets.
  //
  // Runtime state is indexed by player and node. Until a multi-set seed/state surface is added,
  // reject present SSDYNN01 files with more than one set instead of accepting a layout the hot path
  // cannot represent without set-index collisions.
  ANIM_DYN_MAX_SETS = 1
};

static const uint8_t k_anim_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', '0', '1'};

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct {
  uint16_t part_id;
  float c[15];
} MslAnimDynNodeData;

typedef struct {
  uint16_t root_part;
  uint16_t node_count;
  float pos[3];
  MslAnimDynNodeData nodes[MSL_MAX_DYNAMIC_NODES];
} MslAnimDynSetData;

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

  uint8_t* local_buf;
  size_t local_sz;
  uint16_t local_count;
  uint16_t local_anim_count;
  uint16_t* local_part_to_index;        // [65536]
  int16_t* local_parent_part_by_index;  // [local_count]
  uint32_t* local_flags_by_index;       // [local_count]
  uint8_t* local_have_msid;             // [65536]
  uint16_t* local_frame_count_by_msid;  // [65536]
  uint32_t* local_base_off_by_msid;     // [65536] byte offset to frame0/local0 SRT records

  uint16_t dyn_set_count;
  uint16_t dyn_total_nodes;
  MslAnimDynSetData dyn_sets[ANIM_DYN_MAX_SETS];
  uint8_t* dyn_collision_have_msid;  // [65536], extracted SSDYNN01 collision-owner index

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

static float read_f32_le(const uint8_t* p) {
  float v = 0.0f;
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
  alloc_free(t->local_buf);
  alloc_free(t->local_part_to_index);
  alloc_free(t->local_parent_part_by_index);
  alloc_free(t->local_flags_by_index);
  alloc_free(t->local_have_msid);
  alloc_free(t->local_frame_count_by_msid);
  alloc_free(t->local_base_off_by_msid);
  alloc_free(t->dyn_collision_have_msid);
  alloc_free(t->buf);
  *t = (MslAnimPoseTable){0};
}

static int load_locals_into_table(const char* data_dir, const char* rel_path, MslAnimPoseTable* t) {
  if (data_dir == NULL || rel_path == NULL || t == NULL) {
    return -1;
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return 1;
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

  static const uint8_t local_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', 'L', '1'};
  if (sz < ANIM_HDR_BASE_BYTES || memcmp(buf, local_magic, ANIM_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != 1u) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t local_count = read_u16_le(buf + 12);
  const uint16_t anim_count = read_u16_le(buf + 14);
  if (local_count == 0) {
    alloc_free(buf);
    return -1;
  }
  size_t off = ANIM_HDR_BASE_BYTES;
  const uint64_t header_need = (uint64_t)off + (uint64_t)local_count + (uint64_t)local_count * 2u +
                               (uint64_t)local_count * 4u;
  if (header_need > (uint64_t)sz) {
    alloc_free(buf);
    return -1;
  }

  uint16_t* local_part_to_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  int16_t* local_parent_part_by_index =
      (int16_t*)alloc_malloc((size_t)local_count * sizeof(int16_t));
  uint32_t* local_flags_by_index = (uint32_t*)alloc_malloc((size_t)local_count * sizeof(uint32_t));
  uint8_t* local_have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint16_t* local_frame_count_by_msid = (uint16_t*)alloc_calloc(65536, sizeof(uint16_t));
  uint32_t* local_base_off_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  if (local_part_to_index == NULL || local_parent_part_by_index == NULL ||
      local_flags_by_index == NULL || local_have_msid == NULL ||
      local_frame_count_by_msid == NULL || local_base_off_by_msid == NULL) {
    alloc_free(local_part_to_index);
    alloc_free(local_parent_part_by_index);
    alloc_free(local_flags_by_index);
    alloc_free(local_have_msid);
    alloc_free(local_frame_count_by_msid);
    alloc_free(local_base_off_by_msid);
    alloc_free(buf);
    return -1;
  }
  memset(local_part_to_index, 0xFF, 65536 * sizeof(uint16_t));

  for (uint16_t li = 0; li < local_count; li++) {
    const uint8_t part = buf[off + (size_t)li];
    local_part_to_index[(uint16_t)part] = li;
  }
  off += (size_t)local_count;

  for (uint16_t li = 0; li < local_count; li++) {
    uint16_t raw = read_u16_le(buf + off + (size_t)li * 2u);
    local_parent_part_by_index[li] = (int16_t)raw;
  }
  off += (size_t)local_count * 2u;

  for (uint16_t li = 0; li < local_count; li++) {
    local_flags_by_index[li] = read_u32_le(buf + off + (size_t)li * 4u);
  }
  off += (size_t)local_count * 4u;

  enum { LOCAL_SRT_BYTES = 9 * 4 };
  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if (off + 4u > sz) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint16_t frame_count = read_u16_le(buf + off + 2);
    off += 4u;
    if (local_have_msid[msid]) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    local_have_msid[msid] = 1u;
    local_frame_count_by_msid[msid] = frame_count;
    if (off > 0xFFFFFFFFu) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    local_base_off_by_msid[msid] = (uint32_t)off;

    const uint64_t need = (uint64_t)frame_count * (uint64_t)local_count * (uint64_t)LOCAL_SRT_BYTES;
    if (need > (uint64_t)(sz - off)) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    off += (size_t)need;
  }

  if (off != sz) {
    alloc_free(local_part_to_index);
    alloc_free(local_parent_part_by_index);
    alloc_free(local_flags_by_index);
    alloc_free(local_have_msid);
    alloc_free(local_frame_count_by_msid);
    alloc_free(local_base_off_by_msid);
    alloc_free(buf);
    return -1;
  }

  t->local_buf = buf;
  t->local_sz = sz;
  t->local_count = local_count;
  t->local_anim_count = anim_count;
  t->local_part_to_index = local_part_to_index;
  t->local_parent_part_by_index = local_parent_part_by_index;
  t->local_flags_by_index = local_flags_by_index;
  t->local_have_msid = local_have_msid;
  t->local_frame_count_by_msid = local_frame_count_by_msid;
  t->local_base_off_by_msid = local_base_off_by_msid;
  return 0;
}

static int load_dynamics_into_table(const char* data_dir, const char* rel_path,
                                    MslAnimPoseTable* t) {
  if (data_dir == NULL || rel_path == NULL || t == NULL) {
    return -1;
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return 1;
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

  static const uint8_t dyn_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'D', 'Y', 'N', 'N', '0', '1'};
  const uint32_t ver = (sz >= ANIM_HDR_BASE_BYTES) ? read_u32_le(buf + 8) : 0u;
  if (sz < ANIM_HDR_BASE_BYTES || memcmp(buf, dyn_magic, ANIM_MAGIC_LEN) != 0 ||
      (ver != 1u && ver != 2u)) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t set_count = read_u16_le(buf + 12);
  const uint16_t total_nodes = read_u16_le(buf + 14);
  if (set_count > (uint16_t)ANIM_DYN_MAX_SETS || total_nodes > (uint16_t)MSL_MAX_DYNAMIC_NODES) {
    alloc_free(buf);
    return -1;
  }

  size_t off = ANIM_HDR_BASE_BYTES;
  uint16_t seen_nodes = 0;
  memset(t->dyn_sets, 0, sizeof(t->dyn_sets));
  for (uint16_t si = 0; si < set_count; si++) {
    if (off + 16u > sz) {
      alloc_free(buf);
      return -1;
    }
    MslAnimDynSetData* set = &t->dyn_sets[si];
    set->root_part = read_u16_le(buf + off);
    set->node_count = read_u16_le(buf + off + 2u);
    set->pos[0] = read_f32_le(buf + off + 4u);
    set->pos[1] = read_f32_le(buf + off + 8u);
    set->pos[2] = read_f32_le(buf + off + 12u);
    off += 16u;
    if (set->node_count > (uint16_t)MSL_MAX_DYNAMIC_NODES) {
      alloc_free(buf);
      return -1;
    }
    if ((uint32_t)seen_nodes + (uint32_t)set->node_count > (uint32_t)total_nodes) {
      alloc_free(buf);
      return -1;
    }
    for (uint16_t ni = 0; ni < set->node_count; ni++) {
      if (off + 64u > sz) {
        alloc_free(buf);
        return -1;
      }
      set->nodes[ni].part_id = read_u16_le(buf + off);
      off += 4u;  // part + pad
      for (uint16_t ci = 0; ci < 15u; ci++) {
        set->nodes[ni].c[ci] = read_f32_le(buf + off + (size_t)ci * 4u);
      }
      off += 15u * 4u;
    }
    seen_nodes = (uint16_t)(seen_nodes + set->node_count);
  }
  uint8_t* dyn_collision_have_msid = NULL;
  if (ver == 2u) {
    if (off + 4u > sz) {
      alloc_free(buf);
      return -1;
    }
    const uint16_t collision_msid_count = read_u16_le(buf + off);
    off += 4u;  // collision_msid_count + reserved
    if (off + (size_t)collision_msid_count * 2u > sz) {
      alloc_free(buf);
      return -1;
    }
    dyn_collision_have_msid = (uint8_t*)alloc_calloc(65536, 1);
    if (dyn_collision_have_msid == NULL) {
      alloc_free(buf);
      return -1;
    }
    for (uint16_t i = 0; i < collision_msid_count; i++) {
      const uint16_t msid = read_u16_le(buf + off + (size_t)i * 2u);
      dyn_collision_have_msid[msid] = 1u;
    }
    off += (size_t)collision_msid_count * 2u;
  }
  alloc_free(buf);
  if (off != sz || seen_nodes != total_nodes) {
    alloc_free(dyn_collision_have_msid);
    return -1;
  }
  t->dyn_set_count = set_count;
  t->dyn_total_nodes = total_nodes;
  t->dyn_collision_have_msid = dyn_collision_have_msid;
  return 0;
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

  MslAnimPoseTable next = {
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
  const char* slash = strrchr(rel_path, '/');
  const char* file = (slash != NULL) ? slash + 1 : rel_path;
  const char* dot = strchr(file, '.');
  const int stem_len = (dot != NULL) ? (int)(dot - file) : (int)strlen(file);
  char locals_rel[128];
  int local_status = 1;
  if (snprintf(locals_rel, sizeof(locals_rel), "anims/%.*s.locals.bin", stem_len, file) > 0) {
    // Local SRT tables are required whenever dynamic BODY pose data is present. Older synthetic
    // tests that provide only SSANIM01 still run through anim_pose_get_matrix().
    local_status = load_locals_into_table(data_dir, locals_rel, &next);
    if (local_status < 0) {
      free_table(&next);
      return -1;
    }
  }
  char dyn_rel[128];
  int dyn_status = 1;
  if (snprintf(dyn_rel, sizeof(dyn_rel), "anims/%.*s.dyn.bin", stem_len, file) > 0) {
    dyn_status = load_dynamics_into_table(data_dir, dyn_rel, &next);
    if (dyn_status < 0) {
      free_table(&next);
      return -1;
    }
    if (dyn_status == 0 && local_status != 0) {
      free_table(&next);
      return -1;
    }
  }

  // Replace any existing table for this character.
  if (g_table_by_char[char_id].buf) {
    free_table(&g_table_by_char[char_id]);
  }
  g_table_by_char[char_id] = next;
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

static int local_srt_for_part(const MslAnimPoseTable* t, uint16_t msid, uint16_t frame,
                              uint16_t part_id, float rot[3], float pos[3], float scl[3],
                              uint32_t* out_flags, int16_t* out_parent) {
  if (t == NULL || rot == NULL || pos == NULL || scl == NULL || t->local_buf == NULL ||
      t->local_have_msid == NULL || t->local_part_to_index == NULL ||
      t->local_frame_count_by_msid == NULL || t->local_base_off_by_msid == NULL ||
      t->local_flags_by_index == NULL || t->local_parent_part_by_index == NULL) {
    return -1;
  }
  if (!t->local_have_msid[msid]) {
    return -1;
  }
  const uint16_t frame_count = t->local_frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }
  const uint16_t li = t->local_part_to_index[part_id];
  if (li == 0xFFFFu || li >= t->local_count) {
    return -1;
  }
  enum { LOCAL_SRT_FLOATS = 9, LOCAL_SRT_BYTES = LOCAL_SRT_FLOATS * 4 };
  const uint64_t off_u = (uint64_t)t->local_base_off_by_msid[msid] +
                         (uint64_t)frame * (uint64_t)t->local_count * (uint64_t)LOCAL_SRT_BYTES +
                         (uint64_t)li * (uint64_t)LOCAL_SRT_BYTES;
  if (off_u + (uint64_t)LOCAL_SRT_BYTES > (uint64_t)t->local_sz) {
    return -1;
  }
  const uint8_t* p = t->local_buf + (size_t)off_u;
  memcpy(rot, p + 0, 3u * sizeof(float));
  memcpy(pos, p + 12, 3u * sizeof(float));
  memcpy(scl, p + 24, 3u * sizeof(float));
  if (out_flags != NULL) {
    *out_flags = t->local_flags_by_index[li];
  }
  if (out_parent != NULL) {
    *out_parent = t->local_parent_part_by_index[li];
  }
  return 0;
}

static void vec3_cross(const float a[3], const float b[3], float out[3]);
static uint8_t vec3_normalize(float v[3]);
static float vec3_angle(const float a[3], const float b[3]);
static void vec3_rotate_about_unit_axis(const float v[3], const float axis[3], float angle,
                                        float out[3]);

static void mtx34_identity(float m[12]) {
  m[0] = 1.0f;
  m[1] = 0.0f;
  m[2] = 0.0f;
  m[3] = 0.0f;
  m[4] = 0.0f;
  m[5] = 1.0f;
  m[6] = 0.0f;
  m[7] = 0.0f;
  m[8] = 0.0f;
  m[9] = 0.0f;
  m[10] = 1.0f;
  m[11] = 0.0f;
}

static void mtx34_concat(const float a[12], const float b[12], float out[12]) {
  float o[12];
  o[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8];
  o[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9];
  o[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10];
  o[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3];
  o[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8];
  o[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9];
  o[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10];
  o[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7];
  o[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8];
  o[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9];
  o[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10];
  o[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11];
  memcpy(out, o, sizeof(o));
}

static void mtx34_srt_simple(const float rot[3], const float pos[3], const float scl[3],
                             const float* parent_scl, float out[12]) {
  float sx = scl[0], sy = scl[1], sz = scl[2];
  float sx2 = sx, sy2 = sy, sz2 = sz;
  float sx1 = sx, sy1 = sy, sz1 = sz;
  if (parent_scl != NULL) {
    const float psx = parent_scl[0], psy = parent_scl[1], psz = parent_scl[2];
    if (psx != 0.0f && psy != 0.0f && psz != 0.0f) {
      sy2 = sy2 * (psy / psx);
      sz2 = sz2 * (psz / psx);
      sx1 = sx1 * (psx / psy);
      sz1 = sz1 * (psz / psy);
      sx = sx * (psx / psz);
      sy = sy * (psy / psz);
    }
  }

  const float rx = rot[0], ry = rot[1], rz = rot[2];
  const float sin_x = sinf(rx), cos_x = cosf(rx);
  const float sin_y = sinf(ry), cos_y = cosf(ry);
  const float sin_z = sinf(rz), cos_z = cosf(rz);

  out[0] = cos_z * (sx2 * cos_y);
  out[4] = sin_z * (sx1 * cos_y);
  out[8] = -sx * sin_y;

  const float sinx_siny = sin_x * sin_y;
  out[1] = sy2 * (cos_z * sinx_siny - cos_x * sin_z);
  out[5] = sy1 * (sin_z * sinx_siny + cos_x * cos_z);
  out[9] = cos_y * (sy * sin_x);

  const float cosx_siny = cos_x * sin_y;
  out[2] = sz2 * (cos_z * cosx_siny + sin_x * sin_z);
  out[6] = sz1 * (sin_z * cosx_siny - sin_x * cos_z);
  out[10] = cos_y * (sz * cos_x);

  out[3] = pos[0];
  out[7] = pos[1];
  out[11] = pos[2];
}

static void mtx34_apply_world_axis_angle(float m[12], const float axis[3], float angle) {
  float col0[3] = {m[0], m[4], m[8]};
  float col1[3] = {m[1], m[5], m[9]};
  float col2[3] = {m[2], m[6], m[10]};
  float out[3];

  vec3_rotate_about_unit_axis(col0, axis, angle, out);
  m[0] = out[0];
  m[4] = out[1];
  m[8] = out[2];
  vec3_rotate_about_unit_axis(col1, axis, angle, out);
  m[1] = out[0];
  m[5] = out[1];
  m[9] = out[2];
  vec3_rotate_about_unit_axis(col2, axis, angle, out);
  m[2] = out[0];
  m[6] = out[1];
  m[10] = out[2];
}

static int dynamic_set_node_index_for_part(const MslAnimDynSetData* set, uint16_t part_id) {
  if (set == NULL) {
    return -1;
  }
  for (uint16_t i = 0; i < set->node_count; i++) {
    if (set->nodes[i].part_id == part_id) {
      return (int)i;
    }
  }
  return -1;
}

static const MslAnimDynSetData* dynamic_set_for_part(const MslAnimPoseTable* t, uint16_t part_id) {
  if (t == NULL) {
    return NULL;
  }
  for (uint16_t si = 0; si < t->dyn_set_count; si++) {
    const MslAnimDynSetData* set = &t->dyn_sets[si];
    if (dynamic_set_node_index_for_part(set, part_id) >= 0) {
      return set;
    }
  }
  return NULL;
}

static size_t dynamic_state_index(size_t player_idx, uint16_t node_i) {
  return player_idx * (size_t)MSL_MAX_DYNAMIC_NODES + (size_t)node_i;
}

static float vec3_dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float vec3_len(const float v[3]) { return sqrtf(vec3_dot(v, v)); }

static uint8_t vec3_normalize(float v[3]) {
  const float len = vec3_len(v);
  if (!(len > 1.0e-6f)) {
    return 0u;
  }
  const float inv = 1.0f / len;
  v[0] *= inv;
  v[1] *= inv;
  v[2] *= inv;
  return 1u;
}

static float vec3_angle(const float a[3], const float b[3]) {
  float d = vec3_dot(a, b);
  if (d < -1.0f) {
    d = -1.0f;
  } else if (d > 1.0f) {
    d = 1.0f;
  }
  return acosf(d);
}

static void vec3_rotate_about_unit_axis(const float v[3], const float axis[3], float angle,
                                        float out[3]) {
  const float s = sinf(angle);
  const float c = cosf(angle);
  const float one_c = 1.0f - c;
  const float dot = vec3_dot(axis, v);
  float cross[3];
  vec3_cross(axis, v, cross);
  out[0] = v[0] * c + cross[0] * s + axis[0] * dot * one_c;
  out[1] = v[1] * c + cross[1] * s + axis[1] * dot * one_c;
  out[2] = v[2] * c + cross[2] * s + axis[2] * dot * one_c;
}

static void vec3_rotate_towards(float v[3], const float target[3], float step_angle) {
  const float angle = vec3_angle(v, target);
  if (!(angle > 1.0e-6f) || !(step_angle > 0.0f)) {
    return;
  }
  float axis[3];
  vec3_cross(v, target, axis);
  if (!vec3_normalize(axis)) {
    return;
  }
  const float step = (step_angle < angle) ? step_angle : angle;
  float out[3];
  vec3_rotate_about_unit_axis(v, axis, step, out);
  if (vec3_normalize(out)) {
    v[0] = out[0];
    v[1] = out[1];
    v[2] = out[2];
  }
}

static int dynamic_node_base_positions(const MslAnimDynSetData* set, uint8_t char_id, uint16_t msid,
                                       uint16_t frame, float out_pos[MSL_MAX_DYNAMIC_NODES][3]) {
  if (set == NULL || out_pos == NULL) {
    return -1;
  }
  for (uint16_t ni = 0; ni < set->node_count; ni++) {
    float m[12];
    if (anim_pose_get_matrix(char_id, msid, frame, set->nodes[ni].part_id, m) != 0) {
      return -1;
    }
    out_pos[ni][0] = m[3];
    out_pos[ni][1] = m[7];
    out_pos[ni][2] = m[11];
  }
  return 0;
}

static void dynamic_state_initialize_from_locals(MslBatch* batch, size_t idx,
                                                 const MslAnimPoseTable* t,
                                                 const MslAnimDynSetData* set, uint8_t char_id,
                                                 uint16_t msid, uint16_t frame) {
  if (batch == NULL || t == NULL || set == NULL) {
    return;
  }
  const uint16_t node_count = set->node_count;
  batch->state.dynamic_pose_state_valid[idx] = (node_count != 0u) ? 1u : 0u;
  batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
  batch->state.dynamic_pose_node_count[idx] = (uint8_t)node_count;
  batch->state.dynamic_pose_char_id[idx] = char_id;
  batch->state.dynamic_pose_msid[idx] = msid;
  batch->state.dynamic_pose_frame[idx] = frame;
  for (uint16_t ni = 0; ni < (uint16_t)MSL_MAX_DYNAMIC_NODES; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    batch->state.dynamic_pose_axis_x[di] = 1.0f;
    batch->state.dynamic_pose_axis_y[di] = 0.0f;
    batch->state.dynamic_pose_axis_z[di] = 0.0f;
    batch->state.dynamic_pose_angle[di] = 0.0f;
    if (ni >= node_count) {
      batch->state.dynamic_pose_rot_x[di] = 0.0f;
      batch->state.dynamic_pose_rot_y[di] = 0.0f;
      batch->state.dynamic_pose_rot_z[di] = 0.0f;
      batch->state.dynamic_pose_pos_x[di] = 0.0f;
      batch->state.dynamic_pose_pos_y[di] = 0.0f;
      batch->state.dynamic_pose_pos_z[di] = 0.0f;
      continue;
    }
    float rot[3], pos[3], scl[3];
    if (local_srt_for_part(t, msid, frame, set->nodes[ni].part_id, rot, pos, scl, NULL, NULL) !=
        0) {
      rot[0] = rot[1] = rot[2] = 0.0f;
    }
    const float max_bend = fabsf(set->nodes[ni].c[6]);
    float init_x = rot[0];
    if (ni + 1u < node_count) {
      if (init_x < -max_bend) {
        init_x = -max_bend;
      } else if (init_x > max_bend) {
        init_x = max_bend;
      }
    }
    batch->state.dynamic_pose_rot_x[di] = init_x;
    batch->state.dynamic_pose_rot_y[di] = rot[1];
    batch->state.dynamic_pose_rot_z[di] = rot[2];
    float m[12];
    if (anim_pose_get_matrix(char_id, msid, frame, set->nodes[ni].part_id, m) == 0) {
      batch->state.dynamic_pose_pos_x[di] = m[3];
      batch->state.dynamic_pose_pos_y[di] = m[7];
      batch->state.dynamic_pose_pos_z[di] = m[11];
    } else {
      batch->state.dynamic_pose_pos_x[di] = 0.0f;
      batch->state.dynamic_pose_pos_y[di] = 0.0f;
      batch->state.dynamic_pose_pos_z[di] = 0.0f;
    }
  }
}

static void dynamic_state_step(MslBatch* batch, size_t idx, const MslAnimPoseTable* t,
                               const MslAnimDynSetData* set, uint8_t char_id, uint16_t msid,
                               uint16_t frame) {
  if (batch == NULL || t == NULL || set == NULL || set->node_count == 0u) {
    return;
  }
  const uint16_t node_count = set->node_count;
  float base_pos[MSL_MAX_DYNAMIC_NODES][3];
  if (dynamic_node_base_positions(set, char_id, msid, frame, base_pos) != 0) {
    batch->state.dynamic_pose_state_valid[idx] = 0u;
    batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
    return;
  }

  float prev_pos[MSL_MAX_DYNAMIC_NODES][3];
  for (uint16_t ni = 0; ni < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    prev_pos[ni][0] = batch->state.dynamic_pose_pos_x[di];
    prev_pos[ni][1] = batch->state.dynamic_pose_pos_y[di];
    prev_pos[ni][2] = batch->state.dynamic_pose_pos_z[di];
  }

  for (uint16_t ni = 0; ni < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    float rot[3], pos[3], scl[3];
    if (local_srt_for_part(t, msid, frame, set->nodes[ni].part_id, rot, pos, scl, NULL, NULL) !=
        0) {
      continue;
    }
    batch->state.dynamic_pose_rot_x[di] = rot[0];
    batch->state.dynamic_pose_rot_y[di] = rot[1];
    batch->state.dynamic_pose_rot_z[di] = rot[2];
  }

  // Root node position is directly recomputed from the current JObj matrix each frame. Child node
  // positions are the lb_8001044C carry state.
  {
    const size_t root_di = dynamic_state_index(idx, 0u);
    batch->state.dynamic_pose_pos_x[root_di] = base_pos[0][0];
    batch->state.dynamic_pose_pos_y[root_di] = base_pos[0][1];
    batch->state.dynamic_pose_pos_z[root_di] = base_pos[0][2];
  }

  uint8_t apply_collision_pose = 0u;
  for (uint16_t ni = 0; ni + 1u < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    const size_t child_di = dynamic_state_index(idx, (uint16_t)(ni + 1u));

    float parent_pos[3] = {
        batch->state.dynamic_pose_pos_x[di],
        batch->state.dynamic_pose_pos_y[di],
        batch->state.dynamic_pose_pos_z[di],
    };
    float base_vec[3] = {
        base_pos[ni + 1u][0] - base_pos[ni][0],
        base_pos[ni + 1u][1] - base_pos[ni][1],
        base_pos[ni + 1u][2] - base_pos[ni][2],
    };
    const float seg_len = vec3_len(base_vec);
    if (!vec3_normalize(base_vec)) {
      batch->state.dynamic_pose_pos_x[child_di] = base_pos[ni + 1u][0];
      batch->state.dynamic_pose_pos_y[child_di] = base_pos[ni + 1u][1];
      batch->state.dynamic_pose_pos_z[child_di] = base_pos[ni + 1u][2];
      continue;
    }

    float prev_vec[3] = {
        prev_pos[ni + 1u][0] - parent_pos[0],
        prev_pos[ni + 1u][1] - parent_pos[1],
        prev_pos[ni + 1u][2] - parent_pos[2],
    };
    if (!vec3_normalize(prev_vec)) {
      prev_vec[0] = base_vec[0];
      prev_vec[1] = base_vec[1];
      prev_vec[2] = base_vec[2];
    }
    float original_prev[3] = {prev_vec[0], prev_vec[1], prev_vec[2]};

    // Ported shape from lb_8001044C:
    // - node +0x4C and descriptor +0x08 blend previous segment direction toward the current
    //   animation vector.
    // - node +0x8C applies the gravity/down-vector correction derived by lb_80011710 from
    //   descriptor +0x10 / segment length.
    // - node +0x38/+0x44 carries the prior angular correction axis/angle, then +0x84 decays it.
    // - node +0x68 clamps the segment direction cone around the current animation vector.
    const float anim_follow = set->nodes[ni].c[0] * set->pos[0];
    if (anim_follow < 1.0f) {
      vec3_rotate_towards(prev_vec, base_vec,
                          vec3_angle(prev_vec, base_vec) * (1.0f - anim_follow));
    }

    const float inv_len_corr = (seg_len > 1.0e-6f) ? (set->pos[2] / seg_len) : 0.0f;
    if (inv_len_corr != 0.0f) {
      const float down[3] = {0.0f, -1.0f, 0.0f};
      const float down_angle = vec3_angle(prev_vec, down);
      if (down_angle > 1.0e-6f) {
        vec3_rotate_towards(prev_vec, down, fabsf(sinf(down_angle) * inv_len_corr));
      }
    }

    const float carry_angle = batch->state.dynamic_pose_angle[di];
    if (fabsf(carry_angle) > 1.0e-6f) {
      float axis[3] = {
          batch->state.dynamic_pose_axis_x[di],
          batch->state.dynamic_pose_axis_y[di],
          batch->state.dynamic_pose_axis_z[di],
      };
      if (vec3_normalize(axis)) {
        float out[3];
        vec3_rotate_about_unit_axis(prev_vec, axis, carry_angle, out);
        if (vec3_normalize(out)) {
          prev_vec[0] = out[0];
          prev_vec[1] = out[1];
          prev_vec[2] = out[2];
        }
      }
    }

    const float snap_angle = fabsf(set->nodes[ni].c[1]);
    if (snap_angle > 0.0f) {
      const float angle_to_anim = vec3_angle(base_vec, prev_vec);
      if (angle_to_anim < snap_angle) {
        prev_vec[0] = base_vec[0];
        prev_vec[1] = base_vec[1];
        prev_vec[2] = base_vec[2];
      } else {
        vec3_rotate_towards(prev_vec, base_vec, snap_angle);
      }
    }

    const float cone = fabsf(set->nodes[ni].c[6]);
    const float cone_angle = vec3_angle(base_vec, prev_vec);
    if (cone > 0.0f && cone_angle > cone) {
      vec3_rotate_towards(prev_vec, base_vec, cone_angle - cone);
      apply_collision_pose = 1u;
    }

    float carry_axis[3];
    vec3_cross(original_prev, prev_vec, carry_axis);
    float next_carry = 0.0f;
    if (vec3_normalize(carry_axis)) {
      next_carry = vec3_angle(original_prev, prev_vec);
    } else {
      carry_axis[0] = 1.0f;
      carry_axis[1] = 0.0f;
      carry_axis[2] = 0.0f;
    }
    const float decay = fabsf(set->nodes[ni].c[13]);
    if (next_carry > decay) {
      next_carry -= decay;
    } else {
      next_carry = 0.0f;
    }
    if (next_carry > 1.0e-6f) {
      apply_collision_pose = 1u;
    }
    batch->state.dynamic_pose_axis_x[di] = carry_axis[0];
    batch->state.dynamic_pose_axis_y[di] = carry_axis[1];
    batch->state.dynamic_pose_axis_z[di] = carry_axis[2];
    batch->state.dynamic_pose_angle[di] = next_carry;

    batch->state.dynamic_pose_pos_x[child_di] = parent_pos[0] + prev_vec[0] * seg_len;
    batch->state.dynamic_pose_pos_y[child_di] = parent_pos[1] + prev_vec[1] * seg_len;
    batch->state.dynamic_pose_pos_z[child_di] = parent_pos[2] + prev_vec[2] * seg_len;
  }
  batch->state.dynamic_pose_state_valid[idx] = 1u;
  batch->state.dynamic_pose_apply_collision_matrix[idx] = apply_collision_pose;
  batch->state.dynamic_pose_node_count[idx] = (uint8_t)node_count;
  batch->state.dynamic_pose_char_id[idx] = char_id;
  batch->state.dynamic_pose_msid[idx] = msid;
  batch->state.dynamic_pose_frame[idx] = frame;
}

void anim_pose_update_dynamic_state(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t char_id = batch->state.char_id[idx];
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      const uint16_t msid = (uint16_t)anim_u32;
      const uint16_t frame =
          msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
      const MslAnimPoseTable* t = table_for_char(char_id);
      if (t == NULL || t->dyn_set_count == 0u) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      const MslAnimDynSetData* set = &t->dyn_sets[0];
      if (set->node_count == 0u || !t->local_have_msid || !t->local_have_msid[msid]) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }

      const uint8_t sequential = batch->state.dynamic_pose_state_valid[idx] &&
                                 batch->state.dynamic_pose_char_id[idx] == char_id &&
                                 batch->state.dynamic_pose_msid[idx] == msid &&
                                 (uint16_t)(batch->state.dynamic_pose_frame[idx] + 1u) == frame;
      if (!sequential) {
        dynamic_state_initialize_from_locals(batch, idx, t, set, char_id, msid, 0u);
        const uint16_t max_frame = (frame < t->local_frame_count_by_msid[msid])
                                       ? frame
                                       : (uint16_t)(t->local_frame_count_by_msid[msid] - 1u);
        for (uint16_t f = 0; f <= max_frame; f++) {
          dynamic_state_step(batch, idx, t, set, char_id, msid, f);
          if (f == 0xFFFFu) {
            break;
          }
        }
      } else {
        dynamic_state_step(batch, idx, t, set, char_id, msid, frame);
      }
    }
  }
}

static int dynamic_matrix_from_locals(const MslBatch* batch, size_t player_idx,
                                      const MslAnimPoseTable* t, const MslAnimDynSetData* set,
                                      uint16_t msid, uint16_t frame, uint16_t part_id,
                                      float out_3x4[12]) {
  enum { MAX_PATH = 96 };
  uint16_t path[MAX_PATH];
  uint16_t count = 0;
  uint16_t cur = part_id;
  for (;;) {
    if (count >= (uint16_t)MAX_PATH) {
      return -1;
    }
    float rot[3], pos[3], scl[3];
    int16_t parent = -1;
    if (local_srt_for_part(t, msid, frame, cur, rot, pos, scl, NULL, &parent) != 0) {
      return -1;
    }
    path[count++] = cur;
    if (parent < 0) {
      break;
    }
    cur = (uint16_t)parent;
  }

  float world[12];
  mtx34_identity(world);
  float world_scl_by_part[256][3];
  uint8_t have_scl[256] = {0};
  for (int i = (int)count - 1; i >= 0; i--) {
    const uint16_t part = path[i];
    float rot[3], pos[3], scl[3];
    uint32_t flags = 0;
    int16_t parent = -1;
    if (local_srt_for_part(t, msid, frame, part, rot, pos, scl, &flags, &parent) != 0) {
      return -1;
    }
    const int dyn_i = dynamic_set_node_index_for_part(set, part);

    const float* parent_scl = NULL;
    if (parent >= 0 && (uint16_t)parent < 256u && have_scl[(uint16_t)parent]) {
      parent_scl = world_scl_by_part[(uint16_t)parent];
    }
    float local[12];
    mtx34_srt_simple(rot, pos, scl, parent_scl, local);
    mtx34_concat(world, local, world);
    if (batch != NULL && dyn_i >= 0 && (uint16_t)dyn_i < (uint16_t)MSL_MAX_DYNAMIC_NODES &&
        batch->state.dynamic_pose_apply_collision_matrix[player_idx]) {
      const uint16_t dyn_u = (uint16_t)dyn_i;
      const size_t di = dynamic_state_index(player_idx, dyn_u);
      if (dyn_u + 1u < batch->state.dynamic_pose_node_count[player_idx]) {
        const size_t child_di = dynamic_state_index(player_idx, (uint16_t)(dyn_u + 1u));
        float base_parent[12], base_child[12];
        if (anim_pose_get_matrix(batch->state.dynamic_pose_char_id[player_idx], msid, frame, part,
                                 base_parent) == 0 &&
            anim_pose_get_matrix(batch->state.dynamic_pose_char_id[player_idx], msid, frame,
                                 set->nodes[dyn_u + 1u].part_id, base_child) == 0) {
          float base_dir[3] = {
              base_child[3] - base_parent[3],
              base_child[7] - base_parent[7],
              base_child[11] - base_parent[11],
          };
          float dyn_dir[3] = {
              batch->state.dynamic_pose_pos_x[child_di] - batch->state.dynamic_pose_pos_x[di],
              batch->state.dynamic_pose_pos_y[child_di] - batch->state.dynamic_pose_pos_y[di],
              batch->state.dynamic_pose_pos_z[child_di] - batch->state.dynamic_pose_pos_z[di],
          };
          if (vec3_normalize(base_dir) && vec3_normalize(dyn_dir)) {
            float axis[3];
            vec3_cross(base_dir, dyn_dir, axis);
            if (vec3_normalize(axis)) {
              const float angle = vec3_angle(base_dir, dyn_dir);
              if (angle > 1.0e-6f) {
                mtx34_apply_world_axis_angle(world, axis, angle);
              }
            }
          }
        }
      }
    }

    if ((flags & 8u) != 0u) {
      if (parent >= 0 && (uint16_t)parent < 256u && have_scl[(uint16_t)parent]) {
        memcpy(world_scl_by_part[part], world_scl_by_part[(uint16_t)parent], 3u * sizeof(float));
        have_scl[part] = 1u;
      } else {
        have_scl[part] = 0u;
      }
    } else {
      if (parent >= 0 && (uint16_t)parent < 256u && have_scl[(uint16_t)parent]) {
        world_scl_by_part[part][0] = scl[0] * world_scl_by_part[(uint16_t)parent][0];
        world_scl_by_part[part][1] = scl[1] * world_scl_by_part[(uint16_t)parent][1];
        world_scl_by_part[part][2] = scl[2] * world_scl_by_part[(uint16_t)parent][2];
      } else {
        memcpy(world_scl_by_part[part], scl, 3u * sizeof(float));
      }
      have_scl[part] = 1u;
    }
  }
  memcpy(out_3x4, world, MAT_BYTES);
  return 0;
}

int anim_pose_get_collision_matrix(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                   uint16_t frame, uint16_t part_id, float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  if (batch == NULL) {
    return -1;
  }
  const uint8_t char_id = batch->state.char_id[player_idx];
  if (anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4) != 0) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL || t->dyn_collision_have_msid == NULL || !t->dyn_collision_have_msid[msid]) {
    return 0;
  }
  const MslAnimDynSetData* set = dynamic_set_for_part(t, part_id);
  if (set == NULL || !batch->state.dynamic_pose_apply_collision_matrix[player_idx]) {
    return 0;
  }
  if (batch->state.dynamic_pose_char_id[player_idx] != char_id ||
      batch->state.dynamic_pose_msid[player_idx] != msid) {
    return 0;
  }
  float dyn[12];
  if (dynamic_matrix_from_locals(batch, player_idx, t, set, msid, frame, part_id, dyn) == 0) {
    memcpy(out_3x4, dyn, MAT_BYTES);
  }
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
  const uint64_t transn_off_u =
      (uint64_t)base_off + mats_bytes + frame_u * (uint64_t)TRANSN_BYTES_PER_FRAME;
  if (transn_off_u + (uint64_t)TRANSN_BYTES_PER_FRAME > (uint64_t)t->sz) {
    return -1;
  }

  memcpy(out_xyz, t->buf + (size_t)transn_off_u, (size_t)TRANSN_BYTES_PER_FRAME);
  return 0;
}
