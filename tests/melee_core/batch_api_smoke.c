#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/pad.h>

static int viewer_has_item(const MslCoreViewerState* viewer)
{
    int i;
    for (i = 0; i < MSL_CORE_MAX_ITEMS; ++i) {
        if (viewer->items[i].exists) {
            return 1;
        }
    }
    return 0;
}

static void config_init(MslCoreMatchConfig* config, uint8_t stage,
                        uint8_t character, uint8_t stocks)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = stage;
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = 2;
    config->stock_count = stocks;
    config->players[0].char_id = character;
    config->players[1].char_id = character;
}

int main(int argc, char** argv)
{
    enum {
        MATCH_COUNT = 4,
        FRAMES = 180,
        ITEM_TAPE_CAPACITY = 900
    };
    MslCoreGameData* game_data = NULL;
    MslCoreGameData* other_game_data = NULL;
    MslCoreBatch* batch = NULL;
    MslCoreBatch* other_batch = NULL;
    MslCoreMatchConfig configs[MATCH_COUNT];
    MslCoreMatchConfig bad_configs[MATCH_COUNT];
    MslCoreInput inputs[MATCH_COUNT] = { 0 };
    MslCoreInput baseline_inputs[MATCH_COUNT];
    MslCoreState baseline[MATCH_COUNT];
    MslCoreState first[MATCH_COUNT];
    MslCoreState second[MATCH_COUNT];
    MslCoreViewerState viewer[MATCH_COUNT];
    MslCoreObservation observation[MATCH_COUNT];
    struct {
        MslCoreState state;
        uint32_t guard;
    } padded_state[MATCH_COUNT];
    MslCoreState other_state;
    MslCoreState snapshot_continuation;
    MslCoreState dreamland_continuation;
    MslCoreState item_continuation;
    MslCoreState restore_guard;
    MslCoreInput item_tape[ITEM_TAPE_CAPACITY];
    MslCoreViewerState item_viewer;
    MslCoreViewerState restored_item_viewer;
    uint8_t mask[MATCH_COUNT] = { 0, 0, 1, 0 };
    uint8_t mask0[MATCH_COUNT] = { 1, 0, 0, 0 };
    uint8_t mask3[MATCH_COUNT] = { 0, 0, 0, 1 };
    uint8_t mask01[MATCH_COUNT] = { 1, 1, 0, 0 };
    MslCoreTerminal terminal[MATCH_COUNT];
    uint8_t viewpoint[MATCH_COUNT] = { 1, 0, 0, 1 };
    uint32_t copy_destination = 1;
    uint32_t copy_source = 0;
    void* snapshot = NULL;
    void* snapshot_b = NULL;
    void* dreamland_snapshot = NULL;
    void* item_snapshot = NULL;
    size_t snapshot_size = 0;
    size_t snapshot_b_size = 0;
    size_t dreamland_snapshot_size = 0;
    size_t snapshot_written = 0;
    size_t item_snapshot_size = 0;
    int item_tape_frames = 0;
    int frame;
    int i;
    int result = 1;
    const char* phase = "create";

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
        return 2;
    }
    config_init(&configs[0], 32, 1, 4);
    config_init(&configs[1], 31, 22, 2);
    config_init(&configs[2], 2, 9, 3);
    config_init(&configs[3], 28, 15, 1);
    inputs[0].p[0].main_x = 80;
    inputs[1].p[0].main_x = -80;
    inputs[2].p[0].main_x = 40;
    inputs[3].p[0].main_x = -40;
    memcpy(baseline_inputs, inputs, sizeof(baseline_inputs));

    if (msl_core_game_data_create(argv[1], &game_data) != MSL_CORE_OK ||
        msl_core_batch_create(game_data, MATCH_COUNT, &batch) != MSL_CORE_OK ||
        msl_core_batch_create(game_data, 1, &other_batch) != MSL_CORE_OK ||
        msl_core_batch_match_count(batch) != MATCH_COUNT ||
        msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), NULL,
                                    0) != MSL_CORE_INVALID_STATE)
    {
        goto done;
    }
    phase = "invalid match configurations";
    memcpy(bad_configs, configs, sizeof(configs));
    bad_configs[0].num_players = 1;
    if (msl_core_batch_reset_matches(batch, bad_configs,
                                     sizeof(bad_configs[0]), mask0,
                                     sizeof(mask0[0])) !=
        MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }
    memcpy(bad_configs, configs, sizeof(configs));
    bad_configs[0].stage_id = 99;
    if (msl_core_batch_reset_matches(batch, bad_configs,
                                     sizeof(bad_configs[0]), mask0,
                                     sizeof(mask0[0])) !=
        MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }
    memcpy(bad_configs, configs, sizeof(configs));
    bad_configs[0].players[0].char_id = 0;
    if (msl_core_batch_reset_matches(batch, bad_configs,
                                     sizeof(bad_configs[0]), mask0,
                                     sizeof(mask0[0])) !=
        MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }
    memcpy(bad_configs, configs, sizeof(configs));
    bad_configs[0].players[0].facing_and_port = 2;
    bad_configs[0].players[1].facing_and_port = 2;
    if (msl_core_batch_reset_matches(batch, bad_configs,
                                     sizeof(bad_configs[0]), mask0,
                                     sizeof(mask0[0])) !=
            MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]), NULL,
                                     0) != MSL_CORE_OK)
    {
        goto done;
    }
    phase = "buffer contracts";
    memset(padded_state, 0xA5, sizeof(padded_state));
    if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]) - 1, NULL,
                                    0) != MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask,
                                    0) != MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_write_state(batch, first, sizeof(first[0]) - 1, NULL,
                                   0) != MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_write_terminal(batch, terminal,
                                      sizeof(terminal[0]) - 1, -1, NULL, 0) !=
            MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_write_observation(
            batch, viewpoint, sizeof(viewpoint[0]), observation,
            sizeof(observation[0]) - 1, NULL, 0) !=
            MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]) - 1, NULL,
                                    0) != MSL_CORE_INVALID_ARGUMENT ||
        msl_core_batch_write_state(batch, &padded_state[0].state,
                                   sizeof(padded_state[0]), NULL,
                                   0) != MSL_CORE_OK)
    {
        goto done;
    }
    for (i = 0; i < MATCH_COUNT; ++i) {
        if (padded_state[i].guard != UINT32_C(0xA5A5A5A5)) {
            goto done;
        }
    }
    for (frame = 0; frame < FRAMES; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), NULL,
                                        0) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    phase = "Dream Land ground-owner checkpoint";
    if (msl_core_batch_match_save_size(batch, 3, &dreamland_snapshot_size) !=
            MSL_CORE_OK ||
        (dreamland_snapshot = malloc(dreamland_snapshot_size)) == NULL ||
        msl_core_batch_save_match(batch, 3, dreamland_snapshot,
                                  dreamland_snapshot_size, NULL) != MSL_CORE_OK ||
        msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask3,
                                    sizeof(mask3[0])) != MSL_CORE_OK ||
        msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK)
    {
        goto done;
    }
    dreamland_continuation = first[3];
    if (msl_core_batch_restore_match(other_batch, 0, dreamland_snapshot,
                                     dreamland_snapshot_size) != MSL_CORE_OK ||
        msl_core_batch_step_matches(other_batch, &inputs[3], sizeof(inputs[3]),
                                    NULL, 0) != MSL_CORE_OK ||
        msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL, 0) != MSL_CORE_OK ||
        memcmp(&other_state, &dreamland_continuation, sizeof(other_state)) != 0 ||
        msl_core_batch_restore_match(batch, 3, dreamland_snapshot,
                                     dreamland_snapshot_size) != MSL_CORE_OK)
    {
        goto done;
    }
    msl_core_batch_destroy(other_batch);
    other_batch = NULL;
    if (msl_core_batch_create(game_data, 1, &other_batch) != MSL_CORE_OK) {
        goto done;
    }
    phase = "save first checkpoint";
    if (msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK ||
        msl_core_batch_write_terminal(batch, terminal, sizeof(terminal[0]),
                                      -1, NULL, 0) != MSL_CORE_OK ||
        msl_core_batch_write_observation(
            batch, viewpoint, sizeof(viewpoint[0]), observation,
            sizeof(observation[0]), NULL, 0) != MSL_CORE_OK ||
        msl_core_batch_match_save_size(batch, 0, &snapshot_size) !=
            MSL_CORE_OK ||
        (snapshot = malloc(snapshot_size)) == NULL ||
        msl_core_batch_save_match(batch, 0, snapshot, snapshot_size,
                                  &snapshot_written) != MSL_CORE_OK ||
        snapshot_written != snapshot_size)
    {
        goto done;
    }
    for (i = 0; i < MATCH_COUNT; ++i) {
        int self = viewpoint[i];
        int opponent = self == 0 ? 1 : 0;
        if (observation[i].frame_id != first[i].frame_id ||
            observation[i].stage_id != first[i].stage_id ||
            observation[i].num_players != 2 ||
            observation[i].viewpoint_player != self ||
            observation[i].slots[0].source_player != self ||
            observation[i].slots[0].team_relation != 0 ||
            observation[i].slots[1].source_player != opponent ||
            observation[i].slots[1].team_relation != 2 ||
            memcmp(&observation[i].items, &first[i].items,
                   sizeof(observation[i].items)) != 0 ||
            terminal[i].frame_id != first[i].frame_id ||
            terminal[i].stage_id != first[i].stage_id ||
            terminal[i].alive_count != 2)
        {
            goto done;
        }
    }
    viewpoint[2] = 4;
    if (msl_core_batch_write_observation(
            batch, viewpoint, sizeof(viewpoint[0]), observation,
            sizeof(observation[0]), mask, sizeof(mask[0])) !=
        MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }
    viewpoint[2] = 0;
    phase = "reject damaged checkpoint";
    ((uint8_t*) snapshot)[snapshot_size - 1] ^= 0x80;
    if (msl_core_batch_restore_match(other_batch, 0, snapshot,
                                     snapshot_size) != MSL_CORE_INCOMPATIBLE ||
        msl_core_batch_step_matches(other_batch, &inputs[0], sizeof(inputs[0]),
                                    NULL, 0) != MSL_CORE_INVALID_STATE)
    {
        goto done;
    }
    ((uint8_t*) snapshot)[snapshot_size - 1] ^= 0x80;
    if (msl_core_batch_restore_match(other_batch, 0, snapshot,
                                     snapshot_size - 1) !=
            MSL_CORE_INCOMPATIBLE ||
        msl_core_batch_save_match(batch, 0, snapshot, snapshot_size - 1,
                                  NULL) != MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }
    memcpy(baseline, first, sizeof(baseline));
    phase = "advance source suffix";
    for (frame = 0; frame < 60; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]),
                                        mask0,
                                        sizeof(mask0[0])) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(batch, second, sizeof(second[0]), NULL,
                                   0) != MSL_CORE_OK ||
        msl_core_batch_match_save_size(batch, 0, &snapshot_b_size) !=
            MSL_CORE_OK ||
        (snapshot_b = malloc(snapshot_b_size)) == NULL ||
        msl_core_batch_save_match(batch, 0, snapshot_b, snapshot_b_size,
                                  NULL) != MSL_CORE_OK)
    {
        goto done;
    }
    snapshot_continuation = second[0];
    phase = "restore independent batch";
    // A snapshot has no source-batch or source-index affinity. Restore it
    // into an independently allocated, previously uninitialized batch and
    // require the same continuation as its source Match.
    if (msl_core_batch_restore_match(other_batch, 0, snapshot,
                                     snapshot_size) != MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < 60; ++frame) {
        if (msl_core_batch_step_matches(other_batch, &inputs[0],
                                        sizeof(inputs[0]), NULL,
                                        0) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL,
                                   0) != MSL_CORE_OK ||
        memcmp(&other_state, &snapshot_continuation, sizeof(other_state)) != 0)
    {
        goto done;
    }
    // A later snapshot can be restored after the same environment has moved
    // forward, independently of the older snapshot still held by the caller.
    if (msl_core_batch_step_matches(other_batch, &inputs[0], sizeof(inputs[0]),
                                    NULL, 0) != MSL_CORE_OK ||
        msl_core_batch_restore_match(other_batch, 0, snapshot_b,
                                     snapshot_b_size) != MSL_CORE_OK ||
        msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL,
                                   0) != MSL_CORE_OK ||
        memcmp(&other_state, &snapshot_continuation, sizeof(other_state)) != 0)
    {
        goto done;
    }
    phase = "restore nested checkpoint";
    inputs[3] = inputs[0];
    phase = "restore arbitrary match index";
    if (msl_core_batch_restore_match(batch, 3, snapshot, snapshot_size) !=
        MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < 60; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]),
                                        mask3,
                                        sizeof(mask3[0])) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&first[3], &second[0], sizeof(first[3])) != 0 ||
        msl_core_batch_restore_match(batch, 0, snapshot, snapshot_size) !=
            MSL_CORE_OK)
    {
        goto done;
    }
    phase = "repeat restore";
    for (frame = 0; frame < 60; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]),
                                        mask0,
                                        sizeof(mask0[0])) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&first[0], &second[0], sizeof(first[0])) != 0 ||
        msl_core_batch_copy_matches(batch, batch, &copy_destination,
                                    &copy_source, 1) != MSL_CORE_OK ||
        msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&first[0], &first[1], sizeof(first[0])) != 0)
    {
        goto done;
    }
    inputs[1] = inputs[0];
    phase = "copy match";
    if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask01,
                                    sizeof(mask01[0])) != MSL_CORE_OK ||
        msl_core_batch_write_state(batch, first, sizeof(first[0]), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&first[0], &first[1], sizeof(first[0])) != 0)
    {
        goto done;
    }
    memcpy(inputs, baseline_inputs, sizeof(inputs));
    phase = "repeat reset";
    if (msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]), NULL,
                                     0) != MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < FRAMES; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), NULL,
                                        0) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(batch, second, sizeof(second[0]), NULL,
                                   0) != MSL_CORE_OK ||
        memcmp(baseline, second, sizeof(baseline)) != 0 ||
        msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask,
                                    sizeof(mask[0])) != MSL_CORE_OK ||
        msl_core_batch_write_state(batch, second, sizeof(second[0]), NULL,
                                   0) != MSL_CORE_OK)
    {
        goto done;
    }
    for (i = 0; i < MATCH_COUNT; ++i) {
        int expected_frame = baseline[i].frame_id + (i == 2 ? 1 : 0);
        if (second[i].frame_id != expected_frame) {
            goto done;
        }
    }
    if (msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]) - 1,
                                     NULL, 0) != MSL_CORE_INVALID_ARGUMENT)
    {
        goto done;
    }

    // Output is an observer, including on the moving-stage path where a lazy
    // JObj publication would otherwise alter the next gameplay frame.
    phase = "viewer observer semantics";
    config_init(&configs[0], 8, 1, 4);
    configs[1] = configs[0];
    memset(&inputs[0], 0, sizeof(inputs[0]));
    inputs[1] = inputs[0];
    if (msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]),
                                     mask01,
                                     sizeof(mask01[0])) != MSL_CORE_OK ||
        msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]), mask0,
                                    sizeof(mask0[0])) != MSL_CORE_OK ||
        msl_core_batch_write_observation(
            batch, viewpoint, sizeof(viewpoint[0]), observation,
            sizeof(observation[0]), mask0, sizeof(mask0[0])) != MSL_CORE_OK ||
        !observation[0].stage.randall.exists ||
        msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask01,
                                    sizeof(mask01[0])) != MSL_CORE_OK ||
        msl_core_batch_write_state(batch, first, sizeof(first[0]), mask01,
                                   sizeof(mask01[0])) != MSL_CORE_OK ||
        memcmp(&first[0], &first[1], sizeof(first[0])) != 0)
    {
        goto done;
    }

    // Save a live Peach item graph, then destroy and reuse its source fixed
    // pools before restoring the checkpoint into an unrelated Match. This
    // covers allocator/free-list topology that a locomotion-only checkpoint
    // cannot exercise.
    phase = "live item checkpoint";
    config_init(&configs[2], 32, 9, 4);
    memset(&inputs[2], 0, sizeof(inputs[2]));
    if (msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]), mask,
                                     sizeof(mask[0])) != MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < 123; ++frame) {
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask,
                                        sizeof(mask[0])) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    for (frame = 0; frame < 120; ++frame) {
        memset(&inputs[2], 0, sizeof(inputs[2]));
        if (frame < 3) {
            inputs[2].p[0].buttons = PAD_BUTTON_B;
            inputs[2].p[0].main_y = -80;
        }
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask,
                                        sizeof(mask[0])) != MSL_CORE_OK ||
            msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]), mask,
                                        sizeof(mask[0])) != MSL_CORE_OK)
        {
            goto done;
        }
        if (viewer_has_item(&viewer[2])) {
            break;
        }
    }
    if (!viewer_has_item(&viewer[2])) {
        goto done;
    }
    for (frame = 0; frame < 60; ++frame) {
        memset(&inputs[2], 0, sizeof(inputs[2]));
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]), mask,
                                        sizeof(mask[0])) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_match_save_size(batch, 2, &item_snapshot_size) !=
            MSL_CORE_OK ||
        (item_snapshot = malloc(item_snapshot_size)) == NULL ||
        msl_core_batch_save_match(batch, 2, item_snapshot,
                                  item_snapshot_size, NULL) != MSL_CORE_OK)
    {
        goto done;
    }

