// Independent batches must produce the serial result even when their owning
// threads change and other threads create/destroy batches against the same data.
#include "api.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MSL_THREAD_WORKERS 4
#define MSL_THREAD_ENVS 2
#define MSL_THREAD_ROUNDS 13
#define MSL_THREAD_FRAMES 360
#define MSL_CHECK(call)                                                                \
    do {                                                                               \
        if ((call) != MSL_OK) {                                                        \
            fprintf(stderr, "%s failed at line %d\n", #call, __LINE__);                \
            abort();                                                                   \
        }                                                                              \
    } while (0)

typedef struct MslThreadWorker {
    MslBatch* batch;
    MslBatch* copy;
    uint64_t digest;
    uint32_t random;
    int id;
} MslThreadWorker;

static MslThreadWorker msl_workers[MSL_THREAD_WORKERS];
static const char* msl_data_root;
static pthread_mutex_t msl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t msl_condition = PTHREAD_COND_INITIALIZER;
static int msl_arrived;
static int msl_phase;

static void msl_thread_barrier(void)
{
    pthread_mutex_lock(&msl_mutex);
    int current = msl_phase;
    if (++msl_arrived == MSL_THREAD_WORKERS) {
        msl_arrived = 0;
        ++msl_phase;
        pthread_cond_broadcast(&msl_condition);
    } else {
        while (msl_phase == current) {
            pthread_cond_wait(&msl_condition, &msl_mutex);
        }
    }
    pthread_mutex_unlock(&msl_mutex);
}

static uint32_t msl_next_random(MslThreadWorker* worker)
{
    worker->random = worker->random * 1664525U + 1013904223U;
    return worker->random;
}

static void msl_hash(MslThreadWorker* worker, const void* data, size_t size)
{
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; ++i) {
        worker->digest = (worker->digest ^ bytes[i]) * UINT64_C(1099511628211);
    }
}

static void msl_thread_create(MslThreadWorker* worker, int id)
{
    memset(worker, 0, sizeof(*worker));
    worker->id = id;
    worker->random = (uint32_t)id + 1;
    worker->digest = UINT64_C(14695981039346656037);
    MSL_CHECK(msl_game_data_acquire(msl_data_root));
    MSL_CHECK(msl_batch_create(msl_data_root, MSL_THREAD_ENVS, &worker->batch));
    MSL_CHECK(msl_batch_create(msl_data_root, MSL_THREAD_ENVS, &worker->copy));
    msl_game_data_release();
}

static void msl_thread_destroy(MslThreadWorker* worker)
{
    msl_batch_destroy(worker->copy);
    msl_batch_destroy(worker->batch);
}

static void msl_run_round(MslThreadWorker* worker, int round)
{
    static const uint8_t characters[] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    };
    static const uint32_t stages[] = {2, 3, 8, 28, 31, 32};
    MslMatchConfig configs[MSL_THREAD_ENVS];
    MslInput inputs[MSL_THREAD_ENVS] = {0};
    MslObservation observations[MSL_THREAD_ENVS], copied[MSL_THREAD_ENVS];
    MslTerminal terminals[MSL_THREAD_ENVS];
    uint8_t mask[MSL_THREAD_ENVS] = {1, round % 2 == 0};
    uint32_t from = (uint32_t)round % MSL_THREAD_ENVS;
    uint32_t to = 1 - from;
    size_t size, written;
    void* saved;
    MslBatch* temporary;

    // Exercise reference-count changes while other threads step their batches.
    MSL_CHECK(msl_batch_create(msl_data_root, 1, &temporary));
    msl_batch_destroy(temporary);
    for (int env = 0; env < MSL_THREAD_ENVS; ++env) {
        unsigned index = (unsigned)(round * MSL_THREAD_WORKERS + worker->id + env);
        configs[env] = msl_match_config_default();
        configs[env].stage = stages[index % 6];
        configs[env].random_seed = msl_next_random(worker);
        configs[env].num_players = 2 + index % 3;
        configs[env].is_teams = configs[env].num_players > 2;
        for (int p = 0; p < configs[env].num_players; ++p) {
            configs[env].players[p].character = characters[(index + p * 7) % 26];
            configs[env].players[p].costume = (index + p) % 4;
        }
    }
    MSL_CHECK(
        msl_batch_reset(worker->batch, configs, round ? mask : NULL, observations));
    MSL_CHECK(msl_batch_reset(worker->copy, configs, NULL, copied));
    for (int frame = 0; frame < MSL_THREAD_FRAMES; ++frame) {
        for (int env = 0; env < MSL_THREAD_ENVS; ++env) {
            for (int p = 0; p < MSL_MAX_PLAYERS; ++p) {
                uint32_t bits = msl_next_random(worker);
                inputs[env].players[p].buttons = (uint16_t)(bits & 0x1FFF);
                inputs[env].players[p].main_x = (int8_t)((int)(bits % 161) - 80);
                inputs[env].players[p].main_y = (int8_t)((int)((bits >> 8) % 161) - 80);
            }
        }
        MSL_CHECK(msl_batch_step_masked(worker->batch, inputs,
                                        frame % 11 == 0 ? mask : NULL, observations,
                                        terminals));
        msl_hash(worker, observations, sizeof(observations));
        msl_hash(worker, terminals, sizeof(terminals));
    }
    MSL_CHECK(msl_batch_copy(worker->copy, worker->batch, &to, &from, 1));
    MSL_CHECK(msl_batch_observe(worker->copy, copied, terminals));
    if (memcmp(&copied[to], &observations[from], sizeof(copied[to])) != 0) {
        fprintf(stderr, "copy changed observation\n");
        abort();
    }
    MSL_CHECK(msl_batch_save_size(worker->batch, from, &size));
    saved = malloc(size);
    if (saved == NULL)
        abort();
    MSL_CHECK(msl_batch_save(worker->batch, from, saved, size, &written));
    MSL_CHECK(msl_batch_step(worker->batch, inputs, copied, terminals));
    MSL_CHECK(msl_batch_restore(worker->batch, from, saved, written));
    free(saved);
    MSL_CHECK(msl_batch_observe(worker->batch, copied, terminals));
    if (memcmp(&copied[from], &observations[from], sizeof(copied[from])) != 0) {
        fprintf(stderr, "restore changed observation\n");
        abort();
    }
}

