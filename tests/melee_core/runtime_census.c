#define _GNU_SOURCE

#include "runtime/context.h"
#include "runtime/savestate.h"
#include "runtime/scalar.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/pad.h>
#include <baselib/class.h>
#include <baselib/gobjproc.h>
#include <baselib/jobj.h>
#include <baselib/mtx.h>
#include <baselib/objalloc.h>

static const char* const relocation_type_names[] = {
    "RAW",
    "POINTER_ARRAY",
#define MSL_RELOC_TYPE(id, type) #id,
#include "runtime/relocation_types.def"
#undef MSL_RELOC_TYPE
};

static const char* relocation_type_name(uint32_t type) {
  if (type < sizeof(relocation_type_names) / sizeof(relocation_type_names[0])) {
    return relocation_type_names[type];
  }
  return "UNKNOWN";
}

typedef struct PoolHighWater {
  uint32_t object_bytes;
  uint32_t used;
  uint32_t free;
  uint32_t peak;
  uint32_t reloc_type;
} PoolHighWater;

static void config_init(MslCoreMatchConfig* config, uint8_t stage, uint8_t character,
                        uint8_t player_count) {
  uint8_t player;
  memset(config, 0, sizeof(*config));
  config->stage_id = stage;
  config->frame_id = -123;
  config->frame_pre_random_seed = 1;
  config->initial_random_seed = 1;
  config->match_damage_ratio = 1.0F;
  config->num_players = player_count;
  config->is_teams = player_count > 2;
  config->stock_count = 4;
  config->online_fnmsubs_zero = 1;
  config->brawl_offscreen_damage = 1;
  config->freeze_dead_up_fall_physics = 1;
  config->whispy_dead_fighter_fix = 1;
  config->ucf_cardinals_1_0_enabled = 1;
  config->ucf_shield_sdi_enabled = 1;
  config->ucf_sdi_enabled = 1;
  config->ucf_shield_drop_084_enabled = 1;
  for (player = 0; player < player_count; ++player) {
    config->players[player].char_id = character;
    config->players[player].team_id = player == 1 || player == 2;
  }
}

static int step(MslCoreMatch* match, const MslCoreInput* input) {
  MslCoreStageEvents events = {0};
  return msl_core_match_step(match, input, match->random_seed, &events);
}

static int materialize_fighter_scales(MslCoreMatch* match) {
  size_t used = match->memory.used;
  size_t allocations = match->memory.allocation_count;
  uint16_t i;
  HSD_ObjAllocData* vectors = HSD_ObjAllocResolve(HSD_VecGetAllocData());

  // Exercise the maximum simultaneous demand, independent of which animation
  // happens to reach each joint. Non-classical scale always owns one Vec;
  // classical scale can only inherit one or release it. The next case resets
  // the match, so these test-only flag changes cannot affect gameplay checks.
  for (i = 0; i < match->fighter_pose.joint_count; ++i) {
    HSD_JObj* joint = match->fighter_pose.joints[i].joint;
    joint->flags &= ~JOBJ_CLASSICAL_SCALE;
    HSD_JObjMakeMatrix(joint);
    if (joint->scl == NULL) {
      return -1;
    }
  }
  if (vectors->free < 128 || match->memory.used != used ||
      match->memory.allocation_count != allocations) {
    fprintf(stderr, "fighter scale reserve exhausted: stage=%u character=%u players=%u\n",
            match->config.stage_id, match->config.players[0].char_id,
            match->config.num_players);
    return -1;
  }
  return 0;
}

static const char* symbol_name(const void* address, char* fallback, size_t fallback_size) {
  Dl_info info;
  if (address != NULL && dladdr(address, &info) != 0 && info.dli_sname != NULL) {
    return info.dli_sname;
  }
  snprintf(fallback, fallback_size, "%p", address);
  return fallback;
}

