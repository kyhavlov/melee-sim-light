#include "move_tables.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "action_ids.h"
#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct MslFrameWindow {
  int16_t start_af;  // inclusive (0-based action_frame)
  int16_t end_af;    // exclusive (0-based action_frame)
  uint8_t loaded;
} MslFrameWindow;

typedef struct MslThrowRelease {
  int16_t release_af;  // inclusive (0-based action_frame)
  uint8_t hit_idx;
  uint8_t loaded;
} MslThrowRelease;

typedef struct MslThrowHitbox {
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  uint8_t sfx_kind;
  uint8_t sfx_severity;
  uint8_t loaded;
} MslThrowHitbox;

typedef struct MslSmashChargeInfo {
  int16_t start_af;     // inclusive 0-based command frame
  float damage_mul;     // ftCo_800DEE84 damage_mul / smash_attrs.x2120_damageMul
  uint8_t hold_frames;  // ftCo_800DEE84 arg2 / smash_attrs.x211C_holdFrame
  uint8_t loaded;
} MslSmashChargeInfo;

enum { MSL_FRAME_PULSES_MAX = 16 };
typedef struct MslFramePulses {
  int16_t frame[MSL_FRAME_PULSES_MAX];
  uint8_t count;
  uint8_t loaded;
} MslFramePulses;

enum { MSL_ATTACKAIR_KIND_COUNT = 5 };
enum {
  MSL_ATTACKAIR_KIND_N = 0,
  MSL_ATTACKAIR_KIND_F = 1,
  MSL_ATTACKAIR_KIND_B = 2,
  MSL_ATTACKAIR_KIND_HI = 3,
  MSL_ATTACKAIR_KIND_LW = 4,
};

static MslFrameWindow g_cmd0_by_char_attackair[256][MSL_ATTACKAIR_KIND_COUNT];
static MslFrameWindow g_allow_interrupt_by_char_attackair[256][MSL_ATTACKAIR_KIND_COUNT];
enum { MSL_GROUNDED_ATTACK_KIND_COUNT = 10 };
enum {
  MSL_GROUNDED_ATTACK_KIND_11 = 0,
  MSL_GROUNDED_ATTACK_KIND_12 = 1,
  MSL_GROUNDED_ATTACK_KIND_13 = 2,
  MSL_GROUNDED_ATTACK_KIND_DASH = 3,
  MSL_GROUNDED_ATTACK_KIND_S3 = 4,
  MSL_GROUNDED_ATTACK_KIND_HI3 = 5,
  MSL_GROUNDED_ATTACK_KIND_LW3 = 6,
  MSL_GROUNDED_ATTACK_KIND_S4 = 7,
  MSL_GROUNDED_ATTACK_KIND_HI4 = 8,
  MSL_GROUNDED_ATTACK_KIND_LW4 = 9,
};
static MslFrameWindow g_allow_interrupt_by_char_grounded_attack[256]
                                                               [MSL_GROUNDED_ATTACK_KIND_COUNT];
static MslSmashChargeInfo g_smash_charge_by_char_grounded_attack[256]
                                                                [MSL_GROUNDED_ATTACK_KIND_COUNT];
static MslFrameWindow g_allow_interrupt_by_char_escape_n[256];
static MslFrameWindow g_throw_flags_by_char_escape_f[256];
static MslFrameWindow g_jab_combo_by_char_grounded_attack[256][MSL_GROUNDED_ATTACK_KIND_COUNT];
static MslFrameWindow g_jab_rapid_by_char_grounded_attack[256][MSL_GROUNDED_ATTACK_KIND_COUNT];
static MslFramePulses g_attack100_loop_end_check_by_char[256];
static MslFrameWindow g_cmd0_by_char_dash[256];
static MslFrameWindow g_cmd0_by_char_runbrake[256];
static MslFrameWindow g_cmd0_by_char_escapeair[256];
static MslFrameWindow g_throw_flags_by_char_catch[256];
static MslFrameWindow g_throw_flags_by_char_catchdash[256];
static MslFrameWindow g_catchattack_grabbed_hit_by_char[256];
enum { MSL_THROW_KIND_COUNT = 4 };
enum {
  MSL_THROW_KIND_F = 0,
  MSL_THROW_KIND_B = 1,
  MSL_THROW_KIND_HI = 2,
  MSL_THROW_KIND_LW = 3,
};
enum { MSL_THROW_HITBOX_IDX_MAX = 8 };
static MslThrowRelease g_throw_release_by_char[256][MSL_THROW_KIND_COUNT];
static MslFrameWindow g_throw_flip_by_char[256][MSL_THROW_KIND_COUNT];
static MslFrameWindow g_throw_cmd1_by_char[256][MSL_THROW_KIND_COUNT];
static MslFramePulses g_throw_spawn_projectile_by_char[256][MSL_THROW_KIND_COUNT];
static MslThrowHitbox g_throw_hitbox_by_char[256][MSL_THROW_KIND_COUNT][MSL_THROW_HITBOX_IDX_MAX];
enum { MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX = 64 };
typedef struct MslPseudoRandomSfxByMsid {
  uint16_t msid;
  MslFramePulses pulses;
  uint8_t random_range[MSL_FRAME_PULSES_MAX];
} MslPseudoRandomSfxByMsid;
typedef struct MslSpecialCmd0ByMsid {
  uint16_t msid;
  MslFrameWindow window;
} MslSpecialCmd0ByMsid;
static MslPseudoRandomSfxByMsid g_special_pseudo_rng_by_char[256]
                                                            [MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX];
static uint8_t g_special_pseudo_rng_count_by_char[256];
static MslSpecialCmd0ByMsid g_special_cmd0_by_char[256][MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX];
static uint8_t g_special_cmd0_count_by_char[256];
enum { MSL_SPECIAL_CMD0_LATCH_CLEAR_TAIL_FRAMES = 2 };
static int g_loaded = 0;

enum {
  MSLFTSC1_VERSION = 1,
  MSLFTSC1_HEADER_SIZE = 28,
  MSLFTSC1_INDEX_RECORD_SIZE = 12,
  MSLFTSC1_EVENT_HEADER_SIZE = 8,
  MSL_SPECIAL_MSID_FIRST = 295,
};

enum {
  MSL_SCRIPT_EVENT_CREATE_HITBOX = 1,
  MSL_SCRIPT_EVENT_CLEAR_HITBOXES = 6,
  MSL_SCRIPT_EVENT_SET_CMD_VAR = 7,
  MSL_SCRIPT_EVENT_SET_THROW_FLAGS = 8,
  MSL_SCRIPT_EVENT_ALLOW_INTERRUPT = 9,
  MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE = 10,
  MSL_SCRIPT_EVENT_SET_JAB_COMBO = 15,
  MSL_SCRIPT_EVENT_SET_JAB_RAPID = 16,
  MSL_SCRIPT_EVENT_START_SMASH_CHARGE = 19,
  MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX = 20,
  MSL_SCRIPT_EVENT_SET_THROW_HITBOX = 21,
};

typedef struct MslScriptEntryRaw {
  uint16_t msid;
  uint32_t first_event;
  uint32_t event_count;
} MslScriptEntryRaw;

typedef struct MslScriptEventRaw {
  uint16_t frame;
  uint16_t kind_id;
  const char* payload;
  uint32_t payload_len;
} MslScriptEventRaw;

