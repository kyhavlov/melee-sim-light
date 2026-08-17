// Deterministic chaos soak for the sealed-arena pools: drives a lineup with
// pseudo-random inputs for 60000 frames (about a 17-minute match) and tracks
// the per-frame high water of every runtime pool and class mem-piece size
// bucket. The scripted article_pool_smoke scenarios reach each burst on
// purpose; this soak reaches the bursts nobody thought to script, which is
// how the RL fleet found the aborts this tree fixes. Not part of native-smoke
// (a run takes tens of seconds); build and run it when touching pool
// reserves:
//
//   make build/melee_core/native/pool-chaos-soak  # absolute path required
//   ./build/melee_core/native/pool-chaos-soak DATA [seed [c0 c1 [c2 c3]]]
//
// Character ids are the public CSS ids from scalar.c. Reserves are validated
// by comparing the printed peaks against the pools' capacities and the
// class=192 growth against the JObj floor in msl_core_match_reset.

#include "runtime/scalar.h"
#include "runtime/observation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/pad.h>
#include <baselib/class.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/objalloc.h>
#include <baselib/robj.h>
#include "it/item.h"

static uint32_t lcg_state = 12345;
static int arg_char0 = 9, arg_char1 = 6, arg_char2 = 0, arg_char3 = 0, arg_players = 2;
static uint32_t lcg(void) {
  lcg_state = lcg_state * 1664525u + 1013904223u;
  return lcg_state >> 16;
}

static uint32_t pool_peaks[8];
static const char* pool_names[8] = {"fobj", "aobj", "gobj", "item_link", "robj", "gobjproc", "mtx"};

static void track_pools(void) {
  uint32_t used[7];
  int i;
  used[0] = HSD_ObjAllocResolve(HSD_FObjGetAllocData())->used;
  used[1] = HSD_ObjAllocResolve(HSD_AObjGetAllocData())->used;
  used[2] = HSD_ObjAllocResolve(&gobj_alloc_data)->used;
  used[3] = HSD_ObjAllocResolve(&item_link_alloc_data)->used;
  used[4] = HSD_ObjAllocResolve(HSD_RObjGetAllocData())->used;
  used[5] = HSD_ObjAllocResolve(&gobjproc_alloc_data)->used;
  used[6] = HSD_ObjAllocResolve(HSD_MtxGetAllocData())->used;
  for (i = 0; i < 7; ++i) {
    if (used[i] > pool_peaks[i]) {
      pool_peaks[i] = used[i];
    }
  }
}

static uint32_t seal_live[64];
static uint32_t peak_growth[64];
static uint32_t class_sizes[64];

static void track(const MslCoreMatch* match, int initial) {
  const HSD_ClassContext* ctx = &match->class_state;
  s32 i;
  for (i = 0; i < ctx->nb_memory_list && i < 64; ++i) {
    const HSD_MemoryEntry* e = ctx->memory_list[i];
    uint32_t live;
    if (e == NULL) {
      continue;
    }
    live = e->nb_alloc - e->nb_free;
    class_sizes[i] = e->size;
    if (initial) {
      seal_live[i] = live;
    } else if (live > seal_live[i] && live - seal_live[i] > peak_growth[i]) {
      peak_growth[i] = live - seal_live[i];
    }
  }
}

static void report(const MslCoreMatch* match, int frame) {
  const HSD_ClassContext* ctx = &match->class_state;
  s32 i;
  uint32_t free_192 = 0;
  uint32_t free_larger = 0;
  uint32_t live_192 = 0;
  for (i = 0; i < ctx->nb_memory_list; ++i) {
    const HSD_MemoryEntry* e = ctx->memory_list[i];
    if (e == NULL) {
      continue;
    }
    if (e->size == 192) {
      free_192 = e->nb_free;
      live_192 = e->nb_alloc - e->nb_free;
    } else if (e->size > 192) {
      free_larger += e->nb_free;
    }
  }
  printf("frame=%d free192=%u live192=%u free_larger=%u\n", frame, free_192,
         live_192, free_larger);
}