static void print_callbacks(const MslCoreMatch* match) {
  int priority;
  for (priority = 0; priority <= match->gobj.init_data.gproc_pri_max; ++priority) {
    const HSD_GObjProc* proc = match->gobj.proc_heads[priority];
    while (proc != NULL) {
      char fallback[32];
      printf("callback priority=%d classifier=%u owner_link=%u name=%s\n", priority,
             proc->gobj->classifier, proc->gobj->p_link,
             symbol_name((const void*)proc->on_invoke, fallback, sizeof(fallback)));
      proc = proc->next;
    }
  }
}

static void print_pools(const MslCoreMatch* match) {
  uint32_t i;
  for (i = 0; i < match->objalloc.count; ++i) {
    const HSD_ObjAllocData* pool = &match->objalloc.values[i];
    char fallback[32];
    printf(
        "pool index=%u key=%s object_bytes=%u used=%u free=%u peak=%u "
        "reloc_type=%u:%s reloc_count=%u reloc_stride=%u\n",
        i, symbol_name(match->objalloc.keys[i], fallback, sizeof(fallback)), pool->size, pool->used,
        pool->free, pool->peak, match->objalloc.reloc_types[i],
        relocation_type_name(match->objalloc.reloc_types[i]), match->objalloc.reloc_counts[i],
        match->objalloc.reloc_strides[i]);
  }
}

static void capture_pool_high_water(const MslCoreMatch* match, PoolHighWater* high_water,
                                    uint32_t* high_water_count) {
  uint32_t i;
  if (match->objalloc.count > *high_water_count) {
    *high_water_count = match->objalloc.count;
  }
  for (i = 0; i < match->objalloc.count; ++i) {
    const HSD_ObjAllocData* pool = &match->objalloc.values[i];
    PoolHighWater* maximum = &high_water[i];
    maximum->object_bytes = pool->size;
    maximum->reloc_type = match->objalloc.reloc_types[i];
    if (pool->used > maximum->used) {
      maximum->used = pool->used;
    }
    if (pool->free > maximum->free) {
      maximum->free = pool->free;
    }
    if (pool->peak > maximum->peak) {
      maximum->peak = pool->peak;
    }
  }
}

static void print_pool_high_water(const PoolHighWater* high_water, uint32_t high_water_count) {
  uint32_t i;
  size_t total_bytes = 0;
  for (i = 0; i < high_water_count; ++i) {
    const PoolHighWater* pool = &high_water[i];
    size_t bytes = (size_t)pool->object_bytes * (pool->used + pool->free);
    total_bytes += bytes;
    printf(
        "pool_maximum index=%u object_bytes=%u used=%u free=%u "
        "peak=%u reserved_bytes=%zu reloc_type=%u:%s\n",
        i, pool->object_bytes, pool->used, pool->free, pool->peak, bytes, pool->reloc_type,
        relocation_type_name(pool->reloc_type));
  }
  printf("pool_maximum total_reserved_bytes=%zu\n", total_bytes);
}

static void print_relocation_storage(const MslCoreMatch* match) {
  typedef struct RawSize {
    uint32_t stride;
    uint32_t records;
    uint32_t objects;
    size_t bytes;
  } RawSize;
  size_t bytes[MSL_RELOC_TYPE_COUNT] = {0};
  uint32_t counts[MSL_RELOC_TYPE_COUNT] = {0};
  RawSize raw_sizes[256] = {{0}};
  uint32_t raw_size_count = 0;
  const MslRelocRecord* records = msl_reloc_records(match);
  uint32_t i;
  for (i = 0; i < match->relocation_count; ++i) {
    const MslRelocRecord* record = &records[i];
    if (record->type < MSL_RELOC_TYPE_COUNT) {
      bytes[record->type] += (size_t)record->count * record->stride;
      ++counts[record->type];
    }
    if (record->type == MSL_RELOC_RAW) {
      uint32_t size_index;
      for (size_index = 0; size_index < raw_size_count; ++size_index) {
        if (raw_sizes[size_index].stride == record->stride) {
          break;
        }
      }
      if (size_index == raw_size_count && raw_size_count < 256) {
        raw_sizes[raw_size_count++].stride = record->stride;
      }
      if (size_index < raw_size_count) {
        ++raw_sizes[size_index].records;
        raw_sizes[size_index].objects += record->count;
        raw_sizes[size_index].bytes += (size_t)record->count * record->stride;
      }
    }
  }
  for (i = 0; i < MSL_RELOC_TYPE_COUNT; ++i) {
    if (counts[i] != 0) {
      printf("relocation_storage type=%u:%s records=%u bytes=%zu\n", i,
             relocation_type_name(i), counts[i], bytes[i]);
    }
  }
  for (i = 0; i < raw_size_count; ++i) {
    printf("raw_storage stride=%u records=%u objects=%u bytes=%zu\n",
           raw_sizes[i].stride, raw_sizes[i].records, raw_sizes[i].objects,
           raw_sizes[i].bytes);
  }
}

