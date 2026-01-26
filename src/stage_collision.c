#include "stage_collision.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "action_ids.h"
#include "ecb_tables.h"

typedef struct {
  float x0;
  float x1;
  float y0;
  float y1;
  uint16_t segment_i;   // Stable `ground_id` mapping (ISO-derived segment index).
  uint8_t include_max;  // Deterministic endpoint policy for shared vertices.
} MslStageFloorSegment;

typedef struct {
  float left;
  float right;
  float top;
  float bottom;
} MslStageBoundsWorld;

static inline uint8_t stage_seg_x_contains(const MslStageFloorSegment* seg, float x) {
  const float min_x = (seg->x0 < seg->x1) ? seg->x0 : seg->x1;
  const float max_x = (seg->x0 < seg->x1) ? seg->x1 : seg->x0;
  if (x < min_x || x > max_x) {
    return 0;
  }
  // Half-open range by default: [min_x, max_x), with deterministic inclusion of the global right edge.
  if (x == max_x && !seg->include_max) {
    return 0;
  }
  return 1;
}

static inline float stage_seg_y_at_x(const MslStageFloorSegment* seg, float x) {
  const float dx = seg->x1 - seg->x0;
  if (dx == 0.0f) {
    return (seg->y0 > seg->y1) ? seg->y0 : seg->y1;
  }
  const float t = (x - seg->x0) / dx;
  return seg->y0 + (seg->y1 - seg->y0) * t;
}

static MslStageFloorSegment* g_fd_floor_segments = NULL;
static size_t g_fd_floor_segment_count = 0;
static int g_fd_loaded = 0;
static uint8_t g_fd_match_flow_loaded = 0;

static MslStageBoundsWorld g_fd_blast_bounds_world;
static MslStageBoundsWorld g_fd_cam_bounds_world;
static MslStagePoint2 g_fd_spawn_points[MSL_MAX_PLAYERS];
static MslStagePoint2 g_fd_respawn_points[MSL_MAX_PLAYERS];

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

static const char* json_parse_bool(const char* s, uint8_t* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  if (strncmp(s, "true", 4) == 0) {
    if (out) {
      *out = 1;
    }
    return s + 4;
  }
  if (strncmp(s, "false", 5) == 0) {
    if (out) {
      *out = 0;
    }
    return s + 5;
  }
  return NULL;
}

