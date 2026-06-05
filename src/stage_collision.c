#include "stage_collision.h"
#include "ids.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "action_ids.h"
#include "combat.h"
#include "mpcoll_env.h"
#include "mpcoll_ground.h"
#include "mpcoll_wall_ceil.h"
#include "stage_item_params.h"

typedef struct {
  float left;
  float right;
  float top;
  float bottom;
} MslStageBoundsWorld;

typedef struct {
  uint16_t line_id;
  uint8_t kind_id;
  uint8_t platform_id;
  float x0;
  float x1;
  float y_const;
  float height_coeff;
} MslStagePlatformTransform;

typedef struct {
  uint16_t line_id;
  uint16_t frame;
  float x0;
  float y;
  float x1;
} MslStagePlatformPathFrame;

typedef struct {
  uint8_t loaded;
  float home_height;
  float hidden_target_height;
  float max_height;
  float min_visible_height;
  float up_speed;
  float down_speed;
  float wait_min_frames;
  float wait_max_frames;
  float hidden_wait_min_frames;
  float hidden_wait_max_frames;
  float target_delta_min;
  float target_delta_max;
  float bias_below_home;
  float bias_above_home;
  float hidden_weight;
  float stay_weight;
  float move_weight;
} MslFodPlatformMotion;

static inline uint8_t stage_line_x_contains_closed(const MslStageFloorLine* line, float x) {
  return (uint8_t)(x >= line->x0 && x <= line->x1);
}

static inline float stage_line_y_at_x(const MslStageFloorLine* line, float x) {
  const float dx = line->x1 - line->x0;
  if (dx == 0.0f) {
    return (line->y0 > line->y1) ? line->y0 : line->y1;
  }
  const float t = (x - line->x0) / dx;
  return line->y0 + (line->y1 - line->y0) * t;
}

enum {
  MSL_STAGE_ID_LOOKUP_COUNT = 256,
  MSL_STAGE_SEGMENT_LOOKUP_COUNT = 65536,
};

typedef struct {
  uint32_t stage_id;
  const char* bin_name;
  MslStageFloorLine* floor_lines;
  size_t floor_line_count;
  MslStageFloorLine* fighter_floor_lines;
  size_t fighter_floor_line_count;
  MslStageCeilingLine* ceiling_lines;
  size_t ceiling_line_count;
  MslStageWallLine* left_wall_lines;
  size_t left_wall_line_count;
  MslStageWallLine* right_wall_lines;
  size_t right_wall_line_count;
  MslStagePlatformTransform* platform_transforms;
  size_t platform_transform_count;
  MslStagePlatformPathFrame* platform_path_frames;
  size_t platform_path_frame_count;
  MslFodPlatformMotion fod_motion;
  int loaded;
  uint8_t match_flow_loaded;
  MslStageFloorGraph floor_graph;
  MslStageFloorGraph fighter_floor_graph;
  MslStageCeilingGraph ceiling_graph;
  MslStageWallGraph left_wall_graph;
  MslStageWallGraph right_wall_graph;
  int16_t floor_line_index_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  int16_t fighter_floor_line_index_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  int16_t ceiling_line_index_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  int16_t left_wall_line_index_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  int16_t right_wall_line_index_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  uint8_t platform_transform_kind_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  uint8_t platform_transform_id_by_segment[MSL_STAGE_SEGMENT_LOOKUP_COUNT];
  MslStageBoundsWorld blast_bounds_world;
  MslStageBoundsWorld cam_bounds_world;
  MslStagePoint2 spawn_points[MSL_MAX_PLAYERS];
  MslStagePoint2 respawn_points[MSL_MAX_PLAYERS];
  MslStagePoint2 ledge_points[2];
  uint8_t have_ledge_points[2];
  int16_t ledge_floor_line_idx[2];
  uint8_t has_flat_between_sloped_ledges;
  uint8_t has_only_static_cardinal_hard_floors;
  uint8_t has_alternate_floor_endpoint_links;
  uint8_t has_height_platform_transform;
  uint8_t has_deferred_static_floor_transform;
} MslStageSlot;

static MslStageSlot g_stage_slots[] = {
    // Stage ids are the Slippi/stage enum domain. Binaries are ISO-derived MSLSTG01 artifacts.
    // agent_docs/DATA_CONTRACT.md::MSLSTG01
    {.stage_id = MSL_STAGE_ID_FOUNTAIN_OF_DREAMS, .bin_name = "griz.bin"},
    {.stage_id = MSL_STAGE_ID_POKEMON_STADIUM, .bin_name = "grps.bin"},
    {.stage_id = MSL_STAGE_ID_YOSHIS_STORY, .bin_name = "grst.bin"},
    {.stage_id = MSL_STAGE_ID_DREAM_LAND_N64, .bin_name = "grop.bin"},
    {.stage_id = MSL_STAGE_ID_BATTLEFIELD, .bin_name = "grnba.bin"},
    {.stage_id = MSL_STAGE_ID_FINAL_DESTINATION, .bin_name = "grnla.bin"},
};

static MslStageSlot* g_stage_slot_by_id[MSL_STAGE_ID_LOOKUP_COUNT] = {
    [MSL_STAGE_ID_FOUNTAIN_OF_DREAMS] = &g_stage_slots[0],
    [MSL_STAGE_ID_POKEMON_STADIUM] = &g_stage_slots[1],
    [MSL_STAGE_ID_YOSHIS_STORY] = &g_stage_slots[2],
    [MSL_STAGE_ID_DREAM_LAND_N64] = &g_stage_slots[3],
    [MSL_STAGE_ID_BATTLEFIELD] = &g_stage_slots[4],
    [MSL_STAGE_ID_FINAL_DESTINATION] = &g_stage_slots[5],
};

static MslStageSlot* stage_slot_mut(uint32_t stage_id) {
  if (stage_id < (uint32_t)MSL_STAGE_ID_LOOKUP_COUNT) {
    return g_stage_slot_by_id[stage_id];
  }
  return NULL;
}

static const MslStageSlot* stage_slot(uint32_t stage_id) { return stage_slot_mut(stage_id); }

static void stage_slot_refresh_floor_topology_flags(MslStageSlot* slot);

static void stage_segment_index_lookup_reset(int16_t* lookup) {
  if (lookup == NULL) {
    return;
  }
  for (size_t i = 0; i < (size_t)MSL_STAGE_SEGMENT_LOOKUP_COUNT; i++) {
    lookup[i] = -1;
  }
}

static void stage_floor_index_lookup_install(int16_t* lookup, const MslStageFloorLine* lines,
                                             size_t line_count) {
  stage_segment_index_lookup_reset(lookup);
  if (lookup == NULL || lines == NULL) {
    return;
  }
  for (size_t i = 0; i < line_count && i <= (size_t)INT16_MAX; i++) {
    lookup[lines[i].segment_i] = (int16_t)i;
  }
}

static void stage_ceiling_index_lookup_install(int16_t* lookup, const MslStageCeilingLine* lines,
                                               size_t line_count) {
  stage_segment_index_lookup_reset(lookup);
  if (lookup == NULL || lines == NULL) {
    return;
  }
  for (size_t i = 0; i < line_count && i <= (size_t)INT16_MAX; i++) {
    lookup[lines[i].segment_i] = (int16_t)i;
  }
}

static void stage_wall_index_lookup_install(int16_t* lookup, const MslStageWallLine* lines,
                                            size_t line_count) {
  stage_segment_index_lookup_reset(lookup);
  if (lookup == NULL || lines == NULL) {
    return;
  }
  for (size_t i = 0; i < line_count && i <= (size_t)INT16_MAX; i++) {
    lookup[lines[i].segment_i] = (int16_t)i;
  }
}

static int stage_slot_floor_line_index(const MslStageSlot* slot, uint16_t segment_i) {
  if (slot == NULL || !slot->loaded || slot->floor_lines == NULL || slot->floor_line_count == 0u) {
    return -1;
  }
  return (int)slot->floor_line_index_by_segment[segment_i];
}

static void fd_stage_bounds_reset(float* min_x, float* max_x, float* min_y, float* max_y) {
  if (min_x) {
    *min_x = FLT_MAX;
  }
  if (max_x) {
    *max_x = -FLT_MAX;
  }
  if (min_y) {
    *min_y = FLT_MAX;
  }
  if (max_y) {
    *max_y = -FLT_MAX;
  }
}

static void fd_stage_bounds_include(float x, float y, float* min_x, float* max_x, float* min_y,
                                    float* max_y) {
  if (min_x && x < *min_x) {
    *min_x = x;
  }
  if (max_x && x > *max_x) {
    *max_x = x;
  }
  if (min_y && y < *min_y) {
    *min_y = y;
  }
  if (max_y && y > *max_y) {
    *max_y = y;
  }
}

static void fd_stage_set_empty_bounds(float* min_x, float* max_x, float* min_y, float* max_y) {
  if (min_x) {
    *min_x = 1.0f;
  }
  if (max_x) {
    *max_x = -1.0f;
  }
  if (min_y) {
    *min_y = 1.0f;
  }
  if (max_y) {
    *max_y = -1.0f;
  }
}

static void fd_stage_ceiling_graph_set_bounds(MslStageCeilingGraph* g) {
  if (g == NULL || g->lines == NULL || g->line_count == 0u) {
    if (g != NULL) {
      fd_stage_set_empty_bounds(&g->min_x, &g->max_x, &g->min_y, &g->max_y);
    }
    return;
  }
  fd_stage_bounds_reset(&g->min_x, &g->max_x, &g->min_y, &g->max_y);
  for (size_t i = 0; i < g->line_count; i++) {
    fd_stage_bounds_include(g->lines[i].x0, g->lines[i].y0, &g->min_x, &g->max_x, &g->min_y,
                            &g->max_y);
    fd_stage_bounds_include(g->lines[i].x1, g->lines[i].y1, &g->min_x, &g->max_x, &g->min_y,
                            &g->max_y);
  }
  // mpLib endpoint extension can push query endpoints one unit past linked segment ends.
  g->min_x -= 2.0f;
  g->max_x += 2.0f;
  g->min_y -= 2.0f;
  g->max_y += 2.0f;
}

static void fd_stage_wall_graph_set_bounds(MslStageWallGraph* g) {
  if (g == NULL || g->lines == NULL || g->line_count == 0u) {
    if (g != NULL) {
      fd_stage_set_empty_bounds(&g->min_x, &g->max_x, &g->min_y, &g->max_y);
    }
    return;
  }
  fd_stage_bounds_reset(&g->min_x, &g->max_x, &g->min_y, &g->max_y);
  for (size_t i = 0; i < g->line_count; i++) {
    fd_stage_bounds_include(g->lines[i].x0, g->lines[i].y0, &g->min_x, &g->max_x, &g->min_y,
                            &g->max_y);
    fd_stage_bounds_include(g->lines[i].x1, g->lines[i].y1, &g->min_x, &g->max_x, &g->min_y,
                            &g->max_y);
  }
  // mpLib endpoint extension can push query endpoints one unit past linked segment ends.
  g->min_x -= 2.0f;
  g->max_x += 2.0f;
  g->min_y -= 2.0f;
  g->max_y += 2.0f;
}

static void fd_sort_floor_lines_by_id(MslStageFloorLine* lines, size_t n) {
  // Deterministic: keep floor lines ordered by ISO-derived line index (`segment_i`) so per-frame
  // selection ties follow stage line order (decomp shape).
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor (ties resolved by iteration order)
  if (lines == NULL || n <= 1) {
    return;
  }
  for (size_t i = 1; i < n; i++) {
    const MslStageFloorLine key = lines[i];
    size_t j = i;
    while (j > 0 && lines[j - 1].segment_i > key.segment_i) {
      lines[j] = lines[j - 1];
      j--;
    }
    lines[j] = key;
  }
}

static void fd_sort_ceiling_lines_by_id(MslStageCeilingLine* lines, size_t n) {
  // Deterministic: keep ceiling lines ordered by ISO-derived line index (`segment_i`) so per-frame
  // selection ties follow stage line order (decomp shape).
  // refs/melee/src/melee/mp/mplib.c::mpCheckCeiling (ties resolved by iteration order)
  if (lines == NULL || n <= 1) {
    return;
  }
  for (size_t i = 1; i < n; i++) {
    const MslStageCeilingLine key = lines[i];
    size_t j = i;
    while (j > 0 && lines[j - 1].segment_i > key.segment_i) {
      lines[j] = lines[j - 1];
      j--;
    }
    lines[j] = key;
  }
}

static void fd_sort_wall_lines_by_id(MslStageWallLine* lines, size_t n) {
  // Deterministic: keep wall lines ordered by ISO-derived line index (`segment_i`) so per-frame
  // selection ties follow stage line order (decomp shape).
  // refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
  // refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
  if (lines == NULL || n <= 1) {
    return;
  }
  for (size_t i = 1; i < n; i++) {
    const MslStageWallLine key = lines[i];
    size_t j = i;
    while (j > 0 && lines[j - 1].segment_i > key.segment_i) {
      lines[j] = lines[j - 1];
      j--;
    }
    lines[j] = key;
  }
}

typedef enum {
  FD_SEG_UNKNOWN = 0,
  FD_SEG_FLOOR = 1,
  FD_SEG_CEILING = 2,
  FD_SEG_LEFT_WALL = 3,
  FD_SEG_RIGHT_WALL = 4,
  FD_SEG_DYNAMIC = 5,
} FdSegKind;

typedef struct {
  FdSegKind kind;
  uint8_t ledge;
  uint8_t platform;
  uint8_t fighter_solid;
  uint8_t stage_object_support_kind;
  uint8_t reversed;
  uint16_t segment_i;
  int16_t prev_id0;
  int16_t next_id0;
  int16_t prev_id1;
  int16_t next_id1;
  int16_t joint_id;
  float raw_x0;
  float raw_y0;
  float raw_x1;
  float raw_y1;
  float x0;
  float y0;
  float x1;
  float y1;
  float ground_friction_mul;
  uint16_t hi_flags;
  uint16_t lo_flags;
} FdSegTmp;

static FdSegTmp fd_seg_tmp_normalized(FdSegKind kind, uint8_t ledge, uint8_t platform,
                                      uint8_t fighter_solid, uint8_t stage_object_support_kind,
                                      uint16_t segment_i, int16_t prev_id0, int16_t next_id0,
                                      int16_t prev_id1, int16_t next_id1, int16_t joint_id,
                                      float fx0, float fy0, float fx1, float fy1,
                                      float ground_friction_mul, uint16_t hi_flags,
                                      uint16_t lo_flags) {
  // Normalize orientation to match mplib assumptions for each line kind:
  // - floor: x0 <= x1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // - ceiling: x0 >= x1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
  // - left_wall: y0 <= y1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
  // - right_wall: y0 >= y1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
  uint8_t reversed = 0;
  const float raw_x0 = fx0;
  const float raw_y0 = fy0;
  const float raw_x1 = fx1;
  const float raw_y1 = fy1;
  if (kind == FD_SEG_FLOOR) {
    if (fx1 < fx0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
      reversed = 1;
    }
  } else if (kind == FD_SEG_CEILING) {
    if (fx1 > fx0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
      reversed = 1;
    }
  } else if (kind == FD_SEG_LEFT_WALL) {
    if (fy1 < fy0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
      reversed = 1;
    }
  } else if (kind == FD_SEG_RIGHT_WALL) {
    if (fy1 > fy0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
      reversed = 1;
    }
  }
  return (FdSegTmp){
      .kind = kind,
      .ledge = ledge,
      .platform = platform,
      .fighter_solid = fighter_solid,
      .stage_object_support_kind = stage_object_support_kind,
      .reversed = reversed,
      .segment_i = segment_i,
      .prev_id0 = prev_id0,
      .next_id0 = next_id0,
      .prev_id1 = prev_id1,
      .next_id1 = next_id1,
      .joint_id = joint_id,
      .raw_x0 = raw_x0,
      .raw_y0 = raw_y0,
      .raw_x1 = raw_x1,
      .raw_y1 = raw_y1,
      .x0 = fx0,
      .y0 = fy0,
      .x1 = fx1,
      .y1 = fy1,
      .ground_friction_mul = ground_friction_mul,
      .hi_flags = hi_flags,
      .lo_flags = lo_flags,
  };
}

static int fd_seg_tmp_index_by_line_id(const FdSegTmp* seg_tmp, size_t seg_n, int16_t line_id) {
  if (seg_tmp == NULL || line_id < 0) {
    return -1;
  }
  for (size_t i = 0; i < seg_n; i++) {
    if (seg_tmp[i].segment_i == (uint16_t)line_id) {
      return (int)i;
    }
  }
  return -1;
}

static int16_t fd_resolve_source_prev_id(const FdSegTmp* seg_tmp, size_t seg_n, size_t i) {
  // Decomp: mpLineGetPrev first tries prev_id1 when the linked line is enabled/visible and its v1 is
  // within 2 units of this line's v0, otherwise it falls back to prev_id0.
  // refs/melee/src/melee/mp/mplib.c::mpLineGetPrev
  const FdSegTmp* line = &seg_tmp[i];
  if (line->prev_id1 >= 0) {
    const int target_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, line->prev_id1);
    if (target_i >= 0) {
      const FdSegTmp* target = &seg_tmp[(size_t)target_i];
      const float dx = line->raw_x0 - target->raw_x1;
      const float dy = line->raw_y0 - target->raw_y1;
      if ((dx * dx + dy * dy) < 4.0f) {
        return line->prev_id1;
      }
    }
  }
  return line->prev_id0;
}

static int16_t fd_resolve_source_next_id(const FdSegTmp* seg_tmp, size_t seg_n, size_t i) {
  // Decomp: mpLineGetNext mirrors mpLineGetPrev at v1/v0.
  // refs/melee/src/melee/mp/mplib.c::mpLineGetNext
  const FdSegTmp* line = &seg_tmp[i];
  if (line->next_id1 >= 0) {
    const int target_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, line->next_id1);
    if (target_i >= 0) {
      const FdSegTmp* target = &seg_tmp[(size_t)target_i];
      const float dx = line->raw_x1 - target->raw_x0;
      const float dy = line->raw_y1 - target->raw_y0;
      if ((dx * dx + dy * dy) < 4.0f) {
        return line->next_id1;
      }
    }
  }
  return line->next_id0;
}

static void fd_oriented_source_links(const FdSegTmp* seg_tmp, size_t seg_n, size_t i,
                                     int16_t* prev_out, int16_t* next_out) {
  int16_t prev_id = fd_resolve_source_prev_id(seg_tmp, seg_n, i);
  int16_t next_id = fd_resolve_source_next_id(seg_tmp, seg_n, i);
  if (seg_tmp[i].reversed) {
    const int16_t tmp = prev_id;
    prev_id = next_id;
    next_id = tmp;
  }
  if (prev_out) {
    *prev_out = prev_id;
  }
  if (next_out) {
    *next_out = next_id;
  }
}

static uint8_t fd_seg_tmp_line_active_for_floor_graph(const FdSegTmp* seg_tmp, size_t seg_n,
                                                      int16_t line_id) {
  const int target_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, line_id);
  if (target_i < 0) {
    return 0u;
  }
  const FdSegTmp* target = &seg_tmp[(size_t)target_i];
  return (uint8_t)(target->kind == FD_SEG_FLOOR && target->fighter_solid);
}

