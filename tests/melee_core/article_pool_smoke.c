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
#include <baselib/robj.h>
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
  CHAR_LINK = 6,
  CHAR_YOUNG_LINK = 20,
  CHAR_PIKACHU = 12,
  // Thunder cadence for Pikachu ports. The article count per fighter is fixed,
  // so any cadence that keeps four ports overlapping reaches the same peak.
  THUNDER_PERIOD = 30,
  THUNDER_HOLD = 3,
  // Frames each Link move is held; the article overlap needs a long slot.
  LINK_SLOT = 40,
  CHAR_POPO = 10,
  // Ice Climbers ports walk the full move set on this cadence; the live
  // animation-track high water needs both climbers churning through wide
  // animations, which any single held move never reaches.
  ICS_SLOT = 40,
  // Ness's steered PK Thunder: cast on this cadence, then rotate the stick
  // so the head stays airborne and the whole seven-piece trail persists.
  PK_THUNDER_PERIOD = 220,
  PK_THUNDER_CAST = 30,
  PK_THUNDER_STEER_END = 190,
  // Full stick deflection on the source input wire.
  STICK_MAX = 80,
};

// Samus's grapple has two reachable shapes and the airborne one is deeper:
// four ports measured 452 live FObjs in the air against 362 on the ground.
typedef enum GrappleMode { GRAPPLE_GROUND, GRAPPLE_AIR } GrappleMode;

// Ness ports either charge PK Flash to its automatic detonation or cast and
// steer PK Thunder, whose head plus trail is seven live items at once.
typedef enum NessMode { NESS_FLASH, NESS_THUNDER } NessMode;

// Ice Climbers ports either walk the whole move set -- the animation-track
// high water needs both climbers churning through wide animations -- or
// alternate Ice Shot and Blizzard to stack the most concurrent items.
typedef enum IcsMode { ICS_CYCLE, ICS_STORM } IcsMode;

typedef struct Scenario {
  const char* name;
  uint8_t stage_id;
  uint8_t num_players;
  uint8_t char_ids[MSL_CORE_MAX_PLAYERS];
  GrappleMode grapple_mode;
  NessMode ness_mode;
  IcsMode ics_mode;
  // Frames to run; 0 means TOTAL_FRAMES.
  uint32_t frames;
  // Concurrent PK Flash detonations the scenario must actually produce.
  uint32_t expected_detonations;
  // Concurrent live grapple beams the scenario must actually produce.
  uint32_t expected_grapples;
  // Live RObjs / items the scenario must actually reach. The item figure is
  // read from the pool rather than the observation, which stops at fifteen.
  uint32_t expected_robjs;
  uint32_t expected_items;
  // Concurrent Blizzard puffs / PK Thunder pieces the scenario must actually
  // produce, read from the published observation window.
  uint32_t expected_blizzards;
  uint32_t expected_thunder;
} Scenario;

