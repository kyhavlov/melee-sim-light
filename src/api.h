#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MSL_MAX_PLAYERS = 4 };
enum { MSL_MAX_ITEMS = 15 };
enum { MSL_STATE_FLAGS_BYTES = 5 };

// -----------------------------
// Packed on-disk / wire formats
// -----------------------------
// Note: these structs are designed for stable serialization and C<->Python FFI.
// They are not necessarily the optimal in-memory layout (the simulator core uses SoA).

#pragma pack(push, 1)

typedef struct MslInputPlayer {
  // Bitmask of digital buttons. (Mapping is defined in tooling; keep stable.)
  uint16_t buttons;
  // Raw stick values (e.g., -128..127). Tooling defines exact conventions.
  int8_t main_x;
  int8_t main_y;
  int8_t c_x;
  int8_t c_y;
  uint8_t l;
  uint8_t r;
} MslInputPlayer;

typedef struct MslInput {
  MslInputPlayer p[MSL_MAX_PLAYERS];
} MslInput;

typedef struct MslProcessedInputPlayer {
  // Same layout as MslInputPlayer, but stick axes are post-processed (clamped/UCF snapped).
  uint16_t buttons;
  int8_t main_x;
  int8_t main_y;
  int8_t c_x;
  int8_t c_y;
  uint8_t l;
  uint8_t r;
} MslProcessedInputPlayer;

typedef struct MslProcessedInput {
  MslProcessedInputPlayer p[MSL_MAX_PLAYERS];
} MslProcessedInput;

typedef struct MslItem {
  uint8_t exists;  // 0/1
  uint8_t state;   // item state
  uint16_t type;   // item kind/type id

  int8_t owner;  // -1 if none/unknown
  uint8_t _pad0;
  uint16_t instance_id;

  float direction;
  float vel_x;
  float vel_y;
  float pos_x;
  float pos_y;
  uint16_t damage;
  uint16_t _pad1;
  float timer;
  uint32_t spawn_id;
  uint8_t misc0;
  uint8_t misc1;
  uint8_t misc2;
  uint8_t misc3;
} MslItem;

typedef struct MslSeed {
  int32_t frame_id;
  uint32_t
      frame_pre_random_seed;  // pre-frame RNG seed (per-player seeds also exist; this is frame-level)

  uint32_t stage_id;
  uint8_t num_players;  // 2 or 4 (<= MSL_MAX_PLAYERS)
  uint8_t is_teams;     // 0/1
  uint8_t _pad0[2];

  uint8_t team_id[MSL_MAX_PLAYERS];
  uint8_t char_id[MSL_MAX_PLAYERS];

  // Kinematics
  float pos_x[MSL_MAX_PLAYERS];
  float pos_y[MSL_MAX_PLAYERS];
  // Velocities as recorded by Slippi post-frame (when available).
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];     // 0/1
  uint8_t on_ground[MSL_MAX_PLAYERS];  // 0/1
  uint8_t _pad1[2];

  // State machine
  uint16_t action_id[MSL_MAX_PLAYERS];    // GALE01 action id
  int16_t action_frame[MSL_MAX_PLAYERS];  // action frame (can be negative in pre-start)
  uint8_t jumps_left[MSL_MAX_PLAYERS];
  uint8_t stocks[MSL_MAX_PLAYERS];

  // Combat / timers
  float percent[MSL_MAX_PLAYERS];
  float shield_hp[MSL_MAX_PLAYERS];
  uint16_t hitlag[MSL_MAX_PLAYERS];
  uint16_t hitstun[MSL_MAX_PLAYERS];
  uint8_t l_cancel[MSL_MAX_PLAYERS];
  uint8_t hurtbox_state[MSL_MAX_PLAYERS];  // 0 vuln, 1 invuln, 2 intangible
  uint16_t ground_id[MSL_MAX_PLAYERS];
  uint32_t animation_index[MSL_MAX_PLAYERS];
  uint16_t instance_hit_by[MSL_MAX_PLAYERS];
  uint16_t instance_id[MSL_MAX_PLAYERS];
  uint8_t last_attack_landed[MSL_MAX_PLAYERS];
  uint8_t combo_count[MSL_MAX_PLAYERS];
  uint8_t last_hit_by[MSL_MAX_PLAYERS];
  uint8_t _pad2[1];
  uint8_t state_flags[MSL_MAX_PLAYERS][5];

  MslItem items[MSL_MAX_ITEMS];
} MslSeed;

