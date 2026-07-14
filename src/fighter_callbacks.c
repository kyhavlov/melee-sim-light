#include "fighter_callbacks.h"

#include <errno.h>

#include "action.h"
#include "action_ids.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "combat.h"
#include "damage_terminal_owner.h"
#include "damage_source.h"
#include "hitboxes.h"
#include "hitlist.h"
#include "hurtboxes.h"
#include "blaster.h"
#include "grab_attachment.h"
#include "grab_flow.h"
#include "ids.h"
#include "input.h"
#include "input_axis.h"
#include "items.h"
#include "ledge.h"
#include "locomotion.h"
#include "match_flow.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "fighter_script.h"
#include "fighter_contact.h"
#include "fighter_pose.h"
#include "knockdown.h"
#include "physics.h"
#include "shields.h"
#include "sheik_specials.h"
#include "shine.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "mpcoll_env.h"
#include "timers.h"
#include "reflector_bubbles.h"
#include "state_flags.h"

// NOTE: `blaster_update_post_collision` is intentionally not part of the public blaster module API
// yet; keep the forward declaration local to preserve the current include surface.
extern void blaster_update_post_collision(MslBatch* batch);

static void camera_update_zoom_scale_from_player0_cstick(MslBatch* batch,
                                                         const uint8_t* input_bytes,
                                                         size_t input_stride_bytes) {
  if (batch == NULL || input_bytes == NULL || input_stride_bytes < sizeof(MslInput) ||
      batch->camera_zoom_scale_x2bc == NULL || batch->camera_zoom_hold_x2ba == NULL) {
    return;
  }
  // MSL's validation/RL target is normal VS gameplay. Source Camera_8002B0E0 updates x2BC only
  // when gm_8016B41C() is true (Classic/Adventure/All-Star/Training/etc.) and x2C0 is live; in VS
  // replay validation the hidden zoom scalar remains at Camera_80028B9C's initialized 1.0f.
  // Keep the substrate explicit because Fighter_procUpdate gates magnify damage on
  // Camera_80031144()==1.0f, but do not consume C-stick zoom input on the supported VS path.
  // refs/melee/src/melee/cm/camera.c::{Camera_80028B9C,Camera_8002B0E0,Camera_80031144}
  // refs/melee/src/melee/gm/gm_16AE.c::gm_8016B41C
  (void)input_bytes;
  (void)input_stride_bytes;
}

static inline void clear_begin_frame_anim_transients(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Clear per-frame anim-timebase transients.
      batch->state.anim_defer_tick_once[idx] = 0;
    }
  }
}

static inline void clear_landing_status_after_anim_timebase_fighter(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Slippi post-frame `l_cancel` is a 1-frame status on LandingAir* entry. Preserve it through
  // Fighter_8006A360 ordering so frame-0 LandingAir replay seeds can consume the same
  // ftCo_LandingAir_EnterWithLag branch, then clear before current-frame gameplay/output unless
  // collision enters a fresh LandingAir row later this step.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  batch->state.l_cancel[idx] = 0;
}

