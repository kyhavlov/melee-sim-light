#include "runtime/scalar.h"
#include "runtime/context.h"
#include "runtime/savestate.h"
#include "ft/types.h"
#include "pl/player.h"
#include <baselib/random.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MslCoreGameData game_data;
static MslCoreMatch match;
static MslCoreMatch copy;

static int check_input(MslCoreMatch* target, const MslReplayCpuInput* input)
{
    Fighter* fp = target->fighters[1]->user_data;
    msl_core_bind_match(target);
    msl_slippi_state_bind(&target->slippi);
    // A follower shares the port but must not consume the leader's sample.
    fp->x221F_b4 = 1;
    msl_slippi_apply_cpu_input(fp);
    fp->x221F_b4 = 0;
    if (!target->slippi.cpu_inputs[2].valid) return -1;
    msl_slippi_apply_cpu_input(fp);
    if (memcmp(&fp->input.lstick.x, &input->main_x, sizeof(float)) ||
        memcmp(&fp->input.lstick.y, &input->main_y, sizeof(float)) ||
        memcmp(&fp->input.cstick.x, &input->c_x, sizeof(float)) ||
        memcmp(&fp->input.cstick.y, &input->c_y, sizeof(float)) ||
        memcmp(&fp->input.x650, &input->trigger, sizeof(float)) ||
        fp->input.held_inputs != input->buttons ||
        *seed_ptr != input->random_seed ||
        target->slippi.cpu_inputs[2].valid)
    {
        fprintf(stderr, "recorded CPU input lost bits or port ownership\n");
        return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    MslCoreMatchConfig config = { 0 };
    MslCoreInput input = { 0 };
    MslCoreStageEvents events = { 0 };
    MslCoreStageEvents decoded;
    uint8_t wire[sizeof(events)] = { 0 };
    MslReplayCpuInput* sample = &events.cpu_inputs[2];
    void* saved;
    size_t size;
    size_t written;

    if (argc != 2) return 2;
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1.0F;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = config.players[1].char_id = 1;
    config.players[0].facing_and_port = (2 << 1) | 1;
    config.players[1].facing_and_port = 3 << 1;
    config.players[1].cpu_level = 7;
    if (msl_core_game_data_init(&game_data, argv[1]) ||
        msl_core_match_init(&match, &game_data, &config, &input) ||
        msl_core_match_init(&copy, &game_data, &config, &input)) return 1;
    if (Player_GetPlayerSlotType(1) != Gm_PKind_Human ||
        Player_GetPlayerSlotType(2) != Gm_PKind_Cpu ||
        Player_GetCpuLevel(2) != 7) return 1;

    // Include negative zero, a subnormal, and CPU axes off the human 1/80 grid.
    sample->main_x = -0.0F;
    sample->main_y = 61.0F / 127.0F;
    sample->c_x = -53.0F / 128.0F;
    sample->c_y = 0x1p-149F;
    sample->trigger = 0.3125F;
    sample->buttons = 0x80000100U;
    sample->random_seed = 0x89abcdefU;
    sample->valid = 1;
    for (size_t i = 0; i < 7; ++i) {
        uint32_t bits;
        memcpy(&bits, (const uint8_t*) sample + i * 4, 4);
        msl_core_put_le32(wire + offsetof(MslCoreStageEvents, cpu_inputs) +
                             2 * sizeof(*sample) + i * 4, bits);
    }
    wire[offsetof(MslCoreStageEvents, cpu_inputs) +
         2 * sizeof(*sample) + offsetof(MslReplayCpuInput, valid)] = 1;
    msl_core_decode_stage_events(&decoded, wire);
    if (memcmp(&decoded, &events, sizeof(events))) return 1;
    msl_slippi_state_bind(&match.slippi);
    msl_slippi_stage_events_begin(&decoded);
    size = msl_core_match_save_size(&match);
    saved = malloc(size);
    if (!saved || msl_core_match_save(&match, saved, size, &written) ||
        msl_core_match_copy(&copy, &match) || check_input(&copy, sample) ||
        check_input(&match, sample) ||
        msl_core_match_restore(&match, saved, written) ||
        check_input(&match, sample)) return 1;
    free(saved);
    config.players[1].cpu_level = 0;
    if (msl_core_match_reset(&match, &game_data, &config, &input) ||
        match.slippi.cpu_inputs[2].valid ||
        Player_GetPlayerSlotType(2) != Gm_PKind_Human) return 1;
    msl_core_match_destroy(&copy);
    msl_core_match_destroy(&match);
    return 0;
}
