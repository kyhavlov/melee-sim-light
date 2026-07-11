#include "items_internal.h"

static uint8_t yoshi_shyguy_has_live(const MslBatch* batch, int bi,
                                     const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL) {
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] &&
        stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[ii])) {
      return 1u;
    }
  }
  return 0u;
}

static inline void yoshi_shyguy_clear_rollout_rng_owner_if_live(MslBatch* batch, int bi) {
  if (batch == NULL || batch->rollout_clock_rng_owned == NULL) {
    return;
  }
  if (batch->rollout_clock_rng_owned[bi] ==
      (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED_YOSHI_SHYGUY) {
    // `grStory_801E3418` returns immediately while any Heiho item is live. If a replay rollout
    // no-live scheduler owner reaches this state, ownership is over; later live-Heiho frames are
    // seed-owned again and must not keep advancing Slippi's frame RNG lane.
    // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    batch->rollout_clock_rng_owned[bi] = (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
    batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] = 0u;
  }
}

static inline void yoshi_shyguy_install_rollout_spawn_rng_seed(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  if (batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] == 0u) {
    // Autonomous no-live rollout windows can reach a later Shy Guy zero-timer callback after the
    // original replay seed row. When a broader replay-frame RNG owner is active, the callback must
    // sample the current Slippi frame-start stream, not the previous post-frame seed still stored
    // in public state at step start. Explicit spawn lanes above remain the stricter owner for
    // reseeds that already know the source spawn-frame seed.
    // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    combat_rng_use_next_replay_frame_seed_if_unconsumed(batch, bi);
    return;
  }
  // Replay-seeded countdowns do not expose unrelated global HSD consumers between frame starts.
  // The seed lane carries the source spawn-frame stream; install it immediately before
  // grStory_801E3418's zero-timer RNG consumers so combat_rng_consume_* samples the same stream.
  // This callback-local install can coexist with broader replay-frame RNG clock owners (opening
  // countdown, DamageFlyRoll phase, etc.); api.c suppresses only that frame's generic +0x10000
  // commit after this source callback has installed the spawn-frame seed.
  // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  const uint32_t seed = batch->state.stage_yoshi_shyguy_spawn_rng_seed[bi];
  batch->state.frame_pre_random_seed[bi] = seed;
  if (batch->rollout_yoshi_shyguy_spawn_rng_installed != NULL) {
    batch->rollout_yoshi_shyguy_spawn_rng_installed[bi] = 1u;
  }
  if (batch->rng_shadow_seed != NULL) {
    batch->rng_shadow_seed[bi] = seed;
  }
  if (batch->debug_rng_seed_in != NULL) {
    batch->debug_rng_seed_in[bi] = seed;
  }
  if (batch->debug_rng_seed_out != NULL) {
    batch->debug_rng_seed_out[bi] = seed;
  }
}

static inline void yoshi_shyguy_mark_rollout_rng_owner_consumed(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  if (batch->rollout_clock_rng_owned != NULL &&
      batch->rollout_clock_rng_owned[bi] ==
          (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED_YOSHI_SHYGUY) {
    // The zero-timer stage callback has consumed the spawn-frame HSD RNG stream. Slippi's
    // post-frame seed lane stays at that consumed frame-start value for this row, so clear the
    // owner before api.c's rollout-clock commit.
    // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    batch->rollout_clock_rng_owned[bi] = (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
  }
  batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] = 0u;
}

static float yoshi_shyguy_dyn_y_phase_value(const MslYoshiShyguyParams* params, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase <= MSL_YOSHI_SHYGUY_DYN_Y_COUNT) {
    return params->dyn_y_vel[phase - 1];
  }
  return params->dyn_y_vel[256 - phase];
}

static float yoshi_shyguy_dyn_y_pos_after_phase(const MslYoshiShyguyParams* params, int phase) {
  return params->dyn_y_pos_after_phase[phase & 0xFF];
}

static float yoshi_shyguy_dyn_y_rate2_value(const MslYoshiShyguyParams* params, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase == 1) {
    return yoshi_shyguy_dyn_y_phase_value(params, 1);
  }
  const int base = (2 * phase) - 2;
  return yoshi_shyguy_dyn_y_phase_value(params, base) +
         yoshi_shyguy_dyn_y_phase_value(params, base + 1);
}

static float yoshi_shyguy_dyn_y_pair_forward(const MslYoshiShyguyParams* params, int pair) {
  pair &= 0x3F;
  const int base = pair * 2;
  return params->dyn_y_vel[base] + params->dyn_y_vel[(base + 1) & 0x7F];
}

static uint8_t yoshi_shyguy_next_dynamic_vel_y(const MslYoshiShyguyParams* params, float prev_vel_y,
                                               float cur_vel_y, uint8_t rate2, float* out) {
  if (params == NULL || out == NULL || !isfinite(prev_vel_y) || !isfinite(cur_vel_y)) {
    return 0u;
  }
  const float eps = 0.001f;
  if (rate2 != 0u) {
    for (int i = 0; i < 256; i++) {
      const int prev_i = (i + 255) & 0xFF;
      if (fabsf(yoshi_shyguy_dyn_y_rate2_value(params, i) - cur_vel_y) <= eps &&
          fabsf(yoshi_shyguy_dyn_y_rate2_value(params, prev_i) - prev_vel_y) <= eps) {
        *out = yoshi_shyguy_dyn_y_rate2_value(params, (i + 1) & 0xFF);
        return 1u;
      }
    }
    return 0u;
  }
  for (int i = 0; i < 256; i++) {
    const int prev_i = (i + 255) & 0xFF;
    if (fabsf(yoshi_shyguy_dyn_y_phase_value(params, i) - cur_vel_y) <= eps &&
        fabsf(yoshi_shyguy_dyn_y_phase_value(params, prev_i) - prev_vel_y) <= eps) {
      *out = yoshi_shyguy_dyn_y_phase_value(params, (i + 1) & 0xFF);
      return 1u;
    }
  }
  return 0u;
}