typedef struct MslScriptTableRaw {
  uint8_t* file_buf;
  size_t file_size;
  MslScriptEntryRaw* entries;
  MslScriptEventRaw* events;
  uint32_t entry_count;
  uint32_t event_count;
} MslScriptTableRaw;

static uint16_t read_le_u16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int script_entry_cmp_msid(const void* a, const void* b) {
  const MslScriptEntryRaw* ea = (const MslScriptEntryRaw*)a;
  const MslScriptEntryRaw* eb = (const MslScriptEntryRaw*)b;
  return (ea->msid > eb->msid) - (ea->msid < eb->msid);
}

static void script_table_raw_free(MslScriptTableRaw* table) {
  if (table == NULL) {
    return;
  }
  alloc_free(table->events);
  alloc_free(table->entries);
  alloc_free(table->file_buf);
  *table = (MslScriptTableRaw){0};
}

static int script_table_raw_load(const char* data_dir, const char* rel_path,
                                 MslScriptTableRaw* out) {
  if (data_dir == NULL || rel_path == NULL || out == NULL) {
    return -1;
  }
  *out = (MslScriptTableRaw){0};

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
  if (sz < MSLFTSC1_HEADER_SIZE) {
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

  if (memcmp(buf, "MSLFTSC1", 8) != 0 || read_le_u32(buf + 8) != (uint32_t)MSLFTSC1_VERSION) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t entry_count = read_le_u32(buf + 12);
  const uint32_t event_count = read_le_u32(buf + 16);
  const uint32_t index_off = read_le_u32(buf + 20);
  const uint32_t event_off = read_le_u32(buf + 24);
  const uint64_t expected_event_off =
      (uint64_t)MSLFTSC1_HEADER_SIZE + (uint64_t)entry_count * MSLFTSC1_INDEX_RECORD_SIZE;
  if (index_off != (uint32_t)MSLFTSC1_HEADER_SIZE || event_off != expected_event_off ||
      event_off > (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }

  MslScriptEntryRaw* entries =
      (MslScriptEntryRaw*)alloc_calloc((size_t)entry_count, sizeof(MslScriptEntryRaw));
  MslScriptEventRaw* events =
      (MslScriptEventRaw*)alloc_calloc((size_t)event_count, sizeof(MslScriptEventRaw));
  if (entries == NULL || events == NULL) {
    alloc_free(events);
    alloc_free(entries);
    alloc_free(buf);
    return -1;
  }

  for (uint32_t i = 0; i < entry_count; i++) {
    const uint8_t* rec = buf + index_off + (size_t)i * MSLFTSC1_INDEX_RECORD_SIZE;
    entries[i].msid = read_le_u16(rec);
    entries[i].first_event = read_le_u32(rec + 4);
    entries[i].event_count = read_le_u32(rec + 8);
    if (entries[i].first_event > event_count ||
        entries[i].event_count > event_count - entries[i].first_event) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
  }

  size_t off = (size_t)event_off;
  for (uint32_t i = 0; i < event_count; i++) {
    if (off + MSLFTSC1_EVENT_HEADER_SIZE > (size_t)sz) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    const uint8_t* ev = buf + off;
    const uint32_t payload_len = read_le_u32(ev + 4);
    if (payload_len > (uint32_t)((size_t)sz - off - MSLFTSC1_EVENT_HEADER_SIZE)) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    events[i].frame = read_le_u16(ev);
    events[i].kind_id = read_le_u16(ev + 2);
    events[i].payload_len = payload_len;
    events[i].payload = (const char*)(ev + MSLFTSC1_EVENT_HEADER_SIZE);
    off += MSLFTSC1_EVENT_HEADER_SIZE + (size_t)payload_len;
  }
  if (off != (size_t)sz) {
    alloc_free(events);
    alloc_free(entries);
    alloc_free(buf);
    return -1;
  }

  qsort(entries, (size_t)entry_count, sizeof(entries[0]), script_entry_cmp_msid);
  out->file_buf = buf;
  out->file_size = (size_t)sz;
  out->entries = entries;
  out->events = events;
  out->entry_count = entry_count;
  out->event_count = event_count;
  return 0;
}

static const MslScriptEntryRaw* script_table_raw_find_entry(const MslScriptTableRaw* table,
                                                            uint16_t msid) {
  if (table == NULL || table->entries == NULL) {
    return NULL;
  }
  size_t lo = 0;
  size_t hi = table->entry_count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const uint16_t got = table->entries[mid].msid;
    if (got == msid) {
      return &table->entries[mid];
    }
    if (got < msid) {
      lo = mid + 1u;
    } else {
      hi = mid;
    }
  }
  return NULL;
}

static const char* json_skip_ws(const char* s) {
  while (s && *s && isspace((unsigned char)*s)) {
    s++;
  }
  return s;
}

static const char* json_parse_int(const char* s, int* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  char* end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (end == s || errno != 0) {
    return NULL;
  }
  if (out) {
    *out = (int)v;
  }
  return end;
}

static const char* json_parse_f32(const char* s, float* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  char* end = NULL;
  errno = 0;
  const double v = strtod(s, &end);
  if (end == s || errno != 0) {
    return NULL;
  }
  if (out) {
    *out = (float)v;
  }
  return end;
}

static const char* strstr_range(const char* hay, const char* hay_end, const char* needle) {
  if (hay == NULL || hay_end == NULL || needle == NULL) {
    return NULL;
  }
  const size_t nlen = strlen(needle);
  if (nlen == 0) {
    return hay;
  }
  for (const char* p = hay; p + nlen <= hay_end; p++) {
    if (*p == *needle && memcmp(p, needle, nlen) == 0) {
      return p;
    }
  }
  return NULL;
}

static int json_get_i32_in_range(const char* start, const char* end, const char* key, int* out) {
  if (start == NULL || end == NULL || key == NULL || out == NULL || start >= end) {
    return -1;
  }
  char pat[128];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat)) {
    return -1;
  }
  const char* p = strstr_range(start, end, pat);
  if (p == NULL) {
    return -1;
  }
  p = (const char*)memchr(p, ':', (size_t)(end - p));
  if (p == NULL) {
    return -1;
  }
  p++;
  int v = 0;
  if (json_parse_int(p, &v) == NULL) {
    return -1;
  }
  *out = v;
  return 0;
}

static int json_get_f32_in_range(const char* start, const char* end, const char* key, float* out) {
  if (start == NULL || end == NULL || key == NULL || out == NULL || start >= end) {
    return -1;
  }
  char pat[128];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat)) {
    return -1;
  }
  const char* p = strstr_range(start, end, pat);
  if (p == NULL) {
    return -1;
  }
  p = (const char*)memchr(p, ':', (size_t)(end - p));
  if (p == NULL) {
    return -1;
  }
  p++;
  float v = 0.0f;
  if (json_parse_f32(p, &v) == NULL) {
    return -1;
  }
  *out = v;
  return 0;
}

