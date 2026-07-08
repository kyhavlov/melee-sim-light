#include "char_params.h"
#include "data_dir.h"
#include "char_registry.h"
#include "ids.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

static MslCharParams g_params_by_char[256];
const MslCharParams* msl_char_params_ptr_by_char[256];
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

static int json_get_f32_or_default(const char* json, const char* key, float default_v, float* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  float v = 0.0f;
  if (json_get_f32(json, key, &v) != 0) {
    *out = default_v;
    return 0;
  }
  *out = v;
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

static int json_get_u8_or_default(const char* json, const char* key, uint8_t default_v,
                                  uint8_t* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  uint8_t v = 0;
  if (json_get_u8(json, key, &v) != 0) {
    *out = default_v;
    return 0;
  }
  *out = v;
  return 0;
}

static int json_get_bool_u8(const char* json, const char* key, uint8_t* out) {
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
  p = json_skip_ws(p + 1);
  if (strncmp(p, "true", 4) == 0) {
    *out = 1u;
    return 0;
  }
  if (strncmp(p, "false", 5) == 0) {
    *out = 0u;
    return 0;
  }
  return -1;
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
  *out = (uint16_t)(unsigned int)(f + 0.5f);
  return 0;
}

static int json_get_u16_or_default(const char* json, const char* key, uint16_t default_v,
                                   uint16_t* out) {
  // Missing key -> default (used for per-character special params absent on other characters).
  char pat[96];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat) || strstr(json, pat) == NULL) {
    *out = default_v;
    return 0;
  }
  return json_get_u16(json, key, out);
}

static int json_get_u32(const char* json, const char* key, uint32_t* out) {
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
  if (v < 0.0) {
    v = 0.0;
  }
  if (v > 4294967295.0) {
    v = 4294967295.0;
  }
  *out = (uint32_t)(v + 0.5);
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
  if (f > 2147483647.0f) {
    f = 2147483647.0f;
  }
  if (f < -2147483648.0f) {
    f = -2147483648.0f;
  }
  *out = (int32_t)f;
  return 0;
}

static int json_get_i32_or_default(const char* json, const char* key, int32_t default_v,
                                   int32_t* out) {
  char pat[96];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat) || strstr(json, pat) == NULL) {
    *out = default_v;
    return 0;
  }
  return json_get_i32(json, key, out);
}

static int json_get_i8_or_default(const char* json, const char* key, int8_t default_v, int8_t* out);

static int json_get_i8(const char* json, const char* key, int8_t* out) {
  if (json == NULL || key == NULL || out == NULL) {
    return -1;
  }
  int32_t v = 0;
  if (json_get_i32(json, key, &v) != 0) {
    return -1;
  }
  if (v < -128) {
    v = -128;
  }
  if (v > 127) {
    v = 127;
  }
  *out = (int8_t)v;
  return 0;
}

static int json_get_i8_or_default(const char* json, const char* key, int8_t default_v,
                                  int8_t* out) {
  char pat[96];
  const int n = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(pat) || strstr(json, pat) == NULL) {
    *out = default_v;
    return 0;
  }
  return json_get_i8(json, key, out);
}