static float yoshi_shyguy_vel_y_from_phase(const MslYoshiShyguyParams* params, uint8_t phase,
                                           uint8_t state) {
  if (state == 4u) {
    const uint8_t p = phase;
    if (p <= 29u) {
      // Return-flight can enter with the source x24 camera-turn delay set. During this prefix,
      // Slippi keeps x40_vel.y at zero even though `it_802D98C4` still contributes the first child
      // dynamic-bone delta to item position.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D9168,itHeiho_UnkMotion4_Phys}
      return yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    const uint8_t q = (uint8_t)(p - 18u);
    if (q == 139u) {
      // `HSD_AObjInterpretAnim` rewinds the looping child AObj before the next update. The visible
      // dynamic-bone delta is the reset-to-zero jump, not another rate-2 pair.
      // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
      // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
      return -yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q >= 140u) {
      return yoshi_shyguy_dyn_y_pair_forward(params, (int)(q - 140u));
    }
    return yoshi_shyguy_dyn_y_rate2_value(params, (int)(q - 10u));
  }
  return yoshi_shyguy_dyn_y_phase_value(params, ((int)phase + 1) & 0xFF);
}

static float yoshi_shyguy_current_vel_y_for_phase(const MslYoshiShyguyParams* params, uint8_t phase,
                                                  uint8_t state) {
  if (state == 4u) {
    const uint8_t p = phase;
    if (p <= 29u) {
      return 0.0f;
    }
    const uint8_t q = (uint8_t)(p - 18u);
    if (q == 12u) {
      return yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q == 140u) {
      return -yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q >= 141u) {
      return yoshi_shyguy_dyn_y_pair_forward(params, (int)(q - 141u));
    }
    return yoshi_shyguy_dyn_y_rate2_value(params, (int)(q - 11u));
  }
  return yoshi_shyguy_dyn_y_phase_value(params, phase);
}

static uint8_t yoshi_shyguy_state1_current_is_reset_export(const MslYoshiShyguyParams* params,
                                                           uint8_t phase, float prev_vel_y,
                                                           float cur_vel_y) {
  if (params == NULL) {
    return 0u;
  }
  const uint8_t prev_phase = (uint8_t)((phase + 255u) & 0xFFu);
  const float expected_prev = yoshi_shyguy_dyn_y_phase_value(params, prev_phase);
  const float reset_export = -yoshi_shyguy_dyn_y_pos_after_phase(params, phase);
  return (uint8_t)(fabsf(expected_prev - prev_vel_y) <= 0.001f &&
                   fabsf(reset_export - cur_vel_y) <= 0.001f);
}

static inline uint8_t yoshi_shyguy_falling_state_crossed_generic_blast_clear(
    const MslStageBounds* blast_bounds, float x, float y) {
  if (blast_bounds == NULL) {
    return 0u;
  }
  // Knocked/falling Heiho states run the generic item post-Phys destroy gate after position
  // integration. State 2/3 set xDCC_flag.b3 through it_802D8EC8; Item_802696CC then destroys on
  // the enabled side/bottom blast bounds (top uses the generic 10000.0 sentinel and is irrelevant
  // for this stage item).
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion2_Phys,
  //   itHeiho_UnkMotion3_Phys}
  // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
  return (x > blast_bounds->right || x < blast_bounds->left || y < blast_bounds->bottom) ? 1u : 0u;
}

static uint8_t yoshi_shyguy_active_fixed_ecb_wall_contact(const MslYoshiShyguyParams* params,
                                                          uint32_t stage_id, float facing_dir,
                                                          float prev_pos_x, float prev_pos_y,
                                                          float pos_x, float pos_y) {
  if (params == NULL) {
    return 0u;
  }
  const float scale = params->collision_ecb_scale;
  const float ecb_top = params->collision_ecb_up * scale;
  const float ecb_bottom = -params->collision_ecb_down * scale;
  const float ecb_right = params->collision_ecb_right * scale;
  const float ecb_left = -params->collision_ecb_left * scale;
  const int wall_side = (facing_dir < 0.0f) ? 1 : 0;
  // Active and return-flight Heiho call `it_8026DA70`, then `it_80276308` tests wall contact
  // against the item CollData fixed ECB installed from Article ItemAttr.x40 by `it_80275DFC`.
  // The queried wall graph is the wall ahead of the current facing direction.
  // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll}
  // refs/melee/src/melee/it/itgroundcoll.c::it_8026DA70
  // refs/melee/src/melee/it/it_2725.c::{it_80275DFC,it_80276308}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_SetECBSource_Fixed
  return stage_collision_item_fixed_ecb_sweep_hits_wall(stage_id, wall_side, prev_pos_x, prev_pos_y,
                                                        pos_x, pos_y, ecb_left, ecb_right,
                                                        ecb_bottom, ecb_top);
}

static uint8_t yoshi_shyguy_active_fixed_ecb_floor_contact(const MslYoshiShyguyParams* params,
                                                           uint32_t stage_id, float prev_pos_x,
                                                           float prev_pos_y, float pos_x,
                                                           float pos_y) {
  if (params == NULL) {
    return 0u;
  }
  const float scale = params->collision_ecb_scale;
  const float ecb_bottom = -params->collision_ecb_down * scale;
  const float ecb_right = params->collision_ecb_right * scale;
  const float ecb_left = -params->collision_ecb_left * scale;
  // Active and return-flight Heiho call `it_8026DA70`; when the fixed-ECB mpColl pass reports
  // contact but the wall-turn branch does not run, state 1 restarts its active animation and state
  // 4 re-enters return flight. `it_8026DA70` does not copy CollData.cur_pos to Item.pos, so this
  // helper only owns the boolean animation-reset branch.
  // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll}
  // refs/melee/src/melee/it/itgroundcoll.c::it_8026DA70
  return stage_collision_item_fixed_ecb_sweep_hits_floor(stage_id, prev_pos_x, prev_pos_y, pos_x,
                                                         pos_y, ecb_left, ecb_right, ecb_bottom);
}

static int yoshi_shyguy_spawn_count(MslBatch* batch, int bi) {
  // Source calls set_shyguy_spawn_count twice; the second call overwrites the first, but both RNG
  // streams are consumed.
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
  int unused_count = 1;
  if (combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8, 8) == 0) {
    unused_count =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8_BONUS, 3) +
        3;
  }
  (void)unused_count;
  if (combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2, 2) == 0) {
    const int count =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2_BONUS, 3) +
        3;
    // Source count owner: the second `set_shyguy_spawn_count(gp, 2)` overwrites the earlier
    // rarity-8 result and can spawn 3..5 Shy Guys from this bonus sample.
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    return count;
  }
  return 1;
}

static void yoshi_shyguy_spawn_one(MslBatch* batch, int bi, int arg0, float pos_x, float pos_y,
                                   uint8_t speed_index, const MslYoshiShyguyParams* params) {
  if (params == NULL) {
    return;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  batch->state.item_exists[ii] = 1u;
  batch->state.item_state[ii] = 0u;
  batch->state.item_type[ii] = params->item_kind;
  batch->state.item_owner[ii] = -1;
  batch->state.item_instance_id[ii] = 0u;
  batch->state.item_attack_id[ii] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_attack_instance[ii] = 0u;
  batch->state.item_direction[ii] = (pos_x < 0.0f) ? 1.0f : -1.0f;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_pos_x[ii] = pos_x;
  batch->state.item_pos_y[ii] = pos_y;
  batch->state.item_damage[ii] = 0u;
  msl_item_reflect_clear_all_lanes(batch, ii);
  batch->state.item_timer[ii] = 1400.0f;
  batch->state.item_hitlag[ii] = 0u;
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_shyguy_speed_index[ii] = speed_index % 3u;
  batch->state.item_shyguy_speed_index_valid[ii] = 1u;
  batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
  batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
  batch->state.item_shyguy_delay[ii] = (uint16_t)(params->spawn_delay_step * arg0);
  batch->state.item_shyguy_delay_valid[ii] = 1u;
  batch->state.item_shyguy_hitlag[ii] = 0u;
  batch->state.item_shyguy_hitlag_valid[ii] = 1u;
}

void yoshi_shyguy_stage_update(MslBatch* batch, int bi) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      batch->state.stage_yoshi_shyguy_valid[bi] == 0u || params == NULL) {
    return;
  }
  if (yoshi_shyguy_has_live(batch, bi, params) != 0u) {
    yoshi_shyguy_clear_rollout_rng_owner_if_live(batch, bi);
    return;
  }
  if (batch->state.stage_yoshi_shyguy_timer[bi] != 0u) {
    batch->state.stage_yoshi_shyguy_timer[bi]--;
    return;
  }

  yoshi_shyguy_install_rollout_spawn_rng_seed(batch, bi);

  // `reset_shyguy_timer` first samples timer_min + HSD_Randi(timer_rand), then immediately
  // overwrites the timer with 120. The sampled value is discarded but the RNG consumer is real and
  // phase-orders the following pattern/speed/count/jitter samples.
  // refs/melee/src/melee/gr/grstory.c::{reset_shyguy_timer,grStory_801E3418}
  (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_TIMER_WAIT,
                                      (int32_t)params->timer_rand);
  batch->state.stage_yoshi_shyguy_timer[bi] = params->timer_reset;
  uint8_t pattern = batch->state.stage_yoshi_shyguy_pattern[bi] % MSL_YOSHI_SHYGUY_VPOS_COUNT;
  uint8_t next_pattern = pattern;
  do {
    next_pattern = (uint8_t)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_PATTERN, MSL_YOSHI_SHYGUY_VPOS_COUNT);
  } while (next_pattern == pattern);
  batch->state.stage_yoshi_shyguy_pattern[bi] = next_pattern;

  const float base_y = params->vpos[next_pattern];
  const float pos_x = (next_pattern < 3u) ? params->spawn_left_x : params->spawn_right_x;
  const uint8_t speed_index =
      (uint8_t)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_SPEED_INDEX, 3);
  const int count = yoshi_shyguy_spawn_count(batch, bi);
  float pos_y = base_y;
  for (int i = 0; i < count; i++) {
    yoshi_shyguy_spawn_one(batch, bi, i, pos_x, pos_y, speed_index, params);
    const float randf = combat_rng_consume_randf_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_JITTER);
    pos_y = (params->jitter_y_amp * ((2.0f * randf) - 1.0f)) + base_y;
  }
  items_sort(batch, bi);
  yoshi_shyguy_mark_rollout_rng_owner_consumed(batch, bi);
}