static int json_get_bool_in_range(const char* start, const char* end, const char* key, int* out) {
  if (start == NULL || end == NULL || key == NULL || out == NULL || start >= end) {
    return -1;
  }
  char pat[128];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat)) {
    return -1;
  }
  const char* p = strstr_range(start, end, pat);
  if (p == NULL) {
    return -1;
  }
  p = (const char*)memchr(p, ':', (size_t)(end - p));
  if (p == NULL) {
    return -1;
  }
  p = json_skip_ws(p + 1);
  if (p == NULL || p >= end) {
    return -1;
  }
  if ((size_t)(end - p) >= 4 && memcmp(p, "true", 4) == 0) {
    *out = 1;
    return 0;
  }
  if ((size_t)(end - p) >= 5 && memcmp(p, "false", 5) == 0) {
    *out = 0;
    return 0;
  }
  return -1;
}

static void frame_pulses_push(MslFramePulses* out, int frame) {
  if (out == NULL) {
    return;
  }
  if (out->count >= (uint8_t)MSL_FRAME_PULSES_MAX) {
    return;
  }
  // Keep deterministic order and avoid duplicates at the same frame.
  for (uint8_t i = 0; i < out->count; i++) {
    if ((int)out->frame[i] == frame) {
      return;
    }
  }
  out->frame[out->count] = (int16_t)frame;
  out->count = (uint8_t)(out->count + 1u);
}

static const char* script_event_payload_end(const MslScriptEventRaw* ev) {
  if (ev == NULL || ev->payload == NULL) {
    return NULL;
  }
  return ev->payload + ev->payload_len;
}

static const MslScriptEventRaw* script_entry_event(const MslScriptTableRaw* table,
                                                   const MslScriptEntryRaw* entry, uint32_t i) {
  if (table == NULL || entry == NULL || i >= entry->event_count ||
      entry->first_event + i >= table->event_count) {
    return NULL;
  }
  return &table->events[entry->first_event + i];
}

static int parse_entry_cmd0_window(const MslScriptTableRaw* table, const MslScriptEntryRaw* entry,
                                   uint8_t open_end, MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  int on_frame = -1;
  int off_frame = -1;
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_SET_CMD_VAR) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    int idx = -1;
    int value = 0;
    if (json_get_i32_in_range(ev->payload, end, "idx", &idx) == 0 &&
        json_get_i32_in_range(ev->payload, end, "value", &value) == 0 && idx == 0) {
      if (value != 0 && on_frame < 0) {
        on_frame = (int)ev->frame;
      } else if (value == 0 && on_frame >= 0 && off_frame < 0) {
        off_frame = (int)ev->frame;
      }
    }
  }
  if (on_frame < 0) {
    return -1;
  }
  if (off_frame < 0) {
    if (!open_end) {
      return -1;
    }
    off_frame = INT16_MAX;
  }
  if (off_frame < on_frame) {
    return -1;
  }
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)off_frame;
  out->loaded = 1;
  return 0;
}

static int parse_entry_allow_interrupt_window(const MslScriptTableRaw* table,
                                              const MslScriptEntryRaw* entry, MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev != NULL && ev->kind_id == MSL_SCRIPT_EVENT_ALLOW_INTERRUPT) {
      out->start_af = (int16_t)ev->frame;
      out->end_af = (int16_t)INT16_MAX;
      out->loaded = 1;
      return 0;
    }
  }
  return -1;
}

static int parse_entry_start_smash_charge_info(const MslScriptTableRaw* table,
                                               const MslScriptEntryRaw* entry,
                                               MslSmashChargeInfo* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_START_SMASH_CHARGE) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    int hold_frames = 0;
    float damage_mul = 0.0f;
    if (json_get_i32_in_range(ev->payload, end, "hold_frames", &hold_frames) == 0 &&
        json_get_f32_in_range(ev->payload, end, "damage_mul", &damage_mul) == 0 &&
        hold_frames > 0 && hold_frames <= 255 && damage_mul > 0.0f) {
      out->start_af = (int16_t)ev->frame;
      out->damage_mul = damage_mul;
      out->hold_frames = (uint8_t)hold_frames;
      out->loaded = 1u;
      return 0;
    }
  }
  return -1;
}

static int parse_entry_jab_combo_window(const MslScriptTableRaw* table,
                                        const MslScriptEntryRaw* entry, MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_SET_JAB_COMBO) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    int disabled = 0;
    if (json_get_i32_in_range(ev->payload, end, "disabled", &disabled) == 0 && disabled == 0) {
      out->start_af = (int16_t)ev->frame;
      out->end_af = (int16_t)INT16_MAX;
      out->loaded = 1;
      return 0;
    }
  }
  return -1;
}

static int parse_entry_jab_rapid_window(const MslScriptTableRaw* table,
                                        const MslScriptEntryRaw* entry, MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  int on_frame = -1;
  int off_frame = -1;
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_SET_JAB_RAPID) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    int state = 0;
    if (json_get_i32_in_range(ev->payload, end, "state", &state) != 0) {
      continue;
    }
    if (state != 0 && on_frame < 0) {
      on_frame = (int)ev->frame;
    } else if (state == 0 && on_frame >= 0 && off_frame < 0) {
      off_frame = (int)ev->frame;
    }
  }
  if (on_frame < 0) {
    return -1;
  }
  if (off_frame < 0) {
    off_frame = INT16_MAX;
  }
  if (off_frame < on_frame) {
    return -1;
  }
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)off_frame;
  out->loaded = 1;
  return 0;
}

static int parse_entry_throw_flags_window(const MslScriptTableRaw* table,
                                          const MslScriptEntryRaw* entry, int want_hit_idx,
                                          uint8_t use_hit_idx, MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  int on_frame = -1;
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_SET_THROW_FLAGS) {
      continue;
    }
    int hit_idx = -1;
    const char* end = script_event_payload_end(ev);
    if (use_hit_idx && json_get_i32_in_range(ev->payload, end, "hit_idx", &hit_idx) != 0) {
      continue;
    }
    if (!use_hit_idx || hit_idx == want_hit_idx) {
      if (on_frame < 0 || (int)ev->frame < on_frame) {
        on_frame = (int)ev->frame;
      }
    }
  }
  if (on_frame < 0) {
    return -1;
  }
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)INT16_MAX;
  out->loaded = 1;
  return 0;
}

static int parse_entry_set_throw_flags_hit_idx_pulses(const MslScriptTableRaw* table,
                                                      const MslScriptEntryRaw* entry,
                                                      int want_hit_idx, MslFramePulses* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  MslFramePulses pulses = {0};
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_SET_THROW_FLAGS) {
      continue;
    }
    int hit_idx = -1;
    const char* end = script_event_payload_end(ev);
    if (json_get_i32_in_range(ev->payload, end, "hit_idx", &hit_idx) == 0 &&
        hit_idx == want_hit_idx) {
      frame_pulses_push(&pulses, (int)ev->frame);
    }
  }
  if (pulses.count > 0u) {
    pulses.loaded = 1u;
  }
  *out = pulses;
  return pulses.loaded ? 0 : -1;
}