static int16_t fd_resolve_active_floor_prev_id(const FdSegTmp* seg_tmp, size_t seg_n, size_t i) {
  const FdSegTmp* line = &seg_tmp[i];
  if (line->prev_id1 >= 0 &&
      fd_seg_tmp_line_active_for_floor_graph(seg_tmp, seg_n, line->prev_id1)) {
    const int target_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, line->prev_id1);
    const FdSegTmp* target = &seg_tmp[(size_t)target_i];
    const float dx = line->raw_x0 - target->raw_x1;
    const float dy = line->raw_y0 - target->raw_y1;
    if ((dx * dx + dy * dy) < 4.0f) {
      return line->prev_id1;
    }
  }
  return fd_seg_tmp_line_active_for_floor_graph(seg_tmp, seg_n, line->prev_id0) ? line->prev_id0
                                                                                : -1;
}

static int16_t fd_resolve_active_floor_next_id(const FdSegTmp* seg_tmp, size_t seg_n, size_t i) {
  const FdSegTmp* line = &seg_tmp[i];
  if (line->next_id1 >= 0 &&
      fd_seg_tmp_line_active_for_floor_graph(seg_tmp, seg_n, line->next_id1)) {
    const int target_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, line->next_id1);
    const FdSegTmp* target = &seg_tmp[(size_t)target_i];
    const float dx = line->raw_x1 - target->raw_x0;
    const float dy = line->raw_y1 - target->raw_y0;
    if ((dx * dx + dy * dy) < 4.0f) {
      return line->next_id1;
    }
  }
  return fd_seg_tmp_line_active_for_floor_graph(seg_tmp, seg_n, line->next_id0) ? line->next_id0
                                                                                : -1;
}

static void fd_oriented_active_floor_links(const FdSegTmp* seg_tmp, size_t seg_n, size_t i,
                                           int16_t* prev_out, int16_t* next_out) {
  int16_t prev_id = fd_resolve_active_floor_prev_id(seg_tmp, seg_n, i);
  int16_t next_id = fd_resolve_active_floor_next_id(seg_tmp, seg_n, i);
  if (seg_tmp[i].reversed) {
    const int16_t tmp = prev_id;
    prev_id = next_id;
    next_id = tmp;
  }
  if (prev_out) {
    *prev_out = prev_id;
  }
  if (next_out) {
    *next_out = next_id;
  }
}

static int16_t fd_floor_index_by_segment_i(const MslStageFloorLine* lines, size_t n,
                                           int16_t segment_i) {
  if (lines == NULL || segment_i < 0) {
    return -1;
  }
  for (size_t i = 0; i < n; i++) {
    if (lines[i].segment_i == (uint16_t)segment_i) {
      return (int16_t)i;
    }
  }
  return -1;
}

static int16_t fd_ceiling_index_by_segment_i(const MslStageCeilingLine* lines, size_t n,
                                             int16_t segment_i) {
  if (lines == NULL || segment_i < 0) {
    return -1;
  }
  for (size_t i = 0; i < n; i++) {
    if (lines[i].segment_i == (uint16_t)segment_i) {
      return (int16_t)i;
    }
  }
  return -1;
}

static int16_t fd_wall_index_by_segment_i(const MslStageWallLine* lines, size_t n,
                                          int16_t segment_i) {
  if (lines == NULL || segment_i < 0) {
    return -1;
  }
  for (size_t i = 0; i < n; i++) {
    if (lines[i].segment_i == (uint16_t)segment_i) {
      return (int16_t)i;
    }
  }
  return -1;
}

static void fd_apply_floor_raw_links(uint32_t stage_id, MslStageFloorLine* lines, size_t n,
                                     const FdSegTmp* seg_tmp, size_t seg_n) {
  if (lines == NULL || seg_tmp == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].raw_prev_id = -1;
    lines[i].raw_next_id = -1;
    lines[i].prev = -1;
    lines[i].next = -1;
    lines[i].has_prev_link = 0;
    lines[i].has_next_link = 0;
    const int tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, (int16_t)lines[i].segment_i);
    if (tmp_i < 0) {
      continue;
    }
    int16_t prev_id = -1;
    int16_t next_id = -1;
    fd_oriented_source_links(seg_tmp, seg_n, (size_t)tmp_i, &prev_id, &next_id);
    lines[i].raw_prev_id = prev_id;
    lines[i].raw_next_id = next_id;
    int16_t graph_prev_id = -1;
    int16_t graph_next_id = -1;
    fd_oriented_active_floor_links(seg_tmp, seg_n, (size_t)tmp_i, &graph_prev_id, &graph_next_id);
    const int prev_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, graph_prev_id);
    const int next_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, graph_next_id);
    (void)stage_id;
    const uint8_t current_active = lines[i].fighter_solid;
    const uint8_t prev_active = (prev_tmp_i >= 0) ? seg_tmp[(size_t)prev_tmp_i].fighter_solid : 0u;
    const uint8_t next_active = (next_tmp_i >= 0) ? seg_tmp[(size_t)next_tmp_i].fighter_solid : 0u;
    if (current_active && prev_active && prev_tmp_i >= 0 &&
        seg_tmp[(size_t)prev_tmp_i].kind == FD_SEG_FLOOR) {
      lines[i].prev = fd_floor_index_by_segment_i(lines, n, graph_prev_id);
    }
    if (current_active && next_active && next_tmp_i >= 0 &&
        seg_tmp[(size_t)next_tmp_i].kind == FD_SEG_FLOOR) {
      lines[i].next = fd_floor_index_by_segment_i(lines, n, graph_next_id);
    }
    lines[i].has_prev_link = (uint8_t)(lines[i].prev >= 0);
    lines[i].has_next_link = (uint8_t)(lines[i].next >= 0);
  }

  // Active floor graph remap:
  // Frozen PS suppresses transformation ground objects. When a suppressed line sits between two
  // active floor records, the direct mpLineGetPrev/Next result can be absent from the active graph
  // even though the reverse source link on the neighboring active line still names this line.
  // Preserve raw links for debug above, then build the runtime active graph from source adjacency in
  // both directions instead of falling back to endpoint heuristics.
  // refs/melee/src/melee/mp/mplib.c::{mpLineGetPrev,mpLineGetNext,mpCheckFloorRemap}
  // refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
  for (size_t i = 0; i < n; i++) {
    if (!lines[i].fighter_solid) {
      continue;
    }
    if (lines[i].prev < 0) {
      for (size_t j = 0; j < n; j++) {
        if (lines[j].fighter_solid && lines[j].next == (int16_t)i) {
          lines[i].prev = (int16_t)j;
          break;
        }
      }
    }
    if (lines[i].next < 0) {
      for (size_t j = 0; j < n; j++) {
        if (lines[j].fighter_solid && lines[j].prev == (int16_t)i) {
          lines[i].next = (int16_t)j;
          break;
        }
      }
    }
    lines[i].has_prev_link = (uint8_t)(lines[i].prev >= 0);
    lines[i].has_next_link = (uint8_t)(lines[i].next >= 0);
  }
}

static void fd_apply_ceiling_raw_links(MslStageCeilingLine* lines, size_t n,
                                       const FdSegTmp* seg_tmp, size_t seg_n) {
  if (lines == NULL || seg_tmp == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].raw_prev_id = -1;
    lines[i].raw_next_id = -1;
    lines[i].prev = -1;
    lines[i].next = -1;
    lines[i].has_prev_link = 0;
    lines[i].has_next_link = 0;
    const int tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, (int16_t)lines[i].segment_i);
    if (tmp_i < 0) {
      continue;
    }
    int16_t prev_id = -1;
    int16_t next_id = -1;
    fd_oriented_source_links(seg_tmp, seg_n, (size_t)tmp_i, &prev_id, &next_id);
    lines[i].raw_prev_id = prev_id;
    lines[i].raw_next_id = next_id;
    lines[i].has_prev_link = (uint8_t)(prev_id >= 0);
    lines[i].has_next_link = (uint8_t)(next_id >= 0);
    const int prev_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, prev_id);
    const int next_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, next_id);
    if (lines[i].fighter_solid && prev_tmp_i >= 0 &&
        seg_tmp[(size_t)prev_tmp_i].kind == FD_SEG_CEILING &&
        seg_tmp[(size_t)prev_tmp_i].fighter_solid) {
      lines[i].prev = fd_ceiling_index_by_segment_i(lines, n, prev_id);
    }
    if (lines[i].fighter_solid && next_tmp_i >= 0 &&
        seg_tmp[(size_t)next_tmp_i].kind == FD_SEG_CEILING &&
        seg_tmp[(size_t)next_tmp_i].fighter_solid) {
      lines[i].next = fd_ceiling_index_by_segment_i(lines, n, next_id);
    }
  }
}

static void fd_apply_wall_raw_links(MslStageWallLine* lines, size_t n, FdSegKind kind,
                                    const FdSegTmp* seg_tmp, size_t seg_n) {
  if (lines == NULL || seg_tmp == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].raw_prev_id = -1;
    lines[i].raw_next_id = -1;
    lines[i].prev = -1;
    lines[i].next = -1;
    lines[i].has_prev_link = 0;
    lines[i].has_next_link = 0;
    const int tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, (int16_t)lines[i].segment_i);
    if (tmp_i < 0) {
      continue;
    }
    int16_t prev_id = -1;
    int16_t next_id = -1;
    fd_oriented_source_links(seg_tmp, seg_n, (size_t)tmp_i, &prev_id, &next_id);
    lines[i].raw_prev_id = prev_id;
    lines[i].raw_next_id = next_id;
    lines[i].has_prev_link = (uint8_t)(prev_id >= 0);
    lines[i].has_next_link = (uint8_t)(next_id >= 0);
    const int prev_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, prev_id);
    const int next_tmp_i = fd_seg_tmp_index_by_line_id(seg_tmp, seg_n, next_id);
    if (lines[i].fighter_solid && prev_tmp_i >= 0 && seg_tmp[(size_t)prev_tmp_i].kind == kind &&
        seg_tmp[(size_t)prev_tmp_i].fighter_solid) {
      lines[i].prev = fd_wall_index_by_segment_i(lines, n, prev_id);
    }
    if (lines[i].fighter_solid && next_tmp_i >= 0 && seg_tmp[(size_t)next_tmp_i].kind == kind &&
        seg_tmp[(size_t)next_tmp_i].fighter_solid) {
      lines[i].next = fd_wall_index_by_segment_i(lines, n, next_id);
    }
  }
}

static uint8_t fd_points_match(float ax, float ay, float bx, float by) {
  // Load-time endpoint ownership: generated MSLSTG01 endpoints come from the same source MapLine
  // data but pass through float extraction/normalization. Use the same small coordinate epsilon
  // already used by floor horizontal/decomp projection checks in this file.
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  return (uint8_t)(fabsf(ax - bx) <= 0.0001f && fabsf(ay - by) <= 0.0001f);
}

static int16_t fd_wall_index_connected_to_point(const MslStageWallLine* lines, size_t n, float x,
                                                float y) {
  if (lines == NULL) {
    return -1;
  }
  for (size_t i = 0; i < n && i <= (size_t)INT16_MAX; i++) {
    const MslStageWallLine* l = &lines[i];
    if (fd_points_match(l->x0, l->y0, x, y) || fd_points_match(l->x1, l->y1, x, y)) {
      return (int16_t)i;
    }
  }
  return -1;
}

static void fd_apply_floor_adjacent_wall_indices(MslStageFloorLine* floor_lines, size_t floor_n,
                                                 const MslStageWallLine* lw_lines, size_t lw_n,
                                                 const MslStageWallLine* rw_lines, size_t rw_n) {
  if (floor_lines == NULL) {
    return;
  }
  for (size_t i = 0; i < floor_n; i++) {
    int left_floor_i = (int)i;
    for (size_t guard = 0; guard < floor_n; guard++) {
      const int16_t prev = floor_lines[(size_t)left_floor_i].prev;
      if (prev < 0 || (size_t)prev >= floor_n) {
        break;
      }
      left_floor_i = (int)prev;
    }
    const MslStageFloorLine* left_floor = &floor_lines[(size_t)left_floor_i];
    floor_lines[i].adjacent_left_wall =
        fd_wall_index_connected_to_point(lw_lines, lw_n, left_floor->x0, left_floor->y0);

    int right_floor_i = (int)i;
    for (size_t guard = 0; guard < floor_n; guard++) {
      const int16_t next = floor_lines[(size_t)right_floor_i].next;
      if (next < 0 || (size_t)next >= floor_n) {
        break;
      }
      right_floor_i = (int)next;
    }
    const MslStageFloorLine* right_floor = &floor_lines[(size_t)right_floor_i];
    floor_lines[i].adjacent_right_wall =
        fd_wall_index_connected_to_point(rw_lines, rw_n, right_floor->x1, right_floor->y1);
  }
}

