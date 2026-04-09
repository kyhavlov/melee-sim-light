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
static MslFrameWindow g_allow_interrupt_by_char_escape_n[256];
static MslFrameWindow g_throw_flags_by_char_escape_f[256];
static MslFrameWindow g_jab_combo_by_char_grounded_attack[256][MSL_GROUNDED_ATTACK_KIND_COUNT];
static MslFrameWindow g_jab_rapid_by_char_grounded_attack[256][MSL_GROUNDED_ATTACK_KIND_COUNT];
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
static MslPseudoRandomSfxByMsid g_special_pseudo_rng_by_char[256]
                                                            [MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX];
static uint8_t g_special_pseudo_rng_count_by_char[256];
static int g_loaded = 0;

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

static const char* json_find_matching_delim(const char* open, const char* end, char open_ch,
                                            char close_ch) {
  if (open == NULL || end == NULL || open >= end) {
    return NULL;
  }
  if (*open != open_ch) {
    return NULL;
  }

  int depth = 0;
  uint8_t in_str = 0;
  uint8_t esc = 0;

  for (const char* p = open; p < end; p++) {
    const char c = *p;
    if (in_str) {
      if (esc) {
        esc = 0;
      } else if (c == '\\') {
        esc = 1;
      } else if (c == '"') {
        in_str = 0;
      }
      continue;
    }

    if (c == '"') {
      in_str = 1;
      continue;
    }

    if (c == open_ch) {
      depth++;
      continue;
    }
    if (c == close_ch) {
      depth--;
      if (depth == 0) {
        return p;
      }
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

static int json_get_str_eq_in_range(const char* start, const char* end, const char* key,
                                    const char* want) {
  if (start == NULL || end == NULL || key == NULL || want == NULL || start >= end) {
    return 0;
  }
  char pat[128];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat)) {
    return 0;
  }
  const char* p = strstr_range(start, end, pat);
  if (p == NULL) {
    return 0;
  }
  p = (const char*)memchr(p, ':', (size_t)(end - p));
  if (p == NULL) {
    return 0;
  }
  p++;
  p = json_skip_ws(p);
  if (p == NULL || p >= end || *p != '"') {
    return 0;
  }
  p++;
  const size_t want_len = strlen(want);
  if (p + want_len >= end) {
    return 0;
  }
  if (memcmp(p, want, want_len) != 0) {
    return 0;
  }
  if (p[want_len] != '"') {
    return 0;
  }
  return 1;
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

static int parse_attackair_cmd0_window(const char* buf, const char* buf_end, const char* move_key,
                                       MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;
  int off_frame = -1;

  // Iterate event objects in the events array.
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    // Only care about set_cmd_var events (extracted from fighter command scripts).
    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_cmd_var")) {
      int frame = 0;
      int idx = 0;
      int value = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "idx", &idx) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "value", &value) == 0) {
        if (idx == 0) {
          if (value != 0 && on_frame < 0) {
            on_frame = frame;
          } else if (value == 0 && on_frame >= 0 && off_frame < 0) {
            off_frame = frame;
          }
        }
      }
    }

    p = ev_end + 1;
  }

  if (on_frame < 0 || off_frame < 0 || off_frame < on_frame) {
    return -1;
  }

  // Extracted move script frames are 0-based (see tools/extraction/extract_fighter_moves.py).
  // Treat cmd_var[0] as enabled for action_frame in [on, off).
  const int16_t start_af = (int16_t)on_frame;
  const int16_t end_af = (int16_t)off_frame;
  out->start_af = start_af;
  out->end_af = end_af;
  out->loaded = 1;
  return 0;
}

