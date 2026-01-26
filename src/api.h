#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MSL_MAX_PLAYERS = 4 };
enum { MSL_MAX_ITEMS = 15 };
enum { MSL_MAX_HURTCAPS = 32 };
enum { MSL_MAX_HITBOXES = 4 };
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
  float pos_z[MSL_MAX_PLAYERS];
  // Velocities as recorded by Slippi post-frame (when available).
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];
  // Fighter model scale (decomp: fp->x34_scale.y). Slippi does not currently expose this, so
  // tooling defaults it to 1.0 for normal matches; tests may override.
  float fighter_scale_y[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];     // 0/1
  uint8_t on_ground[MSL_MAX_PLAYERS];  // 0/1
  uint8_t _pad1[2];

  // State machine
  uint16_t action_id[MSL_MAX_PLAYERS];    // GALE01 action id
  int16_t action_frame[MSL_MAX_PLAYERS];  // action frame (can be negative in pre-start)
  // Reseed-only match-flow countdown timer (teacher-forcing aid).
  // Used for match-start entry / KO / respawn states where Slippi post-frames do not expose a
  // useful per-frame counter (action_frame is often -1).
  //
  // Convention: decomp-shaped countdown value (fp->x2340-style), derived from ftCommonData constants
  // and elapsed-in-state (contiguous run length so far), clamped to 255.
  //
  // NOTE: This is not "remaining until the action ends" in general, because match-flow states can
  // exit early via inputs (IASA), and the replay action_id run length may be shorter than the
  // internal timer.
  uint8_t match_flow_timer[MSL_MAX_PLAYERS];
  // DownWait countdown timer (seeded; decomp-shaped).
  //
  // Decomp:
  // - init on DownBound->DownWait: fp->mv.co.downwait.x0 = p_ftCommonData->x424
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
  // - decrement + auto-stand in DownWait_Anim:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Anim
  //
  // Slippi post-frame does not expose fp->mv.* unions, so tooling derives this strictly causally
  // from the post-frame action_id sequence.
  int16_t downwait_timer[MSL_MAX_PLAYERS];
  // Decomp-shaped animation/script timebase: fp->cur_anim_frame (float).
  // Slippi post-frame exposes this as `state_age` (float, can be fractional).
  //
  // Source pointers:
  // - refs/melee/src/melee/ft/types.h (Fighter::cur_anim_frame at fp+894)
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm ("send AS frame", loads from 0x894)
  // - refs/melee/src/melee/ft/ftaction.c::ftAction_80073240 (movescript timers use fp->cur_anim_frame)
  float anim_frame_f32[MSL_MAX_PLAYERS];
  // Decomp: fp->frame_speed_mul controls fractional animation advance (HSD AObj rate).
  // Not exposed by Slippi post-frames; we derive/seed it strictly causally in preprocessing.
  //
  // Source pointers:
  // - refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState sets fp->frame_speed_mul)
  // - refs/melee/src/melee/ft/ftanim.c (ftAnim_8006F0FC / ftAnim_SetAnimRate)
  float frame_speed_mul_f32[MSL_MAX_PLAYERS];
  // Guard (shield) tilt pose state (seeded; decomp-shaped).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
  // - mv.co.guard.x8: "frame-ish" index into the Guard tilt timeline (neutral is 10 in GALE01)
  // - mv.co.guard.x4: stick magnitude smoothing used to blend the pose
  uint16_t guard_tilt_x8[MSL_MAX_PLAYERS];
  float guard_tilt_x4[MSL_MAX_PLAYERS];
  uint8_t jumps_left[MSL_MAX_PLAYERS];
  uint8_t stocks[MSL_MAX_PLAYERS];

  // Input-history / locomotion internals (seeded from replay history)
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28 and :44-56
  uint8_t kneebend_jump_input[MSL_MAX_PLAYERS];    // fp->mv.co.kneebend.jump_input (ftCo_JumpInput)
  uint8_t kneebend_is_short_hop[MSL_MAX_PLAYERS];  // fp->mv.co.kneebend.is_short_hop (bool)
  // Decomp: refs/melee/src/melee/ft/fighter.c:1908-2008 (x670/x671 updates each frame)
  uint8_t tilt_timer_x[MSL_MAX_PLAYERS];  // fp->x670_timer_lstick_tilt_x
  uint8_t tilt_timer_y[MSL_MAX_PLAYERS];  // fp->x671_timer_lstick_tilt_y
  // Decomp: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
  uint8_t fall_fast[MSL_MAX_PLAYERS];  // fp->fall_fast (bool)
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 (ftCo_Turn_Anim_Inner)
  uint8_t turn_frames_to_turn[MSL_MAX_PLAYERS];  // fp->mv.co.turn.frames_to_turn
  uint8_t turn_has_turned[MSL_MAX_PLAYERS];      // fp->mv.co.turn.has_turned
  // Decomp: refs/melee/src/melee/ft/fighter.c:2078-2086 (x67F updates each frame).
  uint8_t lr_press_timer[MSL_MAX_PLAYERS];  // fp->x67F (frames since L/R press; saturates at 0xFF)
  // Decomp: refs/melee/src/melee/ft/fighter.c:2020-2050 (x672 updates each frame).
  uint8_t x672_input_timer[MSL_MAX_PLAYERS];  // fp->x672_input_timer_counter (saturates at 0xFE)
  // Decomp: refs/melee/src/melee/ft/fighter.c:1897-2094 (fighter input counters block).
  // Stick/trigger companion timers + "age since last change" counters (saturate at 0xFE).
  uint8_t x673[MSL_MAX_PLAYERS];    // fp->x673 (lstick x companion)
  uint8_t x674[MSL_MAX_PLAYERS];    // fp->x674 (lstick y companion)
  uint8_t x675[MSL_MAX_PLAYERS];    // fp->x675 (trigger companion)
  uint8_t x676_x[MSL_MAX_PLAYERS];  // fp->x676_x ("age since last change", x)
  uint8_t x677_y[MSL_MAX_PLAYERS];  // fp->x677_y ("age since last change", y)
  uint8_t x678[MSL_MAX_PLAYERS];    // fp->x678 ("age since last change", trigger)
  uint8_t x679_x[MSL_MAX_PLAYERS];  // fp->x679_x (lstick x companion)
  uint8_t x67A_y[MSL_MAX_PLAYERS];  // fp->x67A_y (lstick y companion)
  uint8_t x67B[MSL_MAX_PLAYERS];    // fp->x67B (trigger companion)
  // Button timers (saturate at 0xFF, reset to 0 on press).
  uint8_t x67C[MSL_MAX_PLAYERS];  // fp->x67C (A)
  uint8_t x67D[MSL_MAX_PLAYERS];  // fp->x67D (B)
  uint8_t x67E[MSL_MAX_PLAYERS];  // fp->x67E (X/Y)
  uint8_t x680[MSL_MAX_PLAYERS];  // fp->x680 (L or R)
  uint8_t x681[MSL_MAX_PLAYERS];  // fp->x681 (DPad Up)
  uint8_t x682[MSL_MAX_PLAYERS];  // fp->x682 (DPad Down)
  // Button timer capture on press.
  uint8_t x683[MSL_MAX_PLAYERS];  // fp->x683 (captures prev x67C on A press)
  uint8_t x684[MSL_MAX_PLAYERS];  // fp->x684 (captures prev x680 on L/R press)

  // UCF pad buffer (seeded, multi-frame).
  //
  // References:
  // - refs/ucf/include/ucf/pad_buffer.h (UCF_PAD_BUFFER_SIZE=4, index, sdrop_up_frames)
  // - refs/ucf/src/pad_buffer/pad_buffer.cpp (ring-buffer write ordering)
  uint8_t ucf_padbuf_index[MSL_MAX_PLAYERS];
  uint8_t ucf_padbuf_sdrop_up_frames[MSL_MAX_PLAYERS];
  int8_t ucf_padbuf_stick_x[MSL_MAX_PLAYERS][4];
  int8_t ucf_padbuf_stick_y[MSL_MAX_PLAYERS][4];

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

  // Combat rehit/hitlist internals (seeded; strictly causal from history in preprocessing).
  //
  // These fields are required for teacher-forced one-step eval: reseeding wipes rollout history,
  // so combat must carry its rehit suppression state through the seed schema.
  //
  // Shape: [attacker][defender] in player-slot order.
  uint8_t combat_rehit_active[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS];     // 0/1
  uint8_t combat_rehit_hitbox_id[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS];  // 0..3 or 0xFF
  uint16_t combat_rehit_attacker_msid[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS];
  uint16_t combat_rehit_defender_instance_id[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS];

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