static int fd_install_stage_segments(uint32_t stage_id, const FdSegTmp* seg_tmp, size_t seg_n) {
  if (seg_tmp == NULL || seg_n == 0) {
    return -1;
  }

  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (slot == NULL) {
    return -1;
  }

  size_t floor_n = 0;
  size_t ceil_n = 0;
  size_t lw_n = 0;
  size_t rw_n = 0;
  for (size_t i = 0; i < seg_n; i++) {
    switch (seg_tmp[i].kind) {
      case FD_SEG_FLOOR:
        floor_n++;
        break;
      case FD_SEG_CEILING:
        ceil_n++;
        break;
      case FD_SEG_LEFT_WALL:
        lw_n++;
        break;
      case FD_SEG_RIGHT_WALL:
        rw_n++;
        break;
      default:
        break;
    }
  }
  if (floor_n == 0) {
    return -1;
  }

  MslStageFloorLine* floor_lines =
      (MslStageFloorLine*)alloc_calloc(floor_n, sizeof(MslStageFloorLine));
  MslStageCeilingLine* ceil_lines = NULL;
  MslStageWallLine* lw_lines = NULL;
  MslStageWallLine* rw_lines = NULL;
  if (ceil_n) {
    ceil_lines = (MslStageCeilingLine*)alloc_calloc(ceil_n, sizeof(MslStageCeilingLine));
  }
  if (lw_n) {
    lw_lines = (MslStageWallLine*)alloc_calloc(lw_n, sizeof(MslStageWallLine));
  }
  if (rw_n) {
    rw_lines = (MslStageWallLine*)alloc_calloc(rw_n, sizeof(MslStageWallLine));
  }
  if (floor_lines == NULL || (ceil_n && ceil_lines == NULL) || (lw_n && lw_lines == NULL) ||
      (rw_n && rw_lines == NULL)) {
    alloc_free(floor_lines);
    alloc_free(ceil_lines);
    alloc_free(lw_lines);
    alloc_free(rw_lines);
    return -1;
  }

  size_t oi_floor = 0;
  size_t oi_ceil = 0;
  size_t oi_lw = 0;
  size_t oi_rw = 0;
  for (size_t i = 0; i < seg_n; i++) {
    const FdSegTmp* s = &seg_tmp[i];
    if (s->kind == FD_SEG_FLOOR) {
      floor_lines[oi_floor++] = (MslStageFloorLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .raw_x0 = s->raw_x0,
          .raw_y0 = s->raw_y0,
          .raw_x1 = s->raw_x1,
          .raw_y1 = s->raw_y1,
          .is_ledge = s->ledge,
          .is_platform = s->platform,
          .fighter_solid = s->fighter_solid,
          .stage_object_support_kind = s->stage_object_support_kind,
          .ground_friction_mul = s->ground_friction_mul,
          .hi_flags = s->hi_flags,
          .lo_flags = s->lo_flags,
          .joint_id = s->joint_id,
          ._pad_flags = {0},
          .has_prev_link = 0,
          .has_next_link = 0,
          .segment_i = s->segment_i,
          .raw_prev_id = -1,
          .raw_next_id = -1,
          .has_alternate_endpoint_link =
              (uint8_t)((s->prev_id1 >= 0 || s->next_id1 >= 0) ? 1u : 0u),
          ._pad0 = {0},
          .prev = -1,
          .next = -1,
          .adjacent_left_wall = -1,
          .adjacent_right_wall = -1,
      };
    } else if (s->kind == FD_SEG_CEILING) {
      ceil_lines[oi_ceil++] = (MslStageCeilingLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .fighter_solid = s->fighter_solid,
          ._pad0 = {0},
          .segment_i = s->segment_i,
          .hi_flags = s->hi_flags,
          .lo_flags = s->lo_flags,
          .joint_id = s->joint_id,
          .raw_prev_id = -1,
          .raw_next_id = -1,
          .prev = -1,
          .next = -1,
      };
    } else if (s->kind == FD_SEG_LEFT_WALL) {
      lw_lines[oi_lw++] = (MslStageWallLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .min_x = (s->x0 < s->x1) ? s->x0 : s->x1,
          .max_x = (s->x0 > s->x1) ? s->x0 : s->x1,
          .min_y = (s->y0 < s->y1) ? s->y0 : s->y1,
          .max_y = (s->y0 > s->y1) ? s->y0 : s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .fighter_solid = s->fighter_solid,
          ._pad0 = {0},
          .segment_i = s->segment_i,
          .hi_flags = s->hi_flags,
          .lo_flags = s->lo_flags,
          .joint_id = s->joint_id,
          .raw_prev_id = -1,
          .raw_next_id = -1,
          .prev = -1,
          .next = -1,
      };
    } else if (s->kind == FD_SEG_RIGHT_WALL) {
      rw_lines[oi_rw++] = (MslStageWallLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .min_x = (s->x0 < s->x1) ? s->x0 : s->x1,
          .max_x = (s->x0 > s->x1) ? s->x0 : s->x1,
          .min_y = (s->y0 < s->y1) ? s->y0 : s->y1,
          .max_y = (s->y0 > s->y1) ? s->y0 : s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .fighter_solid = s->fighter_solid,
          .segment_i = s->segment_i,
          .hi_flags = s->hi_flags,
          .lo_flags = s->lo_flags,
          .joint_id = s->joint_id,
          .raw_prev_id = -1,
          .raw_next_id = -1,
          .prev = -1,
          .next = -1,
      };
    }
  }

  fd_sort_floor_lines_by_id(floor_lines, floor_n);
  fd_apply_floor_raw_links(stage_id, floor_lines, floor_n, seg_tmp, seg_n);
  for (size_t i = 0; i < floor_n; i++) {
    floor_lines[i].platform_transform_kind =
        slot->platform_transform_kind_by_segment[floor_lines[i].segment_i];
    floor_lines[i].platform_transform_id =
        slot->platform_transform_id_by_segment[floor_lines[i].segment_i];
  }
  size_t fighter_floor_n = 0;
  for (size_t i = 0; i < floor_n; i++) {
    if (!floor_lines[i].is_platform && floor_lines[i].fighter_solid) {
      fighter_floor_n++;
    }
  }
  if (fighter_floor_n == 0u) {
    alloc_free(floor_lines);
    alloc_free(ceil_lines);
    alloc_free(lw_lines);
    alloc_free(rw_lines);
    return -1;
  }
  MslStageFloorLine* fighter_floor_lines =
      (MslStageFloorLine*)alloc_calloc(fighter_floor_n, sizeof(MslStageFloorLine));
  if (fighter_floor_lines == NULL) {
    alloc_free(floor_lines);
    alloc_free(ceil_lines);
    alloc_free(lw_lines);
    alloc_free(rw_lines);
    return -1;
  }
  size_t oi_fighter_floor = 0;
  for (size_t i = 0; i < floor_n; i++) {
    if (!floor_lines[i].is_platform && floor_lines[i].fighter_solid) {
      fighter_floor_lines[oi_fighter_floor++] = floor_lines[i];
    }
  }
  // Keep a non-platform-only graph for debug/tests and for callers that intentionally need static
  // hard floors only. Runtime fighter platform collision uses the full floor graph with
  // pass-through/floor-skip gating in mpcoll_ground.c.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  fd_apply_floor_raw_links(stage_id, fighter_floor_lines, fighter_floor_n, seg_tmp, seg_n);
  if (ceil_n) {
    fd_sort_ceiling_lines_by_id(ceil_lines, ceil_n);
    fd_apply_ceiling_raw_links(ceil_lines, ceil_n, seg_tmp, seg_n);
  }
  if (lw_n) {
    fd_sort_wall_lines_by_id(lw_lines, lw_n);
    fd_apply_wall_raw_links(lw_lines, lw_n, FD_SEG_LEFT_WALL, seg_tmp, seg_n);
  }
  if (rw_n) {
    fd_sort_wall_lines_by_id(rw_lines, rw_n);
    fd_apply_wall_raw_links(rw_lines, rw_n, FD_SEG_RIGHT_WALL, seg_tmp, seg_n);
  }
  fd_apply_floor_adjacent_wall_indices(floor_lines, floor_n, lw_lines, lw_n, rw_lines, rw_n);
  fd_apply_floor_adjacent_wall_indices(fighter_floor_lines, fighter_floor_n, lw_lines, lw_n,
                                       rw_lines, rw_n);

  // Ledge candidates are floor segments with the ISO-derived LINE_FLAG_LEDGE bit.
  //
  // Decomp context: fighter cliff physics snaps each frame to the cliff point obtained from
  // `mpLib_80053ECC_Floor` / `mpLib_80053DA4_Floor`.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  slot->have_ledge_points[0] = 0;
  slot->have_ledge_points[1] = 0;
  slot->ledge_points[0] = (MslStagePoint2){0};
  slot->ledge_points[1] = (MslStagePoint2){0};
  slot->ledge_floor_line_idx[0] = -1;
  slot->ledge_floor_line_idx[1] = -1;
  float best_left_x = FLT_MAX;
  float best_right_x = -FLT_MAX;
  for (size_t i = 0; i < floor_n; i++) {
    const MslStageFloorLine* l = &floor_lines[i];
    if (!l->is_ledge) {
      continue;
    }
    if (l->x0 < best_left_x) {
      best_left_x = l->x0;
      slot->ledge_points[0] = (MslStagePoint2){.x = l->x0, .y = l->y0};
      slot->have_ledge_points[0] = 1;
      slot->ledge_floor_line_idx[0] = (int16_t)i;
    }
    if (l->x1 > best_right_x) {
      best_right_x = l->x1;
      slot->ledge_points[1] = (MslStagePoint2){.x = l->x1, .y = l->y1};
      slot->have_ledge_points[1] = 1;
      slot->ledge_floor_line_idx[1] = (int16_t)i;
    }
  }

  // Raw source graph links above provide endpoint connectivity hints for
  // mpLib_8004ED5C-style endpoint extension.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C

  alloc_free(slot->floor_lines);
  alloc_free(slot->fighter_floor_lines);
  alloc_free(slot->ceiling_lines);
  alloc_free(slot->left_wall_lines);
  alloc_free(slot->right_wall_lines);
  slot->floor_lines = floor_lines;
  slot->floor_line_count = floor_n;
  slot->fighter_floor_lines = fighter_floor_lines;
  slot->fighter_floor_line_count = fighter_floor_n;
  slot->ceiling_lines = ceil_lines;
  slot->ceiling_line_count = ceil_n;
  slot->left_wall_lines = lw_lines;
  slot->left_wall_line_count = lw_n;
  slot->right_wall_lines = rw_lines;
  slot->right_wall_line_count = rw_n;
  slot->floor_graph.lines = slot->floor_lines;
  slot->floor_graph.line_count = slot->floor_line_count;
  slot->fighter_floor_graph.lines = slot->fighter_floor_lines;
  slot->fighter_floor_graph.line_count = slot->fighter_floor_line_count;
  slot->ceiling_graph.lines = slot->ceiling_lines;
  slot->ceiling_graph.line_count = slot->ceiling_line_count;
  fd_stage_ceiling_graph_set_bounds(&slot->ceiling_graph);
  slot->left_wall_graph.lines = slot->left_wall_lines;
  slot->left_wall_graph.line_count = slot->left_wall_line_count;
  fd_stage_wall_graph_set_bounds(&slot->left_wall_graph);
  slot->right_wall_graph.lines = slot->right_wall_lines;
  slot->right_wall_graph.line_count = slot->right_wall_line_count;
  fd_stage_wall_graph_set_bounds(&slot->right_wall_graph);
  // Runtime callers carry ISO-derived segment ids in state (ground_id and raw MapLine refs).
  // Build direct id->graph-index lookups at load time so gameplay paths keep the data-backed
  // mapping without rescanning generated MSLSTG01 line arrays every frame.
  // data/stages/bin/*.bin::MSLSTG01 segment line_id
  stage_floor_index_lookup_install(slot->floor_line_index_by_segment, slot->floor_lines,
                                   slot->floor_line_count);
  stage_floor_index_lookup_install(slot->fighter_floor_line_index_by_segment,
                                   slot->fighter_floor_lines, slot->fighter_floor_line_count);
  stage_ceiling_index_lookup_install(slot->ceiling_line_index_by_segment, slot->ceiling_lines,
                                     slot->ceiling_line_count);
  stage_wall_index_lookup_install(slot->left_wall_line_index_by_segment, slot->left_wall_lines,
                                  slot->left_wall_line_count);
  stage_wall_index_lookup_install(slot->right_wall_line_index_by_segment, slot->right_wall_lines,
                                  slot->right_wall_line_count);

  return 0;
}

enum {
  MSLSTG01_VERSION = 10,
  MSLSTG01_HEADER_BYTES = 64,
  MSLSTG01_SEGMENT_BYTES = 40,
  MSLSTG01_FLAG_PLATFORM = 1,
  MSLSTG01_FLAG_LEDGE = 2,
  MSLSTG01_FLAG_FIGHTER_SOLID = 4,
  MSLSTG01_STAGE_OBJECT_SUPPORT_SHIFT = 3,
  MSLSTG01_STAGE_OBJECT_SUPPORT_MASK = 0x1F,
  MSLSTG01_KIND_FLOOR = 0,
  MSLSTG01_KIND_CEILING = 1,
  MSLSTG01_KIND_RIGHT_WALL = 2,
  MSLSTG01_KIND_LEFT_WALL = 3,
  MSLSTG01_KIND_DYNAMIC = 4,
  MSLSTG01_PLATFORM_MOTION_FOD = 1,
};