static inline void clear_seed_owned_transients_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int platform_id = 0; platform_id < 2; platform_id++) {
      const size_t pidx = (size_t)bi * 2u + (size_t)platform_id;
      // FoD platform-height source bits are seed/current-frame provenance for sparse replay
      // reconstruction, not live scheduler state. Consume them during the reseeded frame; later
      // frames require live velocity/scheduler/contact evidence or generated initial-height data
      // again.
      // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
      // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
      if (batch->state.stage_fod_platform_deferred_velocity_valid[pidx]) {
        // Same-step FoD platform contacts expose the current collision height for this frame before
        // the next grIzumi velocity is source-visible. Promote that deferred velocity only after
        // the reseeded frame so the landing frame does not pre-advance the platform.
        batch->state.stage_fod_platform_velocity[pidx] =
            batch->state.stage_fod_platform_deferred_velocity[pidx];
        batch->state.stage_fod_platform_velocity_valid[pidx] = 1u;
        batch->state.stage_fod_platform_deferred_velocity[pidx] = 0.0f;
        batch->state.stage_fod_platform_deferred_velocity_valid[pidx] = 0u;
      }
      batch->state.stage_fod_platform_height_source[pidx] = 0u;
    }
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.fall_fast_seed_frame_start[idx] = 0u;
      batch->state.fall_fast_seed_frame_start_valid[idx] = 0u;
      batch->state.blaster_gun_spawned_this_frame[idx] = 0u;
      // DamageFly wall-ASDI provenance is allowed to arm on the SpecialAirHi wall-contact frame
      // before combat starts hitlag, but it must become live only if the frame actually enters or
      // continues hitlag. Non-hitlag wall contacts are stale CollData for this owner.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
      if (batch->state.damage_hitlag_wall_asdi_latch[idx] != 0u &&
          batch->state.hitlag_pre_timer[idx] == 0u && batch->state.hitlag[idx] == 0u) {
        batch->state.damage_hitlag_wall_asdi_latch[idx] = 0u;
      }
      if (batch->state.hitlag_pre_timer[idx] == 0u && batch->state.hitlag[idx] == 0u) {
        batch->state.damageflyroll_runtime_x1994_on_exit[idx] = 0u;
      }
      // One-step replay-facing locomotion / instance-order lanes. Runtime producers update the
      // causal state directly; seeded overrides are consumed within the current step only.
      batch->state.turn_kneebend_facing_override[idx] = 0u;
      batch->state.motion_entry_instance_id_override[idx] = 0u;
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBOUND_STOP &&
          batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBOUND) {
        batch->state.rebound_ground_accel_2[idx] = 0.0f;
        batch->state.rebound_anim_rate_fp_q16_16[idx] = 0;
      }
      // `seed_t.walljump_*` can bridge a hidden CollData WallHug phase for one-step replay rows.
      // Runtime after that step must require live mpColl WallHug bits again.
      // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
      batch->state.walljump_seed_phase_valid[idx] = 0u;
      if (!msl_specialhi_rotate_model_action(batch->state.char_id[idx],
                                             batch->state.action_id[idx])) {
        batch->state.specialhi_rotate_model_valid[idx] = 0u;
        batch->state.specialhi_rotate_model[idx] = 0.0f;
      }
    }
    // Teacher-forced shield-contact lanes are one-step reseed surfaces. Normal rollouts must
    // return to live shield geometry / collision ordering after the seeded frame.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    const size_t hb_seed_base =
        ((size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS);
    for (int attacker = 0; attacker < num_players; attacker++) {
      const size_t a_idx = msl_idx_player(bi, attacker);
      // Authoritative per-HitCapsule seed-valid lanes are teacher-forced visibility for the
      // reseeded frame. Runtime rollout should then use live `fighter_hitlist` state, except when
      // the attacker remains hitlag-frozen and Fighter_8006A360 skips the script/update path that
      // would otherwise materialize or clear the live capsule. Leaving old per-HitCapsule seed
      // lanes valid after the source episode can make ftColl_80078C70 reject a later unrelated
      // hitbox-vs-hitbox clank candidate.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      const uint8_t preserve_hitlag_frozen_hitbox_seed =
          (batch->state.hitlag_pre_timer[a_idx] != 0u || batch->state.hitlag[a_idx] != 0u) ? 1u
                                                                                           : 0u;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t hb_valid_i =
            ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
            (size_t)hb;
        if (!preserve_hitlag_frozen_hitbox_seed) {
          batch->state.combat_hitlist_hb_valid[hb_valid_i] = 0u;
        }
        for (int victim = 0; victim < num_players; victim++) {
          const size_t si =
              hb_seed_base + (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb) *
                                  (size_t)MSL_MAX_PLAYERS +
                              (size_t)victim);
          if (!preserve_hitlag_frozen_hitbox_seed) {
            batch->state.combat_hitlist_hb_cd[si] = 0u;
            batch->state.combat_hitlist_hb_victim_iid[si] = 0u;
          }
        }
      }
    }
  }
}

static inline void promote_seed_prev_action_snapshot_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.seed_prev_action_id[idx] = batch->state.prev_action_id[idx];
      batch->state.seed_prev_action_frame[idx] = batch->state.prev_action_frame[idx];
    }
  }
}

static inline void cache_prev_action_state(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.prev_action_id[idx] = batch->state.action_id[idx];
      batch->state.prev_action_frame[idx] = batch->state.action_frame[idx];
      batch->state.frame_start_action_id[idx] = batch->state.action_id[idx];
      batch->state.frame_start_hitstun[idx] = batch->state.hitstun[idx];
      batch->state.frame_start_animation_index[idx] = batch->state.animation_index[idx];
      batch->state.frame_start_instance_id[idx] = batch->state.instance_id[idx];
      batch->state.frame_start_on_ground[idx] = batch->state.on_ground[idx] ? 1u : 0u;
      batch->state.sheik_special_timer_frame_start[idx] = batch->state.sheik_special_timer[idx];
      batch->state.fall_fast_frame_start[idx] =
          batch->state.fall_fast_seed_frame_start_valid[idx]
              ? (batch->state.fall_fast_seed_frame_start[idx] ? 1u : 0u)
              : (batch->state.fall_fast[idx] ? 1u : 0u);
      batch->state.dash_entered_this_frame[idx] = 0u;
      batch->state.blaster_gun_spawned_this_frame[idx] = 0u;
    }
  }
}

