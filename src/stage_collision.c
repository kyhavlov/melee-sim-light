#include "stage_collision.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "action_ids.h"
#include "mpcoll_env.h"
#include "mpcoll_ground.h"
#include "mpcoll_wall_ceil.h"

typedef struct {
  float left;
  float right;
  float top;
  float bottom;
} MslStageBoundsWorld;

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

static MslStageFloorLine* g_fd_floor_lines = NULL;
static size_t g_fd_floor_line_count = 0;
static MslStageCeilingLine* g_fd_ceiling_lines = NULL;
static size_t g_fd_ceiling_line_count = 0;
static MslStageWallLine* g_fd_left_wall_lines = NULL;
static size_t g_fd_left_wall_line_count = 0;
static MslStageWallLine* g_fd_right_wall_lines = NULL;
static size_t g_fd_right_wall_line_count = 0;
static int g_fd_loaded = 0;
static uint8_t g_fd_match_flow_loaded = 0;

static MslStageFloorGraph g_fd_floor_graph;
static MslStageCeilingGraph g_fd_ceiling_graph;
static MslStageWallGraph g_fd_left_wall_graph;
static MslStageWallGraph g_fd_right_wall_graph;

static MslStageBoundsWorld g_fd_blast_bounds_world;
static MslStageBoundsWorld g_fd_cam_bounds_world;
static MslStagePoint2 g_fd_spawn_points[MSL_MAX_PLAYERS];
static MslStagePoint2 g_fd_respawn_points[MSL_MAX_PLAYERS];
static MslStagePoint2 g_fd_ledge_points[2];
static uint8_t g_fd_have_ledge_points[2];
static int16_t g_fd_ledge_floor_line_idx[2];

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

static const char* json_skip_ws(const char* s) {
  while (s && *s && isspace((unsigned char)*s)) {
    s++;
  }
  return s;
}

static const char* json_parse_string_view(const char* s, const char** out_start, size_t* out_len) {
  s = json_skip_ws(s);
  if (s == NULL || *s != '"') {
    return NULL;
  }
  s++;
  const char* start = s;
  while (*s) {
    if (*s == '\\') {
      // Skip escaped char (we don't need to unescape for schema keys/values here).
      s++;
      if (*s) {
        s++;
      }
      continue;
    }
    if (*s == '"') {
      if (out_start) {
        *out_start = start;
      }
      if (out_len) {
        *out_len = (size_t)(s - start);
      }
      return s + 1;
    }
    s++;
  }
  return NULL;
}

static const char* json_expect_char(const char* s, char c) {
  s = json_skip_ws(s);
  if (s == NULL || *s != c) {
    return NULL;
  }
  return s + 1;
}

static const char* json_parse_double(const char* s, double* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  char* end = NULL;
  errno = 0;
  double v = strtod(s, &end);
  if (end == s || errno != 0) {
    return NULL;
  }
  if (out) {
    *out = v;
  }
  return end;
}

static const char* json_parse_bounds_world(const char* s, MslStageBoundsWorld* out) {
  if (s == NULL || out == NULL) {
    return NULL;
  }
  s = json_expect_char(s, '{');
  if (s == NULL) {
    return NULL;
  }

  uint8_t have_left = 0, have_right = 0, have_top = 0, have_bottom = 0;
  float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;

  const char* p = s;
  for (;;) {
    p = json_skip_ws(p);
    if (p == NULL) {
      return NULL;
    }
    if (*p == '}') {
      p++;
      break;
    }
    if (*p == ',') {
      p++;
      continue;
    }

    const char* k = NULL;
    size_t klen = 0;
    p = json_parse_string_view(p, &k, &klen);
    if (p == NULL) {
      return NULL;
    }
    p = json_expect_char(p, ':');
    if (p == NULL) {
      return NULL;
    }
    double v = 0.0;
    p = json_parse_double(p, &v);
    if (p == NULL) {
      return NULL;
    }
    if (klen == 4 && strncmp(k, "left", 4) == 0) {
      left = (float)v;
      have_left = 1;
    } else if (klen == 5 && strncmp(k, "right", 5) == 0) {
      right = (float)v;
      have_right = 1;
    } else if (klen == 3 && strncmp(k, "top", 3) == 0) {
      top = (float)v;
      have_top = 1;
    } else if (klen == 6 && strncmp(k, "bottom", 6) == 0) {
      bottom = (float)v;
      have_bottom = 1;
    }
  }

  if (!(have_left && have_right && have_top && have_bottom)) {
    return NULL;
  }
  *out = (MslStageBoundsWorld){.left = left, .right = right, .top = top, .bottom = bottom};
  return p;
}

