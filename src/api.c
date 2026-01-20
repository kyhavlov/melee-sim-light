#include "api.h"

#include <errno.h>
#include <string.h>

#include "alloc.h"
#include "anim_table.h"
#include "action_ids.h"
#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"
#include "config.h"
#include "stage_collision.h"
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

  if (anim_table_init() != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  return batch;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
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

    batch->state.frame_id[bi] = seed->frame_id;
    batch->state.frame_pre_random_seed[bi] = seed->frame_pre_random_seed;
    batch->state.stage_id[bi] = seed->stage_id;
    batch->state.is_teams[bi] = seed->is_teams ? 1 : 0;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.team_id[idx] = seed->team_id[p];
      batch->state.char_id[idx] = seed->char_id[p];

      batch->state.pos_x[idx] = seed->pos_x[p];
      batch->state.pos_y[idx] = seed->pos_y[p];
      batch->state.speed_air_x_self[idx] = seed->speed_air_x_self[p];
      batch->state.speed_ground_x_self[idx] = seed->speed_ground_x_self[p];
      batch->state.speed_y_self[idx] = seed->speed_y_self[p];
      batch->state.speed_x_attack[idx] = seed->speed_x_attack[p];
      batch->state.speed_y_attack[idx] = seed->speed_y_attack[p];
      batch->state.facing[idx] = seed->facing[p] ? 1 : 0;
      batch->state.on_ground[idx] = seed->on_ground[p] ? 1 : 0;

      batch->state.action_id[idx] = seed->action_id[p];
      batch->state.action_frame[idx] = seed->action_frame[p];
      batch->state.jumps_left[idx] = seed->jumps_left[p];
      batch->state.stocks[idx] = seed->stocks[p];
      batch->state.kneebend_jump_input[idx] = 0;
      batch->state.kneebend_is_short_hop[idx] = 0;
      batch->state.turn_has_turned[idx] = 0;
      batch->state.turn_frames_to_turn[idx] = 0;
      if (seed->action_id[p] == (uint16_t)MSL_ACT_TURN ||
          seed->action_id[p] == (uint16_t)MSL_ACT_TURN_RUN) {
        const MslCharParams* ch = msl_char_params(seed->char_id[p]);
        if (ch != NULL) {
          // Reseed mapping for Turn internals from a teacher-forced `action_frame` snapshot.
          //
          // Decomp:
          // - `ftCo_Turn_Enter_*` initializes `fp->mv.co.turn.frames_to_turn` and `has_turned=false`.
          // - `ftCo_Turn_Enter` calls `ftAnim_8006EBA4(gobj)` immediately after `Fighter_ChangeMotionState`,
          //   advancing `fp->cur_anim_frame` during the entry frame.
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:46-64
          // - `ftCo_Turn_Anim_Inner` decrements `frames_to_turn` once per frame while it is > 0,
          //   then performs a one-time flip when it reaches 0 and `!has_turned`.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:39-44 and :56-88
          //
          // Snapshot/phase note:
          // - `ftAnim_8006EBA4` runs before `fp->anim_cb` in `Fighter_procUpdate`.
          // refs/melee/src/melee/ft/fighter.c:1690-1700
          // - `ftCo_Turn_Enter_*` may call `ftAnim_8006EBA4` immediately on entry (after
          //   `Fighter_ChangeMotionState`), which can advance `state_age` within the entry frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:46-64
          //
          // In teacher-forced one-step eval, the only Turn internals we reseed are:
          // - `frames_to_turn` (countdown)
          // - `has_turned` (one-time flip latch)
          //
          // We interpret a post-frame `state_age` snapshot as having already applied the per-frame
          // animation advance (`ftAnim_8006EBA4`), but not necessarily having advanced the Turn
          // countdown via `ftCo_Turn_Anim_Inner` yet. This means the observed `action_frame` is
          // effectively 1 ahead of the countdown progress (vs a naive `frames_to_turn = tf - af`).
          //
          // Teacher-forcing mapping contract (best-effort):
          // - we must NOT flip early when seeded at `action_frame == turn_frames`.
          // - the earliest allowed flip from this mapping is at `action_frame == turn_frames + 1`,
          //   by reseeding `frames_to_turn` such that the next `ftCo_Turn_Anim_Inner` step performs
          //   the one-time flip when the countdown is at 0 and `!has_turned`.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:74-88
          //
          // Note: some Enter functions call `ftAnim_8006EBA4` immediately (e.g. Turn/Dash), which can
          // shift the relationship between post-frame `state_age` and internal countdowns. Without the
          // full internal history (and without modeling intra-frame callbacks), this mapping is an
          // approximation but remains deterministic.
          const int32_t af = (seed->action_frame[p] < 0) ? 0 : (int32_t)seed->action_frame[p];
          const int32_t tf = (int32_t)ch->turn_frames;

          // Interpret countdown progress as `max(action_frame - 1, 0)`, so:
          // - af=0/1 -> progress=0 (no countdown decrements applied yet)
          // - af=tf+1 -> progress=tf (countdown at 0; flip pending)
          const int32_t progress = (af > 0) ? (af - 1) : 0;

          // Once `action_frame` has advanced beyond the flip-pending point, we assume the flip has
          // already happened and latch `has_turned` to prevent an extra flip.
          if (af > (tf + 1)) {
            batch->state.turn_frames_to_turn[idx] = 0;
            batch->state.turn_has_turned[idx] = 1;
          } else {
            const int32_t rem = tf - progress;
            batch->state.turn_frames_to_turn[idx] = (rem > 0) ? (uint8_t)rem : 0;
            batch->state.turn_has_turned[idx] = 0;
          }
        }
      }

      batch->state.percent[idx] = seed->percent[p];
      batch->state.shield_hp[idx] = seed->shield_hp[p];
      batch->state.hitlag[idx] = seed->hitlag[p];
      batch->state.hitstun[idx] = seed->hitstun[p];
      batch->state.l_cancel[idx] = seed->l_cancel[p];
      batch->state.hurtbox_state[idx] = seed->hurtbox_state[p];
      batch->state.ground_id[idx] = seed->ground_id[p];
      batch->state.animation_index[idx] = seed->animation_index[p];
      batch->state.instance_hit_by[idx] = seed->instance_hit_by[p];
      batch->state.instance_id[idx] = seed->instance_id[p];
      batch->state.last_attack_landed[idx] = seed->last_attack_landed[p];
      batch->state.combo_count[idx] = seed->combo_count[p];
      batch->state.last_hit_by[idx] = seed->last_hit_by[p];

      for (int k = 0; k < 5; k++) {
        batch->state.state_flags[idx * 5 + (size_t)k] = seed->state_flags[p][k];
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