typedef struct MslCompare {
  int32_t frame_id;
  uint32_t frame_pre_random_seed;

  uint32_t stage_id;
  uint8_t num_players;
  uint8_t is_teams;
  uint8_t _pad0[2];

  uint8_t team_id[MSL_MAX_PLAYERS];
  uint8_t char_id[MSL_MAX_PLAYERS];

  float pos_x[MSL_MAX_PLAYERS];
  float pos_y[MSL_MAX_PLAYERS];
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];
  uint8_t on_ground[MSL_MAX_PLAYERS];
  uint8_t is_dead[MSL_MAX_PLAYERS];  // 0/1 (derived or explicit)
  uint8_t _pad1[1];

  uint16_t action_id[MSL_MAX_PLAYERS];
  int16_t action_frame[MSL_MAX_PLAYERS];
  uint8_t jumps_left[MSL_MAX_PLAYERS];
  uint8_t stocks[MSL_MAX_PLAYERS];

  float percent[MSL_MAX_PLAYERS];
  float shield_hp[MSL_MAX_PLAYERS];
  uint16_t hitlag[MSL_MAX_PLAYERS];
  uint16_t hitstun[MSL_MAX_PLAYERS];
  uint8_t l_cancel[MSL_MAX_PLAYERS];
  uint8_t hurtbox_state[MSL_MAX_PLAYERS];
  uint16_t ground_id[MSL_MAX_PLAYERS];
  uint32_t animation_index[MSL_MAX_PLAYERS];
  uint16_t instance_hit_by[MSL_MAX_PLAYERS];
  uint16_t instance_id[MSL_MAX_PLAYERS];
  uint8_t last_attack_landed[MSL_MAX_PLAYERS];
  uint8_t combo_count[MSL_MAX_PLAYERS];
  uint8_t last_hit_by[MSL_MAX_PLAYERS];
  uint8_t _pad2[1];
  uint8_t state_flags[MSL_MAX_PLAYERS][5];

  MslItem items[MSL_MAX_ITEMS];
} MslCompare;

typedef struct MslDatasetHeader {
  char magic[8];  // "MSLDSLT "
  uint32_t record_size;
  uint32_t num_records;
  uint8_t num_players;  // 2 or 4
  uint8_t _pad0[3];
} MslDatasetHeader;

typedef struct MslSample {
  MslSeed seed_t;
  MslInput prev_input_t;
  MslInput input_t;
  MslCompare ref_t1;
} MslSample;

#pragma pack(pop)

// -------------
// Simulator core
// -------------

typedef struct MslBatch MslBatch;

// Allocates a batched simulator handle and all internal SoA buffers.
// This is the only place allocations are permitted.
MslBatch* msl_batch_create(int batch_size, int num_players);

void msl_batch_destroy(MslBatch* batch);

int msl_batch_batch_size(const MslBatch* batch);
int msl_batch_num_players(const MslBatch* batch);

// Mutate small runtime toggles. Safe to call after create; does not allocate.
int msl_batch_set_ucf_enabled(MslBatch* batch, int enabled);
int msl_batch_set_ucf_cardinals_1_0_enabled(MslBatch* batch, int enabled);

// Reseed from packed MslSeed array of length batch_size.
// seed_stride_bytes must be >= sizeof(MslSeed).
int msl_batch_reseed_seed(MslBatch* batch, const uint8_t* seed_bytes, size_t seed_stride_bytes);

// Step one frame using packed inputs. The current "empty sim" stub ignores inputs.
// input_stride_bytes must be >= sizeof(MslInput).
int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes);

// Write packed compare outputs (length batch_size).
// out_stride_bytes must be >= sizeof(MslCompare).
int msl_batch_write_compare(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes);

// Debug/validation helper: write current processed input values.
// out_stride_bytes must be >= sizeof(MslProcessedInput).
int msl_batch_debug_write_processed_input(const MslBatch* batch, uint8_t* out_bytes,
                                          size_t out_stride_bytes);

#ifdef __cplusplus
}
#endif