static const char* json_parse_point2(const char* s, MslStagePoint2* out) {
  if (s == NULL || out == NULL) {
    return NULL;
  }
  s = json_expect_char(s, '{');
  if (s == NULL) {
    return NULL;
  }
  float x = 0.0f, y = 0.0f;
  uint8_t have_x = 0, have_y = 0;
  const char* p = s;
  for (;;) {
    p = json_skip_ws(p);
    if (p == NULL) {
      return NULL;
    }
    if (*p == '}') {
      p++;
      break;
    }
    if (*p == ',') {
      p++;
      continue;
    }
    const char* k = NULL;
    size_t klen = 0;
    p = json_parse_string_view(p, &k, &klen);
    if (p == NULL) {
      return NULL;
    }
    p = json_expect_char(p, ':');
    if (p == NULL) {
      return NULL;
    }
    double v = 0.0;
    p = json_parse_double(p, &v);
    if (p == NULL) {
      return NULL;
    }
    if (klen == 1 && *k == 'x') {
      x = (float)v;
      have_x = 1;
    } else if (klen == 1 && *k == 'y') {
      y = (float)v;
      have_y = 1;
    }
  }
  if (!(have_x && have_y)) {
    return NULL;
  }
  *out = (MslStagePoint2){.x = x, .y = y};
  return p;
}

static int fd_load_match_flow_from_json(const char* json) {
  if (json == NULL) {
    return -1;
  }

  const char* bb = strstr(json, "\"blast_bounds_world\"");
  const char* cb = strstr(json, "\"cam_bounds_world\"");
  const char* sp = strstr(json, "\"spawn_points\"");
  const char* rp = strstr(json, "\"respawn_points\"");
  if (bb == NULL || cb == NULL || sp == NULL || rp == NULL) {
    g_fd_match_flow_loaded = 0;
    return -1;
  }

  // Parse bounds objects.
  bb = strchr(bb, '{');
  cb = strchr(cb, '{');
  if (bb == NULL || cb == NULL) {
    g_fd_match_flow_loaded = 0;
    return -1;
  }
  MslStageBoundsWorld blast = {0};
  MslStageBoundsWorld cam = {0};
  if (json_parse_bounds_world(bb, &blast) == NULL || json_parse_bounds_world(cb, &cam) == NULL) {
    g_fd_match_flow_loaded = 0;
    return -1;
  }

  // Parse 4-entry point arrays.
  MslStagePoint2 respawn[MSL_MAX_PLAYERS] = {0};
  uint8_t have_respawn = 0;
  {
    const char* p = strchr(rp, '[');
    if (p != NULL) {
      p++;
      int out_n = 0;
      for (;;) {
        p = json_skip_ws(p);
        if (p == NULL) {
          break;
        }
        if (*p == ']') {
          p++;
          break;
        }
        if (*p == ',') {
          p++;
          continue;
        }
        if (*p != '{') {
          break;
        }
        if (out_n >= MSL_MAX_PLAYERS) {
          break;
        }
        p = json_parse_point2(p, &respawn[out_n]);
        if (p == NULL) {
          break;
        }
        out_n++;
      }
      have_respawn = (uint8_t)(out_n == MSL_MAX_PLAYERS);
    }
  }

  MslStagePoint2 spawn[MSL_MAX_PLAYERS] = {0};
  uint8_t have_spawn = 0;
  if (sp != NULL) {
    const char* p = strchr(sp, '[');
    if (p != NULL) {
      p++;
      int out_n = 0;
      for (;;) {
        p = json_skip_ws(p);
        if (p == NULL) {
          break;
        }
        if (*p == ']') {
          p++;
          break;
        }
        if (*p == ',') {
          p++;
          continue;
        }
        if (*p != '{') {
          break;
        }
        if (out_n >= MSL_MAX_PLAYERS) {
          break;
        }
        p = json_parse_point2(p, &spawn[out_n]);
        if (p == NULL) {
          break;
        }
        out_n++;
      }
      have_spawn = (uint8_t)(out_n == MSL_MAX_PLAYERS);
    }
  }

  if (!have_respawn) {
    g_fd_match_flow_loaded = 0;
    return -1;
  }
  if (!have_spawn) {
    g_fd_match_flow_loaded = 0;
    return -1;
  }

  g_fd_blast_bounds_world = blast;
  g_fd_cam_bounds_world = cam;
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    g_fd_respawn_points[i] = respawn[i];
    g_fd_spawn_points[i] = have_spawn ? spawn[i] : (MslStagePoint2){0};
  }
  g_fd_match_flow_loaded = 1;
  return 0;
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

