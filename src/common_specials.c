#include "common_specials.h"

void ftco_specials_apply_grounded_sideb_doenter(MslBatch* batch, const MslCharParams* ch,
                                                size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Common grounded Side-B entry owner:
  // gr_vel += -(gr_vel * (1 - co_attrs.xB8)) * ft_GetGroundFrictionMultiplier(fp), then the
  // character-specific ftData_SpecialS entry runs.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
  // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
  // data/characters/*.json::side_special_ground_entry_vel_mul
  batch->state.speed_ground_x_self[idx] +=
      -(batch->state.speed_ground_x_self[idx] * (1.0f - ch->side_special_ground_entry_vel_mul)) *
      batch->state.ground_friction_mul[idx];
  batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
}
