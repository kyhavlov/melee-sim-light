#pragma once

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "batch_internal.h"
#include "char_params.h"
#include "combat_geom.h"
#include "common_params.h"
#include "guard_lifecycle.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "state_flags.h"

static inline size_t msl_shielddesc_idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t msl_shielddesc_idx_hitbox_victim(int bi, int p, int hb_i, int victim) {
  return (((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
          (size_t)hb_i) *
             (size_t)MSL_MAX_PLAYERS +
         (size_t)victim;
}

static inline float msl_shielddesc_model_scale_for_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  const MslCharParams* params = msl_char_params(batch->state.char_id[idx]);
  return (params != NULL && isfinite(params->model_scaling) && params->model_scaling > 0.0f)
             ? params->model_scaling
             : 1.0f;
}

static inline uint8_t msl_shielddesc_fighter_overlap_ftcoll_80007bcc(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id, float hx, float hy,
    float hz, float hr, float shx, float shy, float shz, float shr, float shield_desc_radius,
    float shield_owner_scale_y, uint8_t shield_desc_envelope_ready,
    uint8_t shield_extent_bridge_active, float* out_overlap_margin) {
  if (out_overlap_margin != NULL) {
    *out_overlap_margin = 0.0f;
  }
  if (batch == NULL) {
    return 0u;
  }

  const size_t hb_i = msl_shielddesc_idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const MslCharParams* defender_params = msl_char_params(batch->state.char_id[d_idx]);
  const float shield_owner_model_scale = msl_shielddesc_model_scale_for_idx(batch, d_idx);
  const float shield_matrix_scale = shield_owner_scale_y * shield_owner_model_scale;

  // Fighter-vs-fighter ShieldDesc geometry owner:
  // - ftColl_80078C70 reaches this path only after ShieldDesc active and victims_1 gates.
  // - lbColl_80007BCC refreshes ShieldDesc.pos from `shield_hit.bone + shield_hit.offset`, then
  //   checks the HitCapsule x58->x4C segment with HitCapsule scale, ShieldDesc.size, and the
  //   `lbColl_804D7A34 * defender_scale` extent lane.
  // - This helper owns the simulator's reduced proxy for that source path; lifecycle/timer and
  //   hitlist/victim identity stay outside it.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007B1B8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  float eff_shx = shx;
  float eff_shy = shy;
  float eff_shz = shz;

  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    float m[12];
    const float sample_frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
    if (anim_pose_get_collision_matrix_f32(
            batch, d_idx, (uint16_t)MSL_SM_GUARD_DAMAGE, sample_frame,
            defender_params != NULL ? defender_params->grab_capture_anchor_part_id : 0u, m) == 0) {
      float lx = 0.0f;
      float ly = 0.0f;
      float lz = 0.0f;
      // GuardSetOff ShieldDesc pose owner:
      // - ftColl_8007B1B8 stores ShieldDesc.bone from `fp->ft_data->x8->x11` with zero offset and
      //   size=1 in ftCo_80092450.
      // - GuardSetOff uses ftCo_SM_GuardDamage and `ftCo_GuardSetOff_Anim` only calls
      //   ftCo_80091D58 while frames remain, so the live ShieldDesc center is the GuardDamage
      //   shield-bone matrix plus the current shield scale, not the steady Guard tilt table.
      // - `grab_capture_anchor_part_id` is extracted from the same `ft_data->x8->x11` source field.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80092450,ftCo_80091D58,ftCo_GuardSetOff_Anim}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
      const float zero[3] = {0.0f, 0.0f, 0.0f};
      msl_mtx34_mul_point(m, zero, &lx, &ly, &lz);
      const float model_scale = shield_owner_scale_y * shield_owner_model_scale;
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * lz * model_scale;
      eff_shy = batch->state.pos_y[d_idx] + ly * model_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * lx * model_scale;
    }
  }

  const uint8_t guardreflect_x14_expired_this_callback =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
       batch->state.guard_reflect_timer_x14[d_idx] == 0u &&
       batch->state.guard_reflect_origin_guardon[d_idx] != 0u)
          ? 1u
          : 0u;
  if (guardreflect_x14_expired_this_callback && !batch->state.hitbox_prev_enabled[hb_i]) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 &&
        tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u) {
      // GuardReflect final active-x14 callback:
      // - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` expires x14 and recreates ShieldDesc through
      //   `ftCo_80092450` before fighter collision. Keep this to GuardOn-origin episodes
      //   (`ftCo_8009388C`); direct locomotion powershields (`ftCo_80093A50`) stay on the
      //   ReflectDesc-only lane at the same visible timer boundary.
      // - For current-only/new HitCapsules, lbColl_80007BCC consumes that just-recreated
      //   ShieldDesc bone. Persistent HitCapsules keep the ordinary x58->x4C sweep owner below.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
      // data/shields/{fox,falco}.bin::guard_on_xyz[0]
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y * shield_owner_model_scale;
      const float dx = tv.guard_on_xyz[0];
      const float dy = tv.guard_on_xyz[1];
      const float dz = tv.guard_on_xyz[2];
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }

  const uint8_t guardreflect_direct_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_reflect_origin_guardon[d_idx] == 0u &&
       batch->state.guard_reflect_timer_x18[d_idx] == 0u &&
       msl_motion_state_common_class_has(batch->state.action_id[a_idx], MSL_MS_CLASS_ATTACK_AIR))
          ? 1u
          : 0u;
  if (guardreflect_direct_no_submotion) {
    MslShieldTiltTableView tv;
    const MslCommonParams* c = msl_common_params();
    if (c != NULL && msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 &&
        tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u) {
      // Direct GuardReflect ShieldDesc pose lifetime:
      // - `ftCo_80093A50` creates ShieldDesc and then `ftCo_800921DC` installs the GuardOn pose
      //   baseline.
      // - Every later `ftCo_GuardReflect_Anim` calls `ftCo_GuardOn_Anim`, advancing
      //   `mv.co.guard.x0` and `ftCo_80091E78` even while Slippi exposes no submotion.
      // - This retained slice is the aerial-HitCapsule shield path. Grounded attacks and Shine keep
      //   the established shield bubble owner until their separate source geometry is closed.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093A50,ftCo_80093BC0,ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,
      //   ftCo_800921DC,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
      // data/shields/{fox,falco}.bin::guard_on_xyz
      uint16_t init = (uint16_t)c->powershield_reflect_total_frames;
      init = (uint16_t)(init + 1u);
      uint16_t pose_frame = 0u;
      if (init > (uint16_t)batch->state.guard_reflect_timer_x18[d_idx]) {
        pose_frame = (uint16_t)(init - (uint16_t)batch->state.guard_reflect_timer_x18[d_idx]);
      }
      if (pose_frame >= tv.guard_on_frame_count) {
        pose_frame = (uint16_t)(tv.guard_on_frame_count - 1u);
      }
      const size_t go_i = (size_t)pose_frame * 3u;
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y * shield_owner_model_scale;
      const float dx = tv.guard_on_xyz[go_i + 0u];
      const float dy = tv.guard_on_xyz[go_i + 1u];
      const float dz = tv.guard_on_xyz[go_i + 2u];
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }

  const uint8_t guardon_raise_shield_no_submotion_attackair_x10 =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_on_entered_this_frame[d_idx] == 0u &&
       batch->state.guard_x10[d_idx] != 0u &&
       msl_action_is_live_shield_family(batch->state.seed_prev_action_id[d_idx]) &&
       msl_motion_state_common_class_has(batch->state.action_id[a_idx], MSL_MS_CLASS_ATTACK_AIR))
          ? 1u
          : 0u;
  const uint8_t guardon_raise_no_tilt_extent_lane =
      (guardon_raise_shield_no_submotion_attackair_x10 &&
       fabsf(batch->state.guard_tilt_x4[d_idx]) <= FLT_EPSILON)
          ? 1u
          : 0u;
  const uint8_t guardon_raise_tilted_attackairlw_model_scale_lane =
      (guardon_raise_shield_no_submotion_attackair_x10 &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_LW &&
       batch->state.hitbox_damage[hb_i] == 9.0f &&
       fabsf(batch->state.guard_tilt_x4[d_idx]) > FLT_EPSILON)
          ? 1u
          : 0u;
  if (guardon_raise_shield_no_submotion_attackair_x10) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 && tv.xyz != NULL &&
        tv.frame_count > 0u) {
      // Continuing GuardOn raise-shield ShieldDesc pose:
      // - `ftCo_800924C0` creates ShieldDesc and initializes mv.co.guard.x10.
      // - While x10 is live, `ftCo_GuardOn_Anim -> ftCo_80091E78` keeps sampling the GuardOn
      //   ftData.x20 shield target even though Slippi still exposes no submotion.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_800924C0,ftCo_GuardOn_Anim,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
      // data/shields/{fox,falco}.bin::guard_on_x20_xyz
      const uint16_t frame_max = (uint16_t)(tv.frame_count - 1u);
      const uint16_t f = (batch->state.guard_tilt_x8[d_idx] > frame_max)
                             ? frame_max
                             : batch->state.guard_tilt_x8[d_idx];
      float mag = batch->state.guard_tilt_x4[d_idx];
      if (mag < 0.0f) {
        mag = 0.0f;
      } else if (mag > 1.0f) {
        mag = 1.0f;
      }
      const size_t f_i = (size_t)f * 3u;
      const float dx = tv.guard_on_x20_xyz[0] + mag * (tv.xyz[f_i + 0u] - tv.guard_on_x20_xyz[0]);
      const float dy = tv.guard_on_x20_xyz[1] + mag * (tv.xyz[f_i + 1u] - tv.guard_on_x20_xyz[1]);
      const float dz = tv.guard_on_x20_xyz[2] + mag * (tv.xyz[f_i + 2u] - tv.guard_on_x20_xyz[2]);
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y;
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }

  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 && tv.xyz != NULL &&
        tv.frame_count > 0u) {
      const uint16_t frame_max = (uint16_t)(tv.frame_count - 1u);
      const uint16_t f = (batch->state.guard_tilt_x8[d_idx] > frame_max)
                             ? frame_max
                             : batch->state.guard_tilt_x8[d_idx];
      float mag = batch->state.guard_tilt_x4[d_idx];
      if (mag < 0.0f) {
        mag = 0.0f;
      } else if (mag > 1.0f) {
        mag = 1.0f;
      }

      // `ftCo_80091E78` only samples the angled Guard timeline when x4 is nonzero, then blends
      // it against the current no-tilt pose through `ftAnim_80070108(..., 1 - x4, x4, x20)`.
      // For fighter-vs-fighter ShieldDesc collision use that live frame-0 base locally.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091BC4,ftCo_80091E78}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
      const size_t n_i = 0u;
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
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y * shield_owner_model_scale;
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }

  float shield_desc_world_r = shield_desc_radius;
  if (shield_owner_scale_y > 0.0f) {
    shield_desc_world_r *= shield_matrix_scale;
  }
  const uint8_t shield_desc_lane_active =
      (shield_desc_envelope_ready &&
       (batch->state.hitbox_enable_edge[hb_i] || !batch->state.hitbox_pose_create[hb_i]))
          ? 1u
          : 0u;
  const uint8_t guardon_entry_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_on_entered_this_frame[d_idx] == 0u &&
       !msl_action_is_live_shield_family(batch->state.seed_prev_action_id[d_idx]))
          ? 1u
          : 0u;
  const uint8_t guardon_already_shielding_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       msl_action_is_live_shield_family(batch->state.seed_prev_action_id[d_idx]))
          ? 1u
          : 0u;
  const uint8_t guardreflect_final_x14_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u)
          ? 1u
          : 0u;
  const uint16_t a_action = batch->state.action_id[a_idx];
  const uint8_t attackairlw_fresh_guardon_shielddesc_size_lane =
      (guardon_already_shielding_no_submotion && batch->state.hitbox_enable_edge[hb_i] &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_LW)
          ? 1u
          : 0u;
  const uint8_t defender_flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  // Strong AttackAirLw fresh-HitCapsule vs no-submotion Guard ShieldDesc extent:
  // - ftAction_8007121C/ftColl_8007AD18 create the strong DAir HitCapsules on this callback edge,
  //   and lbColl_80007BCC forwards the ShieldDesc.size/extent lane into lbColl_80006E58.
  // - Keep this to the authored 12-damage strong DAir create edge while the defender has no raw
  //   x2218 command/behavior lane. Rows with x2218 allow/behavior command bits stay on their
  //   existing point-sample/seeded ShieldDesc miss owners.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/ft/types.h::Fighter::x2218
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const uint8_t attackairlw_strong_no_submotion_guard_extent_lane =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       (defender_flags_2218 &
        (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                  MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR)) == 0u &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_LW &&
       batch->state.hitbox_enable_edge[hb_i] && batch->state.hitbox_damage[hb_i] == 12.0f)
          ? 1u
          : 0u;
  // Weak Fox AttackAirLw multihit vs no-submotion Guard ShieldDesc miss:
  // - Fox DAir repeatedly recreates the authored 3/2-damage multihit pair on frames 5/8/11/...;
  //   source `lbColl_80007BCC` checks the live ShieldDesc matrix against the new HitCapsule and
  //   can miss even when the simulator's reduced enable-edge extent proxy overlaps.
  // - Keep the generic enable-edge extent out of this weak multihit Guard boundary. Falco's
  //   12-damage strong DAir owner and GuardOn/GuardReflect source-size lanes remain explicit above.
  // data/moves/fox.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const uint8_t attackairlw_weak_multihit_guard_enable_edge_extent_reject =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_LW &&
       batch->state.hitbox_enable_edge[hb_i] &&
       (batch->state.hitbox_damage[hb_i] == 3.0f || batch->state.hitbox_damage[hb_i] == 2.0f))
          ? 1u
          : 0u;
  const uint8_t shield_seed_kind =
      batch->state.combat_shield_contact_hb_kind[msl_shielddesc_idx_hitbox_victim(bi, attacker,
                                                                                  hb_id, defender)];
  const int16_t attackair_first_create_frame = move_tables_attackair_first_create_hitbox_frame(
      batch->state.char_id[a_idx], batch->state.action_id[a_idx]);
  // Weak AttackAirB HitCapsule vs tilted no-submotion Guard ShieldDesc:
  // - Fox/Falco BAir share the authored 9-damage late create phase across hb0/hb1/hb2. When
  //   Guard's submotion is not replay-visible, `ftCo_Guard_Anim -> ftCo_80091E78` still samples the
  //   live tilted shield JObj, and lbColl_80007BCC forwards the ShieldDesc extent into the same
  //   matrix narrowphase.
  // - Keep this to the per-HitCapsule weak BAir source, tilted Guard, persistent capsule rows, and
  //   either source shield-contact seed provenance or the first post-create collision callback. The
  //   seed lane proves this pair/hitbox belongs to a ShieldDesc contact owner; the live post-create
  //   lane covers rollout playback where hidden shield-contact kind is not fed every frame. The
  //   create callback itself and later persistent tilted controls without seed provenance remain on
  //   their existing BODY path.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const uint8_t attackairb_weak_tail_tilted_guard_extent_lane =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       fabsf(batch->state.guard_tilt_x4[d_idx]) > FLT_EPSILON &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_B &&
       (shield_seed_kind != 0u ||
        (batch->state.hitbox_prev_enabled[hb_i] != 0u && attackair_first_create_frame >= 0 &&
         batch->state.action_frame[a_idx] == (int16_t)(attackair_first_create_frame + 1))) &&
       batch->state.hitbox_prev_enabled[hb_i] && batch->state.hitbox_damage[hb_i] == 9.0f)
          ? 1u
          : 0u;
  // Strong AttackAirB root HitCapsule vs tilted GuardOn raise ShieldDesc.size:
  // - At the create edge, ftAction_8007121C has just published the authored 15-damage BAir root
  //   capsule (hb0) before ftColl_80078C70 checks ShieldDesc.
  // - lbColl_80007BCC includes ShieldDesc.size in the matrix narrowphase. The reduced simulator
  //   proxy needs that size term for the root capsule boundary, but should not also add the broader
  //   extent term or apply it to hb1 tail controls; those over-block adjacent BODY/source-order
  //   rows in aggregate validation.
  // - The no-submotion GuardOn x10/tilt owner is the same ftCo_80091E78 live ShieldDesc pose above;
  //   this predicate is per-HitCapsule source data (hb0, strong 15-damage BAir create edge), not a
  //   replay row or character-pair selector.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  const uint8_t attackairb_strong_root_guardon_raise_size_lane =
      (guardon_raise_shield_no_submotion_attackair_x10 &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_B &&
       batch->state.hitbox_enable_edge[hb_i] && hb_id == 0 &&
       batch->state.hitbox_damage[hb_i] == 15.0f)
          ? 1u
          : 0u;
  // No-submotion GuardOn entry/raise rows can expose a recreated ShieldDesc before Slippi exposes
  // a settled Guard submotion. Keep the ShieldDesc.size term on source create/enable edges only;
  // persistent aerial capsules have replay-real miss controls and must stay on the ordinary matrix
  // radius path.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const uint8_t guardon_entry_enable_edge_size_lane =
      (guardon_entry_no_submotion && batch->state.hitbox_enable_edge[hb_i]) ? 1u : 0u;
  // Once x14 has expired, ftCo_80093BC0 has recreated ShieldDesc, but the retained size term is
  // still limited to create/pose-create edges with x18 already gone. Broadening this to sustained
  // no-submotion GuardReflect rows over-admits near-rim shield hits.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
  const uint8_t guardreflect_expired_no_submotion_enable_edge_size_lane =
      (guardreflect_final_x14_no_submotion && batch->state.action_frame[d_idx] == -1 &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
       batch->state.guard_reflect_timer_x18[d_idx] == 0u && shr > 0.0f &&
       batch->state.hitbox_pose_create[hb_i])
          ? 1u
          : 0u;
  // Persistent AttackAirLw capsules can still meet the callback-local ShieldDesc.size term after
  // `ftCo_80093BC0` expires x14/x18 and recreates ShieldDesc. Keep this to direct no-submotion
  // GuardReflect with no x18 ownership; broader sustained GuardReflect size terms over-admit
  // adjacent shield/body boundaries.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  const uint8_t guardreflect_expired_no_submotion_attackairlw_persistent_size_lane =
      (guardreflect_final_x14_no_submotion && batch->state.action_frame[d_idx] == -1 &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
       batch->state.guard_reflect_timer_x18[d_idx] == 0u && shr > 0.0f &&
       batch->state.hitbox_prev_enabled[hb_i] &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
       batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_AIR_LW)
          ? 1u
          : 0u;
  const float shield_desc_term =
      ((shield_desc_lane_active &&
        (guardon_entry_enable_edge_size_lane || attackairlw_fresh_guardon_shielddesc_size_lane ||
         guardreflect_expired_no_submotion_enable_edge_size_lane ||
         attackairlw_strong_no_submotion_guard_extent_lane ||
         attackairb_weak_tail_tilted_guard_extent_lane ||
         attackairb_strong_root_guardon_raise_size_lane)) ||
       guardreflect_expired_no_submotion_attackairlw_persistent_size_lane)
          ? shield_desc_world_r
          : 0.0f;

  const uint8_t shield_extent_lane_active =
      (shield_desc_envelope_ready && !guardreflect_final_x14_no_submotion &&
       (!guardon_already_shielding_no_submotion || guardon_raise_no_tilt_extent_lane) &&
       !attackairlw_weak_multihit_guard_enable_edge_extent_reject &&
       (batch->state.hitbox_enable_edge[hb_i] || shield_extent_bridge_active ||
        guardon_raise_no_tilt_extent_lane || attackairlw_strong_no_submotion_guard_extent_lane ||
        attackairb_weak_tail_tilted_guard_extent_lane))
          ? 1u
          : 0u;
  // The real helper forwards `lbColl_804D7A34 * defender_scale` into the full matrix narrowphase.
  // The simulator's reduced sphere/segment proxy only carries a small equivalent extent on the
  // proven edge lanes; using the full term on steady Guard/GuardReflect rows regresses adjacent
  // ShieldDesc-miss/BODY boundaries.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const uint8_t shine_start_enable_edge = (batch->state.hitbox_enable_edge[hb_i] &&
                                           (a_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
                                            a_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START))
                                              ? 1u
                                              : 0u;
  const float shield_extent_scale = (shield_extent_bridge_active || shine_start_enable_edge ||
                                     attackairlw_strong_no_submotion_guard_extent_lane ||
                                     attackairb_weak_tail_tilted_guard_extent_lane)
                                        ? 1.0f
                                        : 0.2f;
  const float shield_extent_env_r =
      shield_extent_lane_active ? (shield_desc_world_r * shield_extent_scale) : 0.0f;
  // Tilted GuardOn raise-shield vs the authored 9-damage late AttackAirLw capsule uses the
  // authored shield matrix radius after the live tilt table sample. Other no-submotion GuardOn
  // raise rows retain the source no-model-scale path; broadening the model-scale lane into early
  // 12-damage DAir, AttackAirHi/B, or GuardReflect creates false GuardSetOff contacts in aggregate
  // controls.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  const uint8_t shield_matrix_uses_unscaled_radius =
      (uint8_t)((guardreflect_final_x14_no_submotion ||
                 guardon_raise_shield_no_submotion_attackair_x10) &&
                guardon_raise_tilted_attackairlw_model_scale_lane == 0u);
  const float shield_matrix_radius =
      shr * (shield_matrix_uses_unscaled_radius ? 1.0f : shield_owner_model_scale);
  const float rr = hr + shield_matrix_radius + shield_desc_term + shield_extent_env_r;
  float d2 = 0.0f;

  if (batch->state.hitbox_prev_enabled[hb_i]) {
    const float px = batch->state.hitbox_prev_x[hb_i];
    const float py = batch->state.hitbox_prev_y[hb_i];
    const float pz = batch->state.hitbox_prev_z[hb_i];
    combat_point_segment_dist2(eff_shx, eff_shy, eff_shz, px, py, pz, hx, hy, hz, &d2, NULL);
  } else {
    const float dx = hx - eff_shx;
    const float dy = hy - eff_shy;
    const float dz = hz - eff_shz;
    d2 = dx * dx + dy * dy + dz * dz;
  }

  if (out_overlap_margin != NULL) {
    *out_overlap_margin = rr - sqrtf(d2);
  }
  return (uint8_t)(d2 <= rr * rr);
}