static const Scenario scenarios[] = {
    // Worst FObj case for a single fighter: every port charges from the same
    // frame, so the detonations and their article loads overlap.
    {"four-ness-dreamland", 28, 4, {8, 8, 8, 8}, GRAPPLE_GROUND, NESS_FLASH, ICS_CYCLE,
     0, 4, 0, 0, 0, 0, 0},
    // Worst case for every other pool. Four airborne grapple-catches overran
    // the AObj, GObj, and class mem-piece reserves, each of which was a flat
    // constant sized against one port.
    {"four-samus-air-fd", 32, 4, {13, 13, 13, 13}, GRAPPLE_AIR, NESS_FLASH, ICS_CYCLE, 0,
     0, 2, 0, 0, 0, 0},
    // Mixed lineup: Ness and Samus bursts overlap, so the reserve has to
    // cover their sum rather than the larger of the two. This scenario does
    // not abort under a per-fighter maximum -- it was thin there rather than
    // broken -- so the headroom bar below is what guards the sum form.
    {"ness-samus-dreamland", 28, 4, {8, 8, 13, 13}, GRAPPLE_GROUND, NESS_FLASH,
     ICS_CYCLE, 0, 2, 2, 0, 0, 0, 0},
    // The pairing reported from RL: Ness's detonation lands on top of Link's
    // resident article graph, the heaviest in the supported roster after Ness.
    {"ness-link-dreamland", 28, 2, {8, 6, 0, 0}, GRAPPLE_GROUND, NESS_FLASH, ICS_CYCLE,
     0, 1, 0, 0, 0, 0, 0},
    // Link and Young Link carry twice the RObjs of any other fighter, so four
    // Link ports sat on exactly the flat 16-slot reserve with nothing spare.
    {"four-link-fd", 32, 4, {6, 6, 6, 6}, GRAPPLE_GROUND, NESS_FLASH, ICS_CYCLE,
     0, 0, 0, 16, 0, 0, 0},
    // Pikachu's Thunder is four articles per fighter, so a four-Pikachu mirror
    // wants sixteen live items against a pool that was sized from the
    // fifteen-wide observation array and aborted on the sixteenth.
    {"four-pikachu-fd", 32, 4, {12, 12, 12, 12}, GRAPPLE_GROUND, NESS_FLASH, ICS_CYCLE,
     0, 0, 0, 0, 16, 0, 0},
    // Eight climbers churning through the full move set keep more live
    // animation tracks attached than four of any other fighter: 1058 measured
    // post-compaction against the former flat 1024-track capacity, which
    // asserted in allocate_tracks. The track arena is a bump cursor with no
    // live-count to sample, so completing the cycle without that assert is
    // the check; the Blizzard bar proves the ports actually run their moves.
    {"four-ics-fd", 32, 4, {10, 10, 10, 10}, GRAPPLE_GROUND, NESS_FLASH, ICS_CYCLE,
     1380, 0, 0, 0, 0, 6, 0},
    // The second pairing reported from RL: a steered PK Thunder is seven live
    // items, an Ice Climbers port holds five Blizzard puffs plus ice blocks,
    // and Yoshi's Story's own Shy Guys add waves of three to five on top.
    // Under the former flat eight-per-port item reserve the two ports bought
    // sixteen slots and the seventeenth spawn aborted in
    // itClimbersBlizzard_Spawn.
    {"ness-ics-yoshis", 8, 2, {8, 10, 0, 0}, GRAPPLE_GROUND, NESS_THUNDER,
     ICS_STORM, 700, 0, 0, 0, 15, 4, 6},
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
                          uint32_t* grapples, uint32_t* blizzards,
                          uint32_t* thunder) {
  MslCoreItem items[MSL_CORE_MAX_ITEMS];
  int i;
  *detonations = 0;
  *grapples = 0;
  *blizzards = 0;
  *thunder = 0;
  msl_core_match_write_items(match, items);
  for (i = 0; i < MSL_CORE_MAX_ITEMS; ++i) {
    if (!items[i].exists) {
      continue;
    }
    if (items[i].type == It_Kind_Ness_PKFlush_Explode) {
      *detonations += 1;
    } else if (items[i].type == It_Kind_Samus_GBeam) {
      *grapples += 1;
    } else if (items[i].type == It_Kind_IceClimber_Blizzard) {
      *blizzards += 1;
    } else if (items[i].type >= It_Kind_Ness_PKThunder &&
               items[i].type <= It_Kind_Ness_PKThunder4) {
      *thunder += 1;
    }
  }
}

