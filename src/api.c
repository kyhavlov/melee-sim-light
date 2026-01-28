#include "api.h"

#include <errno.h>
#include <string.h>

#include "alloc.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "anim_pose.h"
#include "action_ids.h"
#include "batch_internal.h"
#include "combat.h"
#include "combat_geom.h"
#include "char_params.h"
#include "common_params.h"
#include "config.h"
#include "move_tables.h"
#include "ecb_tables.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "hit_status_tables.h"
#include "hurtbox_modes_tables.h"
#include "hurtcaps_tables.h"
#include "shield_tilt_table.h"
#include "laser_params.h"
#include "stage_collision.h"
#include "staling_tables.h"
#include "attack_id_tables.h"
#include "state.h"
#include "step.h"

MslBatch* msl_batch_create(int batch_size, int num_players) {
  if (batch_size <= 0) {
    return NULL;
  }
  if (!(num_players == 2 || num_players == 4)) {
    return NULL;
  }

  MslBatch* batch = (MslBatch*)alloc_calloc(1, sizeof(MslBatch));
  if (batch == NULL) {
    return NULL;
  }

  batch->batch_size = batch_size;
  config_default(&batch->config, num_players);

  if (state_alloc(&batch->state, batch_size) != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  // Debug-only per-fighter hit status override table (0xFF = none).
  batch->debug_hit_status_override =
      (uint8_t*)alloc_malloc((size_t)batch_size * (size_t)MSL_MAX_PLAYERS * sizeof(uint8_t));
  if (batch->debug_hit_status_override == NULL) {
    msl_batch_destroy(batch);
    return NULL;
  }
  memset(batch->debug_hit_status_override, 0xFF,
         (size_t)batch_size * (size_t)MSL_MAX_PLAYERS * sizeof(uint8_t));

  if (stage_collision_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (common_params_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (char_params_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (laser_params_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (move_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  // Staling tables are optional (groundwork only): missing artifacts should not prevent running.
  (void)staling_tables_init();

  // Fighter attack identity (x2068/x206C) uses decomp-derived MotionState move_id tables.
  // Require these tables at init: attack identity and staling attribution depend on them.
  if (attack_id_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (anim_table_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (anim_pose_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  // Shield tilt tables are debug-geometry only; treat as optional for now.
  (void)shield_tilt_table_init();

  if (hurtcaps_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (hurtbox_modes_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (hit_status_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (hitboxes_tables_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  if (ecb_table_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }
  if (ecb_extents_table_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  return batch;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  alloc_free(batch->debug_hit_status_override);
  state_free(&batch->state);
  alloc_free(batch);
}

int msl_batch_batch_size(const MslBatch* batch) { return batch ? batch->batch_size : 0; }

int msl_batch_num_players(const MslBatch* batch) {
  return batch ? (int)batch->config.num_players : 0;
}

int msl_batch_set_ucf_enabled(MslBatch* batch, int enabled) {
  if (batch == NULL) {
    return EINVAL;
  }
  batch->config.ucf_enabled = enabled ? 1 : 0;
  return 0;
}

int msl_batch_set_ucf_cardinals_1_0_enabled(MslBatch* batch, int enabled) {
  if (batch == NULL) {
    return EINVAL;
  }
  batch->config.ucf_cardinals_1_0_enabled = enabled ? 1 : 0;
  return 0;
}

int msl_batch_reseed_seed(MslBatch* batch, const uint8_t* seed_bytes, size_t seed_stride_bytes) {
  if (batch == NULL || seed_bytes == NULL) {
    return EINVAL;
  }
  if (seed_stride_bytes < sizeof(MslSeed)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* ptr = seed_bytes + (size_t)bi * seed_stride_bytes;
    const MslSeed* seed = (const MslSeed*)ptr;

    // Initialize the per-environment global stale attack instance counter from seeded values.
    // We don't currently store the counter explicitly in the seed schema; instead, choose the
    // next value after the maximum already present in the seeded snapshots.
    uint16_t max_attack_inst = 0;

    batch->state.frame_id[bi] = seed->frame_id;
    batch->state.frame_pre_random_seed[bi] = seed->frame_pre_random_seed;
    batch->state.stage_id[bi] = seed->stage_id;
    float match_damage_ratio = seed->match_damage_ratio;
    if (!(match_damage_ratio > 0.0f)) {
      match_damage_ratio = 1.0f;
    }
    batch->state.match_damage_ratio[bi] = match_damage_ratio;
    batch->state.is_teams[bi] = seed->is_teams ? 1 : 0;
    batch->state.stage_ledge_occupant_left[bi] = -1;
    batch->state.stage_ledge_occupant_right[bi] = -1;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.team_id[idx] = seed->team_id[p];
      batch->state.char_id[idx] = seed->char_id[p];
      float attack_ratio = seed->attack_ratio[p];
      if (!(attack_ratio > 0.0f)) {
        attack_ratio = 1.0f;
      }
      float defense_ratio = seed->defense_ratio[p];
      if (!(defense_ratio > 0.0f)) {
        defense_ratio = 1.0f;
      }
      batch->state.attack_ratio[idx] = attack_ratio;
      batch->state.defense_ratio[idx] = defense_ratio;

      batch->state.pos_x[idx] = seed->pos_x[p];
      batch->state.pos_y[idx] = seed->pos_y[p];
      batch->state.pos_z[idx] = seed->pos_z[p];
      batch->state.speed_air_x_self[idx] = seed->speed_air_x_self[p];
      batch->state.speed_ground_x_self[idx] = seed->speed_ground_x_self[p];
      batch->state.speed_y_self[idx] = seed->speed_y_self[p];
      batch->state.speed_x_attack[idx] = seed->speed_x_attack[p];
      batch->state.speed_y_attack[idx] = seed->speed_y_attack[p];
      // Decomp: fp->x34_scale is initialized from Player_GetModelScale and copied into y
      // (refs/melee/src/melee/ft/fighter.c). Many collision/bounds computations use fp->x34_scale.y.
      float scale_y = seed->fighter_scale_y[p];
      if (!(scale_y > 0.0f)) {
        scale_y = 1.0f;
      }
      batch->state.fighter_scale_y[idx] = scale_y;
      batch->state.facing[idx] = seed->facing[p] ? 1 : 0;
      batch->state.on_ground[idx] = seed->on_ground[p] ? 1 : 0;
      batch->state.ground_contact_x[idx] = 0.0f;
      batch->state.ground_contact_y[idx] = 0.0f;
      batch->state.ground_normal_x[idx] = 0.0f;
      batch->state.ground_normal_y[idx] = 1.0f;
      batch->state.coll_env_flags[idx] = 0u;
      batch->state.coll_prev_env_flags[idx] = 0u;

      batch->state.action_id[idx] = seed->action_id[p];
      batch->state.match_flow_timer[idx] = seed->match_flow_timer[p];
      batch->state.downwait_timer[idx] = seed->downwait_timer[p];
      // Seed deterministic anim timebase from Slippi post-frame `state_age` (fp->cur_anim_frame)
      // plus a strictly-causal derived fp->frame_speed_mul.
      msl_anim_timebase_seed(batch, idx, seed->anim_frame_f32[p], seed->frame_speed_mul_f32[p]);
      batch->state.guard_tilt_x8[idx] = seed->guard_tilt_x8[p];
      batch->state.guard_tilt_x4[idx] = seed->guard_tilt_x4[p];
      batch->state.jumps_left[idx] = seed->jumps_left[p];
      batch->state.stocks[idx] = seed->stocks[p];
      batch->state.kneebend_jump_input[idx] = seed->kneebend_jump_input[p];
      batch->state.kneebend_is_short_hop[idx] = seed->kneebend_is_short_hop[p];
      batch->state.tilt_timer_x[idx] = seed->tilt_timer_x[p];
      batch->state.tilt_timer_y[idx] = seed->tilt_timer_y[p];
      batch->state.fall_fast[idx] = seed->fall_fast[p] ? 1 : 0;
      batch->state.ledge_cooldown[idx] = seed->ledge_cooldown[p];
      batch->state.ledge_side[idx] = -1;
      // FallSpecial xC mode is not exposed by Slippi directly; derive it deterministically from
      // seeded post-frame velocities when possible.
      //
      // Decomp: ftCo_80096900 stores `mv.co.fallspecial.xC = arg1`, and ftCo_FallSpecial_Phys applies
      // the mobility cap only on the xC==0 branch.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
      //
      // Derivation (best-effort, reseed-friendly):
      // - If we are in FallSpecial, fall_fast is 0, and vy is more negative than the character's
      //   normal terminal velocity, then we must be on the xC==0 branch (since that branch passes
      //   `fast_fall_velocity` as the terminal clamp while fall_fast remains 0).
      // Otherwise default to xC=1 (covers EscapeAir->FallSpecial path: arg1=1).
      batch->state.fallspecial_xc[idx] = 1;
      {
        const uint16_t a = seed->action_id[p];
        if (a == (uint16_t)MSL_ACT_FALL_SPECIAL || a == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
            a == (uint16_t)MSL_ACT_FALL_SPECIAL_B) {
          if (!batch->state.fall_fast[idx]) {
            const MslCharParams* phys = msl_char_params(seed->char_id[p]);
            if (phys != NULL) {
              if (seed->speed_y_self[p] < -phys->terminal_vel) {
                batch->state.fallspecial_xc[idx] = 0;
              }
            }
          }
        }
      }
      batch->state.turn_frames_to_turn[idx] = seed->turn_frames_to_turn[p];
      batch->state.turn_has_turned[idx] = seed->turn_has_turned[p];
      batch->state.lr_press_timer[idx] = seed->lr_press_timer[p];
      batch->state.x672_input_timer[idx] = seed->x672_input_timer[p];
      batch->state.x673[idx] = seed->x673[p];
      batch->state.x674[idx] = seed->x674[p];
      batch->state.x675[idx] = seed->x675[p];
      batch->state.x676_x[idx] = seed->x676_x[p];
      batch->state.x677_y[idx] = seed->x677_y[p];
      batch->state.x678[idx] = seed->x678[p];
      batch->state.x679_x[idx] = seed->x679_x[p];
      batch->state.x67A_y[idx] = seed->x67A_y[p];
      batch->state.x67B[idx] = seed->x67B[p];
      batch->state.x67C[idx] = seed->x67C[p];
      batch->state.x67D[idx] = seed->x67D[p];
      // DownBound entry resets A/B press timers to 0xFF after the per-frame input counters update.
      // Slippi does not expose this override directly; approximate it by detecting action entry
      // using post-frame `state_age` (seed->action_frame == 0).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C
      if ((seed->action_id[p] == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
           seed->action_id[p] == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
          seed->action_frame[p] == 0) {
        batch->state.x67C[idx] = 0xFFu;
        batch->state.x67D[idx] = 0xFFu;
      }
      batch->state.x67E[idx] = seed->x67E[p];
      batch->state.x680[idx] = seed->x680[p];
      batch->state.x681[idx] = seed->x681[p];
      batch->state.x682[idx] = seed->x682[p];
      batch->state.x683[idx] = seed->x683[p];
      batch->state.x684[idx] = seed->x684[p];

      batch->state.ucf_padbuf_index[idx] = seed->ucf_padbuf_index[p];
      batch->state.ucf_padbuf_sdrop_up_frames[idx] = seed->ucf_padbuf_sdrop_up_frames[p];
      for (int k = 0; k < 4; k++) {
        batch->state.ucf_padbuf_stick_x[idx * 4 + (size_t)k] = seed->ucf_padbuf_stick_x[p][k];
        batch->state.ucf_padbuf_stick_y[idx * 4 + (size_t)k] = seed->ucf_padbuf_stick_y[p][k];
      }

      batch->state.percent[idx] = seed->percent[p];
      // Per-frame collision damage accumulator (fp->dmg.x1838_percentTemp) is not part of Slippi post-frame
      // state and is reset by Fighter_ProcessHit each frame; keep it at 0 on reseed.
      batch->state.percent_temp[idx] = 0.0f;
      batch->state.dmg_x2225_b7[idx] = seed->dmg_x2225_b7[p] ? 1 : 0;
      batch->state.dmg_x2224_b2[idx] = seed->dmg_x2224_b2[p] ? 1 : 0;
      batch->state.shield_hp[idx] = seed->shield_hp[p];
      batch->state.hitlag[idx] = seed->hitlag[p];
      batch->state.hitstun[idx] = seed->hitstun[p];
      batch->state.l_cancel[idx] = seed->l_cancel[p];
      batch->state.hurtbox_state[idx] = seed->hurtbox_state[p];
      batch->state.ground_id[idx] = seed->ground_id[p];
      batch->state.animation_index[idx] = seed->animation_index[p];
      batch->state.instance_hit_by[idx] = seed->instance_hit_by[p];
      batch->state.instance_id[idx] = seed->instance_id[p];
      batch->state.attack_id[idx] = seed->attack_id[p];
      batch->state.attack_instance[idx] = seed->attack_instance[p];
      batch->state.attack_identity_last_action_id[idx] = seed->action_id[p];
      batch->state.last_attack_landed[idx] = seed->last_attack_landed[p];
      batch->state.combo_count[idx] = seed->combo_count[p];
      batch->state.combo_victim_port[idx] = seed->combo_victim_port[p];
      batch->state.combo_victim_instance_id[idx] = seed->combo_victim_instance_id[p];
      batch->state.combo_timer_x2098[idx] = seed->combo_timer_x2098[p];
      batch->state.last_hit_by[idx] = seed->last_hit_by[p];

      if (seed->attack_instance[p] > max_attack_inst) {
        max_attack_inst = seed->attack_instance[p];
      }

      for (int k = 0; k < 5; k++) {
        batch->state.state_flags[idx * 5 + (size_t)k] = seed->state_flags[p][k];
      }

      // Stale-move (staling) queue snapshot.
      uint8_t stale_qi = seed->stale_queue_index[p];
      if (stale_qi >= (uint8_t)MSL_STALE_QUEUE_SIZE) {
        stale_qi = 0;
      }
      batch->state.stale_queue_index[idx] = stale_qi;
      const size_t stale_base = idx * (size_t)MSL_STALE_QUEUE_SIZE;
      for (int k = 0; k < MSL_STALE_QUEUE_SIZE; k++) {
        batch->state.stale_move_id[stale_base + (size_t)k] = seed->stale_move_id[p][k];
        batch->state.stale_attack_instance[stale_base + (size_t)k] =
            seed->stale_attack_instance[p][k];
        if (seed->stale_attack_instance[p][k] > max_attack_inst) {
          max_attack_inst = seed->stale_attack_instance[p][k];
        }
      }
    }

    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = msl_idx_item(bi, it);
      const MslItem* item = &seed->items[it];
      batch->state.item_exists[ii] = item->exists;
      batch->state.item_state[ii] = item->state;
      batch->state.item_type[ii] = item->type;
      batch->state.item_owner[ii] = item->owner;
      batch->state.item_instance_id[ii] = item->instance_id;
      batch->state.item_attack_id[ii] = item->attack_id;
      batch->state.item_attack_instance[ii] = item->attack_instance;
      if (item->attack_instance > max_attack_inst) {
        max_attack_inst = item->attack_instance;
      }
      batch->state.item_direction[ii] = item->direction;
      batch->state.item_vel_x[ii] = item->vel_x;
      batch->state.item_vel_y[ii] = item->vel_y;
      batch->state.item_pos_x[ii] = item->pos_x;
      batch->state.item_pos_y[ii] = item->pos_y;
      batch->state.item_damage[ii] = item->damage;
      batch->state.item_timer[ii] = item->timer;
      batch->state.item_spawn_id[ii] = item->spawn_id;
      batch->state.item_misc0[ii] = item->misc0;
      batch->state.item_misc1[ii] = item->misc1;
      batch->state.item_misc2[ii] = item->misc2;
      batch->state.item_misc3[ii] = item->misc3;

      // Item hitlists are derived (not seeded): clear on reseed so reused item slots don't
      // inherit stale victim cooldowns.
      const size_t cd_base = ii * (size_t)MSL_MAX_PLAYERS;
      for (int v = 0; v < MSL_MAX_PLAYERS; v++) {
        batch->state.item_hitlist_cd[cd_base + (size_t)v] = 0;
        batch->state.item_hitlist_victim_iid[cd_base + (size_t)v] = 0;
      }
    }

    // Next value for plStale_IncrementAttackInstance (global counter).
    uint16_t next = (uint16_t)(max_attack_inst + 1u);
    if (next == 0) {
      next = 1;
    }
    batch->state.stale_attack_instance_counter[bi] = next;

    // Combat hitlists are part of the reseed schema (teacher-forced one-step eval).
    const size_t base =
        (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
    for (int attacker = 0; attacker < MSL_MAX_PLAYERS; attacker++) {
      for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
        for (int victim = 0; victim < MSL_MAX_PLAYERS; victim++) {
          const size_t i = base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)g) *
                                       (size_t)MSL_MAX_PLAYERS +
                                   (size_t)victim);
          batch->state.combat_hitlist_cd[i] = seed->combat_hitlist_cd[attacker][g][victim];
          batch->state.combat_hitlist_victim_iid[i] =
              seed->combat_hitlist_victim_iid[attacker][g][victim];
        }
      }
    }
  }

  return 0;
}

int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes) {
  return step_one_frame(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                        input_stride_bytes);
}

static uint8_t msl_is_dead_from_stocks(uint8_t stocks) { return stocks == 0 ? 1 : 0; }

int msl_batch_write_compare(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslCompare)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t* ptr = out_bytes + (size_t)bi * out_stride_bytes;
    MslCompare* out = (MslCompare*)ptr;
    memset(out, 0, sizeof(*out));

    out->frame_id = batch->state.frame_id[bi];
    out->frame_pre_random_seed = batch->state.frame_pre_random_seed[bi];
    out->stage_id = batch->state.stage_id[bi];
    out->num_players = batch->config.num_players;
    out->is_teams = batch->state.is_teams[bi] ? 1 : 0;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      out->team_id[p] = batch->state.team_id[idx];
      out->char_id[p] = batch->state.char_id[idx];

      out->pos_x[p] = batch->state.pos_x[idx];
      out->pos_y[p] = batch->state.pos_y[idx];
      out->speed_air_x_self[p] = batch->state.speed_air_x_self[idx];
      out->speed_ground_x_self[p] = batch->state.speed_ground_x_self[idx];
      out->speed_y_self[p] = batch->state.speed_y_self[idx];
      out->speed_x_attack[p] = batch->state.speed_x_attack[idx];
      out->speed_y_attack[p] = batch->state.speed_y_attack[idx];
      out->facing[p] = batch->state.facing[idx] ? 1 : 0;
      out->on_ground[p] = batch->state.on_ground[idx] ? 1 : 0;

      out->action_id[p] = batch->state.action_id[idx];
      out->action_frame[p] = batch->state.action_frame[idx];
      out->jumps_left[p] = batch->state.jumps_left[idx];
      out->stocks[p] = batch->state.stocks[idx];
      out->is_dead[p] = msl_is_dead_from_stocks(batch->state.stocks[idx]);

      out->percent[p] = batch->state.percent[idx];
      out->shield_hp[p] = batch->state.shield_hp[idx];
      out->hitlag[p] = batch->state.hitlag[idx];
      out->hitstun[p] = batch->state.hitstun[idx];
      out->l_cancel[p] = batch->state.l_cancel[idx];
      out->hurtbox_state[p] = batch->state.hurtbox_state[idx];
      out->ground_id[p] = batch->state.ground_id[idx];
      out->animation_index[p] = batch->state.animation_index[idx];
      out->instance_hit_by[p] = batch->state.instance_hit_by[idx];
      out->instance_id[p] = batch->state.instance_id[idx];
      out->last_attack_landed[p] = batch->state.last_attack_landed[idx];
      out->combo_count[p] = batch->state.combo_count[idx];
      out->last_hit_by[p] = batch->state.last_hit_by[idx];

      for (int k = 0; k < 5; k++) {
        out->state_flags[p][k] = batch->state.state_flags[idx * 5 + (size_t)k];
      }
    }

    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = msl_idx_item(bi, it);
      MslItem* item = &out->items[it];
      item->exists = batch->state.item_exists[ii];
      item->state = batch->state.item_state[ii];
      item->type = batch->state.item_type[ii];
      item->owner = batch->state.item_owner[ii];
      item->instance_id = batch->state.item_instance_id[ii];
      item->attack_id = batch->state.item_attack_id[ii];
      item->attack_instance = batch->state.item_attack_instance[ii];
      item->direction = batch->state.item_direction[ii];
      item->vel_x = batch->state.item_vel_x[ii];
      item->vel_y = batch->state.item_vel_y[ii];
      item->pos_x = batch->state.item_pos_x[ii];
      item->pos_y = batch->state.item_pos_y[ii];
      item->damage = batch->state.item_damage[ii];
      item->timer = batch->state.item_timer[ii];
      item->spawn_id = batch->state.item_spawn_id[ii];
      item->misc0 = batch->state.item_misc0[ii];
      item->misc1 = batch->state.item_misc1[ii];
      item->misc2 = batch->state.item_misc2[ii];
      item->misc3 = batch->state.item_misc3[ii];
    }
  }

  return 0;
}

int msl_batch_debug_write_processed_input(const MslBatch* batch, uint8_t* out_bytes,
                                          size_t out_stride_bytes) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslProcessedInput)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t* ptr = out_bytes + (size_t)bi * out_stride_bytes;
    MslProcessedInput* out = (MslProcessedInput*)ptr;
    memset(out, 0, sizeof(*out));

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      out->p[p].buttons = batch->state.input_buttons[idx];
      out->p[p].main_x = batch->state.input_main_x[idx];
      out->p[p].main_y = batch->state.input_main_y[idx];
      out->p[p].c_x = batch->state.input_c_x[idx];
      out->p[p].c_y = batch->state.input_c_y[idx];
      out->p[p].l = batch->state.input_l[idx];
      out->p[p].r = batch->state.input_r[idx];
    }
  }

  return 0;
}

int msl_batch_debug_write_internals(const MslBatch* batch, uint8_t* out_bytes,
                                    size_t out_stride_bytes) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslDebugInternals)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t* ptr = out_bytes + (size_t)bi * out_stride_bytes;
    MslDebugInternals* out = (MslDebugInternals*)ptr;
    memset(out, 0, sizeof(*out));

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      out->tilt_timer_x[p] = batch->state.tilt_timer_x[idx];
      out->turn_frames_to_turn[p] = batch->state.turn_frames_to_turn[idx];
      out->turn_has_turned[p] = batch->state.turn_has_turned[idx];
      out->attack_id[p] = batch->state.attack_id[idx];
      out->attack_instance[p] = batch->state.attack_instance[idx];
      out->attack_identity_last_action_id[p] = batch->state.attack_identity_last_action_id[idx];
    }
  }

  return 0;
}

