#include "shields.h"

#include <stdint.h>

#include "action_ids.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"

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
  // - We currently do not model shield-tilt joint transforms; center is approximated at fighter
  //   world position (cur_pos) with z=0 for FD/2D.

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
        sx = batch->state.pos_x[idx];
        sy = batch->state.pos_y[idx];
        sz = 0.0f;

        const uint8_t stocks = batch->state.stocks[idx];
        if (stocks != 0 && is_shield_active_action(batch->state.action_id[idx]) &&
            batch->state.shield_hp[idx] > 0.0f && c->start_shield_health > 0.0f) {
          const MslCharParams* ca = msl_char_params(batch->state.char_id[idx]);
          if (ca != NULL) {
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

