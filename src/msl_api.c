#include "msl_api.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct MslBatch {
  int batch_size;
  int num_players;

  // Meta
  int32_t* frame_id;
  uint32_t* frame_pre_random_seed;
  uint32_t* stage_id; // [batch]
  uint8_t* is_teams;  // [batch]
  uint8_t* team_id;   // [batch * MSL_MAX_PLAYERS]
  uint8_t* char_id;   // [batch * MSL_MAX_PLAYERS]

  // Kinematics
  float* pos_x;
  float* pos_y;
  float* speed_air_x_self;
  float* speed_ground_x_self;
  float* speed_y_self;
  float* speed_x_attack;
  float* speed_y_attack;
  uint8_t* facing;
  uint8_t* on_ground;

  // State machine
  uint16_t* action_id;
  int16_t* action_frame;
  uint8_t* jumps_left;
  uint8_t* stocks;

  // Combat/timers
  float* percent;
  float* shield_hp;
  uint16_t* hitlag;
  uint16_t* hitstun;
  uint8_t* l_cancel;
  uint8_t* hurtbox_state;
  uint16_t* ground_id;
  uint32_t* animation_index;
  uint16_t* instance_hit_by;
  uint16_t* instance_id;
  uint8_t* last_attack_landed;
  uint8_t* combo_count;
  uint8_t* last_hit_by;
  uint8_t* state_flags; // [batch * players * 5]

  // Items (fixed-capacity, per-batch)
  uint8_t* item_exists;       // [batch * MSL_MAX_ITEMS]
  uint8_t* item_state;        // [batch * MSL_MAX_ITEMS]
  uint16_t* item_type;        // [batch * MSL_MAX_ITEMS]
  int8_t* item_owner;         // [batch * MSL_MAX_ITEMS]
  uint16_t* item_instance_id; // [batch * MSL_MAX_ITEMS]
  float* item_direction;      // [batch * MSL_MAX_ITEMS]
  float* item_vel_x;          // [batch * MSL_MAX_ITEMS]
  float* item_vel_y;          // [batch * MSL_MAX_ITEMS]
  float* item_pos_x;          // [batch * MSL_MAX_ITEMS]
  float* item_pos_y;          // [batch * MSL_MAX_ITEMS]
  uint16_t* item_damage;      // [batch * MSL_MAX_ITEMS]
  float* item_timer;          // [batch * MSL_MAX_ITEMS]
  uint32_t* item_spawn_id;    // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc0;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc1;
  uint8_t* item_misc2;
  uint8_t* item_misc3;
} MslBatch;

static void* msl_aligned_alloc_64(size_t bytes) {
  void* ptr = NULL;
  // posix_memalign requires alignment to be a power-of-two multiple of sizeof(void*)
  if (posix_memalign(&ptr, 64, bytes) != 0) {
    return NULL;
  }
  memset(ptr, 0, bytes);
  return ptr;
}

