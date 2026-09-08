#include "runtime/wire.h"

#include <stddef.h>
#include <string.h>

void msl_core_decode_match_config(MslCoreMatchConfig* config,
                                  const uint8_t* wire)
{
    memset(config, 0, sizeof(*config));
    config->stage_id =
        msl_core_get_le32(wire + offsetof(MslCoreMatchConfig, stage_id));
    config->frame_id = (int32_t) msl_core_get_le32(
        wire + offsetof(MslCoreMatchConfig, frame_id));
    config->frame_pre_random_seed = msl_core_get_le32(
        wire + offsetof(MslCoreMatchConfig, frame_pre_random_seed));
    config->initial_random_seed = msl_core_get_le32(
        wire + offsetof(MslCoreMatchConfig, initial_random_seed));
    config->match_damage_ratio = msl_core_get_lef32(
        wire + offsetof(MslCoreMatchConfig, match_damage_ratio));
    config->num_players = wire[offsetof(MslCoreMatchConfig, num_players)];
    config->is_teams = wire[offsetof(MslCoreMatchConfig, is_teams)];
    config->friendly_fire = wire[offsetof(MslCoreMatchConfig, friendly_fire)];
    config->stock_count = wire[offsetof(MslCoreMatchConfig, stock_count)];
    config->camera_mode = wire[offsetof(MslCoreMatchConfig, camera_mode)];
    config->online_fnmsubs_zero =
        wire[offsetof(MslCoreMatchConfig, online_fnmsubs_zero)];
    config->brawl_offscreen_damage =
        wire[offsetof(MslCoreMatchConfig, brawl_offscreen_damage)];
    config->freeze_dead_up_fall_physics =
        wire[offsetof(MslCoreMatchConfig, freeze_dead_up_fall_physics)];
    config->ucf_cardinals_1_0_enabled =
        wire[offsetof(MslCoreMatchConfig, ucf_cardinals_1_0_enabled)];
    config->ucf_shield_sdi_enabled =
        wire[offsetof(MslCoreMatchConfig, ucf_shield_sdi_enabled)];
    config->ucf_sdi_enabled =
        wire[offsetof(MslCoreMatchConfig, ucf_sdi_enabled)];
    config->ucf_shield_drop_extended_enabled =
        wire[offsetof(MslCoreMatchConfig, ucf_shield_drop_extended_enabled)];
    config->ucf_shield_drop_084_enabled =
        wire[offsetof(MslCoreMatchConfig, ucf_shield_drop_084_enabled)];
    config->stage_event_streams =
        wire[offsetof(MslCoreMatchConfig, stage_event_streams)];
    memcpy(config->players, wire + offsetof(MslCoreMatchConfig, players),
           sizeof(config->players));
}

void msl_core_decode_stage_events(MslCoreStageEvents* events,
                                  const uint8_t* wire)
{
    memset(events, 0, sizeof(*events));
    events->fod_platform_height[0] = msl_core_get_lef32(
        wire + offsetof(MslCoreStageEvents, fod_platform_height));
    events->fod_platform_height[1] = msl_core_get_lef32(
        wire + offsetof(MslCoreStageEvents, fod_platform_height) +
        sizeof(float));
    events->fighter_pre_random_seed = msl_core_get_le32(
        wire + offsetof(MslCoreStageEvents, fighter_pre_random_seed));
    events->fod_platform_mask =
        wire[offsetof(MslCoreStageEvents, fod_platform_mask)];
    events->dreamland_whispy_valid =
        wire[offsetof(MslCoreStageEvents, dreamland_whispy_valid)];
    events->dreamland_whispy_direction =
        wire[offsetof(MslCoreStageEvents, dreamland_whispy_direction)];
    events->fighter_pre_random_seed_valid =
        wire[offsetof(MslCoreStageEvents, fighter_pre_random_seed_valid)];
}