static inline uint8_t yoshi_shyguy_state_accepts_item_damage(uint8_t state) {
  // Heiho active and return-flight states keep enabled item hurtboxes. Knocked states 2/3 are
  // already in the damage callback lifecycle and state 0 is spawn-delay/hidden motion.
  // refs/melee/src/melee/it/items/itheiho.c::it_803F83F0
  // refs/melee/src/melee/it/itcoll.c::it_8027163C
  return (state == 1u || state == 4u) ? 1u : 0u;
}

static inline uint8_t yoshi_shyguy_seed_return_flight_same_action_rehit_suppresses(
    const MslBatch* batch, size_t p_idx, size_t shy_idx, size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.item_state[shy_idx] != 4u || batch->state.item_damage[shy_idx] == 0u) {
    return 0u;
  }
  if (batch->state.hitlag[p_idx] != 0u || batch->state.hitstun[p_idx] != 0u) {
    return 0u;
  }
  if (batch->state.hitbox_enable_edge[hb_i] != 0u) {
    return 0u;
  }
  // Teacher-forced reseed can restore Heiho's low-damage return-flight state without serializing
  // the fighter HitCapsule's item victim pointer. If the same fighter action is continuing, source
  // ftColl_80076808's victims_1 latch from the prior it_802703E8 hit still suppresses continuing
  // same-group HitCapsules from damaging the same item again. A hitbox enable edge is a new
  // source-created HitCapsule, so same action/state4/damaged-item alone is not enough to infer the
  // victims_1 entry. This bridge only materializes the missing item victim lane; all suppression
  // still flows through hitlist_allows_fighter_item below.
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076808
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168}
  return (batch->state.seed_prev_action_id[p_idx] == batch->state.action_id[p_idx]) ? 1u : 0u;
}

static inline float yoshi_shyguy_fighter_hitcapsule_damage(const MslBatch* batch, size_t p_idx,
                                                           size_t hb_i, uint8_t item_state) {
  if (batch == NULL) {
    return 0.0f;
  }
  // Fighter HitCapsule -> item hurtbox damage consumes HitCapsule.damage. Prefer the explicit
  // frozen HitCapsule lane. Active Heiho state-1 contact is still in the source item-collision
  // callback that sees the current HitCapsule's stale-scaled damage, so use the live stale queue
  // when no frozen lane exists. Knocked/return carry lanes preserve the previous source packet and
  // exclude the same continuing attack instance while older instances of the same move stale
  // normally.
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
  // refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
  float damage = batch->state.hitbox_damage[hb_i];
  float stale_mult = 1.0f;
  if (batch->state.hitbox_stale_damage_valid[hb_i] != 0u &&
      batch->state.hitbox_stale_damage_mul[hb_i] > 0.0f) {
    stale_mult = batch->state.hitbox_stale_damage_mul[hb_i];
  } else if (item_state == 1u) {
    const uint16_t move_id = staling_move_id_from_state(batch, p_idx);
    stale_mult = staling_multiplier_for_move(batch, p_idx, move_id);
  } else {
    const uint16_t move_id = staling_move_id_from_state(batch, p_idx);
    const uint16_t attack_instance = batch->state.attack_instance[p_idx];
    stale_mult =
        staling_multiplier_for_move_excluding_instance(batch, p_idx, move_id, attack_instance);
  }
  if (stale_mult != 1.0f) {
    damage *= stale_mult;
  }
  return damage;
}

typedef struct MslYoshiShyguyKnockedSource {
  int attacker;
  size_t attacker_idx;
  size_t hitbox_idx;
  uint8_t hitbox_id;
  uint8_t hit_group;
  uint8_t rehit_frames;
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
} MslYoshiShyguyKnockedSource;

static uint8_t yoshi_shyguy_select_knocked_state_source_hitbox(MslBatch* batch, int bi,
                                                               int shyguy_slot,
                                                               const MslYoshiShyguyParams* params,
                                                               MslYoshiShyguyKnockedSource* out) {
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
    out->attacker = -1;
  }
  if (batch == NULL || params == NULL || out == NULL || params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (batch->state.item_exists[shy_idx] == 0u ||
      !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
      batch->state.item_state[shy_idx] != 3u || batch->state.item_damage[shy_idx] == 0u ||
      batch->state.item_shyguy_hitlag_valid[shy_idx] == 0u ||
      batch->state.item_shyguy_hitlag[shy_idx] == 0u) {
    return 0u;
  }

  for (int p = 0; p < (int)batch->config.num_players; p++) {
    const size_t p_idx = msl_idx_player(bi, p);
    if (batch->state.hitlag[p_idx] == 0u) {
      continue;
    }
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const size_t hb_i = idx_hitbox(bi, p, hb);
      if (!batch->state.hitbox_enabled[hb_i]) {
        continue;
      }
      const uint16_t flags = batch->state.hitbox_flags[hb_i];
      // refs/melee/src/melee/it/itcoll.c::it_8026D564 (fighter HitCapsule x42_b7 item gate)
      if (!msl_hitbox_x42_b7_enabled(flags) ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
        continue;
      }
      if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT ||
          !(batch->state.hitbox_damage[hb_i] > 0.0f)) {
        continue;
      }
      float hx1 = batch->state.hitbox_x[hb_i];
      float hy1 = batch->state.hitbox_y[hb_i];
      float hz1 = batch->state.hitbox_z[hb_i];
      float hx0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
      float hy0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
      float hz0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
      const float hr = batch->state.hitbox_radius[hb_i];
      for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
        const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
        const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
        const float az = params->hurtbox_a_offset[hi][2];
        const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
        const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
        const float bz = params->hurtbox_b_offset[hi][2];
        const float rr = hr + params->hurtbox_scale[hi] * params->collision_ecb_scale;
        const float d2 =
            item_segment_segment_dist2(hx0, hy0, hz0, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
        if (d2 > rr * rr) {
          continue;
        }

        out->attacker = p;
        out->attacker_idx = p_idx;
        out->hitbox_idx = hb_i;
        out->hitbox_id = (uint8_t)hb;
        out->hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        out->rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        out->damage = yoshi_shyguy_fighter_hitcapsule_damage(batch, p_idx, hb_i,
                                                             batch->state.item_state[shy_idx]);
        out->angle = batch->state.hitbox_angle[hb_i];
        out->kbg = batch->state.hitbox_kbg[hb_i];
        out->wsk = batch->state.hitbox_wsk[hb_i];
        out->bkb = batch->state.hitbox_bkb[hb_i];
        out->element = batch->state.hitbox_element[hb_i];
        return 1u;
      }
    }
  }
  return 0u;
}