int msl_batch_debug_force_anim_timebase_enter(MslBatch* batch, int batch_index, int player_index,
                                              float anim_start_f32, float anim_speed_f32) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  const size_t idx = msl_idx_player(batch_index, player_index);
  msl_anim_timebase_enter(batch, idx, anim_start_f32, anim_speed_f32);
  return 0;
}

int msl_batch_debug_hurtcaps_world(const MslBatch* batch, int batch_index, int player_index,
                                   float* out_caps_7, uint8_t* out_count) {
  if (batch == NULL || out_caps_7 == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  // Zero-fill the full fixed-size output for stable test snapshots.
  memset(out_caps_7, 0, sizeof(float) * (size_t)MSL_MAX_HURTCAPS * 7u);

  const size_t idx = msl_idx_player(batch_index, player_index);
  uint8_t count = batch->state.hurtcap_count[idx];
  if (count > (uint8_t)MSL_MAX_HURTCAPS) {
    count = (uint8_t)MSL_MAX_HURTCAPS;
  }
  *out_count = count;

  const size_t base = ((size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)player_index) *
                      (size_t)MSL_MAX_HURTCAPS;
  for (uint8_t i = 0; i < count; i++) {
    const size_t hi = base + (size_t)i;
    const size_t o = (size_t)i * 7u;
    out_caps_7[o + 0] = batch->state.hurtcap_a_x[hi];
    out_caps_7[o + 1] = batch->state.hurtcap_a_y[hi];
    out_caps_7[o + 2] = batch->state.hurtcap_a_z[hi];
    out_caps_7[o + 3] = batch->state.hurtcap_b_x[hi];
    out_caps_7[o + 4] = batch->state.hurtcap_b_y[hi];
    out_caps_7[o + 5] = batch->state.hurtcap_b_z[hi];
    out_caps_7[o + 6] = batch->state.hurtcap_radius[hi];
  }

  return 0;
}

int msl_batch_debug_hitboxes_world(const MslBatch* batch, int batch_index, int player_index,
                                   float* out_hitboxes_10, uint8_t* out_count) {
  if (batch == NULL || out_hitboxes_10 == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  // Zero-fill the full fixed-size output for stable test snapshots.
  memset(out_hitboxes_10, 0, sizeof(float) * (size_t)MSL_MAX_HITBOXES * 10u);

  const size_t idx = msl_idx_player(batch_index, player_index);
  uint8_t count = batch->state.hitbox_count[idx];
  if (count > (uint8_t)MSL_MAX_HITBOXES) {
    count = (uint8_t)MSL_MAX_HITBOXES;
  }
  *out_count = count;

  const size_t base = ((size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)player_index) *
                      (size_t)MSL_MAX_HITBOXES;
  for (int i = 0; i < MSL_MAX_HITBOXES; i++) {
    const size_t hi = base + (size_t)i;
    const size_t o = (size_t)i * 10u;
    out_hitboxes_10[o + 0] = batch->state.hitbox_x[hi];
    out_hitboxes_10[o + 1] = batch->state.hitbox_y[hi];
    out_hitboxes_10[o + 2] = batch->state.hitbox_z[hi];
    out_hitboxes_10[o + 3] = batch->state.hitbox_radius[hi];
    out_hitboxes_10[o + 4] = batch->state.hitbox_damage[hi];
    out_hitboxes_10[o + 5] = (float)batch->state.hitbox_u16_0[hi];
    out_hitboxes_10[o + 6] = (float)batch->state.hitbox_u16_1[hi];
    out_hitboxes_10[o + 7] = (float)batch->state.hitbox_u16_3[hi];
    out_hitboxes_10[o + 8] = (float)batch->state.hitbox_bone_part_id[hi];
    out_hitboxes_10[o + 9] = (float)batch->state.hitbox_enabled[hi];
  }

  return 0;
}

int msl_batch_debug_hitboxes_world_full(const MslBatch* batch, int batch_index, int player_index,
                                        float* out_hitboxes_16, uint8_t* out_count) {
  if (batch == NULL || out_hitboxes_16 == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  // Zero-fill the full fixed-size output for stable test snapshots.
  memset(out_hitboxes_16, 0, sizeof(float) * (size_t)MSL_MAX_HITBOXES * 16u);

  const size_t idx = msl_idx_player(batch_index, player_index);
  uint8_t count = batch->state.hitbox_count[idx];
  if (count > (uint8_t)MSL_MAX_HITBOXES) {
    count = (uint8_t)MSL_MAX_HITBOXES;
  }
  *out_count = count;

  const size_t base = ((size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)player_index) *
                      (size_t)MSL_MAX_HITBOXES;
  for (int i = 0; i < MSL_MAX_HITBOXES; i++) {
    const size_t hi = base + (size_t)i;
    const size_t o = (size_t)i * 16u;
    out_hitboxes_16[o + 0] = batch->state.hitbox_x[hi];
    out_hitboxes_16[o + 1] = batch->state.hitbox_y[hi];
    out_hitboxes_16[o + 2] = batch->state.hitbox_z[hi];
    out_hitboxes_16[o + 3] = batch->state.hitbox_radius[hi];
    out_hitboxes_16[o + 4] = batch->state.hitbox_damage[hi];
    out_hitboxes_16[o + 5] = (float)batch->state.hitbox_angle[hi];
    out_hitboxes_16[o + 6] = (float)batch->state.hitbox_kbg[hi];
    out_hitboxes_16[o + 7] = (float)batch->state.hitbox_wsk[hi];
    out_hitboxes_16[o + 8] = (float)batch->state.hitbox_bkb[hi];
    out_hitboxes_16[o + 9] = (float)batch->state.hitbox_element[hi];
    out_hitboxes_16[o + 10] = (float)batch->state.hitbox_shield_damage[hi];
    out_hitboxes_16[o + 11] = (float)batch->state.hitbox_sfx_severity[hi];
    out_hitboxes_16[o + 12] = (float)batch->state.hitbox_sfx_kind[hi];
    out_hitboxes_16[o + 13] = (float)batch->state.hitbox_flags[hi];
    out_hitboxes_16[o + 14] = (float)batch->state.hitbox_bone_part_id[hi];
    out_hitboxes_16[o + 15] = (float)batch->state.hitbox_enabled[hi];
  }

  return 0;
}

static inline size_t debug_idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t debug_idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx,
                                               float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static int debug_combat_contacts_impl(const MslBatch* batch, int batch_index,
                                      MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                      uint16_t* out_count, int filtered) {
  if (batch == NULL || out_contacts == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }

  uint16_t written = 0;
  if (max_contacts == 0) {
    *out_count = 0;
    return 0;
  }

  const int num_players = (int)batch->config.num_players;
  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(batch_index, attacker);
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(batch_index, defender);

      if (batch->state.is_teams[batch_index]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = debug_idx_hitbox(batch_index, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }

        if (filtered) {
          // Decomp victim ground/air gate: this_hit->x40_b2 (hit_aerial) / x40_b3 (hit_grounded)
          // against victim_fp->ground_or_air.
          // refs/melee/src/melee/ft/ftcoll.c (HitCapsule eligibility checks).
          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              continue;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              continue;
            }
          }

          // TODO(decomp): extend debug filtering to match ftColl hitcapsule eligibility once the
          // required state is modeled (intangibility, element catch/inert, thrown-fighter rules,
          // grabbed-victim-only, lbColl_8000ACFC, etc.).
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = debug_idx_hurtcap(batch_index, defender, (int)cap_id);
          const float ax = batch->state.hurtcap_a_x[cap_i];
          const float ay = batch->state.hurtcap_a_y[cap_i];
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i];
          const float by = batch->state.hurtcap_b_y[cap_i];
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          if (!combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
            continue;
          }

          MslDebugCombatContact* out = &out_contacts[written];
          memset(out, 0, sizeof(*out));
          out->attacker = (uint8_t)attacker;
          out->defender = (uint8_t)defender;
          out->hitbox_id = (uint8_t)hb_id;
          out->hurtcap_id = cap_id;
          out->attacker_msid = msid;
          out->attacker_action_frame = action_frame;
          out->hitbox_x = hx;
          out->hitbox_y = hy;
          out->hitbox_z = hz;
          out->hitbox_radius = hr;
          out->hitbox_damage = hdmg;
          out->hurtcap_ax = ax;
          out->hurtcap_ay = ay;
          out->hurtcap_az = az;
          out->hurtcap_bx = bx;
          out->hurtcap_by = by;
          out->hurtcap_bz = bz;
          out->hurtcap_radius = cr;
          written++;

          if (written >= max_contacts) {
            *out_count = written;
            return 0;
          }
        }
      }
    }
  }

  *out_count = written;
  return 0;
}

