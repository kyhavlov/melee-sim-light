#include "special_msids.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static MslSpecialMsids g_msids_by_char[256];
static uint8_t g_have_msids_by_char[256];
static int g_loaded = 0;

static const char* json_skip_ws(const char* s) {
  while (s && *s && isspace((unsigned char)*s)) {
    s++;
  }
  return s;
}

static const char* json_parse_double(const char* s, double* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  char* end = NULL;
  errno = 0;
  double v = strtod(s, &end);
  if (end == s || errno != 0) {
    return NULL;
  }
  if (out) {
    *out = v;
  }
  return end;
}

static int json_get_u16_path3(const char* json, const char* k0, const char* k1, const char* k2,
                              uint16_t* out) {
  if (json == NULL || k0 == NULL || k1 == NULL || k2 == NULL || out == NULL) {
    return -1;
  }
  char pat0[64];
  char pat1[64];
  char pat2[64];
  if (snprintf(pat0, sizeof(pat0), "\"%s\"", k0) <= 0) {
    return -1;
  }
  if (snprintf(pat1, sizeof(pat1), "\"%s\"", k1) <= 0) {
    return -1;
  }
  if (snprintf(pat2, sizeof(pat2), "\"%s\"", k2) <= 0) {
    return -1;
  }

  const char* p = strstr(json, pat0);
  if (p == NULL) {
    return -1;
  }
  p = strstr(p, pat1);
  if (p == NULL) {
    return -1;
  }
  p = strstr(p, pat2);
  if (p == NULL) {
    return -1;
  }
  p = strchr(p, ':');
  if (p == NULL) {
    return -1;
  }
  p++;

  double v = 0.0;
  if (json_parse_double(p, &v) == NULL) {
    return -1;
  }
  if (!(v >= 0.0 && v <= 65535.0)) {
    return -1;
  }
  *out = (uint16_t)(unsigned int)(v + 0.5);
  return 0;
}

static int load_one(const char* data_dir, const char* rel_path, uint8_t char_id) {
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

  MslSpecialMsids out = {0};
  if (json_get_u16_path3(buf, "down_ground", "start", "default", &out.speciallw_ground_start) !=
          0 ||
      json_get_u16_path3(buf, "down_ground", "loop", "default", &out.speciallw_ground_loop) != 0 ||
      json_get_u16_path3(buf, "down_ground", "hit", "default", &out.speciallw_ground_hit) != 0 ||
      json_get_u16_path3(buf, "down_ground", "end", "default", &out.speciallw_ground_end) != 0 ||
      json_get_u16_path3(buf, "down_air", "start", "default", &out.speciallw_air_start) != 0 ||
      json_get_u16_path3(buf, "down_air", "loop", "default", &out.speciallw_air_loop) != 0 ||
      json_get_u16_path3(buf, "down_air", "hit", "default", &out.speciallw_air_hit) != 0 ||
      json_get_u16_path3(buf, "down_air", "end", "default", &out.speciallw_air_end) != 0 ||

      json_get_u16_path3(buf, "side_ground", "start", "default", &out.specials_ground_start) != 0 ||
      json_get_u16_path3(buf, "side_ground", "main", "default", &out.specials_ground_main) != 0 ||
      json_get_u16_path3(buf, "side_ground", "end", "default", &out.specials_ground_end) != 0 ||
      json_get_u16_path3(buf, "side_air", "start", "default", &out.specials_air_start) != 0 ||
      json_get_u16_path3(buf, "side_air", "main", "default", &out.specials_air_main) != 0 ||
      json_get_u16_path3(buf, "side_air", "end", "default", &out.specials_air_end) != 0 ||

      json_get_u16_path3(buf, "up_ground", "hold", "default", &out.specialhi_ground_hold) != 0 ||
      json_get_u16_path3(buf, "up_ground", "main", "default", &out.specialhi_ground_main) != 0 ||
      json_get_u16_path3(buf, "up_air", "hold", "default", &out.specialhi_air_hold) != 0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
  g_msids_by_char[char_id] = out;
  g_have_msids_by_char[char_id] = 1;
  return 0;
}

int special_msids_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_one(data_dir, "special_msids/fox.json", (uint8_t)MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_one(data_dir, "special_msids/falco.json", (uint8_t)MSL_CHAR_FALCO) != 0) {
    return -1;
  }

  g_loaded = 1;
  return 0;
}

const MslSpecialMsids* msl_special_msids(uint8_t char_id) {
  if (!g_loaded || !g_have_msids_by_char[char_id]) {
    return NULL;
  }
  return &g_msids_by_char[char_id];
}