static inline void cache_floor_sweep_prev_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp: ft_80081DD4-style map callbacks preserve the previous CollData position as the start
  // of the collision sweep, then write the fighter's current position before calling mpColl. The
  // replay seed surface provides that previous sweep endpoint explicitly.
  //
  // In rollout, do not overwrite the current sweep endpoint with the current root at frame start:
  // DamageFly floor-contact rows can need the prior callback's sweep root to see the floor
  // crossing. The next endpoint is promoted post-frame from the callback-published root, matching
  // `ft_081B` wrappers that assign `coll_data.last_pos = coll_data.cur_pos` before writing the
  // next callback root.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_80081DD4}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpCheckFloor}
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.floor_sweep_seed_prev_valid[idx]) {
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.floor_sweep_seed_prev_pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.floor_sweep_seed_prev_pos_y[idx];
        batch->state.floor_sweep_seed_prev_valid[idx] = 0u;
        batch->state.floor_sweep_prev_source_owned[idx] = 1u;
      } else if (!isfinite(batch->state.floor_sweep_prev_pos_x[idx]) ||
                 !isfinite(batch->state.floor_sweep_prev_pos_y[idx])) {
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.pos_y[idx];
        batch->state.floor_sweep_prev_source_owned[idx] = 0u;
        batch->state.floor_sweep_prev_runtime_owned[idx] = 0u;
      }
    }
  }
}

static inline void promote_floor_sweep_prev_pos_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t damage_hitlag_sdi_floor_publication =
          (uint8_t)(batch->state.live_coll_callback_ran[idx] != 0u &&
                    batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
                    batch->state.damage_hitlag_downward_sdi_consumed[idx] != 0u &&
                    batch->state.coll_floor_result_valid[idx] != 0u &&
                    msl_damage_owner_is_damage_collision_landing_action(
                        batch->state.action_id[idx]));
      const uint8_t preserve_active_damage_hitlag_ecb_sweep =
          // Frozen Damage hitlag can carry a hidden `CollData.ecb` packet while a same-callback
          // floor probe rejects. Source keeps the previous mpCollPrev root for the next callback;
          // the rejected callback-local displacement does not become floor-sweep authority until a
          // live floor contact packet is published.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
          //   mpColl_80044628_Floor}
          (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
           batch->state.coll_damage_hitlag_ecb_valid[idx] != 0u &&
           batch->state.coll_damage_hitlag_floor_contact_runtime[idx] == 0u &&
           msl_damage_owner_is_damage_collision_landing_action(batch->state.action_id[idx]))
              ? 1u
              : 0u;
      if (damage_hitlag_sdi_floor_publication) {
        // ftCo_Damage_OnEveryHitlag moves Fighter.cur_pos before ft_80081DD4. When the stay-airborne
        // collision callback then projects that downward SDI back to a floor, its final
        // CollData.cur_pos is the next mpCollPrev root; carrying the displaced pre-collision root
        // starts the following sweep below the floor and loses the FloorHug continuation.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
        // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80044948_Floor}
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.pos_y[idx];
        batch->state.floor_sweep_prev_source_owned[idx] = 1u;
        batch->state.floor_sweep_prev_runtime_owned[idx] = 1u;
      } else if (!preserve_active_damage_hitlag_ecb_sweep) {
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.prev_pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.prev_pos_y[idx];
        batch->state.floor_sweep_prev_source_owned[idx] = 1u;
        batch->state.floor_sweep_prev_runtime_owned[idx] = 1u;
      }
      // Source `mpCollPrev` preserves CollData.cur_pos across map callbacks. Wall/ceiling
      // callbacks that enter through `ft_CheckGroundAndLedge` must sweep from the last
      // callback-published root, while floor-sweep owners continue to consume the older
      // `floor_sweep_prev_pos_*` lane with its one-step seed semantics.
      // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80046904}
      batch->state.coll_wall_ceil_prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_wall_ceil_prev_pos_y[idx] = batch->state.pos_y[idx];
      batch->state.coll_wall_ceil_prev_pos_valid[idx] = 1u;
    }
  }
}