static uint8_t yoshi_shyguy_try_knocked_state_fighter_body_hit(MslBatch* batch, int bi,
                                                               int shyguy_slot,
                                                               const MslYoshiShyguyParams* params) {
  MslYoshiShyguyKnockedSource src;
  if (!yoshi_shyguy_select_knocked_state_source_hitbox(batch, bi, shyguy_slot, params, &src)) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (!(src.damage > 0.0f) || !batch->state.hitbox_enabled[src.hitbox_idx]) {
    return 0u;
  }
  const float hx = batch->state.hitbox_x[src.hitbox_idx];
  const float hy = batch->state.hitbox_y[src.hitbox_idx];
  const float hz = batch->state.hitbox_z[src.hitbox_idx];
  const float hr = batch->state.hitbox_radius[src.hitbox_idx];
  const float px = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_x[src.hitbox_idx]
                       : hx;
  const float py = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_y[src.hitbox_idx]
                       : hy;
  const float pz = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_z[src.hitbox_idx]
                       : hz;
  const uint16_t src_flags = batch->state.hitbox_flags[src.hitbox_idx];

  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == src.attacker) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    if (batch->state.hitlag[d_idx] != 0u ||
        batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED ||
        !hitlist_allows_fighter(batch, bi, src.attacker, src.hitbox_id, def,
                                batch->state.instance_id[d_idx])) {
      continue;
    }
    if (batch->state.on_ground[d_idx]) {
      if ((src_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
        continue;
      }
    } else if ((src_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
      continue;
    }
    uint8_t hurt_height = 1u;
    uint8_t body_contact = 0u;
    for (int cap = 0; cap < MSL_MAX_HURTCAPS; cap++) {
      if (cap >= (int)batch->state.hurtcap_count[d_idx]) {
        break;
      }
      const size_t cap_i = idx_hurtcap(bi, def, cap);
      if (!batch->state.hurtcap_enabled[cap_i]) {
        continue;
      }
      float d2 = 0.0f;
      combat_segment_segment_dist2(px, py, pz, hx, hy, hz, batch->state.hurtcap_a_x[cap_i],
                                   batch->state.hurtcap_a_y[cap_i], batch->state.hurtcap_a_z[cap_i],
                                   batch->state.hurtcap_b_x[cap_i], batch->state.hurtcap_b_y[cap_i],
                                   batch->state.hurtcap_b_z[cap_i], &d2, NULL, NULL);
      const float rr = hr + batch->state.hurtcap_radius[cap_i];
      if (d2 <= rr * rr) {
        hurt_height = batch->state.hurtcap_height[cap_i];
        body_contact = 1u;
        break;
      }
    }
    if (body_contact == 0u) {
      continue;
    }

    // Knocked Heiho state-3 BODY source:
    // - A fighter HitCapsule hit against the item hurtbox records the source fighter in
    //   item->xCB0/xCEC and item damage provenance in it_80270E30.
    // - While the low-damage state-3 item is still in source hitlag, later fighter BODY contact
    //   uses that carried source owner, not the stage/environment sentinel.
    // - Slippi exposes the carried owner through the victim's last_hit_by/instance_hit_by and the
    //   source fighter's active hitlag, but not the item xCB0/xCEC fields themselves.
    // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_80270E30}
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion3_Phys}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_80076ED8}
    const MslItemHitResult res = combat_apply_item_hit(
        batch, bi, src.attacker, def, batch->state.attack_id[src.attacker_idx],
        batch->state.attack_instance[src.attacker_idx], batch->state.instance_id[src.attacker_idx],
        batch->state.item_type[shy_idx], batch->state.item_state[shy_idx], src.damage, src.angle,
        src.kbg, src.wsk, src.bkb, hurt_height, src.element, 1.0f,
        // Knocked Shy Guy Counter geometry: the contact capsule is the carried source hitbox
        // (hx/hy with radius hr, the same sweep tested against the defender's hurtcaps above),
        // so the descriptor sphere sees the real contact position/radius.
        hx, hy, hr, batch->state.item_vel_x[shy_idx], 1u);
    if (res == MSL_ITEM_HIT_NONE) {
      continue;
    }
    const int damage_i = (int)src.damage;
    combat_apply_deal_hitlag_raw_damage(batch, src.attacker_idx, damage_i);
    hitlist_register_fighter_group(batch, bi, src.attacker, src.hit_group, def,
                                   batch->state.instance_id[d_idx], (int)MSL_LBCOLL_INSERT_FT_BODY,
                                   src.rehit_frames);
    return 1u;
  }
  return 0u;
}

static inline void yoshi_shyguy_seed_return_flight_publish_fresh_prefix_velocity(
    MslBatch* batch, size_t shy_idx, const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL || batch->state.item_state[shy_idx] != 4u ||
      batch->state.item_shyguy_delay_valid[shy_idx] == 0u ||
      batch->state.item_shyguy_delay[shy_idx] < 18u) {
    return;
  }
  // Fresh return-flight prefix after the low-damage callback: it_802D9168 initializes x24=20 and
  // state-4 Anim/Phys publish the source speed/dynamic-bone velocity for the immediate carried
  // item-hitlist rows. Later x24 rows stay on the broader return-flight export owner below.
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168,
  //   itHeiho_UnkMotion4_Anim,itHeiho_UnkMotion4_Phys}
  if (batch->state.item_shyguy_speed_index_valid[shy_idx] != 0u) {
    const uint8_t speed_index =
        batch->state.item_shyguy_speed_index[shy_idx] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
    batch->state.item_vel_x[shy_idx] = params->speed[speed_index] * params->state4_speed_mul *
                                       batch->state.item_direction[shy_idx];
  }
  if (batch->state.item_shyguy_dyn_y_phase_valid[shy_idx] != 0u) {
    const uint8_t phase_before =
        (uint8_t)((batch->state.item_shyguy_dyn_y_phase[shy_idx] + 255u) & 0xFFu);
    batch->state.item_vel_y[shy_idx] = yoshi_shyguy_vel_y_from_phase(params, phase_before, 4u);
  }
}

static inline float yoshi_shyguy_item_kb_applied(const MslCommonParams* c,
                                                 const MslYoshiShyguyParams* params, uint16_t kbg,
                                                 uint16_t wsk, uint16_t bkb, int damage_i) {
  if (c == NULL || params == NULL) {
    return 0.0f;
  }
  // Item-vs-item damage intake uses the ItemCommonData knockback expression in it_80270E30. The
  // constants share the same promoted ftColl KB lanes here; Heiho contributes its Article
  // ItemAttr.x1C damage multiplier extracted into MSLSTIO1.
  // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
  // refs/melee/src/melee/it/types.h::ItemAttr::x1C_damage_mul
  const float kbg_scale = 0.01f * (float)kbg;
  const float bkb_f = (float)bkb;
  float kb = 0.0f;
  if (wsk != 0u) {
    const float pre = params->damage_mul * ((c->kb_base_term * c->kb_wsk_mul) +
                                            (c->kb_dmg_mul * (c->kb_wsk_mul * (float)wsk)));
    const float inner = c->kb_growth_mul * pre + c->kb_base_add;
    kb = bkb_f + kbg_scale * inner;
  } else {
    const float d = (float)damage_i;
    const float pre = params->damage_mul * ((c->kb_base_term * d) + (c->kb_dmg_mul * d * d));
    const float inner = c->kb_growth_mul * pre + c->kb_base_add;
    kb = bkb_f + kbg_scale * inner;
  }
  if (kb > c->kb_applied_max) {
    kb = c->kb_applied_max;
  }
  return kb;
}

