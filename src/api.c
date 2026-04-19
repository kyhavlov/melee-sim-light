#include "api.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "anim_pose.h"
#include "action_ids.h"
#include "batch_internal.h"
#include "items.h"
#include "combat.h"
#include "combat_geom.h"
#include "char_params.h"
#include "common_params.h"
#include "config.h"
#include "special_msids.h"
#include "move_tables.h"
#include "match_flow.h"
#include "ecb_tables.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "hit_status_tables.h"
#include "msl_math.h"
#include "airborne_state_events_tables.h"
#include "state_flags_221c_y_tables.h"
#include "hitboxes.h"
#include "hurtbox_modes_tables.h"
#include "hurtcaps_tables.h"
#include "hurtboxes.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "laser_params.h"
#include "stage_collision.h"
#include "staling.h"
#include "staling_tables.h"
#include "attack_id_tables.h"
#include "state.h"
#include "state_flags.h"
#include "step.h"
#include "grab_attachment.h"
#include "knockdown.h"

static inline uint16_t item_seed_bridge_attack_id(const MslBatch* batch, int bi,
                                                  const MslItem* item) {
  if (batch == NULL || item == NULL) {
    return (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  }
  uint16_t attack_id = item->attack_id;
  if (attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT || !item->exists) {
    return attack_id;
  }
  // Seed bridge for persisted fighter-spawned projectiles/articles:
  // - Decomp item spawn copies the owner's fighter-side attack id into the article
  //   (`item->xD88_attackID = fighter->x2068_attackID`) at spawn.
  // - Slippi item post-frame does not preserve that move-id ownership for persisted Fox/Falco
  //   blaster shots / illusion articles; teacher-forced reseed commonly snapshots them with
  //   attack_id==Default, which disables decomp-shaped staling on shield/body contact.
  // - Recover the move id from the owner's action-move table for the supported projectile/article
  //   families below.
  // refs/melee/src/melee/it/it_2725.c::it_8027B070
  if (item->owner < 0 || item->owner >= (int8_t)batch->config.num_players) {
    return attack_id;
  }
  const size_t owner_idx = msl_idx_player(bi, (int)item->owner);
  if (laser_params_for_item_type(item->type) != NULL) {
    // data/attack_id/move_id/{fox,falco}.bin: action 341/342/343 -> move_id 18
    return attack_id_move_id_from_action(batch->state.char_id[owner_idx],
                                         (uint16_t)MSL_ACT_FX_SPECIAL_N_LOOP);
  }
  if (item_type_is_illusion_article(item->type)) {
    // data/attack_id/move_id/{fox,falco}.bin: action 347/348/349 -> move_id 19
    return attack_id_move_id_from_action(batch->state.char_id[owner_idx],
                                         (uint16_t)MSL_ACT_FX_SPECIAL_S);
  }
  return attack_id;
}

static inline uint16_t colanim_timer_remaining_from_action_frame(uint16_t init_frames,
                                                                 int16_t action_frame) {
  if (init_frames == 0u) {
    return 0u;
  }
  if (action_frame <= 0) {
    return init_frames;
  }
  // Seed bridge: entry frame is action_frame==1 for states that set x1990/x1994 in their
  // ChangeMotionState path; the timer has not decremented yet for that post-frame snapshot.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (x1990/x1994 decrement pass)
  int rem = (int)init_frames + 1 - (int)action_frame;
  if (rem < 0) {
    rem = 0;
  }
  if (rem > 0xFFFF) {
    rem = 0xFFFF;
  }
  return (uint16_t)rem;
}

static inline uint16_t colanim_timer_remaining_from_seed_bridge(uint16_t init_frames,
                                                                int16_t action_frame,
                                                                float anim_frame_f32,
                                                                float frame_speed_mul_f32) {
  // Seed-bridge inference for x1990/x1994 timers:
  // - Decomp decrements x1990/x1994 once per frame in Fighter_8006A360.
  // - Decomp advances cur_anim_frame by frame_speed_mul in ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  //
  // Slippi does not expose x1990/x1994 directly. Recover remaining timer from snapshot-visible
  // animation progress by estimating elapsed frames as round(cur_anim_frame / frame_speed_mul),
  // while keeping action_frame-based fallback for degenerate rates.
  uint16_t rem = colanim_timer_remaining_from_action_frame(init_frames, action_frame);
  const float rate = fabsf(frame_speed_mul_f32);
  if (!(rate > 0.0f) || !isfinite(rate)) {
    return rem;
  }
  const float af = msl_anim_frame_sanitize_f32(anim_frame_f32);
  int elapsed = (int)floorf((af / rate) + 0.5f);
  if (elapsed < 0) {
    elapsed = 0;
  }
  int rem_est = (int)init_frames + 1 - elapsed;
  if (rem_est < 0) {
    rem_est = 0;
  }
  if (rem_est > 0xFFFF) {
    rem_est = 0xFFFF;
  }
  if ((uint16_t)rem_est > rem) {
    rem = (uint16_t)rem_est;
  }
  return rem;
}

static inline uint8_t seed_bridge_has_shine_start_x1988_masked_x198c(const MslSeed* seed, int p,
                                                                     uint8_t seed_hurtbox_state,
                                                                     uint8_t seed_x1988) {
  if (seed == NULL) {
    return 0u;
  }
  if (seed->action_id[p] != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
      seed->action_id[p] != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  if (seed->action_frame[p] != 1 || seed_hurtbox_state != 2u || seed_x1988 != 2u) {
    return 0u;
  }
  if (seed->colanim_hit_status_x198c[p] != 1u || seed->colanim_timer_x1990[p] != 0u ||
      seed->colanim_timer_x1994[p] != 0u || seed->colanim_lock_x2221_b0[p] != 0u) {
    return 0u;
  }
  // Shine Start frame 1 can be reseeded at the entry-origin boundary while visible Slippi
  // hurtbox_state is still the movescript x1988=2 value from the entry script. When seed-history
  // reconstruction proves a hidden x198C=1 lane underneath that x1988 mask, preserve it so the
  // next cmd-script clear falls back to x198C. Same-action hitlag-frozen Shine starts are admitted
  // only when seed-history carried that explicit x198C=1 provenance forward from the causal entry
  // row; controls with x198C=0 still clear to vulnerable.
  //
  // Keep this as a seed-surface reconstruction, not a broad hidden-colanim clear:
  // - data/moves/{fox,falco}.json specials_by_msid["313"/"317"] has set_hit_status 2 at frame 0
  //   and set_hit_status 0 at frame 2.
  // - SendGamePostFrame reports x1988 first, then x198C.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B868}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  return 1u;
}

static inline int throw_index_from_action_id(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROW_F:
      return 0;
    case (uint16_t)MSL_ACT_THROW_B:
      return 1;
    case (uint16_t)MSL_ACT_THROW_HI:
      return 2;
    case (uint16_t)MSL_ACT_THROW_LW:
      return 3;
    default:
      return -1;
  }
}

static inline uint16_t throw_action_from_thrown_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROWN_F:
      return (uint16_t)MSL_ACT_THROW_F;
    case (uint16_t)MSL_ACT_THROWN_B:
      return (uint16_t)MSL_ACT_THROW_B;
    case (uint16_t)MSL_ACT_THROWN_HI:
      return (uint16_t)MSL_ACT_THROW_HI;
    case (uint16_t)MSL_ACT_THROWN_LW:
      return (uint16_t)MSL_ACT_THROW_LW;
    default:
      return 0xFFFFu;
  }
}

