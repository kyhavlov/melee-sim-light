#define _GNU_SOURCE

#include "runtime/batch.h"
#include "runtime/benchmark_wire.h"
#include "runtime/subsystem_profile.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#ifdef MSL_CORE_CALLGRIND
#include <valgrind/callgrind.h>
#endif

#ifdef MSL_CORE_GPROF
extern void moncontrol(int mode);
#endif

enum {
  OBSERVATION_HISTORY = 128,
  STARTUP_GROUP_COUNT = 8,
  STARTUP_BASE_FRAMES = 200,
  STARTUP_FRAME_GAP = 100,
};

typedef struct ReplayCase {
  void* mapping;
  size_t mapping_size;
  const MslCoreBenchmarkCaseHeader* header;
  const MslCoreInput* inputs;
} ReplayCase;

typedef struct Workload {
  uint32_t match_count;
  uint32_t output_match_stride;
  uint32_t output_match_offset;
  uint32_t case_count;
  ReplayCase* cases;
  MslCoreMatchConfig* configs;
  MslCoreInput* inputs;
  MslCoreObservation* observations;
  MslCoreTerminal* terminals;
  uint32_t* case_index;
  uint32_t* frame_index;
  uint32_t* source_index;
  uint32_t* copy_destination;
  uint32_t* copy_source;
  uint8_t* viewpoint;
  uint8_t* step_mask;
  uint8_t* reset_mask;
} Workload;

typedef struct RunResult {
  double seconds;
  double cpu_seconds;
  uint64_t cycles;
  uint64_t resets;
} RunResult;

static double seconds_now(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) {
    return 0.0;
  }
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static double cpu_seconds_now(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) {
    return 0.0;
  }
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static int parse_u32(const char* text, uint32_t* output) {
  char* end;
  unsigned long value;
  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || *text == '\0' || *end != '\0' || value == 0 || value > UINT32_MAX) {
    return -1;
  }
  *output = (uint32_t)value;
  return 0;
}

static int append_case(ReplayCase** cases, uint32_t* count, uint32_t* capacity, const char* path) {
  struct stat status;
  MslCoreBenchmarkCaseHeader* header;
  ReplayCase* grown;
  void* mapping;
  size_t expected_size;
  int fd = open(path, O_RDONLY);
  if (fd < 0 || fstat(fd, &status) != 0 ||
      status.st_size < (off_t)sizeof(MslCoreBenchmarkCaseHeader)) {
    fprintf(stderr, "cannot open benchmark case %s: %s\n", path, strerror(errno));
    if (fd >= 0) {
      close(fd);
    }
    return -1;
  }
  mapping = mmap(NULL, (size_t)status.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapping == MAP_FAILED) {
    fprintf(stderr, "cannot map benchmark case %s: %s\n", path, strerror(errno));
    return -1;
  }
  header = mapping;
  expected_size = (size_t)header->header_size + (size_t)header->frame_count * header->input_size;
  if ((size_t)status.st_size < sizeof(*header) ||
      memcmp(header->magic, msl_core_benchmark_case_magic, sizeof(header->magic)) != 0 ||
      header->version != MSL_CORE_BENCHMARK_CASE_VERSION ||
      header->header_size != sizeof(*header) || header->input_size != sizeof(MslCoreInput) ||
      header->frame_count == 0 || expected_size != (size_t)status.st_size) {
    fprintf(stderr, "invalid benchmark case: %s\n", path);
    munmap(mapping, (size_t)status.st_size);
    return -1;
  }
  if (*count == *capacity) {
    uint32_t next_capacity = *capacity == 0 ? 32 : *capacity * 2;
    grown = realloc(*cases, (size_t)next_capacity * sizeof(**cases));
    if (grown == NULL) {
      munmap(mapping, (size_t)status.st_size);
      return -1;
    }
    *cases = grown;
    *capacity = next_capacity;
  }
  (*cases)[*count].mapping = mapping;
  (*cases)[*count].mapping_size = (size_t)status.st_size;
  (*cases)[*count].header = header;
  (*cases)[*count].inputs = (const MslCoreInput*)((const uint8_t*)mapping + header->header_size);
  ++*count;
  return 0;
}

