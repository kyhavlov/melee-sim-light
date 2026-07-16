#define _GNU_SOURCE

#include "api.h"
#include "runtime/benchmark_wire.h"

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

enum { OBSERVATION_HISTORY = 128 };

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
  uint8_t* viewpoint;
  uint8_t* reset_mask;
} Workload;

typedef struct RunResult {
  double seconds;
  uint64_t resets;
} RunResult;

typedef struct ResetSeed {
  uint32_t case_index;
  uint32_t frame_index;
  void* snapshot;
  size_t snapshot_size;
} ResetSeed;

static double seconds_now(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) {
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
  workload->viewpoint = calloc(match_count, sizeof(*workload->viewpoint));
  workload->reset_mask = calloc(match_count, sizeof(*workload->reset_mask));
  return workload->configs != NULL && workload->inputs != NULL && observations != NULL &&
                 terminals != NULL && workload->case_index != NULL &&
                 workload->frame_index != NULL && workload->viewpoint != NULL &&
                 workload->reset_mask != NULL
             ? 0
             : -1;
}

static void workload_free(Workload* workload) {
  free(workload->configs);
  free(workload->inputs);
  free(workload->case_index);
  free(workload->frame_index);
  free(workload->viewpoint);
  free(workload->reset_mask);
}

static int workload_reset(MslCoreBatch* batch, Workload* workload) {
  uint32_t i;
  for (i = 0; i < workload->match_count; ++i) {
    uint32_t case_index = (workload->output_match_offset + i) % workload->case_count;
    workload->case_index[i] = case_index;
    workload->frame_index[i] = 0;
    workload->configs[i] = workload->cases[case_index].header->config;
  }
  return msl_core_batch_reset_matches(batch, workload->configs, sizeof(workload->configs[0]), NULL,
                                      0) == MSL_CORE_OK
             ? 0
             : -1;
}

static int run_workload(MslCoreBatch* batch, Workload* workload, uint32_t ticks, int write_outputs,
                        RunResult* result) {
  uint64_t resets = 0;
  uint32_t tick;
  double started = seconds_now();
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
  result->seconds = seconds_now() - started;
  result->resets = resets;
  return 0;
}

static int run_sharded_pass(const MslCoreGameData* game_data, ReplayCase* cases,
                            uint32_t case_count, uint32_t logical_match_count,
                            uint32_t resident_match_count, uint32_t ticks, uint32_t warmup_ticks,
                            int write_outputs, MslCoreObservation* observations,
                            MslCoreTerminal* terminals, const ResetSeed* reset_seed,
                            RunResult* total) {
  uint32_t offset;
  total->seconds = 0.0;
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
        workload_reset(batch, &workload) != 0 ||
        run_workload(batch, &workload, warmup_ticks, write_outputs, &warmup) != 0 ||
        workload_reset(batch, &workload) != 0) {
      if (batch != NULL) {
        msl_core_batch_destroy(batch);
      }
      workload_free(&workload);
      return -1;
    }
    workload.case_index[0] = reset_seed->case_index;
    workload.frame_index[0] = reset_seed->frame_index;
    workload.configs[0] = cases[reset_seed->case_index].header->config;
    if (msl_core_batch_restore_match(batch, 0, reset_seed->snapshot, reset_seed->snapshot_size) !=
            MSL_CORE_OK ||
        run_workload(batch, &workload, ticks, write_outputs, &measured) != 0) {
      msl_core_batch_destroy(batch);
      workload_free(&workload);
      return -1;
    }
    total->seconds += measured.seconds;
    total->resets += measured.resets;
    msl_core_batch_destroy(batch);
    workload_free(&workload);
  }
  return 0;
}