static inline void cache_guard_reflect_timer_seed_snapshots(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.guard_reflect_timer_x14_seed[idx] = batch->state.guard_reflect_timer_x14[idx];
      batch->state.guard_reflect_timer_x18_seed[idx] = batch->state.guard_reflect_timer_x18[idx];
      const size_t flags_221b_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      batch->state.guard_seed_shield_desc_active[idx] =
          ((batch->state.state_flags[flags_221b_i] &
            (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u)
              ? 1u
              : 0u;
    }
  }
}

static inline void cache_collision_stage_prev_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp: the ledge-grab AABB builders (mpColl_80044164 / mpColl_800443C4) consume CollData.prev_pos
  // and CollData.cur_pos as managed inside the collision substep loop (mpColl_80043754).
  //
  // This stores the collision-stage interval used by ledge-grab AABB checks:
  // - coll_stage_prev_pos: position at start of collision stage this frame
  // - coll_stage_cur_pos: position immediately after stage_collision_apply() this frame
  // Callback-local floor substep owners that stop earlier publish their own coll_substep_* packet.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_prev_pos_y[idx] = batch->state.pos_y[idx];
      batch->state.coll_stage_prev_ground_id[idx] = batch->state.ground_id[idx];
    }
  }
}

static inline void cache_collision_stage_cur_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_cur_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_cur_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static inline void cache_frame_start_state_flags(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.state_flags_2218_frame_start[idx] =
          batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                   (size_t)MSL_STATE_FLAGS_2218_INDEX];
      batch->state.state_flags_221c_frame_start[idx] =
          batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                   (size_t)MSL_STATE_FLAGS_221C_INDEX];
    }
  }
}

static inline void cache_damagefly_hitlag_exit_sweep_root(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] == 0u ||
          !msl_motion_state_common_class_has_fast(batch->state.action_id[idx],
                                                  MSL_MS_CLASS_DAMAGE_FLY)) {
        continue;
      }
      // Hitlag-exit ownership:
      // - Fighter_8006A1BC ends hitlag and ftCo_Damage_OnExitHitlag applies ASDI before Phys/Coll.
      // - ft_80081DD4 then copies the fighter's post-callback cur_pos into CollData.cur_pos while
      //   retaining CollData.prev_pos as the pre-callback sweep root for mpColl.
      // Cache that root before timer callbacks mutate position; physics_integrate preserves it on
      // the exit frame so DamageFlyRoll_Coll can see wall Hug from the ASDI displacement.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static void fighter_callbacks_begin_frame_phase(MslBatch* batch) {
  clear_begin_frame_anim_transients(batch);
  cache_prev_action_state(batch);
  cache_floor_sweep_prev_pos(batch);
  cache_guard_reflect_timer_seed_snapshots(batch);
  cache_frame_start_state_flags(batch);
  cache_damagefly_hitlag_exit_sweep_root(batch);
}

static int fighter_callbacks_pre_input_anim_phase(MslBatch* batch, const uint8_t* prev_input_bytes,
                                                  size_t prev_input_stride_bytes) {
  // Decomp callback ordering: pre-input anim callbacks (prio1) run before input_cb (prio3), so
  // snapshot prior-frame inputs for pre-input gameplay ownership.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  int err = input_apply_pre_input_snapshot(batch, prev_input_bytes, prev_input_stride_bytes);
  if (err != 0) {
    return err;
  }

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  //
  // Decomp ordering note:
  // - refs/melee/src/melee/ft/fighter.c registers `Fighter_8006A1BC` (timer decrement) at proc prio 0,
  //   before input processing, animation advancement, and physics (`Fighter_procUpdate`).
  // We follow that by updating timers before applying inputs/advancing state.
  timers_update(batch);

  // Damage post-hitlag callback consume (subset) at proc prio 0 before current-frame input.
  // Decomp:
  // - Fighter_8006A1BC decrements hitlag and invokes Fighter_8006D10C when hitlag ends,
  // - damage path uses ftCo_Damage_OnExitHitlag.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
  timers_consume_post_hitlag_callbacks_pre_input(batch);

  // Match flow (Entry/Death/Rebirth) updates its own timers and can change action state before
  // animation timebase advancement.
  match_flow_update_pre_anim(batch);

  items_update_pre_fighter_anim_phase(batch);
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      // Source priority-1 ownership is one complete Fighter_8006A360 procedure per fighter, not
      // one global pass per subphase. This ordering is observable when a CatchPull or Throw Anim
      // callback changes its linked peer before that peer's later procedure runs.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchPull_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      const size_t idx = msl_idx_player(bi, p);
      anim_timebase_update_pre_input_fighter(batch, bi, p);
      fighter_pose_publish_animation_phase_fighter(batch, bi, p);
      fighter_script_advance_fighter(batch, bi, p);
      grab_attachment_refresh_xrotn_constraint_after_anim(batch, idx);
      clear_landing_status_after_anim_timebase_fighter(batch, idx);
      match_flow_update_post_anim_fighter(batch, bi, p);
      timers_update_post_anim_fighter(batch, bi, p);
      const MslFighterCallbackContext ctx = msl_fighter_callback_context_make(
          batch, bi, p, num_players, MSL_FIGHTER_CALLBACK_PHASE_PRE_INPUT_ANIM);
      action_update_anim_callback_pre_input_fighter(&ctx);
      items_spawn_fighter_anim_callback(batch, bi, p);
    }
  }
  items_blaster_gun_anim_phase(batch);
  items_finish_fighter_anim_phase(batch);

  return 0;
}

