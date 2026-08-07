// Guards the sealed-arena headroom of the runtime object pools against the
// deepest supported article animation graphs.
//
// Article animation loads a whole joint graph in a single frame, so pool
// demand is the sum of the concurrent per-fighter bursts. Ness's PK Flash
// detonation is the deepest such graph: itNessPKFlashExplode_UnkMotion0_Anim
// walks the explosion joint tree through Item_80268BE0/HSD_JObjAddAnim, and
// every joint track takes one FObj. Samus's grapple deploy runs the same shape
// across the beam link chain. A flat match-wide FObj reserve was a per-fighter
// bound in disguise; four simultaneous max-charge detonations overran it and
// aborted in HSD_ObjAllocAddFree, because growing a pool after the Match arena
// is sealed by msl_memory_finish_initialization is forbidden.
//
// The burst is not confined to one pool. A joint graph costs FObj tracks,
// AObjs, GObjs, class mem-pieces, and -- for the beam chains -- ItemLinks all
// at once, and each of those reserves was an independent flat constant sized
// against a single port. Four airborne Samus grapple-catches walked through
// them one at a time: GObj, then class mem-pieces, then AObj, then ItemLink at
// 150 of a 151 link reserve.
//
// Each scenario drives its fighters into that burst, then asserts both that
// the articles actually spawned and that every pool kept real headroom. The
// article assertions are what keep this test honest: without them, a timing
// change that stopped the scripted inputs from reaching the burst would leave
// the test silently covering nothing. The headroom bar is the forward guard,
// and it is the whole assertion for lineups that were merely thin rather than
// broken. The class mem-piece allocator has no pool record to sample, so
// completing a scenario without aborting in hsdAllocMemPiece is its check.
//
// refs/melee/src/melee/it/items/{itnesspkflashexplode.c,itsamusgrapple.c}
// refs/melee/src/sysdolphin/baselib/{objalloc.c,fobj.c,aobj.c,gobj.c,class.c}
// src/runtime/scalar.c::msl_core_match_reset

#include "runtime/scalar.h"
#include "runtime/observation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/pad.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/objalloc.h>
#include "it/forward.h"
#include "it/item.h"

enum {
  // The entry animation is still running well past this; a B edge pressed
  // during entry is swallowed and never starts the special.
  CHARGE_START_FRAME = 150,
  // Max charge detonates near frame 330. Keep a tail so the explosion article
  // is fully loaded and released inside the measured window.
  TOTAL_FRAMES = 420,
  // Fraction of pool capacity that must remain unused. A reserve that only
  // just fits is a latent abort in a state these scripts do not reach.
  REQUIRED_HEADROOM_PERCENT = 20,
  // Grab cadence for Samus ports: long enough to close distance between
  // grapples, short enough that several beams are live at once.
  GRAPPLE_PERIOD = 16,
  GRAPPLE_HOLD = 2,
  // Public CSS character ids. scalar.c keeps its own private copies of these,
  // so the values are repeated rather than shared.
  CHAR_NESS = 8,
  CHAR_SAMUS = 13,
  // Full stick deflection on the source input wire.
  STICK_MAX = 80,
};

// Samus's grapple has two reachable shapes and the airborne one is deeper:
// four ports measured 452 live FObjs in the air against 362 on the ground.
typedef enum GrappleMode { GRAPPLE_GROUND, GRAPPLE_AIR } GrappleMode;

typedef struct Scenario {
  const char* name;
  uint8_t stage_id;
  uint8_t num_players;
  uint8_t char_ids[MSL_CORE_MAX_PLAYERS];
  GrappleMode grapple_mode;
  // Concurrent PK Flash detonations the scenario must actually produce.
  uint32_t expected_detonations;
  // Concurrent live grapple beams the scenario must actually produce.
  uint32_t expected_grapples;
} Scenario;

static const Scenario scenarios[] = {
    // Worst FObj case for a single fighter: every port charges from the same
    // frame, so the detonations and their article loads overlap.
    {"four-ness-dreamland", 28, 4, {8, 8, 8, 8}, GRAPPLE_GROUND, 4, 0},
    // Worst case for every other pool. Four airborne grapple-catches overran
    // the AObj, GObj, and class mem-piece reserves, each of which was a flat
    // constant sized against one port.
    {"four-samus-air-fd", 32, 4, {13, 13, 13, 13}, GRAPPLE_AIR, 0, 2},
    // Mixed lineup: Ness and Samus bursts overlap, so the reserve has to
    // cover their sum rather than the larger of the two. This scenario does
    // not abort under a per-fighter maximum -- it was thin there rather than
    // broken -- so the headroom bar below is what guards the sum form.
    {"ness-samus-dreamland", 28, 4, {8, 8, 13, 13}, GRAPPLE_GROUND, 2, 2},
    // The pairing reported from RL: Ness's detonation lands on top of Link's
    // resident article graph, the heaviest in the supported roster after Ness.
    {"ness-link-dreamland", 28, 2, {8, 6, 0, 0}, GRAPPLE_GROUND, 1, 0},
};