static uint16_t stage_read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static int16_t stage_read_s16_le(const uint8_t* p) {
  int16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint32_t stage_read_u32_le(const uint8_t* p) {
  uint32_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static float stage_read_f32_le(const uint8_t* p) {
  float v = 0.0f;
  memcpy(&v, p, sizeof(v));
  return v;
}

static FdSegKind fd_seg_kind_from_mslstg(uint8_t kind_id) {
  switch (kind_id) {
    case MSLSTG01_KIND_FLOOR:
      return FD_SEG_FLOOR;
    case MSLSTG01_KIND_CEILING:
      return FD_SEG_CEILING;
    case MSLSTG01_KIND_LEFT_WALL:
      return FD_SEG_LEFT_WALL;
    case MSLSTG01_KIND_RIGHT_WALL:
      return FD_SEG_RIGHT_WALL;
    case MSLSTG01_KIND_DYNAMIC:
      return FD_SEG_DYNAMIC;
    default:
      return FD_SEG_UNKNOWN;
  }
}

static void stage_install_match_flow_from_mslstg01(uint32_t stage_id, const uint8_t* buf,
                                                   uint16_t segment_count,
                                                   uint16_t stage_point_count, uint16_t spawn_count,
                                                   uint16_t respawn_count) {
  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (buf == NULL || spawn_count < (uint16_t)MSL_MAX_PLAYERS ||
      respawn_count < (uint16_t)MSL_MAX_PLAYERS || slot == NULL) {
    return;
  }

  MslStageBoundsWorld cam = {
      .left = stage_read_f32_le(buf + 32),
      .right = stage_read_f32_le(buf + 36),
      .top = stage_read_f32_le(buf + 40),
      .bottom = stage_read_f32_le(buf + 44),
  };
  MslStageBoundsWorld blast = {
      .left = stage_read_f32_le(buf + 48),
      .right = stage_read_f32_le(buf + 52),
      .top = stage_read_f32_le(buf + 56),
      .bottom = stage_read_f32_le(buf + 60),
  };
  if (!(cam.left < cam.right && cam.bottom < cam.top && blast.left < blast.right &&
        blast.bottom < blast.top)) {
    return;
  }

  const uint8_t* p = buf + MSLSTG01_HEADER_BYTES +
                     (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
                     (size_t)stage_point_count * 12u;
  MslStagePoint2 spawn[MSL_MAX_PLAYERS];
  MslStagePoint2 respawn[MSL_MAX_PLAYERS];
  for (size_t i = 0; i < (size_t)MSL_MAX_PLAYERS; i++, p += 8u) {
    spawn[i] = (MslStagePoint2){.x = stage_read_f32_le(p + 0), .y = stage_read_f32_le(p + 4)};
  }
  p += ((size_t)spawn_count - (size_t)MSL_MAX_PLAYERS) * 8u;
  for (size_t i = 0; i < (size_t)MSL_MAX_PLAYERS; i++, p += 8u) {
    respawn[i] = (MslStagePoint2){.x = stage_read_f32_le(p + 0), .y = stage_read_f32_le(p + 4)};
  }

  slot->cam_bounds_world = cam;
  slot->blast_bounds_world = blast;
  for (size_t i = 0; i < (size_t)MSL_MAX_PLAYERS; i++) {
    slot->spawn_points[i] = spawn[i];
    slot->respawn_points[i] = respawn[i];
  }
  slot->match_flow_loaded = 1u;
}

static int stage_install_platform_transforms_from_mslstg01(
    uint32_t stage_id, const uint8_t* buf, uint16_t segment_count, uint16_t stage_point_count,
    uint16_t spawn_count, uint16_t respawn_count, uint16_t transform_count,
    uint16_t transform_record_bytes) {
  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (slot == NULL) {
    return -1;
  }
  memset(slot->platform_transform_kind_by_segment, 0,
         sizeof(slot->platform_transform_kind_by_segment));
  memset(slot->platform_transform_id_by_segment, 0, sizeof(slot->platform_transform_id_by_segment));
  slot->has_height_platform_transform = 0u;
  slot->has_deferred_static_floor_transform = 0u;
  if (transform_count == 0u) {
    alloc_free(slot->platform_transforms);
    slot->platform_transforms = NULL;
    slot->platform_transform_count = 0u;
    return 0;
  }
  if (transform_record_bytes != 20u) {
    return -1;
  }
  MslStagePlatformTransform* recs = (MslStagePlatformTransform*)alloc_calloc(
      (size_t)transform_count, sizeof(MslStagePlatformTransform));
  if (recs == NULL) {
    return -1;
  }
  const uint8_t* p =
      buf + MSLSTG01_HEADER_BYTES + (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
      (size_t)stage_point_count * 12u + (size_t)spawn_count * 8u + (size_t)respawn_count * 8u;
  for (uint16_t i = 0; i < transform_count; i++, p += 20u) {
    recs[i] = (MslStagePlatformTransform){
        .line_id = stage_read_u16_le(p + 0),
        .kind_id = p[2],
        .platform_id = p[3],
        .x0 = stage_read_f32_le(p + 4),
        .x1 = stage_read_f32_le(p + 8),
        .y_const = stage_read_f32_le(p + 12),
        .height_coeff = stage_read_f32_le(p + 16),
    };
    slot->platform_transform_kind_by_segment[recs[i].line_id] = recs[i].kind_id;
    slot->platform_transform_id_by_segment[recs[i].line_id] = recs[i].platform_id;
    if (recs[i].kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT) {
      slot->has_height_platform_transform = 1u;
    }
    if (recs[i].kind_id != (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
        recs[i].kind_id != (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y) {
      slot->has_deferred_static_floor_transform = 1u;
    }
  }
  alloc_free(slot->platform_transforms);
  slot->platform_transforms = recs;
  slot->platform_transform_count = transform_count;
  return 0;
}

static int stage_install_platform_paths_from_mslstg01(
    uint32_t stage_id, const uint8_t* buf, uint16_t segment_count, uint16_t stage_point_count,
    uint16_t spawn_count, uint16_t respawn_count, uint16_t transform_count,
    uint16_t transform_record_bytes, uint16_t motion_count, uint16_t motion_record_bytes,
    uint16_t path_count, uint16_t path_record_bytes) {
  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (slot == NULL) {
    return -1;
  }
  if (path_count == 0u) {
    alloc_free(slot->platform_path_frames);
    slot->platform_path_frames = NULL;
    slot->platform_path_frame_count = 0u;
    return 0;
  }
  if (path_record_bytes != 16u) {
    return -1;
  }
  MslStagePlatformPathFrame* recs = (MslStagePlatformPathFrame*)alloc_calloc(
      (size_t)path_count, sizeof(MslStagePlatformPathFrame));
  if (recs == NULL) {
    return -1;
  }
  const uint8_t* p =
      buf + MSLSTG01_HEADER_BYTES + (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
      (size_t)stage_point_count * 12u + (size_t)spawn_count * 8u + (size_t)respawn_count * 8u +
      (size_t)transform_count * (size_t)transform_record_bytes +
      (size_t)motion_count * (size_t)motion_record_bytes;
  for (uint16_t i = 0; i < path_count; i++, p += 16u) {
    recs[i] = (MslStagePlatformPathFrame){
        .line_id = stage_read_u16_le(p + 0),
        .frame = stage_read_u16_le(p + 2),
        .x0 = stage_read_f32_le(p + 4),
        .y = stage_read_f32_le(p + 8),
        .x1 = stage_read_f32_le(p + 12),
    };
  }
  alloc_free(slot->platform_path_frames);
  slot->platform_path_frames = recs;
  slot->platform_path_frame_count = path_count;
  return 0;
}

static int stage_install_platform_motion_from_mslstg01(
    uint32_t stage_id, const uint8_t* buf, uint16_t segment_count, uint16_t stage_point_count,
    uint16_t spawn_count, uint16_t respawn_count, uint16_t transform_count,
    uint16_t transform_record_bytes, uint16_t motion_count, uint16_t motion_record_bytes) {
  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (slot == NULL) {
    return -1;
  }
  slot->fod_motion = (MslFodPlatformMotion){0};
  if (motion_count == 0u) {
    return 0;
  }
  if (motion_record_bytes != 72u) {
    return -1;
  }
  const uint8_t* p =
      buf + MSLSTG01_HEADER_BYTES + (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
      (size_t)stage_point_count * 12u + (size_t)spawn_count * 8u + (size_t)respawn_count * 8u +
      (size_t)transform_count * (size_t)transform_record_bytes;
  for (uint16_t i = 0; i < motion_count; i++, p += 72u) {
    const uint8_t kind = p[0];
    const uint8_t platform_count = p[1];
    if (kind != (uint8_t)MSLSTG01_PLATFORM_MOTION_FOD ||
        stage_id != (uint32_t)MSL_STAGE_ID_FOUNTAIN_OF_DREAMS || platform_count < 2u) {
      continue;
    }
    MslFodPlatformMotion m = {0};
    // Source/audit data is packed into MSLSTG01 by tools/extraction/extract_stage_metadata.py.
    // griz.json remains only an audit sidecar; runtime consumes this binary record at init.
    // refs/melee/src/melee/gr/grizumi.c::{FountainParams,grIzumi_801CC358}
    m.home_height = stage_read_f32_le(p + 4);
    m.hidden_target_height = stage_read_f32_le(p + 8);
    m.max_height = stage_read_f32_le(p + 12);
    m.min_visible_height = stage_read_f32_le(p + 16);
    m.up_speed = stage_read_f32_le(p + 20);
    m.down_speed = stage_read_f32_le(p + 24);
    m.wait_min_frames = stage_read_f32_le(p + 28);
    m.wait_max_frames = stage_read_f32_le(p + 32);
    m.hidden_wait_min_frames = stage_read_f32_le(p + 36);
    m.hidden_wait_max_frames = stage_read_f32_le(p + 40);
    m.target_delta_min = stage_read_f32_le(p + 44);
    m.target_delta_max = stage_read_f32_le(p + 48);
    m.bias_below_home = stage_read_f32_le(p + 52);
    m.bias_above_home = stage_read_f32_le(p + 56);
    m.hidden_weight = stage_read_f32_le(p + 60);
    m.stay_weight = stage_read_f32_le(p + 64);
    m.move_weight = stage_read_f32_le(p + 68);
    if (!(m.up_speed > 0.0f) || !(m.down_speed > 0.0f) ||
        !(m.wait_max_frames >= m.wait_min_frames)) {
      return -1;
    }
    m.loaded = 1u;
    slot->fod_motion = m;
  }
  return 0;
}

static int fd_load_floor_lines_from_mslstg01(uint32_t stage_id, const uint8_t* buf, size_t sz) {
  if (buf == NULL || sz < (size_t)MSLSTG01_HEADER_BYTES || memcmp(buf, "MSLSTG01", 8) != 0) {
    return -1;
  }
  const uint32_t version = stage_read_u32_le(buf + 8);
  if (version != (uint32_t)MSLSTG01_VERSION) {
    return -1;
  }
  const uint16_t segment_count = stage_read_u16_le(buf + 12);
  const uint16_t stage_point_count = stage_read_u16_le(buf + 14);
  const uint16_t spawn_count = stage_read_u16_le(buf + 16);
  const uint16_t respawn_count = stage_read_u16_le(buf + 18);
  const uint16_t platform_transform_count = stage_read_u16_le(buf + 20);
  const uint16_t platform_transform_record_bytes = stage_read_u16_le(buf + 22);
  const uint16_t platform_motion_count = stage_read_u16_le(buf + 24);
  const uint16_t platform_motion_record_bytes = stage_read_u16_le(buf + 26);
  const uint16_t platform_path_count = stage_read_u16_le(buf + 28);
  const uint16_t platform_path_record_bytes = stage_read_u16_le(buf + 30);
  const size_t expected =
      (size_t)MSLSTG01_HEADER_BYTES + (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
      (size_t)stage_point_count * 12u + (size_t)spawn_count * 8u + (size_t)respawn_count * 8u +
      (size_t)platform_transform_count * (size_t)platform_transform_record_bytes +
      (size_t)platform_motion_count * (size_t)platform_motion_record_bytes +
      (size_t)platform_path_count * (size_t)platform_path_record_bytes;
  if (sz != expected || segment_count == 0u || segment_count > 4096u) {
    return -1;
  }

  FdSegTmp* seg_tmp = (FdSegTmp*)alloc_calloc((size_t)segment_count, sizeof(FdSegTmp));
  if (seg_tmp == NULL) {
    return -1;
  }
  size_t seg_n = 0;
  const uint8_t* p = buf + MSLSTG01_HEADER_BYTES;
  for (uint16_t i = 0; i < segment_count; i++, p += MSLSTG01_SEGMENT_BYTES) {
    const uint16_t line_id = stage_read_u16_le(p + 0);
    const uint8_t kind_id = p[2];
    const uint8_t flags = p[3];
    const FdSegKind kind = fd_seg_kind_from_mslstg(kind_id);
    if (!(kind == FD_SEG_FLOOR || kind == FD_SEG_CEILING || kind == FD_SEG_LEFT_WALL ||
          kind == FD_SEG_RIGHT_WALL || kind == FD_SEG_DYNAMIC)) {
      continue;
    }
    // MSLSTG01 stores source MapLine links and world-scaled source collision segment endpoints.
    // agent_docs/DATA_CONTRACT.md::MSLSTG01
    seg_tmp[seg_n++] = fd_seg_tmp_normalized(
        kind, (uint8_t)((flags & (uint8_t)MSLSTG01_FLAG_LEDGE) != 0u),
        (uint8_t)((flags & (uint8_t)MSLSTG01_FLAG_PLATFORM) != 0u),
        (uint8_t)((flags & (uint8_t)MSLSTG01_FLAG_FIGHTER_SOLID) != 0u),
        (uint8_t)((flags >> MSLSTG01_STAGE_OBJECT_SUPPORT_SHIFT) &
                  MSLSTG01_STAGE_OBJECT_SUPPORT_MASK),
        line_id, stage_read_s16_le(p + 8), stage_read_s16_le(p + 10), stage_read_s16_le(p + 12),
        stage_read_s16_le(p + 14), stage_read_s16_le(p + 36), stage_read_f32_le(p + 16),
        stage_read_f32_le(p + 20), stage_read_f32_le(p + 24), stage_read_f32_le(p + 28),
        stage_read_f32_le(p + 32), stage_read_u16_le(p + 4), stage_read_u16_le(p + 6));
  }
  stage_install_match_flow_from_mslstg01(stage_id, buf, segment_count, stage_point_count,
                                         spawn_count, respawn_count);
  int err = stage_install_platform_transforms_from_mslstg01(
      stage_id, buf, segment_count, stage_point_count, spawn_count, respawn_count,
      platform_transform_count, platform_transform_record_bytes);
  if (err == 0) {
    err = stage_install_platform_motion_from_mslstg01(
        stage_id, buf, segment_count, stage_point_count, spawn_count, respawn_count,
        platform_transform_count, platform_transform_record_bytes, platform_motion_count,
        platform_motion_record_bytes);
  }
  if (err == 0) {
    err = stage_install_platform_paths_from_mslstg01(
        stage_id, buf, segment_count, stage_point_count, spawn_count, respawn_count,
        platform_transform_count, platform_transform_record_bytes, platform_motion_count,
        platform_motion_record_bytes, platform_path_count, platform_path_record_bytes);
  }
  if (err == 0) {
    err = fd_install_stage_segments(stage_id, seg_tmp, seg_n);
  }
  alloc_free(seg_tmp);
  return err;
}

static int fd_load_floor_lines_from_mslstg01_file(uint32_t stage_id, const char* path) {
  if (path == NULL) {
    return -1;
  }
  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  uint8_t* buf = (uint8_t*)alloc_malloc((size_t)sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    alloc_free(buf);
    return -1;
  }
  const int err = fd_load_floor_lines_from_mslstg01(stage_id, buf, (size_t)sz);
  alloc_free(buf);
  return err;
}

static int stage_load_slot_from_data_dir(MslStageSlot* slot, const char* data_dir) {
  if (slot == NULL || data_dir == NULL || data_dir[0] == '\0') {
    return -1;
  }
  if (slot->loaded && slot->match_flow_loaded) {
    return 0;
  }
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/stages/bin/%s", data_dir, slot->bin_name);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }
  if (fd_load_floor_lines_from_mslstg01_file(slot->stage_id, path) != 0) {
    return -1;
  }
  if (!slot->match_flow_loaded) {
    return -1;
  }
  slot->loaded = 1;
  stage_slot_refresh_floor_topology_flags(slot);
  return 0;
}

int stage_collision_init(void) {
  // All allocations and IO for stage collision must happen here (init-time).
  // stage_collision_apply() is on the per-frame hot path and must remain allocation-free.

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  for (size_t i = 0; i < sizeof(g_stage_slots) / sizeof(g_stage_slots[0]); i++) {
    MslStageSlot* slot = &g_stage_slots[i];
    if (slot->loaded && slot->match_flow_loaded) {
      continue;
    }
    (void)stage_load_slot_from_data_dir(slot, data_dir);
  }

  // FD is still the default/runtime baseline, so normal init requires it. Other registered stages
  // load opportunistically and are required only when requested via stage_collision_require_stage().
  return stage_collision_require_stage((uint32_t)MSL_STAGE_ID_FINAL_DESTINATION) ? 0 : -1;
}

uint8_t stage_collision_require_stage(uint32_t stage_id) {
  MslStageSlot* slot = stage_slot_mut(stage_id);
  if (slot == NULL) {
    return 0u;
  }
  if (slot->loaded && slot->match_flow_loaded) {
    return 1u;
  }
  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }
  if (stage_load_slot_from_data_dir(slot, data_dir) != 0) {
    return 0u;
  }
  return (uint8_t)(slot->loaded && slot->match_flow_loaded);
}

uint8_t stage_collision_stage_registered(uint32_t stage_id) {
  return stage_slot(stage_id) != NULL ? 1u : 0u;
}

uint8_t stage_collision_stage_available(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  return (uint8_t)(slot != NULL && slot->loaded && slot->match_flow_loaded);
}

const MslStageFloorGraph* stage_collision_get_floor_graph(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->floor_lines != NULL && slot->floor_line_count != 0) {
    return &slot->floor_graph;
  }
  return NULL;
}

int stage_collision_floor_line_index(uint32_t stage_id, uint16_t segment_i) {
  return stage_slot_floor_line_index(stage_slot(stage_id), segment_i);
}

static const MslStageFloorLine* stage_floor_line_for_segment(const MslStageSlot* slot,
                                                             uint16_t segment_i) {
  const int line_idx = stage_slot_floor_line_index(slot, segment_i);
  if (line_idx < 0) {
    return NULL;
  }
  return &slot->floor_lines[(size_t)line_idx];
}

static int stage_slot_fighter_floor_line_index(const MslStageSlot* slot, uint16_t segment_i) {
  if (slot == NULL || !slot->loaded || slot->fighter_floor_lines == NULL ||
      slot->fighter_floor_line_count == 0u) {
    return -1;
  }
  return (int)slot->fighter_floor_line_index_by_segment[segment_i];
}

typedef struct MslStageRawLineRef {
  MslStageRawLineKind kind;
  uint8_t fighter_solid;
  int16_t raw_prev_id;
  int16_t raw_next_id;
} MslStageRawLineRef;

static uint8_t stage_collision_raw_line_ref(uint32_t stage_id, uint16_t segment_i,
                                            MslStageRawLineRef* out) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || out == NULL) {
    return 0u;
  }
  for (size_t i = 0; i < slot->floor_line_count; i++) {
    const MslStageFloorLine* line = &slot->floor_lines[i];
    if (line->segment_i == segment_i) {
      *out = (MslStageRawLineRef){
          .kind = MSL_STAGE_RAW_LINE_FLOOR,
          .fighter_solid = line->fighter_solid,
          .raw_prev_id = line->raw_prev_id,
          .raw_next_id = line->raw_next_id,
      };
      return 1u;
    }
  }
  for (size_t i = 0; i < slot->ceiling_line_count; i++) {
    const MslStageCeilingLine* line = &slot->ceiling_lines[i];
    if (line->segment_i == segment_i) {
      *out = (MslStageRawLineRef){
          .kind = MSL_STAGE_RAW_LINE_CEILING,
          .fighter_solid = line->fighter_solid,
          .raw_prev_id = line->raw_prev_id,
          .raw_next_id = line->raw_next_id,
      };
      return 1u;
    }
  }
  for (size_t i = 0; i < slot->left_wall_line_count; i++) {
    const MslStageWallLine* line = &slot->left_wall_lines[i];
    if (line->segment_i == segment_i) {
      *out = (MslStageRawLineRef){
          .kind = MSL_STAGE_RAW_LINE_LEFT_WALL,
          .fighter_solid = line->fighter_solid,
          .raw_prev_id = line->raw_prev_id,
          .raw_next_id = line->raw_next_id,
      };
      return 1u;
    }
  }
  for (size_t i = 0; i < slot->right_wall_line_count; i++) {
    const MslStageWallLine* line = &slot->right_wall_lines[i];
    if (line->segment_i == segment_i) {
      *out = (MslStageRawLineRef){
          .kind = MSL_STAGE_RAW_LINE_RIGHT_WALL,
          .fighter_solid = line->fighter_solid,
          .raw_prev_id = line->raw_prev_id,
          .raw_next_id = line->raw_next_id,
      };
      return 1u;
    }
  }
  return 0u;
}

uint8_t stage_collision_raw_line_kind(uint32_t stage_id, uint16_t segment_i,
                                      MslStageRawLineKind* out_kind) {
  MslStageRawLineRef ref = {0};
  if (!stage_collision_raw_line_ref(stage_id, segment_i, &ref)) {
    return 0u;
  }
  if (out_kind != NULL) {
    *out_kind = ref.kind;
  }
  return 1u;
}

static uint8_t stage_collision_raw_line_non_kind(uint32_t stage_id, uint16_t segment_i,
                                                 MslStageRawLineKind skip_kind, uint8_t next,
                                                 MslStageRawLineKind* out_kind,
                                                 uint16_t* out_segment_i) {
  // Source shape:
  // - mpLineGetPrev/Next traverse stable MapLine ids.
  // - mpLineNext/PrevNon* skip same-kind chains and stop before wrapping to the start line.
  // - callers that need active fighter floors also check mpLib_80054ED8; model frozen-PS
  //   suppression here by rejecting non-fighter-solid floor targets.
  // refs/melee/src/melee/mp/mplib.c::{
  //   mpLineGetPrev,mpLineGetNext,mpLineNextNonFloor,mpLinePrevNonFloor,
  //   mpLineNextNonLeftWall,mpLinePrevNonLeftWall,mpLineNextNonRightWall,mpLinePrevNonRightWall}
  const MslStageSlot* slot = stage_slot(stage_id);
  MslStageRawLineRef ref = {0};
  if (slot == NULL || !slot->loaded || !stage_collision_raw_line_ref(stage_id, segment_i, &ref)) {
    return 0u;
  }
  const uint16_t start = segment_i;
  int16_t cur = next ? ref.raw_next_id : ref.raw_prev_id;
  const size_t max_steps = slot->floor_line_count + slot->ceiling_line_count +
                           slot->left_wall_line_count + slot->right_wall_line_count + 1u;
  for (size_t step = 0; step < max_steps; step++) {
    if (cur < 0 || (uint16_t)cur == start) {
      return 0u;
    }
    if (!stage_collision_raw_line_ref(stage_id, (uint16_t)cur, &ref)) {
      return 0u;
    }
    if (ref.kind != skip_kind) {
      if (!ref.fighter_solid) {
        return 0u;
      }
      if (out_kind != NULL) {
        *out_kind = ref.kind;
      }
      if (out_segment_i != NULL) {
        *out_segment_i = (uint16_t)cur;
      }
      return 1u;
    }
    cur = next ? ref.raw_next_id : ref.raw_prev_id;
  }
  return 0u;
}

uint8_t stage_collision_raw_line_next_non_kind(uint32_t stage_id, uint16_t segment_i,
                                               MslStageRawLineKind skip_kind,
                                               MslStageRawLineKind* out_kind,
                                               uint16_t* out_segment_i) {
  return stage_collision_raw_line_non_kind(stage_id, segment_i, skip_kind, 1u, out_kind,
                                           out_segment_i);
}

uint8_t stage_collision_raw_line_prev_non_kind(uint32_t stage_id, uint16_t segment_i,
                                               MslStageRawLineKind skip_kind,
                                               MslStageRawLineKind* out_kind,
                                               uint16_t* out_segment_i) {
  return stage_collision_raw_line_non_kind(stage_id, segment_i, skip_kind, 0u, out_kind,
                                           out_segment_i);
}

const MslStageFloorGraph* stage_collision_get_fighter_floor_graph(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->fighter_floor_lines != NULL &&
      slot->fighter_floor_line_count != 0) {
    return &slot->fighter_floor_graph;
  }
  return NULL;
}

int stage_collision_fighter_floor_line_index(uint32_t stage_id, uint16_t segment_i) {
  return stage_slot_fighter_floor_line_index(stage_slot(stage_id), segment_i);
}

uint8_t stage_collision_floor_line_caps(uint32_t stage_id, uint16_t segment_i,
                                        MslStageFloorLineCaps* out) {
  const MslStageFloorLine* line = stage_floor_line_for_segment(stage_slot(stage_id), segment_i);
  if (out != NULL) {
    *out = (MslStageFloorLineCaps){0};
  }
  if (line == NULL || out == NULL) {
    return 0u;
  }
  // MSLSTG01 floor-line owner packet: expose the generated source flags together so gameplay
  // callers do not have to duplicate stage-line lookup policy for platform/transform decisions.
  // data/stages/bin/*.bin::MSLSTG01 segment flags + platform_transform records
  out->is_platform = line->is_platform ? 1u : 0u;
  out->fighter_solid = line->fighter_solid ? 1u : 0u;
  out->stage_object_support_kind = line->stage_object_support_kind;
  out->platform_transform_kind = line->platform_transform_kind;
  out->platform_transform_id = line->platform_transform_id;
  out->ground_friction_mul = line->ground_friction_mul;
  return 1u;
}

uint8_t stage_collision_floor_line_is_platform(uint32_t stage_id, uint16_t segment_i) {
  const MslStageFloorLine* line = stage_floor_line_for_segment(stage_slot(stage_id), segment_i);
  if (line == NULL) {
    return 0u;
  }
  return line->is_platform ? 1u : 0u;
}

uint8_t stage_collision_floor_line_is_runtime_fighter_solid(uint32_t stage_id, uint16_t segment_i) {
  const MslStageFloorLine* line = stage_floor_line_for_segment(stage_slot(stage_id), segment_i);
  if (line == NULL) {
    return 0u;
  }
  return line->fighter_solid ? 1u : 0u;
}

uint8_t stage_collision_floor_line_stage_object_support_kind(uint32_t stage_id,
                                                             uint16_t segment_i) {
  const MslStageFloorLine* line = stage_floor_line_for_segment(stage_slot(stage_id), segment_i);
  if (line == NULL) {
    return (uint8_t)MSL_STAGE_OBJECT_SUPPORT_NONE;
  }
  return line->stage_object_support_kind;
}

static inline uint8_t stage_floor_line_is_static_slope(const MslStageFloorLine* line) {
  return (uint8_t)(line != NULL && line->fighter_solid != 0u && line->y0 != line->y1);
}

static uint8_t stage_floor_line_is_flat_between_sloped_ledges(const MslStageSlot* slot,
                                                              const MslStageFloorLine* line) {
  if (slot == NULL || !slot->loaded || line == NULL || slot->floor_lines == NULL ||
      line->fighter_solid == 0u || line->is_platform || line->is_ledge ||
      stage_floor_line_is_static_slope(line) || line->prev < 0 || line->next < 0 ||
      (size_t)line->prev >= slot->floor_line_count ||
      (size_t)line->next >= slot->floor_line_count) {
    return 0u;
  }
  const MslStageFloorLine* prev = &slot->floor_lines[(size_t)line->prev];
  const MslStageFloorLine* next = &slot->floor_lines[(size_t)line->next];
  // Generated MSLSTG01 topology owner: the line is a flat fighter-solid floor whose immediate
  // floor-chain neighbors are sloped ledge floors. This captures Yoshi's Story main-floor shell
  // without a stage-id branch and remains false for ordinary flat floors, soft platforms, and
  // transform-owned support lines.
  // data/stages/bin/*.bin::MSLSTG01 floor flags/links/geometry
  return (uint8_t)((prev->is_ledge && stage_floor_line_is_static_slope(prev) && next->is_ledge &&
                    stage_floor_line_is_static_slope(next))
                       ? 1u
                       : 0u);
}

uint8_t stage_collision_floor_line_is_flat_between_sloped_ledges(uint32_t stage_id,
                                                                 uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  return stage_floor_line_is_flat_between_sloped_ledges(
      slot, stage_floor_line_for_segment(slot, segment_i));
}

uint8_t stage_collision_stage_has_flat_between_sloped_ledges(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  return (uint8_t)((slot != NULL && slot->loaded && slot->has_flat_between_sloped_ledges) ? 1u
                                                                                          : 0u);
}

uint8_t stage_collision_stage_has_only_static_cardinal_hard_floors(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  // Generated MSLSTG01 topology owner for stages whose fighter floor graph is only static flat
  // hard-floor terrain. Ledge flags are allowed here because they are still floor lines in the same
  // cardinal hard-floor shell; soft platforms, slopes, and moving stage-object support disqualify it.
  // data/stages/bin/*.bin::MSLSTG01 floor flags/links/geometry/platform_transforms
  return (uint8_t)((slot != NULL && slot->loaded && slot->has_only_static_cardinal_hard_floors)
                       ? 1u
                       : 0u);
}

uint8_t stage_collision_stage_has_alternate_floor_endpoint_links(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  // MSLSTG01 preserves source MapLine endpoint slot 1 links. Frozen Stadium's active floor shell
  // uses those alternate floor links for transformation-map adjacency; this helper exposes that
  // generated topology fact without naming Pokemon Stadium in gameplay code.
  // data/stages/bin/*.bin::MSLSTG01 segment prev_id1/next_id1
  return (uint8_t)((slot != NULL && slot->loaded && slot->has_alternate_floor_endpoint_links) ? 1u
                                                                                              : 0u);
}

uint8_t stage_collision_stage_has_height_platform_transform(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  return (uint8_t)((slot != NULL && slot->loaded && slot->has_height_platform_transform) ? 1u : 0u);
}

uint8_t stage_collision_stage_has_deferred_static_floor_transform(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  // Static mpLib queries intentionally exclude generated moving/deferred floor transforms such as
  // FoD height platforms and Randall. Callers that need source-current runtime transform state must
  // keep using the graph/env path until that owner is ported.
  // data/stages/bin/*.bin::MSLSTG01 platform_transform records
  return (uint8_t)((slot != NULL && slot->loaded && slot->has_deferred_static_floor_transform)
                       ? 1u
                       : 0u);
}

static void stage_slot_refresh_floor_topology_flags(MslStageSlot* slot) {
  if (slot == NULL || !slot->loaded || slot->floor_lines == NULL || slot->floor_line_count == 0u) {
    return;
  }

  uint8_t saw_fighter_floor = 0u;
  uint8_t only_static_cardinal_hard_floors = 1u;
  uint8_t has_flat_between_sloped_ledges = 0u;
  uint8_t has_alternate_floor_endpoint_links = 0u;
  for (size_t i = 0; i < slot->floor_line_count; i++) {
    const MslStageFloorLine* line = &slot->floor_lines[i];
    if (stage_floor_line_is_flat_between_sloped_ledges(slot, line)) {
      has_flat_between_sloped_ledges = 1u;
    }
    if (line->fighter_solid == 0u) {
      continue;
    }
    saw_fighter_floor = 1u;
    if (line->has_alternate_endpoint_link != 0u) {
      has_alternate_floor_endpoint_links = 1u;
    }
    if (line->is_platform || line->platform_transform_kind != 0u ||
        stage_floor_line_is_static_slope(line)) {
      only_static_cardinal_hard_floors = 0u;
    }
  }

  slot->has_flat_between_sloped_ledges = has_flat_between_sloped_ledges;
  slot->has_only_static_cardinal_hard_floors =
      (uint8_t)((saw_fighter_floor && only_static_cardinal_hard_floors) ? 1u : 0u);
  slot->has_alternate_floor_endpoint_links = has_alternate_floor_endpoint_links;
}

uint8_t stage_collision_floor_line_has_platform_transform(uint32_t stage_id, uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded) {
    return 0u;
  }
  // data/stages/bin/*.bin::MSLSTG01 platform transform records
  // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
  return slot->platform_transform_kind_by_segment[segment_i] != 0u ? 1u : 0u;
}

uint8_t stage_collision_floor_line_has_height_platform_transform(uint32_t stage_id,
                                                                 uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded) {
    return 0u;
  }
  // data/stages/bin/*.bin::MSLSTG01 platform transform records distinguish dynamic grIzumi height
  // transforms from static-y support transforms. Only the former are moving platform owners.
  // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
  return slot->platform_transform_kind_by_segment[segment_i] ==
                 (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT
             ? 1u
             : 0u;
}

uint8_t stage_collision_floor_line_has_static_y_platform_transform(uint32_t stage_id,
                                                                   uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded) {
    return 0u;
  }
  // data/stages/bin/*.bin::MSLSTG01 platform transform records distinguish static-y support
  // transforms from dynamic grIzumi height and path-based Randall transforms.
  // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
  return slot->platform_transform_kind_by_segment[segment_i] ==
                 (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y
             ? 1u
             : 0u;
}

uint8_t stage_collision_floor_line_has_randall_platform_transform(uint32_t stage_id,
                                                                  uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded) {
    return 0u;
  }
  // Yoshi's Randall is extracted as a path-transformed generated stage-object floor. Keep callers
  // keyed to MSLSTG01 transform ownership instead of stage/line constants.
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
  // refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
  // data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
  return slot->platform_transform_kind_by_segment[segment_i] ==
                 (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL
             ? 1u
             : 0u;
}

int stage_collision_randall_floor_line_index(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || slot->platform_transforms == NULL ||
      slot->platform_transform_count == 0u) {
    return -1;
  }
  // Yoshi/Randall has a single generated MSLSTG01 path-transformed floor line. Resolve that owner
  // through the extracted transform records instead of scanning every floor line in hot mpColl
  // paths.
  // data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
  for (size_t i = 0; i < slot->platform_transform_count; i++) {
    const MslStagePlatformTransform* rec = &slot->platform_transforms[i];
    if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL) {
      return stage_slot_floor_line_index(slot, rec->line_id);
    }
  }
  return -1;
}

float stage_collision_floor_ground_friction_mul(uint32_t stage_id, uint16_t segment_i) {
  const MslStageFloorLine* line = stage_floor_line_for_segment(stage_slot(stage_id), segment_i);
  if (line == NULL || !(line->ground_friction_mul > 0.0f)) {
    return 0.0f;
  }
  return line->ground_friction_mul;
}

uint8_t stage_collision_floor_line_platform_transform_id(uint32_t stage_id, uint16_t segment_i,
                                                         uint8_t* platform_id_out) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || platform_id_out == NULL ||
      slot->platform_transform_kind_by_segment[segment_i] == 0u) {
    return 0u;
  }
  // data/stages/bin/*.bin::MSLSTG01 platform transform records
  // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
  *platform_id_out = slot->platform_transform_id_by_segment[segment_i];
  return 1u;
}