static int reset_seed_create(const MslCoreGameData* game_data, ReplayCase* cases,
                             uint32_t case_count, uint32_t ticks, ResetSeed* seed) {
  MslCoreBatch* batch = NULL;
  uint32_t case_index = 0;
  uint32_t prefix;
  uint32_t i;
  size_t written = 0;
  memset(seed, 0, sizeof(*seed));
  for (i = 1; i < case_count; ++i) {
    if (cases[i].header->frame_count < cases[case_index].header->frame_count) {
      case_index = i;
    }
  }
  prefix = cases[case_index].header->frame_count;
  if (prefix > ticks / 2) {
    prefix -= ticks / 2;
  } else {
    prefix = 0;
  }
  if (msl_core_batch_create(game_data, 1, &batch) != MSL_CORE_OK ||
      msl_core_batch_reset_matches(batch, &cases[case_index].header->config,
                                   sizeof(MslCoreMatchConfig), NULL, 0) != MSL_CORE_OK) {
    goto fail;
  }
  for (i = 0; i < prefix; ++i) {
    if (msl_core_batch_step_matches(batch, &cases[case_index].inputs[i], sizeof(MslCoreInput), NULL,
                                    0) != MSL_CORE_OK) {
      goto fail;
    }
  }
  if (msl_core_batch_match_save_size(batch, 0, &seed->snapshot_size) != MSL_CORE_OK ||
      (seed->snapshot = malloc(seed->snapshot_size)) == NULL ||
      msl_core_batch_save_match(batch, 0, seed->snapshot, seed->snapshot_size, &written) !=
          MSL_CORE_OK ||
      written != seed->snapshot_size) {
    goto fail;
  }
  seed->case_index = case_index;
  seed->frame_index = prefix;
  msl_core_batch_destroy(batch);
  return 0;

fail:
  if (batch != NULL) {
    msl_core_batch_destroy(batch);
  }
  free(seed->snapshot);
  memset(seed, 0, sizeof(*seed));
  return -1;
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
  uint32_t match_frames = 32768;
  uint32_t warmup_ticks = 8;
  uint32_t ticks;
  uint32_t case_count = 0;
  uint32_t i;
  uint64_t digest = UINT64_C(14695981039346656037);
  size_t observation_bytes;
  ReplayCase* cases = NULL;
  MslCoreObservation* observations = NULL;
  MslCoreTerminal* terminals = NULL;
  MslCoreGameData* game_data = NULL;
  ResetSeed reset_seed = {0};
  RunResult production;
  RunResult step_only;
  int result = 1;

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
      msl_core_game_data_create(data_root, &game_data) != MSL_CORE_OK ||
      reset_seed_create(game_data, cases, case_count, ticks, &reset_seed) != 0 ||
      run_sharded_pass(game_data, cases, case_count, match_count, resident_match_count, ticks,
                       warmup_ticks, 1, observations, terminals, &reset_seed, &production) != 0) {
    fprintf(stderr, "benchmark workload failed\n");
    goto done;
  }
  digest = hash_bytes(digest, observations,
                      (size_t)match_count * OBSERVATION_HISTORY * sizeof(*observations));
  digest =
      hash_bytes(digest, terminals, (size_t)match_count * OBSERVATION_HISTORY * sizeof(*terminals));
  if (run_sharded_pass(game_data, cases, case_count, match_count, resident_match_count, ticks,
                       warmup_ticks, 0, observations, terminals, &reset_seed, &step_only) != 0) {
    fprintf(stderr, "step-only diagnostic failed\n");
    goto done;
  }
  printf(
      "replay_benchmark mode=%s cases=%u logical_matches=%u "
      "resident_matches=%u ticks=%u history=%u cpu=%d "
      "observation_mib=%.2f\n",
      resident_match_count == match_count ? "resident" : "sharded", case_count, match_count,
      resident_match_count, ticks, OBSERVATION_HISTORY, sched_getcpu(),
      (double)observation_bytes / (1024.0 * 1024.0));
  printf("production seconds=%.6f match_frames=%" PRIu64 " fps=%.0f resets=%" PRIu64
         " digest=%016" PRIx64 "\n",
         production.seconds, (uint64_t)match_count * ticks,
         (double)((uint64_t)match_count * ticks) / production.seconds, production.resets, digest);
  printf("step_only seconds=%.6f match_frames=%" PRIu64 " fps=%.0f resets=%" PRIu64 "\n",
         step_only.seconds, (uint64_t)match_count * ticks,
         (double)((uint64_t)match_count * ticks) / step_only.seconds, step_only.resets);
  result = 0;

done:
  if (game_data != NULL) {
    msl_core_game_data_destroy(game_data);
  }
  free(observations);
  free(terminals);
  free(reset_seed.snapshot);
  unload_cases(cases, case_count);
  return result;
}