static int parse_entry_catchattack_grabbed_hit_window(const MslScriptTableRaw* table,
                                                      const MslScriptEntryRaw* entry,
                                                      MslFrameWindow* out) {
  if (table == NULL || entry == NULL || out == NULL) {
    return -1;
  }
  int on_frame = -1;
  int off_frame = -1;
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL) {
      continue;
    }
    if (ev->kind_id == MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      int only_hit_grabbed = 0;
      const char* end = script_event_payload_end(ev);
      if (json_get_bool_in_range(ev->payload, end, "only_hit_grabbed", &only_hit_grabbed) == 0 &&
          only_hit_grabbed != 0) {
        if (on_frame < 0 || (int)ev->frame < on_frame) {
          on_frame = (int)ev->frame;
        }
      }
    } else if (ev->kind_id == MSL_SCRIPT_EVENT_CLEAR_HITBOXES && on_frame >= 0 &&
               (int)ev->frame >= on_frame) {
      if (off_frame < 0 || (int)ev->frame < off_frame) {
        off_frame = (int)ev->frame;
      }
    }
  }
  if (on_frame < 0) {
    return -1;
  }
  if (off_frame < 0) {
    off_frame = on_frame + 1;
  }
  if (off_frame < on_frame) {
    return -1;
  }
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)off_frame;
  out->loaded = 1;
  return 0;
}

static int parse_entry_throw_release_and_hitboxes(
    const MslScriptTableRaw* table, const MslScriptEntryRaw* entry, MslThrowRelease* out_release,
    MslFrameWindow* out_flip, MslFrameWindow* out_cmd1, MslFramePulses* out_spawn_projectile,
    MslThrowHitbox out_hitboxes[MSL_THROW_HITBOX_IDX_MAX]) {
  if (table == NULL || entry == NULL || out_release == NULL || out_flip == NULL ||
      out_cmd1 == NULL || out_spawn_projectile == NULL || out_hitboxes == NULL) {
    return -1;
  }
  *out_release = (MslThrowRelease){0};
  *out_flip = (MslFrameWindow){0};
  *out_cmd1 = (MslFrameWindow){0};
  *out_spawn_projectile = (MslFramePulses){0};
  for (size_t i = 0; i < MSL_THROW_HITBOX_IDX_MAX; i++) {
    out_hitboxes[i] = (MslThrowHitbox){0};
  }
  int best_release_frame = -1;
  int best_flip_frame = -1;
  int cmd1_on_frame = -1;
  int cmd1_off_frame = -1;

  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    if (ev->kind_id == MSL_SCRIPT_EVENT_SET_THROW_FLAGS) {
      int hit_idx = -1;
      if (json_get_i32_in_range(ev->payload, end, "hit_idx", &hit_idx) == 0) {
        if (hit_idx == 0) {
          if (best_release_frame < 0 || (int)ev->frame < best_release_frame) {
            best_release_frame = (int)ev->frame;
          }
        } else if (hit_idx == 1) {
          if (best_flip_frame < 0 || (int)ev->frame < best_flip_frame) {
            best_flip_frame = (int)ev->frame;
          }
        }
      }
    } else if (ev->kind_id == MSL_SCRIPT_EVENT_SET_CMD_VAR) {
      int idx = -1;
      int value = 0;
      if (json_get_i32_in_range(ev->payload, end, "idx", &idx) == 0 &&
          json_get_i32_in_range(ev->payload, end, "value", &value) == 0 && idx == 1) {
        if (value == 1) {
          if (cmd1_on_frame < 0 || (int)ev->frame < cmd1_on_frame) {
            cmd1_on_frame = (int)ev->frame;
          }
        } else if (cmd1_on_frame >= 0 && (int)ev->frame >= cmd1_on_frame) {
          if (cmd1_off_frame < 0 || (int)ev->frame < cmd1_off_frame) {
            cmd1_off_frame = (int)ev->frame;
          }
        }
      }
    } else if (ev->kind_id == MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE) {
      frame_pulses_push(out_spawn_projectile, (int)ev->frame);
    } else if (ev->kind_id == MSL_SCRIPT_EVENT_SET_THROW_HITBOX) {
      int idx = 0;
      int angle = 0;
      int kbg = 0;
      int wsk = 0;
      int bkb = 0;
      int element = 0;
      int sfx_kind = 0;
      int sfx_severity = 0;
      float damage = 0.0f;
      if (json_get_i32_in_range(ev->payload, end, "idx", &idx) == 0 &&
          json_get_f32_in_range(ev->payload, end, "damage", &damage) == 0 &&
          json_get_i32_in_range(ev->payload, end, "angle", &angle) == 0 &&
          json_get_i32_in_range(ev->payload, end, "kbg", &kbg) == 0 &&
          json_get_i32_in_range(ev->payload, end, "wsk", &wsk) == 0 &&
          json_get_i32_in_range(ev->payload, end, "bkb", &bkb) == 0 &&
          json_get_i32_in_range(ev->payload, end, "element", &element) == 0 &&
          json_get_i32_in_range(ev->payload, end, "sfx_kind", &sfx_kind) == 0 &&
          json_get_i32_in_range(ev->payload, end, "sfx_severity", &sfx_severity) == 0 && idx >= 0 &&
          idx < (int)MSL_THROW_HITBOX_IDX_MAX && !out_hitboxes[(size_t)idx].loaded) {
        out_hitboxes[(size_t)idx].damage = damage;
        out_hitboxes[(size_t)idx].angle = (uint16_t)angle;
        out_hitboxes[(size_t)idx].kbg = (uint16_t)kbg;
        out_hitboxes[(size_t)idx].wsk = (uint16_t)wsk;
        out_hitboxes[(size_t)idx].bkb = (uint16_t)bkb;
        out_hitboxes[(size_t)idx].element = (uint8_t)element;
        out_hitboxes[(size_t)idx].sfx_kind = (uint8_t)sfx_kind;
        out_hitboxes[(size_t)idx].sfx_severity = (uint8_t)sfx_severity;
        out_hitboxes[(size_t)idx].loaded = 1;
      }
    }
  }
  if (best_release_frame >= 0) {
    out_release->release_af = (int16_t)best_release_frame;
    out_release->hit_idx = 0;
    out_release->loaded = 1;
  }
  if (best_flip_frame >= 0) {
    out_flip->start_af = (int16_t)best_flip_frame;
    out_flip->end_af = INT16_MAX;
    out_flip->loaded = 1;
  }
  if (cmd1_on_frame >= 0) {
    out_cmd1->start_af = (int16_t)cmd1_on_frame;
    out_cmd1->end_af = (cmd1_off_frame >= 0 && cmd1_off_frame >= cmd1_on_frame)
                           ? (int16_t)cmd1_off_frame
                           : (int16_t)INT16_MAX;
    out_cmd1->loaded = 1;
  }
  if (out_spawn_projectile->count > 0u) {
    out_spawn_projectile->loaded = 1;
  }
  return 0;
}

static int parse_entry_special_pseudo_random_sfx(const MslScriptTableRaw* table,
                                                 const MslScriptEntryRaw* entry,
                                                 MslFramePulses* out_pulses,
                                                 uint8_t out_ranges[MSL_FRAME_PULSES_MAX]) {
  if (table == NULL || entry == NULL || out_pulses == NULL || out_ranges == NULL) {
    return -1;
  }
  MslFramePulses pulses = {0};
  uint8_t ranges[MSL_FRAME_PULSES_MAX] = {0};
  for (uint32_t i = 0; i < entry->event_count; i++) {
    const MslScriptEventRaw* ev = script_entry_event(table, entry, i);
    if (ev == NULL || ev->kind_id != MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX) {
      continue;
    }
    const char* end = script_event_payload_end(ev);
    int random_range = 0;
    if (json_get_i32_in_range(ev->payload, end, "random_range", &random_range) == 0 &&
        random_range > 0 && random_range <= 255) {
      const uint8_t prev_count = pulses.count;
      frame_pulses_push(&pulses, (int)ev->frame);
      if (pulses.count > prev_count) {
        ranges[prev_count] = (uint8_t)random_range;
      }
    }
  }
  if (pulses.count > 0u) {
    pulses.loaded = 1u;
  }
  *out_pulses = pulses;
  memcpy(out_ranges, ranges, sizeof(ranges));
  return 0;
}