static int fighter_callbacks_input_phase(MslBatch* batch, const uint8_t* prev_input_bytes,
                                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                                         size_t input_stride_bytes) {
  int err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                        input_stride_bytes);
  if (err != 0) {
    return err;
  }
  // Hidden camera zoom scalar:
  // Camera_8002B0E0 updates `cm_80452C68.x2BC` from player-0 C-stick Y; Fighter_procUpdate later
  // admits magnify damage only when Camera_80031144() returns exactly 1.0f.
  // refs/melee/src/melee/cm/camera.c::{Camera_8002B0E0,Camera_80031144}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  camera_update_zoom_scale_from_player0_cstick(batch, input_bytes, input_stride_bytes);

  // Damage hitlag callback consume (subset) after input apply and before collision ownership.
  // Decomp:
  // - hitlag callbacks (`hitlag_cb`) execute during Fighter_procUpdate after current-frame input,
  // - damage path uses ftCo_Damage_OnEveryHitlag while hitlag is still active.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  timers_consume_post_hitlag_callbacks_after_input(batch);
  return 0;
}

static void fighter_callbacks_iasa_phase(MslBatch* batch) {
  // Match flow IASA: certain match-flow states can exit based on current-frame inputs.
  // Decomp: motion state IASA callbacks run after Anim and before Phys/Coll.
  // NOTE: match_flow_update_post_input owns simplified match-flow state (see match_flow.c).
  match_flow_update_post_input(batch);

  action_update(batch);
}

static void fighter_callbacks_phys_phase(MslBatch* batch) {
  state_flags_refresh_camera_targets_pre_physics(batch);
  physics_integrate(batch);
}

static void fighter_callbacks_collision_phase(MslBatch* batch) {
  cache_collision_stage_prev_pos(batch);
  motion_state_install_live_callbacks_before_map(batch);
  motion_state_finalize_seeded_coll_data_before_map(batch);
  stage_collision_apply(batch);
  cache_collision_stage_cur_pos(batch);
  grab_attachment_update_thrown_accessory_phase(batch);
  grab_attachment_update_falcon_dive_accessory_phase(batch);
  // Collision environment flags (mpColl-shaped): owns Collide_LedgeGrabMask for scheduling ledge
  // catch after collision.
  mpcoll_env_update_ledge_grab(batch);
  grab_attachment_update_post_collision(batch);
  ledge_try_catch_post_collision(batch);
  knockdown_update_post_collision(batch);
  physics_apply_attackdash_downbound_overlap_nudge_post_collision(batch);
  match_flow_update_post_physics(batch);
  blaster_update_post_collision(batch);
  locomotion_update_post_collision(batch);
  shine_update_post_collision(batch);
  sheik_specials_update_accessory4_phase(batch);

  // ftCommon_8007D6A4 resets x1969_walljumpUsed during every air-to-ground conversion. Run this
  // after collision callbacks and before combat so a same-frame grounded hit cannot preserve the
  // pre-landing count when ProcessHit launches the fighter back into the air.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.on_ground[idx] != 0u && batch->state.walljump_used_count[idx] != 0u) {
        batch->state.walljump_used_count[idx] = 0u;
      }
    }
  }
}