static inline int32_t throw_anim_rate_fp_from_pair(const MslBatch* batch, size_t owner_idx,
                                                   size_t victim_idx, uint16_t throw_action) {
  if (batch == NULL) {
    return 0;
  }
  const int throw_index = throw_index_from_action_id(throw_action);
  if (throw_index < 0) {
    return 0;
  }
  float throw_anim_speed = 1.0f;
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* owner_ch = msl_char_params(batch->state.char_id[owner_idx]);
  const MslCharParams* victim_ch = msl_char_params(batch->state.char_id[victim_idx]);
  const uint8_t weight_independent =
      (owner_ch != NULL)
          ? ((owner_ch->weight_independent_throws_mask & (uint8_t)(1u << throw_index)) ? 1u : 0u)
          : 0u;
  if (!weight_independent && c != NULL && victim_ch != NULL && victim_ch->weight > 0.0f &&
      c->throw_anim_speed_weight_mul > 0.0f) {
    throw_anim_speed = 1.0f / (victim_ch->weight * c->throw_anim_speed_weight_mul);
    if (!(throw_anim_speed > 0.0f)) {
      throw_anim_speed = 1.0f;
    }
  }
  return msl_q16_16_from_f32(throw_anim_speed);
}

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

  batch->match_init_seed_scratch = (MslSeed*)alloc_malloc(sizeof(MslSeed) * (size_t)batch_size);
  if (batch->match_init_seed_scratch == NULL) {
    msl_batch_destroy(batch);
    return NULL;
  }
  batch->rollout_clock_rng_owned = (uint8_t*)alloc_malloc((size_t)batch_size * sizeof(uint8_t));
  if (batch->rollout_clock_rng_owned == NULL) {
    msl_batch_destroy(batch);
    return NULL;
  }
  memset(batch->rollout_clock_rng_owned, 0, (size_t)batch_size * sizeof(uint8_t));

  // Debug-only per-fighter hit status override table (0xFF = none).
  batch->debug_hit_status_override =
      (uint8_t*)alloc_malloc((size_t)batch_size * (size_t)MSL_MAX_PLAYERS * sizeof(uint8_t));
  if (batch->debug_hit_status_override == NULL) {
    msl_batch_destroy(batch);
    return NULL;
  }
  memset(batch->debug_hit_status_override, 0xFF,
         (size_t)batch_size * (size_t)MSL_MAX_PLAYERS * sizeof(uint8_t));

  batch->debug_rng_shadow_seed = (uint32_t*)alloc_malloc((size_t)batch_size * sizeof(uint32_t));
  batch->debug_rng_seed_in = (uint32_t*)alloc_malloc((size_t)batch_size * sizeof(uint32_t));
  batch->debug_rng_seed_out = (uint32_t*)alloc_malloc((size_t)batch_size * sizeof(uint32_t));
  batch->debug_rng_site_counts =
      (uint16_t*)alloc_malloc((size_t)batch_size * (size_t)MSL_RNG_SITE_COUNT * sizeof(uint16_t));
  if (batch->debug_rng_shadow_seed == NULL || batch->debug_rng_seed_in == NULL ||
      batch->debug_rng_seed_out == NULL || batch->debug_rng_site_counts == NULL) {
    msl_batch_destroy(batch);
    return NULL;
  }
  memset(batch->debug_rng_shadow_seed, 0, (size_t)batch_size * sizeof(uint32_t));
  memset(batch->debug_rng_seed_in, 0, (size_t)batch_size * sizeof(uint32_t));
  memset(batch->debug_rng_seed_out, 0, (size_t)batch_size * sizeof(uint32_t));
  memset(batch->debug_rng_site_counts, 0,
         (size_t)batch_size * (size_t)MSL_RNG_SITE_COUNT * sizeof(uint16_t));

  batch->debug_rng_enable_damage_fly_roll_gate = 0u;
  const char* rng_gate_env = getenv("MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE");
  if (rng_gate_env != NULL && rng_gate_env[0] == '1') {
    batch->debug_rng_enable_damage_fly_roll_gate = 1u;
  }
  batch->debug_rng_disable_pseudo_random_sfx_cmd = 0u;
  const char* rng_pseudo_sfx_env = getenv("MSL_RNG_DISABLE_PSEUDO_RANDOM_SFX_CMD");
  if (rng_pseudo_sfx_env != NULL && rng_pseudo_sfx_env[0] == '1') {
    batch->debug_rng_disable_pseudo_random_sfx_cmd = 1u;
  }
  batch->debug_rng_trace_enabled = 0u;
  batch->debug_rng_trace_file = NULL;
  batch->debug_rng_trace_step_counter = 0u;
  const char* rng_trace_path = getenv("MSL_RNG_TRACE_PATH");
  if (rng_trace_path != NULL && rng_trace_path[0] != '\0') {
    FILE* rng_trace_file = fopen(rng_trace_path, "w");
    if (rng_trace_file != NULL) {
      batch->debug_rng_trace_enabled = 1u;
      batch->debug_rng_trace_file = (void*)rng_trace_file;
      (void)fprintf(rng_trace_file,
                    "step\tbatch_index\tframe_id\tseed_in\tseed_out\tsite_id\tcall_count\n");
    }
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

  if (special_msids_init() != 0) {
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

  // Optional timeline table (x221C_u16_y opcode-52 lane): missing artifacts should not prevent
  // running.
  (void)state_flags_221c_y_tables_init();
  // Optional movescript opcode-25 table (set_airborne_state timeline).
  (void)airborne_state_events_tables_init();

  return batch;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  if (batch->debug_rng_trace_file != NULL) {
    (void)fclose((FILE*)batch->debug_rng_trace_file);
  }
  alloc_free(batch->debug_rng_site_counts);
  alloc_free(batch->debug_rng_seed_out);
  alloc_free(batch->debug_rng_seed_in);
  alloc_free(batch->debug_rng_shadow_seed);
  alloc_free(batch->debug_hit_status_override);
  alloc_free(batch->rollout_clock_rng_owned);
  alloc_free(batch->match_init_seed_scratch);
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

enum {
  // Source: data/stages/final_destination.json is extracted from _iso/GrNLa.dat and uses
  // GALE01/Slippi stage id 32 for Final Destination in this simulator's target domain.
  MSL_STAGE_FINAL_DESTINATION = 32,
  // Character id mapping follows Slippi post-frame `character` (GALE01):
  // - Fox   = 1
  // - Falco = 22
  MSL_CHAR_FOX = 1,
  MSL_CHAR_FALCO = 22,
  // Slippi post-frame sentinel for no animation/submotion.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  MSL_ANIM_NONE_U32 = 0xFFFFFFFFu,
};

static inline uint8_t msl_match_init_supported_char(uint8_t char_id) {
  return (uint8_t)(char_id == (uint8_t)MSL_CHAR_FOX || char_id == (uint8_t)MSL_CHAR_FALCO);
}

static inline uint8_t msl_match_init_point_inside_bounds(const MslStageBounds* b, float x,
                                                         float y) {
  if (b == NULL) {
    return 0u;
  }
  return (uint8_t)(x >= b->left && x <= b->right && y >= b->bottom && y <= b->top);
}

static uint8_t msl_match_init_slippi_neutral_spawn_point(const MslMatchConfig* cfg,
                                                         int active_players, int player,
                                                         MslStagePoint2* out, uint8_t* out_facing) {
  if (cfg == NULL || out == NULL || out_facing == NULL) {
    return 0u;
  }
  if (!cfg->is_teams || active_players != 4 ||
      cfg->stage_id != (uint32_t)MSL_STAGE_FINAL_DESTINATION) {
    return 0u;
  }

  // Slippi neutral-spawn teams mode only applies to 2v2, grouping slots by team id 0..2 and
  // assigning spawn ids from that grouped order. FD teams coordinates are copied from the patch's
  // FD `Teams Data` table; facing is then derived from x <= 0 exactly like `SetSpawn_UpdateFacingDirection`.
  // refs/slippi-ssbm-asm/External/NeutralSpawn/NeutralSpawn.asm::{isTeams,SetSpawn,NeutralSpawnTable}
  uint8_t team_counts[3] = {0u, 0u, 0u};
  for (int p = 0; p < active_players; p++) {
    const uint8_t team_id = cfg->players[p].team_id;
    if (team_id >= 3u) {
      return 0u;
    }
    team_counts[team_id]++;
  }
  for (int team_id = 0; team_id < 3; team_id++) {
    if (team_counts[team_id] == 1u || team_counts[team_id] > 2u) {
      return 0u;
    }
  }

  uint8_t team_order[MSL_MAX_PLAYERS] = {0u, 0u, 0u, 0u};
  int team_order_size = 0;
  for (int team_id = 0; team_id < 3; team_id++) {
    for (int p = 0; p < active_players; p++) {
      if (cfg->players[p].team_id == (uint8_t)team_id) {
        team_order[team_order_size++] = (uint8_t)p;
      }
    }
  }
  if (team_order_size != active_players) {
    return 0u;
  }

  int spawn_id = -1;
  for (int i = 0; i < team_order_size; i++) {
    if (team_order[i] == (uint8_t)player) {
      spawn_id = i;
      break;
    }
  }
  if (spawn_id < 0) {
    return 0u;
  }

  static const MslStagePoint2 fd_teams_spawn_points[MSL_MAX_PLAYERS] = {
      {-60.0f, 10.0f}, {-20.0f, 10.0f}, {60.0f, 10.0f}, {20.0f, 10.0f}};
  *out = fd_teams_spawn_points[spawn_id];
  *out_facing = out->x <= 0.0f ? 1u : 0u;
  return 1u;
}

static inline uint8_t msl_mask_row_selected(const uint8_t* mask_bytes, size_t mask_stride_bytes,
                                            int bi) {
  if (mask_bytes == NULL) {
    return 1u;
  }
  return mask_bytes[(size_t)bi * mask_stride_bytes] ? 1u : 0u;
}

static int msl_batch_reseed_seed_impl(MslBatch* batch, const uint8_t* seed_bytes,
                                      size_t seed_stride_bytes, const uint8_t* mask_bytes,
                                      size_t mask_stride_bytes, uint8_t rollout_owned_after);

static int msl_batch_init_match_impl(MslBatch* batch, const uint8_t* config_bytes,
                                     size_t config_stride_bytes, const uint8_t* mask_bytes,
                                     size_t mask_stride_bytes) {
  if (batch == NULL || config_bytes == NULL) {
    return EINVAL;
  }
  if (config_stride_bytes < sizeof(MslMatchConfig)) {
    return EINVAL;
  }
  if (mask_bytes != NULL && mask_stride_bytes < sizeof(uint8_t)) {
    return EINVAL;
  }

  const int active_players = (int)batch->config.num_players;
  if (!(active_players == 2 || active_players == 4)) {
    return EINVAL;
  }

  MslSeed* seeds = batch->match_init_seed_scratch;
  if (seeds == NULL) {
    return EINVAL;
  }
  memset(seeds, 0, sizeof(MslSeed) * (size_t)batch->batch_size);

  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    return ENOENT;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (!msl_mask_row_selected(mask_bytes, mask_stride_bytes, bi)) {
      continue;
    }
    const uint8_t* ptr = config_bytes + (size_t)bi * config_stride_bytes;
    const MslMatchConfig* cfg = (const MslMatchConfig*)ptr;
    MslSeed* seed = &seeds[bi];

    if (cfg->stage_id != (uint32_t)MSL_STAGE_FINAL_DESTINATION) {
      return EINVAL;
    }
    if ((int)cfg->num_players != active_players) {
      return EINVAL;
    }
    if (cfg->stock_count == 0u) {
      return EINVAL;
    }
    if (!(cfg->match_damage_ratio > 0.0f) || !isfinite(cfg->match_damage_ratio)) {
      return EINVAL;
    }

    MslStageBounds cam_bounds = {0};
    if (!stage_collision_get_cam_bounds_world(cfg->stage_id, &cam_bounds)) {
      return ENOENT;
    }

    seed->frame_id = cfg->frame_id;
    seed->frame_pre_random_seed = cfg->frame_pre_random_seed;
    seed->stage_id = cfg->stage_id;
    seed->match_damage_ratio = cfg->match_damage_ratio;
    seed->num_players = cfg->num_players;
    seed->is_teams = cfg->is_teams ? 1u : 0u;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      seed->grab_owner_port[p] = 0xFFu;
      seed->combo_victim_port[p] = 0xFFu;
      seed->source_port0[p] = (uint8_t)p;
      // Decomp source-owner sentinel: Fighter_UnkInitReset_80067C98 clears source ply to 6.
      // refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
      seed->last_hit_by[p] = 6u;
      seed->fighter_scale_y[p] = 1.0f;
      // Normal no-handicap per-player ratios:
      // refs/melee/src/melee/pl/player.c::{Player_GetAttackRatio,Player_GetDefenseRatio}
      seed->attack_ratio[p] = 1.0f;
      seed->defense_ratio[p] = 1.0f;
      // Default grounded KB friction multiplier lane:
      // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
      seed->ground_friction_mul[p] = 1.0f;
      // Ground id sentinel used for airborne / no-ground contact snapshots.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      seed->ground_id[p] = 0xFFFFu;
      seed->animation_index[p] = (uint32_t)MSL_ANIM_NONE_U32;
      seed->anim_frame_f32[p] = -1.0f;
      seed->frame_speed_mul_f32[p] = 0.0f;
      seed->attack_id[p] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
    }

    for (int p = 0; p < active_players; p++) {
      const MslMatchPlayerConfig* pc = &cfg->players[p];
      if (!msl_match_init_supported_char(pc->char_id)) {
        return EINVAL;
      }
      if (pc->facing > 1u) {
        return EINVAL;
      }

      MslStagePoint2 spawn = {0};
      MslStagePoint2 respawn = {0};
      uint8_t spawn_facing = pc->facing ? 1u : 0u;
      if (!msl_match_init_slippi_neutral_spawn_point(cfg, active_players, p, &spawn,
                                                     &spawn_facing) &&
          !stage_collision_get_spawn_point(cfg->stage_id, p, &spawn)) {
        return ENOENT;
      }
      if (!stage_collision_get_respawn_point(cfg->stage_id, p, &respawn)) {
        return ENOENT;
      }

      const MslCharParams* ch = msl_char_params(pc->char_id);
      if (ch == NULL) {
        return ENOENT;
      }

      seed->team_id[p] = pc->team_id;
      seed->char_id[p] = pc->char_id;
      // Normal no-handicap per-player ratios:
      // refs/melee/src/melee/pl/player.c::{Player_GetAttackRatio,Player_GetDefenseRatio}
      seed->attack_ratio[p] = 1.0f;
      seed->defense_ratio[p] = 1.0f;
      seed->pos_x[p] = spawn.x;
      seed->pos_y[p] = spawn.y;
      // Final Destination singles starts on the 2D plane; spawn_points are 2D stage points.
      // data/stages/final_destination.json: spawn_points
      seed->pos_z[p] = 0.0f;
      // Decomp: fp->x34_scale.y is initialized from Player_GetModelScale, while
      // fp->co_attrs.model_scaling is a separate character attr lane applied by specific
      // subsystems (pose/hurtbox/hitbox scaling, not the base fighter scale itself).
      //
      // Normal match start uses the vanilla player model scale of 1.0f:
      // - player static init sets player->model_scale = 1.0f
      // - match setup writes Player_SetModelScale(slot, match_info->x20); normal versus/opening
      //   flow uses the default scale lane, not co_attrs.model_scaling
      // refs/melee/src/melee/pl/player.c::{Player_GetModelScale,Player_80031EF8}
      // refs/melee/src/melee/gm/gm_16AE.c::fn_8016D71C
      // refs/melee/src/melee/ft/fighter.c
      seed->fighter_scale_y[p] = 1.0f;
      seed->facing[p] = spawn_facing;
      seed->facing_dir1[p] = spawn_facing ? (int8_t)1 : (int8_t)-1;
      // Default grounded KB friction multiplier lane:
      // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
      seed->ground_friction_mul[p] = 1.0f;
      seed->on_ground[p] = 0u;

      // Vanilla match start enters the common Entry motion state, with an invisible/no-submotion
      // timebase and a per-port Player unk4C delay before EntryStart.
      //
      // Decomp:
      // - refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC (adds 5, Player_SetUnk4C)
      // - refs/melee/src/melee/pl/player.c::Player_GetUnk4C
      // - refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C61B0
      seed->action_id[p] = (uint16_t)MSL_ACT_ENTRY;
      // Entry has no submotion; the suite observes frozen action_frame/state_age=-1 for Entry.
      // refs/melee/src/melee/ft/ft_0C31.c::ftCo_Entry_Anim
      seed->action_frame[p] = -1;
      seed->seed_prev_action_id[p] = (uint16_t)MSL_ACT_ENTRY;
      seed->seed_prev_action_frame[p] = -1;
      seed->match_flow_timer[p] = (uint8_t)(5 * (p + 1));
      seed->opening_input_lock_timer[p] = match_flow_sim_init_opening_input_lock_timer();
      seed->entry_end_fall_lock[p] = 0u;
      seed->animation_index[p] = (uint32_t)MSL_ANIM_NONE_U32;
      seed->anim_frame_f32[p] = -1.0f;
      seed->frame_speed_mul_f32[p] = 0.0f;
      seed->jumps_left[p] = ch->max_jumps;
      seed->stocks[p] = cfg->stock_count;
      seed->percent[p] = 0.0f;
      // Source: data/common/ft_common_data.json `start_shield_health`
      // Decomp: p_ftCommonData->x260, Guard/Fighter shield-health initialization.
      seed->shield_hp[p] = common->start_shield_health;
      seed->hurtbox_state[p] = 0u;
      seed->rebirth_camera_anchor_y_f32[p] = respawn.y;
      seed->camera_target_world_x_f32[p] = spawn.x;
      seed->camera_target_world_y_f32[p] = spawn.y;
      seed->camera_target_world_z_f32[p] = 0.0f;
      // TODO(data): char_params does not yet expose data/characters/{fox,falco}.json
      // `camera_box_radius`; leave the promoted seed lane empty until the loader owns that key.
      seed->camera_box_radius_f32[p] = 0.0f;
      seed->camera_target_point_inside_stage_cam_bounds_u8[p] =
          msl_match_init_point_inside_bounds(&cam_bounds, spawn.x, spawn.y);
    }
  }

  const int err = msl_batch_reseed_seed_impl(batch, (const uint8_t*)seeds, sizeof(MslSeed),
                                             mask_bytes, mask_stride_bytes, 1u);
  if (err != 0) {
    return err;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (!msl_mask_row_selected(mask_bytes, mask_stride_bytes, bi)) {
      continue;
    }
    batch->state.opening_input_lock_timer[bi] = match_flow_sim_init_opening_input_lock_timer();
  }

  if (mask_bytes == NULL) {
    state_flags_refresh_post_frame(batch);
  } else {
    state_flags_refresh_post_frame_masked(batch, mask_bytes, mask_stride_bytes);
  }
  return 0;
}

int msl_batch_init_match(MslBatch* batch, const uint8_t* config_bytes, size_t config_stride_bytes) {
  return msl_batch_init_match_impl(batch, config_bytes, config_stride_bytes, NULL, 0);
}

int msl_batch_init_match_masked(MslBatch* batch, const uint8_t* config_bytes,
                                size_t config_stride_bytes, const uint8_t* mask_bytes,
                                size_t mask_stride_bytes) {
  if (mask_bytes == NULL) {
    return EINVAL;
  }
  return msl_batch_init_match_impl(batch, config_bytes, config_stride_bytes, mask_bytes,
                                   mask_stride_bytes);
}