static inline uint8_t fd_f32_eq_ulps1(float a, float b) {
  // Stage connectivity should come from shared vertices and thus be bit-identical after extraction
  // + unit scaling. Accept a 1-ULP difference to reduce brittleness from float parsing while
  // remaining deterministic.
  union {
    float f;
    uint32_t u;
  } ua = {a}, ub = {b};
  if (ua.u == ub.u) {
    return 1;
  }
  // Reject NaNs deterministically (not expected in stage data).
  if (((ua.u & 0x7F800000u) == 0x7F800000u && (ua.u & 0x007FFFFFu) != 0) ||
      ((ub.u & 0x7F800000u) == 0x7F800000u && (ub.u & 0x007FFFFFu) != 0)) {
    return 0;
  }
  // Map float bits to an order-preserving integer space (so ULP distance is meaningful).
  uint32_t ia = ua.u;
  uint32_t ib = ub.u;
  if (ia & 0x80000000u) {
    ia = 0x80000000u - ia;
  }
  if (ib & 0x80000000u) {
    ib = 0x80000000u - ib;
  }
  const uint32_t diff = (ia > ib) ? (ia - ib) : (ib - ia);
  return (uint8_t)(diff <= 1u);
}

static void fd_build_floor_prev_next(MslStageFloorLine* lines, size_t n) {
  if (lines == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].prev = -1;
    lines[i].next = -1;
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      // prev: a line whose right endpoint equals our left endpoint.
      if (fd_f32_eq_ulps1(lines[j].x1, lines[i].x0) && fd_f32_eq_ulps1(lines[j].y1, lines[i].y0)) {
        lines[i].prev = (int16_t)j;
        break;
      }
    }
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      // next: a line whose left endpoint equals our right endpoint.
      if (fd_f32_eq_ulps1(lines[j].x0, lines[i].x1) && fd_f32_eq_ulps1(lines[j].y0, lines[i].y1)) {
        lines[i].next = (int16_t)j;
        break;
      }
    }
  }
}

static void fd_build_ceiling_prev_next(MslStageCeilingLine* lines, size_t n) {
  if (lines == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].prev = -1;
    lines[i].next = -1;
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      if (fd_f32_eq_ulps1(lines[j].x1, lines[i].x0) && fd_f32_eq_ulps1(lines[j].y1, lines[i].y0)) {
        lines[i].prev = (int16_t)j;
        break;
      }
    }
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      if (fd_f32_eq_ulps1(lines[j].x0, lines[i].x1) && fd_f32_eq_ulps1(lines[j].y0, lines[i].y1)) {
        lines[i].next = (int16_t)j;
        break;
      }
    }
  }
}

static void fd_build_wall_prev_next(MslStageWallLine* lines, size_t n) {
  if (lines == NULL) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    lines[i].prev = -1;
    lines[i].next = -1;
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      if (fd_f32_eq_ulps1(lines[j].x1, lines[i].x0) && fd_f32_eq_ulps1(lines[j].y1, lines[i].y0)) {
        lines[i].prev = (int16_t)j;
        break;
      }
    }
    for (size_t j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      if (fd_f32_eq_ulps1(lines[j].x0, lines[i].x1) && fd_f32_eq_ulps1(lines[j].y0, lines[i].y1)) {
        lines[i].next = (int16_t)j;
        break;
      }
    }
  }
}

typedef enum {
  FD_SEG_UNKNOWN = 0,
  FD_SEG_FLOOR = 1,
  FD_SEG_CEILING = 2,
  FD_SEG_LEFT_WALL = 3,
  FD_SEG_RIGHT_WALL = 4,
} FdSegKind;

typedef struct {
  FdSegKind kind;
  uint8_t ledge;
  uint8_t _pad0[2];
  uint16_t segment_i;
  float x0;
  float y0;
  float x1;
  float y1;
} FdSegTmp;

