#pragma once

#include <stddef.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"

static inline float dash_iasa_clamp_absf(float value, float max_abs) {
  if (value > max_abs) {
    return max_abs;
  }
  if (value < -max_abs) {
    return -max_abs;
  }
  return value;
}

static inline void dash_iasa_apply_root_motion_exit_gr_vel_clamp(MslBatch* batch,
                                                                 const MslCharParams* ch,
                                                                 size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Decomp: Fighter_ChangeMotionState snapshots the previous motion's x594 root-motion flags
  // before loading the destination motion. When the previous motion had root motion and the new
  // motion does not, it clamps `fp->gr_vel` to `co_attrs.dash_run_terminal_velocity` before the
  // caller resumes. Dash_IASA can then continue to its terminal scalar in the same callback.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  batch->state.speed_ground_x_self[idx] =
      dash_iasa_clamp_absf(batch->state.speed_ground_x_self[idx], ch->dash_run_terminal_velocity);
  batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
}

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