static int msl_batch_reseed_seed_impl(MslBatch* batch, const uint8_t* seed_bytes,
                                      size_t seed_stride_bytes, const uint8_t* mask_bytes,
                                      size_t mask_stride_bytes, uint8_t rollout_owned_after) {
  if (batch == NULL || seed_bytes == NULL) {
    return EINVAL;
  }
  if (seed_stride_bytes < sizeof(MslSeed)) {
    return EINVAL;
  }
  if (mask_bytes != NULL && mask_stride_bytes < sizeof(uint8_t)) {
    return EINVAL;
  }
  const MslCommonParams* common = msl_common_params();

  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (!msl_mask_row_selected(mask_bytes, mask_stride_bytes, bi)) {
      continue;
    }
    const uint8_t* ptr = seed_bytes + (size_t)bi * seed_stride_bytes;
    const MslSeed* seed = (const MslSeed*)ptr;
    int active_players = (int)batch->config.num_players;
    if (active_players < 1) {
      active_players = 1;
    } else if (active_players > MSL_MAX_PLAYERS) {
      active_players = MSL_MAX_PLAYERS;
    }

    // Initialize per-environment global counters from seeded values.
    // - stale_attack_instance_counter: seeded as next after max observed attack_instance lanes.
    // - instance_id_counter: prefer explicit seed lane (derived strictly causally in tooling);
    //   fallback to next after max observed instance_id lanes for backward compatibility.
    uint16_t max_attack_inst = 0;
    uint16_t max_instance_id = 0;
    uint16_t seeded_instance_id_counter = seed->instance_id_counter;

    batch->state.frame_id[bi] = seed->frame_id;
    batch->state.frame_pre_random_seed[bi] = seed->frame_pre_random_seed;
    batch->state.stage_id[bi] = seed->stage_id;
    batch->state.opening_input_lock_timer[bi] = 0u;
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
      batch->state.handicap[idx] = (seed->handicap[p] != 0u) ? seed->handicap[p] : 9u;
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
      int8_t facing_dir1 = seed->facing_dir1[p];
      if (facing_dir1 == 0) {
        facing_dir1 = batch->state.facing[idx] ? (int8_t)1 : (int8_t)-1;
      }
      batch->state.facing_dir1[idx] = facing_dir1;
      float ground_friction_mul = seed->ground_friction_mul[p];
      if (!(ground_friction_mul > 0.0f)) {
        ground_friction_mul = 1.0f;
      }
      batch->state.ground_friction_mul[idx] = ground_friction_mul;
      batch->state.kb_smashcharge_active[idx] = seed->kb_smashcharge_active[p] ? 1u : 0u;
      batch->state.smash_charge_state[idx] = 0u;
      batch->state.smash_charge_frames[idx] = 0u;
      batch->state.smash_charge_hold_frames_max[idx] = 0u;
      batch->state.smash_charge_saved_rate_fp_q16_16[idx] = 0;
      batch->state.on_ground[idx] = seed->on_ground[p] ? 1 : 0;
      batch->state.frame_start_on_ground[idx] = batch->state.on_ground[idx];
      batch->state.ground_contact_x[idx] = 0.0f;
      batch->state.ground_contact_y[idx] = 0.0f;
      batch->state.ground_normal_x[idx] = 0.0f;
      batch->state.ground_normal_y[idx] = 1.0f;
      batch->state.wall_contact_x[idx] = 0.0f;
      batch->state.wall_contact_y[idx] = 0.0f;
      batch->state.wall_normal_x[idx] = 0.0f;
      batch->state.wall_normal_y[idx] = 0.0f;
      batch->state.wall_id[idx] = 0xFFFFu;
      batch->state.wall_kind[idx] = 0u;
      batch->state.ceiling_contact_x[idx] = 0.0f;
      batch->state.ceiling_contact_y[idx] = 0.0f;
      batch->state.ceiling_normal_x[idx] = 0.0f;
      batch->state.ceiling_normal_y[idx] = 0.0f;
      batch->state.ceiling_id[idx] = 0xFFFFu;
      batch->state.coll_env_flags[idx] = 0u;
      batch->state.coll_prev_env_flags[idx] = 0u;
      batch->state.damage_hitlag_floorhug_latch[idx] = 0u;

      batch->state.action_id[idx] = seed->action_id[p];
      batch->state.seed_prev_action_id[idx] = seed->seed_prev_action_id[p];
      batch->state.seed_prev_action_frame[idx] = seed->seed_prev_action_frame[p];
      batch->state.illusion_ghost_pos0_x[idx] = seed->illusion_ghost_pos0_x[p];
      batch->state.illusion_ghost_pos0_y[idx] = seed->illusion_ghost_pos0_y[p];
      batch->state.illusion_ghost_pos1_x[idx] = seed->illusion_ghost_pos1_x[p];
      batch->state.illusion_ghost_pos1_y[idx] = seed->illusion_ghost_pos1_y[p];
      // Throw pulse-consume seed lane (producer: tools/slippi/make_dataset_from_slp.py).
      // Decomp owner:
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
      batch->state.throw_pulse_consumed[idx] = seed->throw_pulse_consumed[p] ? 1u : 0u;
      // Previous-step throw pulse crossing lane (strictly causal seed producer).
      batch->state.throw_pulse_crossed_prev_frame[idx] = seed->throw_pulse_crossed_prev_frame[p];
      batch->state.throw_pulse_crossed_curr_frame[idx] = 0u;
      batch->state.source_clear_timer_x18c8[idx] = seed->source_clear_timer_x18c8[p];
      batch->state.source_clear_owner_set_phase[idx] =
          seed->source_clear_owner_set_phase[p] ? 1u : 0u;
      batch->state.source_clear_processhit_damage_pending_phase[idx] =
          seed->source_clear_processhit_damage_pending_phase[p] ? 1u : 0u;
      batch->state.fighter_8006cda4_pre_gate_consume_count[idx] =
          seed->fighter_8006cda4_pre_gate_consume_count[p];
      batch->state.source_clear_grounded_damage_clear_phase[idx] =
          seed->source_clear_grounded_damage_clear_phase[p] ? 1u : 0u;
      batch->state.source_clear_terminal_phase[idx] =
          seed->source_clear_terminal_phase[p] ? 1u : 0u;
      batch->state.match_flow_timer[idx] = seed->match_flow_timer[p];
      if (seed->opening_input_lock_timer[p] > batch->state.opening_input_lock_timer[bi]) {
        batch->state.opening_input_lock_timer[bi] = seed->opening_input_lock_timer[p];
      }
      batch->state.entry_end_fall_lock[idx] = seed->entry_end_fall_lock[p] ? 1u : 0u;
      batch->state.camera_box_visible_x221f_b0[idx] =
          seed->camera_box_visible_x221f_b0[p] ? 1u : 0u;
      float rebirth_camera_anchor_y = seed->rebirth_camera_anchor_y_f32[p];
      if (!isfinite(rebirth_camera_anchor_y)) {
        rebirth_camera_anchor_y = 0.0f;
      }
      batch->state.rebirth_camera_anchor_y_f32[idx] = rebirth_camera_anchor_y;
      float camera_target_world_x = seed->camera_target_world_x_f32[p];
      float camera_target_world_y = seed->camera_target_world_y_f32[p];
      float camera_target_world_z = seed->camera_target_world_z_f32[p];
      float camera_box_radius = seed->camera_box_radius_f32[p];
      if (!isfinite(camera_target_world_x)) {
        camera_target_world_x = 0.0f;
      }
      if (!isfinite(camera_target_world_y)) {
        camera_target_world_y = 0.0f;
      }
      if (!isfinite(camera_target_world_z)) {
        camera_target_world_z = 0.0f;
      }
      if (!isfinite(camera_box_radius) || camera_box_radius < 0.0f) {
        camera_box_radius = 0.0f;
      }
      batch->state.camera_target_world_x_f32[idx] = camera_target_world_x;
      batch->state.camera_target_world_y_f32[idx] = camera_target_world_y;
      batch->state.camera_target_world_z_f32[idx] = camera_target_world_z;
      batch->state.camera_box_radius_f32[idx] = camera_box_radius;
      batch->state.camera_target_point_inside_stage_cam_bounds_u8[idx] =
          seed->camera_target_point_inside_stage_cam_bounds_u8[p] ? 1u : 0u;
      batch->state.downwait_timer[idx] = seed->downwait_timer[p];
      batch->state.passivewall_timer[idx] = seed->passivewall_timer[p];
      batch->state.guard_jump_oos_entered_this_frame[idx] = 0u;
      batch->state.shine_jump_iasa_entered_this_frame[idx] = 0u;
      // Seed deterministic anim timebase from Slippi post-frame `state_age` (fp->cur_anim_frame)
      // plus a strictly-causal derived fp->frame_speed_mul.
      msl_anim_timebase_seed(batch, idx, seed->anim_frame_f32[p], seed->frame_speed_mul_f32[p]);
      // Narrow GuardSetOff hidden-rate override:
      // frame_speed_mul_f32 above remains strictly causal. For GuardSetOff last-hitlag rows,
      // Slippi exposes the ftCo_80092F2C x19A4/lightshield-owned rate only after hitlag exits, so
      // preprocessing may seed this explicit GuardSetOff-only lane instead of weakening the general
      // frame_speed_mul_f32 contract.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim}
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      const float guard_setoff_exit_rate = seed->guard_setoff_exit_frame_speed_mul_f32[p];
      if (seed->action_id[p] == (uint16_t)MSL_ACT_GUARD_SET_OFF && seed->hitlag[p] == 1u &&
          seed->guard_setoff_hitlag_exit_phase_u8[p] == 2u && isfinite(guard_setoff_exit_rate) &&
          guard_setoff_exit_rate > 0.0f) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(guard_setoff_exit_rate);
      }
      float walk_anim_source_vel = seed->walk_anim_source_vel_f32[p];
      if (!isfinite(walk_anim_source_vel)) {
        walk_anim_source_vel = 0.0f;
      }
      batch->state.walk_anim_source_vel[idx] = walk_anim_source_vel;
      float walk_retarget_tick_source_vel = seed->walk_retarget_tick_source_vel_f32[p];
      if (!isfinite(walk_retarget_tick_source_vel)) {
        walk_retarget_tick_source_vel = 0.0f;
      }
      batch->state.walk_retarget_tick_source_vel[idx] = walk_retarget_tick_source_vel;
      float run_anim_source_vel = seed->run_anim_source_vel_f32[p];
      if (!isfinite(run_anim_source_vel)) {
        run_anim_source_vel = 0.0f;
      }
      batch->state.run_anim_source_vel[idx] = run_anim_source_vel;
      const uint8_t turn_kb_face = seed->turn_kneebend_facing_override_u8[p];
      batch->state.turn_kneebend_facing_override[idx] = (turn_kb_face <= 2u) ? turn_kb_face : 0u;
      batch->state.anim_defer_tick_once[idx] = 0;
      batch->state.guard_tilt_x8[idx] = seed->guard_tilt_x8[p];
      batch->state.guard_tilt_x4[idx] = seed->guard_tilt_x4[p];
      batch->state.guard_reflect_timer_x14[idx] = seed->guard_reflect_timer_x14[p];
      batch->state.guard_reflect_timer_x18[idx] = seed->guard_reflect_timer_x18[p];
      batch->state.guard_release_latched_xc[idx] = seed->guard_release_latched_xc[p] ? 1 : 0;
      batch->state.guard_x10[idx] = seed->guard_x10[p];
      batch->state.lightshield_amount[idx] = seed->lightshield_amount[p];
      batch->state.guard_setoff_hitlag_damage_min[idx] = seed->guard_setoff_hitlag_damage_min[p];
      batch->state.guard_setoff_hitlag_exit_phase_u8[idx] =
          seed->guard_setoff_hitlag_exit_phase_u8[p];
      batch->state.guard_setoff_post_hitlag_owner_u8[idx] =
          seed->guard_setoff_post_hitlag_owner_u8[p];
      batch->state.jumps_left[idx] = seed->jumps_left[p];
      batch->state.stocks[idx] = seed->stocks[p];
      batch->state.kneebend_jump_input[idx] = seed->kneebend_jump_input[p];
      batch->state.kneebend_is_short_hop[idx] = seed->kneebend_is_short_hop[p];
      batch->state.tilt_timer_x[idx] = seed->tilt_timer_x[p];
      batch->state.tilt_timer_y[idx] = seed->tilt_timer_y[p];
      // Slippi post-frame sends the raw fp+0x221A byte as `state_flags[...,1]` and documents
      // bit 0x08 as "isFastFalling".
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      //
      // Seeded ownership model:
      // - `seed->fall_fast` is the internal fp->fall_fast lane (derived causally in preprocessing).
      // - `seed->fall_fast_hitlag_exit_owner` marks immediate hitlag-exit rows for fastfall-capable
      //   actions, where prio-0 hitlag decrement runs before non-hitlag callback ownership.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
      //
      // On those ownership-marked rows, keep the internal seeded lane authoritative; otherwise
      // use the raw Slippi fp+0x221A isFastFalling bit for teacher-forced reseed parity.
      enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
      enum { MSL_STATE_FLAG_221A_IS_FASTFALL = 0x08 };
      uint8_t fall_fast = seed->fall_fast[p] ? 1u : 0u;
      const uint8_t slippi_fall_fast =
          (seed->state_flags[p][MSL_STATE_FLAGS_221A_INDEX] & MSL_STATE_FLAG_221A_IS_FASTFALL) ? 1u
                                                                                               : 0u;
      if (!seed->fall_fast_hitlag_exit_owner[p]) {
        fall_fast = slippi_fall_fast;
      }
      batch->state.fall_fast[idx] = fall_fast;
      // Decomp: Fighter_ChangeMotionState clears fp->fall_fast unless the motion-state flags
      // include Ft_MF_KeepFastFall.
      // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState; clears when (flags & Ft_MF_KeepFastFall)==0)
      // refs/melee/src/melee/ft/forward.h (Ft_MF_KeepFastFall = 1<<0)
      //
      // Non-hitlag ownership / state_flags parity after reseed is maintained by the runtime update
      // path (physics + state_flags writer); do not add extra replay-fit fall_fast rewrites here.
      // fp+0x2340 AttackDash lane (mv.co.attackdash.x0) targeted seed carry:
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
      // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
      batch->state.attackdash_x0[idx] = seed->attackdash_x0[p];
      // Attack1 jab intent latch (mv.co.attack1.x0) seeded from fp+0x2340 misc AS lane.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (misc AS variable @ fp+0x2340)
      batch->state.jab_x0[idx] = seed->jab_x0[p] ? 1u : 0u;
      batch->state.run_x0[idx] = seed->run_x0[p];
      batch->state.runbrake_cmd0[idx] = seed->runbrake_cmd0[p] ? 1u : 0u;
      batch->state.dash_x4[idx] = seed->dash_x4[p];
      batch->state.shine_release_lag[idx] = seed->shine_release_lag[p];
      batch->state.shine_is_release[idx] = seed->shine_is_release[p];
      batch->state.ecb_lock_timer[idx] = seed->ecb_lock_timer[p];
      batch->state.ledge_cooldown[idx] = seed->ledge_cooldown[p];
      batch->state.ledge_side[idx] = -1;
      batch->state.landing_fallspecial_allow_interrupt[idx] =
          seed->landing_fallspecial_allow_interrupt[p] ? 1u : 0u;
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
      batch->state.turn_x8[idx] = seed->turn_x8[p];
      batch->state.lr_press_timer[idx] = seed->lr_press_timer[p];
      batch->state.x672_input_timer[idx] = seed->x672_input_timer[p];
      batch->state.x673[idx] = seed->x673[p];
      batch->state.x674[idx] = seed->x674[p];
      batch->state.x675[idx] = seed->x675[p];
      batch->state.x676_x[idx] = seed->x676_x[p];
      batch->state.x2228_b7[idx] = seed->x2228_b7[p];
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
      batch->state.hitlag_pre_timer[idx] = (seed->hitlag[p] != 0u) ? 1u : 0u;
      batch->state.hitlag_started_frame[idx] = 0;
      batch->state.hitstun[idx] = seed->hitstun[p];
      int16_t x18ac = seed->damage_time_since_hit_x18ac[p];
      if (x18ac < -1) {
        x18ac = -1;
      }
      batch->state.damage_time_since_hit_x18ac[idx] = x18ac;
      batch->state.damage_jump_buffer_x14[idx] = seed->damage_jump_buffer_x14[p];
      batch->state.damage_post_hitlag_cb_kind[idx] = seed->damage_post_hitlag_cb_kind[p];
      batch->state.attacker_shield_ground_kb_vel[idx] = seed->attacker_shield_ground_kb_vel[p];
      batch->state.throw_pending_victim_port[idx] = 0xFFu;
      batch->state.attached_victim_port[idx] = 0xFFu;
      batch->state.throw_anim_rate_fp_q16_16[idx] = 0;
      batch->state.l_cancel[idx] = seed->l_cancel[p];
      // Collision hit-status ownership bridge (x1988/x198C):
      // - Slippi post-frame `hurtbox_state` reports x1988 when nonzero, else x198C.
      //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      // - x1988 is movescript-owned (opcode 26), while x198C is timer/system-owned.
      //   refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B760,ftColl_8007B7A4}
      uint8_t seed_hurtbox_state = seed->hurtbox_state[p];
      batch->state.hurtbox_state[idx] = seed_hurtbox_state;
      batch->state.colanim_hit_status_x198c[idx] = seed_hurtbox_state;
      batch->state.colanim_timer_x1990[idx] = 0u;
      batch->state.colanim_timer_x1994[idx] = 0u;
      batch->state.colanim_lock_x2221_b0[idx] = 0u;
      {
        // Seed-bridge inference: if the movescript table reports a nonzero x1988 at the seeded
        // (msid, frame) and it matches the seeded merged value, treat the x198C lane as 0.
        // This prevents stale carry when x1988 windows end on the next frame.
        uint8_t seed_x1988 = 0u;
        const uint32_t anim_u32 = seed->animation_index[p];
        if (anim_u32 <= 0xFFFFu) {
          const uint16_t msid = (uint16_t)anim_u32;
          const float af = msl_anim_frame_sanitize_f32(seed->anim_frame_f32[p]);
          const uint16_t fr = msl_anim_frame_floor_u16(af);
          (void)hit_status_get(seed->char_id[p], msid, fr, &seed_x1988);
        }
        if (seed_x1988 != 0u && seed_hurtbox_state == seed_x1988) {
          batch->state.colanim_hit_status_x198c[idx] = 0u;
        }
        if (seed_bridge_has_shine_start_x1988_masked_x198c(seed, p, seed_hurtbox_state,
                                                           seed_x1988)) {
          batch->state.colanim_hit_status_x198c[idx] = 1u;
        }
        // Narrow seed-bridge for x198C=2 timer ownership:
        // - Slippi exposes merged hurtbox_state but not x1990 remaining.
        // - When x1988 is absent and replay history derivation provides a nonzero x1990 hint
        //   (with no x1994/x2221 ownership), seed x1990 so Fighter_8006A360 timer decay can
        //   clear x198C on the correct frame.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        const uint8_t shine_start_x1990_reseed =
            ((seed->action_id[p] == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
              seed->action_id[p] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) &&
             seed_hurtbox_state == 2u)
                ? 1u
                : 0u;
        if (((seed->hitlag[p] == 0u && seed->hitstun[p] == 0u) || shine_start_x1990_reseed) &&
            seed->colanim_hit_status_x198c[p] == 2u && seed->colanim_timer_x1990[p] != 0u &&
            seed->colanim_timer_x1994[p] == 0u && seed->colanim_lock_x2221_b0[p] == 0u) {
          // Shine Start hitlag rows can expose an active x1990/x198C lane at the reseed boundary.
          // Dropping it under the generic hitlag guard clears the visible hurtbox state one frame
          // early. Keep this scoped to SpecialLw Start/AirStart where decomp enters the state via
          // ftAnim_8006EBA4 and the replay-history extraction provides explicit x1990 provenance.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
          //   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          batch->state.colanim_timer_x1990[idx] = seed->colanim_timer_x1990[p];
          batch->state.colanim_hit_status_x198c[idx] = 2u;
        }
        if (shine_start_x1990_reseed && seed->colanim_hit_status_x198c[p] == 2u &&
            seed->colanim_timer_x1990[p] != 0u && seed->colanim_timer_x1994[p] != 0u &&
            seed->colanim_lock_x2221_b0[p] == 0u) {
          // Cliff/Fall -> aerial Shine Start rows can carry both hidden timers: x1990 owns the
          // visible x198C=2 status while x1994 remains queued underneath. Trust the explicit
          // seed-history lane for this Shine Start shape instead of dropping both timers because
          // x1988 currently masks the visible byte.
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B760,ftColl_8007B7A4}
          batch->state.colanim_timer_x1990[idx] = seed->colanim_timer_x1990[p];
          batch->state.colanim_timer_x1994[idx] = seed->colanim_timer_x1994[p];
          batch->state.colanim_hit_status_x198c[idx] = 2u;
        }
        // Narrow explicit x1994/x198C seed bridge:
        // - Slippi merged hurtbox_state can remain 0 on reseeded rows even when dataset/history
        //   captured the explicit color-animation immunity lane (`x198C=1`, `x1994>0`).
        // - Fighter_8006A360 decrements x1994 and keeps x198C=1 until expiry when x1990/x2221 are
        //   both clear.
        // - Trust the explicit seeded internals only for this hidden timer-owned immunity shape so
        //   replay-real DownBound rows preserve invincible-contact semantics without broad x198C
        //   overrides.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B7A4,ftColl_8007B868}
        if ((seed->action_id[p] == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
             seed->action_id[p] == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
            seed->on_ground[p] != 0u && seed->colanim_hit_status_x198c[p] == 1u &&
            seed->colanim_timer_x1994[p] != 0u && seed->colanim_timer_x1990[p] == 0u &&
            seed->colanim_lock_x2221_b0[p] == 0u && seed_hurtbox_state == 0u) {
          batch->state.colanim_timer_x1994[idx] = seed->colanim_timer_x1994[p];
          batch->state.colanim_hit_status_x198c[idx] = 1u;
        }
      }
      if (common != NULL) {
        const uint16_t action = seed->action_id[p];
        const int16_t action_frame = seed->action_frame[p];
        // Throw entry uses ftColl_8007B7A4(..., x348): x1994 timer + x198C={1,2} depending on x1990.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
        if (action == (uint16_t)MSL_ACT_THROW_F || action == (uint16_t)MSL_ACT_THROW_B ||
            action == (uint16_t)MSL_ACT_THROW_HI || action == (uint16_t)MSL_ACT_THROW_LW) {
          const uint16_t rem = colanim_timer_remaining_from_seed_bridge(
              common->colanim_throw_x1994_frames, action_frame, seed->anim_frame_f32[p],
              seed->frame_speed_mul_f32[p]);
          batch->state.colanim_timer_x1994[idx] = rem;
          if (rem != 0u) {
            batch->state.colanim_hit_status_x198c[idx] =
                (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
          }
        }
        // Cliff catch/wait invulnerability path uses x1990 timer (x49C).
        // Decomp callsite anchor:
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C
        if (action == (uint16_t)MSL_ACT_CLIFF_CATCH || action == (uint16_t)MSL_ACT_CLIFF_WAIT) {
          const uint16_t rem = colanim_timer_remaining_from_seed_bridge(
              common->colanim_cliff_x1990_frames, action_frame, seed->anim_frame_f32[p],
              seed->frame_speed_mul_f32[p]);
          batch->state.colanim_timer_x1990[idx] = rem;
          if (rem != 0u) {
            batch->state.colanim_hit_status_x198c[idx] = 2u;
          }
        }
      }
      batch->state.ground_id[idx] = seed->ground_id[p];
      batch->state.animation_index[idx] = seed->animation_index[p];
      batch->state.dynamic_pose_state_valid[idx] = 0u;
      batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
      batch->state.dynamic_pose_node_count[idx] = 0u;
      batch->state.dynamic_pose_char_id[idx] = batch->state.char_id[idx];
      batch->state.dynamic_pose_msid[idx] = (uint16_t)seed->animation_index[p];
      batch->state.dynamic_pose_frame[idx] =
          msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(seed->anim_frame_f32[p]));
      for (uint8_t dn = 0; dn < (uint8_t)MSL_MAX_DYNAMIC_NODES; dn++) {
        const size_t di = idx * (size_t)MSL_MAX_DYNAMIC_NODES + (size_t)dn;
        batch->state.dynamic_pose_rot_x[di] = 0.0f;
        batch->state.dynamic_pose_rot_y[di] = 0.0f;
        batch->state.dynamic_pose_rot_z[di] = 0.0f;
        batch->state.dynamic_pose_pos_x[di] = 0.0f;
        batch->state.dynamic_pose_pos_y[di] = 0.0f;
        batch->state.dynamic_pose_pos_z[di] = 0.0f;
        batch->state.dynamic_pose_axis_x[di] = 1.0f;
        batch->state.dynamic_pose_axis_y[di] = 0.0f;
        batch->state.dynamic_pose_axis_z[di] = 0.0f;
        batch->state.dynamic_pose_angle[di] = 0.0f;
      }
      batch->state.instance_hit_by[idx] = seed->instance_hit_by[p];
      batch->state.instance_id[idx] = seed->instance_id[p];
      batch->state.motion_entry_instance_id_override[idx] =
          seed->motion_entry_instance_id_override_u16[p];
      {
        batch->state.capture_grab_timer[idx] = seed->capture_grab_timer_f32[p];
        batch->state.capture_wait_counter[idx] = seed->capture_wait_counter_f32[p];
        batch->state.capture_wait_anim_rate_timer[idx] = seed->capture_wait_anim_rate_timer_f32[p];
        batch->state.capture_wait_jump_latch[idx] = seed->capture_wait_jump_latch_u8[p] ? 1u : 0u;
        batch->state.capture_breakout_pending[idx] = seed->capture_breakout_pending_u8[p] ? 1u : 0u;
      }
      if (seed->instance_id[p] > max_instance_id) {
        max_instance_id = seed->instance_id[p];
      }
      // Seed the fp->x2073 compare byte used by ft_800895E0's instance_id bump gate.
      // Decomp: ft_800895E0 reads fp+0x2073 and compares against (u8)new_motion_state->x4_flags.
      // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
      //
      // IMPORTANT:
      // We seed this explicitly from the dataset schema (derived strictly causally from replay
      // history in preprocessing) so teacher-forced reseeds preserve the correct compare key even
      // when fp->x2088 (Slippi `instance_id`) is 0.
      //
      // Defensive fallback for synthetic tests / callers that leave the new seed field at 0:
      // - If the motion state's flags_low byte is nonzero, prefer that nonzero value.
      // - If the flags_low byte is 0, preserve 0 (this is observable in GALE01: ft_800895E0 bumps
      //   unconditionally when flags_low==0, and stores the flags word to fp->x2070).
      uint8_t x2073 = seed->instance_id_x2073[p];
      if (x2073 == 0) {
        const uint32_t x4_flags =
            attack_id_x4_flags_from_action(batch->state.char_id[idx], seed->action_id[p]);
        const uint8_t flags_low = (uint8_t)(x4_flags & 0xFFu);
        if (flags_low != 0) {
          x2073 = flags_low;
        }
      }
      batch->state.instance_id_x2073[idx] = x2073;
      batch->state.instance_identity_last_action_id[idx] = seed->action_id[p];
      batch->state.attack_id[idx] = seed->attack_id[p];
      batch->state.attack_instance[idx] = seed->attack_instance[p];
      batch->state.attack_identity_last_action_id[idx] = seed->action_id[p];
      batch->state.last_attack_landed[idx] = seed->last_attack_landed[p];
      batch->state.combo_count[idx] = seed->combo_count[p];
      batch->state.combo_victim_port[idx] = seed->combo_victim_port[p];
      batch->state.combo_victim_instance_id[idx] = seed->combo_victim_instance_id[p];
      batch->state.combo_timer_x2098[idx] = seed->combo_timer_x2098[p];
      batch->state.source_port0[idx] =
          (seed->source_port0[p] < (uint8_t)MSL_MAX_PLAYERS) ? seed->source_port0[p] : (uint8_t)p;
      batch->state.last_hit_by[idx] = seed->last_hit_by[p];
      batch->state.grab_owner_port[idx] = seed->grab_owner_port[p];
      batch->state.grab_mash_stick_x_sign[idx] = seed->grab_mash_stick_x_sign[p];
      batch->state.grab_mash_stick_y_sign[idx] = seed->grab_mash_stick_y_sign[p];

      // BODY sweep ownership (x58/x4C) on reseed:
      // - Decomp keeps previous/current HitCapsule centers in ftColl_8007AD18 and BODY overlap
      //   consumes that lane through lbColl_8000805C -> lbColl_80006E58.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      //
      // Teacher-forced one-step reseed now carries the prior-frame x58 lane when preprocessing can
      // derive it. Older/synthetic seeds keep the translation bootstrap fallback.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      batch->state.hitbox_prev_bootstrap[idx] = 1u;
      const size_t hb_base =
          ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t hb_i = hb_base + (size_t)hb;
        batch->state.hitbox_enabled[hb_i] = 0u;
        batch->state.hitbox_x[hb_i] = 0.0f;
        batch->state.hitbox_y[hb_i] = 0.0f;
        batch->state.hitbox_z[hb_i] = 0.0f;
        if (seed->combat_hitbox_prev_valid[p][hb]) {
          batch->state.hitbox_prev_bootstrap[idx] = 2u;
          batch->state.hitbox_prev_enabled[hb_i] = 1u;
          batch->state.hitbox_prev_x[hb_i] = seed->combat_hitbox_prev_x[p][hb];
          batch->state.hitbox_prev_y[hb_i] = seed->combat_hitbox_prev_y[p][hb];
          batch->state.hitbox_prev_z[hb_i] = seed->combat_hitbox_prev_z[p][hb];
        } else {
          batch->state.hitbox_prev_enabled[hb_i] = 0u;
          batch->state.hitbox_prev_x[hb_i] = 0.0f;
          batch->state.hitbox_prev_y[hb_i] = 0.0f;
          batch->state.hitbox_prev_z[hb_i] = 0.0f;
        }
        batch->state.hitbox_pose_create[hb_i] = 0u;
        batch->state.hitbox_enable_edge[hb_i] = 0u;
        // x43_b2 ownership lane: create/reset starts at 0 before any character callback mutation.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        batch->state.hitbox_x43_b2[hb_i] = 0u;
      }

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

    // Combo victim ownership reconstruction bridge (fp->x2094 equivalent):
    // - Decomp combo continuation in ftColl_800763C0 compares the stored victim pointer
    //   (fp->x2094) and attack id, not just combo_count.
    // - Slippi post-frame does not expose fp->x2094 directly, but does expose victim-side
    //   attribution producers:
    //   * fp->dmg.x18C4_source_ply   -> last_hit_by
    //   * fp->dmg.x18EC_instancehitby -> instance_hit_by
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    //
    // Strict bridge policy:
    // - only if seed combo_count>0 and combo_victim_port is invalid,
    // - infer a victim only when exactly one opponent matches both attribution lanes:
    //     victim.last_hit_by == attacker_port
    //     victim.instance_hit_by == attacker.instance_id
    // This avoids heuristic victim guessing on ambiguous rows.
    for (int attacker_p = 0; attacker_p < active_players; attacker_p++) {
      const size_t a_idx = msl_idx_player(bi, attacker_p);
      if (batch->state.combo_count[a_idx] == 0u) {
        continue;
      }
      const uint8_t seed_victim = batch->state.combo_victim_port[a_idx];
      if (seed_victim < (uint8_t)active_players && seed_victim != (uint8_t)attacker_p) {
        continue;
      }
      const uint16_t attacker_instance = batch->state.instance_id[a_idx];
      int inferred_victim_p = -1;
      for (int victim_p = 0; victim_p < active_players; victim_p++) {
        if (victim_p == attacker_p) {
          continue;
        }
        const size_t v_idx = msl_idx_player(bi, victim_p);
        if (batch->state.last_hit_by[v_idx] != batch->state.source_port0[a_idx]) {
          continue;
        }
        if (batch->state.instance_hit_by[v_idx] != attacker_instance) {
          continue;
        }
        if (inferred_victim_p >= 0) {
          inferred_victim_p = -1;
          break;
        }
        inferred_victim_p = victim_p;
      }
      if (inferred_victim_p < 0) {
        // Narrow reseed bridge fallback for combo-victim ownership (fp->x2094):
        // - ftColl_800763C0 continuation only needs the stored victim pointer + attack id.
        // - ftColl_800764DC / ftCo_8008F744 keep that victim pointer live while the victim remains
        //   in active combo context (victim hitstun or post-hitstun combo timer).
        // - Slippi exposes `last_hit_by`, hitstun, and the replay-derived combo timer seed lane,
        //   but `instance_hit_by` can diverge from the attacker instance on replay-real rows where
        //   x2094 should still continue the combo.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_800764DC}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
        int fallback_victim_p = -1;
        for (int victim_p = 0; victim_p < active_players; victim_p++) {
          if (victim_p == attacker_p) {
            continue;
          }
          const size_t v_idx = msl_idx_player(bi, victim_p);
          if (batch->state.last_hit_by[v_idx] != batch->state.source_port0[a_idx]) {
            continue;
          }
          // Terminal combo-timer rows (`x2098 == 1`) are cleared in ftColl_800764DC before combat
          // ownership applies for the current frame. This simulator mirrors that ordering with
          // timers_update_post_anim() before combat_resolve(), so only preserve combo-timer-only
          // ownership when more than one post-hitstun tick remains.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
          if (batch->state.hitstun[v_idx] == 0u && batch->state.combo_timer_x2098[v_idx] <= 1u) {
            continue;
          }
          if (fallback_victim_p >= 0) {
            fallback_victim_p = -1;
            break;
          }
          fallback_victim_p = victim_p;
        }
        inferred_victim_p = fallback_victim_p;
      }
      if (inferred_victim_p >= 0) {
        const size_t v_idx = msl_idx_player(bi, inferred_victim_p);
        batch->state.combo_victim_port[a_idx] = (uint8_t)inferred_victim_p;
        batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[v_idx];
      }
    }

    // Compute decomp-shaped grab attachment offsets (fp->x1A70 analog) for any seeded victims.
    grab_attachment_reseed_init(batch, bi);

    // Combat hitlist reseed generation:
    // - Seed carries a dense per-(attacker,hit_group,victim) snapshot, but runtime uses per-hitbox
    //   victim rings (HitCapsule-shaped).
    // - Mark all fighter hitboxes as "needs seed materialization" for this reseed.
    uint32_t gen = batch->state.hitlist_reseed_gen[bi] + 1u;
    if (gen == 0u) {
      gen = 1u;
    }
    batch->state.hitlist_reseed_gen[bi] = gen;
    {
      const size_t base = ((size_t)bi * (size_t)MSL_MAX_PLAYERS) * (size_t)MSL_MAX_HITBOXES;
      for (size_t i = 0; i < (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES; i++) {
        batch->state.fighter_hitlist_init_gen[base + i] = 0u;
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
      if (item->instance_id > max_instance_id) {
        max_instance_id = item->instance_id;
      }
      batch->state.item_attack_id[ii] = item_seed_bridge_attack_id(batch, bi, item);
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
      float reflect_mul = seed->item_reflect_damage_mul[it];
      if (!(reflect_mul > 0.0f)) {
        reflect_mul = 1.0f;
      }
      batch->state.item_reflect_damage_mul[ii] = reflect_mul;
      batch->state.item_timer[ii] = item->timer;
      batch->state.item_hitlag[ii] = 0u;
      batch->state.item_spawn_id[ii] = item->spawn_id;
      batch->state.item_misc0[ii] = item->misc0;
      batch->state.item_misc1[ii] = item->misc1;
      batch->state.item_misc2[ii] = item->misc2;
      batch->state.item_misc3[ii] = item->misc3;

      // Item hitlists are derived (not seeded): clear on reseed so reused item slots don't
      // inherit stale victim rings.
      hitlist_capsule_clear(&batch->state.item_hitlist[ii]);
    }

    // Reconstruct the owner-side attached victim pointer (`fp->victim_gobj`) from the seeded
    // victim-side owner links. Common Throw/Thrown logic keys several shared callbacks off the
    // thrower's direct victim pointer rather than repeatedly searching from the victim side.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
    const int num_players = (int)batch->config.num_players;
    for (int owner = 0; owner < num_players; owner++) {
      batch->state.attached_victim_port[msl_idx_player(bi, owner)] = 0xFFu;
    }
    for (int victim = 0; victim < num_players; victim++) {
      const size_t v_idx = msl_idx_player(bi, victim);
      const uint8_t owner = batch->state.grab_owner_port[v_idx];
      if (owner == 0xFFu || owner >= (uint8_t)num_players) {
        continue;
      }
      if (!msl_action_is_grabbed_victim(batch->state.action_id[v_idx])) {
        continue;
      }
      const size_t o_idx = msl_idx_player(bi, (int)owner);
      if (batch->state.attached_victim_port[o_idx] == 0xFFu ||
          (uint8_t)victim < batch->state.attached_victim_port[o_idx]) {
        batch->state.attached_victim_port[o_idx] = (uint8_t)victim;
      }
    }
    for (int p = 0; p < num_players; p++) {
      batch->state.throw_anim_rate_fp_q16_16[msl_idx_player(bi, p)] = 0;
    }
    for (int owner = 0; owner < num_players; owner++) {
      const size_t o_idx = msl_idx_player(bi, owner);
      const uint8_t victim = batch->state.attached_victim_port[o_idx];
      if (victim == 0xFFu || victim >= (uint8_t)num_players || victim == (uint8_t)owner) {
        continue;
      }
      const size_t v_idx = msl_idx_player(bi, (int)victim);
      const uint16_t owner_action = batch->state.action_id[o_idx];
      const uint16_t victim_action = batch->state.action_id[v_idx];
      uint16_t throw_action = owner_action;
      if (throw_index_from_action_id(throw_action) < 0) {
        throw_action = throw_action_from_thrown_action(victim_action);
      }
      if (throw_action == 0xFFFFu) {
        continue;
      }
      const int32_t rate_fp = throw_anim_rate_fp_from_pair(batch, o_idx, v_idx, throw_action);
      if (rate_fp <= 0) {
        continue;
      }
      batch->state.throw_anim_rate_fp_q16_16[o_idx] = rate_fp;
      batch->state.throw_anim_rate_fp_q16_16[v_idx] = rate_fp;
    }

    // Seed bridge: item hitlists (teacher-forced one-step).
    //
    // Decomp shape:
    // - Item collision uses per-item HitCapsule victim rings to prevent re-hitting the same fighter
    //   across frames (similar to fighter hitbox hitlists).
    // - Tick/decrement path: refs/melee/src/melee/it/itcoll.c::it_8027146C
    //
    // Seed reality:
    // - Seed schema currently carries fighter hitlist state but does not carry per-item hitlists.
    // - Under teacher-forced reseed, this can cause false multi-frame item re-hits for items that
    //   are supposed to persist after a suppressed BODY collision (e.g. grab-owner laser on an
    //   attached Thrown*/Capture* victim).
    //
    // Minimal parity fix (suite-focused, deterministic, no allocations):
    // - If a victim is still attached (grab_owner_port) and still in a grabbed-victim action and
    //   is in hitlag from a prior item hit (seeded), pre-latch the matching laser item(s) into the
    //   per-item hitlist so this step cannot apply an additional BODY hit.
    for (int victim = 0; victim < num_players; victim++) {
      if (seed->hitlag[victim] == 0u) {
        continue;
      }
      const uint8_t owner = seed->grab_owner_port[victim];
      if (owner == 0xFFu || owner >= (uint8_t)num_players) {
        continue;
      }
      const uint16_t act = seed->action_id[victim];
      if (!msl_action_is_grabbed_victim(act)) {
        continue;
      }
      const uint16_t hit_iid = seed->instance_hit_by[victim];
      if (hit_iid == 0u) {
        continue;
      }
      const size_t v_idx = msl_idx_player(bi, victim);
      const uint16_t victim_iid = batch->state.instance_id[v_idx];
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii]) {
          continue;
        }
        if (batch->state.item_owner[ii] != (int8_t)owner) {
          continue;
        }
        if (batch->state.item_instance_id[ii] != hit_iid) {
          continue;
        }
        if (laser_params_for_item_type(batch->state.item_type[ii]) == NULL) {
          continue;
        }
        hitlist_register_item_fighter(batch, bi, it, victim, victim_iid,
                                      (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      }
    }

    // Seed bridge: item->shield hitlists on ongoing GuardSetOff hitlag.
    //
    // Decomp/runtime shape:
    // - laser shield hits register the defender in the per-item HitCapsule victim ring with
    //   insert type=shield on the collision frame, preventing the same live shot from rehitting
    //   the same shield on the next callback pass while the item persists.
    // - teacher-forced reseed does not currently serialize per-item hitlists, so an ongoing
    //   GuardSetOff hitlag snapshot can incorrectly accept the same live laser again on t+1.
    // refs/melee/src/melee/it/itcoll.c::it_8027146C
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_HitShield
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    for (int victim = 0; victim < num_players; victim++) {
      if (seed->hitlag[victim] == 0u) {
        continue;
      }
      if (seed->action_id[victim] != (uint16_t)MSL_ACT_GUARD_SET_OFF) {
        continue;
      }
      const size_t v_idx = msl_idx_player(bi, victim);
      const uint16_t victim_iid = batch->state.instance_id[v_idx];
      int candidate_item = -1;
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii]) {
          continue;
        }
        if (laser_params_for_item_type(batch->state.item_type[ii]) == NULL) {
          continue;
        }
        // Narrow carry:
        // - seed item->shield carry only when there is exactly one live laser candidate on the
        //   ongoing GuardSetOff hitlag row,
        // - if multiple live laser candidates exist, do not guess which one authored the prior
        //   shield hit; leave that case to the normal runtime hitlist owner instead of suppressing
        //   untouched shots.
        if (candidate_item >= 0) {
          candidate_item = -2;
          break;
        }
        candidate_item = it;
      }
      if (candidate_item >= 0) {
        hitlist_register_item_fighter(batch, bi, candidate_item, victim, victim_iid,
                                      (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
      }
    }

    // Seed bridge: Illusion/Phantasm article hitlag on ongoing GuardSetOff shield-hit rows.
    //
    // Decomp/runtime shape:
    // - successful item shield contact enters generic item hitlag (`item->xCBC_hitlagFrames`),
    //   and Item_802697D4 skips item Phys/movement while the item remains in hitlag
    //   (`xDC8_word.flags.x9 != 0`).
    // - teacher-forced reseed does not serialize item hitlag, so ongoing GuardSetOff rows would
    //   otherwise let the same live Illusion/Phantasm article keep advancing from
    //   ghostEffectPos[1] instead of staying frozen at the shield-contact point.
    // - Keep the bridge conservative: only seed article hitlag when there is exactly one live
    //   Illusion/Phantasm candidate on the ongoing GuardSetOff hitlag row.
    // refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    for (int victim = 0; victim < num_players; victim++) {
      if (seed->hitlag[victim] == 0u) {
        continue;
      }
      if (seed->action_id[victim] != (uint16_t)MSL_ACT_GUARD_SET_OFF) {
        continue;
      }
      int candidate_item = -1;
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii]) {
          continue;
        }
        if (!item_type_is_illusion_article(batch->state.item_type[ii])) {
          continue;
        }
        if (candidate_item >= 0) {
          candidate_item = -2;
          break;
        }
        candidate_item = it;
      }
      if (candidate_item >= 0) {
        const size_t ii = msl_idx_item(bi, candidate_item);
        batch->state.item_hitlag[ii] = seed->hitlag[victim];
      }
    }

    // Seed bridge: Illusion/Phantasm article hitlag on ongoing BODY-hit rows.
    //
    // Decomp/runtime shape:
    // - successful item BODY contact also raises generic item hitlag (`item->xCBC_hitlagFrames`),
    //   and Item_802697D4 skips item Phys/movement while the item remains in hitlag.
    // - Illusion/Phantasm body hits persist (itFoxIllusion_Logic14_DmgDealt returns false), so
    //   teacher-forced reseed must restore hitlag ownership on ongoing damage-hitlag rows or the
    //   same article incorrectly keeps advancing from ghostEffectPos[1].
    // - Use the seeded victim `instance_hit_by` lane to identify the exact responsible article;
    //   if it does not match a live Illusion/Phantasm item instance, fail open.
    // refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
    // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
    for (int victim = 0; victim < num_players; victim++) {
      if (seed->hitlag[victim] == 0u) {
        continue;
      }
      const uint16_t hit_iid = seed->instance_hit_by[victim];
      if (hit_iid == 0u) {
        continue;
      }
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii]) {
          continue;
        }
        if (!item_type_is_illusion_article(batch->state.item_type[ii])) {
          continue;
        }
        if (batch->state.item_instance_id[ii] != hit_iid) {
          continue;
        }
        if (seed->hitlag[victim] > batch->state.item_hitlag[ii]) {
          batch->state.item_hitlag[ii] = seed->hitlag[victim];
        }
        break;
      }
    }

    // Next value for plStale_IncrementAttackInstance (global counter).
    uint16_t next = (uint16_t)(max_attack_inst + 1u);
    if (next == 0) {
      next = 1;
    }
    batch->state.stale_attack_instance_counter[bi] = next;

    // Next value for plAttack_80037B08 (global counter used for fp->x2088 and many item instance_ids).
    // Decomp: refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    //
    // Seed bridge:
    // - Prefer the explicit seed lane (seed->instance_id_counter), which preprocessing derives
    //   strictly causally from replay-visible fighter/item instance_id history.
    // - Enforce a decomp-shaped lower bound from current live IDs: plAttack_80037B08 returns the
    //   current global counter and then increments, so with no wrap the next counter cannot be
    //   below max(live instance_id)+1.
    //   refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    // - If absent (0), fall back to that lower bound for older datasets/tests.
    //
    // NOTE(seed bridge): 16-bit wrap is theoretically possible in very long sessions. The replay
    // suites used for one-step validation are far below wrap scale, so we enforce the lower bound
    // unconditionally for deterministic reseed parity.
    uint16_t next_iid = seeded_instance_id_counter;
    uint16_t min_next_iid = (uint16_t)(max_instance_id + 1u);
    if (min_next_iid == 0u) {
      min_next_iid = 1u;
    }
    if (next_iid == 0u || next_iid < min_next_iid) {
      next_iid = min_next_iid;
    }
    batch->state.instance_id_counter[bi] = next_iid;

    // Combat hitlists are part of the reseed schema (teacher-forced one-step eval).
    const size_t base =
        (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
    const size_t hb_base =
        (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
    const size_t hb_valid_base = (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES;
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
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t vi =
            hb_valid_base + ((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb);
        batch->state.combat_hitlist_hb_valid[vi] =
            seed->combat_hitlist_hb_valid[attacker][hb] ? 1u : 0u;
        for (int victim = 0; victim < MSL_MAX_PLAYERS; victim++) {
          const size_t i = hb_base + (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb) *
                                          (size_t)MSL_MAX_PLAYERS +
                                      (size_t)victim);
          batch->state.combat_hitlist_hb_cd[i] = seed->combat_hitlist_hb_cd[attacker][hb][victim];
          batch->state.combat_hitlist_hb_victim_iid[i] =
              seed->combat_hitlist_hb_victim_iid[attacker][hb][victim];
        }
      }
    }

    if (batch->rollout_clock_rng_owned != NULL) {
      batch->rollout_clock_rng_owned[bi] = rollout_owned_after ? 1u : 0u;
    }
  }

  return 0;
}

