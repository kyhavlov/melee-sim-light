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
    fprintf(stderr, "ft_common_data.json: missing key \"%s\"\n", key);
    return -1;
  }
  p = strchr(p, ':');
  if (p == NULL) {
    fprintf(stderr, "ft_common_data.json: malformed key \"%s\" (missing ':')\n", key);
    return -1;
  }
  p++;
  double v = 0.0;
  if (json_parse_double(p, &v) == NULL) {
    fprintf(stderr, "ft_common_data.json: failed to parse number for key \"%s\"\n", key);
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

static int json_get_u16(const char* json, const char* key, uint16_t* out) {
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
  if (f > 65535.0f) {
    f = 65535.0f;
  }
  *out = (uint16_t)(int)(f + 0.5f);
  return 0;
}

static int json_get_i32(const char* json, const char* key, int32_t* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  float f = 0.0f;
  if (json_get_f32(json, key, &f) != 0) {
    return -1;
  }
  if (f < -2147483648.0f) {
    f = -2147483648.0f;
  }
  if (f > 2147483647.0f) {
    f = 2147483647.0f;
  }
  *out = (int32_t)f;
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
      json_get_f32(buf, "trigger_deadzone", &g_params.trigger_deadzone) != 0 ||
      json_get_f32(buf, "attack_angle_threshold_radians",
                   &g_params.attack_angle_threshold_radians) != 0) {
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
      json_get_f32(buf, "special_stick_x_threshold_side",
                   &g_params.special_stick_x_threshold_side) != 0 ||
      json_get_f32(buf, "special_stick_y_threshold", &g_params.special_stick_y_threshold) != 0 ||
      json_get_f32(buf, "special_side_reverse_threshold",
                   &g_params.special_side_reverse_threshold) != 0 ||
      json_get_f32(buf, "special_neutral_reverse_threshold",
                   &g_params.special_neutral_reverse_threshold) != 0 ||
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
      json_get_f32(buf, "crouch_stick_threshold", &g_params.crouch_stick_threshold) != 0 ||
      json_get_u8(buf, "tap_jump_tilt_max_frames", &g_params.tap_jump_tilt_max_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  // Cliff / ledge common behavior (ftCo_Cliff*).
  if (json_get_f32(buf, "cliff_drop_stick_threshold", &g_params.cliff_drop_stick_threshold) != 0 ||
      json_get_f32(buf, "cliff_wait_percent_threshold", &g_params.cliff_wait_percent_threshold) !=
          0 ||
      json_get_f32(buf, "cliff_wait_frames_low_percent", &g_params.cliff_wait_frames_low_percent) !=
          0 ||
      json_get_f32(buf, "cliff_wait_frames_high_percent",
                   &g_params.cliff_wait_frames_high_percent) != 0 ||
      json_get_f32(buf, "cliff_option_stick_threshold", &g_params.cliff_option_stick_threshold) !=
          0 ||
      json_get_u16(buf, "ledge_cooldown_frames", &g_params.ledge_cooldown_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  // Match flow constants (KO/death/respawn/entry).
  if (json_get_f32(buf, "dead_up_kb_vel_threshold", &g_params.dead_up_kb_vel_threshold) != 0 ||
      json_get_u16(buf, "dead_timer_frames", &g_params.dead_timer_frames) != 0 ||
      json_get_u16(buf, "dead_up_star_initial_frames", &g_params.dead_up_star_initial_frames) !=
          0 ||
      json_get_u16(buf, "dead_up_star_phase1_frames", &g_params.dead_up_star_phase1_frames) != 0 ||
      json_get_u16(buf, "dead_up_star_phase2_frames", &g_params.dead_up_star_phase2_frames) != 0 ||
      json_get_u16(buf, "rebirth_timer_frames", &g_params.rebirth_timer_frames) != 0 ||
      json_get_u16(buf, "rebirth_wait_timer_frames", &g_params.rebirth_wait_timer_frames) != 0 ||
      json_get_u16(buf, "entry_start_frames", &g_params.entry_start_frames) != 0 ||
      json_get_u16(buf, "entry_end_frames", &g_params.entry_end_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "high_speed_friction_mul", &g_params.high_speed_friction_mul) != 0 ||
      json_get_f32(buf, "run_accel_scale_mul", &g_params.run_accel_scale_mul) != 0 ||
      json_get_f32(buf, "run_friction_mul", &g_params.run_friction_mul) != 0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "powershield_reflect_trigger_min",
                   &g_params.powershield_reflect_trigger_min) != 0 ||
      json_get_u8(buf, "powershield_reflect_window_frames",
                  &g_params.powershield_reflect_window_frames) != 0 ||
      json_get_u8(buf, "powershield_reflect_frames", &g_params.powershield_reflect_frames) != 0 ||
      json_get_u8(buf, "powershield_reflect_total_frames",
                  &g_params.powershield_reflect_total_frames) != 0 ||
      json_get_f32(buf, "spotdodge_stick_y_threshold", &g_params.spotdodge_stick_y_threshold) !=
          0 ||
      json_get_f32(buf, "escape_stick_x_threshold", &g_params.escape_stick_x_threshold) != 0 ||
      json_get_u8(buf, "spotdodge_flick_tilt_max_frames",
                  &g_params.spotdodge_flick_tilt_max_frames) != 0 ||
      json_get_u8(buf, "escape_flick_tilt_max_frames", &g_params.escape_flick_tilt_max_frames) !=
          0 ||
      json_get_f32(buf, "guard_stick_lerp_x44c", &g_params.guard_stick_lerp_x44c) != 0 ||
      json_get_f32(buf, "start_shield_health", &g_params.start_shield_health) != 0 ||
      json_get_f32(buf, "shield_size_lightshield_min", &g_params.shield_size_lightshield_min) !=
          0 ||
      json_get_f32(buf, "shield_size_lightshield_max", &g_params.shield_size_lightshield_max) !=
          0 ||
      json_get_f32(buf, "shield_size_min_scale", &g_params.shield_size_min_scale) != 0 ||
      json_get_f32(buf, "shield_recharge_per_frame", &g_params.shield_recharge_per_frame) != 0 ||
      json_get_f32(buf, "shield_hold_drain_mul", &g_params.shield_hold_drain_mul) != 0 ||
      json_get_f32(buf, "shield_hold_drain_base", &g_params.shield_hold_drain_base) != 0 ||
      json_get_f32(buf, "shield_hold_drain_max", &g_params.shield_hold_drain_max) != 0 ||
      json_get_f32(buf, "shield_hit_damage_mul", &g_params.shield_hit_damage_mul) != 0 ||
      json_get_f32(buf, "shield_hit_damage_base", &g_params.shield_hit_damage_base) != 0 ||
      json_get_f32(buf, "shield_hit_lightshield_min", &g_params.shield_hit_lightshield_min) != 0 ||
      json_get_f32(buf, "shield_hit_lightshield_max", &g_params.shield_hit_lightshield_max) != 0 ||
      json_get_f32(buf, "shield_stun_mul", &g_params.shield_stun_mul) != 0 ||
      json_get_f32(buf, "shield_stun_base", &g_params.shield_stun_base) != 0 ||
      json_get_f32(buf, "shield_stun_lightshield_min", &g_params.shield_stun_lightshield_min) !=
          0 ||
      json_get_f32(buf, "shield_stun_lightshield_max", &g_params.shield_stun_lightshield_max) !=
          0 ||
      json_get_f32(buf, "shield_setoff_push_mul", &g_params.shield_setoff_push_mul) != 0 ||
      json_get_f32(buf, "shield_setoff_push_max", &g_params.shield_setoff_push_max) != 0 ||
      json_get_f32(buf, "shield_setoff_push_mul_non_yoshi",
                   &g_params.shield_setoff_push_mul_non_yoshi) != 0 ||
      json_get_u8(buf, "lcancel_window_frames", &g_params.lcancel_window_frames) != 0 ||
      json_get_f32(buf, "lcancel_lag_div", &g_params.lcancel_lag_div) != 0 ||
      json_get_f32(buf, "landing_fall_special_lag_frames",
                   &g_params.landing_fall_special_lag_frames) != 0 ||
      json_get_f32(buf, "escapeair_deadzone_x", &g_params.escapeair_deadzone_x) != 0 ||
      json_get_f32(buf, "escapeair_deadzone_y", &g_params.escapeair_deadzone_y) != 0 ||
      json_get_u8(buf, "escapeair_timer_frames", &g_params.escapeair_timer_frames) != 0 ||
      json_get_f32(buf, "escapeair_force", &g_params.escapeair_force) != 0 ||
      json_get_f32(buf, "escapeair_decay", &g_params.escapeair_decay) != 0 ||
      json_get_f32(buf, "fall_special_mobility_scalar", &g_params.fall_special_mobility_scalar) !=
          0 ||
      json_get_f32(buf, "hitlag_dmg_mul", &g_params.hitlag_dmg_mul) != 0 ||
      json_get_f32(buf, "hitlag_base", &g_params.hitlag_base) != 0 ||
      json_get_f32(buf, "hitlag_squat_mul", &g_params.hitlag_squat_mul) != 0 ||
      json_get_f32(buf, "air_drift_overmax_friction", &g_params.air_drift_overmax_friction) != 0 ||
      json_get_f32(buf, "kb_weight_mul", &g_params.kb_weight_mul) != 0 ||
      json_get_f32(buf, "kb_weight_mul2", &g_params.kb_weight_mul2) != 0 ||
      json_get_f32(buf, "kb_applied_max", &g_params.kb_applied_max) != 0 ||
      json_get_f32(buf, "kb_base_term", &g_params.kb_base_term) != 0 ||
      json_get_f32(buf, "kb_dmg_mul", &g_params.kb_dmg_mul) != 0 ||
      json_get_f32(buf, "kb_wsk_mul", &g_params.kb_wsk_mul) != 0 ||
      json_get_f32(buf, "kb_growth_mul", &g_params.kb_growth_mul) != 0 ||
      json_get_f32(buf, "kb_base_add", &g_params.kb_base_add) != 0 ||
      json_get_f32(buf, "kb_vel_mul", &g_params.kb_vel_mul) != 0 ||
      json_get_f32(buf, "kb_min", &g_params.kb_min) != 0 ||
      json_get_f32(buf, "kb_squat_mul", &g_params.kb_squat_mul) != 0 ||
      json_get_f32(buf, "kb_ice_mul", &g_params.kb_ice_mul) != 0 ||
      json_get_f32(buf, "kb_smashcharge_mul", &g_params.kb_smashcharge_mul) != 0 ||
      json_get_i32(buf, "ftcoll_percent_base_x6d4", &g_params.ftcoll_percent_base_x6d4) != 0 ||
      json_get_i32(buf, "ftcoll_percent_base_x6d8", &g_params.ftcoll_percent_base_x6d8) != 0 ||
      json_get_f32(buf, "damage_hitstun_mul", &g_params.damage_hitstun_mul) != 0 ||
      json_get_f32(buf, "damage_severity_x158", &g_params.damage_severity_x158) != 0 ||
      json_get_f32(buf, "damage_severity_x15c", &g_params.damage_severity_x15c) != 0 ||
      json_get_f32(buf, "damage_severity_x160", &g_params.damage_severity_x160) != 0 ||
      json_get_f32(buf, "damagefly_top_angle_min_radians",
                   &g_params.damagefly_top_angle_min_radians) != 0 ||
      json_get_f32(buf, "damagefly_top_angle_max_radians",
                   &g_params.damagefly_top_angle_max_radians) != 0 ||
      json_get_f32(buf, "sakurai_air_radians", &g_params.sakurai_air_radians) != 0 ||
      json_get_f32(buf, "sakurai_ground_deg_max", &g_params.sakurai_ground_deg_max) != 0 ||
      json_get_f32(buf, "sakurai_kb_threshold", &g_params.sakurai_kb_threshold) != 0 ||
      json_get_f32(buf, "sakurai_kb_max", &g_params.sakurai_kb_max) != 0 ||
      json_get_f32(buf, "air_motion_kb_mul", &g_params.air_motion_kb_mul) != 0 ||
      json_get_u8(buf, "air_motion_max_frames", &g_params.air_motion_max_frames) != 0 ||
      json_get_u8(buf, "tech_lr_debounce_frames", &g_params.tech_lr_debounce_frames) != 0 ||
      json_get_f32(buf, "tech_window_frames", &g_params.tech_window_frames) != 0 ||
      json_get_f32(buf, "tech_roll_stick_threshold", &g_params.tech_roll_stick_threshold) != 0 ||
      json_get_f32(buf, "damagefly_downbound_kb_vel_threshold",
                   &g_params.damagefly_downbound_kb_vel_threshold) != 0 ||
      json_get_f32(buf, "damagefly_landing_kb_vel_threshold",
                   &g_params.damagefly_landing_kb_vel_threshold) != 0 ||
      json_get_f32(buf, "down_stand_stick_y_threshold", &g_params.down_stand_stick_y_threshold) !=
          0 ||
      json_get_f32(buf, "down_stick_x_threshold", &g_params.down_stick_x_threshold) != 0 ||
      json_get_f32(buf, "down_attack_button_window_frames",
                   &g_params.down_attack_button_window_frames) != 0 ||
      json_get_f32(buf, "down_attack_cstick_up_threshold",
                   &g_params.down_attack_cstick_up_threshold) != 0 ||
      json_get_u16(buf, "combo_timer_post_hitstun_frames",
                   &g_params.combo_timer_post_hitstun_frames) != 0 ||
      json_get_f32(buf, "down_wait_frames", &g_params.down_wait_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
  g_loaded = 1;
  return 0;
}

const MslCommonParams* msl_common_params(void) { return g_loaded ? &g_params : NULL; }
