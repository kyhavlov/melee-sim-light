#include "hitboxes_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  HITBOXES_MAGIC_LEN = 8,
  HITBOXES_HDR_BYTES = 16, // magic[8] + ver[u32] + entry_count[u32]
  HITBOXES_VERSION_V1 = 1,
  INDEX_REC_BYTES_V1 = 12, // msid[u16] + rec_count[u16] + rec_bytes[u32] + payload_off[u32]
  EVENT_REC_BYTES_V1 = 44, // packed event record size
};

static const uint8_t k_magic[HITBOXES_MAGIC_LEN] = {'M', 'S', 'L', 'H', 'I', 'T', 'B', '1'};

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct {
  MslHitboxEvent* events;
  uint32_t event_count;

  uint8_t* have_msid;            // [65536]
  uint16_t* count_by_msid;       // [65536]
  uint32_t* base_index_by_msid;  // [65536] (index into events[])

  uint8_t have;
} MslHitboxesTable;

static MslHitboxesTable g_table_by_char[256];
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

static void free_table(MslHitboxesTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->events);
  alloc_free(t->have_msid);
  alloc_free(t->count_by_msid);
  alloc_free(t->base_index_by_msid);
  *t = (MslHitboxesTable){0};
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

  if (sz < HITBOXES_HDR_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, HITBOXES_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)HITBOXES_VERSION_V1) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t entry_count_u32 = read_u32_le(buf + 12);
  if (entry_count_u32 == 0 || entry_count_u32 > 0xFFFFu) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t entry_count = (uint16_t)entry_count_u32;

  const size_t index_bytes = (size_t)entry_count * (size_t)INDEX_REC_BYTES_V1;
  const size_t index_end = (size_t)HITBOXES_HDR_BYTES + index_bytes;
  if (index_end > sz) {
    alloc_free(buf);
    return -1;
  }

  // Pre-count total event records and validate index entries.
  uint64_t total_events_u = 0;
  for (uint16_t i = 0; i < entry_count; i++) {
    const size_t off = (size_t)HITBOXES_HDR_BYTES + (size_t)i * (size_t)INDEX_REC_BYTES_V1;
    if (off + (size_t)INDEX_REC_BYTES_V1 > sz) {
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off + 0);
    (void)msid;
    const uint16_t rec_count = read_u16_le(buf + off + 2);
    const uint32_t rec_bytes = read_u32_le(buf + off + 4);
    const uint32_t payload_off = read_u32_le(buf + off + 8);
    if (rec_bytes != (uint32_t)rec_count * (uint32_t)EVENT_REC_BYTES_V1) {
      alloc_free(buf);
      return -1;
    }
    if ((size_t)payload_off < index_end) {
      alloc_free(buf);
      return -1;
    }
    if ((size_t)payload_off + (size_t)rec_bytes > sz) {
      alloc_free(buf);
      return -1;
    }
    total_events_u += (uint64_t)rec_count;
  }
  if (total_events_u == 0 || total_events_u > 0xFFFFFFFFu) {
    alloc_free(buf);
    return -1;
  }

  uint8_t* have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint16_t* count_by_msid = (uint16_t*)alloc_calloc(65536, sizeof(uint16_t));
  uint32_t* base_index_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  MslHitboxEvent* events = (MslHitboxEvent*)alloc_malloc((size_t)total_events_u * sizeof(MslHitboxEvent));
  if (have_msid == NULL || count_by_msid == NULL || base_index_by_msid == NULL || events == NULL) {
    alloc_free(have_msid);
    alloc_free(count_by_msid);
    alloc_free(base_index_by_msid);
    alloc_free(events);
    alloc_free(buf);
    return -1;
  }

  uint32_t out_base = 0;
  for (uint16_t i = 0; i < entry_count; i++) {
    const size_t off = (size_t)HITBOXES_HDR_BYTES + (size_t)i * (size_t)INDEX_REC_BYTES_V1;
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint16_t rec_count = read_u16_le(buf + off + 2);
    const uint32_t rec_bytes = read_u32_le(buf + off + 4);
    const uint32_t payload_off = read_u32_le(buf + off + 8);

    if (have_msid[msid]) {
      alloc_free(have_msid);
      alloc_free(count_by_msid);
      alloc_free(base_index_by_msid);
      alloc_free(events);
      alloc_free(buf);
      return -1;
    }
    have_msid[msid] = 1;
    count_by_msid[msid] = rec_count;
    base_index_by_msid[msid] = out_base;

    if ((uint64_t)out_base + (uint64_t)rec_count > total_events_u) {
      alloc_free(have_msid);
      alloc_free(count_by_msid);
      alloc_free(base_index_by_msid);
      alloc_free(events);
      alloc_free(buf);
      return -1;
    }

    // Decode records.
    const size_t base_off = (size_t)payload_off;
    if ((size_t)rec_bytes != (size_t)rec_count * (size_t)EVENT_REC_BYTES_V1) {
      alloc_free(have_msid);
      alloc_free(count_by_msid);
      alloc_free(base_index_by_msid);
      alloc_free(events);
      alloc_free(buf);
      return -1;
    }
    for (uint16_t ri = 0; ri < rec_count; ri++) {
      const size_t roff = base_off + (size_t)ri * (size_t)EVENT_REC_BYTES_V1;
      const uint8_t* r = buf + roff;
      const uint16_t frame = read_u16_le(r + 0);
      const uint8_t kind = r[2];
      const uint8_t hitbox_id = r[3];
      const uint32_t bone_part_id_u32 = read_u32_le(r + 4);
      if (bone_part_id_u32 > 0xFFFFu) {
        alloc_free(have_msid);
        alloc_free(count_by_msid);
        alloc_free(base_index_by_msid);
        alloc_free(events);
        alloc_free(buf);
        return -1;
      }

      MslHitboxEvent ev = {0};
      ev.frame = frame;
      ev.kind = kind;
      ev.hitbox_id = hitbox_id;
      ev.bone_part_id = (uint16_t)bone_part_id_u32;
      ev.x = read_f32_le(r + 8);
      ev.y = read_f32_le(r + 12);
      ev.z = read_f32_le(r + 16);
      ev.radius = read_f32_le(r + 20);
      ev.damage = read_f32_le(r + 24);

      ev.u16_0 = read_u16_le(r + 28);
      ev.u16_1 = read_u16_le(r + 30);
      ev.u16_2 = read_u16_le(r + 32);
      ev.u16_3 = read_u16_le(r + 34);
      ev.u16_4 = read_u16_le(r + 36);
      ev.u16_5 = read_u16_le(r + 38);
      ev.u16_6 = read_u16_le(r + 40);
      ev.u16_7 = read_u16_le(r + 42);

      events[(size_t)out_base + (size_t)ri] = ev;
    }

    out_base += (uint32_t)rec_count;
  }

  alloc_free(buf);

  // Replace any existing table for this character.
  if (g_table_by_char[char_id].have) {
    free_table(&g_table_by_char[char_id]);
  }
  g_table_by_char[char_id] = (MslHitboxesTable){
      .events = events,
      .event_count = (uint32_t)total_events_u,
      .have_msid = have_msid,
      .count_by_msid = count_by_msid,
      .base_index_by_msid = base_index_by_msid,
      .have = 1,
  };
  return 0;
}

