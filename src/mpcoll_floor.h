#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "common_params.h"
#include "escapeair_collision_owner.h"
#include "mpcoll_context.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_wall_ceil.h"
#include "stage_collision.h"

enum {
  MSL_MPCOLL_FLOOR_RESULT_NONE = 0u,
  MSL_MPCOLL_FLOOR_RESULT_DIRECT = 1u,
  MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY = 2u,
  MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE = 3u,
};

enum {
  MSL_MPCOLL_FLOOR_MODE_NONE = 0u,
  MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP = 1u,
  MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION = 2u,
  MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP = 3u,
  MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY = 4u,
  MSL_MPCOLL_FLOOR_MODE_4A908_RETRY = 5u,
  MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION = 6u,
  MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION = 7u,
};

// Final floor-publication reject bits are diagnostics for source-phase guards, not independent
// gameplay owners. Each retained bit must be added through MslMpcollFloorRejectPacket with the
// mpColl source phase that would have accepted or rejected CollData.floor in vanilla; when a guard
// becomes a natural consequence of the shared CollData phase model, delete the bit instead of
// adding another local late-publication exception.
#define MSL_MPCOLL_REJECT_ESCAPEAIR_TRANSFORMED_REMAP (UINT64_C(1) << 0)
#define MSL_MPCOLL_REJECT_SPECIALAIRHI_PLATFORM (UINT64_C(1) << 1)
#define MSL_MPCOLL_REJECT_FALLSPECIAL_FIRST_SUSTAINED (UINT64_C(1) << 2)
#define MSL_MPCOLL_REJECT_FALLSPECIAL_PLATFORM_NO_SOURCE_BOTTOM (UINT64_C(1) << 4)
#define MSL_MPCOLL_REJECT_FALL_SAME_FLOOR_EARLY (UINT64_C(1) << 5)
#define MSL_MPCOLL_REJECT_SPECIALHI_TRANSFORMED_PLATFORM (UINT64_C(1) << 6)
#define MSL_MPCOLL_REJECT_SPECIALHI_UNDERSTAGE_HARD_FLOOR (UINT64_C(1) << 7)
#define MSL_MPCOLL_REJECT_SPECIALHI_FROM_BELOW_HARD_FLOOR (UINT64_C(1) << 8)
#define MSL_MPCOLL_REJECT_SPECIALAIRHI_FLOOR_ANGLE (UINT64_C(1) << 9)
#define MSL_MPCOLL_REJECT_AIRBORNE_TRANSFORMED_PLATFORM_PRE_HANDOFF (UINT64_C(1) << 10)
#define MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY (UINT64_C(1) << 11)
#define MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_FLOOR_SKIP (UINT64_C(1) << 12)
#define MSL_MPCOLL_REJECT_ATTACKAIR_OFFSPAN_HARD_FLOOR_EDGE (UINT64_C(1) << 13)
#define MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_BELOW (UINT64_C(1) << 14)
#define MSL_MPCOLL_REJECT_JUMPAERIAL_TRANSFORMED_PLATFORM_FASTFALL (UINT64_C(1) << 15)
#define MSL_MPCOLL_REJECT_JUMPAERIAL_STATIC_PLATFORM_FROM_BELOW (UINT64_C(1) << 16)
#define MSL_MPCOLL_REJECT_FALL_TRANSFORMED_PLATFORM_FASTFALL (UINT64_C(1) << 17)
#define MSL_MPCOLL_REJECT_FALL_LOOP_WRAP_STAGE_OBJECT_FLOOR_TO_HARD_FLOOR (UINT64_C(1) << 18)
#define MSL_MPCOLL_REJECT_FALL_STALE_PLATFORM_FIRST_HARD_FLOOR (UINT64_C(1) << 34)
#define MSL_MPCOLL_REJECT_ATTACKAIR_SINGLE_CREATE_NO_BOTTOM_OWNER (UINT64_C(1) << 35)
#define MSL_MPCOLL_REJECT_FALL_STATIC_PLATFORM_FROM_BELOW (UINT64_C(1) << 36)
#define MSL_MPCOLL_REJECT_MISSFOOT_ECB_LOCK_FIRST_FLOOR (UINT64_C(1) << 37)
#define MSL_MPCOLL_REJECT_FALL_ATTACKAIR_ENTRY_TRANSFORMED_PLATFORM_ROOT_ONLY (UINT64_C(1) << 38)
#define MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_PLATFORM_LOCK (UINT64_C(1) << 19)
#define MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_LEDGE_LOCK (UINT64_C(1) << 20)
#define MSL_MPCOLL_REJECT_LOCKED_ESCAPEAIR_MISSING_BOTTOM_OWNER (UINT64_C(1) << 21)
#define MSL_MPCOLL_REJECT_LOCKED_DESIRED_PLATFORM_WITHOUT_BOTTOM_SWEEP (UINT64_C(1) << 22)
#define MSL_MPCOLL_REJECT_LOCKED_DESIRED_NONPLATFORM_WITHOUT_BOTTOM_SWEEP (UINT64_C(1) << 23)
#define MSL_MPCOLL_REJECT_KNEEBEND_ESCAPEAIR_SLOPE (UINT64_C(1) << 24)
#define MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_HIGH_LIFT_LEDGE (UINT64_C(1) << 25)
#define MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_STATIC_PLATFORM_OVERSTEP (UINT64_C(1) << 26)
#define MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_ROOT_BELOW_BOTTOM_ABOVE_FLOOR (UINT64_C(1) << 27)
#define MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_DOWNWARD_SDI_AIRBORNE (UINT64_C(1) << 28)
#define MSL_MPCOLL_REJECT_CLIFF_HORIZONTAL_LEDGE_LOCKED (UINT64_C(1) << 29)
#define MSL_MPCOLL_REJECT_SPECIALAIRLW_START_STALE_PLATFORM (UINT64_C(1) << 30)
#define MSL_MPCOLL_REJECT_ATTACKAIR_HARD_SLOPE_ROOT_WITHOUT_BOTTOM (UINT64_C(1) << 31)
#define MSL_MPCOLL_REJECT_FALL_SHALLOW_TERMINAL_HARD_FLOOR (UINT64_C(1) << 32)
#define MSL_MPCOLL_REJECT_ESCAPEAIR_JUMPAERIAL_SOFT_OWNER_EARLY_DIRECT_LAND (UINT64_C(1) << 33)
#define MSL_MPCOLL_REJECT_SHEIK_VANISH_START1_PLATFORM_PASS (UINT64_C(1) << 39)
#define MSL_MPCOLL_REJECT_KNEEBEND_ESCAPEAIR_STATIC_PLATFORM_LOCK (UINT64_C(1) << 40)
#define MSL_MPCOLL_REJECT_DAMAGE_SUSTAINED_PLATFORM_NO_BOTTOM_SWEEP (UINT64_C(1) << 41)
typedef enum MslMpcollFloorRejectRestore {
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_NONE = 0u,
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT = 1u,
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y = 2u,
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_SPECIALHI_UNDERSTAGE_CLEARANCE = 3u,
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL = 4u,
  MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_XY = 5u,
} MslMpcollFloorRejectRestore;