static inline void yoshi_shyguy_apply_damage_common(MslBatch* batch, size_t shy_idx,
                                                    const MslYoshiShyguyParams* params,
                                                    uint16_t angle, uint16_t kbg, uint16_t wsk,
                                                    uint16_t bkb, float damage_f,
                                                    float hitlag_damage_f, float dir) {
  const MslCommonParams* c = msl_common_params();
  int damage_i = (int)damage_f;
  if (damage_i < 0) {
    damage_i = 0;
  }
  uint16_t damage_total = batch->state.item_damage[shy_idx];
  if (damage_total <= (uint16_t)(999u - (uint16_t)damage_i)) {
    damage_total = (uint16_t)(damage_total + (uint16_t)damage_i);
  } else {
    damage_total = 999u;
  }
  batch->state.item_damage[shy_idx] = damage_total;

  const float kb = yoshi_shyguy_item_kb_applied(c, params, kbg, wsk, bkb, damage_i);
  const float kb_vel_mag = (c != NULL) ? (kb * c->kb_vel_mul) : 0.0f;
  const float angle_rad = (angle == 361u && c != NULL) ? c->sakurai_air_radians
                                                       : (0.01745329251994329577f * (float)angle);
  const float vx = kb_vel_mag * cosf(angle_rad);
  const float vy = kb_vel_mag * sinf(angle_rad);

  if (params->damage_threshold != 0u &&
      damage_total > (uint16_t)((float)params->damage_threshold * 0.8f)) {
    // High-damage branch enters state 2 and adds random X scatter. The global RNG stream for this
    // branch is not yet owned, so do not fabricate the scatter velocity here.
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D8EC8_inline}
    batch->state.item_state[shy_idx] = 2u;
  } else {
    // Low-damage branch: it_8027B798 writes KB velocity, x24 becomes 12, and state 3 is entered.
    // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
    // refs/melee/src/melee/it/it_2725.c::it_8027B798
    batch->state.item_state[shy_idx] = 3u;
    batch->state.item_vel_x[shy_idx] = -vx * dir;
    batch->state.item_vel_y[shy_idx] = vy;
    batch->state.item_direction[shy_idx] = dir;
    batch->state.item_shyguy_delay[shy_idx] = 12u;
    batch->state.item_shyguy_delay_valid[shy_idx] = 1u;
  }

  const MslItemCommonParams* item_common = msl_item_common_params();
  const float item_hitlag =
      (item_common != NULL)
          ? (item_common->item_hitlag_base + item_common->item_hitlag_damage_mul * hitlag_damage_f)
          : 0.0f;
  if (item_hitlag > 0.0f) {
    const uint8_t frames = (uint8_t)item_hitlag;
    if (frames > 1u) {
      // Heiho damage callback stores item hitlag, then the source item loop's hitlag/timer pass
      // exposes the post-decrement value on the next replay row. Keep live runtime aligned with
      // the native seed lane so knocked Shy Guys resume their state-3 physics on the same frame as
      // vanilla instead of stale-freezing one extra step.
      // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_802697D4}
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion3_Phys}
      const uint8_t visible_frames = (uint8_t)(frames - 1u);
      batch->state.item_hitlag[shy_idx] = visible_frames;
      batch->state.item_shyguy_hitlag[shy_idx] = visible_frames;
      batch->state.item_shyguy_hitlag_valid[shy_idx] = 1u;
    }
  }
  batch->state.item_shyguy_prev_vel_y_valid[shy_idx] = 0u;
  batch->state.item_shyguy_dyn_y_phase_valid[shy_idx] = 0u;
}

static inline void yoshi_shyguy_apply_item_damage(MslBatch* batch, size_t shy_idx, size_t laser_idx,
                                                  const MslYoshiShyguyParams* params,
                                                  const MslLaserParams* lp, uint8_t laser_state,
                                                  float damage_f) {
  // Caller supplies the item-owned reflected damage lane before Heiho dmg_received consumes it.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
  // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
  const uint16_t angle = (laser_state == 0u) ? lp->angle : lp->state1_angle;
  const uint16_t kbg = (laser_state == 0u) ? lp->kbg : lp->state1_kbg;
  const uint16_t wsk = (laser_state == 0u) ? lp->wsk : lp->state1_wsk;
  const uint16_t bkb = (laser_state == 0u) ? lp->bkb : lp->state1_bkb;
  const float laser_vx = batch->state.item_vel_x[laser_idx];
  const float dir =
      (fabsf(laser_vx) < 0.0001f)
          ? ((batch->state.item_pos_x[shy_idx] > batch->state.item_pos_x[laser_idx]) ? -1.0f : 1.0f)
          : ((laser_vx < 0.0f) ? 1.0f : -1.0f);
  yoshi_shyguy_apply_damage_common(batch, shy_idx, params, angle, kbg, wsk, bkb, damage_f, damage_f,
                                   dir);
}

static int yoshi_shyguy_laser_item_hit_deferred_to_throw_fighter_body(const MslBatch* batch, int bi,
                                                                      size_t laser_idx,
                                                                      const MslLaserParams* lp,
                                                                      uint8_t laser_state,
                                                                      float vx) {
  if (batch == NULL || lp == NULL || laser_state == 0u ||
      !item_type_is_fox_laser(batch->state.item_type[laser_idx])) {
    return -1;
  }
  const int owner = (int)batch->state.item_owner[laser_idx];
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return -1;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_HI) {
    return -1;
  }
  int16_t first_pulse_af = 0;
  int16_t last_pulse_af = 0;
  if (!move_tables_throw_projectile_first_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &first_pulse_af) ||
      !move_tables_throw_projectile_last_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &last_pulse_af) ||
      batch->state.throw_pulse_crossed_prev_frame[o_idx] != (uint8_t)last_pulse_af) {
    return -1;
  }

  int candidate = -1;
  const int num_players = (int)batch->config.num_players;
  for (int vp = 0; vp < num_players; vp++) {
    if (vp == owner) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, vp);
    if (batch->state.hitlag[v_idx] != 0u || batch->state.hitstun[v_idx] == 0u ||
        (!items_action_is_damage_family(batch->state.action_id[v_idx]) &&
         !msl_damage_owner_is_damagefly_action(batch->state.action_id[v_idx])) ||
        !msl_damage_source_victim_port_matches_attacker(batch, v_idx, o_idx, owner) ||
        batch->state.last_attack_landed[v_idx] != (uint8_t)first_pulse_af ||
        ((batch->state.pos_x[v_idx] - batch->state.pos_x[o_idx]) * vx) > 0.0f) {
      continue;
    }
    if (candidate >= 0) {
      return -1;
    }
    candidate = vp;
  }
  if (candidate < 0) {
    return -1;
  }

  // Fox ThrowHi final-pulse fighter callback before Shy Guy item contact:
  // - The final ThrowHi set_throw_spawn_projectile pulse is source-owned by the extracted script
  //   (18/20/24). ftFx_Throw_Anim spawns the state1 Fox laser through it_8029C6CC.
  // - Fighter_8006CB94 / ftColl_8007925C owns item-vs-fighter contact before the laser item
  //   collision callback reaches stage-item contact. In the final-pulse carry frame, the unique
  //   victim is already in same-source DamageFly hitstun from the first pulse and remains on the
  //   non-projectile side; Shy Guy item contact must not clear the shot before the next BODY
  //   callback consumes it.
  // - This is not a replay row key: it requires extracted ThrowHi pulse order, Fox shot data,
  //   source damage provenance, first-pulse item attack id, and unique current victim ownership.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
  // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
  // data/moves/fox.json moves["ftCo_SM_ThrowHi"].events
  return candidate;
}