static void load_if_cmd0(const MslScriptTableRaw* table, uint8_t char_id, uint16_t msid,
                         uint8_t open_end, MslFrameWindow* dst) {
  const MslScriptEntryRaw* entry = script_table_raw_find_entry(table, msid);
  MslFrameWindow win = {0};
  if (entry != NULL && parse_entry_cmd0_window(table, entry, open_end, &win) == 0) {
    *dst = win;
  }
  (void)char_id;
}

static void load_if_allow_interrupt(const MslScriptTableRaw* table, uint16_t msid,
                                    MslFrameWindow* dst) {
  const MslScriptEntryRaw* entry = script_table_raw_find_entry(table, msid);
  MslFrameWindow win = {0};
  if (entry != NULL && parse_entry_allow_interrupt_window(table, entry, &win) == 0) {
    *dst = win;
  }
}

static int load_one(const char* data_dir, const char* rel_path, uint8_t char_id) {
  if (data_dir == NULL || rel_path == NULL) {
    return -1;
  }

  MslScriptTableRaw script = {0};
  if (script_table_raw_load(data_dir, rel_path, &script) != 0) {
    return -1;
  }

  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ATTACK_AIR_N, 0,
               &g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ATTACK_AIR_F, 0,
               &g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ATTACK_AIR_B, 0,
               &g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ATTACK_AIR_HI, 0,
               &g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ATTACK_AIR_LW, 0,
               &g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW]);

  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ATTACK_AIR_N,
                          &g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N]);
  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ATTACK_AIR_F,
                          &g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F]);
  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ATTACK_AIR_B,
                          &g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B]);
  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ATTACK_AIR_HI,
                          &g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI]);
  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ATTACK_AIR_LW,
                          &g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW]);

  static const uint16_t grounded_msids[MSL_GROUNDED_ATTACK_KIND_COUNT] = {
      (uint16_t)MSL_SM_ATTACK_11,   (uint16_t)MSL_SM_ATTACK_12, (uint16_t)MSL_SM_ATTACK_13,
      (uint16_t)MSL_SM_ATTACK_DASH, (uint16_t)MSL_SM_ATTACK_S3, (uint16_t)MSL_SM_ATTACK_HI3,
      (uint16_t)MSL_SM_ATTACK_LW3,  (uint16_t)MSL_SM_ATTACK_S4, (uint16_t)MSL_SM_ATTACK_HI4,
      (uint16_t)MSL_SM_ATTACK_LW4,
  };
  for (size_t i = 0; i < MSL_GROUNDED_ATTACK_KIND_COUNT; i++) {
    load_if_allow_interrupt(&script, grounded_msids[i],
                            &g_allow_interrupt_by_char_grounded_attack[char_id][i]);
  }
  const size_t smash_kinds[] = {MSL_GROUNDED_ATTACK_KIND_S4, MSL_GROUNDED_ATTACK_KIND_HI4,
                                MSL_GROUNDED_ATTACK_KIND_LW4};
  for (size_t i = 0; i < sizeof(smash_kinds) / sizeof(smash_kinds[0]); i++) {
    const size_t kind = smash_kinds[i];
    const MslScriptEntryRaw* entry = script_table_raw_find_entry(&script, grounded_msids[kind]);
    MslSmashChargeInfo info = {0};
    if (entry != NULL && parse_entry_start_smash_charge_info(&script, entry, &info) == 0) {
      g_smash_charge_by_char_grounded_attack[char_id][kind] = info;
    }
  }

  load_if_allow_interrupt(&script, (uint16_t)MSL_SM_ESCAPE_N,
                          &g_allow_interrupt_by_char_escape_n[char_id]);

  const MslScriptEntryRaw* entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_ATTACK_11);
  MslFrameWindow win = {0};
  if (entry != NULL && parse_entry_jab_combo_window(&script, entry, &win) == 0) {
    g_jab_combo_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_11] = win;
  }
  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_ATTACK_12);
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_jab_combo_window(&script, entry, &win) == 0) {
    g_jab_combo_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_12] = win;
  }
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_jab_rapid_window(&script, entry, &win) == 0) {
    g_jab_rapid_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_12] = win;
  }

  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_ATTACK_100_LOOP);
  MslFramePulses pulses = {0};
  if (entry != NULL &&
      parse_entry_set_throw_flags_hit_idx_pulses(&script, entry, 0, &pulses) == 0) {
    g_attack100_loop_end_check_by_char[char_id] = pulses;
  }

  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_DASH, 1, &g_cmd0_by_char_dash[char_id]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_RUN_BRAKE, 1, &g_cmd0_by_char_runbrake[char_id]);
  load_if_cmd0(&script, char_id, (uint16_t)MSL_SM_ESCAPE_AIR, 1,
               &g_cmd0_by_char_escapeair[char_id]);

  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_ESCAPE_F);
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_throw_flags_window(&script, entry, 0, 1, &win) == 0) {
    g_throw_flags_by_char_escape_f[char_id] = win;
  }

  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_CATCH);
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_throw_flags_window(&script, entry, 0, 0, &win) == 0) {
    g_throw_flags_by_char_catch[char_id] = win;
  }
  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_CATCH_DASH);
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_throw_flags_window(&script, entry, 0, 0, &win) == 0) {
    g_throw_flags_by_char_catchdash[char_id] = win;
  }

  entry = script_table_raw_find_entry(&script, (uint16_t)MSL_SM_CATCH_ATTACK);
  win = (MslFrameWindow){0};
  if (entry != NULL && parse_entry_catchattack_grabbed_hit_window(&script, entry, &win) == 0) {
    g_catchattack_grabbed_hit_by_char[char_id] = win;
  }

  static const uint16_t throw_msids[MSL_THROW_KIND_COUNT] = {
      (uint16_t)MSL_SM_THROW_F, (uint16_t)MSL_SM_THROW_B, (uint16_t)MSL_SM_THROW_HI,
      (uint16_t)MSL_SM_THROW_LW};
  for (size_t kind = 0; kind < MSL_THROW_KIND_COUNT; kind++) {
    entry = script_table_raw_find_entry(&script, throw_msids[kind]);
    if (entry == NULL) {
      continue;
    }
    MslThrowRelease rel = {0};
    MslFrameWindow flip = {0};
    MslFrameWindow cmd1 = {0};
    MslFramePulses spawn_projectile = {0};
    MslThrowHitbox hitboxes[MSL_THROW_HITBOX_IDX_MAX];
    if (parse_entry_throw_release_and_hitboxes(&script, entry, &rel, &flip, &cmd1,
                                               &spawn_projectile, hitboxes) == 0) {
      g_throw_release_by_char[char_id][kind] = rel;
      g_throw_flip_by_char[char_id][kind] = flip;
      g_throw_cmd1_by_char[char_id][kind] = cmd1;
      g_throw_spawn_projectile_by_char[char_id][kind] = spawn_projectile;
      memcpy(g_throw_hitbox_by_char[char_id][kind], hitboxes, sizeof(hitboxes));
    }
  }

  uint8_t cmd0_count = 0u;
  uint8_t sfx_count = 0u;
  for (uint32_t i = 0; i < script.entry_count; i++) {
    entry = &script.entries[i];
    if (entry->msid < (uint16_t)MSL_SPECIAL_MSID_FIRST) {
      continue;
    }
    win = (MslFrameWindow){0};
    if (parse_entry_cmd0_window(&script, entry, 1, &win) == 0 &&
        cmd0_count < (uint8_t)MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX) {
      g_special_cmd0_by_char[char_id][cmd0_count].msid = entry->msid;
      g_special_cmd0_by_char[char_id][cmd0_count].window = win;
      cmd0_count = (uint8_t)(cmd0_count + 1u);
    }
    pulses = (MslFramePulses){0};
    uint8_t ranges[MSL_FRAME_PULSES_MAX] = {0};
    if (parse_entry_special_pseudo_random_sfx(&script, entry, &pulses, ranges) == 0 &&
        pulses.count > 0u && sfx_count < (uint8_t)MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX) {
      g_special_pseudo_rng_by_char[char_id][sfx_count].msid = entry->msid;
      g_special_pseudo_rng_by_char[char_id][sfx_count].pulses = pulses;
      memcpy(g_special_pseudo_rng_by_char[char_id][sfx_count].random_range, ranges, sizeof(ranges));
      sfx_count = (uint8_t)(sfx_count + 1u);
    }
  }
  g_special_cmd0_count_by_char[char_id] = cmd0_count;
  g_special_pseudo_rng_count_by_char[char_id] = sfx_count;

  script_table_raw_free(&script);
  return 0;
}