int hitboxes_tables_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_for_char(data_dir, "hitboxes/fox.bin", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_for_char(data_dir, "hitboxes/falco.bin", MSL_CHAR_FALCO) != 0) {
    free_table(&g_table_by_char[MSL_CHAR_FOX]);
    return -1;
  }

  g_loaded = 1;
  return 0;
}

int hitboxes_get_events(uint8_t char_id, uint16_t msid, const MslHitboxEvent** out_events,
                        uint16_t* out_count) {
  if (out_events == NULL || out_count == NULL) {
    return -1;
  }
  *out_events = NULL;
  *out_count = 0;
  if (!g_loaded) {
    return -1;
  }

  const MslHitboxesTable* t = &g_table_by_char[char_id];
  if (!t->have || t->events == NULL || t->have_msid == NULL || t->count_by_msid == NULL ||
      t->base_index_by_msid == NULL) {
    return -1;
  }
  if (!t->have_msid[msid]) {
    return -1;
  }
  const uint16_t count = t->count_by_msid[msid];
  const uint32_t base = t->base_index_by_msid[msid];
  if ((uint64_t)base + (uint64_t)count > (uint64_t)t->event_count) {
    return -1;
  }

  *out_events = t->events + (size_t)base;
  *out_count = count;
  return 0;
}