uint8_t yoshi_shyguy_try_laser_item_hit(MslBatch* batch, int bi, int laser_slot,
                                        const MslLaserParams* lp, uint8_t laser_state, float x0,
                                        float y0, float x, float y, float ux, float uy, float sr,
                                        float laser_prev_scale_z, float laser_scale_z) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || lp == NULL || params == NULL ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t laser_idx = msl_idx_item(bi, laser_slot);
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  const float cur_offset_scale = laser_collision_offset_scale(
      lp, laser_state, laser_scale_z, MSL_LASER_COLLISION_SPACE_BODY, 0u, 1u);
  const float prev_offset_scale = laser_collision_offset_scale(
      lp, laser_state, laser_prev_scale_z, MSL_LASER_COLLISION_SPACE_BODY, 0u, 1u);
  const int deferred_fighter_body_victim =
      yoshi_shyguy_laser_item_hit_deferred_to_throw_fighter_body(
          batch, bi, laser_idx, lp, laser_state, batch->state.item_vel_x[laser_idx]);
  if (deferred_fighter_body_victim >= 0) {
    batch->state.item_hidden_body_hit_victim_port[laser_idx] =
        (uint8_t)deferred_fighter_body_victim;
    batch->state.item_hidden_body_hit_hurt_height[laser_idx] = 1u;
    batch->state.item_hidden_callback_flags[laser_idx] = (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_CLEAR;
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    if (it == laser_slot) {
      continue;
    }
    const size_t shy_idx = msl_idx_item(bi, it);
    if (!batch->state.item_exists[shy_idx] ||
        !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
        !yoshi_shyguy_state_accepts_item_damage(batch->state.item_state[shy_idx])) {
      continue;
    }
    for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
      const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
      const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
      const float az = params->hurtbox_a_offset[hi][2];
      const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
      const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
      const float bz = params->hurtbox_b_offset[hi][2];
      const float rr = sr + params->hurtbox_scale[hi];
      if (off_n == 0u) {
        const float d2 =
            item_segment_segment_dist2(x0, y0, 0.0f, x, y, 0.0f, ax, ay, az, bx, by, bz);
        if (d2 <= rr * rr) {
          const float base_damage = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          const float damage = msl_item_reflect_damage_lane(batch, laser_idx, base_damage);
          yoshi_shyguy_apply_item_damage(batch, shy_idx, laser_idx, params, lp, laser_state,
                                         damage);
          item_slot_clear(batch, laser_idx);
          return 1u;
        }
        continue;
      }
      for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
        const float off_x =
            (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
        const float sx0 = x0 + ux * off_x * prev_offset_scale;
        const float sy0 = y0 + uy * off_x * prev_offset_scale;
        const float sx = x + ux * off_x * cur_offset_scale;
        const float sy = y + uy * off_x * cur_offset_scale;
        const float d2 =
            item_segment_segment_dist2(sx0, sy0, 0.0f, sx, sy, 0.0f, ax, ay, az, bx, by, bz);
        if (d2 <= rr * rr) {
          // Source owner: item-vs-item hitbox against Heiho item hurtbox, then Heiho
          // OnTakeDamageThink/dmg_received callback.
          // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
          // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
          const float base_damage = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          const float damage = msl_item_reflect_damage_lane(batch, laser_idx, base_damage);
          yoshi_shyguy_apply_item_damage(batch, shy_idx, laser_idx, params, lp, laser_state,
                                         damage);
          item_slot_clear(batch, laser_idx);
          return 1u;
        }
      }
    }
  }
  return 0u;
}