static FdSegTmp fd_seg_tmp_normalized(FdSegKind kind, uint8_t ledge, uint16_t segment_i, float fx0,
                                      float fy0, float fx1, float fy1) {
  // Normalize orientation to match mplib assumptions for each line kind:
  // - floor: x0 <= x1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // - ceiling: x0 >= x1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
  // - left_wall: y0 <= y1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
  // - right_wall: y0 >= y1
  //   refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
  if (kind == FD_SEG_FLOOR) {
    if (fx1 < fx0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
    }
  } else if (kind == FD_SEG_CEILING) {
    if (fx1 > fx0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
    }
  } else if (kind == FD_SEG_LEFT_WALL) {
    if (fy1 < fy0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
    }
  } else if (kind == FD_SEG_RIGHT_WALL) {
    if (fy1 > fy0) {
      const float tx = fx0;
      const float ty = fy0;
      fx0 = fx1;
      fy0 = fy1;
      fx1 = tx;
      fy1 = ty;
    }
  }
  return (FdSegTmp){
      .kind = kind,
      .ledge = ledge,
      .segment_i = segment_i,
      .x0 = fx0,
      .y0 = fy0,
      .x1 = fx1,
      .y1 = fy1,
  };
}

static int fd_install_stage_segments(const FdSegTmp* seg_tmp, size_t seg_n) {
  if (seg_tmp == NULL || seg_n == 0) {
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
          .is_ledge = s->ledge,
          .has_prev_link = 0,
          .has_next_link = 0,
          ._pad0 = 0,
          .segment_i = s->segment_i,
          .prev = -1,
          .next = -1,
      };
    } else if (s->kind == FD_SEG_CEILING) {
      ceil_lines[oi_ceil++] = (MslStageCeilingLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .segment_i = s->segment_i,
          .prev = -1,
          .next = -1,
      };
    } else if (s->kind == FD_SEG_LEFT_WALL) {
      lw_lines[oi_lw++] = (MslStageWallLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .segment_i = s->segment_i,
          .prev = -1,
          .next = -1,
      };
    } else if (s->kind == FD_SEG_RIGHT_WALL) {
      rw_lines[oi_rw++] = (MslStageWallLine){
          .x0 = s->x0,
          .y0 = s->y0,
          .x1 = s->x1,
          .y1 = s->y1,
          .has_prev_link = 0,
          .has_next_link = 0,
          .segment_i = s->segment_i,
          .prev = -1,
          .next = -1,
      };
    }
  }

  fd_sort_floor_lines_by_id(floor_lines, floor_n);
  fd_build_floor_prev_next(floor_lines, floor_n);
  if (ceil_n) {
    fd_sort_ceiling_lines_by_id(ceil_lines, ceil_n);
    fd_build_ceiling_prev_next(ceil_lines, ceil_n);
  }
  if (lw_n) {
    fd_sort_wall_lines_by_id(lw_lines, lw_n);
    fd_build_wall_prev_next(lw_lines, lw_n);
  }
  if (rw_n) {
    fd_sort_wall_lines_by_id(rw_lines, rw_n);
    fd_build_wall_prev_next(rw_lines, rw_n);
  }

  // FD ledge candidates are floor segments with the ISO-derived LINE_FLAG_LEDGE bit.
  //
  // Decomp context: fighter cliff physics snaps each frame to the cliff point obtained from
  // `mpLib_80053ECC_Floor` / `mpLib_80053DA4_Floor`.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  g_fd_have_ledge_points[0] = 0;
  g_fd_have_ledge_points[1] = 0;
  g_fd_ledge_points[0] = (MslStagePoint2){0};
  g_fd_ledge_points[1] = (MslStagePoint2){0};
  g_fd_ledge_floor_line_idx[0] = -1;
  g_fd_ledge_floor_line_idx[1] = -1;
  float best_left_x = FLT_MAX;
  float best_right_x = -FLT_MAX;
  for (size_t i = 0; i < floor_n; i++) {
    const MslStageFloorLine* l = &floor_lines[i];
    if (!l->is_ledge) {
      continue;
    }
    if (l->x0 < best_left_x) {
      best_left_x = l->x0;
      g_fd_ledge_points[0] = (MslStagePoint2){.x = l->x0, .y = l->y0};
      g_fd_have_ledge_points[0] = 1;
      g_fd_ledge_floor_line_idx[0] = (int16_t)i;
    }
    if (l->x1 > best_right_x) {
      best_right_x = l->x1;
      g_fd_ledge_points[1] = (MslStagePoint2){.x = l->x1, .y = l->y1};
      g_fd_have_ledge_points[1] = 1;
      g_fd_ledge_floor_line_idx[1] = (int16_t)i;
    }
  }

  // Compute endpoint connectivity hints (mpLib_8004ED5C uses prev/next only as boolean checks).
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  //
  // IMPORTANT: for floor grounding, keep the legacy behavior of extending endpoints only when
  // connected to other floor segments (floor graph prev/next), not when connected to walls/ceilings.
  // This avoids introducing floor-only collision behavior changes when we add wall/ceiling graphs.
  for (size_t i = 0; i < floor_n; i++) {
    floor_lines[i].has_prev_link = (uint8_t)(floor_lines[i].prev >= 0);
    floor_lines[i].has_next_link = (uint8_t)(floor_lines[i].next >= 0);
  }
  for (size_t i = 0; i < ceil_n; i++) {
    const uint16_t seg_i = ceil_lines[i].segment_i;
    const float sx0 = ceil_lines[i].x0;
    const float sy0 = ceil_lines[i].y0;
    const float sx1 = ceil_lines[i].x1;
    const float sy1 = ceil_lines[i].y1;
    uint8_t c0 = 0;
    uint8_t c1 = 0;
    for (size_t j = 0; j < seg_n; j++) {
      if (seg_tmp[j].segment_i == seg_i) {
        continue;
      }
      const float ax0 = seg_tmp[j].x0;
      const float ay0 = seg_tmp[j].y0;
      const float ax1 = seg_tmp[j].x1;
      const float ay1 = seg_tmp[j].y1;
      if ((fd_f32_eq_ulps1(ax0, sx0) && fd_f32_eq_ulps1(ay0, sy0)) ||
          (fd_f32_eq_ulps1(ax1, sx0) && fd_f32_eq_ulps1(ay1, sy0))) {
        c0 = 1;
      }
      if ((fd_f32_eq_ulps1(ax0, sx1) && fd_f32_eq_ulps1(ay0, sy1)) ||
          (fd_f32_eq_ulps1(ax1, sx1) && fd_f32_eq_ulps1(ay1, sy1))) {
        c1 = 1;
      }
      if (c0 && c1) {
        break;
      }
    }
    ceil_lines[i].has_prev_link = c0;
    ceil_lines[i].has_next_link = c1;
  }
  for (size_t i = 0; i < lw_n; i++) {
    const uint16_t seg_i = lw_lines[i].segment_i;
    const float sx0 = lw_lines[i].x0;
    const float sy0 = lw_lines[i].y0;
    const float sx1 = lw_lines[i].x1;
    const float sy1 = lw_lines[i].y1;
    uint8_t c0 = 0;
    uint8_t c1 = 0;
    for (size_t j = 0; j < seg_n; j++) {
      if (seg_tmp[j].segment_i == seg_i) {
        continue;
      }
      const float ax0 = seg_tmp[j].x0;
      const float ay0 = seg_tmp[j].y0;
      const float ax1 = seg_tmp[j].x1;
      const float ay1 = seg_tmp[j].y1;
      if ((fd_f32_eq_ulps1(ax0, sx0) && fd_f32_eq_ulps1(ay0, sy0)) ||
          (fd_f32_eq_ulps1(ax1, sx0) && fd_f32_eq_ulps1(ay1, sy0))) {
        c0 = 1;
      }
      if ((fd_f32_eq_ulps1(ax0, sx1) && fd_f32_eq_ulps1(ay0, sy1)) ||
          (fd_f32_eq_ulps1(ax1, sx1) && fd_f32_eq_ulps1(ay1, sy1))) {
        c1 = 1;
      }
      if (c0 && c1) {
        break;
      }
    }
    lw_lines[i].has_prev_link = c0;
    lw_lines[i].has_next_link = c1;
  }
  for (size_t i = 0; i < rw_n; i++) {
    const uint16_t seg_i = rw_lines[i].segment_i;
    const float sx0 = rw_lines[i].x0;
    const float sy0 = rw_lines[i].y0;
    const float sx1 = rw_lines[i].x1;
    const float sy1 = rw_lines[i].y1;
    uint8_t c0 = 0;
    uint8_t c1 = 0;
    for (size_t j = 0; j < seg_n; j++) {
      if (seg_tmp[j].segment_i == seg_i) {
        continue;
      }
      const float ax0 = seg_tmp[j].x0;
      const float ay0 = seg_tmp[j].y0;
      const float ax1 = seg_tmp[j].x1;
      const float ay1 = seg_tmp[j].y1;
      if ((fd_f32_eq_ulps1(ax0, sx0) && fd_f32_eq_ulps1(ay0, sy0)) ||
          (fd_f32_eq_ulps1(ax1, sx0) && fd_f32_eq_ulps1(ay1, sy0))) {
        c0 = 1;
      }
      if ((fd_f32_eq_ulps1(ax0, sx1) && fd_f32_eq_ulps1(ay0, sy1)) ||
          (fd_f32_eq_ulps1(ax1, sx1) && fd_f32_eq_ulps1(ay1, sy1))) {
        c1 = 1;
      }
      if (c0 && c1) {
        break;
      }
    }
    rw_lines[i].has_prev_link = c0;
    rw_lines[i].has_next_link = c1;
  }

  alloc_free(g_fd_floor_lines);
  alloc_free(g_fd_ceiling_lines);
  alloc_free(g_fd_left_wall_lines);
  alloc_free(g_fd_right_wall_lines);
  g_fd_floor_lines = floor_lines;
  g_fd_floor_line_count = floor_n;
  g_fd_ceiling_lines = ceil_lines;
  g_fd_ceiling_line_count = ceil_n;
  g_fd_left_wall_lines = lw_lines;
  g_fd_left_wall_line_count = lw_n;
  g_fd_right_wall_lines = rw_lines;
  g_fd_right_wall_line_count = rw_n;
  g_fd_floor_graph.lines = g_fd_floor_lines;
  g_fd_floor_graph.line_count = g_fd_floor_line_count;
  g_fd_ceiling_graph.lines = g_fd_ceiling_lines;
  g_fd_ceiling_graph.line_count = g_fd_ceiling_line_count;
  fd_stage_ceiling_graph_set_bounds(&g_fd_ceiling_graph);
  g_fd_left_wall_graph.lines = g_fd_left_wall_lines;
  g_fd_left_wall_graph.line_count = g_fd_left_wall_line_count;
  fd_stage_wall_graph_set_bounds(&g_fd_left_wall_graph);
  g_fd_right_wall_graph.lines = g_fd_right_wall_lines;
  g_fd_right_wall_graph.line_count = g_fd_right_wall_line_count;
  fd_stage_wall_graph_set_bounds(&g_fd_right_wall_graph);

  return 0;
}

