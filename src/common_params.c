#include "common_params.h"
#include "data_dir.h"

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

static int json_get_f32_with_default(const char* json, const char* key, float default_v,
                                     float* out);

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

static int json_get_f32_with_default(const char* json, const char* key, float default_v,
                                     float* out) {
  if (json_get_f32(json, key, out) == 0) {
    return 0;
  }
  *out = default_v;
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

  const char* data_dir = msl_data_dir();

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
      json_get_f32(buf, "z_button_trigger_value", &g_params.z_button_trigger_value) != 0 ||
      json_get_f32(buf, "attack_angle_threshold_radians",
                   &g_params.attack_angle_threshold_radians) != 0 ||
      json_get_f32(buf, "attackair_stick_deadzone_x", &g_params.attackair_stick_deadzone_x) != 0 ||
      json_get_f32(buf, "attackair_stick_deadzone_y", &g_params.attackair_stick_deadzone_y) != 0 ||
      json_get_f32(buf, "smash_stick_threshold", &g_params.smash_stick_threshold) != 0 ||
      json_get_f32(buf, "cstick_smash_threshold", &g_params.cstick_smash_threshold) != 0 ||
      json_get_f32(buf, "attack_s3_stick_threshold_x", &g_params.attack_s3_stick_threshold_x) !=
          0 ||
      json_get_f32(buf, "attack_s3_hi_angle_radians", &g_params.attack_s3_hi_angle_radians) != 0 ||
      json_get_f32(buf, "attack_s3_hi_s_angle_radians", &g_params.attack_s3_hi_s_angle_radians) !=
          0 ||
      json_get_f32(buf, "attack_s3_lw_s_angle_radians", &g_params.attack_s3_lw_s_angle_radians) !=
          0 ||
      json_get_f32(buf, "attack_s3_lw_angle_radians", &g_params.attack_s3_lw_angle_radians) != 0 ||
      json_get_f32(buf, "attack_hi3_stick_threshold_y", &g_params.attack_hi3_stick_threshold_y) !=
          0 ||
      json_get_f32(buf, "attack_lw3_stick_threshold_y", &g_params.attack_lw3_stick_threshold_y) !=
          0 ||
      json_get_f32(buf, "throw_anim_speed_weight_mul", &g_params.throw_anim_speed_weight_mul) !=
          0 ||
      json_get_f32(buf, "attack_hi4_stick_threshold_y", &g_params.attack_hi4_stick_threshold_y) !=
          0 ||
      json_get_u8(buf, "attack_hi4_tilt_max_frames", &g_params.attack_hi4_tilt_max_frames) != 0 ||
      json_get_f32(buf, "attack_lw4_stick_threshold_y", &g_params.attack_lw4_stick_threshold_y) !=
          0 ||
      json_get_u8(buf, "attack_lw4_tilt_max_frames", &g_params.attack_lw4_tilt_max_frames) != 0) {
    alloc_free(buf);
    return -1;
  }

  // Locomotion thresholds (decomp: refs/melee/src/melee/ft/ftwalkcommon.c and ftCommon/ftCo_*.c).
  if (json_get_f32(buf, "walk_stick_threshold", &g_params.walk_stick_threshold) != 0 ||
      json_get_f32(buf, "walk_mid_vel_mul", &g_params.walk_mid_vel_mul) != 0 ||
      json_get_f32(buf, "walk_fast_vel_mul", &g_params.walk_fast_vel_mul) != 0 ||
      json_get_f32(buf, "walk_accel_scale_mul", &g_params.walk_accel_scale_mul) != 0 ||
      json_get_f32(buf, "ottotto_walk_stick_x_threshold",
                   &g_params.ottotto_walk_stick_x_threshold) != 0 ||
      json_get_f32(buf, "turn_stick_x_threshold", &g_params.turn_stick_x_threshold) != 0 ||
      json_get_f32(buf, "turn_run_stick_x_threshold", &g_params.turn_run_stick_x_threshold) != 0 ||
      json_get_f32(buf, "run_stick_x_threshold", &g_params.run_stick_x_threshold) != 0 ||
      json_get_f32(buf, "run_x0_init_x430", &g_params.run_x0_init_x430) != 0 ||
      json_get_f32(buf, "common_fall_blend_air_drift_threshold",
                   &g_params.common_fall_blend_air_drift_threshold) != 0 ||
      json_get_f32(buf, "common_fall_blend_lerp", &g_params.common_fall_blend_lerp) != 0 ||
      json_get_f32(buf, "attackdash_friction_mul", &g_params.attackdash_friction_mul) != 0 ||
      json_get_u8(buf, "attackdash_x0_init_frames", &g_params.attackdash_x0_init_frames) != 0 ||
      json_get_f32(buf, "special_stick_x_threshold_side",
                   &g_params.special_stick_x_threshold_side) != 0 ||
      json_get_f32(buf, "special_stick_y_threshold", &g_params.special_stick_y_threshold) != 0 ||
      json_get_f32(buf, "damagefall_fall_stick_x_threshold",
                   &g_params.damagefall_fall_stick_x_threshold) != 0 ||
      json_get_u8(buf, "damagefall_fall_tilt_max_frames",
                  &g_params.damagefall_fall_tilt_max_frames) != 0 ||
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
      json_get_f32(buf, "dash_run_jump_stick_y_threshold",
                   &g_params.dash_run_jump_stick_y_threshold) != 0 ||
      json_get_f32(buf, "grab_mash_stick_threshold", &g_params.grab_mash_stick_threshold) != 0 ||
      json_get_f32(buf, "capture_grab_timer_base", &g_params.capture_grab_timer_base) != 0 ||
      json_get_f32(buf, "capture_grab_timer_handicap_mul",
                   &g_params.capture_grab_timer_handicap_mul) != 0 ||
      json_get_f32(buf, "capture_grab_timer_handicap_base",
                   &g_params.capture_grab_timer_handicap_base) != 0 ||
      json_get_f32(buf, "capture_grab_timer_slot_mul", &g_params.capture_grab_timer_slot_mul) !=
          0 ||
      json_get_f32(buf, "capture_grab_timer_slot_base", &g_params.capture_grab_timer_slot_base) !=
          0 ||
      json_get_f32(buf, "capture_grab_timer_percent_mul",
                   &g_params.capture_grab_timer_percent_mul) != 0 ||
      json_get_f32(buf, "capture_cut_escape_speed", &g_params.capture_cut_escape_speed) != 0 ||
      json_get_f32(buf, "capture_jump_escape_speed_x", &g_params.capture_jump_escape_speed_x) !=
          0 ||
      json_get_f32(buf, "capture_jump_escape_speed_y", &g_params.capture_jump_escape_speed_y) !=
          0 ||
      json_get_f32(buf, "capture_wait_grab_timer_decrement",
                   &g_params.capture_wait_grab_timer_decrement) != 0 ||
      json_get_f32(buf, "capture_wait_grab_mash_damage", &g_params.capture_wait_grab_mash_damage) !=
          0 ||
      json_get_f32(buf, "capture_wait_jump_latch_window_frames",
                   &g_params.capture_wait_jump_latch_window_frames) != 0 ||
      json_get_f32(buf, "capture_wait_anim_rate_hold_frames",
                   &g_params.capture_wait_anim_rate_hold_frames) != 0 ||
      json_get_f32(buf, "capture_wait_anim_rate", &g_params.capture_wait_anim_rate) != 0 ||
      json_get_f32(buf, "capture_pulled_lw_air_delta_y", &g_params.capture_pulled_lw_air_delta_y) !=
          0 ||
      json_get_f32(buf, "fastfall_stick_threshold", &g_params.fastfall_stick_threshold) != 0 ||
      json_get_u8(buf, "fastfall_tilt_max_frames", &g_params.fastfall_tilt_max_frames) != 0 ||
      json_get_f32(buf, "crouch_stick_threshold", &g_params.crouch_stick_threshold) != 0 ||
      json_get_f32(buf, "crouch_release_stick_threshold",
                   &g_params.crouch_release_stick_threshold) != 0 ||
      json_get_u8(buf, "tap_jump_tilt_max_frames", &g_params.tap_jump_tilt_max_frames) != 0 ||
      json_get_f32(buf, "pass_stick_threshold", &g_params.pass_stick_threshold) != 0 ||
      json_get_u8(buf, "pass_tilt_max_frames", &g_params.pass_tilt_max_frames) != 0 ||
      json_get_u8(buf, "floor_skip_frames", &g_params.floor_skip_frames) != 0 ||
      json_get_f32(buf, "pass_vel_y", &g_params.pass_vel_y) != 0) {
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
      json_get_u16(buf, "ledge_cooldown_frames", &g_params.ledge_cooldown_frames) != 0 ||
      json_get_u16(buf, "colanim_throw_x1994_frames", &g_params.colanim_throw_x1994_frames) != 0 ||
      json_get_u16(buf, "colanim_cliff_x1990_frames", &g_params.colanim_cliff_x1990_frames) != 0 ||
      json_get_u16(buf, "colanim_damage_x1994_frames", &g_params.colanim_damage_x1994_frames) !=
          0) {
    alloc_free(buf);
    return -1;
  }

  // Match flow constants (KO/death/respawn/entry).
  if (json_get_f32(buf, "dead_up_kb_vel_threshold", &g_params.dead_up_kb_vel_threshold) != 0 ||
      json_get_u16(buf, "dead_timer_frames", &g_params.dead_timer_frames) != 0 ||
      json_get_u16(buf, "dead_up_fall_select_percent", &g_params.dead_up_fall_select_percent) !=
          0 ||
      json_get_u16(buf, "dead_up_star_initial_frames", &g_params.dead_up_star_initial_frames) !=
          0 ||
      json_get_u16(buf, "dead_up_star_phase1_frames", &g_params.dead_up_star_phase1_frames) != 0 ||
      json_get_u16(buf, "dead_up_star_phase2_frames", &g_params.dead_up_star_phase2_frames) != 0 ||
      json_get_f32(buf, "dead_up_star_phase1_z_vel_total",
                   &g_params.dead_up_star_phase1_z_vel_total) != 0 ||
      json_get_f32(buf, "dead_up_star_phase1_cam_top_mul",
                   &g_params.dead_up_star_phase1_cam_top_mul) != 0 ||
      json_get_u16(buf, "dead_up_fall_entry_hold_frames",
                   &g_params.dead_up_fall_entry_hold_frames) != 0 ||
      json_get_u16(buf, "dead_up_fall_lerp_frames", &g_params.dead_up_fall_lerp_frames) != 0 ||
      json_get_u16(buf, "dead_up_fall_hitcamera_hold_frames",
                   &g_params.dead_up_fall_hitcamera_hold_frames) != 0 ||
      json_get_u16(buf, "dead_up_fall_phase3_frames", &g_params.dead_up_fall_phase3_frames) != 0 ||
      json_get_u16(buf, "dead_up_fall_phase4_frames", &g_params.dead_up_fall_phase4_frames) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_start_x", &g_params.dead_up_fall_lerp_start_x) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_start_y", &g_params.dead_up_fall_lerp_start_y) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_start_z", &g_params.dead_up_fall_lerp_start_z) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_end_x", &g_params.dead_up_fall_lerp_end_x) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_end_y", &g_params.dead_up_fall_lerp_end_y) != 0 ||
      json_get_f32(buf, "dead_up_fall_lerp_end_z", &g_params.dead_up_fall_lerp_end_z) != 0 ||
      json_get_f32(buf, "dead_up_fall_initial_self_vel_y",
                   &g_params.dead_up_fall_initial_self_vel_y) != 0 ||
      json_get_f32(buf, "dead_up_fall_phase3_gravity", &g_params.dead_up_fall_phase3_gravity) !=
          0 ||
      json_get_f32(buf, "dead_up_fall_phase3_terminal_vel",
                   &g_params.dead_up_fall_phase3_terminal_vel) != 0 ||
      json_get_f32(buf, "dead_up_fall_initial_self_vel_z",
                   &g_params.dead_up_fall_initial_self_vel_z) != 0 ||
      json_get_f32(buf, "dead_up_fall_ice_rot_speed", &g_params.dead_up_fall_ice_rot_speed) != 0 ||
      json_get_u16(buf, "rebirth_timer_frames", &g_params.rebirth_timer_frames) != 0 ||
      json_get_u16(buf, "rebirth_wait_timer_frames", &g_params.rebirth_wait_timer_frames) != 0 ||
      json_get_u16(buf, "colanim_rebirth_fall_x1994_frames",
                   &g_params.colanim_rebirth_fall_x1994_frames) != 0 ||
      json_get_u16(buf, "entry_start_frames", &g_params.entry_start_frames) != 0 ||
      json_get_u16(buf, "entry_end_frames", &g_params.entry_end_frames) != 0 ||
      json_get_u16(buf, "passivewall_timer_frames", &g_params.passivewall_timer_frames) != 0 ||
      json_get_u16(buf, "colanim_passivewall_x1990_frames",
                   &g_params.colanim_passivewall_x1990_frames) != 0 ||
      json_get_f32(buf, "walljump_input_window_frames", &g_params.walljump_input_window_frames) !=
          0 ||
      json_get_f32(buf, "walljump_stick_x_threshold", &g_params.walljump_stick_x_threshold) != 0 ||
      json_get_f32(buf, "walljump_tilt_x_max_frames", &g_params.walljump_tilt_x_max_frames) != 0 ||
      json_get_u16(buf, "walljump_startup_timer_frames", &g_params.walljump_startup_timer_frames) !=
          0 ||
      json_get_f32(buf, "pokemon_stadium_x34_scale_z", &g_params.pokemon_stadium_x34_scale_z) !=
          0) {
    alloc_free(buf);
    return -1;
  }

  if (json_get_f32(buf, "catch_friction_mul", &g_params.catch_friction_mul) != 0 ||
      json_get_f32(buf, "high_speed_friction_mul", &g_params.high_speed_friction_mul) != 0 ||
      json_get_f32(buf, "player_nudge_x", &g_params.player_nudge_x) != 0 ||
      json_get_f32(buf, "player_nudge_z", &g_params.player_nudge_z) != 0 ||
      json_get_f32(buf, "player_nudge_z_max", &g_params.player_nudge_z_max) != 0 ||
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
      json_get_f32(buf, "powershield_reflect_size", &g_params.powershield_reflect_size) != 0 ||
      json_get_f32(buf, "powershield_reflect_damage_mul",
                   &g_params.powershield_reflect_damage_mul) != 0 ||
      json_get_f32(buf, "powershield_reflect_speed_mul", &g_params.powershield_reflect_speed_mul) !=
          0 ||
      json_get_u8(buf, "powershield_reflect_total_frames",
                  &g_params.powershield_reflect_total_frames) != 0 ||
      json_get_u8(buf, "guard_special_enable_frames", &g_params.guard_special_enable_frames) != 0 ||
      json_get_f32(buf, "spotdodge_stick_y_threshold", &g_params.spotdodge_stick_y_threshold) !=
          0 ||
      json_get_f32(buf, "escape_stick_x_threshold", &g_params.escape_stick_x_threshold) != 0 ||
      json_get_u8(buf, "spotdodge_flick_tilt_max_frames",
                  &g_params.spotdodge_flick_tilt_max_frames) != 0 ||
      json_get_u8(buf, "escape_flick_tilt_max_frames", &g_params.escape_flick_tilt_max_frames) !=
          0 ||
      json_get_f32(buf, "guard_stick_lerp_x44c", &g_params.guard_stick_lerp_x44c) != 0 ||
      json_get_f32(buf, "start_shield_health", &g_params.start_shield_health) != 0 ||
      json_get_f32(buf, "shield_break_reset_health", &g_params.shield_break_reset_health) != 0 ||
      json_get_f32(buf, "furafura_timer_percent_base", &g_params.furafura_timer_percent_base) !=
          0 ||
      json_get_f32(buf, "furafura_timer_base", &g_params.furafura_timer_base) != 0 ||
      json_get_f32(buf, "furafura_timer_decrement", &g_params.furafura_timer_decrement) != 0 ||
      json_get_f32(buf, "furafura_mash_decrement", &g_params.furafura_mash_decrement) != 0 ||
      json_get_f32(buf, "shield_size_lightshield_min", &g_params.shield_size_lightshield_min) !=
          0 ||
      json_get_f32(buf, "shield_size_lightshield_max", &g_params.shield_size_lightshield_max) !=
          0 ||
      json_get_f32(buf, "shield_size_min_scale", &g_params.shield_size_min_scale) != 0 ||
      json_get_f32(buf, "guard_x10_init_frames", &g_params.guard_x10_init_frames) != 0 ||
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
      json_get_f32(buf, "phantom_overlap_max_x7a8", &g_params.phantom_overlap_max_x7a8) != 0 ||
      json_get_u16(buf, "magnify_damage_interval_frames",
                   &g_params.magnify_damage_interval_frames) != 0 ||
      json_get_u16(buf, "magnify_damage_percent_limit", &g_params.magnify_damage_percent_limit) !=
          0 ||
      json_get_u16(buf, "magnify_damage_amount", &g_params.magnify_damage_amount) != 0 ||
      json_get_u8(buf, "lcancel_window_frames", &g_params.lcancel_window_frames) != 0 ||
      json_get_f32(buf, "lcancel_lag_div", &g_params.lcancel_lag_div) != 0 ||
      json_get_f32(buf, "landing_fall_special_lag_frames",
                   &g_params.landing_fall_special_lag_frames) != 0 ||
      json_get_f32(buf, "basic_landing_wait_gravity_mult_x30",
                   &g_params.basic_landing_wait_gravity_mult_x30) != 0 ||
      json_get_f32(buf, "basic_landing_wait_scale_param_x310",
                   &g_params.basic_landing_wait_scale_param_x310) != 0 ||
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
      json_get_f32(buf, "hitlag_electric_mul", &g_params.hitlag_electric_mul) != 0 ||
      json_get_i32(buf, "clank_damage_diff_threshold", &g_params.clank_damage_diff_threshold) !=
          0 ||
      json_get_f32(buf, "rebound_damage_x191c_mul", &g_params.rebound_damage_x191c_mul) != 0 ||
      json_get_f32(buf, "rebound_damage_x191c_base", &g_params.rebound_damage_x191c_base) != 0 ||
      json_get_f32(buf, "rebound_ground_x0_mul", &g_params.rebound_ground_x0_mul) != 0 ||
      json_get_f32(buf, "rebound_ground_x0_base", &g_params.rebound_ground_x0_base) != 0 ||
      json_get_f32(buf, "sdi_radius", &g_params.sdi_radius) != 0 ||
      json_get_u8(buf, "sdi_tilt_max_frames", &g_params.sdi_tilt_max_frames) != 0 ||
      json_get_f32(buf, "sdi_step_mul", &g_params.sdi_step_mul) != 0 ||
      json_get_f32(buf, "asdi_step_mul", &g_params.asdi_step_mul) != 0 ||
      json_get_f32(buf, "shield_sdi_mul", &g_params.shield_sdi_mul) != 0 ||
      json_get_f32(buf, "di_max_deg", &g_params.di_max_deg) != 0 ||
      json_get_f32(buf, "lsi_lr_held_mul", &g_params.lsi_lr_held_mul) != 0 ||
      json_get_f32(buf, "shield_attacker_ground_kb_mul", &g_params.shield_attacker_ground_kb_mul) !=
          0 ||
      json_get_f32(buf, "shield_attacker_ground_kb_base",
                   &g_params.shield_attacker_ground_kb_base) != 0 ||
      json_get_f32(buf, "shield_attacker_ground_friction_mul",
                   &g_params.shield_attacker_ground_friction_mul) != 0 ||
      json_get_f32(buf, "ground_kb_friction_mul", &g_params.ground_kb_friction_mul) != 0 ||
      json_get_f32(buf, "knockback_frame_decay", &g_params.knockback_frame_decay) != 0 ||
      json_get_f32(buf, "air_drift_overmax_friction", &g_params.air_drift_overmax_friction) != 0 ||
      json_get_f32(buf, "runbrake_anim_freeze_speed_threshold",
                   &g_params.runbrake_anim_freeze_speed_threshold) != 0 ||
      json_get_f32(buf, "kb_weight_mul", &g_params.kb_weight_mul) != 0 ||
      json_get_f32(buf, "kb_weight_mul2", &g_params.kb_weight_mul2) != 0 ||
      json_get_f32(buf, "kb_applied_max", &g_params.kb_applied_max) != 0 ||
      json_get_f32(buf, "throw_kb_weight_x10c", &g_params.throw_kb_weight_x10c) != 0 ||
      json_get_f32(buf, "kb_base_term", &g_params.kb_base_term) != 0 ||
      json_get_f32(buf, "kb_dmg_mul", &g_params.kb_dmg_mul) != 0 ||
      json_get_f32(buf, "kb_wsk_mul", &g_params.kb_wsk_mul) != 0 ||
      json_get_f32(buf, "kb_growth_mul", &g_params.kb_growth_mul) != 0 ||
      json_get_f32(buf, "kb_base_add", &g_params.kb_base_add) != 0 ||
      json_get_i32(buf, "kb_vel_merge_since_hit_frames", &g_params.kb_vel_merge_since_hit_frames) !=
          0 ||
      json_get_f32(buf, "kb_vel_mul", &g_params.kb_vel_mul) != 0 ||
      json_get_f32(buf, "kb_min", &g_params.kb_min) != 0 ||
      json_get_f32(buf, "kb_squat_mul", &g_params.kb_squat_mul) != 0 ||
      json_get_f32(buf, "ftcoll_damage_mul_x128", &g_params.ftcoll_damage_mul_x128) != 0 ||
      json_get_f32(buf, "kb_ice_mul", &g_params.kb_ice_mul) != 0 ||
      json_get_f32(buf, "kb_smashcharge_mul", &g_params.kb_smashcharge_mul) != 0 ||
      json_get_i32(buf, "ftcoll_percent_base_x6d4", &g_params.ftcoll_percent_base_x6d4) != 0 ||
      json_get_i32(buf, "ftcoll_percent_base_x6d8", &g_params.ftcoll_percent_base_x6d8) != 0 ||
      json_get_f32(buf, "damage_hitstun_mul", &g_params.damage_hitstun_mul) != 0 ||
      json_get_f32(buf, "damage_severity_x158", &g_params.damage_severity_x158) != 0 ||
      json_get_f32(buf, "damage_severity_x15c", &g_params.damage_severity_x15c) != 0 ||
      json_get_f32(buf, "damage_severity_x160", &g_params.damage_severity_x160) != 0 ||
      json_get_u16(buf, "damage_meteor_cancel_angle_min_deg",
                   &g_params.damage_meteor_cancel_angle_min_deg) != 0 ||
      json_get_u16(buf, "damage_meteor_cancel_angle_max_deg",
                   &g_params.damage_meteor_cancel_angle_max_deg) != 0 ||
      json_get_u16(buf, "damage_meteor_cancel_lockout_frames",
                   &g_params.damage_meteor_cancel_lockout_frames) != 0 ||
      json_get_f32(buf, "grounded_tumble_bounce_angle_extra_radians",
                   &g_params.grounded_tumble_bounce_angle_extra_radians) != 0 ||
      json_get_f32(buf, "grounded_tumble_bounce_y_mul", &g_params.grounded_tumble_bounce_y_mul) !=
          0 ||
      json_get_f32(buf, "damagefly_top_angle_min_radians",
                   &g_params.damagefly_top_angle_min_radians) != 0 ||
      json_get_f32(buf, "damagefly_top_angle_max_radians",
                   &g_params.damagefly_top_angle_max_radians) != 0 ||
      json_get_i32(buf, "damagefly_roll_percent_threshold",
                   &g_params.damagefly_roll_percent_threshold) != 0 ||
      json_get_f32(buf, "damagefly_roll_prob", &g_params.damagefly_roll_prob) != 0 ||
      json_get_f32(buf, "sakurai_air_radians", &g_params.sakurai_air_radians) != 0 ||
      json_get_f32(buf, "sakurai_ground_deg_max", &g_params.sakurai_ground_deg_max) != 0 ||
      json_get_f32(buf, "sakurai_kb_threshold", &g_params.sakurai_kb_threshold) != 0 ||
      json_get_f32(buf, "sakurai_kb_max", &g_params.sakurai_kb_max) != 0 ||
      json_get_f32(buf, "air_motion_kb_mul", &g_params.air_motion_kb_mul) != 0 ||
      json_get_u8(buf, "air_motion_max_frames", &g_params.air_motion_max_frames) != 0 ||
      json_get_u8(buf, "tech_lr_debounce_frames", &g_params.tech_lr_debounce_frames) != 0 ||
      json_get_f32(buf, "tech_window_frames", &g_params.tech_window_frames) != 0 ||
      json_get_f32(buf, "tech_roll_stick_threshold", &g_params.tech_roll_stick_threshold) != 0 ||
      json_get_f32_with_default(buf, "multijump_drift_stick_threshold", 0.0f,
                                &g_params.multijump_drift_stick_threshold) != 0 ||
      json_get_f32(buf, "damage_jump_buffer_window_frames",
                   &g_params.damage_jump_buffer_window_frames) != 0 ||
      json_get_f32(buf, "damagefly_downbound_kb_vel_threshold",
                   &g_params.damagefly_downbound_kb_vel_threshold) != 0 ||
      json_get_f32(buf, "damagefly_landing_kb_vel_threshold",
                   &g_params.damagefly_landing_kb_vel_threshold) != 0 ||
      json_get_f32(buf, "damagefly_reflect_speed_threshold",
                   &g_params.damagefly_reflect_speed_threshold) != 0 ||
      json_get_u16(buf, "colanim_flyreflect_x1990_frames",
                   &g_params.colanim_flyreflect_x1990_frames) != 0 ||
      json_get_u16(buf, "damagefly_reflect_lockout_frames",
                   &g_params.damagefly_reflect_lockout_frames) != 0 ||
      json_get_f32(buf, "damagefly_reflect_speed_mul", &g_params.damagefly_reflect_speed_mul) !=
          0 ||
      json_get_f32(buf, "down_stand_stick_y_threshold", &g_params.down_stand_stick_y_threshold) !=
          0 ||
      json_get_f32(buf, "down_stick_x_threshold", &g_params.down_stick_x_threshold) != 0 ||
      json_get_f32(buf, "down_attack_button_window_frames",
                   &g_params.down_attack_button_window_frames) != 0 ||
      json_get_f32(buf, "platform_air_land_stick_y_threshold",
                   &g_params.platform_air_land_stick_y_threshold) != 0 ||
      json_get_f32(buf, "down_attack_cstick_up_threshold",
                   &g_params.down_attack_cstick_up_threshold) != 0 ||
      json_get_u16(buf, "combo_push_count_threshold", &g_params.combo_push_count_threshold) != 0 ||
      json_get_u16(buf, "combo_push_stronger_count_threshold",
                   &g_params.combo_push_stronger_count_threshold) != 0 ||
      json_get_u16(buf, "combo_timer_post_hitstun_frames",
                   &g_params.combo_timer_post_hitstun_frames) != 0 ||
      json_get_u16(buf, "combo_push_timer_frames", &g_params.combo_push_timer_frames) != 0 ||
      json_get_f32(buf, "combo_push_low_speed", &g_params.combo_push_low_speed) != 0 ||
      json_get_f32(buf, "combo_push_high_speed", &g_params.combo_push_high_speed) != 0 ||
      json_get_f32(buf, "down_wait_frames", &g_params.down_wait_frames) != 0 ||
      json_get_i32(buf, "down_damage_percent_threshold", &g_params.down_damage_percent_threshold) !=
          0) {
    alloc_free(buf);
    return -1;
  }

  alloc_free(buf);
  g_loaded = 1;
  return 0;
}

const MslCommonParams* msl_common_params(void) { return g_loaded ? &g_params : NULL; }