static int parse_cmd0_window_open_end(const char* buf, const char* buf_end, const char* move_key,
                                      MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;
  int off_frame = -1;

  // Iterate event objects in the events array.
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    // Only care about set_cmd_var events (extracted from fighter command scripts).
    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_cmd_var")) {
      int frame = 0;
      int idx = 0;
      int value = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "idx", &idx) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "value", &value) == 0) {
        if (idx == 0) {
          if (value != 0 && on_frame < 0) {
            on_frame = frame;
          } else if (value == 0 && on_frame >= 0 && off_frame < 0) {
            off_frame = frame;
          }
        }
      }
    }

    p = ev_end + 1;
  }

  if (on_frame < 0) {
    return -1;
  }
  if (off_frame < 0) {
    // Common pattern for grounded locomotion scripts: cmd_var[0] is set once and never explicitly
    // cleared (it is cleared on motion-state entry instead).
    //
    // Decomp tie-down for Dash:
    // - ftCo_Dash_Enter resets fp->cmd_vars[0] = 0 on motion-state entry.
    //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
    // - The Dash command script then sets cmd_var[0] once (no explicit clear event in the script),
    //   so treating the window as open-ended within that motion state is correct.
    off_frame = INT16_MAX;
  }
  if (off_frame < on_frame) {
    return -1;
  }

  // Extracted move script frames are 0-based (see tools/extraction/extract_fighter_moves.py).
  // Treat cmd_var[0] as enabled for action_frame in [on, off).
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)off_frame;
  out->loaded = 1;
  return 0;
}

static int parse_allow_interrupt_window(const char* buf, const char* buf_end, const char* move_key,
                                        MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;

  // Iterate event objects in the events array.
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    // `allow_interrupt` events are extracted from command scripts and correspond to
    // `fp->allow_interrupt = true` in the runtime.
    // Source: data/moves/{fox,falco}.json moves["ftCo_SM_AttackAir*"]["events"].
    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "allow_interrupt")) {
      int frame = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0) {
        on_frame = frame;
        break;
      }
    }

    p = ev_end + 1;
  }

  if (on_frame < 0) {
    return -1;
  }

  // Treat allow_interrupt as enabled for action_frame in [on, +inf).
  //
  // Decomp:
  // - AttackAir enter clears fp->allow_interrupt to false.
  // - DO_IASA gates on fp->allow_interrupt for all aerials (we only model a subset).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)INT16_MAX;
  out->loaded = 1;
  return 0;
}

static int parse_jab_combo_window_open_end(const char* buf, const char* buf_end,
                                           const char* move_key, MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    // Jab combo gate command: ftAction_80071AE8 (set_jab_combo).
    // refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_jab_combo")) {
      int frame = 0;
      int disabled = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "disabled", &disabled) == 0) {
        if (disabled == 0) {
          on_frame = frame;
          break;
        }
      }
    }
    p = ev_end + 1;
  }

  if (on_frame < 0) {
    return -1;
  }

  out->start_af = (int16_t)on_frame;
  out->end_af = (int16_t)INT16_MAX;
  out->loaded = 1;
  return 0;
}

static int parse_jab_rapid_window_open_end(const char* buf, const char* buf_end,
                                           const char* move_key, MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;
  int off_frame = -1;
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    // Jab rapid gate command: ftAction_80071B28 (set_jab_rapid).
    // refs/melee/src/melee/ft/ftaction.c::ftAction_80071B28
    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_jab_rapid")) {
      int frame = 0;
      int state = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "state", &state) == 0) {
        if (state != 0 && on_frame < 0) {
          on_frame = frame;
        } else if (state == 0 && on_frame >= 0 && off_frame < 0) {
          off_frame = frame;
        }
      }
    }
    p = ev_end + 1;
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

