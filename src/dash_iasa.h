#pragma once

#include <stddef.h>

#include "batch_internal.h"
#include "common_params.h"

static inline void dash_iasa_apply_terminal_velocity_scalar(MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  float friction_mul = batch->state.ground_friction_mul[idx];
  if (!(friction_mul > 0.0f)) {
    friction_mul = 1.0f;
  }
  // Decomp: ftCo_Dash_IASA falls through to this terminal gr_vel scalar after non-returning IASA
  // destinations. The destination state's Phys callback then runs later in the same fighter proc.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80091A4C,ftCo_80093A50,ftCo_800924C0}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
  batch->state.speed_ground_x_self[idx] -=
      batch->state.speed_ground_x_self[idx] * c->dash_iasa_vel_mul * friction_mul;
}