int msl_batch_reseed_seed(MslBatch* batch, const uint8_t* seed_bytes, size_t seed_stride_bytes) {
  return msl_batch_reseed_seed_impl(batch, seed_bytes, seed_stride_bytes, NULL, 0, 0u);
}

static void msl_batch_commit_rollout_clock_rng(MslBatch* batch) {
  if (batch == NULL || batch->rollout_clock_rng_owned == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (batch->state.opening_input_lock_timer[bi] > 0u) {
      batch->state.opening_input_lock_timer[bi] =
          (uint8_t)(batch->state.opening_input_lock_timer[bi] - 1u);
    }
    if (!batch->rollout_clock_rng_owned[bi]) {
      continue;
    }
    // Simulator-owned rollout metadata: unlike replay teacher-forced rows, match-init episodes
    // advance by one simulated frame after each step and carry the global RNG stream after modeled
    // HSD_Rand/HSD_Randi consumers. Reseed paths overwrite both lanes and clear this ownership bit.
    // RNG source: refs/melee/src/sysdolphin/baselib/random.c::{HSD_Rand,HSD_Randi,HSD_Randf}
    batch->state.frame_id[bi] += 1;
    if (batch->debug_rng_seed_out != NULL) {
      batch->state.frame_pre_random_seed[bi] = batch->debug_rng_seed_out[(size_t)bi];
    }
  }
}