static int debug_combat_contacts_classified_impl(const MslBatch* batch, int batch_index,
                                                 MslDebugCombatContactClassified* out_contacts,
                                                 uint16_t max_contacts, uint16_t* out_count,
                                                 int filtered) {
  if (batch == NULL || out_contacts == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }

  uint16_t written = 0;
  if (max_contacts == 0) {
    *out_count = 0;
    return 0;
  }

  const int num_players = (int)batch->config.num_players;
  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(batch_index, attacker);
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(batch_index, defender);

      if (batch->state.is_teams[batch_index]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1 : 0;

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = debug_idx_hitbox(batch_index, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }

        if (filtered) {
          // Decomp victim ground/air gate: this_hit->x40_b2 (hit_aerial) / x40_b3 (hit_grounded)
          // against victim_fp->ground_or_air.
          // refs/melee/src/melee/ft/ftcoll.c (HitCapsule eligibility checks).
          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              continue;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              continue;
            }
          }
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        // SHIELD precedence: if the hitbox intersects the defender shield bubble, classify as SHIELD
        // and skip BODY contacts for this (attacker, defender, hitbox_id).
        if (shield_active && sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr)) {
          MslDebugCombatContactClassified* out = &out_contacts[written];
          memset(out, 0, sizeof(*out));
          out->attacker = (uint8_t)attacker;
          out->defender = (uint8_t)defender;
          out->hitbox_id = (uint8_t)hb_id;
          out->contact_kind = 1;
          out->hurtcap_id = 0xFFu;
          out->attacker_msid = msid;
          out->attacker_action_frame = action_frame;
          out->hitbox_x = hx;
          out->hitbox_y = hy;
          out->hitbox_z = hz;
          out->hitbox_radius = hr;
          out->hitbox_damage = hdmg;
          out->shield_x = shx;
          out->shield_y = shy;
          out->shield_z = shz;
          out->shield_radius = shr;
          written++;
          if (written >= max_contacts) {
            *out_count = written;
            return 0;
          }
          continue;
        }

        // BODY contacts (only when not shielded).
        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = debug_idx_hurtcap(batch_index, defender, (int)cap_id);
          const float ax = batch->state.hurtcap_a_x[cap_i];
          const float ay = batch->state.hurtcap_a_y[cap_i];
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i];
          const float by = batch->state.hurtcap_b_y[cap_i];
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          if (!combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
            continue;
          }

          MslDebugCombatContactClassified* out = &out_contacts[written];
          memset(out, 0, sizeof(*out));
          out->attacker = (uint8_t)attacker;
          out->defender = (uint8_t)defender;
          out->hitbox_id = (uint8_t)hb_id;
          out->contact_kind = 0;
          out->hurtcap_id = cap_id;
          out->attacker_msid = msid;
          out->attacker_action_frame = action_frame;
          out->hitbox_x = hx;
          out->hitbox_y = hy;
          out->hitbox_z = hz;
          out->hitbox_radius = hr;
          out->hitbox_damage = hdmg;
          out->hurtcap_ax = ax;
          out->hurtcap_ay = ay;
          out->hurtcap_az = az;
          out->hurtcap_bx = bx;
          out->hurtcap_by = by;
          out->hurtcap_bz = bz;
          out->hurtcap_radius = cr;
          out->shield_x = shx;
          out->shield_y = shy;
          out->shield_z = shz;
          out->shield_radius = shr;
          written++;

          if (written >= max_contacts) {
            *out_count = written;
            return 0;
          }
        }
      }
    }
  }

  *out_count = written;
  return 0;
}