static const MslStagePlatformTransform* stage_collision_platform_transform_for_line(
    const MslStageSlot* slot, uint16_t line_id) {
  if (slot == NULL || !slot->loaded || slot->platform_transforms == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < slot->platform_transform_count; i++) {
    const MslStagePlatformTransform* rec = &slot->platform_transforms[i];
    if (rec->line_id == line_id) {
      return rec;
    }
  }
  return NULL;
}

typedef struct {
  uint8_t valid;
  uint8_t current_owned;
  uint8_t trusted;
  uint8_t reached_hidden_this_step;
  float height;
} MslFodHeightPlatformLineState;

static uint8_t stage_collision_fod_height_platform_line_state(const MslBatch* batch, int bi,
                                                              const MslStageSlot* slot,
                                                              const MslStagePlatformTransform* rec,
                                                              MslFodHeightPlatformLineState* out) {
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
  }
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || slot == NULL || !slot->loaded ||
      !slot->fod_motion.loaded || rec == NULL ||
      rec->kind_id != (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT || rec->platform_id >= 2u ||
      out == NULL) {
    return 0u;
  }

  const size_t idx = (size_t)bi * 2u + (size_t)rec->platform_id;
  const uint8_t valid = batch->state.stage_fod_platform_valid[idx] != 0u ? 1u : 0u;
  const float h = valid ? batch->state.stage_fod_platform_height[idx] : rec->y_const;
  const uint8_t seed_current_source =
      (uint8_t)((batch->state.stage_fod_platform_height_source[idx] &
                 (uint8_t)(MSL_FOD_PLATFORM_HEIGHT_SOURCE_DIRECT_EVENT |
                           MSL_FOD_PLATFORM_HEIGHT_SOURCE_GROUND_CONTACT |
                           MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT)) != 0u);
  const uint8_t moving_velocity =
      (uint8_t)(batch->state.stage_fod_platform_velocity_valid[idx] != 0u &&
                fabsf(batch->state.stage_fod_platform_velocity[idx]) > 1.0e-6f);
  const uint8_t scheduler_current =
      batch->state.stage_fod_platform_scheduler_valid[idx] != 0u ? 1u : 0u;
  const MslFodPlatformMotion* motion = &slot->fod_motion;
  const uint8_t named_pose =
      (uint8_t)(valid != 0u &&
                (fabsf(h - rec->y_const) <= 1.0e-3f || fabsf(h - motion->home_height) <= 1.0e-3f ||
                 fabsf(h - motion->max_height) <= 1.0e-3f ||
                 fabsf(h - motion->min_visible_height) <= 1.0e-3f ||
                 fabsf(h - motion->hidden_target_height) <= 1.0e-3f));

  out->valid = valid;
  out->height = h;
  out->current_owned = (uint8_t)(seed_current_source || scheduler_current || moving_velocity);
  out->trusted = (uint8_t)(out->current_owned || named_pose);
  out->reached_hidden_this_step = moving_velocity;
  return 1u;
}

uint8_t stage_collision_floor_line_height_platform_state_is_source_trusted(const MslBatch* batch,
                                                                           int bi,
                                                                           uint16_t segment_i) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const MslStageSlot* slot = stage_slot(batch->state.stage_id[(size_t)bi]);
  const MslStagePlatformTransform* rec =
      stage_collision_platform_transform_for_line(slot, segment_i);
  MslFodHeightPlatformLineState state = {0};
  if (!stage_collision_fod_height_platform_line_state(batch, bi, slot, rec, &state) ||
      state.valid == 0u) {
    return 0u;
  }

  if (state.current_owned != 0u) {
    // Prefix/source-owned current platform heights are the live grIzumi/mpLib line owner even when
    // the sparse replay stream cannot derive a per-frame velocity. This separates fresh/current
    // source heights from stale carried sparse heights without enabling the free-running scheduler
    // on teacher-forced seeds.
    // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    return 1u;
  }

  // grIzumi target states are source-owned by the platform JObj and refreshed into mpLib. Sparse
  // replay reconstruction can also carry stale mid-flight heights without the hidden phase/target
  // owner; those rows must stay on mpColl's endpoint pending owner.
  // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
  // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
  return state.trusted;
}

static uint8_t stage_collision_fod_platform_default_height(const MslStageSlot* slot,
                                                           uint8_t platform_id, float* out) {
  if (slot == NULL || out == NULL || slot->platform_transforms == NULL) {
    return 0u;
  }
  for (size_t i = 0; i < slot->platform_transform_count; i++) {
    const MslStagePlatformTransform* rec = &slot->platform_transforms[i];
    if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT &&
        rec->platform_id == platform_id) {
      *out = rec->y_const;
      return 1u;
    }
  }
  return 0u;
}

uint8_t stage_collision_fod_hidden_target_height(float* out) {
  if (out == NULL) {
    return 0u;
  }
  const MslStageSlot* slot = stage_slot((uint32_t)MSL_STAGE_ID_FOUNTAIN_OF_DREAMS);
  if (slot == NULL || slot->fod_motion.loaded == 0u) {
    return 0u;
  }
  *out = slot->fod_motion.hidden_target_height;
  return 1u;
}

static uint8_t stage_collision_platform_path_world_line(const MslStageSlot* slot, uint16_t line_id,
                                                        int32_t frame_id, MslStageFloorLine* out) {
  if (slot == NULL || out == NULL || slot->platform_path_frames == NULL ||
      slot->platform_path_frame_count == 0u) {
    return 0u;
  }
  // MSL frame_id is already the post-start Slippi game frame. The generated Randall
  // `platform_path` table is keyed in that same source clock, so do not subtract the 123 pregame
  // record offset here; doing so uses a stale cloud phase during long rollouts.
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
  // refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
  // data/stages/bin/grst.bin::MSLSTG01 platform_path records
  int frame = (int)(frame_id % 1200);
  if (frame < 0) {
    frame += 1200;
  }
  if (slot->platform_path_frame_count >= 1200u) {
    const MslStagePlatformPathFrame* rec = &slot->platform_path_frames[(size_t)frame];
    if (rec->line_id == line_id && rec->frame == (uint16_t)frame) {
      out->x0 = rec->x0;
      out->x1 = rec->x1;
      out->y0 = rec->y;
      out->y1 = rec->y;
      return 1u;
    }
  }
  for (size_t i = 0; i < slot->platform_path_frame_count; i++) {
    const MslStagePlatformPathFrame* rec = &slot->platform_path_frames[i];
    if (rec->line_id == line_id && rec->frame == (uint16_t)frame) {
      // Randall source owner is the GrSt stage-object JObj animation refreshed into collision by
      // Ground_801C2FE0. Runtime consumes the generated MSLSTG01 path samples instead of keeping
      // stage-object motion constants in gameplay code.
      // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
      // refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
      // data/stages/bin/grst.bin::MSLSTG01 platform_path records
      out->x0 = rec->x0;
      out->x1 = rec->x1;
      out->y0 = rec->y;
      out->y1 = rec->y;
      return 1u;
    }
  }
  return 0u;
}

static int32_t stage_collision_platform_path_frame_id(int32_t frame_id) {
  int frame = (int)(frame_id % 1200);
  if (frame < 0) {
    frame += 1200;
  }
  return (int32_t)frame;
}

static inline void stage_collision_floor_surface_normal(float x0, float y0, float x1, float y1,
                                                        float* nx_out, float* ny_out) {
  float nx = -(y1 - y0);
  float ny = x1 - x0;
  const float len = sqrtf(nx * nx + ny * ny);
  if (len > 0.0f) {
    nx /= len;
    ny /= len;
  } else {
    nx = 0.0f;
    ny = 1.0f;
  }
  if (nx_out != NULL) {
    *nx_out = nx;
  }
  if (ny_out != NULL) {
    *ny_out = ny;
  }
}

static uint8_t stage_collision_floor_line_moving_surface_state_impl(
    const MslBatch* batch, int bi, const MslStageFloorLine* line, uint8_t include_motion,
    MslStageMovingSurfaceState* out) {
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
  }
  if (batch == NULL || line == NULL || out == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const MslStageSlot* slot = stage_slot(batch->state.stage_id[bi]);
  if (slot == NULL || slot->platform_transforms == NULL || slot->platform_transform_count == 0u) {
    return 0u;
  }
  const MslStagePlatformTransform* rec =
      stage_collision_platform_transform_for_line(slot, line->segment_i);
  if (rec == NULL || rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_NONE) {
    return 0u;
  }

  // Single source packet for generated platform-transform floor lines. FoD height platforms use
  // grIzumi-owned JObj height/scheduler state; Randall uses the generated GrStory/Ground path
  // samples. Fighter collision consumers should read this packet via world-line/result helpers
  // instead of adding action-local FoD/Randall branches.
  // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
  // refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
  // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
  // data/stages/bin/*.bin::MSLSTG01 platform_transform/platform_path/platform_motion records
  out->valid = 1u;
  out->active = 1u;
  out->visible = 1u;
  out->current_owned = 1u;
  out->source_trusted = 1u;
  out->platform_transform_kind = rec->kind_id;
  out->platform_transform_id = rec->platform_id;
  out->stage_object_support_kind = line->stage_object_support_kind;
  out->segment_i = line->segment_i;
  out->joint_id = line->joint_id;
  out->source_frame = batch->state.frame_id[bi];
  out->x0 = rec->x0;
  out->x1 = rec->x1;
  out->y0 = line->y0;
  out->y1 = line->y1;

  if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y) {
    out->y0 = rec->y_const;
    out->y1 = rec->y_const;
  } else if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL) {
    MslStageFloorLine world = *line;
    if (!stage_collision_platform_path_world_line(slot, rec->line_id, batch->state.frame_id[bi],
                                                  &world)) {
      memset(out, 0, sizeof(*out));
      return 0u;
    }
    out->x0 = world.x0;
    out->x1 = world.x1;
    out->y0 = world.y0;
    out->y1 = world.y1;
    out->source_frame = stage_collision_platform_path_frame_id(batch->state.frame_id[bi]);

    if (include_motion != 0u) {
      MslStageFloorLine prev = *line;
      MslStageFloorLine cur = *line;
      const int32_t cur_frame = batch->state.frame_id[bi] + 1;
      if (stage_collision_platform_path_world_line(slot, rec->line_id, cur_frame - 1, &prev) &&
          stage_collision_platform_path_world_line(slot, rec->line_id, cur_frame, &cur)) {
        out->velocity_x = cur.x0 - prev.x0;
        out->velocity_y = cur.y0 - prev.y0;
      }
    }
  } else if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT) {
    MslFodHeightPlatformLineState state = {0};
    if (!stage_collision_fod_height_platform_line_state(batch, bi, slot, rec, &state)) {
      return 1u;
    }
    out->valid = state.valid;
    out->current_owned = state.current_owned;
    out->source_trusted = state.trusted;
    out->reached_hidden_this_step = state.reached_hidden_this_step;

    float h = state.height;
    if (state.valid != 0u && state.current_owned != 0u) {
      if (fabsf(h - rec->y_const) <= 1.0e-3f) {
        h = rec->y_const;
      } else if (fabsf(h - slot->fod_motion.home_height) <= 1.0e-3f) {
        h = slot->fod_motion.home_height;
      } else if (fabsf(h - slot->fod_motion.max_height) <= 1.0e-3f) {
        h = slot->fod_motion.max_height;
      } else if (fabsf(h - slot->fod_motion.min_visible_height) <= 1.0e-3f) {
        h = slot->fod_motion.min_visible_height;
      } else if (fabsf(h - slot->fod_motion.hidden_target_height) <= 1.0e-3f) {
        h = slot->fod_motion.hidden_target_height;
      }
    }
    const uint8_t hidden_wait_pose =
        (uint8_t)(state.valid != 0u &&
                  fabsf(h - slot->fod_motion.hidden_target_height) <= 1.0e-4f &&
                  state.reached_hidden_this_step == 0u);
    out->visible = hidden_wait_pose ? 0u : 1u;
    out->active = hidden_wait_pose ? 0u : 1u;
    if (hidden_wait_pose) {
      out->y0 = h * rec->height_coeff;
    } else {
      out->y0 = line->y0 + h * rec->height_coeff;
    }
    out->y1 = out->y0;
    if (include_motion != 0u && rec->platform_id < 2u) {
      const size_t idx = (size_t)bi * 2u + (size_t)rec->platform_id;
      out->source_bits = batch->state.stage_fod_platform_height_source[idx];
      out->source_phase = batch->state.stage_fod_platform_scheduler_phase[idx];
      out->source_timer = batch->state.stage_fod_platform_scheduler_timer[idx];
      out->source_target = batch->state.stage_fod_platform_scheduler_target[idx];
      if (batch->state.stage_fod_platform_valid[idx] &&
          (batch->state.stage_fod_platform_scheduler_valid[idx] ||
           batch->state.stage_fod_platform_velocity_valid[idx])) {
        out->velocity_y = batch->state.stage_fod_platform_velocity[idx] * rec->height_coeff;
      }
    }
  }

  stage_collision_floor_surface_normal(out->x0, out->y0, out->x1, out->y1, &out->normal_x,
                                       &out->normal_y);
  return 1u;
}

uint8_t stage_collision_floor_line_moving_surface_state(const MslBatch* batch, int bi,
                                                        const MslStageFloorLine* line,
                                                        MslStageMovingSurfaceState* out) {
  return stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 1u, out);
}

uint8_t stage_collision_floor_line_world(const MslBatch* batch, int bi,
                                         const MslStageFloorLine* line, MslStageFloorLine* out) {
  if (line == NULL || out == NULL) {
    return 0u;
  }
  *out = *line;
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 1u;
  }
  const MslStageSlot* slot = stage_slot(batch->state.stage_id[bi]);
  if (slot == NULL || slot->platform_transforms == NULL || slot->platform_transform_count == 0u) {
    return 1u;
  }
  if (line->platform_transform_kind == 0u &&
      slot->platform_transform_kind_by_segment[line->segment_i] == 0u) {
    return 1u;
  }

  MslStageMovingSurfaceState surface = {0};
  if (!stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 0u, &surface)) {
    return 0u;
  }
  out->x0 = surface.x0;
  out->y0 = surface.y0;
  out->x1 = surface.x1;
  out->y1 = surface.y1;
  return 1u;
}