static int json_get_f32_array3(const char* json, const char* key, float out3[3]) {
  if (json == NULL || key == NULL || out3 == NULL) {
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
  p = json_skip_ws(p);
  if (p == NULL || *p != '[') {
    return -1;
  }
  p++;
  for (int i = 0; i < 3; i++) {
    double v = 0.0;
    p = json_parse_double(p, &v);
    if (p == NULL) {
      return -1;
    }
    out3[i] = (float)v;
    p = json_skip_ws(p);
    if (p == NULL) {
      return -1;
    }
    if (i < 2) {
      if (*p != ',') {
        return -1;
      }
      p++;
    }
  }
  return 0;
}

static int json_get_u16_array(const char* json, const char* key, uint16_t* out, size_t cap,
                              size_t* out_count) {
  if (json == NULL || key == NULL || out == NULL || out_count == NULL || cap == 0u) {
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
  p = json_skip_ws(p);
  if (p == NULL || *p != '[') {
    return -1;
  }
  p++;
  size_t count = 0;
  for (;;) {
    p = json_skip_ws(p);
    if (p == NULL) {
      return -1;
    }
    if (*p == ']') {
      p++;
      break;
    }
    if (count >= cap) {
      return -1;
    }
    double v = 0.0;
    p = json_parse_double(p, &v);
    if (p == NULL || v < 0.0 || v > 65535.0) {
      return -1;
    }
    out[count++] = (uint16_t)(unsigned int)(v + 0.5);
    p = json_skip_ws(p);
    if (p == NULL) {
      return -1;
    }
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == ']') {
      p++;
      break;
    }
    return -1;
  }
  *out_count = count;
  return 0;
}

static int load_one(const char* data_dir, const char* rel_path, uint8_t char_id) {
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  static const char* k_spacie_required_keys[] = {
      "illusion_gravity_delay_start_frames",
      "illusion_air_friction_start",
      "illusion_fall_accel_start",
      "illusion_ground_end_vel_x",
      "illusion_ground_friction",
      "illusion_air_end_vel_x",
      "illusion_air_friction",
      "illusion_landing_lag_frames",
      "illusion_gravity_delay_end_frames",
      "illusion_fall_accel_end",
      "illusion_item_hitbox_size",
      "illusion_item_lifetime_state01_frames",
      "illusion_item_lifetime_state2_frames",
      "illusion_item_state0_damage",
      "illusion_item_state0_shield_damage",
      "illusion_item_state0_angle",
      "illusion_item_state0_kbg",
      "illusion_item_state0_wsk",
      "illusion_item_state0_bkb",
      "illusion_item_state0_element",
      "illusion_item_state0_hitbox_y_offset",
      "illusion_item_state1_damage",
      "illusion_item_state1_shield_damage",
      "illusion_item_state1_angle",
      "illusion_item_state1_kbg",
      "illusion_item_state1_wsk",
      "illusion_item_state1_bkb",
      "illusion_item_state1_element",
      "illusion_item_state1_hitbox_y_offset",
      "firefox_hold_gravity_delay_frames",
      "firefox_hold_vel_x",
      "firefox_hold_air_friction",
      "firefox_hold_air_fall_accel",
      "firefox_direction_stick_range_min",
      "firefox_launch_duration_frames",
      "firefox_launch_reverse_accel_start_frames",
      "firefox_launch_speed",
      "firefox_launch_reverse_accel",
      "firefox_ground_momentum_end",
      "firefox_bound_vel_x",
      "firefox_facing_stick_range_min",
      "firefox_freefall_mobility",
      "firefox_landing_lag_frames",
      "firefox_bound_angle_degrees",
      "firefox_bound_delay_frames",
      "rapid_jab_window",
      "wait_anim_choice_msids",
      "wait_anim_choice_weights",
      "ecb_joints",
      "grab_capture_anchor_part_id",
      "throw_release_mpcoll_floor_publication_mask",
      "laser_spawn_joint_part_id",
  };

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

  // Helpful error when ISO-derived character attrs are missing new required fields.
  //
  // `.msl/characters/*.json` may be stale when the package is updated after data extraction.
  if (char_id == (uint8_t)MSL_CHAR_ID_FOX || char_id == (uint8_t)MSL_CHAR_ID_FALCO) {
    for (size_t i = 0; i < (sizeof(k_spacie_required_keys) / sizeof(k_spacie_required_keys[0]));
         i++) {
      const char* k = k_spacie_required_keys[i];
      char pat[96];
      const int pn = snprintf(pat, sizeof(pat), "\"%s\"", k);
      if (pn <= 0 || (size_t)pn >= sizeof(pat) || strstr(buf, pat) == NULL) {
        fprintf(stderr, "msl: missing required key %s in %s\n", pat, path);
        fprintf(stderr,
                "hint: this data directory was generated by an older melee-sim-light extractor. "
                "Regenerate .msl from your SSBM ISO:\n"
                "  python -m melee_sim.extract_data --iso /path/to/SSBM.iso\n");
        alloc_free(buf);
        return -1;
      }
    }
  }
  if (strstr(buf, "\"throw_release_mpcoll_floor_publication_mask\"") == NULL) {
    fprintf(stderr,
            "msl: missing required key \"throw_release_mpcoll_floor_publication_mask\" in %s\n",
            path);
    fprintf(stderr,
            "hint: regenerate character data with the current extractor so throw-release "
            "publication ownership is explicit.\n");
    alloc_free(buf);
    return -1;
  }
  if (strstr(buf, "\"fallspecial_xc0_source_fx_kind_mask\"") == NULL) {
    fprintf(stderr, "msl: missing required key \"fallspecial_xc0_source_fx_kind_mask\" in %s\n",
            path);
    fprintf(stderr,
            "hint: regenerate character data with the current extractor/overlays so FallSpecial "
            "xC callsite ownership is explicit.\n");
    alloc_free(buf);
    return -1;
  }
  if (strstr(buf, "\"common_fall_blended_ecb_seed_mask\"") == NULL) {
    fprintf(stderr, "msl: missing required key \"common_fall_blended_ecb_seed_mask\" in %s\n",
            path);
    fprintf(stderr,
            "hint: regenerate character data with the current extractor/overlays so CommonFall "
            "CollData ECB seed ownership is explicit.\n");
    alloc_free(buf);
    return -1;
  }
  if (strstr(buf, "\"escapeair_carried_floor_wall_source\"") == NULL) {
    fprintf(stderr, "msl: missing required key \"escapeair_carried_floor_wall_source\" in %s\n",
            path);
    fprintf(stderr,
            "hint: regenerate character data with the current extractor/overlays so EscapeAir "
            "carried-floor wall ownership is explicit.\n");
    alloc_free(buf);
    return -1;
  }

  MslCharParams out = {0};
  float refl_off[3] = {0};
  float camera_off[3] = {0};
  size_t ecb_joint_count = 0u;
  size_t wait_anim_choice_msid_count = 0u;
  size_t wait_anim_choice_weight_count = 0u;
  if (json_get_f32(buf, "weight", &out.weight) != 0 ||
      json_get_u8_or_default(buf, "weight_independent_throws_mask", 0,
                             &out.weight_independent_throws_mask) != 0 ||
      json_get_f32(buf, "trophy_scale", &out.trophy_scale) != 0 ||
      json_get_f32(buf, "walk_init_vel", &out.walk_init_vel) != 0 ||
      json_get_f32(buf, "walk_accel", &out.walk_accel) != 0 ||
      json_get_f32(buf, "walk_max_vel", &out.walk_max_vel) != 0 ||
      json_get_f32(buf, "gr_friction", &out.gr_friction) != 0 ||
      json_get_f32(buf, "ground_max_horizontal_velocity", &out.ground_max_horizontal_velocity) !=
          0 ||
      json_get_u8(buf, "turn_frames", &out.turn_frames) != 0 ||
      json_get_f32(buf, "rebound_anim_numerator_frames", &out.rebound_anim_numerator_frames) != 0 ||
      json_get_u8(buf, "rapid_jab_window", &out.rapid_jab_window) != 0 ||
      json_get_u16_array(buf, "wait_anim_choice_msids", out.wait_anim_choice_msids,
                         sizeof(out.wait_anim_choice_msids) / sizeof(out.wait_anim_choice_msids[0]),
                         &wait_anim_choice_msid_count) != 0 ||
      json_get_u16_array(
          buf, "wait_anim_choice_weights", out.wait_anim_choice_weights,
          sizeof(out.wait_anim_choice_weights) / sizeof(out.wait_anim_choice_weights[0]),
          &wait_anim_choice_weight_count) != 0 ||
      json_get_u8(buf, "jump_startup_frames", &out.jump_startup_frames) != 0 ||
      json_get_f32(buf, "jump_h_initial_velocity", &out.jump_h_initial_velocity) != 0 ||
      json_get_f32(buf, "jump_v_initial_velocity", &out.jump_v_initial_velocity) != 0 ||
      json_get_f32(buf, "hop_v_initial_velocity", &out.hop_v_initial_velocity) != 0 ||
      json_get_f32(buf, "ground_to_air_jump_momentum_multiplier",
                   &out.ground_to_air_jump_momentum_multiplier) != 0 ||
      json_get_f32(buf, "jump_h_max_velocity", &out.jump_h_max_velocity) != 0 ||
      json_get_f32(buf, "side_special_ground_entry_vel_mul",
                   &out.side_special_ground_entry_vel_mul) != 0 ||
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
      json_get_u16(buf, "camera_zoom_target_bone_part_id", &out.camera_zoom_target_bone_part_id) !=
          0 ||
      json_get_f32_array3(buf, "camera_zoom_target_offset", camera_off) != 0 ||
      json_get_f32(buf, "camera_box_radius", &out.camera_box_radius) != 0 ||
      json_get_f32(buf, "air_jump_v_multiplier", &out.air_jump_v_multiplier) != 0 ||
      json_get_f32(buf, "air_jump_h_multiplier", &out.air_jump_h_multiplier) != 0 ||
      json_get_f32(buf, "dash_initial_velocity", &out.dash_initial_velocity) != 0 ||
      json_get_f32(buf, "dash_run_acceleration_a", &out.dash_run_acceleration_a) != 0 ||
      json_get_f32(buf, "dash_run_acceleration_b", &out.dash_run_acceleration_b) != 0 ||
      json_get_f32(buf, "dash_run_terminal_velocity", &out.dash_run_terminal_velocity) != 0 ||
      json_get_f32(buf, "run_animation_scaling", &out.run_animation_scaling) != 0 ||
      json_get_f32(buf, "max_run_brake_frames", &out.max_run_brake_frames) != 0 ||
      json_get_f32(buf, "initial_shield_size", &out.initial_shield_size) != 0 ||
      json_get_f32(buf, "shield_break_initial_velocity", &out.shield_break_initial_velocity) != 0 ||
      json_get_f32(buf, "model_scaling", &out.model_scaling) != 0 ||
      json_get_f32(buf, "pushbox_x", &out.pushbox_x) != 0 ||
      json_get_f32(buf, "pushbox_y", &out.pushbox_y) != 0 ||
      json_get_f32_or_default(buf, "laser_scale_max", 0.0f, &out.laser_scale_max) != 0 ||
      json_get_u16_or_default(buf, "laser_spawn_joint_part_id", 0,
                              &out.laser_spawn_joint_part_id) != 0 ||
      json_get_u16(buf, "grab_capture_anchor_part_id", &out.grab_capture_anchor_part_id) != 0 ||
      json_get_u8_or_default(buf, "throw_release_mpcoll_floor_publication_mask", 0,
                             &out.throw_release_mpcoll_floor_publication_mask) != 0 ||
      json_get_u32(buf, "fallspecial_xc0_source_fx_kind_mask",
                   &out.fallspecial_xc0_source_fx_kind_mask) != 0 ||
      json_get_u8_or_default(buf, "common_fall_blended_ecb_seed_mask", 0,
                             &out.common_fall_blended_ecb_seed_mask) != 0 ||
      json_get_u8_or_default(buf, "escapeair_carried_floor_wall_source", 0,
                             &out.escapeair_carried_floor_wall_source) != 0 ||
      json_get_u8_or_default(buf, "illusion_gravity_delay_start_frames", 0,
                             &out.illusion_gravity_delay_start_frames) != 0 ||
      json_get_f32_or_default(buf, "illusion_air_friction_start", 0.0f,
                              &out.illusion_air_friction_start) != 0 ||
      json_get_f32_or_default(buf, "illusion_fall_accel_start", 0.0f,
                              &out.illusion_fall_accel_start) != 0 ||
      json_get_f32_or_default(buf, "illusion_ground_vel_x", 0.0f, &out.illusion_ground_vel_x) !=
          0 ||
      json_get_f32_or_default(buf, "illusion_ground_end_vel_x", 0.0f,
                              &out.illusion_ground_end_vel_x) != 0 ||
      json_get_f32_or_default(buf, "illusion_ground_friction", 0.0f,
                              &out.illusion_ground_friction) != 0 ||
      json_get_f32_or_default(buf, "illusion_air_end_vel_x", 0.0f, &out.illusion_air_end_vel_x) !=
          0 ||
      json_get_f32_or_default(buf, "illusion_air_friction", 0.0f, &out.illusion_air_friction) !=
          0 ||
      json_get_u8_or_default(buf, "illusion_landing_lag_frames", 0,
                             &out.illusion_landing_lag_frames) != 0 ||
      json_get_u8_or_default(buf, "illusion_gravity_delay_end_frames", 0,
                             &out.illusion_gravity_delay_end_frames) != 0 ||
      json_get_f32_or_default(buf, "illusion_fall_accel_end", 0.0f, &out.illusion_fall_accel_end) !=
          0 ||
      json_get_f32_or_default(buf, "illusion_item_hitbox_size", 0.0f,
                              &out.illusion_item_hitbox_size) != 0 ||
      json_get_u8_or_default(buf, "illusion_item_lifetime_state01_frames", 0,
                             &out.illusion_item_lifetime_state01_frames) != 0 ||
      json_get_u8_or_default(buf, "illusion_item_lifetime_state2_frames", 0,
                             &out.illusion_item_lifetime_state2_frames) != 0 ||
      json_get_f32_or_default(buf, "illusion_item_state0_damage", 0.0f,
                              &out.illusion_item_state0_damage) != 0 ||
      json_get_i8_or_default(buf, "illusion_item_state0_shield_damage", 0,
                             &out.illusion_item_state0_shield_damage) != 0 ||
      json_get_u16_or_default(buf, "illusion_item_state0_angle", 0,
                              &out.illusion_item_state0_angle) != 0 ||
      json_get_u16_or_default(buf, "illusion_item_state0_kbg", 0, &out.illusion_item_state0_kbg) !=
          0 ||
      json_get_u16_or_default(buf, "illusion_item_state0_wsk", 0, &out.illusion_item_state0_wsk) !=
          0 ||
      json_get_u16_or_default(buf, "illusion_item_state0_bkb", 0, &out.illusion_item_state0_bkb) !=
          0 ||
      json_get_u8_or_default(buf, "illusion_item_state0_element", 0,
                             &out.illusion_item_state0_element) != 0 ||
      json_get_f32_or_default(buf, "illusion_item_state0_hitbox_y_offset", 0.0f,
                              &out.illusion_item_state0_hitbox_y_offset) != 0 ||
      json_get_f32_or_default(buf, "illusion_item_state1_damage", 0.0f,
                              &out.illusion_item_state1_damage) != 0 ||
      json_get_i8_or_default(buf, "illusion_item_state1_shield_damage", 0,
                             &out.illusion_item_state1_shield_damage) != 0 ||
      json_get_u16_or_default(buf, "illusion_item_state1_angle", 0,
                              &out.illusion_item_state1_angle) != 0 ||
      json_get_u16_or_default(buf, "illusion_item_state1_kbg", 0, &out.illusion_item_state1_kbg) !=
          0 ||
      json_get_u16_or_default(buf, "illusion_item_state1_wsk", 0, &out.illusion_item_state1_wsk) !=
          0 ||
      json_get_u16_or_default(buf, "illusion_item_state1_bkb", 0, &out.illusion_item_state1_bkb) !=
          0 ||
      json_get_u8_or_default(buf, "illusion_item_state1_element", 0,
                             &out.illusion_item_state1_element) != 0 ||
      json_get_f32_or_default(buf, "illusion_item_state1_hitbox_y_offset", 0.0f,
                              &out.illusion_item_state1_hitbox_y_offset) != 0 ||
      json_get_u8_or_default(buf, "firefox_hold_gravity_delay_frames", 0,
                             &out.firefox_hold_gravity_delay_frames) != 0 ||
      json_get_f32_or_default(buf, "firefox_hold_vel_x", 0.0f, &out.firefox_hold_vel_x) != 0 ||
      json_get_f32_or_default(buf, "firefox_hold_air_friction", 0.0f,
                              &out.firefox_hold_air_friction) != 0 ||
      json_get_f32_or_default(buf, "firefox_hold_air_fall_accel", 0.0f,
                              &out.firefox_hold_air_fall_accel) != 0 ||
      json_get_f32_or_default(buf, "firefox_direction_stick_range_min", 0.0f,
                              &out.firefox_direction_stick_range_min) != 0 ||
      json_get_u8_or_default(buf, "firefox_launch_duration_frames", 0,
                             &out.firefox_launch_duration_frames) != 0 ||
      json_get_f32_or_default(buf, "firefox_launch_speed", 0.0f, &out.firefox_launch_speed) != 0 ||
      json_get_u8_or_default(buf, "firefox_launch_reverse_accel_start_frames", 0,
                             &out.firefox_launch_reverse_accel_start_frames) != 0 ||
      json_get_f32_or_default(buf, "firefox_launch_reverse_accel", 0.0f,
                              &out.firefox_launch_reverse_accel) != 0 ||
      json_get_f32_or_default(buf, "firefox_ground_momentum_end", 0.0f,
                              &out.firefox_ground_momentum_end) != 0 ||
      json_get_f32_or_default(buf, "firefox_bound_vel_x", 0.0f, &out.firefox_bound_vel_x) != 0 ||
      json_get_f32_or_default(buf, "firefox_facing_stick_range_min", 0.0f,
                              &out.firefox_facing_stick_range_min) != 0 ||
      json_get_f32_or_default(buf, "firefox_freefall_mobility", 0.0f,
                              &out.firefox_freefall_mobility) != 0 ||
      json_get_u8_or_default(buf, "firefox_landing_lag_frames", 0,
                             &out.firefox_landing_lag_frames) != 0 ||
      json_get_f32_or_default(buf, "firefox_bound_angle_degrees", 0.0f,
                              &out.firefox_bound_angle_degrees) != 0 ||
      json_get_u8_or_default(buf, "firefox_bound_delay_frames", 0,
                             &out.firefox_bound_delay_frames) != 0 ||
      json_get_f32(buf, "ledge_jump_horizontal_velocity", &out.ledge_jump_horizontal_velocity) !=
          0 ||
      json_get_f32(buf, "ledge_jump_vertical_velocity", &out.ledge_jump_vertical_velocity) != 0 ||
      json_get_f32(buf, "passivewall_vel_x", &out.passivewall_vel_x) != 0 ||
      json_get_f32(buf, "wall_jump_horizontal_velocity", &out.wall_jump_horizontal_velocity) != 0 ||
      json_get_f32(buf, "wall_jump_vertical_velocity", &out.wall_jump_vertical_velocity) != 0 ||
      json_get_bool_u8(buf, "can_walljump", &out.can_walljump) != 0 ||
      json_get_f32(buf, "walljump_setup_x_delta_threshold",
                   &out.walljump_setup_x_delta_threshold) != 0 ||
      json_get_f32(buf, "ecb_side_y_offset", &out.ecb_side_y_offset) != 0 ||
      json_get_u16_array(buf, "ecb_joints", out.ecb_joints,
                         sizeof(out.ecb_joints) / sizeof(out.ecb_joints[0]),
                         &ecb_joint_count) != 0 ||
      json_get_f32(buf, "ledge_snap_x", &out.ledge_snap_x) != 0 ||
      json_get_f32(buf, "ledge_snap_y", &out.ledge_snap_y) != 0 ||
      json_get_f32(buf, "ledge_snap_height", &out.ledge_snap_height) != 0 ||

      json_get_u8_or_default(buf, "reflector_release_lag_frames", 0,
                             &out.reflector_release_lag_frames) != 0 ||
      json_get_u8_or_default(buf, "reflector_turn_frames", 0, &out.reflector_turn_frames) != 0 ||
      json_get_u8_or_default(buf, "reflector_gravity_delay_frames", 0,
                             &out.reflector_gravity_delay_frames) != 0 ||
      json_get_f32_or_default(buf, "reflector_momentum_preserve_x", 0.0f,
                              &out.reflector_momentum_preserve_x) != 0 ||
      json_get_f32_or_default(buf, "reflector_fall_accel", 0.0f, &out.reflector_fall_accel) != 0 ||
      json_get_u16_or_default(buf, "reflector_bone_id", 0, &out.reflector_bone_part_id) != 0 ||
      json_get_i32_or_default(buf, "reflector_max_damage", 0, &out.reflector_max_damage) != 0 ||
      (strstr(buf, "\"reflector_offset\"") != NULL &&
       json_get_f32_array3(buf, "reflector_offset", refl_off) != 0) ||
      json_get_f32_or_default(buf, "reflector_size", 0.0f, &out.reflector_size) != 0 ||
      json_get_f32_or_default(buf, "reflector_damage_mul", 0.0f, &out.reflector_damage_mul) != 0 ||
      json_get_f32_or_default(buf, "reflector_speed_mul", 0.0f, &out.reflector_speed_mul) != 0 ||
      json_get_u8_or_default(buf, "reflector_behavior", 0, &out.reflector_behavior) != 0) {
    fprintf(stderr, "msl: char params parse failed (required field chain) in %s\n", path);
    alloc_free(buf);
    return -1;
  }
  if (ecb_joint_count != (sizeof(out.ecb_joints) / sizeof(out.ecb_joints[0]))) {
    fprintf(stderr, "msl: char params ecb_joints count %u != expected in %s\n",
            (unsigned)ecb_joint_count, path);
    alloc_free(buf);
    return -1;
  }
  out.ecb_joint_count = (uint8_t)ecb_joint_count;
  // Count 0 (matched empty arrays) is a real source case: a NULL ftData.x24 means the char has
  // no Wait roulette and ftCo_8008A7A8 takes the plain Wait path (puff). Consumers early-out on
  // wait_anim_choice_count == 0.
  // refs/melee/src/melee/ft/ftwaitanim.c::ftCo_8008A7A8
  if (wait_anim_choice_msid_count != wait_anim_choice_weight_count ||
      wait_anim_choice_msid_count >
          (sizeof(out.wait_anim_choice_msids) / sizeof(out.wait_anim_choice_msids[0]))) {
    fprintf(stderr, "msl: char params wait_anim_choice counts invalid in %s\n", path);
    alloc_free(buf);
    return -1;
  }
  out.wait_anim_choice_count = (uint8_t)wait_anim_choice_msid_count;
  // Backward-compatible optional fields (added for ftWalkCommon_800DFDDC parity):
  // if stale local artifacts are missing these keys, default to walk_max_vel so init keeps
  // working; regenerated extracts provide the decomp-sourced values.
  out.slow_walk_max = out.walk_max_vel;
  out.mid_walk_point = out.walk_max_vel;
  out.fast_walk_min = out.walk_max_vel;
  if (json_get_f32_or_default(buf, "slow_walk_max", out.walk_max_vel, &out.slow_walk_max) != 0 ||
      json_get_f32_or_default(buf, "mid_walk_point", out.walk_max_vel, &out.mid_walk_point) != 0 ||
      json_get_f32_or_default(buf, "fast_walk_min", out.walk_max_vel, &out.fast_walk_min) != 0) {
    alloc_free(buf);
    return -1;
  }
  // Marth-family sword special attrs (optional; zero for other ext-attr layouts).
  float ctr_off[3] = {0.0f, 0.0f, 0.0f};
  if (json_get_i32_or_default(buf, "specialn_charge_max_seconds", 0,
                              &out.specialn_charge_max_seconds) != 0 ||
      json_get_i32_or_default(buf, "specialn_release_damage_base", 0,
                              &out.specialn_release_damage_base) != 0 ||
      json_get_i32_or_default(buf, "specialn_release_damage_per_second", 0,
                              &out.specialn_release_damage_per_second) != 0 ||
      json_get_f32_or_default(buf, "specialn_entry_vel_divisor", 0.0f,
                              &out.specialn_entry_vel_divisor) != 0 ||
      json_get_f32_or_default(buf, "specialn_start_friction", 0.0f, &out.specialn_start_friction) !=
          0 ||
      json_get_f32_or_default(buf, "specials_air_entry_vel_x_divisor", 0.0f,
                              &out.specials_air_entry_vel_x_divisor) != 0 ||
      json_get_f32_or_default(buf, "specials_air_friction", 0.0f, &out.specials_air_friction) !=
          0 ||
      json_get_f32_or_default(buf, "specials_air_entry_vel_y", 0.0f,
                              &out.specials_air_entry_vel_y) != 0 ||
      json_get_f32_or_default(buf, "specials_fall_accel", 0.0f, &out.specials_fall_accel) != 0 ||
      json_get_f32_or_default(buf, "specials_terminal_vel", 0.0f, &out.specials_terminal_vel) !=
          0 ||
      json_get_f32_or_default(buf, "specialhi_freefall_mobility_mul", 0.0f,
                              &out.specialhi_freefall_mobility_mul) != 0 ||
      json_get_f32_or_default(buf, "specialhi_landing_lag_frames", 0.0f,
                              &out.specialhi_landing_lag_frames) != 0 ||
      json_get_f32_or_default(buf, "specialhi_breverse_stick_threshold", 0.0f,
                              &out.specialhi_breverse_stick_threshold) != 0 ||
      json_get_f32_or_default(buf, "specialhi_angle_stick_threshold", 0.0f,
                              &out.specialhi_angle_stick_threshold) != 0 ||
      json_get_f32_or_default(buf, "specialhi_angle_max_degrees", 0.0f,
                              &out.specialhi_angle_max_degrees) != 0 ||
      json_get_f32_or_default(buf, "specialhi_air_entry_vel_x_mul", 0.0f,
                              &out.specialhi_air_entry_vel_x_mul) != 0 ||
      json_get_f32_or_default(buf, "specialhi_launch_decay_mul", 0.0f,
                              &out.specialhi_launch_decay_mul) != 0 ||
      json_get_f32_or_default(buf, "specialhi_fall_accel", 0.0f, &out.specialhi_fall_accel) != 0 ||
      json_get_f32_or_default(buf, "specialhi_terminal_vel", 0.0f, &out.specialhi_terminal_vel) !=
          0 ||
      json_get_f32_or_default(buf, "speciallw_air_entry_vel_x_divisor", 0.0f,
                              &out.speciallw_air_entry_vel_x_divisor) != 0 ||
      json_get_f32_or_default(buf, "speciallw_air_friction", 0.0f, &out.speciallw_air_friction) !=
          0 ||
      json_get_f32_or_default(buf, "speciallw_fall_accel", 0.0f, &out.speciallw_fall_accel) != 0 ||
      json_get_f32_or_default(buf, "speciallw_terminal_vel", 0.0f, &out.speciallw_terminal_vel) !=
          0 ||
      json_get_f32_or_default(buf, "speciallw_counter_damage_mul", 0.0f,
                              &out.speciallw_counter_damage_mul) != 0 ||
      json_get_f32_or_default(buf, "speciallw_counter_shield_strength", 0.0f,
                              &out.speciallw_counter_shield_strength) != 0 ||
      json_get_i32_or_default(buf, "speciallw_counter_desc_bone", 0,
                              &out.speciallw_counter_desc_bone) != 0 ||
      (strstr(buf, "\"speciallw_counter_desc_offset\"") != NULL &&
       json_get_f32_array3(buf, "speciallw_counter_desc_offset", ctr_off) != 0) ||
      json_get_f32_or_default(buf, "speciallw_counter_desc_size", 0.0f,
                              &out.speciallw_counter_desc_size) != 0) {
    fprintf(stderr, "msl: char params sword special attr parse failed in %s\n", path);
    alloc_free(buf);
    return -1;
  }
  out.speciallw_counter_desc_offset_x = ctr_off[0];
  out.speciallw_counter_desc_offset_y = ctr_off[1];
  out.speciallw_counter_desc_offset_z = ctr_off[2];
  // Sheik ftSeakAttributes special attrs. These are runtime-required for Sheik itself because
  // Vanish/Needle/Chain/Transform state machines consume them directly; stale Sheik data must fail
  // loudly instead of booting with zeroed special mechanics. Non-Sheik characters keep zero
  // defaults because their ftData.x4 ext-attr layouts are unrelated.
  // refs/melee/src/melee/ft/chara/ftSeak/types.h::ftSeakAttributes
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Special{N,S,Hi,Lw}.c
  const uint8_t require_sheik_special_attrs = (uint8_t)(char_id == (uint8_t)MSL_CHAR_ID_SHEIK);
#define MSL_GET_SHEIK_F32(key, field)                                 \
  (require_sheik_special_attrs ? json_get_f32(buf, (key), &out.field) \
                               : json_get_f32_or_default(buf, (key), 0.0f, &out.field))
#define MSL_GET_SHEIK_I32(key, field)                                 \
  (require_sheik_special_attrs ? json_get_i32(buf, (key), &out.field) \
                               : json_get_i32_or_default(buf, (key), 0, &out.field))
  if (MSL_GET_SHEIK_F32("sheik_needle_ground_spawn_x_offset", sheik_needle_ground_spawn_x_offset) !=
          0 ||
      MSL_GET_SHEIK_F32("sheik_needle_ground_spawn_y_offset", sheik_needle_ground_spawn_y_offset) !=
          0 ||
      MSL_GET_SHEIK_F32("sheik_needle_air_spawn_x_offset", sheik_needle_air_spawn_x_offset) != 0 ||
      MSL_GET_SHEIK_F32("sheik_needle_air_spawn_y_offset", sheik_needle_air_spawn_y_offset) != 0 ||
      MSL_GET_SHEIK_F32("sheik_needle_air_end_fallspecial_lag_frames",
                        sheik_needle_air_end_fallspecial_lag_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_release_min_frames", sheik_chain_release_min_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_extension_frames", sheik_chain_extension_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_spawn_frame", sheik_chain_spawn_frame) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_start_end_frame", sheik_chain_start_end_frame) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_retract_frame", sheik_chain_retract_frame) != 0 ||
      MSL_GET_SHEIK_F32("sheik_chain_destroy_frame", sheik_chain_destroy_frame) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_air_entry_vel_y", sheik_vanish_air_entry_vel_y) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_start_air_gravity", sheik_vanish_start_air_gravity) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_start_air_terminal_vel",
                        sheik_vanish_start_air_terminal_vel) != 0 ||
      MSL_GET_SHEIK_I32("sheik_vanish_travel_frames", sheik_vanish_travel_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_ground_contact_min_frames",
                        sheik_vanish_ground_contact_min_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_stick_mag_min", sheik_vanish_stick_mag_min) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_travel_speed_stick_mul",
                        sheik_vanish_travel_speed_stick_mul) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_travel_speed_base", sheik_vanish_travel_speed_base) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_air_end_drift_mul", sheik_vanish_air_end_drift_mul) != 0 ||
      MSL_GET_SHEIK_I32("sheik_vanish_wall_bounce_degrees", sheik_vanish_wall_bounce_degrees) !=
          0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_end_vel_mul", sheik_vanish_end_vel_mul) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_fallspecial_mobility_mul",
                        sheik_vanish_fallspecial_mobility_mul) != 0 ||
      MSL_GET_SHEIK_F32("sheik_vanish_landing_lag_frames", sheik_vanish_landing_lag_frames) != 0 ||
      MSL_GET_SHEIK_F32("sheik_transform_vel_x_divisor", sheik_transform_vel_x_divisor) != 0 ||
      MSL_GET_SHEIK_F32("sheik_transform_vel_y_divisor", sheik_transform_vel_y_divisor) != 0 ||
      MSL_GET_SHEIK_F32("sheik_transform_air_gravity", sheik_transform_air_gravity) != 0 ||
      MSL_GET_SHEIK_F32("sheik_transform_air_terminal_vel", sheik_transform_air_terminal_vel) !=
          0 ||
      MSL_GET_SHEIK_F32("sheik_transform_finish_start_frame", sheik_transform_finish_start_frame) !=
          0) {
    fprintf(stderr, "msl: char params Sheik special attr parse failed in %s\n", path);
    alloc_free(buf);
    return -1;
  }