int msl_batch_debug_combat_contacts(const MslBatch* batch, int batch_index,
                                    MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                    uint16_t* out_count) {
  return debug_combat_contacts_impl(batch, batch_index, out_contacts, max_contacts, out_count, 0);
}

int msl_batch_debug_combat_contacts_filtered(const MslBatch* batch, int batch_index,
                                             MslDebugCombatContact* out_contacts,
                                             uint16_t max_contacts, uint16_t* out_count) {
  return debug_combat_contacts_impl(batch, batch_index, out_contacts, max_contacts, out_count, 1);
}

int msl_batch_debug_combat_contacts_classified(const MslBatch* batch, int batch_index,
                                               MslDebugCombatContactClassified* out_contacts,
                                               uint16_t max_contacts, uint16_t* out_count) {
  return debug_combat_contacts_classified_impl(batch, batch_index, out_contacts, max_contacts,
                                               out_count, 0);
}

int msl_batch_debug_combat_contacts_classified_filtered(
    const MslBatch* batch, int batch_index, MslDebugCombatContactClassified* out_contacts,
    uint16_t max_contacts, uint16_t* out_count) {
  return debug_combat_contacts_classified_impl(batch, batch_index, out_contacts, max_contacts,
                                               out_count, 1);
}

int msl_batch_debug_shield_bubbles_world(const MslBatch* batch, int batch_index,
                                         float* out_xyzw_4p) {
  if (batch == NULL || out_xyzw_4p == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }

  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    const size_t idx = msl_idx_player(batch_index, p);
    const size_t o = (size_t)p * 4u;
    if (p >= num_players) {
      out_xyzw_4p[o + 0] = 0.0f;
      out_xyzw_4p[o + 1] = 0.0f;
      out_xyzw_4p[o + 2] = 0.0f;
      out_xyzw_4p[o + 3] = 0.0f;
      continue;
    }
    out_xyzw_4p[o + 0] = batch->state.shield_x[idx];
    out_xyzw_4p[o + 1] = batch->state.shield_y[idx];
    out_xyzw_4p[o + 2] = batch->state.shield_z[idx];
    out_xyzw_4p[o + 3] = batch->state.shield_radius[idx];
  }

  return 0;
}