static int load_cases(const char* manifest, ReplayCase** output, uint32_t* output_count) {
  ReplayCase* cases = NULL;
  uint32_t count = 0;
  uint32_t capacity = 0;
  char line[4096];
  FILE* input = fopen(manifest, "r");
  if (input == NULL) {
    fprintf(stderr, "cannot open benchmark manifest %s: %s\n", manifest, strerror(errno));
    return -1;
  }
  while (fgets(line, sizeof(line), input) != NULL) {
    char* separator;
    size_t length;
    if (line[0] == '#') {
      continue;
    }
    separator = strchr(line, '\t');
    if (separator != NULL) {
      *separator = '\0';
    } else {
      length = strlen(line);
      if (length != 0 && line[length - 1] == '\n') {
        line[length - 1] = '\0';
      }
    }
    if (line[0] != '\0' && append_case(&cases, &count, &capacity, line) != 0) {
      fclose(input);
      goto fail;
    }
  }
  if (ferror(input) || fclose(input) != 0 || count == 0) {
    fprintf(stderr, "benchmark manifest has no usable cases\n");
    goto fail;
  }
  *output = cases;
  *output_count = count;
  return 0;

fail:
  while (count != 0) {
    --count;
    munmap(cases[count].mapping, cases[count].mapping_size);
  }
  free(cases);
  return -1;
}

static void unload_cases(ReplayCase* cases, uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    munmap(cases[i].mapping, cases[i].mapping_size);
  }
  free(cases);
}

static int workload_allocate(Workload* workload, ReplayCase* cases, uint32_t case_count,
                             uint32_t match_count, uint32_t output_match_stride,
                             uint32_t output_match_offset, MslCoreObservation* observations,
                             MslCoreTerminal* terminals) {
  memset(workload, 0, sizeof(*workload));
  workload->match_count = match_count;
  workload->output_match_stride = output_match_stride;
  workload->output_match_offset = output_match_offset;
  workload->case_count = case_count;
  workload->cases = cases;
  workload->configs = calloc(match_count, sizeof(*workload->configs));
  workload->inputs = calloc(match_count, sizeof(*workload->inputs));
  workload->observations = observations;
  workload->terminals = terminals;
  workload->case_index = calloc(match_count, sizeof(*workload->case_index));
  workload->frame_index = calloc(match_count, sizeof(*workload->frame_index));
  workload->source_index = calloc(match_count, sizeof(*workload->source_index));
  workload->copy_destination = calloc(match_count, sizeof(*workload->copy_destination));
  workload->copy_source = calloc(match_count, sizeof(*workload->copy_source));
  workload->viewpoint = calloc(match_count, sizeof(*workload->viewpoint));
  workload->step_mask = calloc(match_count, sizeof(*workload->step_mask));
  workload->reset_mask = calloc(match_count, sizeof(*workload->reset_mask));
  return workload->configs != NULL && workload->inputs != NULL && observations != NULL &&
                 terminals != NULL && workload->case_index != NULL &&
                 workload->frame_index != NULL && workload->source_index != NULL &&
                 workload->copy_destination != NULL && workload->copy_source != NULL &&
                 workload->viewpoint != NULL && workload->step_mask != NULL &&
                 workload->reset_mask != NULL
             ? 0
             : -1;
}

static void workload_free(Workload* workload) {
  free(workload->configs);
  free(workload->inputs);
  free(workload->case_index);
  free(workload->frame_index);
  free(workload->source_index);
  free(workload->copy_destination);
  free(workload->copy_source);
  free(workload->viewpoint);
  free(workload->step_mask);
  free(workload->reset_mask);
}

static int workload_reset(MslCoreBatch* batch, Workload* workload) {
  uint32_t i;
  for (i = 0; i < workload->match_count; ++i) {
    uint32_t j;
    uint32_t case_index = (workload->output_match_offset + i) % workload->case_count;
    workload->case_index[i] = case_index;
    workload->frame_index[i] = 0;
    workload->source_index[i] = i;
    for (j = 0; j < i; ++j) {
      if (workload->case_index[j] == case_index) {
        workload->source_index[i] = j;
        break;
      }
    }
    workload->configs[i] = workload->cases[case_index].header->config;
  }
  if (msl_core_batch_reset_matches(batch, workload->configs, sizeof(workload->configs[0]), NULL,
                                   0) != MSL_CORE_OK) {
    return -1;
  }
  return 0;
}

