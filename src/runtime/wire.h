#ifndef MSL_CORE_RUNTIME_WIRE_H
#define MSL_CORE_RUNTIME_WIRE_H

#include "api.h"

#include <stddef.h>
#include <stdint.h>

enum {
    MSL_CORE_MAX_PLAYERS = MSL_MAX_PLAYERS,
    MSL_CORE_MAX_ITEMS = MSL_MAX_ITEMS,
    MSL_CORE_MAX_HITBOXES = 4,
    MSL_CORE_STATE_FLAGS_BYTES = 5,
};

#pragma pack(push, 1)

typedef struct MslCoreInputPlayer {
    uint16_t buttons;
    // Physical signed bytes retained for UCF's raw-input consumers.
    int8_t main_x;
    int8_t main_y;
    int8_t c_x;
    int8_t c_y;
    uint8_t l;
    uint8_t r;
    // Optional post-clamp HSD axes. Slippi playback restores these floats to
    // Fighter input state independently of the physical bytes above.
    int8_t nml_main_x;
    int8_t nml_main_y;
    int8_t nml_c_x;
    int8_t nml_c_y;
    uint8_t nml_valid;
} MslCoreInputPlayer;

enum {
    MSL_CORE_INPUT_NML_MAIN_VALID = 1 << 0,
    MSL_CORE_INPUT_NML_C_VALID = 1 << 1,
};

typedef struct MslCoreInput {
    MslCoreInputPlayer p[MSL_CORE_MAX_PLAYERS];
} MslCoreInput;

typedef struct MslCoreStageEvents {
    float fod_platform_height[2];
    // Slippi records the global RNG both at the frame-start scheduler boundary
    // and again when the first fighter input callback begins. The latter
    // closes over earlier stage/effect consumers without making them fighter
    // logic.
    uint32_t fighter_pre_random_seed;
    uint8_t fod_platform_mask;
    uint8_t dreamland_whispy_valid;
    uint8_t dreamland_whispy_direction;
    uint8_t fighter_pre_random_seed_valid;
} MslCoreStageEvents;

typedef struct MslCoreStreamFrame {
    uint32_t frame_pre_random_seed;
    MslCoreInput input;
    MslCoreStageEvents stage_events;
} MslCoreStreamFrame;

typedef struct MslCoreMatchPlayerConfig {
    uint8_t char_id;
    uint8_t team_id;
    // Bit 0 is facing. Bits 1..3 optionally carry the one-based physical
    // controller port; zero keeps the compact legacy slot default.
    uint8_t facing_and_port;
    uint8_t costume_id;
    uint8_t handicap;
} MslCoreMatchPlayerConfig;

typedef struct MslCoreMatchConfig {
    uint32_t stage_id;
    int32_t frame_id;
    uint32_t frame_pre_random_seed;
    // Slippi's game-start event records the seed restored before match/stage
    // construction. Online frame seeds subsequently add a per-frame high-word
    // offset and must not initialize persistent stage state.
    uint32_t initial_random_seed;
    float match_damage_ratio;
    uint8_t num_players;
    uint8_t is_teams;
    uint8_t friendly_fire;
    uint8_t stock_count;
    uint8_t camera_mode;
    // Runtime capabilities are explicit because scene major alone does not
    // distinguish Slippi's offline Dolphin patch set from Nintendont retail.
    uint8_t online_fnmsubs_zero;
    uint8_t brawl_offscreen_damage;
    uint8_t freeze_dead_up_fall_physics;
    // UCF 0.84 and its 1.0 cardinal-coordinate patch are independently
    // selectable in real Slippi recordings. Older recordings may contain
    // UCF dashback/shield-drop behavior without cardinal snapping.
    // refs/slippi-ssbm-asm/Output/InjectionLists/list_netplay.json
    uint8_t ucf_cardinals_1_0_enabled;
    // UCF's shield-SDI injection was not present in every Slippi UCF profile.
    // Keep it independent of dashback, shield drop, and cardinal snapping so
    // older recordings do not receive a second, unrecorded SDI displacement.
    // refs/slippi-ssbm-asm/External/UCF 0.84/UCF/UCF Shield SDI.asm
    uint8_t ucf_shield_sdi_enabled;
    // UCF's ordinary hitlag-SDI injection is a separate rollout/capability
    // from shield SDI and from the original dashback/shield-drop mechanics.
    // refs/slippi-ssbm-asm/External/UCF 0.84/UCF/UCF SDI.asm
    uint8_t ucf_sdi_enabled;
    // Bit 0: Slippi 3.18+ FoD platform events; bit 1: Dream Land Whispy
    // direction events. A present stream is authoritative even on frames with
    // no event, where the last published value remains active.
    uint8_t stage_event_streams;
    MslCoreMatchPlayerConfig players[MSL_CORE_MAX_PLAYERS];
} MslCoreMatchConfig;