int move_tables_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  // Exact command-script windows are packed from decoded fighter scripts into MSLFTSC1.
  // Extractor: tools/extraction/extract_fighter_script_timeline.py
  if (load_one(data_dir, "scripts/fox.bin", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_one(data_dir, "scripts/falco.bin", MSL_CHAR_FALCO) != 0) {
    return -1;
  }

  g_loaded = 1;
  return 0;
}

static inline int attackair_kind_from_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
      return MSL_ATTACKAIR_KIND_N;
    case MSL_ACT_ATTACK_AIR_F:
      return MSL_ATTACKAIR_KIND_F;
    case MSL_ACT_ATTACK_AIR_B:
      return MSL_ATTACKAIR_KIND_B;
    case MSL_ACT_ATTACK_AIR_HI:
      return MSL_ATTACKAIR_KIND_HI;
    case MSL_ACT_ATTACK_AIR_LW:
      return MSL_ATTACKAIR_KIND_LW;
    default:
      return -1;
  }
}

static inline int grounded_attack_kind_from_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_ATTACK_11:
      return MSL_GROUNDED_ATTACK_KIND_11;
    case MSL_ACT_ATTACK_12:
      return MSL_GROUNDED_ATTACK_KIND_12;
    case MSL_ACT_ATTACK_13:
      return MSL_GROUNDED_ATTACK_KIND_13;
    case MSL_ACT_ATTACK_DASH:
      return MSL_GROUNDED_ATTACK_KIND_DASH;
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
      return MSL_GROUNDED_ATTACK_KIND_S3;
    case MSL_ACT_ATTACK_HI3:
      return MSL_GROUNDED_ATTACK_KIND_HI3;
    case MSL_ACT_ATTACK_LW3:
      return MSL_GROUNDED_ATTACK_KIND_LW3;
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
      return MSL_GROUNDED_ATTACK_KIND_S4;
    case MSL_ACT_ATTACK_HI4:
      return MSL_GROUNDED_ATTACK_KIND_HI4;
    case MSL_ACT_ATTACK_LW4:
      return MSL_GROUNDED_ATTACK_KIND_LW4;
    default:
      return -1;
  }
}