enum {
  MSLSTG01_VERSION = 1,
  MSLSTG01_HEADER_BYTES = 56,
  MSLSTG01_SEGMENT_BYTES = 24,
  MSLSTG01_FLAG_PLATFORM = 1,
  MSLSTG01_FLAG_LEDGE = 2,
  MSLSTG01_KIND_FLOOR = 0,
  MSLSTG01_KIND_CEILING = 1,
  MSLSTG01_KIND_RIGHT_WALL = 2,
  MSLSTG01_KIND_LEFT_WALL = 3,
  MSLSTG01_KIND_DYNAMIC = 4,
};

static uint16_t stage_read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
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
    default:
      return FD_SEG_UNKNOWN;
  }
}

static int fd_load_floor_lines_from_mslstg01(const uint8_t* buf, size_t sz) {
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
  const size_t expected =
      (size_t)MSLSTG01_HEADER_BYTES + (size_t)segment_count * (size_t)MSLSTG01_SEGMENT_BYTES +
      (size_t)stage_point_count * 12u + (size_t)spawn_count * 8u + (size_t)respawn_count * 8u;
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
    if ((flags & (uint8_t)MSLSTG01_FLAG_PLATFORM) != 0u ||
        kind_id == (uint8_t)MSLSTG01_KIND_DYNAMIC) {
      continue;
    }
    const FdSegKind kind = fd_seg_kind_from_mslstg(kind_id);
    if (!(kind == FD_SEG_FLOOR || kind == FD_SEG_CEILING || kind == FD_SEG_LEFT_WALL ||
          kind == FD_SEG_RIGHT_WALL)) {
      continue;
    }
    // MSLSTG01 v1 stores exactly the source collision segment endpoints used by the previous
    // JSON path; FD's stage scale is 1.0. Keep match-flow roles on JSON because MSLSTG01 marks
    // spawn/respawn/camera/blast roles reserved until the DAT -> stage_info.x280 mapping is known.
    // docs/DATA_CONTRACT.md::MSLSTG01
    seg_tmp[seg_n++] =
        fd_seg_tmp_normalized(kind, (uint8_t)((flags & (uint8_t)MSLSTG01_FLAG_LEDGE) != 0u),
                              line_id, stage_read_f32_le(p + 8), stage_read_f32_le(p + 12),
                              stage_read_f32_le(p + 16), stage_read_f32_le(p + 20));
  }
  const int err = fd_install_stage_segments(seg_tmp, seg_n);
  alloc_free(seg_tmp);
  return err;
}