static uint8_t yoshi_shyguy_try_fighter_hitbox_hit(MslBatch* batch, int bi, int shyguy_slot,
                                                   const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (!batch->state.item_exists[shy_idx] ||
      !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
      !yoshi_shyguy_state_accepts_item_damage(batch->state.item_state[shy_idx])) {
    return 0u;
  }

  typedef struct MslShyguyFighterHit {
    uint16_t angle;
    uint16_t kbg;
    uint16_t wsk;
    uint16_t bkb;
    float dir;
  } MslShyguyFighterHit;
  MslShyguyFighterHit damage_hits[MSL_MAX_PLAYERS * MSL_MAX_HITBOXES] = {{0}};
  uint8_t damage_hit_count = 0u;
  float damage_total = 0.0f;
  float max_damage = 0.0f;
  uint8_t any_hit = 0u;

  for (int p = 0; p < (int)batch->config.num_players; p++) {
    const size_t p_idx = msl_idx_player(bi, p);
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const size_t hb_i = idx_hitbox(bi, p, hb);
      if (!batch->state.hitbox_enabled[hb_i]) {
        continue;
      }
      const uint16_t flags = batch->state.hitbox_flags[hb_i];
      // refs/melee/src/melee/it/itcoll.c::it_8026D564 (fighter HitCapsule x42_b7 item gate)
      if (!msl_hitbox_x42_b7_enabled(flags) ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
        continue;
      }
      const uint8_t is_inert = batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT;
      if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          (!is_inert && !(batch->state.hitbox_damage[hb_i] > 0.0f))) {
        continue;
      }
      uint8_t hit_group = 0u;
      uint8_t rehit_frames = 0u;
      if (!is_inert) {
        hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        const uint8_t seed_rehit_suppresses =
            yoshi_shyguy_seed_return_flight_same_action_rehit_suppresses(batch, p_idx, shy_idx,
                                                                         hb_i);
        if (seed_rehit_suppresses) {
          yoshi_shyguy_seed_return_flight_publish_fresh_prefix_velocity(batch, shy_idx, params);
          hitlist_register_fighter_group_item(batch, bi, p, hit_group, shyguy_slot,
                                              batch->state.item_spawn_id[shy_idx],
                                              (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
        }
        if (!hitlist_allows_fighter_item(batch, bi, p, hb, shyguy_slot,
                                         batch->state.item_spawn_id[shy_idx])) {
          continue;
        }
      }

      const float hx1 = batch->state.hitbox_x[hb_i];
      const float hy1 = batch->state.hitbox_y[hb_i];
      const float hz1 = batch->state.hitbox_z[hb_i];
      const float hx0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
      const float hy0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
      const float hz0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
      const float hr = batch->state.hitbox_radius[hb_i];
      for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
        const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
        const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
        const float az = params->hurtbox_a_offset[hi][2];
        const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
        const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
        const float bz = params->hurtbox_b_offset[hi][2];
        // Heiho item hurtboxes are tested through lbColl_8000805C with the item object's `scl`
        // scale. MSLSTIO1 exposes that source scale as collision_ecb_scale; using the raw hurtbox
        // radius over-admits near-miss fighter/item contacts at the Shy Guy edge.
        // refs/melee/src/melee/it/itcoll.c::it_802703E8
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        // data/stage_items/yoshi_shyguy.bin::MSLSTIO1 collision_ecb_scale
        const float rr = hr + params->hurtbox_scale[hi] * params->collision_ecb_scale;
        const float d2 =
            item_segment_segment_dist2(hx0, hy0, hz0, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
        if (d2 > rr * rr) {
          continue;
        }

        if (is_inert) {
          // it_802703E8's HitElement_Inert branch writes only fighter->unk_gobj: no item damage,
          // hitlag, or victims_1 registration. Falcon's hurtbox_detect_cb then admits the source
          // ItemKind families encoded in MSLITAR1.
          // refs/melee/src/melee/it/itcoll.c::it_802703E8
          // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialS_OnDetect
          if (falcon_specials_item_kind_eligible(batch->state.item_type[shy_idx])) {
            falcon_specials_on_inert_body_contact(batch, p_idx);
            any_hit = 1u;
            // it_802703E8 breaks only this item-hurtbox scan. The outer fighter/HitCapsule
            // traversal continues, so a later ordinary capsule can still damage the same item.
            // refs/melee/src/melee/it/itcoll.c::it_802703E8
            break;
          }
          continue;
        }

        // Fighter-hitbox vs item-hurtbox source path:
        // `it_802703E8` tests fighter HitCapsules with x42_b7 item interaction against enabled
        // item hurtboxes via `lbColl_8000805C`, records damage with `it_8026F9AC(type=1)`, then
        // `it_80270E30` publishes incoming direction before Heiho's dmg_received callback enters
        // state 2/3 through `it_802D8EC8`.
        // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026F9AC,it_80270E30}
        // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
        const float hit_damage = yoshi_shyguy_fighter_hitcapsule_damage(
            batch, p_idx, hb_i, batch->state.item_state[shy_idx]);
        const int hit_damage_i = (int)hit_damage;
        const float dir =
            (batch->state.item_pos_x[shy_idx] > batch->state.pos_x[p_idx]) ? -1.0f : 1.0f;
        if (damage_hit_count < (uint8_t)(MSL_MAX_PLAYERS * MSL_MAX_HITBOXES)) {
          MslShyguyFighterHit* hit = &damage_hits[damage_hit_count++];
          hit->angle = batch->state.hitbox_angle[hb_i];
          hit->kbg = batch->state.hitbox_kbg[hb_i];
          hit->wsk = batch->state.hitbox_wsk[hb_i];
          hit->bkb = batch->state.hitbox_bkb[hb_i];
          hit->dir = dir;
          damage_total += hit_damage;
          if (hit_damage > max_damage) {
            max_damage = hit_damage;
          }
        }
        hitlist_register_fighter_group_item(batch, bi, p, hit_group, shyguy_slot,
                                            batch->state.item_spawn_id[shy_idx],
                                            (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
        combat_apply_deal_hitlag_raw_damage(batch, p_idx, hit_damage_i);
        any_hit = 1u;
        break;
      }
    }
  }

  if (damage_hit_count != 0u) {
    const MslCommonParams* c = msl_common_params();
    const int damage_i = (int)damage_total;
    uint8_t selected = 0u;
    float selected_kb = -1.0f;
    for (uint8_t i = 0u; i < damage_hit_count; i++) {
      const MslShyguyFighterHit* hit = &damage_hits[i];
      const float kb =
          yoshi_shyguy_item_kb_applied(c, params, hit->kbg, hit->wsk, hit->bkb, damage_i);
      if (kb > selected_kb) {
        selected = i;
        selected_kb = kb;
      }
    }
    const MslShyguyFighterHit* hit = &damage_hits[selected];
    // it_802703E8 records every overlap into the fixed DmgLog; it_80270E30 then selects the
    // highest-KB entry against aggregate xCA0 damage and runs Heiho's damage callback once.
    // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_80270E30}
    yoshi_shyguy_apply_damage_common(batch, shy_idx, params, hit->angle, hit->kbg, hit->wsk,
                                     hit->bkb, damage_total, max_damage, hit->dir);
  }
  return any_hit;
}

void yoshi_shyguy_items_update(MslBatch* batch, int bi) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params == NULL) {
    return;
  }
  MslStageBounds blast_bounds = {0};
  const uint8_t has_blast_bounds =
      stage_collision_get_blast_bounds_world(batch->state.stage_id[bi], &blast_bounds);
  uint8_t needs_sort = 0u;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii] ||
        !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[ii])) {
      continue;
    }
    const uint8_t state = batch->state.item_state[ii];
    if (state == 2u || state == 3u) {
      if (batch->state.item_shyguy_hitlag_valid[ii] != 0u &&
          batch->state.item_shyguy_hitlag[ii] > 0u) {
        // Generic item hitlag decrements before the item Phys/movement proc and leaves
        // xDC8_word.flags.x9 set while positive, so Item_802697D4 skips both Phys and position.
        // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_802697D4}
        batch->state.item_shyguy_hitlag[ii]--;
        batch->state.item_hitlag[ii] = batch->state.item_shyguy_hitlag[ii];
        if (state == 3u) {
          (void)yoshi_shyguy_try_knocked_state_fighter_body_hit(batch, bi, it, params);
        }
        continue;
      }
      if (state == 3u && batch->state.item_shyguy_delay_valid[ii] != 0u &&
          batch->state.item_shyguy_delay[ii] == 0u) {
        // State 3 Phys enters the return-flight state through it_802D9168 once x24 has counted
        // down. The native seed lane resets x24 on repeated low-damage callbacks while the item
        // remains in state 3, matching it_802D8EC8's same-state writeback.
        // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion3_Phys,it_802D9168,
        //   it_802D8EC8}
        batch->state.item_state[ii] = 4u;
        batch->state.item_vel_x[ii] = 0.0f;
        batch->state.item_vel_y[ii] = 0.0f;
        batch->state.item_direction[ii] = (batch->state.item_pos_x[ii] < 0.0f) ? -1.0f : 1.0f;
        batch->state.item_shyguy_delay[ii] = (uint16_t)MSL_YOSHI_SHYGUY_TURN_DELAY_FRAMES;
        batch->state.item_shyguy_delay_valid[ii] = 1u;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 0u;
        continue;
      }
      // Knocked/falling Shy Guy states use item gravity, then generic item position integration.
      // State 3 additionally delays before entering the return-flight state.
      // refs/melee/src/melee/it/items/itheiho.c::{
      //   itHeiho_UnkMotion2_Phys,itHeiho_UnkMotion3_Phys,it_802D9168}
      if (state == 2u) {
        // `itHeiho_UnkMotion2_Phys` uses generic item falling with the Article ItemAttr max-fall
        // speed. Once x40_vel.y is at/past the max threshold, vanilla preserves it rather than
        // applying another gravity tick.
        // refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion2_Phys
        // refs/melee/src/melee/it/it_26B1.c::it_80272860
        if (batch->state.item_vel_y[ii] > -params->fall_speed_max) {
          batch->state.item_vel_y[ii] -= params->fall_accel;
        }
      } else {
        batch->state.item_vel_y[ii] -= params->fall_accel;
      }
      batch->state.item_pos_x[ii] += batch->state.item_vel_x[ii];
      batch->state.item_pos_y[ii] += batch->state.item_vel_y[ii];
      if (state == 3u && batch->state.item_shyguy_delay_valid[ii] != 0u &&
          batch->state.item_shyguy_delay[ii] > 0u) {
        batch->state.item_shyguy_delay[ii]--;
      }
      if (has_blast_bounds != 0u &&
          yoshi_shyguy_falling_state_crossed_generic_blast_clear(
              &blast_bounds, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
        item_slot_clear(batch, ii);
        needs_sort = 1u;
      }
      continue;
    }
    if (state == 0u && batch->state.item_shyguy_delay_valid[ii] != 0u) {
      // State 0 owns the spawn staggering delay. When x24 reaches zero, the state changes to
      // active Shy Guy but does not run the active Phys callback until the next item proc.
      // refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion0_Phys
      if (batch->state.item_shyguy_delay[ii] == 0u) {
        batch->state.item_state[ii] = 1u;
        batch->state.item_vel_x[ii] = 0.0f;
        batch->state.item_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
      } else {
        batch->state.item_shyguy_delay[ii]--;
      }
      continue;
    }
    if (state != 1u && state != 4u) {
      continue;
    }
    // Active Shy Guy states run their Anim callback, which refreshes x40_vel from a dynamic-bone
    // translation delta, then Phys recomputes source-owned X velocity and generic item proc
    // integrates `item->pos += item->x40_vel + item->x70_nudge`. Slippi exposes current x40_vel
    // but not itemVar.heiho.x3C or the JObj AObj frame, so one-step seeds carry the previous
    // visible Y velocity for the same item identity as a prefix-causal phase key into the
    // GrSt.dat FObj delta table. The scratch is not a future lane: preprocessing derives it from
    // frame t-1 only. state-0 x24 delay, collision turnarounds, and spawn RNG remain separate
    // stage-object owners.
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D98C4,itHeiho_UnkMotion1_Anim,
    //   itHeiho_UnkMotion4_Anim}
    // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Phys,itHeiho_UnkMotion4_Phys}
    // refs/melee/src/melee/it/item.c::Item_802697D4
    if (has_blast_bounds != 0u && state == 1u) {
      // Active state 1 calls `it_802D9714` from Phys. That helper sets the generic clear flag only
      // after the Shy Guy has entered the interior and then crosses blast bounds with a 20-unit
      // margin. x22 is hidden; direction plus the crossed side is enough for the legal-stage Shy
      // Guy paths currently represented in replay rows.
      // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Phys,it_802D9714}
      const float x = batch->state.item_pos_x[ii];
      const float y = batch->state.item_pos_y[ii];
      const float dir = batch->state.item_direction[ii];
      if ((dir > 0.0f && x > blast_bounds.right + 20.0f) ||
          (dir < 0.0f && x < blast_bounds.left - 20.0f) || y > blast_bounds.top + 20.0f ||
          y < blast_bounds.bottom - 20.0f) {
        item_slot_clear(batch, ii);
        needs_sort = 1u;
        continue;
      }
    }
    float move_vel_x = batch->state.item_vel_x[ii];
    float export_vel_x = batch->state.item_vel_x[ii];
    if (batch->state.item_shyguy_speed_index_valid[ii] != 0u) {
      const uint8_t speed_index =
          batch->state.item_shyguy_speed_index[ii] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
      float speed_x = params->speed[speed_index] * batch->state.item_direction[ii];
      if (state == 4u) {
        speed_x *= params->state4_speed_mul;
      }
      move_vel_x = speed_x;
      export_vel_x = speed_x;
    }
    float move_vel_y = batch->state.item_vel_y[ii];
    float export_vel_y = batch->state.item_vel_y[ii];
    uint8_t phase_before_update = batch->state.item_shyguy_dyn_y_phase[ii];
    const uint8_t phase_before_update_valid = batch->state.item_shyguy_dyn_y_phase_valid[ii];
    uint8_t phase_used = 0u;
    if (phase_used == 0u && batch->state.item_shyguy_dyn_y_phase_valid[ii] != 0u) {
      const uint8_t phase = batch->state.item_shyguy_dyn_y_phase[ii];
      const float expected_cur = yoshi_shyguy_current_vel_y_for_phase(params, phase, state);
      if (fabsf(expected_cur - move_vel_y) <= 0.001f) {
        move_vel_y = yoshi_shyguy_vel_y_from_phase(params, phase, state);
        export_vel_y = move_vel_y;
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase + 1u) & 0xFFu);
        phase_used = 1u;
      } else if (state == 1u && batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u &&
                 yoshi_shyguy_state1_current_is_reset_export(
                     params, phase, batch->state.item_shyguy_prev_vel_y[ii], move_vel_y) != 0u) {
        move_vel_y = params->dyn_y_vel[0];
        export_vel_y = params->dyn_y_vel[0];
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase + 1u) & 0xFFu);
        phase_used = 1u;
      } else if (state == 1u && batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u) {
        const uint8_t prev_phase = (uint8_t)((phase + 255u) & 0xFFu);
        const float prev_reset_export = -yoshi_shyguy_dyn_y_pos_after_phase(params, prev_phase);
        if (fabsf(prev_reset_export - batch->state.item_shyguy_prev_vel_y[ii]) <= 0.001f &&
            fabsf(params->dyn_y_vel[0] - move_vel_y) <= 0.001f) {
          move_vel_y = params->dyn_y_vel[1];
          export_vel_y = params->dyn_y_vel[1];
          batch->state.item_shyguy_dyn_y_phase[ii] = 2u;
          phase_used = 1u;
        } else {
          batch->state.item_shyguy_dyn_y_phase_valid[ii] = 0u;
        }
      } else {
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 0u;
      }
    }
    if (phase_used == 0u) {
      if (batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u) {
        const float prev_vel_y = batch->state.item_shyguy_prev_vel_y[ii];
        float next_vel_y = move_vel_y;
        if (yoshi_shyguy_next_dynamic_vel_y(params, prev_vel_y, move_vel_y, (state == 4u) ? 1u : 0u,
                                            &next_vel_y) != 0u) {
          move_vel_y = next_vel_y;
          export_vel_y = next_vel_y;
        }
      } else if (fabsf(move_vel_y) <= 0.001f && state == 1u) {
        // `itHeiho_UnkMotion0_Phys` enters state 1 through `it_802D8918`, which resets x3C and
        // immediately samples the active child-JObj translation once before the first active Phys.
        // refs/melee/src/melee/it/items/itheiho.c::{it_802D8918,it_802D98AC,it_802D98C4}
        move_vel_y = params->dyn_y_vel[0];
        export_vel_y = params->dyn_y_vel[0];
      }
    }
    const uint8_t turn_delay_started_positive =
        (uint8_t)(batch->state.item_shyguy_delay_valid[ii] != 0u &&
                  batch->state.item_shyguy_delay[ii] > 0u);
    uint8_t turn_contact_allowed = 1u;
    if (turn_delay_started_positive != 0u) {
      batch->state.item_shyguy_delay[ii]--;
      if (batch->state.item_shyguy_delay[ii] > 0u) {
        turn_contact_allowed = 0u;
      }
    }
    if (state == 4u && turn_delay_started_positive != 0u) {
      // Return-flight x24 turn/camera delay: keep the existing broad export owner for rows whose
      // dynamic-bone provenance is not reasserted by a live source item-hitlist bridge below.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D9168,itHeiho_UnkMotion4_Phys}
      export_vel_x = batch->state.item_vel_x[ii];
      export_vel_y = batch->state.item_vel_y[ii];
    }
    batch->state.item_shyguy_prev_vel_y[ii] = batch->state.item_vel_y[ii];
    batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
    const float prev_pos_x = batch->state.item_pos_x[ii];
    const float prev_pos_y = batch->state.item_pos_y[ii];
    batch->state.item_pos_x[ii] += move_vel_x;
    batch->state.item_pos_y[ii] += move_vel_y;
    if (has_blast_bounds != 0u && state == 4u &&
        yoshi_shyguy_falling_state_crossed_generic_blast_clear(
            &blast_bounds, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
      // Return-flight state 4 does not call `it_802D9714`. It is entered from the low-damage
      // callback path after `it_802D8EC8` sets xDCC_flag.b3, so the generic item proc clears it on
      // exact side/bottom blast bounds after Phys/integration and before Coll.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168,
      //   itHeiho_UnkMotion4_Phys}
      // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
      item_slot_clear(batch, ii);
      needs_sort = 1u;
      continue;
    }
    const uint8_t wall_contact =
        (uint8_t)(turn_contact_allowed != 0u &&
                  yoshi_shyguy_active_fixed_ecb_wall_contact(
                      params, batch->state.stage_id[bi], batch->state.item_direction[ii],
                      prev_pos_x, prev_pos_y, batch->state.item_pos_x[ii],
                      batch->state.item_pos_y[ii]) != 0u);
    if (wall_contact != 0u) {
      const float new_dir = -batch->state.item_direction[ii];
      batch->state.item_direction[ii] = new_dir;
      if (batch->state.item_shyguy_speed_index_valid[ii] != 0u) {
        const uint8_t speed_index =
            batch->state.item_shyguy_speed_index[ii] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
        export_vel_x = params->speed[speed_index] * new_dir;
      } else {
        export_vel_x = -export_vel_x;
      }
      batch->state.item_shyguy_delay[ii] = (uint16_t)MSL_YOSHI_SHYGUY_TURN_DELAY_FRAMES;
      batch->state.item_shyguy_delay_valid[ii] = 1u;
    } else if (yoshi_shyguy_active_fixed_ecb_floor_contact(
                   params, batch->state.stage_id[bi], prev_pos_x, prev_pos_y,
                   batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
      // `temp_r31 == 1` restarts the current active/return animation through
      // `itHeiho_UnkMotion1_Anim_inline` or `it_802D9168`. State 1 resets itemVar.heiho.x3C and
      // immediately calls `it_802D98C4`, so the post-frame export is the reset-to-current child-JObj
      // delta while X stays at the floor-contact value; preserve the phase+previous-velocity pair so
      // the following Anim consumes the first ordinary child-delta frame. Return-flight state 4
      // re-enters the same `it_802D9168` return-flight owner and exports zero velocity on the
      // contact frame before the next Anim callback refreshes its dynamic-bone delta.
      // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll,
      //   itHeiho_UnkMotion1_Anim_inline,it_802D9168}
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D98AC,it_802D98C4}
      if (state == 4u) {
        batch->state.item_direction[ii] = (batch->state.item_pos_x[ii] < 0.0f) ? -1.0f : 1.0f;
        batch->state.item_shyguy_delay[ii] = 0u;
        batch->state.item_shyguy_delay_valid[ii] = 1u;
      }
      export_vel_x = 0.0f;
      if (state == 1u && phase_before_update_valid != 0u) {
        export_vel_y =
            -yoshi_shyguy_dyn_y_pos_after_phase(params, (int)((phase_before_update + 1u) & 0xFFu));
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase_before_update + 1u) & 0xFFu);
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = batch->state.item_vel_y[ii];
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
      } else {
        export_vel_y = 0.0f;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
      }
    }
    batch->state.item_vel_x[ii] = export_vel_x;
    batch->state.item_vel_y[ii] = export_vel_y;
    (void)yoshi_shyguy_try_fighter_hitbox_hit(batch, bi, it, params);
  }
  if (needs_sort != 0u) {
    items_sort(batch, bi);
  }
}
