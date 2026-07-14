#include "combat_internal.h"

float combat_damage_ground_angle_to_floor_radians(float nx, float ny, float vx, float vy) {
  const float vmag_sq = vx * vx + vy * vy;
  if (!(vmag_sq > 0.0f)) {
    return 0.0f;
  }
  const float inv_vmag = 1.0f / sqrtf(vmag_sq);
  float cos_theta = (nx * vx + ny * vy) * inv_vmag;
  if (cos_theta > 1.0f) {
    cos_theta = 1.0f;
  } else if (cos_theta < -1.0f) {
    cos_theta = -1.0f;
  }
  return acosf(cos_theta);
}

void combat_damage_install_grounded_kb(const MslCommonParams* c, MslBatch* batch, size_t d_idx,
                                       float kb_applied, float kb_x, float kb_y,
                                       uint16_t hitlag_frames, uint8_t force_tumble_severity,
                                       uint8_t allow_damagefly_hitlag_ecb_lock) {
  const float nx = batch->state.ground_normal_x[d_idx];
  const float ny = batch->state.ground_normal_y[d_idx];
  const float angle_to_floor = combat_damage_ground_angle_to_floor_radians(nx, ny, kb_x, kb_y);
  const uint8_t sev = force_tumble_severity ? 3u : combat_damage_severity_u8_from_kb(c, kb_applied);

  // Grounded KB install owner:
  // - ftCo_8008DCE0 compares the floor normal to the raw KB vector with lbVector_Angle.
  // - angle < PI/2 launches immediately with full KB and clears grounded state.
  // - angle >= PI/2 projects along the floor for low/med severity.
  // - tumble severity (sev==3) still clears grounded state, and steep downward meteors
  //   (`angle > PI/2 + x1E8`) reflect their Y component upward with multiplier x1EC.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (angle_to_floor < combat_pi_over_two_f32()) {
    combat_apply_ftCommon_8007D5D4_ground_to_air(batch, d_idx);
    if (allow_damagefly_hitlag_ecb_lock && hitlag_frames != 0u) {
      // Supported runtime slice of the ftCommon_8007D5D4 ECB-lock producer:
      // - ftCommon_8007D5D4 sets fp->ecb_lock=10 on ground->air damage launch.
      // - This simulator only consumes that runtime-produced lock for active-hitlag DamageFly
      //   floor-callback handoffs that remain on a persisted ledge-floor line until hitlag exit.
      // - Other helper users remain on their existing seed/runtime owners until their callback
      //   consumers are modeled; replay seeds still carry explicit ecb_lock_timer when needed.
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
      batch->state.ecb_lock_timer[d_idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
    }
    combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
    return;
  }
  if (sev != 3u) {
    combat_damage_calc_vel(batch, d_idx, ny * kb_x, -nx * kb_x);
    return;
  }

  combat_apply_ftCommon_8007D5D4_ground_to_air(batch, d_idx);
  if (allow_damagefly_hitlag_ecb_lock && hitlag_frames != 0u) {
    batch->state.ecb_lock_timer[d_idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
  }
  if (angle_to_floor > (combat_pi_over_two_f32() + c->grounded_tumble_bounce_angle_extra_radians)) {
    combat_damage_calc_vel(batch, d_idx, kb_x, -kb_y * c->grounded_tumble_bounce_y_mul);
    return;
  }
  combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
}

uint8_t combat_sheik_chain_start_terminal_owns_low_hurt_height(const MslBatch* batch,
                                                               size_t source_a_idx,
                                                               size_t source_hb_i,
                                                               uint8_t defender_on_ground_before,
                                                               uint8_t source_hb_valid) {
  if (batch == NULL || source_hb_valid == 0u || defender_on_ground_before == 0u ||
      batch->state.char_id[source_a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[source_a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S_START &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i % (size_t)MSL_MAX_HITBOXES);
  if (hb_id != 3u) {
    return 0u;
  }
  // Chain Start terminal frontier DmgLog height:
  // source `it_802BCB88` publishes hb3 as the terminal Chain payload while
  // `ftColl_8007A06C` selects the grounded DamageHi/N/Lw group from the accepted DmgLog hurt
  // height. The terminal Start payload's replay-visible KB/damage pair selects DamageLw2 even
  // when the lite matrix scaffold also overlaps a high torso capsule, so keep the source hb3 DmgLog
  // height low instead of letting the scaffold's higher sibling reclassify the same hit.
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007A06C}
  return 1u;
}