// Debug/validation helper: read a small set of internal locomotion/input-history fields.
// This struct is packed for stable C<->Python inspection in tests.
typedef struct MslDebugInternals {
  uint8_t tilt_timer_x[MSL_MAX_PLAYERS];         // fp->x670_timer_lstick_tilt_x
  uint8_t turn_frames_to_turn[MSL_MAX_PLAYERS];  // fp->mv.co.turn.frames_to_turn
  uint8_t turn_has_turned[MSL_MAX_PLAYERS];      // fp->mv.co.turn.has_turned
} MslDebugInternals;

// Debug/validation helper: record a single hitbox-vs-hurtcap contact candidate.
// This is a compact snapshot of the world-space primitives used in combat pass 1.
typedef struct MslDebugCombatContact {
  uint8_t attacker;    // player index
  uint8_t defender;    // player index
  uint8_t hitbox_id;   // 0..MSL_MAX_HITBOXES-1
  uint8_t hurtcap_id;  // 0..MSL_MAX_HURTCAPS-1 (world array index)

  uint16_t attacker_msid;  // Slippi post-frame `animation_index` truncated to u16
  int16_t attacker_action_frame;

  float hitbox_x;
  float hitbox_y;
  float hitbox_z;
  float hitbox_radius;
  float hitbox_damage;

  float hurtcap_ax;
  float hurtcap_ay;
  float hurtcap_az;
  float hurtcap_bx;
  float hurtcap_by;
  float hurtcap_bz;
  float hurtcap_radius;
} MslDebugCombatContact;