int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes) {
  const int err = step_one_frame(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                 input_stride_bytes);
  if (err != 0) {
    return err;
  }
  msl_batch_commit_rollout_clock_rng(batch);
  return 0;
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

static void msl_write_rl_player_observation(const MslBatch* batch, int bi, int p,
                                            uint8_t team_relation, MslRlPlayerObservation* out) {
  const size_t idx = msl_idx_player(bi, p);
  memset(out, 0, sizeof(*out));
  out->present = 1u;
  out->source_player = (uint8_t)p;
  out->team_relation = team_relation;
  out->team_id = batch->state.team_id[idx];
  out->pos_x = batch->state.pos_x[idx];
  out->pos_y = batch->state.pos_y[idx];
  out->speed_air_x_self = batch->state.speed_air_x_self[idx];
  out->speed_ground_x_self = batch->state.speed_ground_x_self[idx];
  out->speed_y_self = batch->state.speed_y_self[idx];
  out->speed_x_attack = batch->state.speed_x_attack[idx];
  out->speed_y_attack = batch->state.speed_y_attack[idx];
  out->percent = batch->state.percent[idx];
  out->shield_hp = batch->state.shield_hp[idx];
  out->action_id = batch->state.action_id[idx];
  out->action_frame = batch->state.action_frame[idx];
  out->hitlag = batch->state.hitlag[idx];
  out->hitstun = batch->state.hitstun[idx];
  out->char_id = batch->state.char_id[idx];
  out->stocks = batch->state.stocks[idx];
  out->facing = batch->state.facing[idx] ? 1u : 0u;
  out->on_ground = batch->state.on_ground[idx] ? 1u : 0u;
  out->jumps_left = batch->state.jumps_left[idx];
  out->hurtbox_state = batch->state.hurtbox_state[idx];
}

int msl_batch_write_rl_observation(const MslBatch* batch, const uint8_t* viewpoint_player_bytes,
                                   size_t viewpoint_player_stride_bytes, uint8_t* out_bytes,
                                   size_t out_stride_bytes) {
  if (batch == NULL || viewpoint_player_bytes == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (viewpoint_player_stride_bytes < sizeof(uint8_t) ||
      out_stride_bytes < sizeof(MslRlObservation)) {
    return EINVAL;
  }
  const int active_players = (int)batch->config.num_players;
  if (active_players < 1 || active_players > MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t vp = viewpoint_player_bytes[(size_t)bi * viewpoint_player_stride_bytes];
    if ((int)vp >= active_players) {
      return EINVAL;
    }
    MslRlObservation* out = (MslRlObservation*)(out_bytes + (size_t)bi * out_stride_bytes);
    memset(out, 0, sizeof(*out));
    out->frame_id = batch->state.frame_id[bi];
    out->frame_pre_random_seed = batch->state.frame_pre_random_seed[bi];
    out->stage_id = batch->state.stage_id[bi];
    out->num_players = batch->config.num_players;
    out->is_teams = batch->state.is_teams[bi] ? 1u : 0u;
    out->viewpoint_player = vp;
    const size_t vp_idx = msl_idx_player(bi, (int)vp);
    const uint8_t vp_team = batch->state.team_id[vp_idx];
    int slot = 0;
    msl_write_rl_player_observation(batch, bi, (int)vp, 0u, &out->slots[slot++]);
    if (batch->state.is_teams[bi]) {
      for (int p = 0; p < active_players && slot < MSL_MAX_PLAYERS; p++) {
        if (p == (int)vp) {
          continue;
        }
        const size_t idx = msl_idx_player(bi, p);
        if (batch->state.team_id[idx] == vp_team) {
          msl_write_rl_player_observation(batch, bi, p, 1u, &out->slots[slot++]);
        }
      }
    }
    for (int p = 0; p < active_players && slot < MSL_MAX_PLAYERS; p++) {
      if (p == (int)vp) {
        continue;
      }
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.is_teams[bi] && batch->state.team_id[idx] == vp_team) {
        continue;
      }
      msl_write_rl_player_observation(batch, bi, p, 2u, &out->slots[slot++]);
    }
  }

  return 0;
}

int msl_batch_write_terminal(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes,
                             int32_t max_frame_id) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslTerminal)) {
    return EINVAL;
  }
  const int active_players = (int)batch->config.num_players;
  if (active_players < 1 || active_players > MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t alive_count = 0u;
    uint8_t alive_team_count = 0u;
    uint8_t team_alive_mask = 0u;
    uint8_t stockout = 0u;
    for (int p = 0; p < active_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u) {
        stockout = 1u;
      } else {
        alive_count++;
        uint8_t team_bit = batch->state.team_id[idx];
        if (team_bit >= 8u) {
          team_bit = 7u;
        }
        team_alive_mask |= (uint8_t)(1u << team_bit);
      }
    }
    for (uint8_t bit = 0u; bit < 8u; bit++) {
      if ((team_alive_mask & (uint8_t)(1u << bit)) != 0u) {
        alive_team_count++;
      }
    }
    const uint8_t match_ended = batch->state.is_teams[bi]
                                    ? (uint8_t)(alive_team_count <= 1u)
                                    : (uint8_t)(stockout || alive_count <= 1u);
    const uint8_t max_frame_reached =
        (uint8_t)(max_frame_id >= 0 && batch->state.frame_id[bi] >= max_frame_id);

    MslTerminal* out = (MslTerminal*)(out_bytes + (size_t)bi * out_stride_bytes);
    memset(out, 0, sizeof(*out));
    out->frame_id = batch->state.frame_id[bi];
    out->stage_id = batch->state.stage_id[bi];
    out->done = (uint8_t)(match_ended || max_frame_reached);
    out->match_ended = match_ended;
    out->stockout = stockout;
    out->max_frame_reached = max_frame_reached;
    out->alive_count = alive_count;
    out->alive_team_count = alive_team_count;
    out->team_alive_mask = team_alive_mask;
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

    out->instance_id_counter = batch->state.instance_id_counter[bi];

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      out->tilt_timer_x[p] = batch->state.tilt_timer_x[idx];
      out->turn_frames_to_turn[p] = batch->state.turn_frames_to_turn[idx];
      out->turn_has_turned[p] = batch->state.turn_has_turned[idx];
      out->guard_reflect_timer_x14[p] = batch->state.guard_reflect_timer_x14[idx];
      out->entry_end_fall_lock[p] = batch->state.entry_end_fall_lock[idx];
      out->attack_id[p] = batch->state.attack_id[idx];
      out->attack_instance[p] = batch->state.attack_instance[idx];
      out->attack_identity_last_action_id[p] = batch->state.attack_identity_last_action_id[idx];
      out->instance_id[p] = batch->state.instance_id[idx];
      out->instance_id_x2073[p] = batch->state.instance_id_x2073[idx];
      out->instance_identity_last_action_id[p] = batch->state.instance_identity_last_action_id[idx];
      out->throw_pulse_consumed[p] = batch->state.throw_pulse_consumed[idx];
      out->throw_pulse_crossed_prev_frame[p] = batch->state.throw_pulse_crossed_prev_frame[idx];
      out->throw_pending_victim_port[p] = batch->state.throw_pending_victim_port[idx];
      out->throw_pending_hit_idx[p] = batch->state.throw_pending_hit_idx[idx];
      out->attached_victim_port[p] = batch->state.attached_victim_port[idx];
    }
  }

  return 0;
}