int msl_batch_debug_clear_hitboxes_world(MslBatch* batch, int batch_index, int player_index) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  const size_t idx = msl_idx_player(batch_index, player_index);
  batch->state.hitbox_count[idx] = 0;
  for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
    const size_t hb_i = debug_idx_hitbox(batch_index, player_index, hb_id);
    batch->state.hitbox_enabled[hb_i] = 0;
    batch->state.hitbox_x[hb_i] = 0.0f;
    batch->state.hitbox_y[hb_i] = 0.0f;
    batch->state.hitbox_z[hb_i] = 0.0f;
    batch->state.hitbox_radius[hb_i] = 0.0f;
    batch->state.hitbox_damage[hb_i] = 0.0f;
    batch->state.hitbox_u16_7[hb_i] = 0;
    batch->state.hitbox_u16_6[hb_i] = 0;
    batch->state.hitbox_flags[hb_i] = 0;
  }

  // Debug helper: approximate the engine's "clear hitboxes" behavior by also resetting rehit
  // suppression for this attacker so that re-enabling a hitbox can immediately apply a new hit.
  for (uint8_t g = 0; g < (uint8_t)MSL_HITLIST_GROUPS; g++) {
    hitlist_clear_group(batch, batch_index, player_index, g);
  }

  return 0;
}