static size_t msl_players_len(const MslBatch* batch) {
  return (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS;
}

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
  batch->num_players = num_players;

  const size_t b = (size_t)batch_size;
  const size_t bp = b * (size_t)MSL_MAX_PLAYERS;
  const size_t bi = b * (size_t)MSL_MAX_ITEMS;

  batch->frame_id = (int32_t*)msl_aligned_alloc_64(sizeof(int32_t) * b);
  batch->frame_pre_random_seed = (uint32_t*)msl_aligned_alloc_64(sizeof(uint32_t) * b);
  batch->stage_id = (uint32_t*)msl_aligned_alloc_64(sizeof(uint32_t) * b);
  batch->is_teams = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * b);
  batch->team_id = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->char_id = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);

  batch->pos_x = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->pos_y = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->speed_air_x_self = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->speed_ground_x_self = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->speed_y_self = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->speed_x_attack = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->speed_y_attack = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->facing = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->on_ground = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);

  batch->action_id = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->action_frame = (int16_t*)msl_aligned_alloc_64(sizeof(int16_t) * bp);
  batch->jumps_left = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->stocks = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);

  batch->percent = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->shield_hp = (float*)msl_aligned_alloc_64(sizeof(float) * bp);
  batch->hitlag = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->hitstun = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->l_cancel = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->hurtbox_state = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->ground_id = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->animation_index = (uint32_t*)msl_aligned_alloc_64(sizeof(uint32_t) * bp);
  batch->instance_hit_by = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->instance_id = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bp);
  batch->last_attack_landed = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->combo_count = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->last_hit_by = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp);
  batch->state_flags = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bp * 5);

  batch->item_exists = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);
  batch->item_state = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);
  batch->item_type = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bi);
  batch->item_owner = (int8_t*)msl_aligned_alloc_64(sizeof(int8_t) * bi);
  batch->item_instance_id = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bi);
  batch->item_direction = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_vel_x = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_vel_y = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_pos_x = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_pos_y = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_damage = (uint16_t*)msl_aligned_alloc_64(sizeof(uint16_t) * bi);
  batch->item_timer = (float*)msl_aligned_alloc_64(sizeof(float) * bi);
  batch->item_spawn_id = (uint32_t*)msl_aligned_alloc_64(sizeof(uint32_t) * bi);
  batch->item_misc0 = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);
  batch->item_misc1 = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);
  batch->item_misc2 = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);
  batch->item_misc3 = (uint8_t*)msl_aligned_alloc_64(sizeof(uint8_t) * bi);

  if (!batch->frame_id || !batch->frame_pre_random_seed || !batch->stage_id || !batch->is_teams ||
      !batch->team_id || !batch->char_id ||
      !batch->pos_x || !batch->pos_y || !batch->speed_air_x_self || !batch->speed_ground_x_self ||
      !batch->speed_y_self || !batch->speed_x_attack || !batch->speed_y_attack ||
      !batch->facing || !batch->on_ground || !batch->action_id || !batch->action_frame ||
      !batch->jumps_left || !batch->stocks || !batch->percent || !batch->shield_hp ||
      !batch->hitlag || !batch->hitstun || !batch->l_cancel || !batch->hurtbox_state ||
      !batch->ground_id || !batch->animation_index || !batch->instance_hit_by || !batch->instance_id ||
      !batch->last_attack_landed || !batch->combo_count || !batch->last_hit_by || !batch->state_flags ||
      !batch->item_exists || !batch->item_state || !batch->item_type || !batch->item_owner ||
      !batch->item_instance_id || !batch->item_direction || !batch->item_vel_x || !batch->item_vel_y ||
      !batch->item_pos_x || !batch->item_pos_y || !batch->item_damage || !batch->item_timer ||
      !batch->item_spawn_id || !batch->item_misc0 || !batch->item_misc1 || !batch->item_misc2 || !batch->item_misc3) {
    msl_batch_destroy(batch);
    return NULL;
  }

  return batch;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  free(batch->frame_id);
  free(batch->frame_pre_random_seed);
  free(batch->stage_id);
  free(batch->is_teams);
  free(batch->team_id);
  free(batch->char_id);

  free(batch->pos_x);
  free(batch->pos_y);
  free(batch->speed_air_x_self);
  free(batch->speed_ground_x_self);
  free(batch->speed_y_self);
  free(batch->speed_x_attack);
  free(batch->speed_y_attack);
  free(batch->facing);
  free(batch->on_ground);

  free(batch->action_id);
  free(batch->action_frame);
  free(batch->jumps_left);
  free(batch->stocks);

  free(batch->percent);
  free(batch->shield_hp);
  free(batch->hitlag);
  free(batch->hitstun);
  free(batch->l_cancel);
  free(batch->hurtbox_state);
  free(batch->ground_id);
  free(batch->animation_index);
  free(batch->instance_hit_by);
  free(batch->instance_id);
  free(batch->last_attack_landed);
  free(batch->combo_count);
  free(batch->last_hit_by);
  free(batch->state_flags);

  free(batch->item_exists);
  free(batch->item_state);
  free(batch->item_type);
  free(batch->item_owner);
  free(batch->item_instance_id);
  free(batch->item_direction);
  free(batch->item_vel_x);
  free(batch->item_vel_y);
  free(batch->item_pos_x);
  free(batch->item_pos_y);
  free(batch->item_damage);
  free(batch->item_timer);
  free(batch->item_spawn_id);
  free(batch->item_misc0);
  free(batch->item_misc1);
  free(batch->item_misc2);
  free(batch->item_misc3);

  free(batch);
}