enum {
  MSL_MPCOLL_FLOOR_REJECT_SIDE_FLOOR_SKIP_TO_CONTACT = 1u << 0,
  MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_PUBLISH_SKIP_FROM_SWEEP = 1u << 1,
  MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_CLEAR_FLOOR_SKIP = 1u << 2,
};

typedef struct MslMpcollFloorContact {
  uint16_t ground_id;
  float contact_x;
  float contact_y;
  float normal_x;
  float normal_y;
} MslMpcollFloorContact;

typedef struct MslMpcollFloorHit {
  uint8_t result_mode;
  MslMpcollFloorContact contact;
} MslMpcollFloorHit;

typedef struct MslMpcoll800471F8EscapeAirPacket {
  float prev_x;
  float prev_y;
  uint8_t entry_desired_bottom_owner;
} MslMpcoll800471F8EscapeAirPacket;

typedef struct MslMpcollFloorPublication {
  uint8_t on_ground;
  uint8_t result_mode;
  uint16_t airborne_ground_id;
  MslMpcollFloorContact contact;
  float cur_bottom_x;
  float cur_bottom_y;
} MslMpcollFloorPublication;

typedef struct MslMpcollFinalFloorLineState {
  int line_idx;
  const MslStageFloorLine* line;
  uint8_t has_platform_transform;
  uint8_t has_height_platform_transform;
  uint8_t height_same_step_contact;
  uint8_t height_live_scheduler_source;
  uint8_t height_current_source;
  uint8_t line_y_valid;
  float line_y;
} MslMpcollFinalFloorLineState;

typedef struct MslMpcollFloorRejectPacket {
  uint64_t bits;
  uint32_t source_phases;
  uint32_t side_effects;
  uint8_t restore;
} MslMpcollFloorRejectPacket;

typedef struct MslMpcollFloorSweepResult {
  uint8_t hit;
  uint8_t mode;
  int hit_line_idx;
  int projected_line_idx;
  uint16_t hit_segment_id;
  uint16_t projected_segment_id;
  uint8_t hit_is_platform;
  uint8_t hit_is_ledge;
  uint8_t hit_has_platform_transform;
  uint8_t hit_has_height_platform_transform;
  uint8_t projected_is_platform;
  uint8_t projected_is_ledge;
  uint8_t projected_has_platform_transform;
  uint8_t projected_has_height_platform_transform;
  float hit_x;
  float hit_y;
  float projected_contact_x;
  float projected_contact_y;
  float projected_y_corr;
  float normal_x;
  float normal_y;
} MslMpcollFloorSweepResult;