uint8_t stage_collision_get_randall_position(const MslBatch* batch, int bi, float* x_out,
                                             float* y_out) {
  if (x_out != NULL) {
    *x_out = 0.0f;
  }
  if (y_out != NULL) {
    *y_out = 0.0f;
  }
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }

  const MslStageFloorGraph* graph = stage_collision_get_floor_graph(batch->state.stage_id[bi]);
  if (graph == NULL || graph->lines == NULL) {
    return 0u;
  }
  const int line_idx = stage_collision_randall_floor_line_index(batch->state.stage_id[bi]);
  if (line_idx < 0 || (size_t)line_idx >= graph->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &graph->lines[(size_t)line_idx];
  MslStageFloorLine world = *line;
  if (!stage_collision_floor_line_world(batch, bi, line, &world)) {
    return 0u;
  }
  if (x_out != NULL) {
    *x_out = 0.5f * (world.x0 + world.x1);
  }
  if (y_out != NULL) {
    *y_out = 0.5f * (world.y0 + world.y1);
  }
  return 1u;
}

uint8_t stage_collision_floor_line_motion_delta(const MslBatch* batch, int bi,
                                                const MslStageFloorLine* line, float* dx_out,
                                                float* dy_out) {
  if (dx_out != NULL) {
    *dx_out = 0.0f;
  }
  if (dy_out != NULL) {
    *dy_out = 0.0f;
  }
  if (batch == NULL || line == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const MslStageSlot* slot = stage_slot(batch->state.stage_id[bi]);
  if (slot == NULL || slot->platform_transforms == NULL || slot->platform_transform_count == 0u) {
    return 0u;
  }
  const MslStagePlatformTransform* rec =
      stage_collision_platform_transform_for_line(slot, line->segment_i);
  if (rec == NULL) {
    return 0u;
  }
  MslStageMovingSurfaceState surface = {0};
  if (!stage_collision_floor_line_moving_surface_state(batch, bi, line, &surface)) {
    return 0u;
  }
  if (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT && surface.valid == 0u) {
    return 0u;
  }
  if (rec->kind_id != (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL &&
      rec->kind_id != (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT) {
    return 0u;
  }
  if (dx_out != NULL) {
    *dx_out = surface.velocity_x;
  }
  if (dy_out != NULL) {
    *dy_out = surface.velocity_y;
  }
  return (uint8_t)(surface.velocity_x != 0.0f || surface.velocity_y != 0.0f ||
                   rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL ||
                   (rec->kind_id == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT &&
                    rec->platform_id < 2u &&
                    (batch->state.stage_fod_platform_scheduler_valid[(size_t)bi * 2u +
                                                                     (size_t)rec->platform_id] ||
                     batch->state.stage_fod_platform_velocity_valid[(size_t)bi * 2u +
                                                                    (size_t)rec->platform_id])));
}

const MslStageCeilingGraph* stage_collision_get_ceiling_graph(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->ceiling_lines != NULL &&
      slot->ceiling_line_count != 0) {
    return &slot->ceiling_graph;
  }
  return NULL;
}

const MslStageWallGraph* stage_collision_get_left_wall_graph(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->left_wall_lines != NULL &&
      slot->left_wall_line_count != 0) {
    return &slot->left_wall_graph;
  }
  return NULL;
}

const MslStageWallGraph* stage_collision_get_right_wall_graph(uint32_t stage_id) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->right_wall_lines != NULL &&
      slot->right_wall_line_count != 0) {
    return &slot->right_wall_graph;
  }
  return NULL;
}

int stage_collision_ceiling_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || slot->ceiling_lines == NULL ||
      slot->ceiling_line_count == 0u) {
    return -1;
  }
  return (int)slot->ceiling_line_index_by_segment[segment_i];
}

int stage_collision_left_wall_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || slot->left_wall_lines == NULL ||
      slot->left_wall_line_count == 0u) {
    return -1;
  }
  return (int)slot->left_wall_line_index_by_segment[segment_i];
}

int stage_collision_right_wall_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || slot->right_wall_lines == NULL ||
      slot->right_wall_line_count == 0u) {
    return -1;
  }
  return (int)slot->right_wall_line_index_by_segment[segment_i];
}