static int workload_preroll(MslCoreBatch* batch, Workload* workload) {
  uint32_t max_frames = STARTUP_BASE_FRAMES +
                        (STARTUP_GROUP_COUNT - 1) * STARTUP_FRAME_GAP;
  uint32_t tick;
  for (tick = 0; tick < max_frames; ++tick) {
    uint32_t i;
    int any_reset = 0;
    memset(workload->step_mask, 0, workload->match_count);
    for (i = 0; i < workload->match_count; ++i) {
      uint32_t target_frames =
          STARTUP_BASE_FRAMES +
          (workload->case_index[i] % STARTUP_GROUP_COUNT) * STARTUP_FRAME_GAP;
      if (workload->source_index[i] == i && tick + target_frames >= max_frames) {
        ReplayCase* replay = &workload->cases[workload->case_index[i]];
        workload->inputs[i] = replay->inputs[workload->frame_index[i]];
        workload->step_mask[i] = 1;
      }
    }
    if (msl_core_batch_step_matches(batch, workload->inputs, sizeof(workload->inputs[0]),
                                    workload->step_mask, sizeof(workload->step_mask[0])) !=
        MSL_CORE_OK) {
      return -1;
    }
    memset(workload->reset_mask, 0, workload->match_count);
    for (i = 0; i < workload->match_count; ++i) {
      ReplayCase* replay;
      if (!workload->step_mask[i]) {
        continue;
      }
      replay = &workload->cases[workload->case_index[i]];
      if (++workload->frame_index[i] == replay->header->frame_count) {
        workload->frame_index[i] = 0;
        workload->reset_mask[i] = 1;
        any_reset = 1;
      }
    }
    if (any_reset && msl_core_batch_reset_matches(
                         batch, workload->configs, sizeof(workload->configs[0]),
                         workload->reset_mask, sizeof(workload->reset_mask[0])) != MSL_CORE_OK) {
      return -1;
    }
  }
  {
    uint32_t copy_count = 0;
    uint32_t i;
    for (i = 0; i < workload->match_count; ++i) {
      if (workload->source_index[i] != i) {
        workload->copy_destination[copy_count] = i;
        workload->copy_source[copy_count] = workload->source_index[i];
        workload->frame_index[i] = workload->frame_index[workload->source_index[i]];
        ++copy_count;
      }
    }
    if (msl_core_batch_copy_matches(batch, batch, workload->copy_destination,
                                    workload->copy_source, copy_count) != MSL_CORE_OK) {
      return -1;
    }
  }
  return 0;
}

static int run_workload(MslCoreBatch* batch, Workload* workload, uint32_t ticks, int write_outputs,
                        RunResult* result) {
  uint64_t resets = 0;
  uint64_t cycle_started;
  unsigned int cycle_aux;
  uint32_t tick;
  double started = seconds_now();
  double cpu_started = cpu_seconds_now();
  cycle_started = __rdtscp(&cycle_aux);
  for (tick = 0; tick < ticks; ++tick) {
    uint32_t i;
    int any_reset = 0;
    for (i = 0; i < workload->match_count; ++i) {
      ReplayCase* replay = &workload->cases[workload->case_index[i]];
      workload->inputs[i] = replay->inputs[workload->frame_index[i]];
    }
    if (msl_core_batch_step_matches(batch, workload->inputs, sizeof(workload->inputs[0]), NULL,
                                    0) != MSL_CORE_OK) {
      return -1;
    }
    if (write_outputs) {
      size_t ring_offset = (size_t)(tick % OBSERVATION_HISTORY) * workload->output_match_stride +
                           workload->output_match_offset;
      if (msl_core_batch_write_observation(
              batch, workload->viewpoint, sizeof(workload->viewpoint[0]),
              workload->observations + ring_offset, sizeof(workload->observations[0]), NULL,
              0) != MSL_CORE_OK ||
          msl_core_batch_write_terminal(batch, workload->terminals + ring_offset,
                                        sizeof(workload->terminals[0]), -1, NULL,
                                        0) != MSL_CORE_OK) {
        return -1;
      }
    }
    memset(workload->reset_mask, 0, workload->match_count);
    for (i = 0; i < workload->match_count; ++i) {
      ReplayCase* replay = &workload->cases[workload->case_index[i]];
      if (++workload->frame_index[i] == replay->header->frame_count) {
        workload->frame_index[i] = 0;
        workload->reset_mask[i] = 1;
        any_reset = 1;
        ++resets;
      }
    }
    if (any_reset && msl_core_batch_reset_matches(
                         batch, workload->configs, sizeof(workload->configs[0]),
                         workload->reset_mask, sizeof(workload->reset_mask[0])) != MSL_CORE_OK) {
      return -1;
    }
  }
  result->cycles = __rdtscp(&cycle_aux) - cycle_started;
  result->cpu_seconds = cpu_seconds_now() - cpu_started;
  result->seconds = seconds_now() - started;
  result->resets = resets;
  return 0;
}