int msl_batch_debug_write_collision_contacts(const MslBatch* batch, uint8_t* out_bytes,
                                             size_t out_stride_bytes) {
  if (batch == NULL || out_bytes == NULL) {
    return EINVAL;
  }
  if (out_stride_bytes < sizeof(MslDebugCollisionContacts)) {
    return EINVAL;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t* ptr = out_bytes + (size_t)bi * out_stride_bytes;
    MslDebugCollisionContacts* out = (MslDebugCollisionContacts*)ptr;
    memset(out, 0, sizeof(*out));

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      out->wall_kind[p] = batch->state.wall_kind[idx];
      out->wall_id[p] = batch->state.wall_id[idx];
      out->wall_contact_x[p] = batch->state.wall_contact_x[idx];
      out->wall_contact_y[p] = batch->state.wall_contact_y[idx];
      out->wall_normal_x[p] = batch->state.wall_normal_x[idx];
      out->wall_normal_y[p] = batch->state.wall_normal_y[idx];

      out->ceiling_id[p] = batch->state.ceiling_id[idx];
      out->ceiling_contact_x[p] = batch->state.ceiling_contact_x[idx];
      out->ceiling_contact_y[p] = batch->state.ceiling_contact_y[idx];
      out->ceiling_normal_x[p] = batch->state.ceiling_normal_x[idx];
      out->ceiling_normal_y[p] = batch->state.ceiling_normal_y[idx];

      out->coll_env_flags[p] = batch->state.coll_env_flags[idx];
      out->coll_prev_env_flags[p] = batch->state.coll_prev_env_flags[idx];
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
  msl_anim_timebase_restart(batch, idx, anim_start_f32, anim_speed_f32);
  return 0;
}

int msl_batch_debug_step_input_pre_combat(MslBatch* batch, const uint8_t* prev_input_bytes,
                                          size_t prev_input_stride_bytes,
                                          const uint8_t* input_bytes, size_t input_stride_bytes) {
  return step_one_frame_pre_combat(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                   input_stride_bytes);
}

int msl_batch_debug_knockdown_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return EINVAL;
  }
  // Debug-only branch isolation for Damage/Knockdown Anim ownership tests.
  // Do not route training/rollout code through this helper.
  knockdown_update_pre_physics(batch);
  return 0;
}

