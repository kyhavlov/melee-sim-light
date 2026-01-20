#include "char_params.h"

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

static MslCharParams g_params_by_char[256];
static uint8_t g_have_params_by_char[256];
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
  *out = (float)v;
  return 0;
}

static int json_get_u8(const char* json, const char* key, uint8_t* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  float f = 0.0f;
  if (json_get_f32(json, key, &f) != 0) {
    return -1;
  }
  if (f < 0.0f) {
    f = 0.0f;
  }
  if (f > 255.0f) {
    f = 255.0f;
  }
  *out = (uint8_t)(int)(f + 0.5f);
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

  MslCharParams out = {0};
  if (json_get_f32(buf, "walk_init_vel", &out.walk_init_vel) != 0 ||
      json_get_f32(buf, "walk_accel", &out.walk_accel) != 0 ||
      json_get_f32(buf, "walk_max_vel", &out.walk_max_vel) != 0 ||
      json_get_f32(buf, "gr_friction", &out.gr_friction) != 0 ||
      json_get_f32(buf, "ground_max_horizontal_velocity", &out.ground_max_horizontal_velocity) !=
          0 ||
      json_get_u8(buf, "turn_frames", &out.turn_frames) != 0 ||
      json_get_u8(buf, "jump_startup_frames", &out.jump_startup_frames) != 0 ||
      json_get_f32(buf, "jump_h_initial_velocity", &out.jump_h_initial_velocity) != 0 ||
      json_get_f32(buf, "jump_v_initial_velocity", &out.jump_v_initial_velocity) != 0 ||
      json_get_f32(buf, "hop_v_initial_velocity", &out.hop_v_initial_velocity) != 0 ||
      json_get_f32(buf, "ground_to_air_jump_momentum_multiplier",
                   &out.ground_to_air_jump_momentum_multiplier) != 0 ||
      json_get_f32(buf, "jump_h_max_velocity", &out.jump_h_max_velocity) != 0 ||
      json_get_u8(buf, "max_jumps", &out.max_jumps) != 0 ||
      json_get_u8(buf, "landing_lag_frames", &out.landing_lag_frames) != 0 ||
      json_get_u8(buf, "landing_airn_lag_frames", &out.landing_airn_lag_frames) != 0 ||
      json_get_u8(buf, "landing_airf_lag_frames", &out.landing_airf_lag_frames) != 0 ||
      json_get_u8(buf, "landing_airb_lag_frames", &out.landing_airb_lag_frames) != 0 ||
      json_get_u8(buf, "landing_airhi_lag_frames", &out.landing_airhi_lag_frames) != 0 ||
      json_get_u8(buf, "landing_airlw_lag_frames", &out.landing_airlw_lag_frames) != 0 ||
      json_get_f32(buf, "grav", &out.grav) != 0 ||
      json_get_f32(buf, "terminal_vel", &out.terminal_vel) != 0 ||
      json_get_f32(buf, "fast_fall_velocity", &out.fast_fall_velocity) != 0 ||
      json_get_f32(buf, "air_max_horizontal_velocity", &out.air_max_horizontal_velocity) != 0 ||
      json_get_f32(buf, "air_drift_stick_mul", &out.air_drift_stick_mul) != 0 ||
      json_get_f32(buf, "aerial_drift_base", &out.aerial_drift_base) != 0 ||
      json_get_f32(buf, "air_drift_max", &out.air_drift_max) != 0 ||
      json_get_f32(buf, "aerial_friction", &out.aerial_friction) != 0 ||
      json_get_f32(buf, "air_jump_v_multiplier", &out.air_jump_v_multiplier) != 0 ||
      json_get_f32(buf, "air_jump_h_multiplier", &out.air_jump_h_multiplier) != 0 ||
      json_get_f32(buf, "dash_initial_velocity", &out.dash_initial_velocity) != 0 ||
      json_get_f32(buf, "dash_run_acceleration_a", &out.dash_run_acceleration_a) != 0 ||
      json_get_f32(buf, "dash_run_acceleration_b", &out.dash_run_acceleration_b) != 0 ||
      json_get_f32(buf, "dash_run_terminal_velocity", &out.dash_run_terminal_velocity) != 0 ||
      json_get_f32(buf, "run_animation_scaling", &out.run_animation_scaling) != 0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
  g_params_by_char[char_id] = out;
  g_have_params_by_char[char_id] = 1;
  return 0;
}

int char_params_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  if (load_one(data_dir, "characters/fox.json", MSL_CHAR_FOX) != 0) {
    return -1;
  }
  if (load_one(data_dir, "characters/falco.json", MSL_CHAR_FALCO) != 0) {
    return -1;
  }

  g_loaded = 1;
  return 0;
}

const MslCharParams* msl_char_params(uint8_t char_id) {
  if (!g_loaded || !g_have_params_by_char[char_id]) {
    return NULL;
  }
  return &g_params_by_char[char_id];
}
