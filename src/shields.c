#include "shields.h"

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "shield_tilt_table.h"

static inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return trigger_u8_to_unit(m);
}

static inline uint8_t is_shield_active_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_REFLECT:
    case MSL_ACT_GUARD_SET_OFF:
      return 1;
    default:
      return 0;
  }
}

static inline uint16_t clamp_u16(uint16_t x, uint16_t lo, uint16_t hi) {
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline uint16_t guard_tilt_frame_from_stick(float stick_x_unit, float stick_y_unit,
                                                   float facing_dir, uint16_t neutral_frame,
                                                   uint16_t frame_max) {
  // Decomp (GALE01): ftCo_80091BC4 computes an angle in degrees from the L-stick direction, after
  // folding X by facing_dir:
  //   lstick_x = fp->input.lstick.x * fp->facing_dir;
  //   rad = lb_8000D008(fp->input.lstick.y, lstick_x); // atan2-like
  //   if (rad < 0) rad += 2*pi;
  //   deg = rad_to_deg * rad; clamp to [0, 359]
  //   fp->mv.co.guard.x8 is centered around +10 (ftCo_800921DC sets x8=10).
  //
  // We intentionally do not model the guard.x8 smoothing/inertia state here to keep shield bubble
  // placement suite-neutral (debug-only) and non-mutating.
  const float x = stick_x_unit * facing_dir;
  const float y = stick_y_unit;

  // Keep π as an explicit constant to avoid relying on nonstandard libm macros.
  const float k_pi = 3.14159265358979323846f;
  float rad = atan2f(y, x);
  if (rad < 0.0f) {
    rad += 2.0f * k_pi;
  }
  float deg_f = rad * (180.0f / k_pi);
  if (deg_f < 0.0f) {
    deg_f = 0.0f;
  }
  if (deg_f > 359.0f) {
    deg_f = 359.0f;
  }
  const uint16_t deg = (uint16_t)deg_f;

  // The Melee guard tilt timeline uses frames in [0..370], with neutral at 10.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC (x8=10).
  uint16_t f = (uint16_t)(neutral_frame + deg);
  f = clamp_u16(f, 0, frame_max);
  return f;
}

static inline float stick_mag_unit(float stick_x_unit, float stick_y_unit) {
  // Decomp: ftCo_80091BC4 uses sqrt(x^2 + y^2) and clamps to 1.
  float m = sqrtf(stick_x_unit * stick_x_unit + stick_y_unit * stick_y_unit);
  if (m > 1.0f) {
    m = 1.0f;
  }
  if (m < 0.0f) {
    m = 0.0f;
  }
  return m;
}

void shields_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Shield bubble size follows ftCo_Guard.c's inlineB0:
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:172-190.
  //
  // Notes:
  // - `initial_shield_size` is per-character (co attrs; ISO-extracted to data/characters/*.json).
  // - `shield_size_*` and `start_shield_health` are ftCommonData fields (ISO-extracted to
  //   data/common/ft_common_data.json).
  // - Shield bubble center is pose/guard-derived in Melee (via a dedicated "shield bone" whose
  //   translation is driven by guard tilt). We approximate this by sampling an ISO-derived guard
  //   tilt table (data/shields/*.bin) and applying stick direction + facing.

  const float denom = 1.0f - c->trigger_deadzone;
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);

      float sx = 0.0f;
      float sy = 0.0f;
      float sz = 0.0f;
      float sr = 0.0f;

      if (p < num_players) {
        // Default center approximation: fighter world position; z=0 in our current 2D stage model.
        // If guard tilt data is available and the shield is active, we override this below.
        const float pos_x = batch->state.pos_x[idx];
        const float pos_y = batch->state.pos_y[idx];
        sx = pos_x;
        sy = pos_y;
        sz = 0.0f;

        const uint8_t stocks = batch->state.stocks[idx];
        if (stocks != 0 && is_shield_active_action(batch->state.action_id[idx]) &&
            batch->state.shield_hp[idx] > 0.0f && c->start_shield_health > 0.0f) {
          const MslCharParams* ca = msl_char_params(batch->state.char_id[idx]);
          if (ca != NULL) {
            // Guard-tilt shield bubble center (decomp-shaped):
            // - Sample the ISO-derived msid=38 ("Guard") tilt timeline in data/shields/<char>.bin.
            // - Use stick direction (main stick) and facing to choose an angle frame.
            // - Blend towards that angled center based on stick magnitude (0..1).
            MslShieldTiltTableView tv;
            if (msl_shield_tilt_table_view(batch->state.char_id[idx], &tv) == 0 &&
                tv.xyz != NULL && tv.frame_count > 0) {
              const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
              const float stick_x_unit = (float)batch->state.input_main_x[idx] * (1.0f / 80.0f);
              const float stick_y_unit = (float)batch->state.input_main_y[idx] * (1.0f / 80.0f);
              const float mag = stick_mag_unit(stick_x_unit, stick_y_unit);

              const uint16_t frame_max = (uint16_t)(tv.frame_count - 1);
              const uint16_t neutral = tv.neutral_frame;
              const uint16_t f =
                  guard_tilt_frame_from_stick(stick_x_unit, stick_y_unit, facing_dir, neutral, frame_max);

              const size_t n_i = (size_t)neutral * 3u;
              const size_t f_i = (size_t)f * 3u;
              const float nx = tv.xyz[n_i + 0];
              const float ny = tv.xyz[n_i + 1];
              const float nz = tv.xyz[n_i + 2];
              const float fx = tv.xyz[f_i + 0];
              const float fy = tv.xyz[f_i + 1];
              const float fz = tv.xyz[f_i + 2];

              const float dx = nx + mag * (fx - nx);
              const float dy = ny + mag * (fy - ny);
              const float dz = nz + mag * (fz - nz);

              // Match hurtcaps scaling policy: SSANIM-derived offsets are extracted without per-fighter
              // runtime scale (fp->x34_scale.y), so apply fighter_scale_y uniformly.
              const float scale_y = batch->state.fighter_scale_y[idx];
              sx = pos_x + (dx * scale_y * facing_dir);
              sy = pos_y + (dy * scale_y);
              sz = dz * scale_y;
            }

            const float trig = trigger_unit_from_input(batch->state.input_buttons[idx],
                                                       batch->state.input_l[idx],
                                                       batch->state.input_r[idx]);
            const float light = (denom > 0.0f) ? clamp01((trig - c->trigger_deadzone) / denom) : 0.0f;
            const float hp_ratio = clamp01(batch->state.shield_hp[idx] / c->start_shield_health);
            const float light_scale =
                (light * (c->shield_size_lightshield_max - c->shield_size_lightshield_min)) +
                c->shield_size_lightshield_min;
            const float n1 = hp_ratio * light_scale;
            const float n2 = 1.0f - c->shield_size_min_scale;
            const float scale = (n2 * n1) + c->shield_size_min_scale;

            sr = scale * ca->initial_shield_size * batch->state.fighter_scale_y[idx];
          }
        }
      }

      batch->state.shield_x[idx] = sx;
      batch->state.shield_y[idx] = sy;
      batch->state.shield_z[idx] = sz;
      batch->state.shield_radius[idx] = sr;
    }
  }
}
