#include "motion_state_owners.h"
#include "data_dir.h"
#include "char_registry.h"
#include "ids.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  TABLE_MAGIC_LEN = 8,
  TABLE_HDR_BYTES_V1 = 8 + 4 + 2 + 2 + 4 * 11,
};

static const uint8_t k_magic[TABLE_MAGIC_LEN] = {'M', 'S', 'L', 'M', 'S', 'O', '0', '1'};
static const uint32_t k_format_version = 18;

typedef struct {
  uint8_t* buf;
  size_t sz;
  uint16_t action_count;
  uint8_t* submotion_id_by_action;
  uint8_t* x4_flags_by_action;
  uint8_t* motion_word_by_action;
  uint8_t* anim_cb_by_action;
  uint8_t* iasa_cb_by_action;
  uint8_t* phys_cb_by_action;
  uint8_t* coll_cb_by_action;
  uint8_t* cam_cb_by_action;
  uint8_t* class_bits_by_action;
  uint8_t* class2_bits_by_action;
  uint8_t* class3_bits_by_action;
  uint8_t* fx_special_kind_by_action;
  uint8_t have;
} MslMotionStateOwnerTable;

static MslMotionStateOwnerTable g_table_by_char[256];
const uint8_t* msl_motion_state_fx_special_kind_by_char[256];
uint16_t msl_motion_state_action_count_by_char[256];
uint32_t msl_motion_state_common_class_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP];
uint32_t msl_motion_state_common_class2_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP];
uint32_t msl_motion_state_common_class3_bits_by_action[MSL_MOTION_STATE_COMMON_ACTION_CAP];
// 0 = not attempted, 1 = loaded ok, -1 = attempted and failed.
static int g_load_state = 0;

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

static const char* data_dir_or_default(void) {
  const char* data_dir = msl_data_dir();
  return data_dir;
}

static void print_generate_hint(const char* data_dir) {
  const char* dd = (data_dir != NULL && data_dir[0] != '\0') ? data_dir : "data";
  fprintf(stderr,
          "msl: generate a complete data root with:\n"
          "  uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso --out-dir %s\n",
          dd);
}

uint32_t motion_state_owners_format_version(void) { return k_format_version; }

static int load_file_buf(const char* path, uint8_t** out_buf, size_t* out_sz, int* out_errno) {
  if (path == NULL || out_buf == NULL || out_sz == NULL) {
    return -1;
  }
  *out_buf = NULL;
  *out_sz = 0;

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    return -2;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    alloc_free(buf);
    return -3;
  }

  *out_buf = buf;
  *out_sz = sz;
  return 0;
}

static uint8_t range_ok(uint32_t off, uint32_t bytes, size_t sz) {
  return (off <= (uint32_t)sz && bytes <= (uint32_t)sz && off <= (uint32_t)sz - bytes) ? 1u : 0u;
}

