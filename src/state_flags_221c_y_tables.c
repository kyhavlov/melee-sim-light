#include "state_flags_221c_y_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  FLAGS_MAGIC_LEN = 8,
  FLAGS_HDR_BYTES =
      20,  // magic[8] + ver[u32] + frame_count[u16] + reserved[u16] + entry_count[u32]
  FLAGS_VERSION_V1 = 1,
  INDEX_REC_BYTES_V1 = 12,   // msid[u16] + reserved[u16] + payload_bytes[u32] + payload_off[u32]
  PAYLOAD_REC_BYTES_V1 = 1,  // u8 flags per frame (low 3 bits used)
};

static const uint8_t k_magic[FLAGS_MAGIC_LEN] = {'M', 'S', 'L', 'S', 'F', '3', 'Y', '1'};

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct {
  uint8_t* flags;  // [entry_count * frame_count]
  uint32_t flags_count;

  uint8_t* have_msid;            // [65536]
  uint32_t* base_index_by_msid;  // [65536] (index into flags[])

  uint16_t frame_count;
  uint8_t have;
} MslStateFlags221CYTable;

static MslStateFlags221CYTable g_table_by_char[256];
// 0 = uninitialized, 1 = loaded, -1 = optional artifact missing/unavailable.
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

static void free_table(MslStateFlags221CYTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->flags);
  alloc_free(t->have_msid);
  alloc_free(t->base_index_by_msid);
  *t = (MslStateFlags221CYTable){0};
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

  if (sz < FLAGS_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, FLAGS_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)FLAGS_VERSION_V1) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t frame_count = read_u16_le(buf + 12);
  const uint16_t reserved_hdr = read_u16_le(buf + 14);
  const uint32_t entry_count = read_u32_le(buf + 16);
  if (reserved_hdr != 0) {
    alloc_free(buf);
    return -1;
  }
  if (frame_count == 0 || entry_count == 0) {
    alloc_free(buf);
    return -1;
  }

  const size_t index_bytes = (size_t)entry_count * (size_t)INDEX_REC_BYTES_V1;
  const size_t index_end = (size_t)FLAGS_HDR_BYTES + index_bytes;
  if (index_end > sz) {
    alloc_free(buf);
    return -1;
  }

  const uint32_t want_payload_bytes = (uint32_t)frame_count * (uint32_t)PAYLOAD_REC_BYTES_V1;
  for (uint32_t i = 0; i < entry_count; i++) {
    const size_t off = (size_t)FLAGS_HDR_BYTES + (size_t)i * (size_t)INDEX_REC_BYTES_V1;
    if (off + (size_t)INDEX_REC_BYTES_V1 > sz) {
      alloc_free(buf);
      return -1;
    }
    const uint16_t _msid = read_u16_le(buf + off + 0);
    (void)_msid;
    const uint16_t reserved = read_u16_le(buf + off + 2);
    const uint32_t payload_bytes = read_u32_le(buf + off + 4);
    const uint32_t payload_off = read_u32_le(buf + off + 8);
    if (reserved != 0) {
      alloc_free(buf);
      return -1;
    }
    if (payload_bytes != want_payload_bytes) {
      alloc_free(buf);
      return -1;
    }
    if ((size_t)payload_off < index_end) {
      alloc_free(buf);
      return -1;
    }
    if ((size_t)payload_off + (size_t)payload_bytes > sz) {
      alloc_free(buf);
      return -1;
    }
  }

  uint8_t* have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint32_t* base_index_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  uint8_t* flags =
      (uint8_t*)alloc_malloc((size_t)entry_count * (size_t)frame_count * sizeof(uint8_t));
  if (have_msid == NULL || base_index_by_msid == NULL || flags == NULL) {
    alloc_free(have_msid);
    alloc_free(base_index_by_msid);
    alloc_free(flags);
    alloc_free(buf);
    return -1;
  }

  for (uint32_t i = 0; i < entry_count; i++) {
    const size_t off = (size_t)FLAGS_HDR_BYTES + (size_t)i * (size_t)INDEX_REC_BYTES_V1;
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint32_t payload_off = read_u32_le(buf + off + 8);

    if (have_msid[msid]) {
      alloc_free(have_msid);
      alloc_free(base_index_by_msid);
      alloc_free(flags);
      alloc_free(buf);
      return -1;
    }
    have_msid[msid] = 1;

    const uint32_t base = i * (uint32_t)frame_count;
    base_index_by_msid[msid] = base;
    if ((uint64_t)base + (uint64_t)frame_count > (uint64_t)entry_count * (uint64_t)frame_count) {
      alloc_free(have_msid);
      alloc_free(base_index_by_msid);
      alloc_free(flags);
      alloc_free(buf);
      return -1;
    }
    memcpy(flags + base, buf + (size_t)payload_off, (size_t)frame_count);
  }

  alloc_free(buf);

  if (g_table_by_char[char_id].have) {
    free_table(&g_table_by_char[char_id]);
  }

  g_table_by_char[char_id] = (MslStateFlags221CYTable){
      .flags = flags,
      .flags_count = (uint32_t)entry_count * (uint32_t)frame_count,
      .have_msid = have_msid,
      .base_index_by_msid = base_index_by_msid,
      .frame_count = frame_count,
      .have = 1,
  };
  return 0;
}

int state_flags_221c_y_tables_init(void) {
  if (g_load_state != 0) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_for_char(data_dir, "state_flags_221c_y/fox.bin", MSL_CHAR_FOX) != 0 ||
      load_for_char(data_dir, "state_flags_221c_y/falco.bin", MSL_CHAR_FALCO) != 0) {
    free_table(&g_table_by_char[MSL_CHAR_FOX]);
    free_table(&g_table_by_char[MSL_CHAR_FALCO]);
    g_load_state = -1;
    return 0;
  }

  g_load_state = 1;
  return 0;
}

int state_flags_221c_y_get(uint8_t char_id, uint16_t msid, uint16_t frame, uint8_t* out_flags) {
  if (out_flags == NULL) {
    return -1;
  }
  *out_flags = 0;
  if (g_load_state <= 0) {
    return 1;
  }

  const MslStateFlags221CYTable* t = &g_table_by_char[char_id];
  if (!t->have || t->flags == NULL || t->have_msid == NULL || t->base_index_by_msid == NULL ||
      t->frame_count == 0) {
    return 1;
  }
  if (!t->have_msid[msid]) {
    return 1;
  }

  uint16_t f = frame;
  if (f >= t->frame_count) {
    f = (uint16_t)(t->frame_count - 1);
  }
  const uint32_t base = t->base_index_by_msid[msid];
  if ((uint64_t)base + (uint64_t)f >= (uint64_t)t->flags_count) {
    return 1;
  }

  *out_flags = (uint8_t)(t->flags[base + (uint32_t)f] & 0x7u);
  return 0;
}