uint8_t move_tables_attackair_cmd0_active(uint8_t char_id, uint16_t attackair_action_id,
                                          float cur_anim_frame_f32) {
  const int kind = attackair_kind_from_action(attackair_action_id);
  if (kind < 0) {
    return 0;
  }

  const MslFrameWindow win = g_cmd0_by_char_attackair[char_id][(size_t)kind];
  if (!win.loaded) {
    // Conservative fallback: treat as auto-cancel (no landing lag).
    return 0;
  }

  // Decomp: ftCo_LandingAir_EnterWithLag uses fp->cmd_vars[0] to pick between LandingAir* (lag)
  // and Landing_Enter_Basic (auto-cancel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
  //
  // Command-script frame events are evaluated on fp->cur_anim_frame (float), not on an integer
  // action_frame counter. Use anim_frame_f32 (seeded from Slippi state_age) as our proxy.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820 (set_cmd_var)
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_attackair_allow_interrupt(uint8_t char_id, uint16_t attackair_action_id,
                                              float cur_anim_frame_f32) {
  const int kind = attackair_kind_from_action(attackair_action_id);
  if (kind < 0) {
    return 0;
  }

  const MslFrameWindow win = g_allow_interrupt_by_char_attackair[char_id][(size_t)kind];
  if (!win.loaded) {
    // Conservative fallback: treat as never-interruptible.
    return 0;
  }

  // Decomp: DO_IASA gates on fp->allow_interrupt (set by the move script).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
  //
  // Source: the command script emits an "allow interrupt" cmd (ftAction_80071950), which toggles
  // fp->allow_interrupt based on fp->cur_anim_frame (float) timing.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_grounded_attack_allow_interrupt(uint8_t char_id, uint16_t grounded_action_id,
                                                    float cur_anim_frame_f32) {
  const int kind = grounded_attack_kind_from_action(grounded_action_id);
  if (kind < 0) {
    return 0;
  }

  const MslFrameWindow win = g_allow_interrupt_by_char_grounded_attack[char_id][(size_t)kind];
  if (!win.loaded) {
    return 0;
  }

  // Decomp: grounded Attack* input callbacks gate on fp->allow_interrupt.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  // Source: command-script `allow_interrupt` events in data/scripts/{fox,falco}.bin (MSLFTSC1).
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_grounded_smash_charge_crossed(uint8_t char_id, uint16_t grounded_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32,
                                                  uint8_t* out_hold_frames) {
  const int kind = grounded_attack_kind_from_action(grounded_action_id);
  if (kind < 0 || out_hold_frames == NULL) {
    return 0;
  }
  const MslSmashChargeInfo info = g_smash_charge_by_char_grounded_attack[char_id][(size_t)kind];
  if (!info.loaded) {
    return 0;
  }

  // Decomp: ftAction_80073008 runs when the movescript crosses the command frame boundary under
  // ftAnim_8006EBA4 / ftAction_80073240. Model this as a one-shot threshold crossing in (prev, cur].
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80073008,ftAction_80073240}
  if (!(prev_anim_frame_f32 < (float)info.start_af && cur_anim_frame_f32 >= (float)info.start_af)) {
    return 0;
  }
  *out_hold_frames = info.hold_frames;
  return 1u;
}

float move_tables_grounded_smash_charge_damage_mul(uint8_t char_id, uint16_t grounded_action_id) {
  const int kind = grounded_attack_kind_from_action(grounded_action_id);
  if (kind < 0) {
    return 1.0f;
  }
  const MslSmashChargeInfo info = g_smash_charge_by_char_grounded_attack[char_id][(size_t)kind];
  if (!info.loaded || !(info.damage_mul > 0.0f)) {
    return 1.0f;
  }
  return info.damage_mul;
}

uint8_t move_tables_escape_allow_interrupt(uint8_t char_id, uint16_t action_id,
                                           float cur_anim_frame_f32) {
  if (action_id != (uint16_t)MSL_ACT_ESCAPE_N) {
    return 0;
  }

  const MslFrameWindow win = g_allow_interrupt_by_char_escape_n[char_id];
  if (!win.loaded) {
    return 0;
  }

  // Decomp: command-script allow_interrupt writes fp->allow_interrupt at runtime.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_escapef_should_flip_facing(uint8_t char_id, int16_t prev_action_frame,
                                               int16_t cur_action_frame) {
  const MslFrameWindow win = g_throw_flags_by_char_escape_f[char_id];
  if (!win.loaded) {
    return 0;
  }
  // EscapeF consumes the set_throw_flags(hit_idx=0) bit through ftCheckThrowB3 during Anim.
  // Model the one-shot consume as an action-frame threshold crossing within the current step.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim
  // refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
  return (prev_action_frame < win.start_af && cur_action_frame >= win.start_af) ? 1u : 0u;
}

uint8_t move_tables_jab_combo_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32) {
  const int kind = grounded_attack_kind_from_action(grounded_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFrameWindow win = g_jab_combo_by_char_grounded_attack[char_id][(size_t)kind];
  if (!win.loaded) {
    return 0;
  }
  // Decomp: ftAction_80071AE8 sets x2218_b1 when command timeline reaches set_jab_combo.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_jab_rapid_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32) {
  const int kind = grounded_attack_kind_from_action(grounded_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFrameWindow win = g_jab_rapid_by_char_grounded_attack[char_id][(size_t)kind];
  if (!win.loaded) {
    return 0;
  }
  // Decomp: ftAction_80071B28 writes x2218_b2 from set_jab_rapid command events.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071B28
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_attack100_loop_end_check_crossed(uint8_t char_id, int16_t prev_action_frame,
                                                     int16_t cur_action_frame) {
  const MslFramePulses pulses = g_attack100_loop_end_check_by_char[char_id];
  if (!pulses.loaded || pulses.count == 0u) {
    return 0u;
  }
  for (uint8_t i = 0; i < pulses.count; i++) {
    const int16_t on = pulses.frame[i];
    if (cur_action_frame >= prev_action_frame) {
      if (prev_action_frame < on && cur_action_frame >= on) {
        return 1u;
      }
    } else if (prev_action_frame < on || cur_action_frame >= on) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_dash_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  const MslFrameWindow win = g_cmd0_by_char_dash[char_id];
  if (!win.loaded) {
    // Conservative fallback: treat as never-enabled.
    return 0;
  }
  // Command-script frame events are evaluated on fp->cur_anim_frame (float).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820 (set_cmd_var)
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_runbrake_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  const MslFrameWindow win = g_cmd0_by_char_runbrake[char_id];
  if (!win.loaded) {
    return 0;
  }
  // Command-script frame events are evaluated on fp->cur_anim_frame (float).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_escapeair_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  const MslFrameWindow win = g_cmd0_by_char_escapeair[char_id];
  if (!win.loaded) {
    return 0;
  }
  // Command-script frame events are evaluated on fp->cur_anim_frame (float).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

uint8_t move_tables_special_cmd0_active_at_frame(uint8_t char_id, uint16_t msid, int action_frame) {
  const uint8_t count = g_special_cmd0_count_by_char[char_id];
  for (uint8_t i = 0; i < count; i++) {
    const MslSpecialCmd0ByMsid* ent = &g_special_cmd0_by_char[char_id][i];
    if (ent->msid != msid || !ent->window.loaded) {
      continue;
    }
    // Command-script frames are 0-based in extracted data. SpecialN loop-repeat inference asks
    // whether the B press happened while cmd_var[0] was active, not whether cmd_var[0] is still
    // active on the terminal Anim callback frame.
    // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    // SpecialN's IASA writes a persistent mv.fx.SpecialN.isBlasterLoop latch; the command-script
    // clear frame ends the cmd_var window but does not itself clear a latch set by a nearby B edge.
    // Keep a narrow terminal tail tied to the extracted clear event, not to replay ids/actions.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
    const int end_tail = (int)ent->window.end_af + MSL_SPECIAL_CMD0_LATCH_CLEAR_TAIL_FRAMES;
    return (action_frame >= (int)ent->window.start_af && action_frame < end_tail) ? 1u : 0u;
  }
  return 0u;
}

uint8_t move_tables_catchpull_should_enter_wait(uint8_t char_id, uint16_t catch_action_id,
                                                float cur_anim_frame_f32) {
  // Decomp: CatchPull_Anim triggers the CatchWait transition on a throw_flags bit that is set by the
  // move script (`set_throw_flags`) and then cleared when consumed.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  //
  // In this simulator, we approximate the flag mutation using extracted move script event timing:
  // data/scripts/{fox,falco}.bin (MSLFTSC1) moves["ftCo_SM_Catch*"]["events"] set_throw_flags.
  const MslFrameWindow win = (catch_action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL)
                                 ? g_throw_flags_by_char_catchdash[char_id]
                                 : g_throw_flags_by_char_catch[char_id];
  if (!win.loaded) {
    return 0;
  }
  return (cur_anim_frame_f32 >= (float)win.start_af) ? 1 : 0;
}

uint8_t move_tables_catchattack_grabbed_hit_active(uint8_t char_id, float cur_anim_frame_f32) {
  const MslFrameWindow win = g_catchattack_grabbed_hit_by_char[char_id];
  if (!win.loaded) {
    return 0;
  }
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
}

static inline int throw_kind_from_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_THROW_F:
      return MSL_THROW_KIND_F;
    case MSL_ACT_THROW_B:
      return MSL_THROW_KIND_B;
    case MSL_ACT_THROW_HI:
      return MSL_THROW_KIND_HI;
    case MSL_ACT_THROW_LW:
      return MSL_THROW_KIND_LW;
    default:
      return -1;
  }
}

uint8_t move_tables_throw_has_release(uint8_t char_id, uint16_t throw_action_id) {
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  return g_throw_release_by_char[char_id][(size_t)kind].loaded ? 1 : 0;
}

uint8_t move_tables_throw_release_frame(uint8_t char_id, uint16_t throw_action_id,
                                        float* out_release_af) {
  if (out_release_af == NULL) {
    return 0;
  }
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslThrowRelease rel = g_throw_release_by_char[char_id][(size_t)kind];
  if (!rel.loaded) {
    return 0;
  }
  *out_release_af = (float)rel.release_af;
  return 1;
}

uint8_t move_tables_throw_release_hit_idx(uint8_t char_id, uint16_t throw_action_id,
                                          float cur_anim_frame_f32, uint8_t* out_hit_idx) {
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }

  const MslThrowRelease rel = g_throw_release_by_char[char_id][(size_t)kind];
  if (!rel.loaded) {
    return 0;
  }

  if (cur_anim_frame_f32 >= (float)rel.release_af) {
    if (out_hit_idx) {
      *out_hit_idx = rel.hit_idx;
    }
    return 1;
  }
  return 0;
}

uint8_t move_tables_throw_hitbox_params(uint8_t char_id, uint16_t throw_action_id, uint8_t hit_idx,
                                        MslThrowHitboxParams* out) {
  if (out == NULL) {
    return 0;
  }
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  if (hit_idx >= (uint8_t)MSL_THROW_HITBOX_IDX_MAX) {
    return 0;
  }

  const MslThrowHitbox hb = g_throw_hitbox_by_char[char_id][(size_t)kind][(size_t)hit_idx];
  if (!hb.loaded) {
    return 0;
  }

  out->damage = hb.damage;
  out->angle = hb.angle;
  out->kbg = hb.kbg;
  out->wsk = hb.wsk;
  out->bkb = hb.bkb;
  out->element = hb.element;
  out->sfx_kind = hb.sfx_kind;
  out->sfx_severity = hb.sfx_severity;
  return 1;
}

uint8_t move_tables_throw_should_flip_facing(uint8_t char_id, uint16_t throw_action_id,
                                             float prev_anim_frame_f32, float cur_anim_frame_f32) {
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFrameWindow flip = g_throw_flip_by_char[char_id][(size_t)kind];
  if (!flip.loaded) {
    return 0;
  }
  // set_throw_flags(hit_idx=1) is interpreted by ftAction_800718A4 as a "flip facing" flag that is
  // later consumed once by ftCo_800DD724 (clearing the underlying bit).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  //
  // Use a "crossing" check so we don't miss the flip if frame_speed_mul > 1.0 advances the
  // animation timebase by more than one frame in a single step.
  const float on = (float)flip.start_af;
  return (prev_anim_frame_f32 < on && cur_anim_frame_f32 >= on) ? 1u : 0u;
}

uint8_t move_tables_throw_cmd1_active(uint8_t char_id, uint16_t throw_action_id,
                                      float cur_anim_frame_f32) {
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFrameWindow win = g_throw_cmd1_by_char[char_id][(size_t)kind];
  if (!win.loaded) {
    return 0;
  }
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1u
                                                                                               : 0u;
}

uint8_t move_tables_throw_should_spawn_projectile(uint8_t char_id, uint16_t throw_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32) {
  return move_tables_throw_crossed_projectile_pulse_frame(
             char_id, throw_action_id, prev_anim_frame_f32, cur_anim_frame_f32, NULL)
             ? 1u
             : 0u;
}

uint8_t move_tables_throw_crossed_projectile_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                         float prev_anim_frame_f32,
                                                         float cur_anim_frame_f32,
                                                         int16_t* out_pulse_frame) {
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFramePulses pulses = g_throw_spawn_projectile_by_char[char_id][(size_t)kind];
  if (!pulses.loaded || pulses.count == 0u) {
    return 0;
  }
  // ftAction_80071974 sets throw_flags_b0 as a pulse at script-time.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
  // Use crossing semantics so frame_speed_mul > 1.0 does not skip a pulse.
  for (uint8_t i = 0; i < pulses.count; i++) {
    const float on = (float)pulses.frame[i];
    if (prev_anim_frame_f32 < on && cur_anim_frame_f32 >= on) {
      if (out_pulse_frame != NULL) {
        *out_pulse_frame = pulses.frame[i];
      }
      return 1u;
    }
  }
  return 0;
}

uint8_t move_tables_throw_projectile_first_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                       int16_t* out_first_pulse_frame) {
  if (out_first_pulse_frame == NULL) {
    return 0;
  }
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFramePulses pulses = g_throw_spawn_projectile_by_char[char_id][(size_t)kind];
  if (!pulses.loaded || pulses.count == 0u) {
    return 0;
  }
  int16_t first = pulses.frame[0];
  for (uint8_t i = 1; i < pulses.count; i++) {
    if (pulses.frame[i] < first) {
      first = pulses.frame[i];
    }
  }
  *out_first_pulse_frame = first;
  return 1u;
}