typedef enum MslMpcollSourcePhase {
  MSL_MPCOLL_PHASE_AIR_471F8 = 1u << 0,
  MSL_MPCOLL_PHASE_AIR_473CC = 1u << 1,
  MSL_MPCOLL_PHASE_AIR_477E0 = 1u << 2,
  MSL_MPCOLL_PHASE_GROUND_B108 = 1u << 3,
  MSL_MPCOLL_PHASE_GROUNDED_4ACE4 = 1u << 4,
  MSL_MPCOLL_PHASE_GROUNDED_4A908_RETRY = 1u << 5,
  MSL_MPCOLL_PHASE_EDGE_SNAP = 1u << 6,
  MSL_MPCOLL_PHASE_PLATFORM_PASS = 1u << 7,
} MslMpcollSourcePhase;

typedef uint32_t MslMpcollSourcePhases;

enum {
  MSL_MPCOLL_FLOOR_PROBE_OWNER_NONE = 0u,
  MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8 = 1u,
  MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0 = 2u,
  MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14 = 3u,
  MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC = 4u,
};

enum {
  MSL_MPCOLL_FLOOR_PROBE_REJECT_NONE = 0u,
  MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER = 1u,
  MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_RUNTIME_PREV = 2u,
  MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP = 3u,
  MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER = 4u,
  MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION = 5u,
  MSL_MPCOLL_FLOOR_PROBE_ACCEPTED = 6u,
};

typedef struct MslMpcollCollDataState {
  uint16_t floor_index;
  uint16_t floor_skip_segment_id;
  MslMpcollSourcePhases source_phases;
} MslMpcollCollDataState;

typedef struct MslMpcollCarriedCliffLedgeFloorAuthority {
  uint8_t carried_floor_valid;
  uint8_t candidate_matches;
  uint8_t owner_live;
  uint8_t strict_span;
  uint8_t producer_start_span;
  uint8_t callback_bottom_root_accepted;
  uint8_t current_floor_matches;
  uint8_t desired_bottom_crosses_carried_floor;
  uint8_t desired_bottom_reaches_carried_floor;
  uint8_t live_bottom_sweep_authority;
  uint8_t live_desired_bottom_authority;
  uint8_t live_current_floor_continuation_authority;
  uint8_t current_floor_source_owned;
  uint8_t source_authority;
  uint8_t restored_only;
} MslMpcollCarriedCliffLedgeFloorAuthority;

typedef struct MslAttackAirPlatformEcbOwner {
  uint8_t shallow;
  uint8_t first_phase;
  uint8_t late;
} MslAttackAirPlatformEcbOwner;

// Coordinator-facing floor owner helpers split from mpcoll_ground.c.
uint8_t is_damage_collision_landing_action(uint16_t a);
uint8_t is_damage_fly_collision_action(uint16_t a);
uint8_t is_damage_ground_collision_action(uint16_t a);
uint8_t is_common_damage_ground_pose_ecb_action(uint16_t a);
uint8_t is_capture_lw_allow_ground_to_air_collision_action(uint16_t a);
uint8_t is_attackair_action(uint16_t a);
uint8_t is_spacie_air_special_floor_collision_action(uint8_t char_id, uint16_t a);
uint8_t is_common_fallspecial_action(uint16_t a);
uint8_t mpcoll_ground_escapeair_fall_iasa_source_owner(const MslBatch* batch, size_t idx);
uint8_t is_spacie_specialhi_end_fallspecial_source(uint8_t char_id, uint16_t a);
uint8_t is_spacie_sideb_air_end_fallspecial_source(uint8_t char_id, uint16_t a);
uint8_t action_uses_ftco_80096cc8_floor_callback(uint16_t a);
uint8_t damage_hitlag_exit_carry_source_is_thrown_needle(const MslBatch* batch, size_t idx);
uint8_t is_just_entered_specialairn_end_from_loop(uint8_t char_id, uint16_t action_id,
                                                  uint16_t prev_action_id, int16_t action_frame);
uint8_t damage_hitlag_floorhug_attempts_downward_sdi(const MslBatch* batch, size_t idx,
                                                     const MslCommonParams* c);
uint8_t action_uses_active_hitlag_downward_sdi_floorhug(uint16_t action_id, const MslBatch* batch,
                                                        size_t idx);
uint8_t mpcoll_damageair_action(uint16_t action_id);
uint8_t mpcoll_damage_active_hitlag_stay_airborne_floor_owner(
    const MslBatch* batch, size_t idx, uint16_t action_id, uint8_t prefer_line_valid,
    uint8_t prefer_line_is_platform, uint8_t prefer_line_is_ledge, uint8_t prefer_line_is_slope,
    uint8_t prefer_line_has_platform_transform, uint8_t prefer_line_is_fighter_solid,
    uint8_t prefer_line_is_terminal_cardinal_hard_floor, float prefer_line_root_y,
    float source_prev_root_y, uint8_t downdamage_x_axis_fresh_sdi_edge);