int msl_batch_debug_set_hitbox_world(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, float x, float y, float z, float radius,
                                     float damage, int enabled) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  const size_t hb_i = debug_idx_hitbox(batch_index, player_index, hitbox_id);
  batch->state.hitbox_enabled[hb_i] = enabled ? 1 : 0;
  batch->state.hitbox_x[hb_i] = x;
  batch->state.hitbox_y[hb_i] = y;
  batch->state.hitbox_z[hb_i] = z;
  batch->state.hitbox_radius[hb_i] = radius;
  batch->state.hitbox_damage[hb_i] = damage;

  // Keep hitbox_count consistent with enabled slots.
  const size_t idx = msl_idx_player(batch_index, player_index);
  uint8_t count = 0;
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t i = debug_idx_hitbox(batch_index, player_index, hb);
    if (batch->state.hitbox_enabled[i]) {
      count++;
    }
  }
  batch->state.hitbox_count[idx] = count;

  return 0;
}

int msl_batch_debug_set_hitbox_flags(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, uint16_t hitbox_flags) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  const size_t hb_i = debug_idx_hitbox(batch_index, player_index, hitbox_id);
  // Keep both the raw extracted field and decoded mirror consistent for debug-set primitives.
  batch->state.hitbox_u16_6[hb_i] = hitbox_flags;
  batch->state.hitbox_flags[hb_i] = hitbox_flags;
  return 0;
}