static void config_init(MslCoreMatchConfig* config, const Scenario* scenario) {
  uint8_t player;
  memset(config, 0, sizeof(*config));
  config->stage_id = scenario->stage_id;
  config->frame_id = -123;
  config->frame_pre_random_seed = 1;
  config->initial_random_seed = 1;
  config->match_damage_ratio = 1.0F;
  config->num_players = scenario->num_players;
  config->stock_count = 4;
  config->online_fnmsubs_zero = 1;
  config->brawl_offscreen_damage = 1;
  config->freeze_dead_up_fall_physics = 1;
  config->ucf_cardinals_1_0_enabled = 1;
  config->ucf_shield_sdi_enabled = 1;
  config->ucf_sdi_enabled = 1;
  for (player = 0; player < scenario->num_players; ++player) {
    config->players[player].char_id = scenario->char_ids[player];
  }
}

static int step(MslCoreMatch* match, const MslCoreInput* input) {
  MslCoreStageEvents events = {0};
  return msl_core_match_step(match, input, match->random_seed, &events);
}

static void live_articles(const MslCoreMatch* match, uint32_t* detonations,
                          uint32_t* grapples) {
  MslCoreItem items[MSL_CORE_MAX_ITEMS];
  int i;
  *detonations = 0;
  *grapples = 0;
  msl_core_match_write_items(match, items);
  for (i = 0; i < MSL_CORE_MAX_ITEMS; ++i) {
    if (!items[i].exists) {
      continue;
    }
    if (items[i].type == It_Kind_Ness_PKFlush_Explode) {
      *detonations += 1;
    } else if (items[i].type == It_Kind_Samus_GBeam) {
      *grapples += 1;
    }
  }
}

// used + free is the pool's capacity; it is constant unless the pool grew, and
// growing after the seal aborts the process rather than returning here.
static int check_pool(const char* scenario_name, const char* pool_name, HSD_ObjAllocData* data,
                      uint32_t peak) {
  uint32_t capacity;
  uint32_t reserved;
  data = HSD_ObjAllocResolve(data);
  capacity = data->used + data->free;
  reserved = capacity * REQUIRED_HEADROOM_PERCENT / 100;
  printf("%s %s object_bytes=%u peak=%u capacity=%u\n", scenario_name, pool_name, data->size, peak,
         capacity);
  if (capacity == 0 || peak + reserved > capacity) {
    fprintf(stderr, "%s: %s peak %u leaves under %u%% headroom in a %u slot pool\n", scenario_name,
            pool_name, peak, (unsigned)REQUIRED_HEADROOM_PERCENT, capacity);
    return -1;
  }
  return 0;
}