static int fd_load_floor_lines_from_mslstg01_file(const char* path) {
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
  const int err = fd_load_floor_lines_from_mslstg01(buf, (size_t)sz);
  alloc_free(buf);
  return err;
}

int stage_collision_init(void) {
  if (g_fd_loaded) {
    return 0;
  }

  // All allocations and IO for stage collision must happen here (init-time).
  // stage_collision_apply() is on the per-frame hot path and must remain allocation-free.

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  char path[512];
  int n = snprintf(path, sizeof(path), "%s/stages/bin/grnla.bin", data_dir);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }
  if (fd_load_floor_lines_from_mslstg01_file(path) != 0) {
    return -1;
  }

  // Match-flow roles remain on the legacy JSON until MSLSTG01 has a source-backed stage point
  // role mapping. Collision segments above are loaded from MSLSTG01.
  n = snprintf(path, sizeof(path), "%s/stages/final_destination.json", data_dir);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
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

  char* buf = (char*)alloc_malloc((size_t)sz + 1);
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
  buf[sz] = '\0';

  if (fd_load_match_flow_from_json(buf) != 0) {
    alloc_free(buf);
    return -1;
  }
  alloc_free(buf);

  g_fd_loaded = 1;
  return 0;
}

const MslStageFloorGraph* stage_collision_get_floor_graph(uint32_t stage_id) {
  if (!g_fd_loaded) {
    return NULL;
  }
  if (stage_id != 32) {
    return NULL;
  }
  if (g_fd_floor_lines == NULL || g_fd_floor_line_count == 0) {
    return NULL;
  }
  return &g_fd_floor_graph;
}

