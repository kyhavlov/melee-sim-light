#include "item_common_params.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "msl_math.h"

static MslItemCommonParams g_params;
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
  if (out != NULL) {
    *out = v;
  }
  return end;
}

static int json_get_f32(const char* json, const char* key, float* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  char pat[128];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat)) {
    return -1;
  }
  const char* p = strstr(json, pat);
  if (p == NULL) {
    fprintf(stderr, "item_common.json: missing key \"%s\"\n", key);
    return -1;
  }
  p = strchr(p, ':');
  if (p == NULL) {
    fprintf(stderr, "item_common.json: malformed key \"%s\" (missing ':')\n", key);
    return -1;
  }
  p++;
  double v = 0.0;
  if (json_parse_double(p, &v) == NULL) {
    fprintf(stderr, "item_common.json: failed to parse number for key \"%s\"\n", key);
    return -1;
  }
  *out = (float)v;
  return 0;
}

int item_common_params_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/items/item_common.json", data_dir);
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

  if (json_get_f32(buf, "shield_bounce_extra_degrees", &g_params.shield_bounce_extra_degrees) !=
      0) {
    alloc_free(buf);
    return -1;
  }
  if (json_get_f32(buf, "item_hitlag_damage_mul", &g_params.item_hitlag_damage_mul) != 0 ||
      json_get_f32(buf, "item_hitlag_base", &g_params.item_hitlag_base) != 0) {
    alloc_free(buf);
    return -1;
  }
  g_params.shield_bounce_threshold_radians =
      ((90.0f + g_params.shield_bounce_extra_degrees) * MSL_PI_F) / 180.0f;

  alloc_free(buf);
  g_loaded = 1;
  return 0;
}

const MslItemCommonParams* msl_item_common_params(void) { return g_loaded ? &g_params : NULL; }
