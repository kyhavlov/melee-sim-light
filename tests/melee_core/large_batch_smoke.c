#define _GNU_SOURCE

#include "api.h"
#include "platform/memory.h"
#include "runtime/scalar.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum { OBSERVATION_HISTORY = 128 };

typedef struct ProcessMemory {
    uint64_t virtual_bytes;
    uint64_t resident_bytes;
    uint64_t proportional_bytes;
    uint64_t private_bytes;
} ProcessMemory;

static double seconds_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (double) now.tv_sec + (double) now.tv_nsec / 1000000000.0;
}

static int process_memory(ProcessMemory* memory)
{
    FILE* input;
    char line[256];
    unsigned long pages;
    unsigned long resident;
    long page_size = sysconf(_SC_PAGESIZE);
    input = fopen("/proc/self/statm", "r");
    if (input == NULL || page_size <= 0 ||
        fscanf(input, "%lu %lu", &pages, &resident) != 2)
    {
        if (input != NULL) {
            fclose(input);
        }
        return -1;
    }
    fclose(input);
    memory->virtual_bytes = (uint64_t) pages * (uint64_t) page_size;
    memory->resident_bytes = (uint64_t) resident * (uint64_t) page_size;
    memory->proportional_bytes = 0;
    memory->private_bytes = 0;
    input = fopen("/proc/self/smaps_rollup", "r");
    if (input == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        unsigned long kib;
        if (sscanf(line, "Pss: %lu kB", &kib) == 1) {
            memory->proportional_bytes = (uint64_t) kib * 1024;
        } else if (sscanf(line, "Private_Clean: %lu kB", &kib) == 1 ||
                   sscanf(line, "Private_Dirty: %lu kB", &kib) == 1)
        {
            memory->private_bytes += (uint64_t) kib * 1024;
        }
    }
    fclose(input);
    return 0;
}

static int parse_count(const char* text, uint32_t* count)
{
    char* end;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || *end != '\0' || value == 0 ||
        value > 16384)
    {
        return -1;
    }
    *count = (uint32_t) value;
    return 0;
}

static void config_init(MslCoreMatchConfig* config, uint32_t index)
{
    static const uint8_t stages[] = { 32, 31, 3, 2, 8, 28, 32, 31 };
    static const uint8_t characters[] = { 1, 22, 18, 7, 15, 19, 9, 2 };
    uint32_t variant = index % 8;
    uint8_t player;
    memset(config, 0, sizeof(*config));
    config->stage_id = stages[variant];
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = variant == 3 ? 4 : 2;
    config->stock_count = 4;
    config->online_fnmsubs_zero = 1;
    config->brawl_offscreen_damage = 1;
    config->freeze_dead_up_fall_physics = 1;
    config->ucf_cardinals_1_0_enabled = 1;
    config->ucf_shield_sdi_enabled = 1;
    config->ucf_sdi_enabled = 1;
    for (player = 0; player < config->num_players; ++player) {
        config->players[player].char_id = characters[variant];
    }
}

static void touch_mapping(uint8_t* mapping, size_t bytes)
{
    size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    size_t offset;
    for (offset = 0; offset < bytes; offset += page_size) {
        mapping[offset] = (uint8_t) (offset / page_size);
    }
    if (bytes != 0) {
        mapping[bytes - 1] = 1;
    }
}

static double gib(uint64_t bytes)
{
    return (double) bytes / (1024.0 * 1024.0 * 1024.0);
}

static int within_growth(uint64_t value, uint64_t baseline, uint64_t budget)
{
    return value <= baseline || value - baseline <= budget;
}

