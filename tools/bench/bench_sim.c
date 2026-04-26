#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api.h"
#include "buttons.h"
#include "config.h"

typedef enum BenchMode {
  BENCH_MODE_ROLLOUT,
  BENCH_MODE_ROLLOUT_COMPARE,
} BenchMode;

typedef struct BenchConfig {
  int batch_size;
  int frames;
  int warmup_frames;
  int input_ring;
  BenchMode mode;
} BenchConfig;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t xorshift32(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x ? x : 0x9E3779B9u;
  return *state;
}

static int8_t stick_axis_from_rng(uint32_t r) {
  const int v = (int)(r & 0xFFu) - 128;
  return (int8_t)v;
}

static void fill_match_configs(MslMatchConfig* configs, int batch_size) {
  memset(configs, 0, sizeof(*configs) * (size_t)batch_size);
  for (int bi = 0; bi < batch_size; bi++) {
    MslMatchConfig* cfg = &configs[bi];
    cfg->stage_id = 32u;  // Final Destination, Slippi external stage id.
    cfg->frame_id = -123;
    cfg->frame_pre_random_seed = 0x319E1C7Du ^ (uint32_t)bi * 0x9E3779B9u;
    cfg->match_damage_ratio = 1.0f;
    cfg->num_players = 2;
    cfg->is_teams = 0;
    cfg->stock_count = 4;
    cfg->players[0].char_id = 1;   // Fox, Slippi external id.
    cfg->players[0].team_id = 0;
    cfg->players[0].facing = 1;
    cfg->players[1].char_id = 22;  // Falco, Slippi external id.
    cfg->players[1].team_id = 1;
    cfg->players[1].facing = 0;
  }
}

static uint16_t random_buttons(uint32_t r) {
  uint16_t buttons = 0;
  if (r & (1u << 0)) {
    buttons |= (uint16_t)MSL_BUTTON_A;
  }
  if (r & (1u << 3)) {
    buttons |= (uint16_t)MSL_BUTTON_B;
  }
  if (r & (1u << 6)) {
    buttons |= (uint16_t)MSL_BUTTON_X;
  }
  if (r & (1u << 9)) {
    buttons |= (uint16_t)MSL_BUTTON_Y;
  }
  if (r & (1u << 12)) {
    buttons |= (uint16_t)MSL_BUTTON_Z;
  }
  if (r & (1u << 15)) {
    buttons |= (uint16_t)MSL_BUTTON_L;
  }
  if (r & (1u << 18)) {
    buttons |= (uint16_t)MSL_BUTTON_R;
  }
  return buttons;
}

static void fill_inputs(MslInput* inputs, int ring, int batch_size) {
  memset(inputs, 0, sizeof(*inputs) * (size_t)ring * (size_t)batch_size);
  uint32_t rng = 0xC0FFEE11u;
  for (int f = 0; f < ring; f++) {
    for (int bi = 0; bi < batch_size; bi++) {
      MslInput* in = &inputs[(size_t)f * (size_t)batch_size + (size_t)bi];
      for (int p = 0; p < 2; p++) {
        const uint32_t r0 = xorshift32(&rng);
        const uint32_t r1 = xorshift32(&rng);
        const uint32_t r2 = xorshift32(&rng);
        in->p[p].buttons = random_buttons(r0);
        in->p[p].main_x = stick_axis_from_rng(r1);
        in->p[p].main_y = stick_axis_from_rng(r1 >> 8);
        in->p[p].c_x = stick_axis_from_rng(r2);
        in->p[p].c_y = stick_axis_from_rng(r2 >> 8);
        in->p[p].l = (uint8_t)(r1 >> 16);
        in->p[p].r = (uint8_t)(r2 >> 16);
      }
    }
  }
}

static int parse_int_arg(const char* value, int* out) {
  char* end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed <= 0 || parsed > 1000000000L) {
    return -1;
  }
  *out = (int)parsed;
  return 0;
}

static int parse_args(int argc, char** argv, BenchConfig* cfg) {
  cfg->batch_size = 1024;
  cfg->frames = 20000;
  cfg->warmup_frames = 2000;
  cfg->input_ring = 256;
  cfg->mode = BENCH_MODE_ROLLOUT;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &cfg->batch_size) != 0) {
        return -1;
      }
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &cfg->frames) != 0) {
        return -1;
      }
    } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &cfg->warmup_frames) != 0) {
        return -1;
      }
    } else if (strcmp(argv[i], "--input-ring") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &cfg->input_ring) != 0) {
        return -1;
      }
    } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
      const char* mode = argv[++i];
      if (strcmp(mode, "rollout") == 0) {
        cfg->mode = BENCH_MODE_ROLLOUT;
      } else if (strcmp(mode, "rollout_compare") == 0) {
        cfg->mode = BENCH_MODE_ROLLOUT_COMPARE;
      } else {
        return -1;
      }
    } else if (strcmp(argv[i], "--help") == 0) {
      return 1;
    } else {
      return -1;
    }
  }
  return 0;
}