static void print_class_storage(const MslCoreMatch* match) {
  int i;
  for (i = 0; i < match->class_state.nb_memory_list; ++i) {
    const HSD_MemoryEntry* entry = match->class_state.memory_list[i];
    if (entry != NULL) {
      printf("class_storage size=%u allocated=%u free=%u active=%u\n", entry->size,
             entry->nb_alloc, entry->nb_free, entry->nb_alloc - entry->nb_free);
    }
  }
}

int main(int argc, char** argv) {
  static const uint8_t characters[] = {
      1, 22, 18, 2, 7, 19, 15, 9, 17, 0, 21, 13, 10, 12, 3, 25, 14, 5,
      8, 6, 20, 16, 24, 26, 23, 4,
  };
  static const uint8_t stages[] = {32, 31, 3, 2, 8, 28};
  static const uint8_t player_counts[] = {2, 3, 4};
  MslCoreGameData* game_data = NULL;
  MslCoreMatch* match = NULL;
  MslCoreMatchConfig config;
  MslCoreInput previous = {0};
  MslCoreInput input = {0};
  size_t max_arena_used = 0;
  size_t max_allocations = 0;
  uint32_t max_relocations = 0;
  uint16_t max_pose_joints = 0;
  uint16_t max_pose_tracks = 0;
  uint8_t max_stage = 0;
  uint8_t max_character = 0;
  uint8_t max_player_count = 0;
  PoolHighWater pool_high_water[HSD_OBJALLOC_CONTEXT_CAPACITY] = {{0}};
  uint32_t pool_high_water_count = 0;
  size_t allocation_before_steps;
  size_t used_before_steps;
  size_t save_size;
  size_t header_size;
  size_t i;
  size_t j;
  int frame;
  int result = 1;

  if (argc != 2) {
    fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
    return 2;
  }
  game_data = calloc(1, sizeof(*game_data));
  match = calloc(1, sizeof(*match));
  if (game_data == NULL || match == NULL || msl_core_game_data_init(game_data, argv[1]) != 0 ||
      msl_core_match_storage_init(match) != 0) {
    goto done;
  }
  for (i = 0; i < sizeof(player_counts); ++i) {
    size_t stage_index;
    for (stage_index = 0; stage_index < sizeof(stages); ++stage_index) {
      for (j = 0; j < sizeof(characters); ++j) {
        config_init(&config, stages[stage_index], characters[j], player_counts[i]);
        if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
          goto done;
        }
        capture_pool_high_water(match, pool_high_water, &pool_high_water_count);
        if (match->memory.used > max_arena_used) {
          max_arena_used = match->memory.used;
          max_stage = stages[stage_index];
          max_character = characters[j];
          max_player_count = player_counts[i];
        }
        if (match->memory.allocation_count > max_allocations) {
          max_allocations = match->memory.allocation_count;
        }
        if (match->relocation_count > max_relocations) {
          max_relocations = match->relocation_count;
        }
        if (match->fighter_pose.joint_count > max_pose_joints) {
          max_pose_joints = match->fighter_pose.joint_count;
        }
        if (match->fighter_pose.track_used > max_pose_tracks) {
          max_pose_tracks = match->fighter_pose.track_used;
        }
        if (materialize_fighter_scales(match) != 0) {
          goto done;
        }
      }
    }
  }

  config_init(&config, max_stage, max_character, max_player_count);
  if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
    goto done;
  }
  print_relocation_storage(match);

  config_init(&config, 32, 9, 2);
  if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
    goto done;
  }
  allocation_before_steps = match->memory.allocation_count;
  used_before_steps = match->memory.used;
  for (frame = 0; frame < 123; ++frame) {
    if (step(match, &input) != 0) {
      goto done;
    }
  }
  input.p[0].buttons = PAD_BUTTON_B;
  input.p[0].main_y = -80;
  for (frame = 0; frame < 3; ++frame) {
    if (step(match, &input) != 0) {
      goto done;
    }
  }
  memset(&input, 0, sizeof(input));
  for (frame = 0; frame < 180; ++frame) {
    if (step(match, &input) != 0) {
      goto done;
    }
  }
  capture_pool_high_water(match, pool_high_water, &pool_high_water_count);
  print_class_storage(match);
  save_size = msl_core_match_save_size(match);
  header_size = save_size - sizeof(*match) - match->memory.used;

  printf(
      "layout match_bytes=%zu game_data_bytes=%zu source_match_bytes=%zu "
      "relocation_registry_bytes=%zu memory_context_bytes=%zu\n",
      sizeof(*match), sizeof(*game_data), sizeof(match->source),
      msl_reloc_resident_bytes(match),
      sizeof(match->memory));
  printf(
      "game_data arena_reserved=%zu arena_used=%zu allocations=%zu "
      "native_dat_reserved=%u native_dat_used=%zu archive_cache=%u\n",
      game_data->memory.capacity, game_data->memory.used, game_data->memory.allocation_count,
      MSL_NATIVE_DAT_ARENA_BYTES, game_data->native_dat.arena_used,
      game_data->native_dat.archive_cache_count);
  printf(
      "match maximum_arena_reserved=%zu maximum_arena_used=%zu "
      "maximum_allocations=%zu maximum_relocations=%u stage=%u "
      "character=%u players=%u\n",
      match->memory.capacity, max_arena_used, max_allocations, max_relocations, max_stage,
      max_character, max_player_count);
  printf("fighter_pose maximum_joints=%u maximum_tracks=%u\n",
         max_pose_joints, max_pose_tracks);
  printf(
      "runtime_allocation_lock before_used=%zu after_used=%zu "
      "before_allocations=%zu after_allocations=%zu\n",
      used_before_steps, match->memory.used, allocation_before_steps,
      match->memory.allocation_count);
  printf(
      "savestate total_bytes=%zu header_bytes=%zu match_bytes=%zu "
      "arena_bytes=%zu\n",
      save_size, header_size, sizeof(*match), match->memory.used);
  printf("observation per_frame_bytes=%zu history=%d per_env_history_bytes=%zu\n",
         sizeof(MslCoreObservation) + sizeof(MslCoreTerminal), 128,
         (sizeof(MslCoreObservation) + sizeof(MslCoreTerminal)) * 128);
  {
    HSD_ObjAllocData* gobjs = HSD_ObjAllocResolve(&match->gobj.gobj_alloc);
    HSD_ObjAllocData* procs = HSD_ObjAllocResolve(&match->gobj.proc_alloc);
    printf(
        "scheduler gobjs_used=%u gobjs_peak=%u procs_used=%u "
        "procs_peak=%u\n",
        gobjs->used, gobjs->peak, procs->used, procs->peak);
  }
  print_pools(match);
  print_pool_high_water(pool_high_water, pool_high_water_count);
  print_callbacks(match);
  result = 0;

done:
  if (match != NULL && match->memory.arena != NULL) {
    msl_core_match_destroy(match);
  }
  if (game_data != NULL && game_data->memory.arena != NULL) {
    msl_core_game_data_deinit(game_data);
  }
  free(match);
  free(game_data);
  return result;
}