uint8_t move_tables_throw_projectile_last_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                      int16_t* out_last_pulse_frame) {
  if (out_last_pulse_frame == NULL) {
    return 0;
  }
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFramePulses pulses = g_throw_spawn_projectile_by_char[char_id][(size_t)kind];
  if (!pulses.loaded || pulses.count == 0u) {
    return 0;
  }
  int16_t last = pulses.frame[0];
  for (uint8_t i = 1; i < pulses.count; i++) {
    if (pulses.frame[i] > last) {
      last = pulses.frame[i];
    }
  }
  *out_last_pulse_frame = last;
  return 1u;
}

uint8_t move_tables_throw_projectile_pulse_ordinal(uint8_t char_id, uint16_t throw_action_id,
                                                   int16_t pulse_frame, uint8_t* out_ordinal) {
  if (out_ordinal == NULL) {
    return 0;
  }
  const int kind = throw_kind_from_action(throw_action_id);
  if (kind < 0) {
    return 0;
  }
  const MslFramePulses pulses = g_throw_spawn_projectile_by_char[char_id][(size_t)kind];
  if (!pulses.loaded || pulses.count == 0u) {
    return 0;
  }
  uint8_t ordinal = 0u;
  for (uint8_t i = 0; i < pulses.count; i++) {
    if (pulses.frame[i] <= pulse_frame) {
      ordinal++;
    }
    if (pulses.frame[i] == pulse_frame) {
      *out_ordinal = ordinal;
      return 1u;
    }
  }
  return 0;
}

uint8_t move_tables_special_pseudo_random_sfx_ranges_crossed(uint8_t char_id, uint16_t msid,
                                                             float prev_anim_frame_f32,
                                                             float cur_anim_frame_f32,
                                                             uint8_t* out_random_ranges,
                                                             uint8_t max_out) {
  if (out_random_ranges == NULL || max_out == 0u) {
    return 0u;
  }
  const uint8_t n = g_special_pseudo_rng_count_by_char[char_id];
  if (n == 0u) {
    return 0u;
  }
  for (uint8_t i = 0; i < n; i++) {
    const MslPseudoRandomSfxByMsid* ent = &g_special_pseudo_rng_by_char[char_id][i];
    if (ent->msid != msid || !ent->pulses.loaded || ent->pulses.count == 0u) {
      continue;
    }
    uint8_t out_n = 0u;
    // Command-script events execute when crossing the command frame boundary.
    // refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
    for (uint8_t pi = 0; pi < ent->pulses.count && out_n < max_out; pi++) {
      const float on = (float)ent->pulses.frame[pi];
      // Frame-0 command pulses execute on fresh motion-state entry scripts (cur_anim_frame starts at
      // 0 and advances to 1 in the first steady frame). Preserve that entry pulse with an explicit
      // frame-0 bridge so `on==0` events are not skipped under teacher-forced reseed snapshots.
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
      if ((prev_anim_frame_f32 < on && cur_anim_frame_f32 >= on) ||
          (on == 0.0f && prev_anim_frame_f32 == 0.0f && cur_anim_frame_f32 > 0.0f)) {
        out_random_ranges[out_n++] = ent->random_range[pi];
      }
    }
    return out_n;
  }
  return 0u;
}