// Native validation workers keep immutable GameData alive across jobs. The
// frame count gives each job an explicit boundary without closing the stream;
// config remains the same little-endian wire consumed by the one-shot runner.
typedef struct MslCoreStreamJobHeader {
    uint32_t frame_count;
    MslCoreMatchConfig config;
    MslCoreInput previous;
} MslCoreStreamJobHeader;

typedef MslItem MslCoreItem;

typedef struct MslCoreCompare {
    int32_t frame_id;
    uint32_t frame_pre_random_seed;
    uint32_t stage_id;
    uint8_t num_players;
    uint8_t is_teams;
    uint8_t _pad0[2];
    uint8_t team_id[MSL_CORE_MAX_PLAYERS];
    uint8_t char_id[MSL_CORE_MAX_PLAYERS];
    float pos_x[MSL_CORE_MAX_PLAYERS];
    float pos_y[MSL_CORE_MAX_PLAYERS];
    float speed_air_x_self[MSL_CORE_MAX_PLAYERS];
    float speed_ground_x_self[MSL_CORE_MAX_PLAYERS];
    float speed_y_self[MSL_CORE_MAX_PLAYERS];
    float speed_x_attack[MSL_CORE_MAX_PLAYERS];
    float speed_y_attack[MSL_CORE_MAX_PLAYERS];
    uint8_t facing[MSL_CORE_MAX_PLAYERS];
    uint8_t on_ground[MSL_CORE_MAX_PLAYERS];
    uint8_t is_dead[MSL_CORE_MAX_PLAYERS];
    uint8_t _pad1[1];
    uint16_t action_id[MSL_CORE_MAX_PLAYERS];
    int16_t action_frame[MSL_CORE_MAX_PLAYERS];
    uint8_t jumps_left[MSL_CORE_MAX_PLAYERS];
    uint8_t stocks[MSL_CORE_MAX_PLAYERS];
    float percent[MSL_CORE_MAX_PLAYERS];
    float shield_hp[MSL_CORE_MAX_PLAYERS];
    uint16_t hitlag[MSL_CORE_MAX_PLAYERS];
    uint16_t hitstun[MSL_CORE_MAX_PLAYERS];
    uint8_t l_cancel[MSL_CORE_MAX_PLAYERS];
    uint8_t hurtbox_state[MSL_CORE_MAX_PLAYERS];
    uint16_t ground_id[MSL_CORE_MAX_PLAYERS];
    uint32_t animation_index[MSL_CORE_MAX_PLAYERS];
    uint16_t instance_hit_by[MSL_CORE_MAX_PLAYERS];
    uint16_t instance_id[MSL_CORE_MAX_PLAYERS];
    uint8_t last_attack_landed[MSL_CORE_MAX_PLAYERS];
    uint8_t combo_count[MSL_CORE_MAX_PLAYERS];
    uint8_t last_hit_by[MSL_CORE_MAX_PLAYERS];
    uint8_t _pad2[1];
    uint8_t state_flags[MSL_CORE_MAX_PLAYERS][MSL_CORE_STATE_FLAGS_BYTES];
    // Follower (Ice Climbers Nana) lanes mirror the leader lanes above.
    // Slippi records follower pre/post rows only while the follower fighter
    // is awake: her Dead* animation frames are still recorded, rows stop when
    // she enters ftCo_MS_Sleep (fp->x221F_b3) and resume with the leader's
    // Rebirth. follower_present owns that life-cycle; every other follower
    // lane is zero while absent. Slippi has no follower team/elimination
    // observation, so there are no follower team_id/is_dead lanes.
    // refs/melee/src/melee/ft/ftcolanim.c::ftCo_800BFD04
    uint8_t follower_present[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_char_id[MSL_CORE_MAX_PLAYERS];
    float follower_pos_x[MSL_CORE_MAX_PLAYERS];
    float follower_pos_y[MSL_CORE_MAX_PLAYERS];
    float follower_speed_air_x_self[MSL_CORE_MAX_PLAYERS];
    float follower_speed_ground_x_self[MSL_CORE_MAX_PLAYERS];
    float follower_speed_y_self[MSL_CORE_MAX_PLAYERS];
    float follower_speed_x_attack[MSL_CORE_MAX_PLAYERS];
    float follower_speed_y_attack[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_facing[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_on_ground[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_action_id[MSL_CORE_MAX_PLAYERS];
    int16_t follower_action_frame[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_jumps_left[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_stocks[MSL_CORE_MAX_PLAYERS];
    float follower_percent[MSL_CORE_MAX_PLAYERS];
    float follower_shield_hp[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_hitlag[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_hitstun[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_l_cancel[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_hurtbox_state[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_ground_id[MSL_CORE_MAX_PLAYERS];
    uint32_t follower_animation_index[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_instance_hit_by[MSL_CORE_MAX_PLAYERS];
    uint16_t follower_instance_id[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_last_attack_landed[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_combo_count[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_last_hit_by[MSL_CORE_MAX_PLAYERS];
    uint8_t follower_state_flags[MSL_CORE_MAX_PLAYERS]
                                [MSL_CORE_STATE_FLAGS_BYTES];
    MslCoreItem items[MSL_CORE_MAX_ITEMS];
} MslCoreCompare;

typedef MslObservationPlayer MslCoreObservationPlayer;
typedef MslObservationRandall MslCoreObservationRandall;
typedef MslObservationFodPlatforms MslCoreObservationFodPlatforms;
typedef MslObservationStage MslCoreObservationStage;
typedef MslObservation MslCoreObservation;
typedef MslTerminal MslCoreTerminal;

// Stable production projection used by interactive consumers. This is a
// separate schema from MslCoreCompare: replay-forensic lanes can evolve
// independently without becoming part of the render/API contract.
typedef struct MslCoreViewerHitbox {
    float x;
    float y;
    float z;
    float radius;
    float damage;
    uint16_t bone_part_id;
    uint8_t enabled;
    uint8_t _pad0;
} MslCoreViewerHitbox;

typedef struct MslCoreViewerPlayer {
    uint8_t char_id;
    uint8_t team_id;
    uint8_t facing;
    uint8_t on_ground;
    uint8_t is_dead;
    uint8_t jumps_left;
    uint8_t stocks;
    uint8_t hurtbox_state;
    uint8_t l_cancel;
    uint8_t last_attack_landed;
    uint8_t combo_count;
    uint8_t last_hit_by;
    uint8_t state_flags[MSL_CORE_STATE_FLAGS_BYTES];
    uint8_t _pad0[3];
    uint16_t action_id;
    int16_t action_frame;
    uint16_t hitlag;
    uint16_t hitstun;
    uint16_t ground_id;
    uint16_t _pad1;
    uint32_t animation_index;
    float pos_x;
    float pos_y;
    float speed_air_x_self;
    float speed_ground_x_self;
    float speed_y_self;
    float speed_x_attack;
    float speed_y_attack;
    float percent;
    float shield_hp;
    float shield_x;
    float shield_y;
    float shield_z;
    float shield_radius;
    // Unit-space visual direction from ftCo Guard's smoothed angle/magnitude.
    // The viewer combines this with its per-character render offset; it is not
    // a gameplay collision coordinate.
    float shield_tilt_x;
    float shield_tilt_y;
    MslCoreViewerHitbox hitboxes[MSL_CORE_MAX_HITBOXES];
} MslCoreViewerPlayer;

typedef struct MslCoreViewerStage {
    // Source actor order is 0=right and 1=left, matching Slippi's FoD event
    // protocol and the existing viewer adapter.
    float fod_platform_height[2];
    uint8_t fod_platform_valid[2];
    uint8_t randall_exists;
    uint8_t _pad0;
    float randall_x;
    float randall_y;
} MslCoreViewerStage;

typedef struct MslCoreViewerCamera {
    float eye_x;
    float eye_y;
    float eye_z;
    float interest_x;
    float interest_y;
    float interest_z;
    float fov;
} MslCoreViewerCamera;

typedef struct MslCoreViewerState {
    int32_t frame_id;
    uint32_t random_seed;
    uint32_t stage_id;
    float damage_ratio;
    uint8_t num_players;
    uint8_t is_teams;
    uint8_t friendly_fire;
    uint8_t terminal;
    uint8_t stock_count;
    uint8_t _pad0[3];
    MslCoreViewerPlayer players[MSL_CORE_MAX_PLAYERS];
    MslCoreItem items[MSL_CORE_MAX_ITEMS];
    MslCoreViewerStage stage;
    MslCoreViewerCamera camera;
} MslCoreViewerState;

#pragma pack(pop)

_Static_assert(sizeof(MslCoreInputPlayer) == 13, "MslCoreInputPlayer wire size");
_Static_assert(sizeof(MslCoreInput) == 52, "MslCoreInput wire size");
_Static_assert(sizeof(MslCoreStageEvents) == 16, "stage events wire size");
_Static_assert(sizeof(MslCoreStreamFrame) == 72, "stream frame wire size");
_Static_assert(sizeof(MslCoreMatchConfig) == 52,
               "MslCoreMatchConfig wire size");
_Static_assert(sizeof(MslCoreStreamJobHeader) == 108,
               "stream job header wire size");
_Static_assert(sizeof(MslCoreItem) == 48, "MslCoreItem wire size");
_Static_assert(sizeof(MslCoreCompare) == 1302, "MslCoreCompare wire size");
_Static_assert(sizeof(MslCoreObservationPlayer) == 56,
               "MslCoreObservationPlayer wire size");
_Static_assert(sizeof(MslCoreObservationStage) == 20,
               "MslCoreObservationStage wire size");
_Static_assert(sizeof(MslCoreObservation) == 980,
               "MslCoreObservation wire size");
_Static_assert(sizeof(MslCoreTerminal) == 16,
               "MslCoreTerminal wire size");
_Static_assert(sizeof(MslCoreViewerHitbox) == 24,
               "MslCoreViewerHitbox wire size");
_Static_assert(sizeof(MslCoreViewerPlayer) == 192,
               "MslCoreViewerPlayer wire size");
_Static_assert(sizeof(MslCoreViewerStage) == 20,
               "MslCoreViewerStage wire size");
_Static_assert(sizeof(MslCoreViewerCamera) == 28,
               "MslCoreViewerCamera wire size");
_Static_assert(sizeof(MslCoreViewerState) == 1560,
               "MslCoreViewerState wire size");

uint16_t msl_core_get_le16(const void* ptr);
uint32_t msl_core_get_le32(const void* ptr);
float msl_core_get_lef32(const void* ptr);
void msl_core_put_le16(void* ptr, uint16_t value);
void msl_core_put_le32(void* ptr, uint32_t value);
void msl_core_put_lef32(void* ptr, float value);
void msl_core_decode_match_config(MslCoreMatchConfig* config,
                                  const uint8_t* wire);
void msl_core_decode_stage_events(MslCoreStageEvents* events,
                                  const uint8_t* wire);

#endif