int msl_batch_debug_set_hitbox_element(MslBatch* batch, int batch_index, int player_index,
                                       int hitbox_id, uint8_t element) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  const size_t hb_i = debug_idx_hitbox(batch_index, player_index, hitbox_id);
  // Keep both the raw extracted field and decoded mirror consistent for debug-set primitives.
  //
  // src/hitboxes_tables.h: MSLHITB1 u16_4 packs (element low 8 | shield_damage high 8).
  const uint16_t u16_4 = batch->state.hitbox_u16_4[hb_i];
  batch->state.hitbox_u16_4[hb_i] = (uint16_t)((u16_4 & 0xFF00u) | (uint16_t)element);
  batch->state.hitbox_element[hb_i] = element;
  return 0;
}

int msl_batch_debug_set_hitbox_kb_params(MslBatch* batch, int batch_index, int player_index,
                                         int hitbox_id, uint16_t angle_deg, uint16_t kbg,
                                         uint16_t wsk, uint16_t bkb) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  const size_t hb_i = debug_idx_hitbox(batch_index, player_index, hitbox_id);

  // Keep both the raw extracted u16 tail and the decoded mirrors consistent.
  //
  // src/hitboxes_tables.h: MSLHITB1 u16 tail layout:
  // - u16_0: angle (degrees; 361 == Sakurai angle)
  // - u16_1: kbg
  // - u16_2: wsk
  // - u16_3: bkb
  batch->state.hitbox_u16_0[hb_i] = angle_deg;
  batch->state.hitbox_u16_1[hb_i] = kbg;
  batch->state.hitbox_u16_2[hb_i] = wsk;
  batch->state.hitbox_u16_3[hb_i] = bkb;

  batch->state.hitbox_angle[hb_i] = angle_deg;
  batch->state.hitbox_kbg[hb_i] = kbg;
  batch->state.hitbox_wsk[hb_i] = wsk;
  batch->state.hitbox_bkb[hb_i] = bkb;

  return 0;
}