// Debug/validation helper: record a single "would-hit" contact candidate, classified as BODY or
// SHIELD without mutating gameplay state.
//
// contact_kind:
//  0 = BODY   (hitbox intersects any hurtcap, and does NOT intersect shield bubble)
//  1 = SHIELD (hitbox intersects defender shield bubble)
typedef struct MslDebugCombatContactClassified {
  uint8_t attacker;      // player index
  uint8_t defender;      // player index
  uint8_t hitbox_id;     // 0..MSL_MAX_HITBOXES-1
  uint8_t contact_kind;  // 0=BODY, 1=SHIELD

  // BODY: 0..MSL_MAX_HURTCAPS-1 (world array index)
  // SHIELD: 0xFF
  uint8_t hurtcap_id;
  uint8_t _pad0[3];

  uint16_t attacker_msid;  // Slippi post-frame `animation_index` truncated to u16
  int16_t attacker_action_frame;

  float hitbox_x;
  float hitbox_y;
  float hitbox_z;
  float hitbox_radius;
  float hitbox_damage;

  // BODY payload (zeroed for SHIELD).
  float hurtcap_ax;
  float hurtcap_ay;
  float hurtcap_az;
  float hurtcap_bx;
  float hurtcap_by;
  float hurtcap_bz;
  float hurtcap_radius;

  // SHIELD payload (zeroed when defender shield bubble inactive).
  float shield_x;
  float shield_y;
  float shield_z;
  float shield_radius;
} MslDebugCombatContactClassified;

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

// Debug/validation helper: write selected internal fields.
// out_stride_bytes must be >= sizeof(MslDebugInternals).
int msl_batch_debug_write_internals(const MslBatch* batch, uint8_t* out_bytes,
                                    size_t out_stride_bytes);

// Debug/validation helper: read pose-driven world-space hurt capsules for a single fighter.
// Writes `MSL_MAX_HURTCAPS * 7` floats into out_caps_7 as rows:
//   [ax, ay, az, bx, by, bz, radius]
// and returns the active capsule count in out_count.
int msl_batch_debug_hurtcaps_world(const MslBatch* batch, int batch_index, int player_index,
                                   float* out_caps_7, uint8_t* out_count);

// Debug/validation helper: read pose-driven world-space hitbox centers for a single fighter.
// Writes `MSL_MAX_HITBOXES * 10` floats into out_hitboxes_10 as rows:
//   [x, y, z, radius, damage, u16_0, u16_1, u16_3, bone_part_id, enabled]
// and returns the active hitbox count in out_count.
int msl_batch_debug_hitboxes_world(const MslBatch* batch, int batch_index, int player_index,
                                   float* out_hitboxes_10, uint8_t* out_count);

// Debug/validation helper: read pose-driven world-space hitboxes with full decoded attributes.
// Writes `MSL_MAX_HITBOXES * 16` floats into out_hitboxes_16 as rows:
//   [x, y, z, radius, damage, angle, kbg, wsk, bkb, element, shield_damage, sfx_severity, sfx_kind,
//    flags, bone_part_id, enabled]
// and returns the active hitbox count in out_count.
int msl_batch_debug_hitboxes_world_full(const MslBatch* batch, int batch_index, int player_index,
                                        float* out_hitboxes_16, uint8_t* out_count);

// Debug/validation helper: compute and dump hitbox-vs-hurtcap contacts for one batch element.
// Writes up to max_contacts entries into out_contacts and returns the number written in out_count.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// hitbox_id 0..3, hurtcap_id 0..count-1.
int msl_batch_debug_combat_contacts(const MslBatch* batch, int batch_index,
                                    MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                    uint16_t* out_count);

