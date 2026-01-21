#include "data_tables.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "action_ids.h"
#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

typedef struct MslCmdVar0Window {
  int16_t start_af;  // inclusive (0-based action_frame)
  int16_t end_af;    // exclusive (0-based action_frame)
  uint8_t loaded;
} MslCmdVar0Window;

enum { MSL_ATTACKAIR_KIND_COUNT = 5 };
enum {
  MSL_ATTACKAIR_KIND_N = 0,
  MSL_ATTACKAIR_KIND_F = 1,
  MSL_ATTACKAIR_KIND_B = 2,
  MSL_ATTACKAIR_KIND_HI = 3,
  MSL_ATTACKAIR_KIND_LW = 4,
};

static MslCmdVar0Window g_cmd0_by_char_attackair[256][MSL_ATTACKAIR_KIND_COUNT];
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
                                       MslCmdVar0Window* out) {
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

  MslCmdVar0Window win = {0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirN", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_N] = win;

  win = (MslCmdVar0Window){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirF", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_F] = win;

  win = (MslCmdVar0Window){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirB", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_B] = win;

  win = (MslCmdVar0Window){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirHi", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_HI] = win;

  win = (MslCmdVar0Window){0};
  if (parse_attackair_cmd0_window(buf, buf_end, "ftCo_SM_AttackAirLw", &win) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_cmd0_by_char_attackair[char_id][MSL_ATTACKAIR_KIND_LW] = win;

  alloc_free(buf);
  return 0;
}

int data_tables_init(void) {
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

uint8_t data_tables_attackair_cmd0_active(uint8_t char_id, uint16_t attackair_action_id,
                                          int16_t action_frame) {
  const int kind = attackair_kind_from_action(attackair_action_id);
  if (kind < 0) {
    return 0;
  }

  const MslCmdVar0Window win = g_cmd0_by_char_attackair[char_id][(size_t)kind];
  if (!win.loaded) {
    // Conservative fallback: treat as auto-cancel (no landing lag).
    return 0;
  }

  // Decomp: ftCo_LandingAir_EnterWithLag uses fp->cmd_vars[0] to pick between LandingAir* (lag)
  // and Landing_Enter_Basic (auto-cancel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
  return (action_frame >= win.start_af && action_frame < win.end_af) ? 1 : 0;
}
