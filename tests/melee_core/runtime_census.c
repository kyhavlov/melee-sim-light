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
#include <baselib/gobjproc.h>
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

static void config_init(MslCoreMatchConfig* config, uint8_t stage, uint8_t character) {
  memset(config, 0, sizeof(*config));
  config->stage_id = stage;
  config->frame_id = -123;
  config->frame_pre_random_seed = 1;
  config->initial_random_seed = 1;
  config->match_damage_ratio = 1.0F;
  config->num_players = 2;
  config->stock_count = 4;
  config->online_fnmsubs_zero = 1;
  config->brawl_offscreen_damage = 1;
  config->freeze_dead_up_fall_physics = 1;
  config->ucf_cardinals_1_0_enabled = 1;
  config->ucf_shield_sdi_enabled = 1;
  config->ucf_sdi_enabled = 1;
  config->players[0].char_id = character;
  config->players[1].char_id = character;
}

static int step(MslCoreMatch* match, const MslCoreInput* input) {
  MslCoreStageEvents events = {0};
  return msl_core_match_step(match, input, match->random_seed, &events);
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

int main(int argc, char** argv) {
  static const uint8_t characters[] = {1, 22, 18, 2, 7, 19, 15, 9};
  static const uint8_t stages[] = {32, 31, 3, 2, 8, 28};
  MslCoreGameData* game_data = NULL;
  MslCoreMatch* match = NULL;
  MslCoreMatchConfig config;
  MslCoreInput previous = {0};
  MslCoreInput input = {0};
  size_t max_arena_used = 0;
  size_t max_allocations = 0;
  uint32_t max_relocations = 0;
  uint8_t max_stage = 0;
  uint8_t max_character = 0;
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
  for (i = 0; i < sizeof(stages); ++i) {
    for (j = 0; j < sizeof(characters); ++j) {
      config_init(&config, stages[i], characters[j]);
      if (msl_core_match_reset(match, game_data, &config, &previous) != 0) {
        goto done;
      }
      if (match->memory.used > max_arena_used) {
        max_arena_used = match->memory.used;
        max_stage = stages[i];
        max_character = characters[j];
      }
      if (match->memory.allocation_count > max_allocations) {
        max_allocations = match->memory.allocation_count;
      }
      if (match->relocation.count > max_relocations) {
        max_relocations = match->relocation.count;
      }
    }
  }

  config_init(&config, 32, 9);
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
  save_size = msl_core_match_save_size(match);
  header_size = save_size - sizeof(*match) - match->memory.used;

  printf(
      "layout match_bytes=%zu game_data_bytes=%zu source_match_bytes=%zu "
      "relocation_registry_bytes=%zu memory_context_bytes=%zu\n",
      sizeof(*match), sizeof(*game_data), sizeof(match->source), sizeof(match->relocation),
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
      "character=%u\n",
      match->memory.capacity, max_arena_used, max_allocations, max_relocations, max_stage,
      max_character);
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
