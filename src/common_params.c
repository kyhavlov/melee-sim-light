#include "common_params.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

static MslCommonParams g_params;
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

int common_params_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/common/ft_common_data.json", data_dir);
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

  // Input thresholds
  if (json_get_f32(buf, "lstick_deadzone_x", &g_params.lstick_deadzone_x) != 0 ||
      json_get_f32(buf, "lstick_deadzone_y", &g_params.lstick_deadzone_y) != 0 ||
      json_get_f32(buf, "lstick_tilt_x_thresh", &g_params.lstick_tilt_x_thresh) != 0 ||
      json_get_f32(buf, "lstick_tilt_y_thresh", &g_params.lstick_tilt_y_thresh) != 0 ||
      json_get_f32(buf, "trigger_deadzone", &g_params.trigger_deadzone) != 0) {
    alloc_free(buf);
    return -1;
  }

  // Locomotion thresholds (decomp: refs/melee/src/melee/ft/ftwalkcommon.c and ftCommon/ftCo_*.c).
  if (json_get_f32(buf, "walk_stick_threshold", &g_params.walk_stick_threshold) != 0 ||
      json_get_f32(buf, "walk_mid_vel_mul", &g_params.walk_mid_vel_mul) != 0 ||
      json_get_f32(buf, "walk_fast_vel_mul", &g_params.walk_fast_vel_mul) != 0 ||
      json_get_f32(buf, "walk_accel_scale_mul", &g_params.walk_accel_scale_mul) != 0 ||
      json_get_f32(buf, "turn_stick_x_threshold", &g_params.turn_stick_x_threshold) != 0 ||
      json_get_f32(buf, "run_stick_x_threshold", &g_params.run_stick_x_threshold) != 0 ||
      json_get_f32(buf, "dash_flick_abs", &g_params.dash_flick_abs) != 0 ||
      json_get_u8(buf, "dash_flick_tilt_max_frames", &g_params.dash_flick_tilt_max_frames) != 0 ||
      json_get_f32(buf, "dash_iasa_vel_mul", &g_params.dash_iasa_vel_mul) != 0 ||
      json_get_f32(buf, "dash_iasa_x44", &g_params.dash_iasa_x44) != 0 ||
      json_get_f32(buf, "dash_iasa_x48", &g_params.dash_iasa_x48) != 0 ||
      json_get_f32(buf, "dash_iasa_x4c", &g_params.dash_iasa_x4c) != 0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "tap_jump_threshold", &g_params.tap_jump_threshold) != 0 ||
      json_get_f32(buf, "tap_jump_release_threshold", &g_params.tap_jump_release_threshold) != 0 ||
      json_get_f32(buf, "jump_back_x_threshold", &g_params.jump_back_x_threshold) != 0 ||
      json_get_f32(buf, "fastfall_stick_threshold", &g_params.fastfall_stick_threshold) != 0 ||
      json_get_u8(buf, "fastfall_tilt_max_frames", &g_params.fastfall_tilt_max_frames) != 0 ||
      json_get_u8(buf, "tap_jump_tilt_max_frames", &g_params.tap_jump_tilt_max_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "high_speed_friction_mul", &g_params.high_speed_friction_mul) != 0 ||
      json_get_f32(buf, "run_friction_mul", &g_params.run_friction_mul) != 0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "powershield_reflect_trigger_min", &g_params.powershield_reflect_trigger_min) != 0 ||
      json_get_u8(buf, "powershield_reflect_window_frames", &g_params.powershield_reflect_window_frames) != 0 ||
      json_get_u8(buf, "powershield_reflect_frames", &g_params.powershield_reflect_frames) != 0 ||
      json_get_u8(buf, "powershield_reflect_total_frames", &g_params.powershield_reflect_total_frames) != 0 ||
      json_get_f32(buf, "spotdodge_stick_y_threshold", &g_params.spotdodge_stick_y_threshold) != 0 ||
      json_get_f32(buf, "escape_stick_x_threshold", &g_params.escape_stick_x_threshold) != 0 ||
      json_get_u8(buf, "spotdodge_flick_tilt_max_frames", &g_params.spotdodge_flick_tilt_max_frames) != 0 ||
      json_get_u8(buf, "escape_flick_tilt_max_frames", &g_params.escape_flick_tilt_max_frames) != 0 ||
      json_get_f32(buf, "start_shield_health", &g_params.start_shield_health) != 0 ||
      json_get_f32(buf, "shield_recharge_per_frame", &g_params.shield_recharge_per_frame) != 0 ||
      json_get_f32(buf, "shield_hold_drain_mul", &g_params.shield_hold_drain_mul) != 0 ||
      json_get_f32(buf, "shield_hold_drain_base", &g_params.shield_hold_drain_base) != 0 ||
      json_get_f32(buf, "shield_hold_drain_max", &g_params.shield_hold_drain_max) != 0 ||
      json_get_u8(buf, "lcancel_window_frames", &g_params.lcancel_window_frames) != 0 ||
      json_get_f32(buf, "lcancel_lag_div", &g_params.lcancel_lag_div) != 0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
  g_loaded = 1;
  return 0;
}

const MslCommonParams* msl_common_params(void) { return g_loaded ? &g_params : NULL; }
