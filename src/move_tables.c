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
static MslFrameWindow g_cmd0_by_char_dash[256];
static MslFrameWindow g_throw_flags_by_char_catch[256];
static MslFrameWindow g_throw_flags_by_char_catchdash[256];
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

static int parse_attackair_allow_interrupt_window(const char* buf, const char* buf_end,
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

static int parse_throw_flags_window_open_end(const char* buf, const char* buf_end, const char* move_key,
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
  if (parse_attackair_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirN", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_attackair_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirF", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_attackair_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirB", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_attackair_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirHi", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI] = win;
  }
  win = (MslFrameWindow){0};
  if (parse_attackair_allow_interrupt_window(buf, buf_end, "ftCo_SM_AttackAirLw", &win) == 0) {
    g_allow_interrupt_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW] = win;
  }

  // Dash cmd_var[0] window (used for Dash IASA late transitions).
  //
  // Decomp: Dash IASA gates late transitions on `fp->cmd_vars[0]`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  win = (MslFrameWindow){0};
  if (parse_cmd0_window_open_end(buf, buf_end, "ftCo_SM_Dash", &win) == 0) {
    g_cmd0_by_char_dash[char_id] = win;
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

uint8_t move_tables_catchpull_should_enter_wait(uint8_t char_id, uint16_t catch_action_id,
                                                float cur_anim_frame_f32) {
  // Decomp: CatchPull_Anim triggers the CatchWait transition on a throw_flags bit that is set by the
  // move script (`set_throw_flags`) and then cleared when consumed.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  //
  // In this simulator, we approximate the flag mutation using extracted move script event timing:
  // data/moves/{fox,falco}.json moves["ftCo_SM_Catch*"]["events"] set_throw_flags.
  const MslFrameWindow win =
      (catch_action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL) ? g_throw_flags_by_char_catchdash[char_id]
                                                            : g_throw_flags_by_char_catch[char_id];
  if (!win.loaded) {
    return 0;
  }
  return (cur_anim_frame_f32 >= (float)win.start_af) ? 1 : 0;
}