uint8_t grounded_damage_hitlag_allows_downward_floor_projection(const MslBatch* batch, size_t idx,
                                                                uint16_t action_id);
void stay_airborne_floor_projection_point(float fighter_x, float fighter_y, float bottom_x,
                                          float bottom_y, float* proj_x_out, float* proj_y_out);
uint8_t mpcoll_replay_rollout_advanced_past_reseed(const MslBatch* batch, int bi);
MslMpcollSourcePhases mpcoll_source_phases_for_motion_state(
    uint8_t char_id, uint16_t action_id, uint8_t ft_check_ground_ledge_uses_no_ledge_path);
uint8_t mpcoll_ft_check_ground_ledge_uses_no_ledge_path(const MslBatch* batch, size_t idx);
uint8_t mpcoll_source_phases_has(MslMpcollSourcePhases phases, MslMpcollSourcePhase phase);
uint8_t mpcoll_vanish_ft_check_jobj_ecb_owner(uint8_t char_id, uint16_t action_id,
                                              MslMpcollSourcePhases source_phases);
uint8_t mpcoll_source_phases_preserve_grounded_floor(MslMpcollSourcePhases phases);
uint8_t floor_line_y_at_x_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                  int line_idx, float x, float* y_out);
uint8_t floor_x_within_line_bounds(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                   int line_idx, float x);
uint8_t mpcoll_floor_sweep_prev_root_is_source_owned(const MslBatch* batch, size_t idx);
uint8_t mpcoll_floor_sweep_prev_root_is_runtime_owned(const MslBatch* batch, size_t idx);
void mpcoll_colldata_state_load(const MslMpcollContext* ctx, MslMpcollCollDataState* out,
                                MslMpcollSourcePhases source_phases);
void mpcoll_materialize_floor_publication_result(const MslMpcollContext* ctx,
                                                 const MslMpcollFloorPublication* publication);
uint8_t mpcoll_materialize_active_damage_hitlag_stay_airborne_floor(
    const MslMpcollContext* ctx, MslMpcollFloorPublication* publication);
uint8_t mpcoll_source_phases_allow_floor_edge_snap(MslMpcollSourcePhases phases);
uint8_t action_is_down_bound(uint16_t a);
uint8_t action_uses_landing_floor_release_coll(uint16_t a);
uint8_t floor_lines_connected(const MslStageFloorGraph* g, int a, int b);
uint8_t stage_floor_graph_has_height_platform_transform(uint32_t stage_id,
                                                        const MslStageFloorGraph* g);
uint8_t stage_height_platform_line_has_current_source(const MslBatch* batch, int bi,
                                                      uint32_t stage_id, uint16_t segment_i);
uint8_t stage_height_platform_line_has_same_step_contact_source(const MslBatch* batch, int bi,
                                                                uint32_t stage_id,
                                                                uint16_t segment_i);
uint8_t stage_height_platform_line_has_live_scheduler_source(const MslBatch* batch, int bi,
                                                             uint32_t stage_id, uint16_t segment_i);
MslStageFloorLine floor_line_world_for_env(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx);
uint8_t floor_x_within_line_segment_strict(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx, float x);
uint8_t floor_line_admitted_by_source_callback(const MslBatch* batch, size_t idx,
                                               const MslStageFloorGraph* g, uint32_t stage_id,
                                               int line_idx, uint16_t skip_platform_segment_i,
                                               const MslCommonParams* c);
uint8_t floor_line_normal_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                  int line_idx, float* nx_out, float* ny_out);
uint8_t active_damage_hard_floor_projection_source_accepted(const MslBatch* batch, size_t idx,
                                                            int bi, const MslStageFloorGraph* g,
                                                            int out_line_idx, float cur_bottom_x,
                                                            float cur_bottom_y,
                                                            uint8_t carried_source_floor_contact,
                                                            const MslCommonParams* c);
uint8_t active_damage_hitlag_ledge_edge_floorhug_owner(const MslBatch* batch, size_t idx, int bi,
                                                       const MslStageFloorGraph* g,
                                                       uint32_t stage_id, int line_idx,
                                                       float projection_x, const MslCommonParams* c,
                                                       float* edge_x_out, float* edge_y_out);
uint8_t grounded_height_platform_reproject(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, uint32_t stage_id,
                                           size_t idx, uint16_t action_id, uint16_t action_frame,
                                           int current_line_idx, int* out_line_idx,
                                           float* out_y_corr);
uint8_t hidden_height_platform_remaps_to_solid_floor(const MslBatch* batch, int bi,
                                                     const MslStageFloorGraph* g, uint32_t stage_id,
                                                     int current_line_idx, float root_x,
                                                     float root_y, int* out_line_idx);
uint8_t floor_line_x_near_endpoint_for_env(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx, float x,
                                           float max_dist);