int stage_collision_floor_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL) {
    return -1;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].segment_i == segment_i) {
      return (int)i;
    }
  }
  return -1;
}

const MslStageCeilingGraph* stage_collision_get_ceiling_graph(uint32_t stage_id) {
  if (!g_fd_loaded) {
    return NULL;
  }
  if (stage_id != 32) {
    return NULL;
  }
  if (g_fd_ceiling_lines == NULL || g_fd_ceiling_line_count == 0) {
    return NULL;
  }
  return &g_fd_ceiling_graph;
}

const MslStageWallGraph* stage_collision_get_left_wall_graph(uint32_t stage_id) {
  if (!g_fd_loaded) {
    return NULL;
  }
  if (stage_id != 32) {
    return NULL;
  }
  if (g_fd_left_wall_lines == NULL || g_fd_left_wall_line_count == 0) {
    return NULL;
  }
  return &g_fd_left_wall_graph;
}

const MslStageWallGraph* stage_collision_get_right_wall_graph(uint32_t stage_id) {
  if (!g_fd_loaded) {
    return NULL;
  }
  if (stage_id != 32) {
    return NULL;
  }
  if (g_fd_right_wall_lines == NULL || g_fd_right_wall_line_count == 0) {
    return NULL;
  }
  return &g_fd_right_wall_graph;
}

int stage_collision_ceiling_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageCeilingGraph* g = stage_collision_get_ceiling_graph(stage_id);
  if (g == NULL) {
    return -1;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].segment_i == segment_i) {
      return (int)i;
    }
  }
  return -1;
}

int stage_collision_left_wall_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageWallGraph* g = stage_collision_get_left_wall_graph(stage_id);
  if (g == NULL) {
    return -1;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].segment_i == segment_i) {
      return (int)i;
    }
  }
  return -1;
}

int stage_collision_right_wall_line_index(uint32_t stage_id, uint16_t segment_i) {
  const MslStageWallGraph* g = stage_collision_get_right_wall_graph(stage_id);
  if (g == NULL) {
    return -1;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].segment_i == segment_i) {
      return (int)i;
    }
  }
  return -1;
}