static inline uint8_t stage_static_line_allows_joint(int16_t line_joint_id, int16_t joint_id_skip,
                                                     int16_t joint_id_only) {
  if (joint_id_skip >= 0 && line_joint_id == joint_id_skip) {
    return 0u;
  }
  if (joint_id_only >= 0 && line_joint_id != joint_id_only) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t stage_static_floor_line_query_active(const MslStageFloorLine* line) {
  if (line == NULL || !line->fighter_solid) {
    return 0u;
  }
  // Static Phase-1 substrate only. Generated moving/platform-transform records remain visible in
  // metadata but are deferred from this static query helper.
  // data/stages/bin/*.bin::MSLSTG01 platform_transform records
  return (uint8_t)(line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
                   line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y);
}

static inline uint8_t stage_static_line_query_active(uint8_t fighter_solid) {
  return fighter_solid ? 1u : 0u;
}

static inline uint8_t stage_static_sweep_overlaps_line(float sweep_min_x, float sweep_max_x,
                                                       float sweep_min_y, float sweep_max_y,
                                                       float x0, float y0, float x1, float y1,
                                                       float pad) {
  const float line_min_x = (x0 < x1 ? x0 : x1) - pad;
  const float line_max_x = (x0 > x1 ? x0 : x1) + pad;
  const float line_min_y = (y0 < y1 ? y0 : y1) - pad;
  const float line_max_y = (y0 > y1 ? y0 : y1) + pad;
  return (uint8_t)(line_max_x >= sweep_min_x && line_min_x <= sweep_max_x &&
                   line_max_y >= sweep_min_y && line_min_y <= sweep_max_y);
}

static inline void stage_static_extend_endpoints(uint8_t has_prev, uint8_t has_next, float* x0,
                                                 float* y0, float* x1, float* y1) {
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  const float old_x0 = *x0;
  const float old_y0 = *y0;
  const float old_x1 = *x1;
  const float old_y1 = *y1;
  float dist = 0.0f;
  uint8_t have_dist = 0u;
  if (has_prev) {
    dist = sqrtf(((*x0 - *x1) * (*x0 - *x1)) + ((*y0 - *y1) * (*y0 - *y1)));
    have_dist = 1u;
    if (dist > 0.001f) {
      *x0 += (*x0 - *x1) / dist;
      *y0 += (*y0 - *y1) / dist;
    }
  }
  if (has_next) {
    if (!have_dist) {
      dist =
          sqrtf(((old_x0 - old_x1) * (old_x0 - old_x1)) + ((old_y0 - old_y1) * (old_y0 - old_y1)));
    }
    if (dist > 0.001f) {
      *x1 += (*x1 - *x0) / dist;
      *y1 += (*y1 - *y0) / dist;
    }
  }
}

static inline uint8_t stage_static_intersect_line(float a0x, float a0y, float a1x, float a1y,
                                                  float b0x, float b0y, float b1x, float b1y,
                                                  float* ix_out, float* iy_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersection
  uint8_t b1_below_a = 0u;
  uint8_t b2_above_a = 0u;
  if (a0x <= a1x) {
    if ((b0x < a0x && b1x < a0x) || (a1x < b0x && a1x < b1x)) {
      return 0u;
    }
  } else if ((b0x < a1x && b1x < a1x) || (a0x < b0x && a0x < b1x)) {
    return 0u;
  }
  if (a0y <= a1y) {
    if ((b0y < a0y && b1y < a0y) || (a1y < b0y && a1y < b1y)) {
      return 0u;
    }
  } else if ((b0y < a1y && b1y < a1y) || (a0y < b0y && a0y < b1y)) {
    return 0u;
  }

  const double ah = (double)a1y - (double)a0y;
  const double d0x = (double)b0x - (double)a0x;
  const double aw = (double)a1x - (double)a0x;
  const double d0y = (double)b0y - (double)a0y;
  const double hs_b0_a = (aw * d0y) - (ah * d0x);
  if (hs_b0_a < 0.0) {
    if (hs_b0_a < -0.1) {
      return 0u;
    }
    b1_below_a = 1u;
  }

  const double d1x = (double)b1x - (double)a1x;
  const double d1y = (double)b1y - (double)a1y;
  const double hs_b1_a = (aw * d1y) - (ah * d1x);
  if (hs_b1_a > 0.0) {
    if (hs_b1_a > 0.1) {
      return 0u;
    }
    b2_above_a = 1u;
  }
  if (hs_b0_a == 0.0 && hs_b1_a == 0.0) {
    return 0u;
  }

  const double det = (d0x * d1y) - (d0y * d1x);
  if (det < hs_b0_a) {
    if (det < hs_b1_a) {
      return 0u;
    }
  } else if (det > hs_b0_a && det > hs_b1_a) {
    return 0u;
  }

  const double bw = (double)b1x - (double)b0x;
  const double bh = (double)b1y - (double)b0y;
  if (!((bw == 0.0 && bh == 0.0) || (b1_below_a && b2_above_a) || (hs_b0_a >= 0.0 && b2_above_a))) {
    const double area = (bw * ah) - (bh * aw);
    if (fabs(area) > 0.0001) {
      const double t = ((bw * d0y) - (bh * d0x)) / area;
      if (t > 0.0) {
        if (t < 1.0) {
          *ix_out = (float)((aw * t) + (double)a0x);
          *iy_out = (float)((ah * t) + (double)a0y);
        } else {
          *ix_out = a1x;
          *iy_out = a1y;
        }
      } else {
        *ix_out = a0x;
        *iy_out = a0y;
      }
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t stage_static_intersect_segment(float ax, float ay, float bx, float by,
                                                     float x0, float y0, float x1, float y1,
                                                     float* ix_out, float* iy_out) {
  return stage_static_intersect_line(x0, y0, x1, y1, ax, ay, bx, by, ix_out, iy_out);
}

static inline uint8_t stage_static_intersect_h(float a0x, float a0y, float a1x, float b0x,
                                               float b0y, float b1x, float b1y, float* ix_out,
                                               float* iy_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
  float min_ax = 0.0f;
  float max_ax = 0.0f;
  if (a0x < a1x) {
    if ((b0x < a0x && b1x < a0x) || (a1x < b0x && a1x < b1x)) {
      return 0u;
    }
    if ((double)b0y - (double)a0y < -0.0001 || (double)b1y - (double)a0y > 0.0001) {
      return 0u;
    }
    min_ax = a0x;
    max_ax = a1x;
  } else {
    if ((b0x < a1x && b1x < a1x) || (a0x < b0x && a0x < b1x)) {
      return 0u;
    }
    if ((double)b1y - (double)a0y < -0.0001 || (double)b0y - (double)a0y > 0.0001) {
      return 0u;
    }
    min_ax = a1x;
    max_ax = a0x;
  }
  const double dby = (double)b1y - (double)b0y;
  const double dbx = (double)b1x - (double)b0x;
  if (fabs(dby) < 0.0001) {
    return 0u;
  }
  double new_x = (dbx / dby * ((double)a0y - (double)b0y)) + (double)b0x;
  double dx = new_x - (double)min_ax;
  if (dx < 0.0) {
    if (dx < -0.1) {
      return 0u;
    }
    new_x = (double)min_ax;
  }
  dx = new_x - (double)max_ax;
  if (dx > 0.0) {
    if (dx > 0.1) {
      return 0u;
    }
    new_x = (double)max_ax;
  }
  *ix_out = (float)new_x;
  *iy_out = a0y;
  return 1u;
}

static inline uint8_t stage_static_intersect_v(float a0x, float a0y, float a1y, float b0x,
                                               float b0y, float b1x, float b1y, float* ix_out,
                                               float* iy_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionV
  float min_ay = 0.0f;
  float max_ay = 0.0f;
  if (a0y < a1y) {
    if ((b0y < a0y && b1y < a0y) || (a1y < b0y && a1y < b1y)) {
      return 0u;
    }
    if ((double)b1x - (double)a0x < -0.0001 || (double)b0x - (double)a0x > 0.0001) {
      return 0u;
    }
    min_ay = a0y;
    max_ay = a1y;
  } else {
    if ((b0y < a1y && b1y < a1y) || (a0y < b0y && a0y < b1y)) {
      return 0u;
    }
    if ((double)b0x - (double)a0x < -0.0001 || (double)b1x - (double)a0x > 0.0001) {
      return 0u;
    }
    min_ay = a1y;
    max_ay = a0y;
  }
  const double dby = (double)b1y - (double)b0y;
  const double dbx = (double)b1x - (double)b0x;
  if (fabs(dbx) < 0.0001) {
    return 0u;
  }
  double new_y = (dby / dbx * ((double)a0x - (double)b0x)) + (double)b0y;
  double dy = new_y - (double)min_ay;
  if (dy < 0.0) {
    if (dy < -0.1) {
      return 0u;
    }
    new_y = (double)min_ay;
  }
  dy = new_y - (double)max_ay;
  if (dy > 0.0) {
    if (dy > 0.1) {
      return 0u;
    }
    new_y = (double)max_ay;
  }
  *ix_out = a0x;
  *iy_out = (float)new_y;
  return 1u;
}

static inline void stage_static_normal(float x0, float y0, float x1, float y1, float* nx_out,
                                       float* ny_out) {
  float nx = -(y1 - y0);
  float ny = x1 - x0;
  const float len = sqrtf((nx * nx) + (ny * ny));
  if (len > 0.0f) {
    nx /= len;
    ny /= len;
  }
  *nx_out = nx;
  *ny_out = ny;
}

static inline void stage_static_hit_set(MslStageQueryHit* out, MslStageRawLineKind kind,
                                        int32_t line_idx, uint16_t segment_i, int16_t joint_id,
                                        uint16_t flags, float ix, float iy, float nx, float ny,
                                        float dist2) {
  if (out == NULL) {
    return;
  }
  *out = (MslStageQueryHit){
      .kind = kind,
      .line_idx = line_idx,
      .segment_i = segment_i,
      .joint_id = joint_id,
      .flags = flags,
      .x = ix,
      .y = iy,
      .normal_x = nx,
      .normal_y = ny,
      .dist2 = dist2,
  };
}

static inline uint8_t stage_static_consider_hit(float ax, float ay, float ix, float iy,
                                                MslStageRawLineKind kind, int32_t line_idx,
                                                uint16_t segment_i, int16_t joint_id,
                                                uint16_t flags, float nx, float ny,
                                                float* best_dist2, MslStageQueryHit* out) {
  const float dx = ix - ax;
  const float dy = iy - ay;
  const float dist2 = (dx * dx) + (dy * dy);
  if (!(dist2 < *best_dist2)) {
    return 0u;
  }
  *best_dist2 = dist2;
  stage_static_hit_set(out, kind, line_idx, segment_i, joint_id, flags, ix, iy, nx, ny, dist2);
  return 1u;
}

uint8_t stage_collision_static_query(uint32_t stage_id, uint32_t checks, float x0, float y0,
                                     float x1, float y1, uint16_t line_id_skip,
                                     int16_t joint_id_skip, int16_t joint_id_only,
                                     MslStageQueryHit* out) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (out != NULL) {
    *out = (MslStageQueryHit){0};
  }
  if (slot == NULL || !slot->loaded) {
    return 0u;
  }

  float best_dist2 = FLT_MAX;
  const uint8_t use_joint_filter = (joint_id_skip >= 0 || joint_id_only >= 0) ? 1u : 0u;
  const float sweep_min_x = x0 < x1 ? x0 : x1;
  const float sweep_max_x = x0 > x1 ? x0 : x1;
  const float sweep_min_y = y0 < y1 ? y0 : y1;
  const float sweep_max_y = y0 > y1 ? y0 : y1;
  if (checks & (uint32_t)MSL_STAGE_QUERY_FLOOR) {
    for (size_t i = 0; i < slot->floor_line_count; i++) {
      const MslStageFloorLine* line = &slot->floor_lines[i];
      if (!stage_static_floor_line_query_active(line) ||
          !stage_static_sweep_overlaps_line(sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y,
                                            line->x0, line->y0, line->x1, line->y1, 1.101f) ||
          (use_joint_filter &&
           !stage_static_line_allows_joint(line->joint_id, joint_id_skip, joint_id_only)) ||
          (line_id_skip != 0xFFFFu && line->segment_i == line_id_skip)) {
        continue;
      }
      float lx0 = line->x0, ly0 = line->y0, lx1 = line->x1, ly1 = line->y1;
      stage_static_extend_endpoints((uint8_t)(line->raw_prev_id >= 0),
                                    (uint8_t)(line->raw_next_id >= 0), &lx0, &ly0, &lx1, &ly1);
      float ix = 0.0f, iy = 0.0f;
      const uint8_t hit =
          (fabsf(ly0 - ly1) > 0.0001f)
              ? stage_static_intersect_segment(x0, y0, x1, y1, lx0, ly0, lx1, ly1, &ix, &iy)
              : ((y0 >= y1) ? stage_static_intersect_h(lx0, ly0, lx1, x0, y0, x1, y1, &ix, &iy)
                            : 0u);
      if (!hit) {
        continue;
      }
      float nx = 0.0f, ny = 0.0f;
      stage_static_normal(lx0, ly0, lx1, ly1, &nx, &ny);
      (void)stage_static_consider_hit(x0, y0, ix, iy, MSL_STAGE_RAW_LINE_FLOOR, (int32_t)i,
                                      line->segment_i, line->joint_id, line->lo_flags, nx, ny,
                                      &best_dist2, out);
    }
  }
  if (checks & (uint32_t)MSL_STAGE_QUERY_CEILING) {
    for (size_t i = 0; i < slot->ceiling_line_count; i++) {
      const MslStageCeilingLine* line = &slot->ceiling_lines[i];
      if (!stage_static_line_query_active(line->fighter_solid) ||
          !stage_static_sweep_overlaps_line(sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y,
                                            line->x0, line->y0, line->x1, line->y1, 1.101f) ||
          (use_joint_filter &&
           !stage_static_line_allows_joint(line->joint_id, joint_id_skip, joint_id_only))) {
        continue;
      }
      float lx0 = line->x0, ly0 = line->y0, lx1 = line->x1, ly1 = line->y1;
      stage_static_extend_endpoints((uint8_t)(line->raw_prev_id >= 0),
                                    (uint8_t)(line->raw_next_id >= 0), &lx0, &ly0, &lx1, &ly1);
      float ix = 0.0f, iy = 0.0f;
      const uint8_t hit =
          (fabsf(ly0 - ly1) > 0.0001f)
              ? stage_static_intersect_segment(x0, y0, x1, y1, lx0, ly0, lx1, ly1, &ix, &iy)
              : ((y0 <= y1) ? stage_static_intersect_h(lx0, ly0, lx1, x0, y0, x1, y1, &ix, &iy)
                            : 0u);
      if (!hit) {
        continue;
      }
      float nx = 0.0f, ny = 0.0f;
      stage_static_normal(lx0, ly0, lx1, ly1, &nx, &ny);
      (void)stage_static_consider_hit(x0, y0, ix, iy, MSL_STAGE_RAW_LINE_CEILING, (int32_t)i,
                                      line->segment_i, line->joint_id, line->lo_flags, nx, ny,
                                      &best_dist2, out);
    }
  }
  if (checks & (uint32_t)MSL_STAGE_QUERY_LEFT_WALL) {
    for (size_t i = 0; i < slot->left_wall_line_count; i++) {
      const MslStageWallLine* line = &slot->left_wall_lines[i];
      if (!stage_static_line_query_active(line->fighter_solid) ||
          !stage_static_sweep_overlaps_line(sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y,
                                            line->x0, line->y0, line->x1, line->y1, 0.101f) ||
          (use_joint_filter &&
           !stage_static_line_allows_joint(line->joint_id, joint_id_skip, joint_id_only))) {
        continue;
      }
      float ix = 0.0f, iy = 0.0f;
      const uint8_t hit = (fabsf(line->x0 - line->x1) > 0.0001f)
                              ? stage_static_intersect_segment(x0, y0, x1, y1, line->x0, line->y0,
                                                               line->x1, line->y1, &ix, &iy)
                              : ((x0 <= x1) ? stage_static_intersect_v(line->x0, line->y0, line->y1,
                                                                       x0, y0, x1, y1, &ix, &iy)
                                            : 0u);
      if (!hit) {
        continue;
      }
      float nx = 0.0f, ny = 0.0f;
      stage_static_normal(line->x0, line->y0, line->x1, line->y1, &nx, &ny);
      (void)stage_static_consider_hit(x0, y0, ix, iy, MSL_STAGE_RAW_LINE_LEFT_WALL, (int32_t)i,
                                      line->segment_i, line->joint_id, line->lo_flags, nx, ny,
                                      &best_dist2, out);
    }
  }
  if (checks & (uint32_t)MSL_STAGE_QUERY_RIGHT_WALL) {
    for (size_t i = 0; i < slot->right_wall_line_count; i++) {
      const MslStageWallLine* line = &slot->right_wall_lines[i];
      if (!stage_static_line_query_active(line->fighter_solid) ||
          !stage_static_sweep_overlaps_line(sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y,
                                            line->x0, line->y0, line->x1, line->y1, 0.101f) ||
          (use_joint_filter &&
           !stage_static_line_allows_joint(line->joint_id, joint_id_skip, joint_id_only))) {
        continue;
      }
      float ix = 0.0f, iy = 0.0f;
      const uint8_t hit = (fabsf(line->x0 - line->x1) > 0.0001f)
                              ? stage_static_intersect_segment(x0, y0, x1, y1, line->x0, line->y0,
                                                               line->x1, line->y1, &ix, &iy)
                              : ((x0 >= x1) ? stage_static_intersect_v(line->x0, line->y0, line->y1,
                                                                       x0, y0, x1, y1, &ix, &iy)
                                            : 0u);
      if (!hit) {
        continue;
      }
      float nx = 0.0f, ny = 0.0f;
      stage_static_normal(line->x0, line->y0, line->x1, line->y1, &nx, &ny);
      (void)stage_static_consider_hit(x0, y0, ix, iy, MSL_STAGE_RAW_LINE_RIGHT_WALL, (int32_t)i,
                                      line->segment_i, line->joint_id, line->lo_flags, nx, ny,
                                      &best_dist2, out);
    }
  }
  return (uint8_t)(best_dist2 < FLT_MAX);
}

uint8_t stage_collision_get_blast_bounds_world(uint32_t stage_id, MslStageBounds* out) {
  if (out == NULL) {
    return 0;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->match_flow_loaded) {
    out->left = slot->blast_bounds_world.left;
    out->right = slot->blast_bounds_world.right;
    out->top = slot->blast_bounds_world.top;
    out->bottom = slot->blast_bounds_world.bottom;
    return 1;
  }
  return 0;
}

uint8_t stage_collision_get_cam_bounds_world(uint32_t stage_id, MslStageBounds* out) {
  if (out == NULL) {
    return 0;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->match_flow_loaded) {
    out->left = slot->cam_bounds_world.left;
    out->right = slot->cam_bounds_world.right;
    out->top = slot->cam_bounds_world.top;
    out->bottom = slot->cam_bounds_world.bottom;
    return 1;
  }
  return 0;
}

uint8_t stage_collision_get_spawn_point(uint32_t stage_id, int port, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (port < 0 || port >= (int)MSL_MAX_PLAYERS) {
    return 0;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->match_flow_loaded) {
    *out = slot->spawn_points[port];
    return 1;
  }
  return 0;
}

uint8_t stage_collision_get_respawn_point(uint32_t stage_id, int port, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (port < 0 || port >= (int)MSL_MAX_PLAYERS) {
    return 0;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->match_flow_loaded) {
    *out = slot->respawn_points[port];
    return 1;
  }
  return 0;
}

uint8_t stage_collision_get_ledge_point(uint32_t stage_id, int side, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (!(side == 0 || side == 1)) {
    return 0;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded && slot->have_ledge_points[side]) {
    *out = slot->ledge_points[side];
    return 1;
  }
  return 0;
}

const MslStageFloorLine* stage_collision_get_ledge_floor_line(uint32_t stage_id, int side) {
  if (!(side == 0 || side == 1)) {
    return NULL;
  }
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot != NULL && slot->loaded) {
    const int16_t li = slot->ledge_floor_line_idx[side];
    if (li >= 0 && (size_t)li < slot->floor_line_count) {
      return &slot->floor_lines[(size_t)li];
    }
  }
  return NULL;
}

static inline float stage_cross2(float ax, float ay, float bx, float by) {
  return ax * by - ay * bx;
}

static inline uint8_t stage_segment_intersects(float ax0, float ay0, float ax1, float ay1,
                                               float bx0, float by0, float bx1, float by1) {
  const float rx = ax1 - ax0;
  const float ry = ay1 - ay0;
  const float sx = bx1 - bx0;
  const float sy = by1 - by0;
  const float denom = stage_cross2(rx, ry, sx, sy);
  if (denom == 0.0f) {
    return 0u;
  }

  const float qpx = bx0 - ax0;
  const float qpy = by0 - ay0;
  const float t = stage_cross2(qpx, qpy, sx, sy) / denom;
  const float u = stage_cross2(qpx, qpy, rx, ry) / denom;
  return (uint8_t)(t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f);
}

static inline uint8_t stage_floor_segment_intersects_item(float ax0, float ay0, float ax1,
                                                          float ay1, float bx0, float by0,
                                                          float bx1, float by1) {
  // mpCheckAllRemap -> mpCheckFloorRemap only admits horizontal floor lines when the motion segment
  // is travelling downward; sloped floors use the general line intersection path.
  // refs/melee/src/melee/mp/mplib.c::{mpCheckAllRemap,mpCheckFloorRemap}
  if (fabsf(by0 - by1) <= 0.0001f && ay0 < ay1) {
    return 0u;
  }
  return stage_segment_intersects(ax0, ay0, ax1, ay1, bx0, by0, bx1, by1);
}

static inline uint8_t stage_floor_prev_point_can_enter_item_floor(const MslStageFloorLine* seg,
                                                                  float x, float y) {
  if (seg == NULL) {
    return 0u;
  }
  const float min_x = fminf(seg->x0, seg->x1);
  const float max_x = fmaxf(seg->x0, seg->x1);
  if (x < min_x - 0.001f || x > max_x + 0.001f) {
    return 1u;
  }
  const float dx = seg->x1 - seg->x0;
  float floor_y = seg->y0;
  if (fabsf(dx) > 0.0001f) {
    floor_y = seg->y0 + ((x - seg->x0) * (seg->y1 - seg->y0) / dx);
  }
  return (y >= floor_y - 0.001f) ? 1u : 0u;
}

static inline uint8_t stage_ceiling_segment_intersects_item(float ax0, float ay0, float ax1,
                                                            float ay1, float bx0, float by0,
                                                            float bx1, float by1) {
  // mpCheckCeilingRemap is the mirror of the floor path: horizontal ceilings only admit upward
  // motion. Keep sloped ceilings on the general intersection branch.
  // refs/melee/src/melee/mp/mplib.c::{mpCheckAllRemap,mpCheckCeilingRemap}
  if (fabsf(by0 - by1) <= 0.0001f && ay0 > ay1) {
    return 0u;
  }
  return stage_segment_intersects(ax0, ay0, ax1, ay1, bx0, by0, bx1, by1);
}

static inline uint8_t stage_line_is_active_for_item_collision(uint8_t active_runtime_line) {
  // Item projectile collision consumes the same active mpLib line set as fighters. The generated
  // MSLSTG01 field is still named `fighter_solid`, but for frozen Pokemon it is the runtime active
  // line mask after Slippi's Stadium transform suppression, not an item-specific material rule.
  // refs/melee/src/melee/it/it_266F.c::it_8026E9A4
  // refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
  // data/stages/bin/grps.bin::MSLSTG01 segments[*].fighter_solid
  return active_runtime_line ? 1u : 0u;
}

uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded) {
    return 0;
  }
  const MslStageFloorLine* segs = slot->floor_lines;
  const size_t n = slot->floor_line_count;
  const MslStageWallLine* left_walls = slot->left_wall_lines;
  const size_t left_wall_n = slot->left_wall_line_count;
  const MslStageWallLine* right_walls = slot->right_wall_lines;
  const size_t right_wall_n = slot->right_wall_line_count;
  const MslStageCeilingLine* ceilings = slot->ceiling_lines;
  const size_t ceiling_n = slot->ceiling_line_count;
  if (segs == NULL || n == 0) {
    return 0;
  }

  // Treat the projectile as a point and intersect against stage collision segments.
  // Decomp laser collision calls it_8029C4D4 -> it_8026E9A4 against the active mpLib collision
  // line set, not a floor-only helper; FD ledge lasers can hit vertical wall segments below the
  // floor before crossing the floor y. MSLSTG01 keeps inactive frozen-Stadium transformation
  // geometry in the debug graph; those lines are not active runtime collision and must not delete
  // lasers as invisible terrain.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
  // refs/melee/src/melee/it/it_266F.c::it_8026E9A4
  // refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
  // data/stages/bin/grps.bin::MSLSTG01 segments[*].fighter_solid
  for (size_t si = 0; si < n; si++) {
    const MslStageFloorLine* seg = &segs[si];
    if (!stage_line_is_active_for_item_collision(seg->fighter_solid)) {
      continue;
    }
    if (stage_floor_segment_intersects_item(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < left_wall_n; si++) {
    const MslStageWallLine* seg = &left_walls[si];
    if (!stage_line_is_active_for_item_collision(seg->fighter_solid)) {
      continue;
    }
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < right_wall_n; si++) {
    const MslStageWallLine* seg = &right_walls[si];
    if (!stage_line_is_active_for_item_collision(seg->fighter_solid)) {
      continue;
    }
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < ceiling_n; si++) {
    const MslStageCeilingLine* seg = &ceilings[si];
    if (!stage_line_is_active_for_item_collision(seg->fighter_solid)) {
      continue;
    }
    if (stage_ceiling_segment_intersects_item(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  return 0;
}

static inline uint8_t stage_vertical_wall_intersects_sweep_source(float wall_x, float wall_y0,
                                                                  float wall_y1, float ax0,
                                                                  float ay0, float ax1, float ay1) {
  // Mirrors `mpLineIntersectionV`: vertical wall checks admit small endpoint overshoots
  // (0.1 world units) and clamp to the segment endpoint.
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionV
  const float min_y = (wall_y0 < wall_y1) ? wall_y0 : wall_y1;
  const float max_y = (wall_y0 > wall_y1) ? wall_y0 : wall_y1;
  if ((ay0 < min_y && ay1 < min_y) || (max_y < ay0 && max_y < ay1)) {
    return 0u;
  }
  if (wall_y0 < wall_y1) {
    if (ax1 - wall_x < -0.0001f || ax0 - wall_x > 0.0001f) {
      return 0u;
    }
  } else {
    if (ax0 - wall_x < -0.0001f || ax1 - wall_x > 0.0001f) {
      return 0u;
    }
  }
  const float dx = ax1 - ax0;
  if (fabsf(dx) < 0.0001f) {
    return 0u;
  }
  const float y = (((ay1 - ay0) / dx) * (wall_x - ax0)) + ay0;
  if (y < min_y) {
    return (uint8_t)((y - min_y) >= -0.1f);
  }
  if (y > max_y) {
    return (uint8_t)((y - max_y) <= 0.1f);
  }
  return 1u;
}

static inline uint8_t stage_wall_line_intersects_sweep_segment(const MslStageWallLine* seg,
                                                               int side, float ax0, float ay0,
                                                               float ax1, float ay1) {
  if (seg == NULL || !stage_line_is_active_for_item_collision(seg->fighter_solid)) {
    return 0u;
  }
  const float min_x = (ax0 < ax1) ? ax0 : ax1;
  const float max_x = (ax0 > ax1) ? ax0 : ax1;
  const float min_y = (ay0 < ay1) ? ay0 : ay1;
  const float max_y = (ay0 > ay1) ? ay0 : ay1;
  if (seg->max_x < min_x || seg->min_x > max_x || seg->max_y < min_y || seg->min_y > max_y) {
    return 0u;
  }
  if (fabsf(seg->x0 - seg->x1) <= 0.0001f) {
    if ((side == 0 && ax0 <= ax1) || (side == 1 && ax0 >= ax1)) {
      return stage_vertical_wall_intersects_sweep_source(seg->x0, seg->y0, seg->y1, ax0, ay0, ax1,
                                                         ay1);
    }
    return 0u;
  }
  return stage_segment_intersects(ax0, ay0, ax1, ay1, seg->x0, seg->y0, seg->x1, seg->y1);
}

static uint8_t stage_wall_graph_hits_fixed_ecb_sweep(const MslStageWallGraph* graph, int side,
                                                     float prev_cx, float prev_cy, float cx,
                                                     float cy, float ecb_left, float ecb_right,
                                                     float ecb_bottom, float ecb_top) {
  if (graph == NULL || graph->lines == NULL || graph->line_count == 0u) {
    return 0u;
  }
  const float side_prev_x = prev_cx + ((side == 0) ? ecb_right : ecb_left);
  const float side_prev_y = prev_cy;
  const float side_cur_x = cx + ((side == 0) ? ecb_right : ecb_left);
  const float side_cur_y = cy;
  const float bottom_prev_x = prev_cx;
  const float bottom_prev_y = prev_cy + ecb_bottom;
  const float bottom_cur_x = cx;
  const float bottom_cur_y = cy + ecb_bottom;
  const float top_prev_x = prev_cx;
  const float top_prev_y = prev_cy + ecb_top;
  const float top_cur_x = cx;
  const float top_cur_y = cy + ecb_top;
  for (size_t i = 0; i < graph->line_count; i++) {
    const MslStageWallLine* seg = &graph->lines[i];
    if (stage_wall_line_intersects_sweep_segment(seg, side, side_prev_x, side_prev_y, side_cur_x,
                                                 side_cur_y) ||
        stage_wall_line_intersects_sweep_segment(seg, side, bottom_prev_x, bottom_prev_y,
                                                 bottom_cur_x, bottom_cur_y) ||
        stage_wall_line_intersects_sweep_segment(seg, side, top_prev_x, top_prev_y, top_cur_x,
                                                 top_cur_y) ||
        stage_wall_line_intersects_sweep_segment(seg, side, bottom_cur_x, bottom_cur_y, side_cur_x,
                                                 side_cur_y) ||
        stage_wall_line_intersects_sweep_segment(seg, side, top_cur_x, top_cur_y, side_cur_x,
                                                 side_cur_y)) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t stage_collision_item_fixed_ecb_sweep_hits_wall(uint32_t stage_id, int side,
                                                       float prev_center_x, float prev_center_y,
                                                       float center_x, float center_y,
                                                       float ecb_left, float ecb_right,
                                                       float ecb_bottom, float ecb_top) {
  const MslStageWallGraph* graph =
      (side == 0) ? stage_collision_get_left_wall_graph(stage_id)
                  : ((side == 1) ? stage_collision_get_right_wall_graph(stage_id) : NULL);
  if (graph == NULL || graph->lines == NULL || graph->line_count == 0u) {
    return 0u;
  }
  const float sweep_min_x = fminf(fminf(prev_center_x + ecb_left, prev_center_x + ecb_right),
                                  fminf(center_x + ecb_left, center_x + ecb_right));
  const float sweep_max_x = fmaxf(fmaxf(prev_center_x + ecb_left, prev_center_x + ecb_right),
                                  fmaxf(center_x + ecb_left, center_x + ecb_right));
  const float sweep_min_y = fminf(fminf(prev_center_y + ecb_bottom, prev_center_y + ecb_top),
                                  fminf(center_y + ecb_bottom, center_y + ecb_top));
  const float sweep_max_y = fmaxf(fmaxf(prev_center_y + ecb_bottom, prev_center_y + ecb_top),
                                  fmaxf(center_y + ecb_bottom, center_y + ecb_top));
  if (sweep_max_x < graph->min_x || sweep_min_x > graph->max_x || sweep_max_y < graph->min_y ||
      sweep_min_y > graph->max_y) {
    return 0u;
  }
  return stage_wall_graph_hits_fixed_ecb_sweep(graph, side, prev_center_x, prev_center_y, center_x,
                                               center_y, ecb_left, ecb_right, ecb_bottom, ecb_top);
}

uint8_t stage_collision_item_fixed_ecb_sweep_hits_floor(uint32_t stage_id, float prev_center_x,
                                                        float prev_center_y, float center_x,
                                                        float center_y, float ecb_left,
                                                        float ecb_right, float ecb_bottom) {
  const MslStageSlot* slot = stage_slot(stage_id);
  if (slot == NULL || !slot->loaded || slot->floor_lines == NULL || slot->floor_line_count == 0u) {
    return 0u;
  }
  if (center_y > prev_center_y) {
    return 0u;
  }
  // Fixed-ECB item collision calls `mpColl_800471F8`, which loads the ItemAttr.x40 fixed ECB
  // (`mpColl_LoadECB_inline(..., 6)`) and runs the normal floor solver. For the Shy Guy callers
  // that only test the boolean return, the source-owned visible effect is the active animation
  // reset; `it_8026DA70` does not copy CollData.cur_pos back to Item.pos.
  // Floor admission follows mpCheckFloorRemap's floor directionality: after the reset, the next
  // upward child-JObj delta is not another floor hit even if the previous bottom point was just
  // below the floor plane.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_8004ACE4}
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloorRemap
  // refs/melee/src/melee/it/it_266F.c::it_8026DA70
  const float bottom_prev_cx = prev_center_x;
  const float bottom_prev_y = prev_center_y + ecb_bottom;
  const float bottom_cur_cx = center_x;
  const float bottom_cur_y = center_y + ecb_bottom;
  (void)ecb_left;
  (void)ecb_right;
  const float sweep_min_x = fminf(bottom_prev_cx, bottom_cur_cx);
  const float sweep_max_x = fmaxf(bottom_prev_cx, bottom_cur_cx);
  const float sweep_min_y = fminf(bottom_prev_y, bottom_cur_y);
  const float sweep_max_y = fmaxf(bottom_prev_y, bottom_cur_y);
  for (size_t i = 0; i < slot->floor_line_count; i++) {
    const MslStageFloorLine* seg = &slot->floor_lines[i];
    if (!stage_line_is_active_for_item_collision(seg->fighter_solid)) {
      continue;
    }
    const float seg_min_x = fminf(seg->x0, seg->x1);
    const float seg_max_x = fmaxf(seg->x0, seg->x1);
    const float seg_min_y = fminf(seg->y0, seg->y1);
    const float seg_max_y = fmaxf(seg->y0, seg->y1);
    if (sweep_max_x < seg_min_x || sweep_min_x > seg_max_x || sweep_max_y < seg_min_y ||
        sweep_min_y > seg_max_y) {
      continue;
    }
    // Fixed-ECB floor admission follows mpCheckFloorRemap's bottom-vertex "entering from above"
    // shape. Horizontal ECB extents are wall/side-collision inputs; using them for floor entry
    // admits lateral below-floor grazes at Yoshi's sloped edge that vanilla ignores.
    // refs/melee/src/melee/mp/mplib.c::mpCheckFloorRemap
    // refs/melee/src/melee/it/it_266F.c::it_8026DA70
    if (stage_floor_prev_point_can_enter_item_floor(seg, bottom_prev_cx, bottom_prev_y) != 0u &&
        stage_floor_segment_intersects_item(bottom_prev_cx, bottom_prev_y, bottom_cur_cx,
                                            bottom_cur_y, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1u;
    }
  }
  return 0u;
}

static int stage_fod_rand_range_span(int min_val, int max_val) {
  // gr/inlines.h::rand_range(max,min) calls HSD_Randi(max-min), so the upper bound is
  // exclusive. Equality still consumes no useful range and returns the shared endpoint.
  // refs/melee/src/melee/gr/inlines.h::rand_range
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  return (max_val > min_val) ? (max_val - min_val) : 1;
}

static void stage_collision_update_fod_platform_motion(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // grIzumi advances platform ground-object height and refreshes mpLib line coordinates before
  // fighter collision. Replay-seeded eval supplies the current height and, when recoverable from
  // prefix events/contact, the current per-frame height delta. Live/new-match runtime without a
  // replay height seed owns the same phase/timer/target state internally from extracted GrIz.dat
  // `yakumono_param`.
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_FOUNTAIN_OF_DREAMS) {
      continue;
    }
    const MslStageSlot* slot = stage_slot((uint32_t)MSL_STAGE_ID_FOUNTAIN_OF_DREAMS);
    const MslFodPlatformMotion* motion =
        (slot != NULL && slot->fod_motion.loaded) ? &slot->fod_motion : NULL;
    for (size_t platform_order = 0; platform_order < 2u; platform_order++) {
      // grIzumi creates the left platform from joint 4 before the right platform from joint 6.
      // Runtime state arrays use Slippi/extracted platform ids (0=right, 1=left), so source proc
      // order is left then right while collision/data lookup ids remain unchanged.
      // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CBE64
      const size_t platform_id = (platform_order == 0u) ? 1u : 0u;
      const size_t idx = (size_t)bi * 2u + platform_id;
      if (batch->state.stage_fod_platform_valid[idx] &&
          batch->state.stage_fod_platform_velocity_valid[idx] &&
          !batch->state.stage_fod_platform_scheduler_valid[idx]) {
        if (motion != NULL &&
            (batch->state.stage_fod_platform_height_source[idx] &
             (uint8_t)(MSL_FOD_PLATFORM_HEIGHT_SOURCE_DIRECT_EVENT |
                       MSL_FOD_PLATFORM_HEIGHT_SOURCE_GROUND_CONTACT |
                       MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT)) != 0u &&
            fabsf(batch->state.stage_fod_platform_height[idx] - motion->hidden_target_height) <=
                1.0e-3f) {
          // Source bits mark the current grIzumi/mpLib pose for this fighter callback. Visible
          // moving-platform source rows still apply velocity here so grounded riders follow the
          // platform. At the generated hidden target, however, grIzumi has already published the
          // hidden-wait target pose; a sparse replay velocity on that row is stale. Clear it so this
          // frame stays owned by the hidden target instead of looking like the final moving frame.
          // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
          // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
          batch->state.stage_fod_platform_velocity[idx] = 0.0f;
          batch->state.stage_fod_platform_velocity_valid[idx] = 0u;
          continue;
        }
        batch->state.stage_fod_platform_height[idx] +=
            batch->state.stage_fod_platform_velocity[idx];
        continue;
      }
      if (motion != NULL && !batch->state.stage_fod_platform_scheduler_valid[idx] &&
          batch->state.stage_fod_platform_valid[idx]) {
        const float h0 = batch->state.stage_fod_platform_height[idx];
        const uint8_t current_source =
            (uint8_t)((batch->state.stage_fod_platform_height_source[idx] &
                       (uint8_t)(MSL_FOD_PLATFORM_HEIGHT_SOURCE_DIRECT_EVENT |
                                 MSL_FOD_PLATFORM_HEIGHT_SOURCE_GROUND_CONTACT |
                                 MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT)) != 0u);
        float initial_h = 0.0f;
        const uint8_t at_initial = (uint8_t)(stage_collision_fod_platform_default_height(
                                                 slot, (uint8_t)platform_id, &initial_h) &&
                                             fabsf(h0 - initial_h) <= 1.0e-3f);
        const uint8_t at_named_visible =
            (uint8_t)(fabsf(h0 - motion->home_height) <= 1.0e-3f ||
                      fabsf(h0 - motion->max_height) <= 1.0e-3f ||
                      fabsf(h0 - motion->min_visible_height) <= 1.0e-3f);
        const uint8_t at_hidden = (uint8_t)(fabsf(h0 - motion->hidden_target_height) <= 1.0e-3f);
        if (current_source && (at_initial || at_named_visible || at_hidden)) {
          // A replay/current source bit proves that grIzumi has just published the JObj/mpLib pose
          // for this frame. If that source-owned pose is one of grIzumi's generated stationary
          // states, resume the scheduler instead of freezing after the transient source bit is
          // consumed. This is not exact replay RNG phase reconstruction: the next wait/target draw
          // still comes from the rollout HSD stream.
          // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
          // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
          batch->state.stage_fod_platform_scheduler_valid[idx] = 1u;
          batch->state.stage_fod_platform_scheduler_phase[idx] = at_hidden ? 3u : 0u;
          batch->state.stage_fod_platform_scheduler_timer[idx] = 0u;
          batch->state.stage_fod_platform_scheduler_target[idx] = h0;
          batch->state.stage_fod_platform_scheduler_wait_origin[idx] = 0u;
          batch->state.stage_fod_platform_scheduler_next_frame_rng[idx] = 0u;
        }
      }
      if (motion == NULL || !batch->state.stage_fod_platform_scheduler_valid[idx]) {
        continue;
      }
      if (!batch->state.stage_fod_platform_valid[idx]) {
        float h = 0.0f;
        if (!stage_collision_fod_platform_default_height(slot, (uint8_t)platform_id, &h)) {
          continue;
        }
        batch->state.stage_fod_platform_height[idx] = h;
        batch->state.stage_fod_platform_valid[idx] = 1u;
        batch->state.stage_fod_platform_scheduler_target[idx] = h;
        batch->state.stage_fod_platform_scheduler_phase[idx] = 0u;
        batch->state.stage_fod_platform_scheduler_wait_origin[idx] = 0u;
        batch->state.stage_fod_platform_scheduler_next_frame_rng[idx] = 0u;
      }

      float h = batch->state.stage_fod_platform_height[idx];
      float v = 0.0f;
      uint8_t phase = batch->state.stage_fod_platform_scheduler_phase[idx];
      uint16_t timer = batch->state.stage_fod_platform_scheduler_timer[idx];
      float target = batch->state.stage_fod_platform_scheduler_target[idx];
      uint8_t wait_origin = batch->state.stage_fod_platform_scheduler_wait_origin[idx];
      uint8_t next_frame_rng = batch->state.stage_fod_platform_scheduler_next_frame_rng[idx];
      if (phase == 0u) {
        const uint8_t visible_choice_seeded =
            (uint8_t)(batch->state.stage_fod_platform_visible_choice_valid[idx] != 0u &&
                      fabsf(h - motion->home_height) <= 1.0e-4f);
        const uint8_t hidden_return_wait = visible_choice_seeded;
        if (next_frame_rng != 0u) {
          combat_rng_use_next_replay_frame_seed_if_unconsumed(batch, bi);
        }
        const int min_wait = (int)(motion->wait_min_frames + 0.5f);
        const int max_wait = (int)(motion->wait_max_frames + 0.5f);
        const int span = stage_fod_rand_range_span(min_wait, max_wait);
        timer = visible_choice_seeded
                    ? batch->state.stage_fod_platform_visible_choice_timer[idx]
                    : (uint16_t)(min_wait + combat_rng_consume_randi_site(
                                                batch, bi, MSL_RNG_SITE_FOD_PLATFORM_WAIT, span));
        target = h;
        phase = 1u;
        wait_origin = hidden_return_wait ? 2u : 0u;
        next_frame_rng = 0u;
      } else if (phase == 1u) {
        const uint8_t min_visible_wait =
            (uint8_t)(fabsf(target - motion->min_visible_height) <= 1.0e-4f);
        // Source gates visible waits with signed `xC6-- < 0`. Existing FoD replay locks show the
        // ordinary home/max waits aligned at zero in this compact replay-clock model, but
        // lower-visible waits and waits installed immediately after a hidden return need the signed
        // underflow boundary to sample the same source frame.
        // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
        const uint8_t signed_wait =
            (uint8_t)(min_visible_wait || wait_origin == 1u || wait_origin == 2u);
        const uint8_t wait_elapsed = (uint8_t)(signed_wait ? (timer == UINT16_MAX) : (timer == 0u));
        if (!wait_elapsed) {
          timer = (uint16_t)(timer - 1u);
        } else {
          if (wait_origin == 2u) {
            combat_rng_set_replay_frame_seed_if_unconsumed(
                batch, bi, batch->state.stage_fod_platform_visible_choice_rng_seed[idx]);
            batch->state.stage_fod_platform_visible_choice_valid[idx] = 0u;
          }
          next_frame_rng = 0u;
          const float total = motion->hidden_weight + motion->stay_weight + motion->move_weight;
          const float choice =
              combat_rng_consume_randf_site(batch, bi, MSL_RNG_SITE_FOD_PLATFORM_CHOICE) * total;
          if (choice < motion->hidden_weight) {
            target = motion->hidden_target_height;
            phase = 2u;
          } else if (choice < motion->hidden_weight + motion->move_weight) {
            const float amount =
                motion->target_delta_min +
                combat_rng_consume_randf_site(batch, bi, MSL_RNG_SITE_FOD_PLATFORM_TARGET) *
                    (motion->target_delta_max - motion->target_delta_min);
            float sign = 1.0f;
            if (h < motion->home_height) {
              sign =
                  combat_rng_consume_randf_site(
                      batch, bi, MSL_RNG_SITE_FOD_PLATFORM_TARGET_ADJUST) < motion->bias_below_home
                      ? -1.0f
                      : 1.0f;
            } else if (h > motion->home_height) {
              sign =
                  combat_rng_consume_randf_site(
                      batch, bi, MSL_RNG_SITE_FOD_PLATFORM_TARGET_ADJUST) < motion->bias_above_home
                      ? 1.0f
                      : -1.0f;
            } else {
              sign = combat_rng_consume_randf_site(batch, bi,
                                                   MSL_RNG_SITE_FOD_PLATFORM_TARGET_SIDE) < 0.5f
                         ? 1.0f
                         : -1.0f;
            }
            target = h + sign * amount;
            if (target > motion->max_height) {
              target = motion->max_height;
            } else if (target < motion->min_visible_height) {
              target = motion->min_visible_height;
            }
            phase = 2u;
          } else {
            const int min_wait = (int)(motion->wait_min_frames + 0.5f);
            const int max_wait = (int)(motion->wait_max_frames + 0.5f);
            const int span = stage_fod_rand_range_span(min_wait, max_wait);
            timer = (uint16_t)(min_wait + combat_rng_consume_randi_site(
                                              batch, bi, MSL_RNG_SITE_FOD_PLATFORM_WAIT, span));
            wait_origin = 1u;
          }
        }
      } else if (phase == 2u) {
        const float delta = target - h;
        const uint8_t hidden_return_to_home =
            (uint8_t)(next_frame_rng != 0u && fabsf(target - motion->home_height) <= 1.0e-4f);
        // grIzumi writes xD0 directly to the target when the remaining distance is smaller than
        // the per-frame speed. The line refresh therefore moves by the residual delta on that
        // frame, not by zero.
        // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
        // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
        if (delta > 0.0f) {
          if (delta < motion->up_speed) {
            v = delta;
            h = target;
            phase = 0u;
            next_frame_rng = hidden_return_to_home;
          } else {
            v = motion->up_speed;
            h += v;
            next_frame_rng = hidden_return_to_home;
          }
        } else if (delta < 0.0f) {
          next_frame_rng = 0u;
          if (-delta < motion->down_speed) {
            v = delta;
            h = target;
            phase = (h < motion->min_visible_height) ? 3u : 0u;
            next_frame_rng = 0u;
          } else {
            v = -motion->down_speed;
            h += v;
          }
        } else {
          phase = 0u;
          next_frame_rng = 0u;
        }
      } else if (phase == 3u) {
        const int min_wait = (int)(motion->hidden_wait_min_frames + 0.5f);
        const int max_wait = (int)(motion->hidden_wait_max_frames + 0.5f);
        const int span = stage_fod_rand_range_span(min_wait, max_wait);
        timer = (uint16_t)(min_wait + combat_rng_consume_randi_site(
                                          batch, bi, MSL_RNG_SITE_FOD_PLATFORM_HIDDEN_WAIT, span));
        phase = 4u;
        wait_origin = 0u;
      } else if (phase == 4u) {
        if (timer != 0u) {
          timer--;
        } else {
          target = motion->home_height;
          phase = 2u;
          wait_origin = 0u;
          next_frame_rng = 0u;
        }
      } else {
        phase = 0u;
        wait_origin = 0u;
        next_frame_rng = 0u;
      }

      batch->state.stage_fod_platform_height[idx] = h;
      batch->state.stage_fod_platform_velocity[idx] = v;
      batch->state.stage_fod_platform_velocity_valid[idx] = 1u;
      batch->state.stage_fod_platform_scheduler_phase[idx] = phase;
      batch->state.stage_fod_platform_scheduler_timer[idx] = timer;
      batch->state.stage_fod_platform_scheduler_target[idx] = target;
      batch->state.stage_fod_platform_scheduler_wait_origin[idx] = wait_origin;
      batch->state.stage_fod_platform_scheduler_next_frame_rng[idx] = next_frame_rng;
    }
  }
}

static uint8_t stage_collision_whispy_point_inside(float x, float y, float left, float right,
                                                   float bottom, float top) {
  // Source helper uses strict interior checks after normalizing rectangle endpoints.
  // refs/melee/src/melee/gr/groldpupupu.c::grOldPupupu_8021128C
  return (uint8_t)(left < x && x < right && bottom < y && y < top);
}

static void stage_collision_publish_fod_ground_contact_height_sources(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_FOUNTAIN_OF_DREAMS) {
      continue;
    }
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.on_ground[idx] == 0u || batch->state.ground_id[idx] == 0xFFFFu) {
        continue;
      }
      uint8_t platform_id = 0u;
      if (!stage_collision_floor_line_platform_transform_id(
              batch->state.stage_id[bi], batch->state.ground_id[idx], &platform_id) ||
          platform_id >= 2u) {
        continue;
      }
      const size_t pidx = (size_t)bi * 2u + (size_t)platform_id;
      if (batch->state.stage_fod_platform_valid[pidx] == 0u) {
        continue;
      }
      // A live CollData floor on an FoD height-platform line proves that mpLib has current JObj
      // authority for that side-platform pose this frame. Publish the same ground-contact source
      // bit that replay preprocessing derives from grounded platform contact so later fighters in
      // this collision pass can consume source-owned transformed-platform height.
      // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
      // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_8004B108}
      batch->state.stage_fod_platform_height_source[pidx] |=
          (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_GROUND_CONTACT;
    }
  }
}

static void stage_collision_apply_dream_whispy_wind(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslDreamWhispyParams* params = stage_item_params_dream_whispy();
  if (params == NULL || params->loaded == 0u) {
    return;
  }

  // Dream Land Whispy wind is accumulated by ftColl_GetWindOffsetVec after normal physics/collision
  // and platform carry, then added directly to cur_pos.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_GetWindOffsetVec
  // refs/melee/src/melee/gr/groldpupupu.c::fn_802112F4
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_DREAM_LAND_N64 ||
        batch->state.stage_dream_whispy_wind_valid[bi] == 0u) {
      continue;
    }
    const uint8_t dir = batch->state.stage_dream_whispy_wind_dir[bi];
    // The generated seed lane is a prefix-causal current-frame reconstruction of hidden
    // `gp->gv.unk.xDC`, not Whispy's full scheduler. When reseeded inside a live wind episode, carry
    // it only through the source active-window bound: grOldPupupu_802113E0 publishes xDC for
    // xD0 in (45, 320), so a seeded active frame has at most 274 remaining wind applications.
    // This keeps same-episode rollout continuity without stale-carrying a wind direction across an
    // arbitrary long replay.
    // refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4}
    if ((dir != 1u && dir != 2u) || batch->state.stage_dream_whispy_wind_timer[bi] == 0u) {
      batch->state.stage_dream_whispy_wind_dir[bi] = 0u;
      batch->state.stage_dream_whispy_wind_valid[bi] = 0u;
      continue;
    }
    const float x_add = (dir == 1u) ? -params->wind_speed : params->wind_speed;
    const float left = (dir == 1u) ? params->left_rect_left : params->right_rect_left;
    const float right = (dir == 1u) ? params->left_rect_right : params->right_rect_right;
    const int num_players = (int)batch->config.num_players;
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u || batch->state.hitlag[idx] != 0u) {
        continue;
      }
      if (stage_collision_whispy_point_inside(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                              left, right, params->rect_bottom, params->rect_top)) {
        batch->state.pos_x[idx] += x_add;
        mpcoll_ground_refresh_grounded_root_floor_index(batch, bi, p);
      }
    }
    batch->state.stage_dream_whispy_wind_timer[bi]--;
    if (batch->state.stage_dream_whispy_wind_timer[bi] == 0u) {
      batch->state.stage_dream_whispy_wind_dir[bi] = 0u;
      batch->state.stage_dream_whispy_wind_valid[bi] = 0u;
    }
  }
}

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  stage_collision_update_fod_platform_motion(batch);
  stage_collision_publish_fod_ground_contact_height_sources(batch);
  // Ground contact substrate (mpColl-shaped): owns on_ground/ground_id for loaded MSLSTG01 stages.
  mpcoll_ground_apply(batch);
  // Wall + ceiling contact substrate (mpColl-shaped): owns wall/ceiling contact metadata.
  mpcoll_wall_ceil_apply(batch);
  stage_collision_apply_dream_whispy_wind(batch);
}