// Debug/validation helper: compute hitbox-vs-hurtcap contacts with decomp-shaped gating.
//
// This is a debug-only filter pass: it does not mutate validated gameplay state. The intent is to
// keep the overlap diagnostics closer to "real combat" without enabling percent/hitlag/hitstun.
//
// Current filters:
// - Victim ground/air eligibility: HIT_GROUNDED / HIT_AERIAL from extracted hitbox flags
//   (MSLHITB1 u16_6; decoded as state.hitbox_flags).
//
// Deterministic ordering matches msl_batch_debug_combat_contacts; filters only skip/keep.
int msl_batch_debug_combat_contacts_filtered(const MslBatch* batch, int batch_index,
                                             MslDebugCombatContact* out_contacts,
                                             uint16_t max_contacts, uint16_t* out_count);

// Debug/validation helper: compute hitbox-vs-shield and hitbox-vs-hurtcap contacts for one batch
// element, classifying each as BODY or SHIELD.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// hitbox_id 0..3, and within a hitbox: SHIELD first, then BODY (if applicable).
int msl_batch_debug_combat_contacts_classified(const MslBatch* batch, int batch_index,
                                               MslDebugCombatContactClassified* out_contacts,
                                               uint16_t max_contacts, uint16_t* out_count);

// Deterministic ordering matches msl_batch_debug_combat_contacts_classified; filters only skip/keep.
int msl_batch_debug_combat_contacts_classified_filtered(
    const MslBatch* batch, int batch_index, MslDebugCombatContactClassified* out_contacts,
    uint16_t max_contacts, uint16_t* out_count);

// Debug/validation helper: write per-player shield bubble world params for a batch element.
// Writes `MSL_MAX_PLAYERS * 4` floats into out_xyzw_4p as rows: [x, y, z, radius].
int msl_batch_debug_shield_bubbles_world(const MslBatch* batch, int batch_index,
                                         float* out_xyzw_4p);

// Debug/testing helper: allow unit tests to write world-space primitives directly and invoke combat
// without touching upstream pose systems.
int msl_batch_debug_clear_hitboxes_world(MslBatch* batch, int batch_index, int player_index);
int msl_batch_debug_set_hitbox_world(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, float x, float y, float z, float radius,
                                     float damage, int enabled);
int msl_batch_debug_set_hitbox_flags(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, uint16_t hitbox_flags);
int msl_batch_debug_set_hitbox_element(MslBatch* batch, int batch_index, int player_index,
                                       int hitbox_id, uint8_t element);
int msl_batch_debug_set_hitbox_kb_params(MslBatch* batch, int batch_index, int player_index,
                                         int hitbox_id, uint16_t angle_deg, uint16_t kbg,
                                         uint16_t wsk, uint16_t bkb);
int msl_batch_debug_clear_hurtcaps_world(MslBatch* batch, int batch_index, int player_index);
int msl_batch_debug_set_hurtcap_world(MslBatch* batch, int batch_index, int player_index,
                                      int hurtcap_id, float ax, float ay, float az, float bx,
                                      float by, float bz, float radius);
int msl_batch_debug_set_hurtcap_height(MslBatch* batch, int batch_index, int player_index,
                                       int hurtcap_id, uint8_t height);
int msl_batch_debug_set_hurtcap_enabled(MslBatch* batch, int batch_index, int player_index,
                                        int hurtcap_id, int enabled);
int msl_batch_debug_set_hitlag(MslBatch* batch, int batch_index, int player_index,
                               uint16_t hitlag_frames);
// Debug/testing helper: override movescript-derived hit status (opcode 26) eligibility.
// - Pass status=-1 to clear the override (use extracted tables).
// - Otherwise status must fit in u8 (0=normal, 1=invincible, 2=intangible in current decomp domain).
int msl_batch_debug_set_hit_status_override(MslBatch* batch, int batch_index, int player_index,
                                            int status);
int msl_batch_debug_combat_resolve(MslBatch* batch);

// Debug/testing only: run combat pass-1 BODY-hit selection (non-mutating) and return the chosen
// contacts in deterministic order (at most 1 per attacker→defender per call).
int msl_batch_debug_combat_select_body_hits(MslBatch* batch, int batch_index,
                                            MslDebugCombatContact* out_contacts,
                                            uint16_t max_contacts, uint16_t* out_count);

// Debug/testing helper: pure geometry routine for unit tests.
int msl_debug_point_segment_dist2(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz, float* out_d2, float* out_t);

// Test/debug helper: reset selected global init-time tables that depend on MSL_DATA_DIR so they can
// be reloaded within the same process. This exists for synthetic tests; do not call while any live
// batches exist.
int msl_debug_reset_pose_and_hitboxes_tables(void);

#ifdef __cplusplus
}
#endif
