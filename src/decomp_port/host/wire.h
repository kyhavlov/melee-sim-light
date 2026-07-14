#ifndef MSL_DECOMP_PORT_WIRE_H
#define MSL_DECOMP_PORT_WIRE_H

#include <stddef.h>
#include <stdint.h>

enum {
    MSL_DP_MAX_PLAYERS = 4,
    MSL_DP_MAX_ITEMS = 15,
    MSL_DP_STATE_FLAGS_BYTES = 5,
};

#pragma pack(push, 1)

typedef struct MslDpInputPlayer {
    uint16_t buttons;
    int8_t main_x;
    int8_t main_y;
    int8_t c_x;
    int8_t c_y;
    uint8_t l;
    uint8_t r;
} MslDpInputPlayer;

typedef struct MslDpInput {
    MslDpInputPlayer p[MSL_DP_MAX_PLAYERS];
} MslDpInput;

typedef struct MslDpStreamFrame {
    uint32_t frame_pre_random_seed;
    MslDpInput input;
} MslDpStreamFrame;

typedef struct MslDpMatchPlayerConfig {
    uint8_t char_id;
    uint8_t team_id;
    uint8_t facing;
    uint8_t costume_id;
} MslDpMatchPlayerConfig;

typedef struct MslDpMatchConfig {
    uint32_t stage_id;
    int32_t frame_id;
    uint32_t frame_pre_random_seed;
    float match_damage_ratio;
    uint8_t num_players;
    uint8_t is_teams;
    uint8_t stock_count;
    uint8_t camera_mode;
    MslDpMatchPlayerConfig players[MSL_DP_MAX_PLAYERS];
} MslDpMatchConfig;

typedef struct MslDpItem {
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
} MslDpItem;

typedef struct MslDpCompare {
    int32_t frame_id;
    uint32_t frame_pre_random_seed;
    uint32_t stage_id;
    uint8_t num_players;
    uint8_t is_teams;
    uint8_t _pad0[2];
    uint8_t team_id[MSL_DP_MAX_PLAYERS];
    uint8_t char_id[MSL_DP_MAX_PLAYERS];
    float pos_x[MSL_DP_MAX_PLAYERS];
    float pos_y[MSL_DP_MAX_PLAYERS];
    float speed_air_x_self[MSL_DP_MAX_PLAYERS];
    float speed_ground_x_self[MSL_DP_MAX_PLAYERS];
    float speed_y_self[MSL_DP_MAX_PLAYERS];
    float speed_x_attack[MSL_DP_MAX_PLAYERS];
    float speed_y_attack[MSL_DP_MAX_PLAYERS];
    uint8_t facing[MSL_DP_MAX_PLAYERS];
    uint8_t on_ground[MSL_DP_MAX_PLAYERS];
    uint8_t is_dead[MSL_DP_MAX_PLAYERS];
    uint8_t _pad1[1];
    uint16_t action_id[MSL_DP_MAX_PLAYERS];
    int16_t action_frame[MSL_DP_MAX_PLAYERS];
    uint8_t jumps_left[MSL_DP_MAX_PLAYERS];
    uint8_t stocks[MSL_DP_MAX_PLAYERS];
    float percent[MSL_DP_MAX_PLAYERS];
    float shield_hp[MSL_DP_MAX_PLAYERS];
    uint16_t hitlag[MSL_DP_MAX_PLAYERS];
    uint16_t hitstun[MSL_DP_MAX_PLAYERS];
    uint8_t l_cancel[MSL_DP_MAX_PLAYERS];
    uint8_t hurtbox_state[MSL_DP_MAX_PLAYERS];
    uint16_t ground_id[MSL_DP_MAX_PLAYERS];
    uint32_t animation_index[MSL_DP_MAX_PLAYERS];
    uint16_t instance_hit_by[MSL_DP_MAX_PLAYERS];
    uint16_t instance_id[MSL_DP_MAX_PLAYERS];
    uint8_t last_attack_landed[MSL_DP_MAX_PLAYERS];
    uint8_t combo_count[MSL_DP_MAX_PLAYERS];
    uint8_t last_hit_by[MSL_DP_MAX_PLAYERS];
    uint8_t _pad2[1];
    uint8_t state_flags[MSL_DP_MAX_PLAYERS][MSL_DP_STATE_FLAGS_BYTES];
    MslDpItem items[MSL_DP_MAX_ITEMS];
} MslDpCompare;

#pragma pack(pop)

_Static_assert(sizeof(MslDpInputPlayer) == 8, "MslInputPlayer wire size");
_Static_assert(sizeof(MslDpInput) == 32, "MslInput wire size");
_Static_assert(sizeof(MslDpStreamFrame) == 36, "stream frame wire size");
_Static_assert(sizeof(MslDpMatchConfig) == 36, "MslMatchConfig wire size");
_Static_assert(sizeof(MslDpItem) == 48, "MslItem wire size");
_Static_assert(sizeof(MslDpCompare) == 1022, "MslCompare wire size");

uint16_t msl_dp_get_le16(const void* ptr);
uint32_t msl_dp_get_le32(const void* ptr);
float msl_dp_get_lef32(const void* ptr);
void msl_dp_put_le16(void* ptr, uint16_t value);
void msl_dp_put_le32(void* ptr, uint32_t value);
void msl_dp_put_lef32(void* ptr, float value);

#endif