int msl_batch_debug_clear_hurtcaps_world(MslBatch* batch, int batch_index, int player_index) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  const size_t idx = msl_idx_player(batch_index, player_index);
  batch->state.hurtcap_count[idx] = 0;
  for (int cap_id = 0; cap_id < MSL_MAX_HURTCAPS; cap_id++) {
    const size_t cap_i = debug_idx_hurtcap(batch_index, player_index, cap_id);
    batch->state.hurtcap_enabled[cap_i] = 0;
    batch->state.hurtcap_a_x[cap_i] = 0.0f;
    batch->state.hurtcap_a_y[cap_i] = 0.0f;
    batch->state.hurtcap_a_z[cap_i] = 0.0f;
    batch->state.hurtcap_b_x[cap_i] = 0.0f;
    batch->state.hurtcap_b_y[cap_i] = 0.0f;
    batch->state.hurtcap_b_z[cap_i] = 0.0f;
    batch->state.hurtcap_radius[cap_i] = 0.0f;
    batch->state.hurtcap_is_grabbable[cap_i] = 0;
    batch->state.hurtcap_height[cap_i] = 0;
  }

  return 0;
}

int msl_batch_debug_set_hurtcap_world(MslBatch* batch, int batch_index, int player_index,
                                      int hurtcap_id, float ax, float ay, float az, float bx,
                                      float by, float bz, float radius) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hurtcap_id < 0 || hurtcap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }

  const size_t cap_i = debug_idx_hurtcap(batch_index, player_index, hurtcap_id);
  batch->state.hurtcap_enabled[cap_i] = 1;
  batch->state.hurtcap_a_x[cap_i] = ax;
  batch->state.hurtcap_a_y[cap_i] = ay;
  batch->state.hurtcap_a_z[cap_i] = az;
  batch->state.hurtcap_b_x[cap_i] = bx;
  batch->state.hurtcap_b_y[cap_i] = by;
  batch->state.hurtcap_b_z[cap_i] = bz;
  batch->state.hurtcap_radius[cap_i] = radius;

  const size_t idx = msl_idx_player(batch_index, player_index);
  uint8_t count = batch->state.hurtcap_count[idx];
  const uint8_t want = (uint8_t)(hurtcap_id + 1);
  if (want > count) {
    count = want;
  }
  batch->state.hurtcap_count[idx] = count;

  return 0;
}

int msl_batch_debug_set_hurtcap_height(MslBatch* batch, int batch_index, int player_index,
                                       int hurtcap_id, uint8_t height) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hurtcap_id < 0 || hurtcap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }

  const size_t cap_i = debug_idx_hurtcap(batch_index, player_index, hurtcap_id);
  batch->state.hurtcap_height[cap_i] = height;
  return 0;
}

int msl_batch_debug_set_hurtcap_enabled(MslBatch* batch, int batch_index, int player_index,
                                        int hurtcap_id, int enabled) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hurtcap_id < 0 || hurtcap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }
  const size_t cap_i = debug_idx_hurtcap(batch_index, player_index, hurtcap_id);
  batch->state.hurtcap_enabled[cap_i] = enabled ? 1 : 0;
  return 0;
}

int msl_batch_debug_set_hitlag(MslBatch* batch, int batch_index, int player_index,
                               uint16_t hitlag_frames) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  const size_t idx = msl_idx_player(batch_index, player_index);
  batch->state.hitlag[idx] = hitlag_frames;
  return 0;
}

int msl_batch_debug_set_hit_status_override(MslBatch* batch, int batch_index, int player_index,
                                            int status) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  if (batch->debug_hit_status_override == NULL) {
    return EINVAL;
  }

  const size_t idx = msl_idx_player(batch_index, player_index);
  if (status < 0) {
    batch->debug_hit_status_override[idx] = 0xFFu;
    return 0;
  }
  if (status > 0xFF) {
    return EINVAL;
  }
  batch->debug_hit_status_override[idx] = (uint8_t)status;
  return 0;
}

int msl_batch_debug_combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return EINVAL;
  }
  combat_processhit_consume(batch);
  combat_resolve(batch);
  return 0;
}

int msl_batch_debug_combat_select_body_hits(MslBatch* batch, int batch_index,
                                            MslDebugCombatContact* out_contacts,
                                            uint16_t max_contacts, uint16_t* out_count) {
  return combat_debug_select_body_hits(batch, batch_index, out_contacts, max_contacts, out_count);
}

int msl_debug_point_segment_dist2(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz, float* out_d2, float* out_t) {
  if (out_d2 == NULL || out_t == NULL) {
    return EINVAL;
  }
  combat_point_segment_dist2(px, py, pz, ax, ay, az, bx, by, bz, out_d2, out_t);
  return 0;
}

int msl_debug_reset_pose_and_hitboxes_tables(void) {
  // Intended only for synthetic unit tests that need to swap MSL_DATA_DIR within a single process.
  // Do not call this while any batches exist; they may depend on cached table pointers.
  anim_pose_reset_for_tests();
  hitboxes_tables_reset_for_tests();
  return 0;
}