static int run_scenario(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const Scenario* scenario) {
  MslCoreMatchConfig config;
  MslCoreInput previous = {0};
  MslCoreInput input;
  MslCoreObservation observation = {0};
  uint32_t fobj_peak = 0;
  uint32_t aobj_peak = 0;
  uint32_t gobj_peak = 0;
  uint32_t link_peak = 0;
  uint32_t detonation_peak = 0;
  uint32_t grapple_peak = 0;
  int frame;

  config_init(&config, scenario);
  if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
    fprintf(stderr, "%s: match reset failed\n", scenario->name);
    return -1;
  }

  for (frame = 0; frame < TOTAL_FRAMES; ++frame) {
    uint32_t detonations;
    uint32_t grapples;
    uint32_t fobj_used;
    uint32_t aobj_used;
    uint32_t gobj_used;
    uint32_t link_used;
    uint8_t player;

    memset(&input, 0, sizeof(input));
    if (frame >= CHARGE_START_FRAME) {
      for (player = 0; player < scenario->num_players; ++player) {
        if (scenario->char_ids[player] == CHAR_NESS) {
          // Hold B with a neutral stick and no other button: any extra input
          // cancels the charge before it reaches the automatic detonation.
          input.p[player].buttons = PAD_BUTTON_B;
        } else if (scenario->char_ids[player] == CHAR_SAMUS) {
          // Walk toward the stage centre so the grapple reaches a target and
          // takes the doubled beam-link path, then grab on a fixed cadence.
          float pos_x = observation.slots[player].pos_x;
          int phase = (frame + player * 3) % GRAPPLE_PERIOD;
          if (scenario->grapple_mode == GRAPPLE_AIR && phase == 0) {
            // Jump first so the following Z is the airborne grapple-catch.
            input.p[player].buttons = PAD_BUTTON_X;
          } else if (scenario->grapple_mode == GRAPPLE_AIR
                         ? (phase >= 4 && phase < 4 + GRAPPLE_HOLD)
                         : phase < GRAPPLE_HOLD) {
            input.p[player].buttons = PAD_TRIGGER_Z;
          } else {
            input.p[player].main_x = (int8_t)(pos_x > 0.0F ? -STICK_MAX : STICK_MAX);
          }
        } else {
          // Every other port drives its own neutral special, so the measured
          // burst sits on top of a realistic resident article load rather than
          // on an idle stage.
          input.p[player].buttons = PAD_BUTTON_B;
        }
      }
    }
    if (step(match, &input) != 0) {
      fprintf(stderr, "%s: step failed at frame %d\n", scenario->name, frame);
      return -1;
    }
    if (msl_core_match_write_observation(match, 0, &observation) != 0) {
      fprintf(stderr, "%s: observation failed at frame %d\n", scenario->name, frame);
      return -1;
    }

    // Sample every frame. The article is released a few frames after it is
    // loaded, so end-of-match values miss the burst entirely.
    fobj_used = HSD_ObjAllocResolve(HSD_FObjGetAllocData())->used;
    aobj_used = HSD_ObjAllocResolve(HSD_AObjGetAllocData())->used;
    gobj_used = HSD_ObjAllocResolve(&gobj_alloc_data)->used;
    link_used = HSD_ObjAllocResolve(&item_link_alloc_data)->used;
    live_articles(match, &detonations, &grapples);
    if (fobj_used > fobj_peak) {
      fobj_peak = fobj_used;
    }
    if (aobj_used > aobj_peak) {
      aobj_peak = aobj_used;
    }
    if (gobj_used > gobj_peak) {
      gobj_peak = gobj_used;
    }
    if (link_used > link_peak) {
      link_peak = link_used;
    }
    if (detonations > detonation_peak) {
      detonation_peak = detonations;
    }
    if (grapples > grapple_peak) {
      grapple_peak = grapples;
    }
  }

  printf("%s detonations=%u grapples=%u\n", scenario->name, detonation_peak, grapple_peak);
  if (detonation_peak < scenario->expected_detonations) {
    fprintf(stderr,
            "%s: saw %u concurrent PK Flash detonations, expected %u; the scripted inputs no "
            "longer reach the article burst this test exists to cover\n",
            scenario->name, detonation_peak, scenario->expected_detonations);
    return -1;
  }
  if (grapple_peak < scenario->expected_grapples) {
    fprintf(stderr,
            "%s: saw %u concurrent grapple beams, expected %u; the scripted inputs no longer "
            "reach the article burst this test exists to cover\n",
            scenario->name, grapple_peak, scenario->expected_grapples);
    return -1;
  }
  if (check_pool(scenario->name, "fobj", HSD_FObjGetAllocData(), fobj_peak) != 0) {
    return -1;
  }
  // The same burst takes one AObj per animated joint and one GObj per beam
  // link, so those reserves have to clear it too. The class mem-piece
  // allocator is the fourth consumer; it has no pool record to sample, so
  // reaching this line without aborting in hsdAllocMemPiece is its assertion.
  if (check_pool(scenario->name, "aobj", HSD_AObjGetAllocData(), aobj_peak) != 0) {
    return -1;
  }
  if (check_pool(scenario->name, "gobj", &gobj_alloc_data, gobj_peak) != 0) {
    return -1;
  }
  // The grapple beam is an ItemLink chain, and four ports sat at 150 of a 151
  // link reserve before it was scaled, so this pool needs an explicit bar.
  if (check_pool(scenario->name, "item_link", &item_link_alloc_data, link_peak) != 0) {
    return -1;
  }
  return 0;
}

int main(int argc, char** argv) {
  MslCoreGameData* game_data = NULL;
  MslCoreMatch* match = NULL;
  size_t i;
  int result = 1;

  if (argc != 2) {
    fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
    return 2;
  }
  game_data = calloc(1, sizeof(*game_data));
  match = calloc(1, sizeof(*match));
  if (game_data == NULL || match == NULL || msl_core_game_data_init(game_data, argv[1]) != 0 ||
      msl_core_match_storage_init(match) != 0) {
    fprintf(stderr, "setup failed\n");
    goto done;
  }
  for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
    if (run_scenario(match, game_data, &scenarios[i]) != 0) {
      goto done;
    }
  }
  result = 0;

done:
  free(match);
  free(game_data);
  return result;
}