uint8_t floor_find_low_raw_floor_contact(const MslBatch* batch, size_t idx, int bi,
                                         const MslStageFloorGraph* g, uint32_t stage_id, float x,
                                         float y, float max_lift, uint16_t skip_platform_segment_i,
                                         const MslCommonParams* c, int* out_line_idx, float* out_y,
                                         float* out_nx, float* out_ny);
uint8_t grounded_action_allows_height_platform_y_correction(uint16_t action_id,
                                                            uint16_t action_frame, uint8_t char_id);
uint8_t grounded_action_allows_stage_object_platform_carry(uint16_t action_id);
int mpcoll_find_current_randall_floor_line_at_root(const MslBatch* batch, int bi,
                                                   const MslStageFloorGraph* g, float root_x,
                                                   float root_y);
uint8_t grounded_entry_same_step_height_platform_admits_line(const MslBatch* batch, int bi,
                                                             size_t idx, uint32_t stage_id,
                                                             uint16_t action_id,
                                                             uint16_t selected_segment_i);
uint8_t floor_line_is_generated_stage_slope(const MslBatch* batch, int bi,
                                            const MslStageFloorGraph* g, int line_idx);
uint8_t floor_line_is_generated_sloped_ledge(const MslBatch* batch, int bi,
                                             const MslStageFloorGraph* g, int line_idx);
uint8_t floor_line_is_terminal_cardinal_hard_floor(const MslBatch* batch, int bi,
                                                   const MslStageFloorGraph* g, uint32_t stage_id,
                                                   int line_idx);
uint8_t fallspecial_sustained_same_terminal_cardinal_floor_delay(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t action_id, uint16_t seed_ground_id, uint16_t ground_id, int ground_line_idx,
    float contact_y, float root_y);
uint8_t floor_line_is_terminal_cardinal_ledge_floor(const MslBatch* batch, int bi,
                                                    const MslStageFloorGraph* g, uint32_t stage_id,
                                                    int line_idx);
uint8_t platform_pass_input_below_raw_threshold(const MslBatch* batch, size_t idx,
                                                const MslCommonParams* c);
void publish_common_air_transformed_platform_skip_from_root_crossing(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    MslMpcollSourcePhases source_phases, const MslCommonParams* c);
void publish_attackair_transformed_platform_skip_from_root_crossing(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    MslMpcollSourcePhases source_phases, const MslCommonParams* c);
uint8_t jumpaerial_terminal_fastfall_descent(const MslBatch* batch, size_t idx);
float specialhi_understage_floor_reject_clearance(const MslEcbWorldPoints* prev_ecb);
uint8_t specialhi_understage_floor_clip_action(uint8_t char_id, uint16_t action_id);
uint8_t specialhi_floor_candidate_starts_below_source_floor(uint8_t char_id, uint16_t action_id,
                                                            float prev_root_y, float prev_bottom_y,
                                                            float cur_bottom_y, float floor_y,
                                                            float speed_y_self);
uint8_t specialairhi_floor_contact_angle_continues_launch(uint16_t action_id, uint8_t char_id,
                                                          float floor_normal_x,
                                                          float floor_normal_y, float speed_x_self,
                                                          float speed_y_self);
float mpcoll_floor_projection_lift_allowance(const MslEcbWorldPoints* cur_ecb);
uint8_t grounded_persistence_allows_signed_dd90_y_correction(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, int current_line_idx,
    int projected_line_idx, size_t idx, uint16_t action_id, uint16_t action_frame);
void floor_ed5c_endpoints(const MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx,
                          float* x0_out, float* y0_out, float* x1_out, float* y1_out);
uint8_t floor_line_y_at_x_ed5c_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                       int line_idx, float x, float* y_out);
int msl_mplib_8004dd90_floor(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                             int line_idx, float x_in, float y_in, float* y_out, float* nx_out,
                             float* ny_out);
uint8_t mpcoll_grounded_final_root_flat_seam_remap(const MslMpcollContext* ctx,
                                                   MslMpcollFloorContact* contact);
void mpcoll_penultimate_interpolated_ecb(MslEcbWorldPoints* out,
                                         const MslEcbWorldPoints* step_count_start_ecb,
                                         const MslEcbWorldPoints* interpolation_start_ecb,
                                         const MslEcbWorldPoints* cur_ecb, float last_x,
                                         float last_y, float cur_x, float cur_y);
uint8_t grounded_sideb_substep_floor_loss(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, uint8_t char_id, uint16_t action_id,
    int prefer_line_idx, const MslEcbWorldPoints* prev_ecb, const MslEcbWorldPoints* cur_ecb,
    float prev_x, float prev_y, float cur_x, float cur_y, float* sub_prev_x_out,
    float* sub_prev_y_out, float* sub_cur_x_out, float* sub_cur_y_out,
    MslEcbWorldPoints* sub_cur_ecb_out);