int msl_batch_batch_size(const MslBatch* batch) {
  return batch ? batch->batch_size : 0;
}

int msl_batch_num_players(const MslBatch* batch) {
  return batch ? batch->num_players : 0;
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

  const size_t bp = msl_players_len(batch);
  (void)bp;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* ptr = seed_bytes + (size_t)bi * seed_stride_bytes;
    const MslSeedV0* seed = (const MslSeedV0*)ptr;

    batch->frame_id[bi] = seed->frame_id;
    batch->frame_pre_random_seed[bi] = seed->frame_pre_random_seed;
    batch->stage_id[bi] = seed->stage_id;
    batch->is_teams[bi] = seed->is_teams ? 1 : 0;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = (size_t)bi * MSL_MAX_PLAYERS + (size_t)p;
      batch->team_id[idx] = seed->team_id[p];
      batch->char_id[idx] = seed->char_id[p];

      batch->pos_x[idx] = seed->pos_x[p];
      batch->pos_y[idx] = seed->pos_y[p];
      batch->speed_air_x_self[idx] = seed->speed_air_x_self[p];
      batch->speed_ground_x_self[idx] = seed->speed_ground_x_self[p];
      batch->speed_y_self[idx] = seed->speed_y_self[p];
      batch->speed_x_attack[idx] = seed->speed_x_attack[p];
      batch->speed_y_attack[idx] = seed->speed_y_attack[p];
      batch->facing[idx] = seed->facing[p] ? 1 : 0;
      batch->on_ground[idx] = seed->on_ground[p] ? 1 : 0;

      batch->action_id[idx] = seed->action_id[p];
      batch->action_frame[idx] = seed->action_frame[p];
      batch->jumps_left[idx] = seed->jumps_left[p];
      batch->stocks[idx] = seed->stocks[p];

      batch->percent[idx] = seed->percent[p];
      batch->shield_hp[idx] = seed->shield_hp[p];
      batch->hitlag[idx] = seed->hitlag[p];
      batch->hitstun[idx] = seed->hitstun[p];
      batch->l_cancel[idx] = seed->l_cancel[p];
      batch->hurtbox_state[idx] = seed->hurtbox_state[p];
      batch->ground_id[idx] = seed->ground_id[p];
      batch->animation_index[idx] = seed->animation_index[p];
      batch->instance_hit_by[idx] = seed->instance_hit_by[p];
      batch->instance_id[idx] = seed->instance_id[p];
      batch->last_attack_landed[idx] = seed->last_attack_landed[p];
      batch->combo_count[idx] = seed->combo_count[p];
      batch->last_hit_by[idx] = seed->last_hit_by[p];

      for (int k = 0; k < 5; k++) {
        batch->state_flags[idx * 5 + (size_t)k] = seed->state_flags[p][k];
      }
    }

    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = (size_t)bi * MSL_MAX_ITEMS + (size_t)it;
      const MslItemV0* item = &seed->items[it];
      batch->item_exists[ii] = item->exists;
      batch->item_state[ii] = item->state;
      batch->item_type[ii] = item->type;
      batch->item_owner[ii] = item->owner;
      batch->item_instance_id[ii] = item->instance_id;
      batch->item_direction[ii] = item->direction;
      batch->item_vel_x[ii] = item->vel_x;
      batch->item_vel_y[ii] = item->vel_y;
      batch->item_pos_x[ii] = item->pos_x;
      batch->item_pos_y[ii] = item->pos_y;
      batch->item_damage[ii] = item->damage;
      batch->item_timer[ii] = item->timer;
      batch->item_spawn_id[ii] = item->spawn_id;
      batch->item_misc0[ii] = item->misc0;
      batch->item_misc1[ii] = item->misc1;
      batch->item_misc2[ii] = item->misc2;
      batch->item_misc3[ii] = item->misc3;
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
  if (batch == NULL) {
    return EINVAL;
  }
  // Empty sim: ignore inputs. Still validate pointer/stride to catch integration bugs.
  if (prev_input_bytes == NULL || input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInputV0) || input_stride_bytes < sizeof(MslInputV0)) {
    return EINVAL;
  }
  return 0;
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

    out->frame_id = batch->frame_id[bi];
    out->frame_pre_random_seed = batch->frame_pre_random_seed[bi];
    out->stage_id = batch->stage_id[bi];
    out->num_players = (uint8_t)batch->num_players;
    out->is_teams = batch->is_teams[bi] ? 1 : 0;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = (size_t)bi * MSL_MAX_PLAYERS + (size_t)p;
      out->team_id[p] = batch->team_id[idx];
      out->char_id[p] = batch->char_id[idx];

      out->pos_x[p] = batch->pos_x[idx];
      out->pos_y[p] = batch->pos_y[idx];
      out->speed_air_x_self[p] = batch->speed_air_x_self[idx];
      out->speed_ground_x_self[p] = batch->speed_ground_x_self[idx];
      out->speed_y_self[p] = batch->speed_y_self[idx];
      out->speed_x_attack[p] = batch->speed_x_attack[idx];
      out->speed_y_attack[p] = batch->speed_y_attack[idx];
      out->facing[p] = batch->facing[idx] ? 1 : 0;
      out->on_ground[p] = batch->on_ground[idx] ? 1 : 0;

      out->action_id[p] = batch->action_id[idx];
      out->action_frame[p] = batch->action_frame[idx];
      out->jumps_left[p] = batch->jumps_left[idx];
      out->stocks[p] = batch->stocks[idx];
      out->is_dead[p] = msl_is_dead_from_stocks(batch->stocks[idx]);

      out->percent[p] = batch->percent[idx];
      out->shield_hp[p] = batch->shield_hp[idx];
      out->hitlag[p] = batch->hitlag[idx];
      out->hitstun[p] = batch->hitstun[idx];
      out->l_cancel[p] = batch->l_cancel[idx];
      out->hurtbox_state[p] = batch->hurtbox_state[idx];
      out->ground_id[p] = batch->ground_id[idx];
      out->animation_index[p] = batch->animation_index[idx];
      out->instance_hit_by[p] = batch->instance_hit_by[idx];
      out->instance_id[p] = batch->instance_id[idx];
      out->last_attack_landed[p] = batch->last_attack_landed[idx];
      out->combo_count[p] = batch->combo_count[idx];
      out->last_hit_by[p] = batch->last_hit_by[idx];

      for (int k = 0; k < 5; k++) {
        out->state_flags[p][k] = batch->state_flags[idx * 5 + (size_t)k];
      }
    }

    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = (size_t)bi * MSL_MAX_ITEMS + (size_t)it;
      MslItemV0* item = &out->items[it];
      item->exists = batch->item_exists[ii];
      item->state = batch->item_state[ii];
      item->type = batch->item_type[ii];
      item->owner = batch->item_owner[ii];
      item->instance_id = batch->item_instance_id[ii];
      item->direction = batch->item_direction[ii];
      item->vel_x = batch->item_vel_x[ii];
      item->vel_y = batch->item_vel_y[ii];
      item->pos_x = batch->item_pos_x[ii];
      item->pos_y = batch->item_pos_y[ii];
      item->damage = batch->item_damage[ii];
      item->timer = batch->item_timer[ii];
      item->spawn_id = batch->item_spawn_id[ii];
      item->misc0 = batch->item_misc0[ii];
      item->misc1 = batch->item_misc1[ii];
      item->misc2 = batch->item_misc2[ii];
      item->misc3 = batch->item_misc3[ii];
    }
  }

  return 0;
}
