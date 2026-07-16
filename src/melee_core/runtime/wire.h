#ifndef MSL_CORE_RUNTIME_WIRE_H
#define MSL_CORE_RUNTIME_WIRE_H

#include <stddef.h>
#include <stdint.h>

enum {
    MSL_CORE_MAX_PLAYERS = 4,
    MSL_CORE_MAX_ITEMS = 15,
    MSL_CORE_STATE_FLAGS_BYTES = 5,
};

#pragma pack(push, 1)

typedef struct MslCoreInputPlayer {
    uint16_t buttons;
    int8_t main_x;
    int8_t main_y;
    int8_t c_x;
    int8_t c_y;
    uint8_t l;
    uint8_t r;
} MslCoreInputPlayer;

typedef struct MslCoreInput {
    MslCoreInputPlayer p[MSL_CORE_MAX_PLAYERS];
} MslCoreInput;

typedef struct MslCoreStageEvents {
    float fod_platform_height[2];
    uint8_t fod_platform_mask;
    uint8_t dreamland_whispy_valid;
    uint8_t dreamland_whispy_direction;
    uint8_t _pad0;
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
    // Bit 0: Slippi 3.18+ FoD platform events; bit 1: Dream Land Whispy
    // direction events. A present stream is authoritative even on frames with
    // no event, where the last published value remains active.
    uint8_t stage_event_streams;
    MslCoreMatchPlayerConfig players[MSL_CORE_MAX_PLAYERS];
} MslCoreMatchConfig;

typedef struct MslCoreItem {
    uint8_t exists;
    uint8_t state;
    uint16_t type;
    int8_t owner;
    uint8_t _pad0;
    uint16_t instance_id;
    uint16_t attack_id;
    uint16_t attack_instance;
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
} MslCoreItem;

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
    MslCoreItem items[MSL_CORE_MAX_ITEMS];
} MslCoreCompare;

#pragma pack(pop)

_Static_assert(sizeof(MslCoreInputPlayer) == 8, "MslCoreInputPlayer wire size");
_Static_assert(sizeof(MslCoreInput) == 32, "MslCoreInput wire size");
_Static_assert(sizeof(MslCoreStageEvents) == 12, "stage events wire size");
_Static_assert(sizeof(MslCoreStreamFrame) == 48, "stream frame wire size");
_Static_assert(sizeof(MslCoreMatchConfig) == 49, "MslCoreMatchConfig wire size");
_Static_assert(sizeof(MslCoreItem) == 48, "MslCoreItem wire size");
_Static_assert(sizeof(MslCoreCompare) == 1022, "MslCoreCompare wire size");

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