int msl_batch_debug_refresh_combat_geometry(MslBatch* batch) {
  if (batch == NULL) {
    return EINVAL;
  }
  anim_pose_update_dynamic_state(batch);
  hurtboxes_refresh(batch);
  hitboxes_refresh(batch);
  return 0;
}

int msl_batch_debug_dynamic_pose_state(const MslBatch* batch, int batch_index, int player_index,
                                       MslDebugDynamicPoseState* out_state) {
  if (batch == NULL || out_state == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  memset(out_state, 0, sizeof(*out_state));
  if (player_index >= (int)batch->config.num_players) {
    return 0;
  }

  const size_t idx = msl_idx_player(batch_index, player_index);
  out_state->player = (uint8_t)player_index;
  out_state->char_id = batch->state.dynamic_pose_char_id[idx];
  out_state->state_valid = batch->state.dynamic_pose_state_valid[idx];
  out_state->apply_collision_matrix = batch->state.dynamic_pose_apply_collision_matrix[idx];
  out_state->node_count = batch->state.dynamic_pose_node_count[idx];
  out_state->msid = batch->state.dynamic_pose_msid[idx];
  out_state->frame = batch->state.dynamic_pose_frame[idx];
  for (uint8_t dn = 0; dn < (uint8_t)MSL_MAX_DYNAMIC_NODES; dn++) {
    const size_t di = idx * (size_t)MSL_MAX_DYNAMIC_NODES + (size_t)dn;
    out_state->rot_x[dn] = batch->state.dynamic_pose_rot_x[di];
    out_state->rot_y[dn] = batch->state.dynamic_pose_rot_y[di];
    out_state->rot_z[dn] = batch->state.dynamic_pose_rot_z[di];
    out_state->pos_x[dn] = batch->state.dynamic_pose_pos_x[di];
    out_state->pos_y[dn] = batch->state.dynamic_pose_pos_y[di];
    out_state->pos_z[dn] = batch->state.dynamic_pose_pos_z[di];
    out_state->axis_x[dn] = batch->state.dynamic_pose_axis_x[di];
    out_state->axis_y[dn] = batch->state.dynamic_pose_axis_y[di];
    out_state->axis_z[dn] = batch->state.dynamic_pose_axis_z[di];
    out_state->angle[dn] = batch->state.dynamic_pose_angle[di];
  }
  return 0;
}

int msl_batch_debug_timebase(const MslBatch* batch, int batch_index, float* out_rows_8p) {
  if (batch == NULL || out_rows_8p == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }

  // Zero-fill full fixed-size output for stable debug snapshots.
  memset(out_rows_8p, 0, sizeof(float) * (size_t)MSL_MAX_PLAYERS * 8u);

  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    const size_t o = (size_t)p * 8u;
    if (p >= num_players) {
      continue;
    }
    const size_t idx = msl_idx_player(batch_index, p);
    const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
    const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
    const float speed_mul_f32 = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
    out_rows_8p[o + 0] = (float)batch->state.action_id[idx];
    out_rows_8p[o + 1] = (float)batch->state.animation_index[idx];
    out_rows_8p[o + 2] = (float)batch->state.action_frame[idx];
    out_rows_8p[o + 3] = anim_frame_f32;
    out_rows_8p[o + 4] = speed_mul_f32;
    out_rows_8p[o + 5] = (float)pose_frame;
    out_rows_8p[o + 6] = (float)batch->state.hitlag_started_frame[idx];
    out_rows_8p[o + 7] = (float)batch->state.hurtbox_state[idx];
  }

  return 0;
}

static inline uint8_t hb_events_affects_slot(const MslHitboxEvent* ev, uint8_t hb_id) {
  if (ev == NULL) {
    return 0;
  }
  if (ev->kind != 1) {
    return (ev->hitbox_id == hb_id) ? 1u : 0u;
  }
  // Clear event: hb_id==0xFF clears all.
  if (ev->hitbox_id == 0xFFu) {
    return 1u;
  }
  return (ev->hitbox_id == hb_id) ? 1u : 0u;
}

static inline void debug_hb_defs_apply_event(const MslHitboxEvent* ev,
                                             uint8_t have_def[MSL_MAX_HITBOXES],
                                             MslHitboxEvent def[MSL_MAX_HITBOXES]) {
  if (ev == NULL) {
    return;
  }
  if (ev->kind == 1) {
    if (ev->hitbox_id == 0xFFu) {
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        have_def[hi] = 0;
      }
      return;
    }
    if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
      have_def[ev->hitbox_id] = 0;
    }
    return;
  }
  if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
    have_def[ev->hitbox_id] = 1;
    def[ev->hitbox_id] = *ev;
  }
}

static uint8_t debug_sample_hitbox_center_proxy(const MslBatch* batch, size_t idx, uint8_t char_id,
                                                uint16_t msid, uint16_t pose_frame,
                                                const MslHitboxEvent* def, float* out_x,
                                                float* out_y, float* out_z, float* out_radius) {
  if (batch == NULL || def == NULL || out_x == NULL || out_y == NULL || out_z == NULL ||
      out_radius == NULL) {
    return 0;
  }

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, pose_frame, def->bone_part_id, m) != 0) {
    return 0;
  }

  const float pos_x = batch->state.pos_x[idx];
  const float pos_y = batch->state.pos_y[idx];
  const float pos_z = batch->state.pos_z[idx];
  const float scale_y = batch->state.fighter_scale_y[idx];
  const MslCharParams* chp = msl_char_params(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float model_scale = scale_y * model_scaling;
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

  const float off[3] = {def->x, def->y, def->z};
  float cx = 0.0f;
  float cy = 0.0f;
  float cz = 0.0f;
  msl_mtx34_mul_point(m, off, &cx, &cy, &cz);
  cx *= model_scale;
  cy *= model_scale;
  cz *= model_scale;

  if (def->bone_part_id < 256u &&
      batch->state.action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
      msl_anim_part_under_xrotn(char_id, def->bone_part_id)) {
    float xrotn_m[12];
    if (anim_pose_get_matrix(char_id, msid, pose_frame, 2u, xrotn_m) == 0) {
      const float vel_x = batch->state.speed_air_x_self[idx];
      const float vel_y = batch->state.speed_y_self[idx];
      if (fabsf(vel_x) > 0.0f || fabsf(vel_y) > 0.0f) {
        float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
        float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const float local_x[3] = {1.0f, 0.0f, 0.0f};
        msl_mtx34_mul_point(xrotn_m, origin, &ax0, &ay0, &az0);
        msl_mtx34_mul_point(xrotn_m, local_x, &ax1, &ay1, &az1);
        ax0 *= model_scale;
        ay0 *= model_scale;
        az0 *= model_scale;
        ax1 *= model_scale;
        ay1 *= model_scale;
        az1 *= model_scale;

        float axis_x = ax1 - ax0;
        float axis_y = ay1 - ay0;
        float axis_z = az1 - az0;
        const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
        if (axis_len > 0.0f) {
          axis_x /= axis_len;
          axis_y /= axis_len;
          axis_z /= axis_len;

          // Decomp: Firefox/Firebird launch rotates FtPart_XRotN by `2*pi - rotateModel`,
          // where `rotateModel = atan2f(self_vel.y, self_vel.x * facing_dir)`.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
          //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
          const float angle = (2.0f * MSL_PI_F) - atan2f(vel_y, vel_x * facing_dir);
          const float px = cx - ax0;
          const float py = cy - ay0;
          const float pz = cz - az0;
          const float c = cosf(angle);
          const float s = sinf(angle);
          const float dot = axis_x * px + axis_y * py + axis_z * pz;
          const float cross_x = axis_y * pz - axis_z * py;
          const float cross_y = axis_z * px - axis_x * pz;
          const float cross_z = axis_x * py - axis_y * px;
          cx = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
          cy = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
          cz = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
        }
      }
    }
  }

  const float cx_rot_x = facing_dir * cz;
  const float cx_rot_z = -facing_dir * cx;
  cx = cx_rot_x + pos_x;
  cy += pos_y;
  cz = cx_rot_z + pos_z;

  float radius = def->radius;
  if (!msl_hitbox_ignore_fighter_scale(def->u16_6)) {
    radius *= scale_y;
  }

  *out_x = cx;
  *out_y = cy;
  *out_z = cz;
  *out_radius = radius;
  return 1;
}