static const char* json_parse_int32(const char* s, int32_t* out) {
  s = json_skip_ws(s);
  if (s == NULL) {
    return NULL;
  }
  char* end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (end == s || errno != 0) {
    return NULL;
  }
  if (out) {
    *out = (int32_t)v;
  }
  return end;
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
  *out = (MslStageBoundsWorld){ .left = left, .right = right, .top = top, .bottom = bottom };
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
  *out = (MslStagePoint2){ .x = x, .y = y };
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
  if (bb == NULL || cb == NULL || rp == NULL) {
    // Optional: allow stage collision to load even if match-flow points are absent.
    g_fd_match_flow_loaded = 0;
    return 0;
  }

  // Parse bounds objects.
  bb = strchr(bb, '{');
  cb = strchr(cb, '{');
  if (bb == NULL || cb == NULL) {
    g_fd_match_flow_loaded = 0;
    return 0;
  }
  MslStageBoundsWorld blast = {0};
  MslStageBoundsWorld cam = {0};
  if (json_parse_bounds_world(bb, &blast) == NULL || json_parse_bounds_world(cb, &cam) == NULL) {
    g_fd_match_flow_loaded = 0;
    return 0;
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
    return 0;
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

static int fd_load_floor_segments_from_json(const char* json) {
  if (json == NULL) {
    return -1;
  }

  // Temporary loader: parse ISO-extracted `data/stages/*.json` at init-time only.
  // We will switch to a compact binary stage collision artifact later to avoid JSON parsing entirely.
  //
  // IMPORTANT: this function is init-only and may allocate; stage_collision_apply() must remain alloc-free.

  // Extract `line_count` to size our temporary segment buffer deterministically.
  const char* lc = strstr(json, "\"line_count\"");
  if (lc == NULL) {
    return -1;
  }
  lc = strchr(lc, ':');
  if (lc == NULL) {
    return -1;
  }
  lc++;
  int32_t line_count = 0;
  if (json_parse_int32(lc, &line_count) == NULL) {
    return -1;
  }
  // Safety guard: cap allocations for malformed/untrusted files. Real Melee stages are far below this.
  // (FD currently reports `line_count`=16 in `data/stages/final_destination.json`.)
  if (line_count <= 0 || line_count > 4096) {
    return -1;
  }

  // Extracted stage coordinates are unscaled `coll_data->verts` from the stage DAT.
  // (See `tools/extraction/extract_stage_collision.py` and decomp notes there.)
  //
  // Note: `data/stages/*.json` also stores `unit_scale` (aka `grGroundParam.x0` / `Ground_801C0498()`),
  // which `mpLibLoad()` uses to build the runtime scaled collision vertices:
  // - `f31 = Ground_801C0498()` reads `stage_info.param->x0` (`grGroundParam.x0`)
  //   refs/melee/src/melee/gr/ground.c:270
  // - `groundCollVtx[i].pos = f31 * coll_data->verts[i]`
  //   refs/melee/src/melee/mp/mplib.c:174,252-263
  //
  // Slippi post-frame positions are in the runtime/world coordinate system, so we apply `unit_scale`
  // to the extracted segment coordinates here at init-time.

  double unit_scale = 1.0;
  const char* us = strstr(json, "\"unit_scale\"");
  if (us != NULL) {
    us = strchr(us, ':');
    if (us != NULL) {
      us++;
      (void)json_parse_double(us, &unit_scale);
    }
  }
  if (!(unit_scale > 0.0)) {
    unit_scale = 1.0;
  }

  const char* segs = strstr(json, "\"segments\"");
  if (segs == NULL) {
    return -1;
  }
  segs = strchr(segs, '[');
  if (segs == NULL) {
    return -1;
  }

  // Allocate at most `line_count` segments; we compact the floor subset into the prefix.
  MslStageFloorSegment* tmp =
      (MslStageFloorSegment*)alloc_calloc((size_t)line_count, sizeof(MslStageFloorSegment));
  if (tmp == NULL) {
    return -1;
  }
  size_t out_n = 0;

  const char* p = segs + 1;
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
      alloc_free(tmp);
      return -1;
    }
    p++;

    uint8_t platform = 0;
    uint8_t kind_is_floor = 0;
    int32_t seg_i = -1;
    double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
    uint8_t have_i = 0, have_x0 = 0, have_x1 = 0, have_y0 = 0, have_y1 = 0, have_platform = 0,
            have_kind = 0;

    for (;;) {
      p = json_skip_ws(p);
      if (p == NULL) {
        alloc_free(tmp);
        return -1;
      }
      if (*p == '}') {
        p++;
        break;
      }
      if (*p == ',') {
        p++;
        continue;
      }

      const char* key = NULL;
      size_t key_len = 0;
      p = json_parse_string_view(p, &key, &key_len);
      if (p == NULL) {
        alloc_free(tmp);
        return -1;
      }
      p = json_expect_char(p, ':');
      if (p == NULL) {
        alloc_free(tmp);
        return -1;
      }

      if (key_len == 4 && strncmp(key, "kind", 4) == 0) {
        const char* val = NULL;
        size_t val_len = 0;
        p = json_parse_string_view(p, &val, &val_len);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        kind_is_floor = (uint8_t)(val_len == 5 && strncmp(val, "floor", 5) == 0);
        have_kind = 1;
      } else if (key_len == 8 && strncmp(key, "platform", 8) == 0) {
        p = json_parse_bool(p, &platform);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_platform = 1;
      } else if (key_len == 1 && *key == 'i') {
        p = json_parse_int32(p, &seg_i);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_i = 1;
      } else if (key_len == 2 && strncmp(key, "x0", 2) == 0) {
        p = json_parse_double(p, &x0);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_x0 = 1;
      } else if (key_len == 2 && strncmp(key, "x1", 2) == 0) {
        p = json_parse_double(p, &x1);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_x1 = 1;
      } else if (key_len == 2 && strncmp(key, "y0", 2) == 0) {
        p = json_parse_double(p, &y0);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_y0 = 1;
      } else if (key_len == 2 && strncmp(key, "y1", 2) == 0) {
        p = json_parse_double(p, &y1);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        have_y1 = 1;
      } else {
        // Skip unknown value (primitive/object/array) by scanning until the next ',' or '}' at depth 0.
        p = json_skip_ws(p);
        if (p == NULL) {
          alloc_free(tmp);
          return -1;
        }
        int depth = 0;
        for (; *p; p++) {
          if (*p == '"') {
            // skip string
            const char* dummy = NULL;
            size_t dummy_len = 0;
            const char* next = json_parse_string_view(p, &dummy, &dummy_len);
            if (next == NULL) {
              alloc_free(tmp);
              return -1;
            }
            p = next - 1;
            continue;
          }
          if (*p == '{' || *p == '[') {
            depth++;
          } else if (*p == '}' || *p == ']') {
            if (depth == 0) {
              break;
            }
            depth--;
          } else if (*p == ',' && depth == 0) {
            break;
          }
        }
      }
    }

    if (have_kind && kind_is_floor && have_platform && !platform && have_i && have_x0 && have_x1 &&
        have_y0 && have_y1) {
      if (out_n < (size_t)line_count) {
        tmp[out_n] = (MslStageFloorSegment){
            .x0 = (float)(unit_scale * x0),
            .x1 = (float)(unit_scale * x1),
            .y0 = (float)(unit_scale * y0),
            .y1 = (float)(unit_scale * y1),
            .segment_i = (uint16_t)seg_i,
            .include_max = 0,
        };
        out_n++;
      }
    }
  }

  if (out_n == 0) {
    alloc_free(tmp);
    return -1;
  }

  // Deterministic endpoint policy: floor segments are treated as [min_x, max_x) except that any segment
  // that touches the global rightmost x gets to include its max endpoint. This disambiguates shared
  // vertices without relying on iteration order.
  float global_max_x = -FLT_MAX;
  for (size_t i = 0; i < out_n; i++) {
    const float a = tmp[i].x0;
    const float b = tmp[i].x1;
    const float mx = (a > b) ? a : b;
    if (mx > global_max_x) {
      global_max_x = mx;
    }
  }
  for (size_t i = 0; i < out_n; i++) {
    const float a = tmp[i].x0;
    const float b = tmp[i].x1;
    const float mx = (a > b) ? a : b;
    // Note: exact float-equality is fine here because `global_max_x` is computed from the same parsed
    // float32 values. If generalized to other stages (or if extraction changes), consider a more
    // explicit/deterministic policy (e.g., track the argmax segment by index/bitpattern).
    tmp[i].include_max = (uint8_t)(mx == global_max_x);
  }

  alloc_free(g_fd_floor_segments);
  g_fd_floor_segments = tmp;
  g_fd_floor_segment_count = out_n;
  return 0;
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
  const int n = snprintf(path, sizeof(path), "%s/stages/final_destination.json", data_dir);
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

  const int err = fd_load_floor_segments_from_json(buf);
  (void)fd_load_match_flow_from_json(buf);
  alloc_free(buf);
  if (err != 0) {
    return -1;
  }

  g_fd_loaded = 1;
  return 0;
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

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Final Destination grounding based on extracted stage collision segments.
  //
  // Source of truth: `data/stages/final_destination.json` (ISO-derived).
  // We currently only ground against non-platform `kind:"floor"` segments for FD.
  // `segment_i` is the stable, ISO-derived segment index (used as `ground_id`).
  const MslStageFloorSegment* fd_floor_segments = g_fd_floor_segments;
  const size_t fd_floor_segment_count = g_fd_floor_segment_count;

  // Numerical tolerance: accept tiny positive/negative penetration around the segment surface.
  const float ground_epsilon = 1024.0f * FLT_EPSILON;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Stage ids in our datasets come from Slippi `game.start.stage` (see tools/slippi/make_dataset_from_slp.py),
    // which corresponds to the GALE01 "stage kind" / `InternalStageId` (aka `Stage_802251E8(idx, ...)` arg).
    //
    // In vanilla, `InternalStageId` is mapped to the StageData index via `unk_arr_803E9960[idx].stage_id`.
    // For Final Destination, idx=32 maps to stage_id=37 which selects `grNLa_803E7F90` ("/GrNLa.dat").
    // Decomp refs:
    // - refs/melee/src/melee/gr/stage.c:341-349 (unk_arr_803E9960)
    // - refs/melee/src/melee/gr/ground.c:124-139 (Ground_803DFEDC includes grNLa_803E7F90)
    // - refs/melee/src/melee/gr/grlast.c:151 (grNLa_803E7F90 uses "/GrNLa.dat")
    if (batch->state.stage_id[bi] != 32) {
      continue;
    }
    if (fd_floor_segments == NULL || fd_floor_segment_count == 0) {
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];

      // Match flow states (Dead*/Rebirth*/Entry*) are not grounded against the stage collision mesh
      // in the suite (on_ground=0). Skip grounding to avoid snapping during invisible/respawn
      // phases.
      if (action_id == (uint16_t)MSL_ACT_DEAD_DOWN || action_id == (uint16_t)MSL_ACT_DEAD_LEFT ||
          action_id == (uint16_t)MSL_ACT_DEAD_RIGHT ||
          action_id == (uint16_t)MSL_ACT_DEAD_UP_STAR || action_id == (uint16_t)MSL_ACT_REBIRTH ||
          action_id == (uint16_t)MSL_ACT_REBIRTH_WAIT || action_id == (uint16_t)MSL_ACT_ENTRY ||
          action_id == (uint16_t)MSL_ACT_ENTRY_START || action_id == (uint16_t)MSL_ACT_ENTRY_END) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      // Ordering contract: physics_integrate() records prev_pos_* and then integrates pos_*.
      // stage_collision_apply() runs after physics_integrate() (see src/step.c).
      const float y_prev = batch->state.prev_pos_y[idx];
      const float dy = y - y_prev;

      if (dy > 0.0f) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      // Grounding uses ECB bottom (decomp-shaped): compare/snap based on the per-frame minimum Y of
      // the 6 ECB source joints (mpColl_LoadECB_JObj expands the ECB to contain those joints).
      //
      // Source of truth: `data/ecb/<char>_bottom.bin` (derived from SSANIM01 v3 matrices and
      // `data/characters/<char>.json` ecb_joints).
      const uint32_t anim = batch->state.animation_index[idx];
      const int af = (int)batch->state.action_frame[idx];
      const int af_prev = (af > 0) ? (af - 1) : 0;

      // Decomp-first: use previous ECB when comparing pre/post positions (mpColl uses prev_ecb vs ecb).
      // This removes the dy==0 teacher-forcing shortcut and makes grounding stable under pose-driven
      // ECB changes even when root dy==0.
      //
      // Source pointers:
      // - ECB extrema from joints: refs/melee/src/melee/mp/mpcoll.c:328 (mpColl_LoadECB_JObj joint loop)
      // - prev_ecb usage examples: refs/melee/src/melee/mp/mpcoll.c:1377-1381 (prev_bottom vs bottom)
      const float ecb_off = msl_ecb_bottom_rel_y(batch->state.char_id[idx], anim, af);
      const float prev_ecb_off = msl_ecb_bottom_rel_y(batch->state.char_id[idx], anim, af_prev);
      const float y_bot = y + ecb_off;
      const float y_prev_bot = y_prev + prev_ecb_off;

      // Optional (helps ground_id at boundaries): use ECB footprint to break ties between same-height
      // floor segments. Note that extracted matrices are fighter-local with TransN removed; mirror X
      // based on facing like mpColl_LoadECB_Fixed does for (front,back).
      MslEcbExtentsRel ex = msl_ecb_extents_rel(batch->state.char_id[idx], anim, af);
      float ecb_left_rel_x = ex.min_x;
      float ecb_right_rel_x = ex.max_x;
      if (!batch->state.facing[idx]) {
        const float l = -ex.max_x;
        const float r = -ex.min_x;
        ecb_left_rel_x = l;
        ecb_right_rel_x = r;
      }
      const float ecb_left_world_x = x + ecb_left_rel_x;
      const float ecb_right_world_x = x + ecb_right_rel_x;

      uint8_t found = 0;
      float best_y_at_x = -FLT_MAX;
      uint16_t best_segment_i = 0;
      uint8_t best_foot_score = 0;

      for (size_t si = 0; si < fd_floor_segment_count; si++) {
        const MslStageFloorSegment* seg = &fd_floor_segments[si];
        if (!stage_seg_x_contains(seg, x)) {
          continue;
        }

        const float y_at_x = stage_seg_y_at_x(seg, x);

        // Only ground if we are at/below the segment surface (with epsilon), and we crossed it this
        // frame (based on pre/post integration positions). This prevents snapping up from far below.
        if (dy < 0.0f) {
          if (!(y_bot <= (y_at_x + ground_epsilon) && y_prev_bot >= (y_at_x - ground_epsilon))) {
            continue;
          }
        } else {  // dy == 0
          // If we were grounded entering the frame, gate stability on the previous-frame ECB bottom
          // instead of skipping the constraint entirely.
          if (batch->state.prev_on_ground[idx]) {
            // If we were grounded entering the frame, use prev ECB to keep grounded actions stable
            // when pose changes move ECB bottom without any vertical root motion.
            //
            // Important for teacher-forcing/reseeds: do not require the seeded root Y to already be
            // perfectly aligned with ECB bottom; allow snapping up from penetration deterministically.
            if (!((y_bot <= (y_at_x + ground_epsilon)) ||
                  (y_prev_bot >= (y_at_x - ground_epsilon)))) {
              continue;
            }
          } else if (!(y_bot <= (y_at_x + ground_epsilon) && y_bot >= (y_at_x - ground_epsilon))) {
            continue;
          }
        }

        const uint8_t foot_score = (uint8_t)(stage_seg_x_contains(seg, ecb_left_world_x) ? 1 : 0) +
                                   (uint8_t)(stage_seg_x_contains(seg, ecb_right_world_x) ? 1 : 0);

        if (!found || (y_at_x > best_y_at_x) ||
            (y_at_x == best_y_at_x && foot_score > best_foot_score) ||
            (y_at_x == best_y_at_x && foot_score == best_foot_score &&
             seg->segment_i < best_segment_i)) {
          found = 1;
          best_y_at_x = y_at_x;
          best_segment_i = seg->segment_i;
          best_foot_score = foot_score;
        }
      }

      if (found) {
        // Snap the fighter root so ECB bottom rests on the segment surface.
        //
        // For dy==0 when we were grounded entering the frame, avoid snapping *down* from above the
        // surface (pose can lift ECB bottom slightly while still grounded). Still snap up to
        // resolve penetration deterministically.
        const float snap_y = best_y_at_x - ecb_off;
        if (dy == 0.0f && batch->state.prev_on_ground[idx]) {
          if (y_bot <= (best_y_at_x + ground_epsilon)) {
            batch->state.pos_y[idx] = snap_y;
          }
        } else {
          batch->state.pos_y[idx] = snap_y;
        }
        batch->state.on_ground[idx] = 1;
        // Preserve pre-collision vertical velocity on the *landing* frame (air -> ground) to match
        // Slippi post-frames: on_ground can become true while `velocities.self_y` remains negative
        // for exactly one frame, then resets to 0 on the next grounded frame.
        if (batch->state.prev_on_ground[idx]) {
          batch->state.speed_y_self[idx] = 0.0f;
        }
        batch->state.ground_id[idx] = best_segment_i;
      } else {
        batch->state.on_ground[idx] = 0;
      }
    }
  }
}