uint16_t platform_floor_skip_segment_id(const MslBatch* batch, size_t idx, uint32_t stage_id);
uint8_t action_consumes_cliff_ledge_floor_owner(uint8_t char_id, uint16_t action_id);
MslMpcollCarriedCliffLedgeFloorAuthority mpcoll_carried_cliff_ledge_floor_authority(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint8_t char_id,
    uint16_t action_id, int candidate_line_idx, int raw_current_floor_line_idx, float root_x,
    float root_y, float candidate_floor_y, uint8_t ecb_lock_timer_seed,
    uint8_t callback_bottom_root_accepted);
MslAttackAirPlatformEcbOwner attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id);
uint8_t action_uses_shallow_attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id);
uint8_t action_uses_late_attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id);
MslMpcollFinalFloorLineState mpcoll_final_floor_line_state(const MslBatch* batch, int bi,
                                                           const MslStageFloorGraph* g,
                                                           uint32_t stage_id, uint16_t ground_id,
                                                           float root_x);
uint8_t mpcoll_attackairlw_air471f8_live_platform_publication_owner(
    const MslBatch* batch, size_t idx, const MslMpcollFinalFloorLineState* floor,
    uint16_t action_id, int16_t first_create_frame, int16_t second_create_frame,
    uint16_t skip_platform_segment_i);
uint8_t action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(uint8_t char_id,
                                                                   uint16_t action_id);
uint8_t floor_line_is_skipped_platform(uint32_t stage_id, const MslStageFloorGraph* g, int line_idx,
                                       uint16_t skip_segment_i);
void publish_attackair_transformed_platform_floor_skip_from_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslCommonParams* c, uint8_t force_source_owned, int line_idx, float x, float prev_y,
    float y);
uint8_t floor_line_is_runtime_fighter_solid(const MslStageFloorGraph* g, uint32_t stage_id,
                                            int line_idx);
uint8_t carried_floor_line_is_live_yoshi_shyguy_support(const MslBatch* batch, int bi,
                                                        const MslStageFloorGraph* g,
                                                        uint32_t stage_id, int line_idx);
uint8_t floor_intersect_horiz(float x0, float y0, float x1, float ax, float ay, float bx, float by,
                              float* ix_out, float* iy_out);
uint8_t wall_blocks_floor_edge_probe(const MslStageWallGraph* wg, float ax, float ay, float bx,
                                     float by);
uint32_t floor_edge_suppression_flags(MslBatch* batch, size_t idx, uint32_t stage_id,
                                      const MslStageFloorGraph* fg, int line_idx, uint8_t char_id,
                                      uint32_t anim, uint16_t ecb_frame, uint8_t was_grounded,
                                      const MslEcbWorldPoints* loaded_current_ecb);
void mpcoll_commit_grounded_floor_contact(const MslMpcollContext* ctx,
                                          const MslMpcollFloorContact* contact,
                                          uint8_t was_grounded);
void mpcoll_apply_final_floor_rejection_bits(const MslMpcollContext* ctx,
                                             MslMpcollFloorPublication* publication,
                                             MslMpcollFloorRejectPacket packet, float y,
                                             float cur_bottom_x, float cur_bottom_y,
                                             float cur_bot_rel_y,
                                             const MslEcbWorldPoints* prev_ecb_points,
                                             int final_ground_line_idx, float x, float prev_y);
void mpcoll_apply_late_floor_publication_guards(
    const MslMpcollContext* ctx, MslMpcollFloorPublication* publication,
    uint8_t ecb_lock_timer_seed, int prefer_line_idx, uint16_t seed_ground_id, float y,
    float prev_x, float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    const MslEcbWorldPoints* cur_ecb_points, int raw_current_floor_line_idx,
    uint8_t escapeair_stale_platform_root_handoff_hit,
    uint8_t escapeair_fresh_jump_height_platform_handoff_hit,
    uint8_t damage_active_hitlag_downward_sdi_airborne_owner,
    uint8_t damage_active_hitlag_root_below_bottom_above_floor_owner);
void mpcoll_commit_final_floor_state(const MslMpcollContext* ctx,
                                     const MslMpcollFloorPublication* publication);
uint8_t msl_mpcheck_hard_floor(const MslBatch* batch, size_t idx, int bi,
                               const MslStageFloorGraph* g, uint32_t stage_id, float ax, float ay,
                               float bx, float by, int prefer_line_idx, int skip_line_idx,
                               uint8_t admit_ledge, int* out_line_idx, float* out_ix, float* out_iy,
                               float* out_nx, float* out_ny);
uint16_t mpcoll_probe_segment_for_line(const MslStageFloorGraph* g, int line_idx);
uint8_t msl_mpcheck_floor(const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
                          uint32_t stage_id, float ax, float ay, float bx, float by,
                          uint16_t skip_platform_segment_i, int prefer_line_idx, int skip_line_idx,
                          const MslCommonParams* c, int* out_line_idx, float* out_ix, float* out_iy,
                          float* out_nx, float* out_ny);