int main(int argc, char** argv) {
  MslCoreGameData* game_data = calloc(1, sizeof(MslCoreGameData));
  MslCoreMatch* match = calloc(1, sizeof(MslCoreMatch));
  MslCoreMatchConfig config;
  MslCoreInput previous = {0};
  MslCoreInput input;
  int frame;

  if (argc >= 3) lcg_state = (uint32_t) atoi(argv[2]);
  if (argc >= 5) { arg_char0 = atoi(argv[3]); arg_char1 = atoi(argv[4]); }
  if (argc >= 7) { arg_char2 = atoi(argv[5]); arg_char3 = atoi(argv[6]); arg_players = 4; }
  if (argc < 2 || msl_core_game_data_init(game_data, argv[1]) != 0 ||
      msl_core_match_storage_init(match) != 0) {
    fprintf(stderr, "setup failed\n");
    return 2;
  }
  memset(&config, 0, sizeof(config));
  config.stage_id = 28;  // Dream Land
  config.frame_id = -123;
  config.frame_pre_random_seed = 1;
  config.initial_random_seed = 1;
  config.match_damage_ratio = 1.0F;
  config.num_players = (uint8_t) arg_players;
  config.stock_count = 99;
  config.players[0].char_id = (uint8_t) arg_char0;
  config.players[1].char_id = (uint8_t) arg_char1;
  config.players[2].char_id = (uint8_t) arg_char2;
  config.players[3].char_id = (uint8_t) arg_char3;
  if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
    fprintf(stderr, "reset failed\n");
    return 1;
  }
  report(match, -1);
  track(match, 1);
  for (frame = 0; frame < 60000; ++frame) {
    int p;
    MslCoreStageEvents events = {0};
    memset(&input, 0, sizeof(input));
    if (frame >= 150) {
      for (p = 0; p < arg_players; ++p) {
        uint32_t r = lcg();
        int8_t sx = (int8_t)((int)(r % 3) - 1) * 80;
        int8_t sy = (int8_t)((int)((r >> 2) % 3) - 1) * 80;
        input.p[p].main_x = sx;
        input.p[p].main_y = sy;
        if ((r >> 4) % 7 == 0) input.p[p].buttons |= PAD_BUTTON_B;
        if ((r >> 7) % 7 == 0) input.p[p].buttons |= PAD_BUTTON_A;
        if ((r >> 10) % 7 == 0) input.p[p].buttons |= PAD_BUTTON_X;
        if ((r >> 13) % 7 == 0) input.p[p].buttons |= PAD_TRIGGER_Z;
      }
    }
    if (msl_core_match_step(match, &input, match->random_seed, &events) != 0) {
      fprintf(stderr, "step failed at %d\n", frame);
      return 1;
    }
    track(match, 0);
    track_pools();
    if (frame % 10000 == 0) {
      report(match, frame);
    }
  }
  report(match, frame);
  {
    int i;
    HSD_ObjAllocData* pools[7];
    pools[0] = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    pools[1] = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    pools[2] = HSD_ObjAllocResolve(&gobj_alloc_data);
    pools[3] = HSD_ObjAllocResolve(&item_link_alloc_data);
    pools[4] = HSD_ObjAllocResolve(HSD_RObjGetAllocData());
    pools[5] = HSD_ObjAllocResolve(&gobjproc_alloc_data);
    pools[6] = HSD_ObjAllocResolve(HSD_MtxGetAllocData());
    for (i = 0; i < 7; ++i) {
      printf("pool=%s peak=%u capacity=%u\n", pool_names[i], pool_peaks[i],
             pools[i]->used + pools[i]->free);
    }
    for (i = 0; i < 64; ++i) {
      if (peak_growth[i] != 0) {
        printf("class=%u seal_live=%u peak_growth=%u\n", class_sizes[i],
               seal_live[i], peak_growth[i]);
      }
    }
  }
  printf("soak survived\n");
  return 0;
}