static void* msl_run_thread(void* arg)
{
    int id = *(int*)arg;
    msl_thread_create(&msl_workers[id], id);
    msl_thread_barrier();
    for (int round = 0; round < MSL_THREAD_ROUNDS; ++round) {
        // Each round hands the complete batch to a different thread.
        msl_run_round(&msl_workers[(id + round) % MSL_THREAD_WORKERS], round);
        msl_thread_barrier();
    }
    msl_thread_destroy(&msl_workers[id]);
    return NULL;
}

int main(int argc, char** argv)
{
    uint64_t expected[MSL_THREAD_WORKERS];
    pthread_t threads[MSL_THREAD_WORKERS];
    int ids[MSL_THREAD_WORKERS];
    if (argc != 2)
        return 2;
    msl_data_root = argv[1];
    for (int i = 0; i < MSL_THREAD_WORKERS; ++i)
        msl_thread_create(&msl_workers[i], i);
    for (int round = 0; round < MSL_THREAD_ROUNDS; ++round) {
        for (int i = 0; i < MSL_THREAD_WORKERS; ++i)
            msl_run_round(&msl_workers[i], round);
    }
    for (int i = 0; i < MSL_THREAD_WORKERS; ++i) {
        expected[i] = msl_workers[i].digest;
        msl_thread_destroy(&msl_workers[i]);
    }
    if (msl_game_data_references() != 0)
        abort();
    for (int i = 0; i < MSL_THREAD_WORKERS; ++i) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, msl_run_thread, &ids[i]) != 0)
            abort();
    }
    for (int i = 0; i < MSL_THREAD_WORKERS; ++i) {
        pthread_join(threads[i], NULL);
        if (msl_workers[i].digest != expected[i]) {
            fprintf(stderr, "thread %d differs: %016lx != %016lx\n", i,
                    (unsigned long)msl_workers[i].digest, (unsigned long)expected[i]);
            return 1;
        }
    }
    if (msl_game_data_references() != 0)
        abort();

    // Fork after preloading and after the thread lifetime tests have completed.
    MSL_CHECK(msl_game_data_acquire(msl_data_root));
    pid_t child = fork();
    if (child < 0)
        abort();
    if (child == 0) {
        msl_thread_create(&msl_workers[0], 0);
        msl_run_round(&msl_workers[0], 0);
        msl_thread_destroy(&msl_workers[0]);
        if (msl_game_data_references() != 1)
            _exit(1);
        msl_game_data_release();
        _exit(msl_game_data_references() != 0);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 1;
    if (msl_game_data_references() != 1)
        abort();
    msl_game_data_release();
    printf("thread batches: %d serial/concurrent env-frames each; copy, restore, "
           "migration, lifetime and fork passed\n",
           MSL_THREAD_WORKERS * MSL_THREAD_ENVS * MSL_THREAD_ROUNDS *
               MSL_THREAD_FRAMES);
    return 0;
}