int main(int argc, char** argv)
{
    MslCoreGameData* game_data = NULL;
    MslCoreBatch* batch = NULL;
    MslCoreMatchConfig* configs = NULL;
    MslCoreInput* inputs = NULL;
    uint8_t* viewpoints = NULL;
    uint8_t* mask = NULL;
    MslCoreObservation* observations = MAP_FAILED;
    MslCoreTerminal* terminals = MAP_FAILED;
    void* snapshot = NULL;
    ProcessMemory baseline;
    ProcessMemory created;
    ProcessMemory reset;
    ProcessMemory ring;
    ProcessMemory destroyed;
    uint32_t count;
    uint32_t i;
    size_t snapshot_size = 0;
    size_t observation_count;
    size_t observation_bytes;
    size_t terminal_bytes;
    double create_started;
    double create_seconds;
    double reset_started;
    double reset_seconds;
    double step_started;
    double step_seconds;
    int result = 1;
    const char* phase = "arguments";

    if (argc != 3 || parse_count(argv[2], &count) != 0) {
        fprintf(stderr, "usage: %s DATA_ROOT MATCH_COUNT\n", argv[0]);
        return 2;
    }
    observation_count = (size_t) count * OBSERVATION_HISTORY;
    if (observation_count > SIZE_MAX / sizeof(*observations)) {
        return 2;
    }
    observation_bytes = observation_count * sizeof(*observations);
    terminal_bytes = observation_count * sizeof(*terminals);
    if (process_memory(&baseline) != 0) {
        goto done;
    }
    configs = calloc(count, sizeof(*configs));
    inputs = calloc(count, sizeof(*inputs));
    viewpoints = calloc(count, sizeof(*viewpoints));
    mask = calloc(count, sizeof(*mask));
    if (configs == NULL || inputs == NULL || viewpoints == NULL || mask == NULL) {
        goto done;
    }
    for (i = 0; i < count; ++i) {
        config_init(&configs[i], i);
    }

    phase = "create";
    create_started = seconds_now();
    if (msl_core_game_data_create(argv[1], &game_data) != MSL_CORE_OK ||
        msl_core_batch_create(game_data, count, &batch) != MSL_CORE_OK)
    {
        goto done;
    }
    create_seconds = seconds_now() - create_started;
    if (process_memory(&created) != 0) {
        goto done;
    }

    phase = "reset";
    reset_started = seconds_now();
    if (msl_core_batch_reset_matches(batch, configs, sizeof(*configs), NULL,
                                     0) != MSL_CORE_OK)
    {
        goto done;
    }
    reset_seconds = seconds_now() - reset_started;
    if (process_memory(&reset) != 0) {
        goto done;
    }

    phase = "observation ring";
    observations = mmap(NULL, observation_bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    terminals = mmap(NULL, terminal_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (observations == MAP_FAILED || terminals == MAP_FAILED) {
        goto done;
    }
    touch_mapping((uint8_t*) observations, observation_bytes);
    touch_mapping((uint8_t*) terminals, terminal_bytes);
    if (process_memory(&ring) != 0) {
        goto done;
    }

    phase = "save and step";
    if (msl_core_batch_match_save_size(batch, 0, &snapshot_size) !=
            MSL_CORE_OK ||
        (snapshot = malloc(snapshot_size)) == NULL ||
        msl_core_batch_save_match(batch, 0, snapshot, snapshot_size, NULL) !=
            MSL_CORE_OK)
    {
        goto done;
    }
    step_started = seconds_now();
    if (msl_core_batch_step_matches(batch, inputs, sizeof(*inputs), NULL, 0) !=
            MSL_CORE_OK ||
        msl_core_batch_write_observation(
            batch, viewpoints, sizeof(*viewpoints), observations,
            sizeof(*observations), NULL, 0) != MSL_CORE_OK ||
        msl_core_batch_write_terminal(batch, terminals, sizeof(*terminals),
                                      -1, NULL, 0) != MSL_CORE_OK)
    {
        goto done;
    }
    step_seconds = seconds_now() - step_started;

    phase = "arbitrary restore";
    if (msl_core_batch_restore_match(batch, count - 1, snapshot,
                                     snapshot_size) != MSL_CORE_OK)
    {
        goto done;
    }
    memset(mask, 0, count);
    mask[count - 1] = 1;
    if (msl_core_batch_step_matches(batch, inputs, sizeof(*inputs),
                                    mask, sizeof(*mask)) != MSL_CORE_OK)
    {
        goto done;
    }
    mask[0] = 1;
    if (
        msl_core_batch_write_observation(
            batch, viewpoints, sizeof(*viewpoints), observations,
            sizeof(*observations), mask, sizeof(*mask)) != MSL_CORE_OK ||
        msl_core_batch_write_terminal(batch, terminals, sizeof(*terminals),
                                      -1, mask, sizeof(*mask)) != MSL_CORE_OK ||
        memcmp(&observations[0], &observations[count - 1],
               sizeof(*observations)) != 0 ||
        memcmp(&terminals[0], &terminals[count - 1], sizeof(*terminals)) != 0)
    {
        goto done;
    }

    phase = "destroy";
    msl_core_batch_destroy(batch);
    batch = NULL;
    msl_core_game_data_destroy(game_data);
    game_data = NULL;
    if (process_memory(&destroyed) != 0) {
        goto done;
    }

    phase = "memory budget";
    if (!within_growth(
            created.virtual_bytes, baseline.virtual_bytes,
            (uint64_t) count *
                    (msl_memory_match_capacity() + sizeof(MslCoreMatch)) +
                UINT64_C(512) * 1024 * 1024) ||
        !within_growth(
            reset.resident_bytes, baseline.resident_bytes,
            (uint64_t) count *
                    (msl_memory_match_capacity() + sizeof(MslCoreMatch)) +
                UINT64_C(512) * 1024 * 1024) ||
        !within_growth(
            ring.resident_bytes, baseline.resident_bytes,
            (uint64_t) count *
                    (msl_memory_match_capacity() + sizeof(MslCoreMatch)) +
                observation_bytes + terminal_bytes +
                UINT64_C(512) * 1024 * 1024) ||
        !within_growth(destroyed.resident_bytes, baseline.resident_bytes,
                       observation_bytes + terminal_bytes +
                           UINT64_C(256) * 1024 * 1024))
    {
        goto done;
    }

    printf(
        "large_batch matches=%u history=%u match_value_mib=%.2f "
        "arena_reserved_gib=%.2f observation_gib=%.2f\n",
        count, OBSERVATION_HISTORY,
        (double) ((size_t) count * sizeof(MslCoreMatch)) / (1024.0 * 1024.0),
        gib((uint64_t) count * msl_memory_match_capacity()),
        gib(observation_bytes + terminal_bytes));
    printf(
        "lifecycle create_s=%.3f reset_s=%.3f one_step_s=%.3f "
        "one_step_fps=%.0f snapshot_bytes=%zu\n",
        create_seconds, reset_seconds, step_seconds,
        (double) count / step_seconds, snapshot_size);
    printf(
        "memory baseline_v_gib=%.2f baseline_rss_gib=%.2f "
        "created_v_gib=%.2f created_rss_gib=%.2f reset_v_gib=%.2f "
        "reset_rss_gib=%.2f reset_pss_gib=%.2f reset_private_gib=%.2f "
        "ring_v_gib=%.2f ring_rss_gib=%.2f ring_pss_gib=%.2f "
        "destroyed_v_gib=%.2f destroyed_rss_gib=%.2f\n",
        gib(baseline.virtual_bytes), gib(baseline.resident_bytes),
        gib(created.virtual_bytes), gib(created.resident_bytes),
        gib(reset.virtual_bytes), gib(reset.resident_bytes),
        gib(reset.proportional_bytes), gib(reset.private_bytes),
        gib(ring.virtual_bytes), gib(ring.resident_bytes),
        gib(ring.proportional_bytes),
        gib(destroyed.virtual_bytes), gib(destroyed.resident_bytes));
    printf(
        "resident_components initialized_state_gib=%.2f "
        "observation_ring_gib=%.2f post_destroy_ring_gib=%.2f\n",
        gib(reset.resident_bytes > created.resident_bytes
                ? reset.resident_bytes - created.resident_bytes
                : 0),
        gib(ring.resident_bytes > reset.resident_bytes
                ? ring.resident_bytes - reset.resident_bytes
                : 0),
        gib(destroyed.resident_bytes > baseline.resident_bytes
                ? destroyed.resident_bytes - baseline.resident_bytes
                : 0));
    result = 0;

done:
    if (result != 0) {
        fprintf(stderr, "large batch smoke failed: phase=%s\n", phase);
    }
    free(snapshot);
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    if (observations != MAP_FAILED) {
        munmap(observations, observation_bytes);
    }
    if (terminals != MAP_FAILED) {
        munmap(terminals, terminal_bytes);
    }
    free(mask);
    free(viewpoints);
    free(inputs);
    free(configs);
    return result;
}