void mpcoll_floor_probe_clear(MslBatch* batch, size_t idx);
void mpcoll_floor_probe_begin(const MslMpcollContext* ctx, uint8_t owner,
                              MslMpcollSourcePhases source_phases, int candidate_line_idx,
                              uint8_t reject_reason);
void mpcoll_floor_probe_lines(const MslMpcollContext* ctx, int candidate_line_idx,
                              int projected_line_idx);
void mpcoll_floor_probe_reject_bits(const MslMpcollContext* ctx, uint64_t bits,
                                    MslMpcollSourcePhases source_phases, int candidate_line_idx,
                                    int projected_line_idx);
void mpcoll_floor_probe_bottom_interval(const MslMpcollContext* ctx, float prev_bottom_x,
                                        float prev_bottom_y, float cur_bottom_x,
                                        float cur_bottom_y);
void mpcoll_floor_probe_result(const MslMpcollContext* ctx, const MslMpcollFloorSweepResult* sweep,
                               uint8_t raw_hit, uint8_t projection_hit, uint8_t reject_reason);
void mpcoll_discard_callback_floor_result(const MslMpcollContext* ctx);
void mpcoll_clear_callback_floor_result(const MslMpcollContext* ctx, float prev_x, float prev_y,
                                        float cur_x, float cur_y);
void mpcoll_record_callback_floor_result_with_mode(const MslMpcollContext* ctx, uint8_t source,
                                                   uint8_t mode, uint16_t segment_id,
                                                   float contact_x, float contact_y, float normal_x,
                                                   float normal_y);
void mpcoll_record_callback_floor_result(const MslMpcollContext* ctx, uint8_t source,
                                         uint16_t segment_id, float contact_x, float contact_y,
                                         float normal_x, float normal_y);
void mpcoll_record_escapeair_floor_producer_runtime_authority(const MslMpcollContext* ctx);
uint8_t mpcoll_callback_floor_result_valid(const MslMpcollContext* ctx);
uint8_t mpcoll_floor_contact_from_callback_result(const MslMpcollContext* ctx,
                                                  MslMpcollFloorContact* io);

static inline void mpcoll_floor_reject_add_if_state(MslMpcollFloorRejectPacket* packet,
                                                    uint8_t condition, uint64_t bit,
                                                    MslMpcollFloorRejectRestore restore,
                                                    uint32_t side_effects, uint32_t source_phases) {
  if (packet != NULL && condition) {
    packet->bits |= bit;
    packet->side_effects |= side_effects;
    packet->source_phases |= source_phases;
    if (packet->restore == (uint8_t)MSL_MPCOLL_FLOOR_REJECT_RESTORE_NONE) {
      packet->restore = (uint8_t)restore;
    }
  }
}
void mpcoll_floor_reject_add_escapeair_final_owners(
    MslMpcollFloorRejectPacket* packet, const MslEscapeAirFinalPublicationOwners* owners);

uint8_t mpcoll_collect_bottom_sweep_hit(const MslBatch* batch, size_t idx, int bi,
                                        const MslStageFloorGraph* g, uint32_t stage_id,
                                        float prev_bottom_x, float prev_bottom_y,
                                        float cur_bottom_x, float cur_bottom_y,
                                        uint16_t skip_platform_segment_i, int prefer_line_idx,
                                        int skip_line_idx, const MslCommonParams* c,
                                        MslMpcollFloorSweepResult* out);
void mpcoll_project_bottom_sweep_floor_result(const MslBatch* batch, int bi,
                                              const MslStageFloorGraph* g, uint32_t stage_id,
                                              float cur_bottom_y, MslMpcollFloorSweepResult* out);
uint8_t mpcoll_collect_bottom_sweep_floor_result(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    uint16_t skip_platform_segment_i, int prefer_line_idx, int skip_line_idx,
    const MslCommonParams* c, MslMpcollFloorSweepResult* out);
uint8_t mpcoll_collect_bottom_sweep_hard_floor_result(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    int prefer_line_idx, int skip_line_idx, uint8_t admit_ledge, MslMpcollFloorSweepResult* out);
uint8_t mpcoll_bottom_sweep_hits_segment(const MslBatch* batch, size_t idx, int bi,
                                         const MslStageFloorGraph* g, uint32_t stage_id,
                                         float prev_bottom_x, float prev_bottom_y,
                                         float cur_bottom_x, float cur_bottom_y,
                                         uint16_t skip_platform_segment_i, int prefer_line_idx,
                                         int skip_line_idx, const MslCommonParams* c,
                                         uint16_t target_segment_id);

