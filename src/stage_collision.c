#include "stage_collision.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

typedef struct {
  float x0;
  float x1;
  float y0;
  float y1;
  uint16_t segment_i;  // Stable `ground_id` mapping (ISO-derived segment index).
  uint8_t include_max; // Deterministic endpoint policy for shared vertices.
} MslStageFloorSegment;

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
  // (FD currently reports `line_count`=23 in `data/stages/final_destination.json`.)
  if (line_count <= 0 || line_count > 4096) {
    return -1;
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
    uint8_t have_i = 0, have_x0 = 0, have_x1 = 0, have_y0 = 0, have_y1 = 0, have_platform = 0, have_kind = 0;

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
          if (*p == '"' ) {
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

    if (have_kind && kind_is_floor && have_platform && !platform &&
        have_i && have_x0 && have_x1 && have_y0 && have_y1) {
      if (out_n < (size_t)line_count) {
        tmp[out_n] = (MslStageFloorSegment){
            .x0 = (float)x0,
            .x1 = (float)x1,
            .y0 = (float)y0,
            .y1 = (float)y1,
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
  // vertices like x=-60 / x=60 on FD without relying on iteration order.
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
    // Note: FD's endpoints are stable float32s when extracted; this exact-equality is fine here
    // because `global_max_x` is computed from the same parsed floats. If generalized to other stages
    // (or if extraction changes), consider a more explicit/deterministic policy (e.g., track the argmax
    // segment by index/bitpattern rather than float equality).
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
  const int n = snprintf(
      path,
      sizeof(path),
      "%s/stages/final_destination.json",
      data_dir);
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
  alloc_free(buf);
  if (err != 0) {
    return -1;
  }

  g_fd_loaded = 1;
  return 0;
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
    // Slippi stage id for Final Destination.
    if (batch->state.stage_id[bi] != 32) {
      continue;
    }
    if (fd_floor_segments == NULL || fd_floor_segment_count == 0) {
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

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

      uint8_t found = 0;
      float best_y_at_x = -FLT_MAX;
      uint16_t best_segment_i = 0;

      for (size_t si = 0; si < fd_floor_segment_count; si++) {
        const MslStageFloorSegment* seg = &fd_floor_segments[si];
        if (!stage_seg_x_contains(seg, x)) {
          continue;
        }

        const float y_at_x = stage_seg_y_at_x(seg, x);

        // Only ground if we are at/below the segment surface (with epsilon), and we crossed it this
        // frame (based on pre/post integration positions). This prevents snapping up from far below.
        if (dy < 0.0f) {
          if (!(y <= (y_at_x + ground_epsilon) && y_prev >= (y_at_x - ground_epsilon))) {
            continue;
          }
        } else { // dy == 0
          if (!(y <= (y_at_x + ground_epsilon) && y >= (y_at_x - ground_epsilon))) {
            continue;
          }
        }

        if (!found || (y_at_x > best_y_at_x) ||
            (y_at_x == best_y_at_x && seg->segment_i < best_segment_i)) {
          found = 1;
          best_y_at_x = y_at_x;
          best_segment_i = seg->segment_i;
        }
      }

      if (found) {
        batch->state.pos_y[idx] = best_y_at_x;
        batch->state.on_ground[idx] = 1;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.ground_id[idx] = best_segment_i;
      } else {
        batch->state.on_ground[idx] = 0;
      }
    }
  }
}