uint8_t stage_collision_get_blast_bounds_world(uint32_t stage_id, MslStageBounds* out) {
  if (out == NULL) {
    return 0;
  }
  if (!g_fd_loaded || !g_fd_match_flow_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  out->left = g_fd_blast_bounds_world.left;
  out->right = g_fd_blast_bounds_world.right;
  out->top = g_fd_blast_bounds_world.top;
  out->bottom = g_fd_blast_bounds_world.bottom;
  return 1;
}

uint8_t stage_collision_get_cam_bounds_world(uint32_t stage_id, MslStageBounds* out) {
  if (out == NULL) {
    return 0;
  }
  if (!g_fd_loaded || !g_fd_match_flow_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  out->left = g_fd_cam_bounds_world.left;
  out->right = g_fd_cam_bounds_world.right;
  out->top = g_fd_cam_bounds_world.top;
  out->bottom = g_fd_cam_bounds_world.bottom;
  return 1;
}

uint8_t stage_collision_get_spawn_point(uint32_t stage_id, int port, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (!g_fd_loaded || !g_fd_match_flow_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  if (port < 0 || port >= (int)MSL_MAX_PLAYERS) {
    return 0;
  }
  *out = g_fd_spawn_points[port];
  return 1;
}

uint8_t stage_collision_get_respawn_point(uint32_t stage_id, int port, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (!g_fd_loaded || !g_fd_match_flow_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  if (port < 0 || port >= (int)MSL_MAX_PLAYERS) {
    return 0;
  }
  *out = g_fd_respawn_points[port];
  return 1;
}

uint8_t stage_collision_get_ledge_point(uint32_t stage_id, int side, MslStagePoint2* out) {
  if (out == NULL) {
    return 0;
  }
  if (!g_fd_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  if (!(side == 0 || side == 1)) {
    return 0;
  }
  if (!g_fd_have_ledge_points[side]) {
    return 0;
  }
  *out = g_fd_ledge_points[side];
  return 1;
}

const MslStageFloorLine* stage_collision_get_ledge_floor_line(uint32_t stage_id, int side) {
  if (!g_fd_loaded) {
    return NULL;
  }
  if (stage_id != 32) {
    return NULL;
  }
  if (!(side == 0 || side == 1)) {
    return NULL;
  }
  const int16_t li = g_fd_ledge_floor_line_idx[side];
  if (li < 0) {
    return NULL;
  }
  if ((size_t)li >= g_fd_floor_line_count) {
    return NULL;
  }
  return &g_fd_floor_lines[(size_t)li];
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

uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1) {
  if (!g_fd_loaded) {
    return 0;
  }
  if (stage_id != 32) {
    return 0;
  }
  const MslStageFloorLine* segs = g_fd_floor_lines;
  const size_t n = g_fd_floor_line_count;
  if (segs == NULL || n == 0) {
    return 0;
  }

  // Treat the projectile as a point and intersect against stage collision segments.
  // Decomp laser collision calls it_8029C4D4 -> it_8026E9A4, not a floor-only helper; FD ledge
  // lasers can hit vertical wall segments below the floor before crossing the floor y.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
  // refs/melee/src/melee/it/it_266F.c::it_8026E9A4
  for (size_t si = 0; si < n; si++) {
    const MslStageFloorLine* seg = &segs[si];
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < g_fd_left_wall_line_count; si++) {
    const MslStageWallLine* seg = &g_fd_left_wall_lines[si];
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < g_fd_right_wall_line_count; si++) {
    const MslStageWallLine* seg = &g_fd_right_wall_lines[si];
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  for (size_t si = 0; si < g_fd_ceiling_line_count; si++) {
    const MslStageCeilingLine* seg = &g_fd_ceiling_lines[si];
    if (stage_segment_intersects(x0, y0, x1, y1, seg->x0, seg->y0, seg->x1, seg->y1)) {
      return 1;
    }
  }
  return 0;
}

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  // Ground contact substrate (mpColl-shaped): owns on_ground/ground_id for FD.
  mpcoll_ground_apply(batch);
  // Wall + ceiling contact substrate (mpColl-shaped): owns wall/ceiling contact metadata for FD.
  mpcoll_wall_ceil_apply(batch);
}