static int run_sharded_pass(const MslCoreGameData* game_data, ReplayCase* cases,
                            uint32_t case_count, uint32_t logical_match_count,
                            uint32_t resident_match_count, uint32_t ticks, uint32_t warmup_ticks,
                            int write_outputs, MslCoreObservation* observations,
                            MslCoreTerminal* terminals, RunResult* total) {
  uint32_t offset;
  total->seconds = 0.0;
  total->cpu_seconds = 0.0;
  total->cycles = 0;
  total->resets = 0;
  for (offset = 0; offset < logical_match_count; offset += resident_match_count) {
    uint32_t count = logical_match_count - offset;
    Workload workload;
    MslCoreBatch* batch = NULL;
    RunResult warmup;
    RunResult measured;
    if (count > resident_match_count) {
      count = resident_match_count;
    }
    if (workload_allocate(&workload, cases, case_count, count, logical_match_count, offset,
                          observations, terminals) != 0 ||
        msl_core_batch_create(game_data, count, &batch) != MSL_CORE_OK ||
        workload_reset(batch, &workload) != 0 || workload_preroll(batch, &workload) != 0 ||
        run_workload(batch, &workload, warmup_ticks, 0, &warmup) != 0) {
      if (batch != NULL) {
        msl_core_batch_destroy(batch);
      }
      workload_free(&workload);
      return -1;
    }
#ifdef MSL_SUBSYSTEM_PROFILE
    if (write_outputs) {
      msl_subsystem_profile_reset();
    }
#endif
#ifdef MSL_CORE_GPROF
    if (write_outputs) {
      moncontrol(1);
    }
#endif
#ifdef MSL_CORE_CALLGRIND
    if (write_outputs) {
      CALLGRIND_ZERO_STATS;
      CALLGRIND_START_INSTRUMENTATION;
    }
#endif
    if (run_workload(batch, &workload, ticks, write_outputs, &measured) != 0) {
#ifdef MSL_CORE_GPROF
      moncontrol(0);
#endif
#ifdef MSL_CORE_CALLGRIND
      CALLGRIND_STOP_INSTRUMENTATION;
#endif
      msl_core_batch_destroy(batch);
      workload_free(&workload);
      return -1;
    }
#ifdef MSL_CORE_GPROF
    moncontrol(0);
#endif
#ifdef MSL_CORE_CALLGRIND
    if (write_outputs) {
      CALLGRIND_STOP_INSTRUMENTATION;
      CALLGRIND_DUMP_STATS;
    }
#endif
    total->seconds += measured.seconds;
    total->cpu_seconds += measured.cpu_seconds;
    total->cycles += measured.cycles;
    total->resets += measured.resets;
    msl_core_batch_destroy(batch);
    workload_free(&workload);
  }
  return 0;
}