static int load_table_for_char_into(uint8_t char_id, const char* rel_name,
                                    MslMotionStateOwnerTable* out) {
  if (out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));

  const char* data_dir = data_dir_or_default();
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/motion_state/owners/%s.bin", data_dir, rel_name);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    fprintf(stderr, "msl: motion-state owner table path too long (MSL_DATA_DIR=%s)\n", data_dir);
    print_generate_hint(data_dir);
    return -1;
  }

  uint8_t* buf = NULL;
  size_t sz = 0;
  int open_errno = 0;
  const int load_rc = load_file_buf(path, &buf, &sz, &open_errno);
  if (load_rc != 0) {
    if (load_rc == -2) {
      fprintf(stderr, "msl: could not open motion-state owner table for char_id=%u: %s (%s)\n",
              (unsigned)char_id, path, strerror(open_errno));
    } else {
      fprintf(stderr, "msl: failed to read motion-state owner table for char_id=%u: %s (%s)\n",
              (unsigned)char_id, path, strerror(open_errno));
    }
    fprintf(stderr, "msl: set MSL_DATA_DIR to point at the extracted data root if needed\n");
    print_generate_hint(data_dir);
    return -1;
  }
  if (sz < (size_t)TABLE_HDR_BYTES_V1) {
    fprintf(stderr, "msl: motion-state owner table header too small for char_id=%u: %s (sz=%zu)\n",
            (unsigned)char_id, path, sz);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, TABLE_MAGIC_LEN) != 0) {
    fprintf(stderr, "msl: motion-state owner table bad magic for char_id=%u: %s\n",
            (unsigned)char_id, path);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != k_format_version) {
    fprintf(stderr,
            "msl: motion-state owner table bad version for char_id=%u: %s (got=%u expected=%u)\n",
            (unsigned)char_id, path, (unsigned)ver, (unsigned)k_format_version);
    fprintf(stderr,
            "msl: table/runtime schema mismatch; rebuild or reinstall melee-sim-light, then "
            "regenerate .msl if needed\n");
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }

  const uint16_t action_count = read_u16_le(buf + 12);
  const uint32_t submotion_off = read_u32_le(buf + 16);
  const uint32_t x4_flags_off = read_u32_le(buf + 20);
  const uint32_t motion_word_off = read_u32_le(buf + 24);
  const uint32_t anim_cb_off = read_u32_le(buf + 28);
  const uint32_t iasa_cb_off = read_u32_le(buf + 32);
  const uint32_t phys_cb_off = read_u32_le(buf + 36);
  const uint32_t coll_cb_off = read_u32_le(buf + 40);
  const uint32_t cam_cb_off = read_u32_le(buf + 44);
  const uint32_t class_bits_off = read_u32_le(buf + 48);
  const uint32_t class2_bits_off = read_u32_le(buf + 52);
  const uint32_t class3_bits_off = read_u32_le(buf + 56);
  const uint32_t fx_special_kind_off = read_u32_le(buf + 60);
  const uint32_t file_bytes = fx_special_kind_off + (uint32_t)action_count;

  if (file_bytes != (uint32_t)sz) {
    fprintf(stderr,
            "msl: motion-state owner table file size mismatch for char_id=%u: %s "
            "(derived=%u actual=%zu)\n",
            (unsigned)char_id, path, (unsigned)file_bytes, sz);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  const uint32_t u16_bytes = (uint32_t)action_count * 2u;
  const uint32_t u32_bytes = (uint32_t)action_count * 4u;
  if (!range_ok(submotion_off, u16_bytes, sz) || !range_ok(x4_flags_off, u32_bytes, sz) ||
      !range_ok(motion_word_off, u32_bytes, sz) || !range_ok(anim_cb_off, u16_bytes, sz) ||
      !range_ok(iasa_cb_off, u16_bytes, sz) || !range_ok(phys_cb_off, u16_bytes, sz) ||
      !range_ok(coll_cb_off, u16_bytes, sz) || !range_ok(cam_cb_off, u16_bytes, sz) ||
      !range_ok(class_bits_off, u32_bytes, sz) || !range_ok(class2_bits_off, u32_bytes, sz) ||
      !range_ok(class3_bits_off, u32_bytes, sz) ||
      !range_ok(fx_special_kind_off, (uint32_t)action_count, sz)) {
    fprintf(stderr, "msl: motion-state owner table bad offsets for char_id=%u: %s\n",
            (unsigned)char_id, path);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }

  out->buf = buf;
  out->sz = sz;
  out->action_count = action_count;
  out->submotion_id_by_action = buf + submotion_off;
  out->x4_flags_by_action = buf + x4_flags_off;
  out->motion_word_by_action = buf + motion_word_off;
  out->anim_cb_by_action = buf + anim_cb_off;
  out->iasa_cb_by_action = buf + iasa_cb_off;
  out->phys_cb_by_action = buf + phys_cb_off;
  out->coll_cb_by_action = buf + coll_cb_off;
  out->cam_cb_by_action = buf + cam_cb_off;
  out->class_bits_by_action = buf + class_bits_off;
  out->class2_bits_by_action = buf + class2_bits_off;
  out->class3_bits_by_action = buf + class3_bits_off;
  out->fx_special_kind_by_action = buf + fx_special_kind_off;
  out->have = 1u;
  return 0;
}

int motion_state_owners_init(void) {
  if (g_load_state == 1) {
    return 0;
  }
  if (g_load_state == -1) {
    return -1;
  }
  int load_failed = 0;
  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    const uint8_t char_id = MSL_CHAR_REGISTRY[ci].char_id;
    if (load_table_for_char_into(char_id, MSL_CHAR_REGISTRY[ci].name, &g_table_by_char[char_id]) !=
        0) {
      load_failed = 1;
      break;
    }
    msl_motion_state_fx_special_kind_by_char[char_id] =
        g_table_by_char[char_id].fx_special_kind_by_action;
    msl_motion_state_action_count_by_char[char_id] = g_table_by_char[char_id].action_count;
  }
  if (load_failed) {
    g_load_state = -1;
    return -1;
  }
  for (uint16_t action = 0; action < (uint16_t)MSL_MOTION_STATE_COMMON_ACTION_CAP; action++) {
    uint32_t class_bits = UINT32_MAX;
    uint32_t class2_bits = UINT32_MAX;
    uint32_t class3_bits = UINT32_MAX;
    for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
      const uint8_t char_id = MSL_CHAR_REGISTRY[ci].char_id;
      const MslMotionStateOwnerTable* t = &g_table_by_char[char_id];
      if (t == NULL || action >= t->action_count) {
        class_bits = 0u;
        class2_bits = 0u;
        class3_bits = 0u;
        break;
      }
      class_bits &= read_u32_le(t->class_bits_by_action + (size_t)action * 4u);
      class2_bits &= read_u32_le(t->class2_bits_by_action + (size_t)action * 4u);
      class3_bits &= read_u32_le(t->class3_bits_by_action + (size_t)action * 4u);
    }
    msl_motion_state_common_class_bits_by_action[action] = class_bits;
    msl_motion_state_common_class2_bits_by_action[action] = class2_bits;
    msl_motion_state_common_class3_bits_by_action[action] = class3_bits;
  }
  g_load_state = 1;
  return 0;
}