int msl_batch_debug_hitbox_event_timing(const MslBatch* batch, int batch_index, int attacker,
                                        int hb_id, MslDebugHitboxEventTiming* out_timing) {
  if (batch == NULL || out_timing == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  memset(out_timing, 0, sizeof(*out_timing));
  out_timing->attacker = (uint8_t)attacker;
  out_timing->hb_id = (uint8_t)hb_id;
  out_timing->last_affect_kind_le = 0xFFu;
  out_timing->last_affect_kind_eq = 0xFFu;
  out_timing->start_frame = -1;
  out_timing->end_frame = -1;
  out_timing->prev_hit_group = 0xFFu;
  out_timing->cur_hit_group = 0xFFu;

  const int num_players = (int)batch->config.num_players;
  if (attacker >= num_players) {
    return 0;
  }

  const size_t idx = msl_idx_player(batch_index, attacker);
  const uint8_t char_id = batch->state.char_id[idx];
  out_timing->char_id = char_id;

  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return 0;
  }

  const uint16_t msid = (uint16_t)anim_u32;
  out_timing->msid = msid;

  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  out_timing->anim_frame_f32 = anim_frame_f32;
  const float speed_mul_f32 = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
  out_timing->frame_speed_mul_f32 = speed_mul_f32;

  const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
  out_timing->pose_frame = pose_frame;

  const MslHitboxEvent* events = NULL;
  uint16_t event_count = 0;
  if (hitboxes_get_events(char_id, msid, &events, &event_count) != 0 || events == NULL ||
      event_count == 0) {
    return 0;
  }

  uint8_t enabled_prev = 0;
  uint16_t u16_7_prev = 0;
  int16_t start_prev = -1;

  // Build state at the end of (pose_frame - 1) by applying events strictly before pose_frame.
  for (uint16_t ei = 0; ei < event_count; ei++) {
    const MslHitboxEvent* ev = &events[ei];
    if ((float)ev->frame > anim_frame_f32) {
      continue;
    }
    if (ev->frame >= pose_frame) {
      break;
    }
    if (!hb_events_affects_slot(ev, (uint8_t)hb_id)) {
      continue;
    }
    if (ev->kind == 1) {
      enabled_prev = 0;
      u16_7_prev = 0;
      start_prev = -1;
      out_timing->last_affect_kind_le = 1u;
      out_timing->last_affect_frame_le = ev->frame;
      out_timing->last_affect_u16_7_le = 0;
    } else {
      enabled_prev = 1;
      u16_7_prev = ev->u16_7;
      start_prev = (int16_t)ev->frame;
      out_timing->last_affect_kind_le = 0u;
      out_timing->last_affect_frame_le = ev->frame;
      out_timing->last_affect_u16_7_le = ev->u16_7;
    }
  }

  out_timing->enabled_prev = enabled_prev;
  if (enabled_prev) {
    out_timing->prev_hit_group = hitlist_hit_group_from_u16_7(u16_7_prev);
  }

  // Initialize current from prev and apply all pose_frame events in order.
  uint8_t enabled_cur = enabled_prev;
  uint16_t u16_7_cur = u16_7_prev;
  int16_t start_cur = start_prev;

  uint8_t pose_create = 0;
  uint8_t pose_clear = 0;
  uint8_t pose_clear_all = 0;
  uint8_t enable_edge = 0;

  for (uint16_t ei = 0; ei < event_count; ei++) {
    const MslHitboxEvent* ev = &events[ei];
    if ((float)ev->frame > anim_frame_f32) {
      continue;
    }
    if (ev->frame > pose_frame) {
      break;
    }
    if (ev->frame != pose_frame) {
      continue;
    }

    const uint8_t affects = hb_events_affects_slot(ev, (uint8_t)hb_id);
    if (!affects) {
      continue;
    }

    if (ev->kind == 1) {
      if (ev->hitbox_id == 0xFFu) {
        pose_clear_all++;
      } else {
        pose_clear++;
      }
      enabled_cur = 0;
      u16_7_cur = 0;
      start_cur = -1;

      out_timing->last_affect_kind_le = 1u;
      out_timing->last_affect_frame_le = ev->frame;
      out_timing->last_affect_u16_7_le = 0;
      out_timing->last_affect_kind_eq = 1u;
      out_timing->last_affect_frame_eq = ev->frame;
      out_timing->last_affect_u16_7_eq = 0;
    } else {
      pose_create++;

      const uint8_t had_old = enabled_cur ? 1u : 0u;
      const uint8_t old_g = had_old ? hitlist_hit_group_from_u16_7(u16_7_cur) : 0u;
      const uint8_t new_g = hitlist_hit_group_from_u16_7(ev->u16_7);
      if (!had_old || old_g != new_g) {
        enable_edge = 1u;
      }

      enabled_cur = 1;
      u16_7_cur = ev->u16_7;
      start_cur = (int16_t)ev->frame;

      out_timing->last_affect_kind_le = 0u;
      out_timing->last_affect_frame_le = ev->frame;
      out_timing->last_affect_u16_7_le = ev->u16_7;
      out_timing->last_affect_kind_eq = 0u;
      out_timing->last_affect_frame_eq = ev->frame;
      out_timing->last_affect_u16_7_eq = ev->u16_7;
    }
  }

  out_timing->pose_create_count = pose_create;
  out_timing->pose_clear_count = pose_clear;
  out_timing->pose_clear_all_count = pose_clear_all;
  out_timing->enable_edge = enable_edge;

  out_timing->enabled_cur = enabled_cur;
  if (enabled_cur) {
    out_timing->cur_hit_group = hitlist_hit_group_from_u16_7(u16_7_cur);
    out_timing->start_frame = start_cur;

    // Find the next clear (for hb_id or clear-all) after pose_frame.
    for (uint16_t ei = 0; ei < event_count; ei++) {
      const MslHitboxEvent* ev = &events[ei];
      if (ev->frame <= pose_frame) {
        continue;
      }
      if (ev->kind != 1) {
        continue;
      }
      if (!hb_events_affects_slot(ev, (uint8_t)hb_id)) {
        continue;
      }
      out_timing->end_frame = (int16_t)ev->frame;
      break;
    }
  }

  return 0;
}

int msl_batch_debug_hitbox_sweep_proxy(const MslBatch* batch, int batch_index, int attacker,
                                       int hb_id, MslDebugHitboxSweepProxy* out_proxy) {
  if (batch == NULL || out_proxy == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  memset(out_proxy, 0, sizeof(*out_proxy));
  out_proxy->attacker = (uint8_t)attacker;
  out_proxy->hb_id = (uint8_t)hb_id;
  out_proxy->arg3_var_r22_known = 1u;
  out_proxy->arg3_var_r22_from_extracted = 0u;
  out_proxy->arg3_var_r22_gates_collision = 1u;

  const int num_players = (int)batch->config.num_players;
  if (attacker >= num_players) {
    return 0;
  }

  const size_t idx = msl_idx_player(batch_index, attacker);
  const uint8_t char_id = batch->state.char_id[idx];
  out_proxy->char_id = char_id;

  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return 0;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  out_proxy->msid = msid;

  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const float speed_mul_f32 = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
  const float prev_anim_frame_f32 = msl_anim_frame_sanitize_f32(anim_frame_f32 - speed_mul_f32);
  const uint16_t pose_cur = msl_anim_frame_floor_u16(anim_frame_f32);
  const uint16_t pose_prev = msl_anim_frame_floor_u16(prev_anim_frame_f32);

  out_proxy->anim_frame_f32 = anim_frame_f32;
  out_proxy->frame_speed_mul_f32 = speed_mul_f32;
  out_proxy->prev_anim_frame_f32 = prev_anim_frame_f32;
  out_proxy->pose_cur = pose_cur;
  out_proxy->pose_prev = pose_prev;

  const MslHitboxEvent* events = NULL;
  uint16_t event_count = 0;
  if (hitboxes_get_events(char_id, msid, &events, &event_count) != 0 || events == NULL ||
      event_count == 0) {
    return 0;
  }

  uint8_t have_prev[MSL_MAX_HITBOXES] = {0};
  uint8_t have_cur[MSL_MAX_HITBOXES] = {0};
  MslHitboxEvent def_prev[MSL_MAX_HITBOXES] = {0};
  MslHitboxEvent def_cur[MSL_MAX_HITBOXES] = {0};

  for (uint16_t ei = 0; ei < event_count; ei++) {
    const MslHitboxEvent* ev = &events[ei];
    if ((float)ev->frame > anim_frame_f32) {
      break;
    }
    if (ev->frame > pose_cur) {
      break;
    }
    debug_hb_defs_apply_event(ev, have_cur, def_cur);
    if (ev->frame <= pose_prev) {
      debug_hb_defs_apply_event(ev, have_prev, def_prev);
    }
  }

  const int slot = hb_id;
  out_proxy->enabled_prev = have_prev[slot] ? 1u : 0u;
  out_proxy->enabled_cur = have_cur[slot] ? 1u : 0u;

  if (have_prev[slot]) {
    out_proxy->u16_6_prev = def_prev[slot].u16_6;
    out_proxy->u16_7_prev = def_prev[slot].u16_7;
    if (debug_sample_hitbox_center_proxy(batch, idx, char_id, msid, pose_prev, &def_prev[slot],
                                         &out_proxy->prev_x, &out_proxy->prev_y, &out_proxy->prev_z,
                                         &out_proxy->prev_radius)) {
      out_proxy->prev_valid = 1u;
    }
  }
  if (have_cur[slot]) {
    out_proxy->u16_6_cur = def_cur[slot].u16_6;
    out_proxy->u16_7_cur = def_cur[slot].u16_7;
    if (debug_sample_hitbox_center_proxy(batch, idx, char_id, msid, pose_cur, &def_cur[slot],
                                         &out_proxy->cur_x, &out_proxy->cur_y, &out_proxy->cur_z,
                                         &out_proxy->cur_radius)) {
      out_proxy->cur_valid = 1u;
    }
  }

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

int msl_batch_debug_hurtcap_slot_flags(const MslBatch* batch, int batch_index, int player_index,
                                       int cap_id, MslDebugHurtcapSlotFlags* out_flags) {
  if (batch == NULL || out_flags == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (player_index < 0 || player_index >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }
  const int num_players = (int)batch->config.num_players;
  if (player_index >= num_players) {
    return EINVAL;
  }

  memset(out_flags, 0, sizeof(*out_flags));
  out_flags->cap_id = (uint16_t)cap_id;

  const size_t p_idx = msl_idx_player(batch_index, player_index);
  const size_t hc_idx = debug_idx_hurtcap(batch_index, player_index, cap_id);

  out_flags->enabled = batch->state.hurtcap_enabled[hc_idx];
  out_flags->height = batch->state.hurtcap_height[hc_idx];
  out_flags->is_grabbable = batch->state.hurtcap_is_grabbable[hc_idx];
  out_flags->char_id = batch->state.char_id[p_idx];

  const uint32_t anim_u32 = batch->state.animation_index[p_idx];
  if (anim_u32 > 0xFFFFu) {
    return 0;
  }

  const uint16_t msid = (uint16_t)anim_u32;
  out_flags->msid = msid;
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[p_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  out_flags->frame = frame;

  const uint8_t cap_count_u8 = batch->state.hurtcap_count[p_idx];
  const uint16_t cap_count = (cap_count_u8 > (uint8_t)MSL_MAX_HURTCAPS) ? (uint16_t)MSL_MAX_HURTCAPS
                                                                        : (uint16_t)cap_count_u8;
  uint32_t can_hit_mask = 0xFFFFFFFFu;
  (void)hurtbox_modes_can_hit_mask(out_flags->char_id, msid, frame, cap_count, &can_hit_mask);
  out_flags->can_hit_mask = can_hit_mask;
  out_flags->mode_can_hit_bit = (uint8_t)((can_hit_mask >> cap_id) & 0x1u);

  return 0;
}

int msl_batch_debug_attackairb_continuation_overlap(const MslBatch* batch, int batch_index,
                                                    int attacker, int hb_id, int defender,
                                                    int cap_id, float* out_overlap) {
  return combat_debug_attackairb_continuation_overlap(batch, batch_index, attacker, hb_id, defender,
                                                      cap_id, out_overlap);
}

int msl_batch_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                        int hb_id, int defender, int cap_id, float* out_overlap) {
  return combat_debug_body_matrix_overlap(batch, batch_index, attacker, hb_id, defender, cap_id,
                                          out_overlap);
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

int msl_batch_debug_shield_candidate_decisions(MslBatch* batch, int batch_index,
                                               MslDebugShieldCandidateDecision* out_rows,
                                               uint16_t max_rows, uint16_t* out_count) {
  return combat_debug_shield_candidate_decisions(batch, batch_index, out_rows, max_rows, out_count);
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
    batch->state.hitbox_x43_b2[hb_i] = 0u;
  }

  // Debug helper: approximate the engine's "clear hitboxes" behavior by also resetting rehit
  // suppression for this attacker so that re-enabling a hitbox can immediately apply a new hit.
  hitlist_debug_clear_fighter_attacker(batch, batch_index, player_index);

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
  if (!enabled) {
    batch->state.hitbox_x43_b2[hb_i] = 0u;
  }

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

int msl_batch_debug_hitlist_fighter_contains(const MslBatch* batch, int batch_index, int attacker,
                                             int hb_id, int victim, int* out_present) {
  if (out_present == NULL) {
    return EINVAL;
  }
  *out_present = 0;
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }
  if (victim < 0 || victim >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }

  const size_t hl_i = ((size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) *
                          (size_t)MSL_MAX_HITBOXES +
                      (size_t)hb_id;
  const uint8_t key =
      msl_hitlist_victim_pack((uint8_t)MSL_HITLIST_VICTIM_KIND_FIGHTER, (uint8_t)victim);
  const MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
  for (int i = 0; i < (int)MSL_HITLIST_VICTIM_CAP; i++) {
    if (hit->victims_1[i].kind_slot == key) {
      *out_present = 1;
      break;
    }
  }
  return 0;
}

int msl_batch_debug_hitlist_fighter_capsule(const MslBatch* batch, int batch_index, int attacker,
                                            int hb_id, MslDebugHitlistCapsule* out_capsule) {
  if (out_capsule == NULL) {
    return EINVAL;
  }
  memset(out_capsule, 0, sizeof(*out_capsule));
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return EINVAL;
  }

  const size_t hl_i = ((size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) *
                          (size_t)MSL_MAX_HITBOXES +
                      (size_t)hb_id;
  const MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
  out_capsule->ring_1 = hit->ring_1;
  out_capsule->ring_2 = hit->ring_2;
  for (int i = 0; i < (int)MSL_HITLIST_VICTIM_CAP; i++) {
    const MslHitlistVictimEntry* s1 = &hit->victims_1[i];
    MslDebugHitlistVictimEntry* d1 = &out_capsule->victims_1[i];
    d1->id32 = s1->id32;
    d1->id16 = s1->id16;
    d1->kind_slot = s1->kind_slot;
    d1->cd = s1->cd;

    const MslHitlistVictimEntry* s2 = &hit->victims_2[i];
    MslDebugHitlistVictimEntry* d2 = &out_capsule->victims_2[i];
    d2->id32 = s2->id32;
    d2->id16 = s2->id16;
    d2->kind_slot = s2->kind_slot;
    d2->cd = s2->cd;
  }
  return 0;
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
