#pragma once

#include <stddef.h>

#include "batch_internal.h"

static inline void msl_mpcoll_copy_colldata_lane(MslBatch* batch, size_t dst, size_t src) {
  if (batch == NULL || dst == src) {
    return;
  }

  // Source `mpCopyCollData` copies CollData-owned root, ECB, env/contact, surface, skip, and
  // joint_id_skip state. MSL keeps only modeled CollData fields here; action ids, animation state,
  // player stocks, joint_id_only, and other Fighter-owned/non-copied lanes are intentionally not
  // copied.
  // refs/melee/src/melee/mp/mpcoll.c::mpCopyCollData
  batch->state.pos_x[dst] = batch->state.pos_x[src];
  batch->state.pos_y[dst] = batch->state.pos_y[src];
  batch->state.prev_pos_x[dst] = batch->state.prev_pos_x[src];
  batch->state.prev_pos_y[dst] = batch->state.prev_pos_y[src];
  batch->state.floor_sweep_prev_pos_x[dst] = batch->state.floor_sweep_prev_pos_x[src];
  batch->state.floor_sweep_prev_pos_y[dst] = batch->state.floor_sweep_prev_pos_y[src];
  batch->state.floor_sweep_prev_source_owned[dst] = batch->state.floor_sweep_prev_source_owned[src];
  batch->state.floor_sweep_prev_runtime_owned[dst] =
      batch->state.floor_sweep_prev_runtime_owned[src];
  batch->state.coll_wall_ceil_prev_pos_x[dst] = batch->state.coll_wall_ceil_prev_pos_x[src];
  batch->state.coll_wall_ceil_prev_pos_y[dst] = batch->state.coll_wall_ceil_prev_pos_y[src];
  batch->state.coll_wall_ceil_prev_pos_valid[dst] = batch->state.coll_wall_ceil_prev_pos_valid[src];
  batch->state.coll_substep_prev_pos_x[dst] = batch->state.coll_substep_prev_pos_x[src];
  batch->state.coll_substep_prev_pos_y[dst] = batch->state.coll_substep_prev_pos_y[src];
  batch->state.coll_substep_cur_pos_x[dst] = batch->state.coll_substep_cur_pos_x[src];
  batch->state.coll_substep_cur_pos_y[dst] = batch->state.coll_substep_cur_pos_y[src];
  batch->state.coll_last_pos_x[dst] = batch->state.coll_last_pos_x[src];
  batch->state.coll_last_pos_y[dst] = batch->state.coll_last_pos_y[src];

  batch->state.facing_dir1[dst] = batch->state.facing_dir1[src];
  batch->state.floor_skip_segment_id[dst] = batch->state.floor_skip_segment_id[src];
  batch->state.mpcoll_joint_id_skip[dst] = batch->state.mpcoll_joint_id_skip[src];

  batch->state.coll_ecb_bottom_rel_y[dst] = batch->state.coll_ecb_bottom_rel_y[src];
  batch->state.coll_ecb_top_rel_y[dst] = batch->state.coll_ecb_top_rel_y[src];
  batch->state.coll_ecb_left_rel_x[dst] = batch->state.coll_ecb_left_rel_x[src];
  batch->state.coll_ecb_right_rel_x[dst] = batch->state.coll_ecb_right_rel_x[src];
  batch->state.coll_ecb_side_rel_y[dst] = batch->state.coll_ecb_side_rel_y[src];
  batch->state.coll_prev_ecb_bottom_rel_y[dst] = batch->state.coll_prev_ecb_bottom_rel_y[src];
  batch->state.coll_prev_ecb_top_rel_y[dst] = batch->state.coll_prev_ecb_top_rel_y[src];
  batch->state.coll_prev_ecb_left_rel_x[dst] = batch->state.coll_prev_ecb_left_rel_x[src];
  batch->state.coll_prev_ecb_right_rel_x[dst] = batch->state.coll_prev_ecb_right_rel_x[src];
  batch->state.coll_prev_ecb_side_rel_y[dst] = batch->state.coll_prev_ecb_side_rel_y[src];
  batch->state.coll_desired_ecb_bottom_rel_y[dst] = batch->state.coll_desired_ecb_bottom_rel_y[src];
  batch->state.coll_desired_ecb_top_rel_y[dst] = batch->state.coll_desired_ecb_top_rel_y[src];
  batch->state.coll_desired_ecb_left_rel_x[dst] = batch->state.coll_desired_ecb_left_rel_x[src];
  batch->state.coll_desired_ecb_right_rel_x[dst] = batch->state.coll_desired_ecb_right_rel_x[src];
  batch->state.coll_desired_ecb_side_rel_y[dst] = batch->state.coll_desired_ecb_side_rel_y[src];
  batch->state.coll_squeeze_restore_ecb_bottom_rel_y[dst] =
      batch->state.coll_squeeze_restore_ecb_bottom_rel_y[src];
  batch->state.coll_squeeze_restore_ecb_top_rel_y[dst] =
      batch->state.coll_squeeze_restore_ecb_top_rel_y[src];
  batch->state.coll_squeeze_restore_ecb_left_rel_x[dst] =
      batch->state.coll_squeeze_restore_ecb_left_rel_x[src];
  batch->state.coll_squeeze_restore_ecb_right_rel_x[dst] =
      batch->state.coll_squeeze_restore_ecb_right_rel_x[src];
  batch->state.coll_squeeze_restore_ecb_side_rel_y[dst] =
      batch->state.coll_squeeze_restore_ecb_side_rel_y[src];
  batch->state.coll_ecb_bottom_valid[dst] = batch->state.coll_ecb_bottom_valid[src];
  batch->state.coll_prev_ecb_bottom_valid[dst] = batch->state.coll_prev_ecb_bottom_valid[src];
  batch->state.coll_desired_ecb_bottom_valid[dst] = batch->state.coll_desired_ecb_bottom_valid[src];
  batch->state.coll_desired_ecb_bottom_locked_owner[dst] =
      batch->state.coll_desired_ecb_bottom_locked_owner[src];
  batch->state.coll_common_fall_blended_ecb_seed_valid[dst] =
      batch->state.coll_common_fall_blended_ecb_seed_valid[src];
  batch->state.coll_squeeze_restore_ecb_valid[dst] =
      batch->state.coll_squeeze_restore_ecb_valid[src];
  batch->state.coll_damage_hitlag_ecb_valid[dst] = batch->state.coll_damage_hitlag_ecb_valid[src];
  batch->state.coll_damage_hitlag_ecb_source_kind[dst] =
      batch->state.coll_damage_hitlag_ecb_source_kind[src];

  batch->state.coll_env_flags[dst] = batch->state.coll_env_flags[src];
  batch->state.coll_prev_env_flags[dst] = batch->state.coll_prev_env_flags[src];
  batch->state.ground_id[dst] = batch->state.ground_id[src];
  batch->state.ground_contact_x[dst] = batch->state.ground_contact_x[src];
  batch->state.ground_contact_y[dst] = batch->state.ground_contact_y[src];
  batch->state.ground_normal_x[dst] = batch->state.ground_normal_x[src];
  batch->state.ground_normal_y[dst] = batch->state.ground_normal_y[src];
  batch->state.wall_id[dst] = batch->state.wall_id[src];
  batch->state.wall_kind[dst] = batch->state.wall_kind[src];
  batch->state.wall_contact_x[dst] = batch->state.wall_contact_x[src];
  batch->state.wall_contact_y[dst] = batch->state.wall_contact_y[src];
  batch->state.wall_normal_x[dst] = batch->state.wall_normal_x[src];
  batch->state.wall_normal_y[dst] = batch->state.wall_normal_y[src];
  batch->state.ceiling_id[dst] = batch->state.ceiling_id[src];
  batch->state.ceiling_contact_x[dst] = batch->state.ceiling_contact_x[src];
  batch->state.ceiling_contact_y[dst] = batch->state.ceiling_contact_y[src];
  batch->state.ceiling_normal_x[dst] = batch->state.ceiling_normal_x[src];
  batch->state.ceiling_normal_y[dst] = batch->state.ceiling_normal_y[src];

  batch->state.coll_floor_result_valid[dst] = batch->state.coll_floor_result_valid[src];
  batch->state.coll_floor_result_source[dst] = batch->state.coll_floor_result_source[src];
  batch->state.coll_floor_result_mode[dst] = batch->state.coll_floor_result_mode[src];
  batch->state.coll_floor_result_segment_id[dst] = batch->state.coll_floor_result_segment_id[src];
  batch->state.coll_floor_result_contact_x[dst] = batch->state.coll_floor_result_contact_x[src];
  batch->state.coll_floor_result_contact_y[dst] = batch->state.coll_floor_result_contact_y[src];
  batch->state.coll_floor_result_normal_x[dst] = batch->state.coll_floor_result_normal_x[src];
  batch->state.coll_floor_result_normal_y[dst] = batch->state.coll_floor_result_normal_y[src];
}