// item_alloc_data is file-static in item.c, so locate its pool by object size
// in this match's allocator context instead of by symbol.
static HSD_ObjAllocData* pool_by_size(MslCoreMatch* match, u32 object_bytes) {
  u32 i;
  for (i = 0; i < match->objalloc.count; ++i) {
    if (match->objalloc.values[i].size == object_bytes) {
      return &match->objalloc.values[i];
    }
  }
  return NULL;
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
  uint32_t robj_peak = 0;
  uint32_t item_peak = 0;
  HSD_ObjAllocData* item_pool;
  uint32_t detonation_peak = 0;
  uint32_t grapple_peak = 0;
  uint32_t blizzard_peak = 0;
  uint32_t thunder_peak = 0;
  int total_frames =
      scenario->frames != 0 ? (int)scenario->frames : TOTAL_FRAMES;
  int frame;

  config_init(&config, scenario);
  if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
    fprintf(stderr, "%s: match reset failed\n", scenario->name);
    return -1;
  }

  for (frame = 0; frame < total_frames; ++frame) {
    uint32_t detonations;
    uint32_t grapples;
    uint32_t blizzards;
    uint32_t thunder;
    uint32_t fobj_used;
    uint32_t aobj_used;
    uint32_t gobj_used;
    uint32_t link_used;
    uint32_t robj_used;
    uint32_t item_used;
    uint8_t player;

    memset(&input, 0, sizeof(input));
    if (frame >= CHARGE_START_FRAME) {
      for (player = 0; player < scenario->num_players; ++player) {
        if (scenario->char_ids[player] == CHAR_NESS &&
            scenario->ness_mode == NESS_THUNDER) {
          // Cast PK Thunder, then rotate the stick through eight directions
          // so the head keeps flying and the whole trail stays live. The
          // rotation is open loop; the liveness bar below is what proves the
          // trail actually persisted.
          static const int8_t steer_x[8] = {0, 56, 80, 56, 0, -56, -80, -56};
          static const int8_t steer_y[8] = {80, 56, 0, -56, -80, -56, 0, 56};
          float pos_x = observation.slots[player].pos_x;
          int phase = (frame - CHARGE_START_FRAME) % PK_THUNDER_PERIOD;
          if (phase < 3) {
            // PK Fire toward the centre keeps one more item live beside the
            // trail, matching what an RL policy freely mixes in.
            input.p[player].buttons = PAD_BUTTON_B;
            input.p[player].main_x =
                (int8_t)(pos_x > 0.0F ? -STICK_MAX : STICK_MAX);
          } else if (phase >= PK_THUNDER_CAST && phase < PK_THUNDER_CAST + 3) {
            input.p[player].buttons = PAD_BUTTON_B;
            input.p[player].main_y = STICK_MAX;
          } else if (phase >= PK_THUNDER_CAST + 4 &&
                     phase < PK_THUNDER_STEER_END) {
            int dir = ((phase - PK_THUNDER_CAST - 4) / 9) % 8;
            input.p[player].main_x = steer_x[dir];
            input.p[player].main_y = steer_y[dir];
          }
        } else if (scenario->char_ids[player] == CHAR_NESS) {
          // Hold B with a neutral stick and no other button: any extra input
          // cancels the charge before it reaches the automatic detonation.
          input.p[player].buttons = PAD_BUTTON_B;
        } else if (scenario->char_ids[player] == CHAR_POPO) {
          float pos_x = observation.slots[player].pos_x;
          int8_t toward = (int8_t)(pos_x > 0.0F ? -STICK_MAX : STICK_MAX);
          if (scenario->ics_mode == ICS_STORM) {
            // Alternate Ice Shot and Blizzard: one port's densest item mix,
            // an ice block per climber plus the puff stream.
            int slot = ((frame - CHARGE_START_FRAME) / (ICS_SLOT + 5)) % 2;
            int phase = (frame - CHARGE_START_FRAME) % (ICS_SLOT + 5);
            if (phase < 4) {
              input.p[player].buttons = PAD_BUTTON_B;
              if (slot == 0) {
                input.p[player].main_y = -STICK_MAX;
              } else {
                input.p[player].main_x = toward;
              }
            }
          } else {
            // Walk the full move set so both climbers keep churning through
            // wide animations; a single held move never reaches the live
            // track high water.
            int slot = ((frame - CHARGE_START_FRAME) / ICS_SLOT) % 10;
            int phase = (frame - CHARGE_START_FRAME) % ICS_SLOT;
            if (phase < 4) {
              switch (slot) {
                case 0: /* ice shot */
                  input.p[player].buttons = PAD_BUTTON_B;
                  break;
                case 1: /* squall hammer */
                  input.p[player].buttons = PAD_BUTTON_B;
                  input.p[player].main_x = toward;
                  break;
                case 2: /* belay */
                  input.p[player].buttons = PAD_BUTTON_B;
                  input.p[player].main_y = STICK_MAX;
                  break;
                case 3: /* blizzard */
                  input.p[player].buttons = PAD_BUTTON_B;
                  input.p[player].main_y = -STICK_MAX;
                  break;
                case 4: /* grab */
                  input.p[player].buttons = PAD_TRIGGER_Z;
                  break;
                case 5: /* jump */
                  input.p[player].buttons = PAD_BUTTON_X;
                  break;
                case 6: /* forward smash */
                  input.p[player].c_x = STICK_MAX;
                  break;
                case 7: /* up smash */
                  input.p[player].c_y = STICK_MAX;
                  break;
                case 8: /* down smash */
                  input.p[player].c_y = -STICK_MAX;
                  break;
                default: /* second blizzard */
                  input.p[player].buttons = PAD_BUTTON_B;
                  input.p[player].main_y = -STICK_MAX;
                  break;
              }
            } else if (slot == 4 && phase % 12 < 2) {
              input.p[player].buttons = PAD_TRIGGER_Z;
            } else if (phase > 8) {
              input.p[player].main_x = toward;
            }
          }
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
        } else if (scenario->char_ids[player] == CHAR_PIKACHU) {
          // Down-B is Thunder; up-B is Quick Attack and spawns nothing.
          if ((frame + player) % THUNDER_PERIOD < THUNDER_HOLD) {
            input.p[player].buttons = PAD_BUTTON_B;
            input.p[player].main_y = -STICK_MAX;
          }
        } else if (scenario->char_ids[player] == CHAR_LINK ||
                   scenario->char_ids[player] == CHAR_YOUNG_LINK) {
          // The hookshot is a tether, so these ports need the grapple cadence
          // as well as their specials. Hold each move for a full slot: the
          // RObj high water needs the articles to overlap, which a fast cycle
          // never reaches.
          int slot = ((frame - CHARGE_START_FRAME) / LINK_SLOT) % 6;
          int phase = (frame - CHARGE_START_FRAME) % LINK_SLOT;
          if (phase < 4) {
            switch (slot) {
              case 0: /* bow */
                input.p[player].buttons = PAD_BUTTON_B;
                break;
              case 1: /* boomerang */
                input.p[player].buttons = PAD_BUTTON_B;
                input.p[player].main_x = STICK_MAX;
                break;
              case 2: /* spin attack */
                input.p[player].buttons = PAD_BUTTON_B;
                input.p[player].main_y = STICK_MAX;
                break;
              case 3: /* bomb pull */
                input.p[player].buttons = PAD_BUTTON_B;
                input.p[player].main_y = -STICK_MAX;
                break;
              case 4: /* ground hookshot */
                input.p[player].buttons = PAD_TRIGGER_Z;
                break;
              default: /* jump, then airborne hookshot below */
                input.p[player].buttons = PAD_BUTTON_X;
                break;
            }
          } else if (slot == 5 && (phase == 6 || phase == 7 || phase == 20 || phase == 21)) {
            input.p[player].buttons = PAD_TRIGGER_Z;
          } else if (slot == 4 && phase % 12 < 2) {
            input.p[player].buttons = PAD_TRIGGER_Z;
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
    robj_used = HSD_ObjAllocResolve(HSD_RObjGetAllocData())->used;
    item_pool = pool_by_size(match, (u32) sizeof(Item));
    item_used = item_pool != NULL ? item_pool->used : 0;
    live_articles(match, &detonations, &grapples, &blizzards, &thunder);
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
    if (robj_used > robj_peak) {
      robj_peak = robj_used;
    }
    if (item_used > item_peak) {
      item_peak = item_used;
    }
    if (detonations > detonation_peak) {
      detonation_peak = detonations;
    }
    if (grapples > grapple_peak) {
      grapple_peak = grapples;
    }
    if (blizzards > blizzard_peak) {
      blizzard_peak = blizzards;
    }
    if (thunder > thunder_peak) {
      thunder_peak = thunder;
    }
  }

  printf("%s detonations=%u grapples=%u robjs=%u items=%u blizzards=%u thunder=%u\n",
         scenario->name, detonation_peak, grapple_peak, robj_peak, item_peak, blizzard_peak,
         thunder_peak);
  if (detonation_peak < scenario->expected_detonations) {
    fprintf(stderr,
            "%s: saw %u concurrent PK Flash detonations, expected %u; the scripted inputs no "
            "longer reach the article burst this test exists to cover\n",
            scenario->name, detonation_peak, scenario->expected_detonations);
    return -1;
  }
  if (robj_peak < scenario->expected_robjs) {
    fprintf(stderr,
            "%s: saw %u live RObjs, expected %u; the scripted inputs no longer reach the "
            "state this test exists to cover\n",
            scenario->name, robj_peak, scenario->expected_robjs);
    return -1;
  }
  if (item_peak < scenario->expected_items) {
    fprintf(stderr,
            "%s: saw %u live items, expected %u; the scripted inputs no longer reach the "
            "state this test exists to cover\n",
            scenario->name, item_peak, scenario->expected_items);
    return -1;
  }
  if (grapple_peak < scenario->expected_grapples) {
    fprintf(stderr,
            "%s: saw %u concurrent grapple beams, expected %u; the scripted inputs no longer "
            "reach the article burst this test exists to cover\n",
            scenario->name, grapple_peak, scenario->expected_grapples);
    return -1;
  }
  if (blizzard_peak < scenario->expected_blizzards) {
    fprintf(stderr,
            "%s: saw %u concurrent Blizzard puffs, expected %u; the scripted inputs no longer "
            "reach the state this test exists to cover\n",
            scenario->name, blizzard_peak, scenario->expected_blizzards);
    return -1;
  }
  if (thunder_peak < scenario->expected_thunder) {
    fprintf(stderr,
            "%s: saw %u concurrent PK Thunder pieces, expected %u; the scripted inputs no "
            "longer reach the state this test exists to cover\n",
            scenario->name, thunder_peak, scenario->expected_thunder);
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
  if (check_pool(scenario->name, "robj", HSD_RObjGetAllocData(), robj_peak) != 0) {
    return -1;
  }
  item_pool = pool_by_size(match, (u32) sizeof(Item));
  if (item_pool == NULL) {
    fprintf(stderr, "%s: no item pool in this match\n", scenario->name);
    return -1;
  }
  if (check_pool(scenario->name, "item", item_pool, item_peak) != 0) {
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