static uint64_t hash_bytes(uint64_t hash, const void* data, size_t size) {
  const uint8_t* bytes = data;
  size_t i;
  for (i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

int main(int argc, char** argv) {
  const char* data_root;
  const char* manifest;
  uint32_t match_count = 256;
  uint32_t resident_match_count = 16;
  uint32_t match_frames = 65536;
  uint32_t warmup_ticks = 8;
  uint32_t ticks;
  uint32_t case_count = 0;
  uint32_t preroll_source_count;
  uint32_t i;
  uint64_t digest = UINT64_C(14695981039346656037);
  uint64_t preroll_match_frames = 0;
  uint64_t stage_mask = 0;
  uint64_t character_mask = 0;
  size_t observation_bytes;
  ReplayCase* cases = NULL;
  MslCoreObservation* observations = NULL;
  MslCoreTerminal* terminals = NULL;
  MslCoreGameData* game_data = NULL;
  RunResult production;
  int result = 1;

#ifdef MSL_CORE_GPROF
  moncontrol(0);
#endif

  if (argc < 3) {
    fprintf(stderr,
            "usage: %s DATA_ROOT MANIFEST [--matches N] [--match-frames N] "
            "[--resident-matches N] [--warmup-ticks N]\n",
            argv[0]);
    return 2;
  }
  data_root = argv[1];
  manifest = argv[2];
  for (i = 3; i < (uint32_t)argc; i += 2) {
    uint32_t* destination = NULL;
    if (i + 1 >= (uint32_t)argc) {
      fprintf(stderr, "invalid benchmark argument: %s\n", argv[i]);
      return 2;
    }
    if (strcmp(argv[i], "--matches") == 0) {
      destination = &match_count;
    } else if (strcmp(argv[i], "--resident-matches") == 0) {
      destination = &resident_match_count;
    } else if (strcmp(argv[i], "--match-frames") == 0) {
      destination = &match_frames;
    } else if (strcmp(argv[i], "--warmup-ticks") == 0) {
      destination = &warmup_ticks;
    }
    if (destination == NULL || parse_u32(argv[i + 1], destination) != 0) {
      fprintf(stderr, "invalid benchmark argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (resident_match_count > match_count) {
    resident_match_count = match_count;
  }
  ticks = (match_frames + match_count - 1) / match_count;
  observation_bytes = (size_t)match_count * OBSERVATION_HISTORY *
                      (sizeof(MslCoreObservation) + sizeof(MslCoreTerminal));
  observations = calloc((size_t)match_count * OBSERVATION_HISTORY, sizeof(*observations));
  terminals = calloc((size_t)match_count * OBSERVATION_HISTORY, sizeof(*terminals));
  if (observations == NULL || terminals == NULL || load_cases(manifest, &cases, &case_count) != 0 ||
      msl_core_game_data_create(data_root, &game_data) != MSL_CORE_OK) {
    fprintf(stderr, "benchmark workload failed\n");
    goto done;
  }
  for (i = 0; i < case_count; ++i) {
    const MslCoreMatchConfig* config = &cases[i].header->config;
    uint32_t player;
    if (config->stage_id < 64) {
      stage_mask |= UINT64_C(1) << config->stage_id;
    }
    for (player = 0; player < config->num_players; ++player) {
      if (config->players[player].char_id < 64) {
        character_mask |= UINT64_C(1) << config->players[player].char_id;
      }
    }
  }
  preroll_source_count = match_count < case_count ? match_count : case_count;
  for (i = 0; i < preroll_source_count; ++i) {
    preroll_match_frames +=
        STARTUP_BASE_FRAMES + (i % STARTUP_GROUP_COUNT) * STARTUP_FRAME_GAP;
  }
  if (run_sharded_pass(game_data, cases, case_count, match_count, resident_match_count, ticks,
                       warmup_ticks, 1, observations, terminals, &production) != 0) {
#ifdef MSL_CORE_GPROF
    moncontrol(0);
#endif
    fprintf(stderr, "benchmark workload failed\n");
    goto done;
  }
  digest = hash_bytes(digest, observations,
                      (size_t)match_count * OBSERVATION_HISTORY * sizeof(*observations));
  digest =
      hash_bytes(digest, terminals, (size_t)match_count * OBSERVATION_HISTORY * sizeof(*terminals));
#ifdef MSL_SUBSYSTEM_PROFILE
  msl_subsystem_profile_report();
#endif
  printf(
      "replay_benchmark mode=%s cases=%u logical_matches=%u "
      "resident_matches=%u ticks=%u history=%u startup_groups=%u "
      "startup_base=%u startup_gap=%u preroll_sources=%u preroll_match_frames=%" PRIu64
      " stage_mask=%016" PRIx64
      " character_mask=%016" PRIx64 " cpu=%d "
      "observation_mib=%.2f\n",
      resident_match_count == match_count ? "resident" : "sharded", case_count, match_count,
      resident_match_count, ticks, OBSERVATION_HISTORY, STARTUP_GROUP_COUNT,
      STARTUP_BASE_FRAMES, STARTUP_FRAME_GAP, preroll_source_count,
      preroll_match_frames, stage_mask, character_mask, sched_getcpu(),
      (double)observation_bytes / (1024.0 * 1024.0));
  printf("production seconds=%.6f cpu_seconds=%.6f cycles=%" PRIu64
         " cycles_per_frame=%.1f match_frames=%" PRIu64
         " fps=%.0f cpu_fps=%.0f resets=%" PRIu64 " digest=%016" PRIx64 "\n",
         production.seconds, production.cpu_seconds, production.cycles,
         production.cycles / (double)((uint64_t)match_count * ticks),
         (uint64_t)match_count * ticks,
         (double)((uint64_t)match_count * ticks) / production.seconds,
         (double)((uint64_t)match_count * ticks) / production.cpu_seconds, production.resets,
         digest);
  result = 0;

done:
  if (game_data != NULL) {
    msl_core_game_data_destroy(game_data);
  }
  free(observations);
  free(terminals);
  unload_cases(cases, case_count);
  return result;
}