static void fighter_callbacks_primitive_refresh_phase(MslBatch* batch, uint8_t run_combat) {
  // Motion-state entry owners that request an immediate ftAnim_8006EBA4-equivalent tick must do so
  // before primitive refresh. Otherwise hurtcaps/hitboxes are built from the entry-start pose while
  // the post-frame action_frame already reflects the advanced AObj timebase.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  anim_timebase_apply_deferred_tick_once_pre_collision(batch);
  grab_flow_refresh_catch_contract(batch);
  if (run_combat) {
    // Fighter hitbox refresh needs the current-frame merged hurtbox state for no-damage contact
    // carry, but BODY/catch/item collision only consumes endpoint geometry when an item or opposing
    // HitCapsule exists. Keep the decomp-shaped state/mode pass unconditional and defer the
    // lb_8000B1CC-style endpoint matrix sampling until the current frame has contact demand.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B868,ftColl_80076ED8,ftColl_80078A2C}
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    hurtboxes_refresh_metadata(batch);
    hitboxes_refresh(batch);
    hurtboxes_refresh_contact_geometry(batch);
  } else {
    // Debug pre-combat must expose the same primitive refresh state that full combat consumes.
    // Keep only combat_resolve()/post-combat mutations skipped.
    hurtboxes_refresh_metadata(batch);
    hitboxes_refresh(batch);
    hurtboxes_refresh_contact_geometry(batch);
  }
}

static void fighter_callbacks_item_collision_and_combat_phase(MslBatch* batch, uint8_t run_combat) {
  hitlist_tick(batch);
  shields_refresh(batch);
  reflector_bubbles_refresh(batch);
  if (run_combat) {
    combat_processhit_pending_begin(batch);
    fighter_contact_resolve_catch(batch);
    // Fighter_8006CB94 traverses fighter HitCapsules before the same fighter's item-HitCapsule
    // contacts in ftColl_8007925C. This is observable when a fresh Vanish article appears after
    // accessory publication: an aerial can first contact the invincible owner, then clank with
    // the article without also reaching its BODY packet.
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007925C}
    fighter_contact_resolve_damage(batch);
  }
  items_update_collision_phase(batch);
  if (run_combat) {
    combat_processhit_resolve(batch);
    items_update_post_combat(batch);
  }
  knockdown_update_post_combat(batch);
}

static void fighter_callbacks_post_frame_phase(MslBatch* batch) {
  anim_timebase_apply_deferred_tick_once_post_combat(batch);
  // Fighter_8006D9AC owns ftCo_8009E0A8 at GObj priority 0x10, after priority-0xD fighter
  // collision and priority-0xE ProcessHit. The solved JObj rotation is persistent input to the
  // next frame's collision pass; advancing it before primitive refresh makes the chain one frame
  // early and is observably wrong at action transitions.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_Create,Fighter_8006D9AC}
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009E0A8,ftCo_8009DD94}
  anim_pose_update_dynamic_state(batch);
  state_flags_refresh_post_frame(batch);
  sheik_specials_cache_transform_twins_post_frame(batch);
  timers_update_magnify_damage_post_frame(batch);
  promote_floor_sweep_prev_pos_post_frame(batch);
  promote_seed_prev_action_snapshot_post_frame(batch);
  clear_seed_owned_transients_post_frame(batch);
}

int fighter_callbacks_step_frame(MslBatch* batch, const uint8_t* prev_input_bytes,
                                 size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                                 size_t input_stride_bytes, uint8_t run_combat) {
  if (batch == NULL) {
    return EINVAL;
  }

  combat_rng_trace_begin_frame(batch);
  fighter_callbacks_begin_frame_phase(batch);
  int err =
      fighter_callbacks_pre_input_anim_phase(batch, prev_input_bytes, prev_input_stride_bytes);
  if (err != 0) {
    combat_rng_trace_end_frame(batch);
    return err;
  }
  err = fighter_callbacks_input_phase(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                      input_stride_bytes);
  if (err != 0) {
    combat_rng_trace_end_frame(batch);
    return err;
  }
  fighter_callbacks_iasa_phase(batch);
  fighter_callbacks_phys_phase(batch);
  fighter_callbacks_collision_phase(batch);
  fighter_callbacks_primitive_refresh_phase(batch, run_combat);
  fighter_callbacks_item_collision_and_combat_phase(batch, run_combat);
  fighter_callbacks_post_frame_phase(batch);
  combat_rng_trace_end_frame(batch);
  return 0;
}