// Coordinator-facing floor callback helpers.
uint8_t msl_mpcoll_80044838_floor_edge_snap_from_bottom(
    MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx, float cur_bottom_x,
    float cur_bottom_y, uint8_t allow_hard_floor, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_80044628_floor_wall_adjacent_fallback(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslMpcollOrderedWallCeilResult* wall_ceil, float cur_bottom_x, float cur_bottom_y,
    uint16_t skip_platform_segment_i, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_8004a45c_floor_edge_snap(MslBatch* batch, size_t idx, int bi,
                                            const MslStageFloorGraph* g, uint32_t stage_id,
                                            int line_idx, float cur_bottom_x, uint8_t char_id,
                                            uint32_t anim, uint16_t ecb_frame, uint8_t was_grounded,
                                            uint16_t* ground_id_out, float* contact_x_out,
                                            float* contact_y_out, float* floor_nx_out,
                                            float* floor_ny_out);
uint8_t msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(const MslBatch* batch, size_t idx,
                                                              int bi, const MslStageFloorGraph* g,
                                                              uint32_t stage_id, uint16_t action_id,
                                                              uint16_t current_ground_id,
                                                              uint16_t carried_ground_id,
                                                              int carried_line_idx);
uint8_t msl_mpcoll_8004b108_downbound_project_attack_speed(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint8_t was_grounded, uint16_t floor_segment_id, const MslMpcollFloorContact* source_contact);
MslMpcoll800471F8EscapeAirPacket msl_mpcoll_800471f8_escapeair_packet(const MslMpcollContext* ctx,
                                                                      uint8_t ecb_lock_active,
                                                                      uint8_t ecb_lock_timer_seed,
                                                                      float fallback_prev_x,
                                                                      float fallback_prev_y);
uint8_t msl_mpcoll_800471f8_escapeair_entry_floor_publication(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint8_t was_grounded, uint8_t stage_has_only_static_cardinal_hard_floors,
    uint8_t stage_has_height_platform_transform, uint8_t ecb_lock_timer_seed, float prev_x,
    float prev_y, float x, float y, float escapeair_bottom_rel0, uint16_t skip_platform_segment_i,
    const MslCommonParams* c, MslMpcollFloorHit* out);
uint8_t fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(
    const MslBatch* batch, size_t idx, const MslStageFloorGraph* g, uint32_t stage_id);
uint8_t msl_mpcoll_80047e14_fallspecial_prephysics_floor_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_rel_y, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_80047e14_common_air_hard_floor_bottom_sweep(
    const MslMpcollContext* ctx, const MslBatch* batch, size_t idx, int bi,
    const MslStageFloorGraph* g, uint32_t stage_id, float prev_bottom_x, float prev_bottom_y,
    float cur_bottom_x, float cur_bottom_y, int prefer_line_idx, uint16_t skip_platform_segment_i,
    const MslCommonParams* c, uint16_t* ground_id_out, float* y_corr_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_800473cc_damage_stay_airborne_hard_floor_sweep(
    const MslMpcollContext* ctx, MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
    uint32_t stage_id, MslMpcollSourcePhases source_phases, float prev_bottom_x,
    float prev_bottom_y, float cur_bottom_x, float cur_bottom_y, int prefer_line_idx,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out);
uint8_t msl_mpcoll_80047e14_flags6_root_floor_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_root_x, float prev_root_y, float cur_root_x, float cur_root_y, float cur_bottom_y,
    uint16_t terminal_source_action_id, int prefer_line_idx, uint16_t skip_platform_segment_i,
    uint8_t ecb_lock_timer_seed, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_80047e14_fallspecial_connected_hard_floor_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_root_y, float cur_root_x, float cur_root_y, int prefer_line_idx,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out);
uint8_t msl_mpcoll_80047e14_reject_fall_same_floor_early_final_land(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t action_id, uint16_t seed_ground_id, uint16_t ground_id, int final_ground_line_idx,
    float contact_y, float root_y);
uint8_t floor_4a908_retry(MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
                          uint32_t stage_id, int persisted_line_idx, float prev_bottom_x,
                          float prev_bottom_y, float prev_side_mid_y, float cur_bottom_x,
                          float cur_bottom_y, uint16_t skip_platform_segment_i,
                          uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out,
                          float* floor_nx_out, float* floor_ny_out);
uint8_t attackair_flags0_floor_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslEcbWorldPoints* prev_ecb, const MslEcbWorldPoints* cur_ecb, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out);
uint8_t msl_mpcoll_800471f8_escapeair_locked_root_publication(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint8_t escapeair_locked, uint8_t ecb_lock_timer, uint8_t ecb_lock_timer_seed,
    uint8_t stage_has_height_platform_transform, const MslEcbWorldPoints* prev_ecb,
    const MslEcbWorldPoints* cur_ecb, uint16_t skip_platform_segment_i, int prefer_line_idx,
    uint8_t char_id, uint32_t anim, uint16_t ecb_frame_cur, uint8_t locked_desired_ecb_bottom_valid,
    const MslCommonParams* c, MslMpcollFloorContact* out, uint8_t* platform_root_hit_out);