#define STEP_ITEM_TAPE(row)                                                   \
    do {                                                                      \
        if (item_tape_frames == ITEM_TAPE_CAPACITY) {                         \
            goto done;                                                        \
        }                                                                     \
        item_tape[item_tape_frames++] = (row);                                \
        inputs[2] = (row);                                                    \
        if (msl_core_batch_step_matches(batch, inputs, sizeof(inputs[0]),     \
                                        mask, sizeof(mask[0])) !=             \
            MSL_CORE_OK)                                                      \
        {                                                                     \
            goto done;                                                        \
        }                                                                     \
    } while (0)

    phase = "item allocator reuse continuation";
    {
        MslCoreInput item_input = { 0 };
        int disappeared = 0;
        int respawned = 0;
        for (frame = 0; frame < 60; ++frame) {
            STEP_ITEM_TAPE(item_input);
        }
        item_input.p[0].buttons = PAD_BUTTON_A;
        item_input.p[0].main_x = 80;
        STEP_ITEM_TAPE(item_input);
        memset(&item_input, 0, sizeof(item_input));
        for (frame = 0; frame < 500; ++frame) {
            STEP_ITEM_TAPE(item_input);
            if (msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]),
                                            mask, sizeof(mask[0])) !=
                MSL_CORE_OK)
            {
                goto done;
            }
            if (!viewer_has_item(&viewer[2])) {
                disappeared = 1;
                break;
            }
        }
        if (!disappeared) {
            goto done;
        }
        for (frame = 0; frame < 60; ++frame) {
            STEP_ITEM_TAPE(item_input);
        }
        item_input.p[0].buttons = PAD_BUTTON_B;
        item_input.p[0].main_y = -80;
        for (frame = 0; frame < 3; ++frame) {
            STEP_ITEM_TAPE(item_input);
        }
        memset(&item_input, 0, sizeof(item_input));
        for (frame = 0; frame < 120; ++frame) {
            STEP_ITEM_TAPE(item_input);
            if (msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]),
                                            mask, sizeof(mask[0])) !=
                MSL_CORE_OK)
            {
                goto done;
            }
            if (viewer_has_item(&viewer[2])) {
                respawned = 1;
                break;
            }
        }
        if (!respawned) {
            goto done;
        }
        for (frame = 0; frame < 60; ++frame) {
            STEP_ITEM_TAPE(item_input);
        }
    }