static int parse_throw_release_and_hitboxes(const char* buf, const char* buf_end,
                                            const char* move_key, MslThrowRelease* out_release,
                                            MslFrameWindow* out_flip, MslFrameWindow* out_cmd1,
                                            MslFramePulses* out_spawn_projectile,
                                            MslThrowHitbox out_hitboxes[MSL_THROW_HITBOX_IDX_MAX]) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out_release == NULL ||
      out_flip == NULL || out_cmd1 == NULL || out_spawn_projectile == NULL ||
      out_hitboxes == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
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

  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_throw_flags")) {
      int frame = 0;
      int hit_idx = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "hit_idx", &hit_idx) == 0) {
        // Decomp: ftAction_800718A4 interprets hit_idx as a selector for which throw_flags bit to set:
        // - 0 => throw_flags_b3 (release / apply throw hit)
        // - 1 => throw_flags_b4 (flip facing)
        // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
        if (hit_idx == 0) {
          if (best_release_frame < 0 || frame < best_release_frame) {
            best_release_frame = frame;
          }
        } else if (hit_idx == 1) {
          if (best_flip_frame < 0 || frame < best_flip_frame) {
            best_flip_frame = frame;
          }
        }
      }
    } else if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_cmd_var")) {
      int frame = 0;
      int idx = -1;
      int value = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "idx", &idx) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "value", &value) == 0) {
        // Throw-side blaster flow in ftFx_Throw_Anim switches on cmd_vars[1]:
        // - 1 => spawn/update gun + shoot handling
        // - 2 => clear pointer path
        // - 0 => disabled path
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        if (idx == 1) {
          if (value == 1) {
            if (cmd1_on_frame < 0 || frame < cmd1_on_frame) {
              cmd1_on_frame = frame;
            }
          } else if (cmd1_on_frame >= 0 && frame >= cmd1_on_frame) {
            if (cmd1_off_frame < 0 || frame < cmd1_off_frame) {
              cmd1_off_frame = frame;
            }
          }
        }
      }
    } else if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_throw_spawn_projectile")) {
      int frame = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0) {
        frame_pulses_push(out_spawn_projectile, frame);
      }
    } else if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_throw_hitbox")) {
      int idx = 0;
      int angle = 0;
      int kbg = 0;
      int wsk = 0;
      int bkb = 0;
      int element = 0;
      int sfx_kind = 0;
      int sfx_severity = 0;
      float damage = 0.0f;
      if (json_get_i32_in_range(ev_start, ev_end, "idx", &idx) == 0 &&
          json_get_f32_in_range(ev_start, ev_end, "damage", &damage) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "angle", &angle) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "kbg", &kbg) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "wsk", &wsk) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "bkb", &bkb) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "element", &element) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "sfx_kind", &sfx_kind) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "sfx_severity", &sfx_severity) == 0) {
        if (idx >= 0 && idx < (int)MSL_THROW_HITBOX_IDX_MAX && !out_hitboxes[(size_t)idx].loaded) {
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

    p = ev_end + 1;
  }

  if (best_release_frame >= 0) {
    out_release->release_af = (int16_t)best_release_frame;
    // set_throw_flags(hit_idx=0) gates the throw-hit application; the throw hitbox params are
    // configured separately by set_throw_hitbox(idx=...).
    //
    // Decomp: ftCo_800DDDE4 reads fp->xDF4[0] for the throw hit capsule (HitCapsule).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    //
    // Current sim policy (Fox/Falco v1): use throw hitbox idx 0.
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
  if (out_spawn_projectile->count > 0) {
    out_spawn_projectile->loaded = 1;
  }
  return 0;
}

static int parse_throw_flags_window_open_end(const char* buf, const char* buf_end,
                                             const char* move_key, MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;

  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "set_throw_flags")) {
      int frame = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0) {
        if (on_frame < 0 || frame < on_frame) {
          on_frame = frame;
        }
      }
    }

    p = ev_end + 1;
  }

  if (on_frame < 0) {
    return -1;
  }

  out->start_af = (int16_t)on_frame;
  out->end_af = INT16_MAX;
  out->loaded = 1;
  return 0;
}

