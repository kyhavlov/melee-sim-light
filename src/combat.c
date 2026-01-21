#include "combat.h"

#include <stdint.h>

#include "combat_geom.h"
#include "common_params.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg, uint16_t msid) {
  if (c == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Notes for this pass:
  // - We use mul=1.0f (fp->x1960_vibrateMult defaults to 1.0; refs/melee/src/melee/ft/fighter.c).
  // - We do not yet apply the Squat/SquatWait multiplier branch (msid check) because we don't yet
  //   have motion-state id constants in-core; add once we model FtMotionId tables.
  const float tmp_f = (float)dmg * c->hitlag_dmg_mul + c->hitlag_base;
  int tmp = (int)tmp_f;

  // Default vibrate multiplier:
  // refs/melee/src/melee/ft/fighter.c (init sets fp->x1960_vibrateMult = 1).
  const float mul = 1.0f;

  float result_f = (float)tmp * mul;
  // TODO(decomp): apply squat scaling for msid in [ftCo_MS_Squat, ftCo_MS_SquatWait] using
  // p_ftCommonData->x1A0 (extracted as hitlag_squat_mul) once FtMotionId constants are modeled.
  (void)msid;

  int result_i = (int)result_f;
  if (result_i < 0) {
    result_i = 0;
  }
  if (result_i > 0xFFFF) {
    result_i = 0xFFFF;
  }
  return (uint16_t)result_i;
}

void combat_resolve(MslBatch* batch) {
  // NOTE: Combat resolution is currently non-mutating.
  //
  // Root cause: without decomp-backed hitlist/rehit-rate/shields/intangibility/clanks/trades/etc.,
  // applying percent/hitlag/hitstun/attribution based only on geometric overlap creates false
  // positives and repeated hits, degrading teacher-forced one-step validation.
  //
  // Keep geometry + debug contacts:
  // - `src/combat_geom.h` implements the intersection primitive.
  // - `msl_batch_debug_combat_contacts` (src/api.c) exposes overlap diagnostics without mutating
  //   validated state.
  (void)batch;
}