#undef STEP_ITEM_TAPE

    if (msl_core_batch_write_state(batch, first, sizeof(first[0]), mask,
                                   sizeof(mask[0])) != MSL_CORE_OK ||
        msl_core_batch_write_viewer(batch, viewer, sizeof(viewer[0]), mask,
                                    sizeof(mask[0])) != MSL_CORE_OK)
    {
        goto done;
    }
    item_continuation = first[2];
    item_viewer = viewer[2];
    phase = "restore item allocator topology";
    if (msl_core_batch_reset_matches(other_batch, &configs[2],
                                     sizeof(configs[2]), NULL, 0) !=
            MSL_CORE_OK ||
        msl_core_batch_write_state(other_batch, &restore_guard,
                                   sizeof(restore_guard), NULL, 0) !=
            MSL_CORE_OK)
    {
        goto done;
    }
    ((uint8_t*) item_snapshot)[item_snapshot_size - 1] ^= 0x80;
    if (msl_core_batch_restore_match(other_batch, 0, item_snapshot,
                                     item_snapshot_size) !=
            MSL_CORE_INCOMPATIBLE ||
        msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&other_state, &restore_guard, sizeof(other_state)) != 0)
    {
        goto done;
    }
    ((uint8_t*) item_snapshot)[item_snapshot_size - 1] ^= 0x80;
    if (msl_core_batch_restore_match(other_batch, 0, item_snapshot,
                                     item_snapshot_size) != MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < item_tape_frames; ++frame) {
        if (msl_core_batch_step_matches(other_batch, &item_tape[frame],
                                        sizeof(item_tape[frame]), NULL, 0) !=
            MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL, 0) !=
            MSL_CORE_OK ||
        msl_core_batch_write_viewer(other_batch, &restored_item_viewer,
                                    sizeof(restored_item_viewer), NULL, 0) !=
            MSL_CORE_OK ||
        memcmp(&other_state, &item_continuation, sizeof(other_state)) != 0 ||
        memcmp(&restored_item_viewer, &item_viewer,
               sizeof(restored_item_viewer)) != 0)
    {
        goto done;
    }

    // The opaque artifact also survives destruction of its source GameData.
    // Recreate equivalent immutable data, restore into a fresh batch, and
    // require the same continuation. The native DAT graph is intentionally
    // remapped at its low-address ABI base between the two lifetimes.
    msl_core_batch_destroy(other_batch);
    phase = "restore after GameData recreation";
    other_batch = NULL;
    msl_core_batch_destroy(batch);
    batch = NULL;
    msl_core_game_data_destroy(game_data);
    game_data = NULL;
    if (msl_core_game_data_create(argv[1], &other_game_data) != MSL_CORE_OK) {
        goto done;
    }
    phase = "create batch after GameData recreation";
    if (msl_core_batch_create(other_game_data, 1, &other_batch) != MSL_CORE_OK)
    {
        goto done;
    }
    phase = "restore checkpoint after GameData recreation";
    if (msl_core_batch_restore_match(other_batch, 0, snapshot,
                                     snapshot_size) != MSL_CORE_OK)
    {
        goto done;
    }
    phase = "advance after GameData recreation";
    for (frame = 0; frame < 60; ++frame) {
        if (msl_core_batch_step_matches(other_batch, &baseline_inputs[0],
                                        sizeof(baseline_inputs[0]), NULL,
                                        0) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    phase = "compare after GameData recreation";
    if (msl_core_batch_write_state(other_batch, &other_state,
                                   sizeof(other_state), NULL,
                                   0) != MSL_CORE_OK ||
        memcmp(&other_state, &snapshot_continuation, sizeof(other_state)) != 0)
    {
        goto done;
    }
    result = 0;

done:
    if (result != 0) {
        fprintf(stderr, "batch API smoke failed during %s\n", phase);
    }
    free(item_snapshot);
    free(dreamland_snapshot);
    free(snapshot_b);
    free(snapshot);
    msl_core_batch_destroy(other_batch);
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(other_game_data);
    msl_core_game_data_destroy(game_data);
    return result;
}