static const MslMotionStateOwnerTable* table_for_char(uint8_t char_id) {
  const MslMotionStateOwnerTable* t = &g_table_by_char[char_id];
  return t->have ? t : NULL;
}

static uint8_t in_range(const MslMotionStateOwnerTable* t, uint16_t action_id) {
  return (t != NULL && action_id < t->action_count) ? 1u : 0u;
}

uint16_t msl_motion_state_submotion_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->submotion_id_by_action + (size_t)action_id * 2u)
                                : 0xFFFFu;
}

uint32_t msl_motion_state_x4_flags(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u32_le(t->x4_flags_by_action + (size_t)action_id * 4u) : 0u;
}

uint32_t msl_motion_state_word(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u32_le(t->motion_word_by_action + (size_t)action_id * 4u)
                                : 0u;
}

uint16_t msl_motion_state_anim_cb_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->anim_cb_by_action + (size_t)action_id * 2u) : 0u;
}

uint16_t msl_motion_state_iasa_cb_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->iasa_cb_by_action + (size_t)action_id * 2u) : 0u;
}

uint16_t msl_motion_state_phys_cb_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->phys_cb_by_action + (size_t)action_id * 2u) : 0u;
}

uint16_t msl_motion_state_coll_cb_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->coll_cb_by_action + (size_t)action_id * 2u) : 0u;
}

uint16_t msl_motion_state_cam_cb_id(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  return in_range(t, action_id) ? read_u16_le(t->cam_cb_by_action + (size_t)action_id * 2u) : 0u;
}

uint8_t msl_motion_state_has_motion_flag(uint8_t char_id, uint16_t action_id, uint32_t flag_mask) {
  if (flag_mask == 0u) {
    return 0u;
  }
  return ((msl_motion_state_x4_flags(char_id, action_id) & flag_mask) == flag_mask) ? 1u : 0u;
}

uint32_t msl_motion_state_class_bits(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  if (!in_range(t, action_id)) {
    return 0u;
  }
  return read_u32_le(t->class_bits_by_action + (size_t)action_id * 4u);
}

uint8_t msl_motion_state_class_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit) {
  if (class_bit == 0u) {
    return 0u;
  }
  return ((msl_motion_state_class_bits(char_id, action_id) & class_bit) != 0u) ? 1u : 0u;
}

uint32_t msl_motion_state_class2_bits(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  if (!in_range(t, action_id)) {
    return 0u;
  }
  return read_u32_le(t->class2_bits_by_action + (size_t)action_id * 4u);
}

uint8_t msl_motion_state_class2_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit) {
  if (class_bit == 0u) {
    return 0u;
  }
  return ((msl_motion_state_class2_bits(char_id, action_id) & class_bit) != 0u) ? 1u : 0u;
}

uint32_t msl_motion_state_class3_bits(uint8_t char_id, uint16_t action_id) {
  const MslMotionStateOwnerTable* t = table_for_char(char_id);
  if (!in_range(t, action_id)) {
    return 0u;
  }
  return read_u32_le(t->class3_bits_by_action + (size_t)action_id * 4u);
}

uint8_t msl_motion_state_fx_special_kind(uint8_t char_id, uint16_t action_id) {
  return msl_motion_state_fx_special_kind_fast(char_id, action_id);
}

uint8_t msl_motion_state_class3_has(uint8_t char_id, uint16_t action_id, uint32_t class_bit) {
  if (class_bit == 0u) {
    return 0u;
  }
  return ((msl_motion_state_class3_bits(char_id, action_id) & class_bit) != 0u) ? 1u : 0u;
}

uint8_t msl_motion_state_common_class_has(uint16_t action_id, uint32_t class_bit) {
  return msl_motion_state_common_class_has_fast(action_id, class_bit);
}

uint8_t msl_motion_state_common_class2_has(uint16_t action_id, uint32_t class_bit) {
  return msl_motion_state_common_class2_has_fast(action_id, class_bit);
}

uint8_t msl_motion_state_common_class3_has(uint16_t action_id, uint32_t class_bit) {
  return msl_motion_state_common_class3_has_fast(action_id, class_bit);
}