static int parse_catchattack_grabbed_hit_window(const char* buf, const char* buf_end,
                                                const char* move_key, MslFrameWindow* out) {
  if (buf == NULL || buf_end == NULL || move_key == NULL || out == NULL) {
    return -1;
  }

  char pat[128];
  const int pn = snprintf(pat, sizeof(pat), "\"%s\"", move_key);
  if (pn <= 0 || (size_t)pn >= sizeof(pat)) {
    return -1;
  }

  const char* key_pos = strstr_range(buf, buf_end, pat);
  if (key_pos == NULL) {
    return -1;
  }
  const char* obj_start = (const char*)memchr(key_pos, '{', (size_t)(buf_end - key_pos));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  const char* events_key = strstr_range(obj_start, obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  int on_frame = -1;
  int off_frame = -1;

  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "create_hitbox")) {
      int frame = 0;
      int only_hit_grabbed = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_bool_in_range(ev_start, ev_end, "only_hit_grabbed", &only_hit_grabbed) == 0 &&
          only_hit_grabbed != 0) {
        if (on_frame < 0 || frame < on_frame) {
          on_frame = frame;
        }
      }
    } else if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "clear_hitboxes") &&
               on_frame >= 0) {
      int frame = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 && frame >= on_frame) {
        if (off_frame < 0 || frame < off_frame) {
          off_frame = frame;
        }
      }
    }

    p = ev_end + 1;
  }

  if (on_frame < 0) {
    return -1;
  }
  if (off_frame < 0) {
    // CatchAttack scripts clear hitboxes explicitly in extracted Fox/Falco data. Keep an open
    // fallback so missing clear events still provide a deterministic active window.
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

static int parse_special_pseudo_random_sfx_ranges(const char* move_obj_start,
                                                  const char* move_obj_end,
                                                  MslFramePulses* out_pulses,
                                                  uint8_t out_ranges[MSL_FRAME_PULSES_MAX]) {
  if (move_obj_start == NULL || move_obj_end == NULL || out_pulses == NULL || out_ranges == NULL) {
    return -1;
  }
  const char* events_key = strstr_range(move_obj_start, move_obj_end, "\"events\"");
  if (events_key == NULL) {
    return -1;
  }
  const char* arr_start = (const char*)memchr(events_key, '[', (size_t)(move_obj_end - events_key));
  if (arr_start == NULL) {
    return -1;
  }
  const char* arr_end = json_find_matching_delim(arr_start, move_obj_end, '[', ']');
  if (arr_end == NULL) {
    return -1;
  }

  MslFramePulses pulses = {0};
  uint8_t ranges[MSL_FRAME_PULSES_MAX] = {0};
  const char* p = arr_start;
  while (p && p < arr_end) {
    const char* ev_start = (const char*)memchr(p, '{', (size_t)(arr_end - p));
    if (ev_start == NULL) {
      break;
    }
    const char* ev_end = json_find_matching_delim(ev_start, arr_end, '{', '}');
    if (ev_end == NULL) {
      break;
    }

    if (json_get_str_eq_in_range(ev_start, ev_end, "kind", "pseudo_random_sfx")) {
      int frame = 0;
      int random_range = 0;
      if (json_get_i32_in_range(ev_start, ev_end, "frame", &frame) == 0 &&
          json_get_i32_in_range(ev_start, ev_end, "random_range", &random_range) == 0 &&
          random_range > 0 && random_range <= 255) {
        // Decomp command opcode 38 consumes one HSD_Randi(random_range) when executed.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
        // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
        const uint8_t prev_count = pulses.count;
        frame_pulses_push(&pulses, frame);
        if (pulses.count > prev_count) {
          ranges[prev_count] = (uint8_t)random_range;
        }
      }
    }

    p = ev_end + 1;
  }

  if (pulses.count > 0u) {
    pulses.loaded = 1u;
  }
  *out_pulses = pulses;
  memcpy(out_ranges, ranges, sizeof(ranges));
  return 0;
}

static int parse_specials_by_msid_pseudo_random_sfx(const char* buf, const char* buf_end,
                                                    uint8_t char_id) {
  if (buf == NULL || buf_end == NULL) {
    return -1;
  }
  const char* specials_key = strstr_range(buf, buf_end, "\"specials_by_msid\"");
  if (specials_key == NULL) {
    g_special_pseudo_rng_count_by_char[char_id] = 0u;
    return 0;
  }
  const char* obj_start = (const char*)memchr(specials_key, '{', (size_t)(buf_end - specials_key));
  if (obj_start == NULL) {
    return -1;
  }
  const char* obj_end = json_find_matching_delim(obj_start, buf_end, '{', '}');
  if (obj_end == NULL) {
    return -1;
  }

  uint8_t entry_count = 0u;
  const char* p = obj_start + 1;
  while (p && p < obj_end) {
    const char* key_start = (const char*)memchr(p, '"', (size_t)(obj_end - p));
    if (key_start == NULL || key_start >= obj_end) {
      break;
    }
    const char* key_end =
        (const char*)memchr(key_start + 1, '"', (size_t)(obj_end - (key_start + 1)));
    if (key_end == NULL || key_end >= obj_end) {
      break;
    }

    char key_buf[16];
    const size_t key_len = (size_t)(key_end - (key_start + 1));
    if (key_len == 0 || key_len >= sizeof(key_buf)) {
      p = key_end + 1;
      continue;
    }
    memcpy(key_buf, key_start + 1, key_len);
    key_buf[key_len] = '\0';

    char* key_parse_end = NULL;
    errno = 0;
    long msid_long = strtol(key_buf, &key_parse_end, 10);
    if (key_parse_end == key_buf || *key_parse_end != '\0' || errno != 0 || msid_long < 0 ||
        msid_long > 0xFFFFL) {
      p = key_end + 1;
      continue;
    }

    const char* colon = (const char*)memchr(key_end, ':', (size_t)(obj_end - key_end));
    if (colon == NULL || colon >= obj_end) {
      break;
    }
    const char* move_obj_start = json_skip_ws(colon + 1);
    if (move_obj_start == NULL || move_obj_start >= obj_end || *move_obj_start != '{') {
      p = colon + 1;
      continue;
    }
    const char* move_obj_end = json_find_matching_delim(move_obj_start, obj_end, '{', '}');
    if (move_obj_end == NULL) {
      break;
    }

    MslFramePulses pulses = {0};
    uint8_t ranges[MSL_FRAME_PULSES_MAX] = {0};
    if (parse_special_pseudo_random_sfx_ranges(move_obj_start, move_obj_end, &pulses, ranges) ==
            0 &&
        pulses.count > 0u && entry_count < (uint8_t)MSL_SPECIAL_PSEUDO_RNG_ENTRIES_MAX) {
      MslPseudoRandomSfxByMsid* ent = &g_special_pseudo_rng_by_char[char_id][entry_count];
      ent->msid = (uint16_t)msid_long;
      ent->pulses = pulses;
      memcpy(ent->random_range, ranges, sizeof(ranges));
      entry_count = (uint8_t)(entry_count + 1u);
    }
    p = move_obj_end + 1;
  }
  g_special_pseudo_rng_count_by_char[char_id] = entry_count;
  return 0;
}

static int load_one(const char* data_dir, const char* rel_path, uint8_t char_id) {
  if (data_dir == NULL || rel_path == NULL) {
    return -1;
  }

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

  char* buf = (char*)alloc_malloc((size_t)sz + 1);
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
  buf[sz] = '\0';

  const char* buf_end = buf + (size_t)sz;

  MslFrameWindow win = {0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirN", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N] = win;

  win = (MslFrameWindow){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirF", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F] = win;

  win = (MslFrameWindow){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirB", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B] = win;

  win = (MslFrameWindow){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirHi", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI] = win;

  win = (MslFrameWindow){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirLw", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW] = win;

  // `allow_interrupt` windows (IASA gating) for AttackAir*.
  //
  // Source: data/moves/{fox,falco}.json moves["ftCo_SM_AttackAir*"]["events"] allow_interrupt events.
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c (DO_IASA).
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirN", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirF", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirB", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirHi", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirLw", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW] = win;
  }

  // Grounded Attack* `allow_interrupt` windows (DO_IASA gate).
  //
  // Decomp:
  // - Grounded attack IASA handlers gate on fp->allow_interrupt before delegating to grounded
  //   interrupt checks (typically ftCo_Wait_IASA).
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Attack1.c,ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_Attack11", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_11] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_Attack12", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_12] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_Attack13", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_13] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackDash", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_DASH] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackS3", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_S3] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackHi3", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_HI3] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackLw3", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_LW3] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackS4", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_S4] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackHi4", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_HI4] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackLw4", &win) == 0) {
    g_allow_interrupt_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_LW4] = win;
  }

  // EscapeN (spotdodge) `allow_interrupt` window.
  //
  // Decomp:
  // - EscapeN motion itself has no explicit IASA callback body, but command-script `allow_interrupt`
  //   still toggles fp->allow_interrupt via ftAction_80071950 and is exposed in Slippi state_flags.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  //
  // Source: data/moves/{fox,falco}.json moves["ftCo_SM_EscapeN"]["events"] allow_interrupt.
  win = (MslFrameWindow){0};
  if (parse_allow_interrupt_window(buf, buf_end, "ftCo_SM_EscapeN", &win) == 0) {
    g_allow_interrupt_by_char_escape_n[char_id] = win;
  }

  // Jab lifecycle command windows (x2218_b1/x2218_b2 ownership).
  //
  // Decomp:
  // - ftAction_80071AE8 sets x2218_b1 (set_jab_combo).
  // - ftAction_80071B28 sets x2218_b2 (set_jab_rapid).
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071AE8,ftAction_80071B28}
  win = (MslFrameWindow){0};
  if (parse_jab_combo_window_open_end(buf, buf_end, "ftCo_SM_Attack11", &win) == 0) {
    g_jab_combo_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_11] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_jab_combo_window_open_end(buf, buf_end, "ftCo_SM_Attack12", &win) == 0) {
    g_jab_combo_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_12] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_jab_rapid_window_open_end(buf, buf_end, "ftCo_SM_Attack12", &win) == 0) {
    g_jab_rapid_by_char_grounded_attack[char_id][MSL_GROUNDED_ATTACK_KIND_12] = win;
  }

  // Dash cmd_var[0] window (used for Dash IASA late transitions).
  //
  // Decomp: Dash IASA gates late transitions on `fp->cmd_vars[0]`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  win = (MslFrameWindow){0};
  if (parse_cmd0_window_open_end(buf, buf_end, "ftCo_SM_Dash", &win) == 0) {
    g_cmd0_by_char_dash[char_id] = win;
  }
  // RunBrake cmd_var[0] window (used for RunBrake -> TurnRun IASA branch).
  //
  // Decomp:
  // - ftCo_RunBrake_Enter resets fp->cmd_vars[0].
  // - ftCo_RunBrake_IASA checks fp->cmd_vars[0] before fn_800C9CEC.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
  //   ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  win = (MslFrameWindow){0};
  if (parse_cmd0_window_open_end(buf, buf_end, "ftCo_SM_RunBrake", &win) == 0) {
    g_cmd0_by_char_runbrake[char_id] = win;
  }
  // EscapeAir cmd_var[0] window (`cmd_skip_decay`).
  //
  // Decomp:
  // - EscapeAir enter clears cmd_vars[0].
  // - the action script later sets cmd_vars[0], and EscapeAir_Phys then switches from the decay
  //   branch to ft_80084DB0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
  //   ftCo_80099A9C,ftCo_EscapeAir_Phys
  // }
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  win = (MslFrameWindow){0};
  if (parse_cmd0_window_open_end(buf, buf_end, "ftCo_SM_EscapeAir", &win) == 0) {
    g_cmd0_by_char_escapeair[char_id] = win;
  }

  // EscapeF script-facing flip trigger.
  //
  // Decomp:
  // - Escape_Anim consumes ftCheckThrowB3(fp) and flips facing when the bit is set.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim
  // refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
  //
  // Source of truth:
  // - data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"]["events"] set_throw_flags(hit_idx=0)
  win = (MslFrameWindow){0};
  if (parse_throw_flags_window_open_end(buf, buf_end, "ftCo_SM_EscapeF", &win) == 0) {
    g_throw_flags_by_char_escape_f[char_id] = win;
  }

  // Catch/CatchDash set_throw_flags triggers (used by CatchPull_Anim -> CatchWait transition).
  //
  // Decomp: CatchPull_Anim tests fp->throw_flags and enters CatchWait (fn_800DA1D8) when set.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  win = (MslFrameWindow){0};
  if (parse_throw_flags_window_open_end(buf, buf_end, "ftCo_SM_Catch", &win) == 0) {
    g_throw_flags_by_char_catch[char_id] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_throw_flags_window_open_end(buf, buf_end, "ftCo_SM_CatchDash", &win) == 0) {
    g_throw_flags_by_char_catchdash[char_id] = win;
  }

  // CatchAttack grabbed-victim hitbox active window (pummel damage timing).
  //
  // Decomp:
  // - CatchWait IASA enters CatchAttack via fn_800DA4FC.
  // - CatchAttack then drives the grabbed-victim CaptureDamage* transition when its grabbed-only
  //   hitbox connects (ftCo_800DC284 / ftCo_800DC3A4).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DA4C0,fn_800DA4FC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DC284,ftCo_800DC3A4}
  //
  // Source of truth:
  // data/moves/{fox,falco}.json moves["ftCo_SM_CatchAttack"]["events"] create_hitbox
  // (only_hit_grabbed=true) and clear_hitboxes.
  win = (MslFrameWindow){0};
  if (parse_catchattack_grabbed_hit_window(buf, buf_end, "ftCo_SM_CatchAttack", &win) == 0) {
    g_catchattack_grabbed_hit_by_char[char_id] = win;
  }

  // Throw release timing + throw hitbox params.
  //
  // Source:
  // - data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags (release)
  // - data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_hitbox
  //
  // Note: parsing is init-time only; per-frame queries must remain alloc-free.
  MslThrowRelease rel = {0};
  MslFrameWindow flip = {0};
  MslFrameWindow cmd1 = {0};
  MslFramePulses spawn_projectile = {0};
  MslThrowHitbox hitboxes[MSL_THROW_HITBOX_IDX_MAX];
  if (parse_throw_release_and_hitboxes(buf, buf_end, "ftCo_SM_ThrowF", &rel, &flip, &cmd1,
                                       &spawn_projectile, hitboxes) == 0) {
    g_throw_release_by_char[char_id][MSL_THROW_KIND_F] = rel;
    g_throw_flip_by_char[char_id][MSL_THROW_KIND_F] = flip;
    g_throw_cmd1_by_char[char_id][MSL_THROW_KIND_F] = cmd1;
    g_throw_spawn_projectile_by_char[char_id][MSL_THROW_KIND_F] = spawn_projectile;
    memcpy(g_throw_hitbox_by_char[char_id][MSL_THROW_KIND_F], hitboxes, sizeof(hitboxes));
  }
  rel = (MslThrowRelease){0};
  flip = (MslFrameWindow){0};
  cmd1 = (MslFrameWindow){0};
  spawn_projectile = (MslFramePulses){0};
  if (parse_throw_release_and_hitboxes(buf, buf_end, "ftCo_SM_ThrowB", &rel, &flip, &cmd1,
                                       &spawn_projectile, hitboxes) == 0) {
    g_throw_release_by_char[char_id][MSL_THROW_KIND_B] = rel;
    g_throw_flip_by_char[char_id][MSL_THROW_KIND_B] = flip;
    g_throw_cmd1_by_char[char_id][MSL_THROW_KIND_B] = cmd1;
    g_throw_spawn_projectile_by_char[char_id][MSL_THROW_KIND_B] = spawn_projectile;
    memcpy(g_throw_hitbox_by_char[char_id][MSL_THROW_KIND_B], hitboxes, sizeof(hitboxes));
  }
  rel = (MslThrowRelease){0};
  flip = (MslFrameWindow){0};
  cmd1 = (MslFrameWindow){0};
  spawn_projectile = (MslFramePulses){0};
  if (parse_throw_release_and_hitboxes(buf, buf_end, "ftCo_SM_ThrowHi", &rel, &flip, &cmd1,
                                       &spawn_projectile, hitboxes) == 0) {
    g_throw_release_by_char[char_id][MSL_THROW_KIND_HI] = rel;
    g_throw_flip_by_char[char_id][MSL_THROW_KIND_HI] = flip;
    g_throw_cmd1_by_char[char_id][MSL_THROW_KIND_HI] = cmd1;
    g_throw_spawn_projectile_by_char[char_id][MSL_THROW_KIND_HI] = spawn_projectile;
    memcpy(g_throw_hitbox_by_char[char_id][MSL_THROW_KIND_HI], hitboxes, sizeof(hitboxes));
  }
  rel = (MslThrowRelease){0};
  flip = (MslFrameWindow){0};
  cmd1 = (MslFrameWindow){0};
  spawn_projectile = (MslFramePulses){0};
  if (parse_throw_release_and_hitboxes(buf, buf_end, "ftCo_SM_ThrowLw", &rel, &flip, &cmd1,
                                       &spawn_projectile, hitboxes) == 0) {
    g_throw_release_by_char[char_id][MSL_THROW_KIND_LW] = rel;
    g_throw_flip_by_char[char_id][MSL_THROW_KIND_LW] = flip;
    g_throw_cmd1_by_char[char_id][MSL_THROW_KIND_LW] = cmd1;
    g_throw_spawn_projectile_by_char[char_id][MSL_THROW_KIND_LW] = spawn_projectile;
    memcpy(g_throw_hitbox_by_char[char_id][MSL_THROW_KIND_LW], hitboxes, sizeof(hitboxes));
  }

  // Pseudo-random SFX command (opcode 38) event pulses for specials keyed by msid.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
  if (parse_specials_by_msid_pseudo_random_sfx(buf, buf_end, char_id) != 0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
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

  // cmd_var[0] windows are extracted from the fighter command scripts into data/moves/*.json.
  // Extractor: tools/extraction/extract_fighter_moves.py
  if (load_one(data_dir, "moves/fox.json", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_one(data_dir, "moves/falco.json", MSL_CHAR_FALCO) != 0) {
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
  // Source: command-script `allow_interrupt` events in data/moves/{fox,falco}.json.
  return (cur_anim_frame_f32 >= (float)win.start_af && cur_anim_frame_f32 < (float)win.end_af) ? 1
                                                                                               : 0;
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

uint8_t move_tables_catchpull_should_enter_wait(uint8_t char_id, uint16_t catch_action_id,
                                                float cur_anim_frame_f32) {
  // Decomp: CatchPull_Anim triggers the CatchWait transition on a throw_flags bit that is set by the
  // move script (`set_throw_flags`) and then cleared when consumed.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  //
  // In this simulator, we approximate the flag mutation using extracted move script event timing:
  // data/moves/{fox,falco}.json moves["ftCo_SM_Catch*"]["events"] set_throw_flags.
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
