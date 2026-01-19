#include "msl_api.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "msl_batch_internal.h"
#include "msl_config.h"
#include "msl_state.h"
#include "msl_step.h"

MslBatch* msl_batch_create(int batch_size, int num_players) {
  if (batch_size <= 0) {
    return NULL;
  }
  if (!(num_players == 2 || num_players == 4)) {
    return NULL;
  }

  MslBatch* batch = (MslBatch*)calloc(1, sizeof(MslBatch));
  if (batch == NULL) {
    return NULL;
  }

  batch->batch_size = batch_size;
  msl_config_default(&batch->config, num_players);

  if (msl_state_alloc(&batch->state, batch_size) != 0) {
    msl_batch_destroy(batch);
    return NULL;
  }

  return batch;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  msl_state_free(&batch->state);
  free(batch);
}

int msl_batch_batch_size(const MslBatch* batch) {
  return batch ? batch->batch_size : 0;
}

int msl_batch_num_players(const MslBatch* batch) {
  return batch ? (int)batch->config.num_players : 0;
}

int msl_batch_reseed_seed_v0(
    MslBatch* batch,
    const uint8_t* seed_bytes,
    size_t seed_stride_bytes) {
  if (batch == NULL || seed_bytes == NULL) {
    return EINVAL;
  }
  if (seed_stride_bytes < sizeof(MslSeedV0)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* ptr = seed_bytes + (size_t)bi * seed_stride_bytes;
    const MslSeedV0* seed = (const MslSeedV0*)ptr;

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
      const MslItemV0* item = &seed->items[it];
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

int msl_batch_step_input_v0(
    MslBatch* batch,
    const uint8_t* prev_input_bytes,
    size_t prev_input_stride_bytes,
    const uint8_t* input_bytes,
    size_t input_stride_bytes) {
  return msl_step_one_frame_v0(
      batch,
      prev_input_bytes,
      prev_input_stride_bytes,
      input_bytes,
      input_stride_bytes);
}

static uint8_t msl_is_dead_from_stocks(uint8_t stocks) {
  return stocks == 0 ? 1 : 0;
}

int msl_batch_write_compare_v0(
    const MslBatch* batch,
    uint8_t* out_bytes,
    size_t out_stride_bytes) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslCompareV0)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t* ptr = out_bytes + (size_t)bi * out_stride_bytes;
    MslCompareV0* out = (MslCompareV0*)ptr;
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
      MslItemV0* item = &out->items[it];
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