static const char* mode_name(BenchMode mode) {
  switch (mode) {
    case BENCH_MODE_ROLLOUT:
      return "rollout";
    case BENCH_MODE_ROLLOUT_COMPARE:
      return "rollout_compare";
  }
  return "unknown";
}

static void print_usage(const char* argv0) {
  fprintf(stderr,
          "usage: %s [--batch N] [--frames N] [--warmup N] [--input-ring N]\n"
          "          [--mode rollout|rollout_compare]\n",
          argv0);
}

static int run_steps(MslBatch* batch, const MslInput* inputs, MslCompare* compares,
                     const BenchConfig* cfg, int frames) {
  for (int frame = 0; frame < frames; frame++) {
    const int prev_i = (frame == 0) ? 0 : ((frame - 1) % cfg->input_ring);
    const int cur_i = frame % cfg->input_ring;
    const MslInput* prev = &inputs[(size_t)prev_i * (size_t)cfg->batch_size];
    const MslInput* cur = &inputs[(size_t)cur_i * (size_t)cfg->batch_size];
    int err = msl_batch_step_input(batch, (const uint8_t*)prev, sizeof(MslInput),
                                   (const uint8_t*)cur, sizeof(MslInput));
    if (err != 0) {
      fprintf(stderr, "msl_batch_step_input failed: %d\n", err);
      return err;
    }
    if (cfg->mode == BENCH_MODE_ROLLOUT_COMPARE) {
      err = msl_batch_write_compare(batch, (uint8_t*)compares, sizeof(MslCompare));
      if (err != 0) {
        fprintf(stderr, "msl_batch_write_compare failed: %d\n", err);
        return err;
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  BenchConfig cfg;
  const int parse = parse_args(argc, argv, &cfg);
  if (parse != 0) {
    print_usage(argv[0]);
    return parse > 0 ? 0 : 2;
  }

  MslMatchConfig* match_configs =
      (MslMatchConfig*)calloc((size_t)cfg.batch_size, sizeof(MslMatchConfig));
  MslInput* inputs =
      (MslInput*)calloc((size_t)cfg.input_ring * (size_t)cfg.batch_size, sizeof(MslInput));
  MslCompare* compares = (MslCompare*)calloc((size_t)cfg.batch_size, sizeof(MslCompare));
  if (match_configs == NULL || inputs == NULL || compares == NULL) {
    fprintf(stderr, "benchmark allocation failed\n");
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  fill_match_configs(match_configs, cfg.batch_size);
  fill_inputs(inputs, cfg.input_ring, cfg.batch_size);

  MslBatch* batch = msl_batch_create(cfg.batch_size, 2);
  if (batch == NULL) {
    fprintf(stderr, "msl_batch_create failed\n");
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  int err = msl_batch_init_match(batch, (const uint8_t*)match_configs, sizeof(MslMatchConfig));
  if (err != 0) {
    fprintf(stderr, "msl_batch_init_match failed: %d\n", err);
    msl_batch_destroy(batch);
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  err = run_steps(batch, inputs, compares, &cfg, cfg.warmup_frames);
  if (err != 0) {
    msl_batch_destroy(batch);
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  err = msl_batch_init_match(batch, (const uint8_t*)match_configs, sizeof(MslMatchConfig));
  if (err != 0) {
    fprintf(stderr, "msl_batch_init_match failed after warmup: %d\n", err);
    msl_batch_destroy(batch);
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  const uint64_t start_ns = now_ns();
  err = run_steps(batch, inputs, compares, &cfg, cfg.frames);
  const uint64_t elapsed_ns = now_ns() - start_ns;
  if (err != 0) {
    msl_batch_destroy(batch);
    free(compares);
    free(inputs);
    free(match_configs);
    return 1;
  }

  const double seconds = (double)elapsed_ns / 1000000000.0;
  const double env_steps = (double)cfg.frames * (double)cfg.batch_size;
  const double env_steps_per_sec = env_steps / seconds;
  const double ns_per_env_step = (double)elapsed_ns / env_steps;
  volatile uint64_t checksum = 0;
  for (int bi = 0; bi < cfg.batch_size; bi++) {
    checksum += (uint64_t)compares[bi].frame_id;
    checksum += (uint64_t)compares[bi].action_id[0] + (uint64_t)compares[bi].action_id[1];
  }

  printf("mode=%s batch=%d frames=%d warmup=%d input_ring=%d\n", mode_name(cfg.mode),
         cfg.batch_size, cfg.frames, cfg.warmup_frames, cfg.input_ring);
  printf("elapsed_sec=%.9f env_steps=%.0f env_steps_per_sec=%.3f ns_per_env_step=%.3f\n",
         seconds, env_steps, env_steps_per_sec, ns_per_env_step);
  printf("checksum=%" PRIu64 "\n", checksum);

  msl_batch_destroy(batch);
  free(compares);
  free(inputs);
  free(match_configs);
  return 0;
}