#undef MSL_GET_SHEIK_F32
#undef MSL_GET_SHEIK_I32
  // Zelda ftZelda_DatAttrs Down-B transform attrs. These are required only for Zelda: this pass
  // supports Zelda through generic data plus ftZd_SpecialLw replay swaps, while unrelated
  // characters keep zero defaults.
  // refs/melee/src/melee/ft/chara/ftZelda/types.h::ftZelda_DatAttrs
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::{
  //   ftZelda_SpecialLw_StartAction_Helper,ftZd_SpecialAirLw_Phys,ftZd_SpecialLw_8013B4D8}
  const uint8_t require_zelda_special_attrs = (uint8_t)(char_id == (uint8_t)MSL_CHAR_ID_ZELDA);
#define MSL_GET_ZELDA_F32(key, field)                                 \
  (require_zelda_special_attrs ? json_get_f32(buf, (key), &out.field) \
                               : json_get_f32_or_default(buf, (key), 0.0f, &out.field))
  if (MSL_GET_ZELDA_F32("zelda_transform_vel_x_divisor", zelda_transform_vel_x_divisor) != 0 ||
      MSL_GET_ZELDA_F32("zelda_transform_vel_y_divisor", zelda_transform_vel_y_divisor) != 0 ||
      MSL_GET_ZELDA_F32("zelda_transform_air_gravity", zelda_transform_air_gravity) != 0 ||
      MSL_GET_ZELDA_F32("zelda_transform_air_terminal_vel", zelda_transform_air_terminal_vel) !=
          0 ||
      MSL_GET_ZELDA_F32("zelda_transform_finish_start_frame", zelda_transform_finish_start_frame) !=
          0) {
    fprintf(stderr, "msl: char params Zelda special attr parse failed in %s\n", path);
    alloc_free(buf);
    return -1;
  }
#undef MSL_GET_ZELDA_F32
  // Captain Falcon ftCaptain_DatAttrs special attrs. Runtime-required for Falcon (the
  // Special{N,S,Hi,Lw} state machines consume them directly); zero defaults for the other
  // ext-attr layouts. The unk0/unk1/ext_x68/specials_unk* fields stay extraction-only until a
  // runtime consumer names them.
  // refs/melee/src/melee/ft/chara/ftCaptain/types.h::ftCaptain_DatAttrs
  const uint8_t require_falcon_special_attrs = (uint8_t)(char_id == (uint8_t)MSL_CHAR_ID_FALCON);
#define MSL_GET_FALCON_F32(key, field)                                 \
  (require_falcon_special_attrs ? json_get_f32(buf, (key), &out.field) \
                                : json_get_f32_or_default(buf, (key), 0.0f, &out.field))
#define MSL_GET_FALCON_I32(key, field)                                 \
  (require_falcon_special_attrs ? json_get_i32(buf, (key), &out.field) \
                                : json_get_i32_or_default(buf, (key), 0, &out.field))
  if (MSL_GET_FALCON_F32("falcon_specialn_stick_range_y_neg", falcon_specialn_stick_range_y_neg) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_specialn_stick_range_y_pos", falcon_specialn_stick_range_y_pos) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_specialn_angle_diff", falcon_specialn_angle_diff) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialn_vel_x", falcon_specialn_vel_x) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialn_vel_mul", falcon_specialn_vel_mul) != 0 ||
      MSL_GET_FALCON_F32("falcon_specials_gr_vel_x", falcon_specials_gr_vel_x) != 0 ||
      MSL_GET_FALCON_F32("falcon_specials_grav", falcon_specials_grav) != 0 ||
      MSL_GET_FALCON_F32("falcon_specials_terminal_vel", falcon_specials_terminal_vel) != 0 ||
      MSL_GET_FALCON_F32("falcon_specials_miss_landing_lag", falcon_specials_miss_landing_lag) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_specials_hit_landing_lag", falcon_specials_hit_landing_lag) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_air_friction_mul", falcon_specialhi_air_friction_mul) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_horz_vel", falcon_specialhi_horz_vel) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_freefall_air_spd_mul",
                         falcon_specialhi_freefall_air_spd_mul) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_landing_lag", falcon_specialhi_landing_lag) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_input_var", falcon_specialhi_input_var) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_unk2", falcon_specialhi_unk2) != 0 ||
      MSL_GET_FALCON_F32("falcon_specialhi_catch_grav", falcon_specialhi_catch_grav) != 0 ||
      MSL_GET_FALCON_I32("falcon_specialhi_air_var", falcon_specialhi_air_var) != 0 ||
      MSL_GET_FALCON_I32("falcon_speciallw_unk1", falcon_speciallw_unk1) != 0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_flame_particle_angle",
                         falcon_speciallw_flame_particle_angle) != 0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_on_hit_spd_modifier",
                         falcon_speciallw_on_hit_spd_modifier) != 0 ||
      MSL_GET_FALCON_I32("falcon_speciallw_unk2", falcon_speciallw_unk2) != 0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_ground_lag_mul", falcon_speciallw_ground_lag_mul) != 0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_landing_lag_mul", falcon_speciallw_landing_lag_mul) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_ground_traction", falcon_speciallw_ground_traction) !=
          0 ||
      MSL_GET_FALCON_F32("falcon_speciallw_air_landing_traction",
                         falcon_speciallw_air_landing_traction) != 0) {
    fprintf(stderr, "msl: char params Falcon special attr parse failed in %s\n", path);
    alloc_free(buf);
    return -1;
  }
#undef MSL_GET_FALCON_F32
#undef MSL_GET_FALCON_I32
  out.reflector_offset_x = refl_off[0];
  out.reflector_offset_y = refl_off[1];
  out.reflector_offset_z = refl_off[2];
  out.camera_zoom_target_offset_x = camera_off[0];
  out.camera_zoom_target_offset_y = camera_off[1];
  out.camera_zoom_target_offset_z = camera_off[2];

  alloc_free(buf);
  g_params_by_char[char_id] = out;
  g_have_params_by_char[char_id] = 1;
  msl_char_params_ptr_by_char[char_id] = &g_params_by_char[char_id];
  return 0;
}

int char_params_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = msl_data_dir();

  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    char rel[64];
    snprintf(rel, sizeof(rel), "characters/%s.json", MSL_CHAR_REGISTRY[ci].name);
    if (load_one(data_dir, rel, MSL_CHAR_REGISTRY[ci].char_id) != 0) {
      return -1;
    }
  }

  g_loaded = 1;
  return 0;
}

const MslCharParams* msl_char_params(uint8_t char_id) {
  const MslCharParams* params = msl_char_params_ptr_by_char[char_id];
  if (!g_loaded || params == NULL) {
    return NULL;
  }
  return params;
}
