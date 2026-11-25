// Exercise temporary model-class overrides against another thread's loads,
// and match resets against readers of the preloaded immutable tables.
#include "ft/forward.h"
#include "ft/ftparts.h"
#include "it/it_3F14.h"
#include "runtime/scalar.h"

#include <baselib/jobj.h>
#include <baselib/pobj.h>
#include <baselib/spline.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PART_IDS (UINT8_MAX + 1)

static const MslCoreGameData* game_data;
static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static int ready;
static int started;
static atomic_int stop;
static u32 expected_part_flags[FTKIND_MAX][PART_IDS];
static it_804D6D20_t* expected_item_table;

static HSD_Joint interp_joint;
static HSD_Spline spline;
static HSD_Joint spline_joint = { .flags = JOBJ_SPLINE, .u.spline = &spline };

typedef struct Worker {
    MslCoreMatch match;
    MslCoreMatchConfig config;
    int init_failed;
    unsigned long runs;
    unsigned long wrong;
} Worker;

static void config_init(MslCoreMatchConfig* config)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = 32;
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = 2;
    config->stock_count = 4;
    config->players[0].char_id = 1;
    config->players[1].char_id = 1;
}

static int worker_begin(Worker* worker)
{
    MslCoreInput input = { 0 };
    int ok;
    config_init(&worker->config);
    ok = msl_core_match_init(&worker->match, game_data, &worker->config,
                             &input) == 0;
    if (ok) {
        msl_core_bind_match(&worker->match);
    } else {
        worker->init_failed = 1;
    }
    pthread_mutex_lock(&start_mutex);
    ++ready;
    pthread_cond_broadcast(&start_cond);
    while (!started) {
        pthread_cond_wait(&start_cond, &start_mutex);
    }
    pthread_mutex_unlock(&start_mutex);
    return ok;
}

static void worker_end(Worker* worker)
{
    if (!worker->init_failed) {
        msl_core_match_destroy(&worker->match);
    }
}

static void* interp_loader(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        HSD_JObjRemoveAll(ftParts_8007482C(&interp_joint));
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* spline_loader(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        HSD_JObj* jobj = HSD_JObjLoadJoint(&spline_joint);
        if (jobj->u.spline != &spline) {
            ++worker->wrong;
        }
        HSD_JObjRemoveAll(jobj);
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* pobj_class_setter(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        ftPartsPObjSetDefaultClass();
        ftPartsPObjClearDefaultClass();
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* pobj_class_reader(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        worker->wrong += HSD_PObjGetDefaultClass() != &hsdPObj;
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* match_resetter(void* arg)
{
    Worker* worker = arg;
    MslCoreInput input = { 0 };
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        if (msl_core_match_reset(&worker->match, game_data, &worker->config,
                                 &input) != 0)
        {
            worker->init_failed = 1;
            break;
        }
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* part_flag_reader(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    int kind;
    int part;
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        for (kind = 0; kind < FTKIND_MAX; ++kind) {
            for (part = 0; part < PART_IDS; ++part) {
                worker->wrong +=
                    ftParts_8007506C(kind, part) !=
                    expected_part_flags[kind][part];
            }
        }
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static void* archive_symbol_reader(void* arg)
{
    Worker* worker = arg;
    int ok = worker_begin(worker);
    while (ok && !atomic_load_explicit(&stop, memory_order_relaxed)) {
        worker->wrong +=
            __atomic_load_n(&it_804D6D20, __ATOMIC_RELAXED) != expected_item_table;
        ++worker->runs;
    }
    worker_end(worker);
    return NULL;
}

static int run_case(const char* name, void* (*writer)(void*),
                    void* (*reader)(void*))
{
    static Worker writer_worker;
    static Worker reader_worker;
    pthread_t writer_thread;
    pthread_t reader_thread;
    struct timespec run = { 0, 250000000 };

    memset(&writer_worker, 0, sizeof(writer_worker));
    memset(&reader_worker, 0, sizeof(reader_worker));
    atomic_store_explicit(&stop, 0, memory_order_relaxed);
    ready = started = 0;
    if (pthread_create(&writer_thread, NULL, writer, &writer_worker) != 0 ||
        pthread_create(&reader_thread, NULL, reader, &reader_worker) != 0)
    {
        fprintf(stderr, "%s: cannot create worker threads\n", name);
        abort();
    }
    pthread_mutex_lock(&start_mutex);
    while (ready != 2) {
        pthread_cond_wait(&start_cond, &start_mutex);
    }
    started = 1;
    pthread_cond_broadcast(&start_cond);
    pthread_mutex_unlock(&start_mutex);
    nanosleep(&run, NULL);
    atomic_store_explicit(&stop, 1, memory_order_relaxed);
    pthread_join(writer_thread, NULL);
    pthread_join(reader_thread, NULL);

    if (writer_worker.init_failed || reader_worker.init_failed) {
        fprintf(stderr, "%s: Match init or reset failed\n", name);
        return 0;
    }
    printf("%s: %lu writer runs, %lu reader runs, %lu wrong reads\n", name,
           writer_worker.runs, reader_worker.runs, reader_worker.wrong);
    if (writer_worker.runs == 0 || reader_worker.runs == 0) {
        fprintf(stderr, "%s: a thread never ran\n", name);
        return 0;
    }
    if (reader_worker.wrong != 0) {
        fprintf(stderr, "%s: another thread's Match changed shared state\n",
                name);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv)
{
    static MslCoreGameData data;
    int ok;
    int kind;
    int part;

    if (argc != 2 || msl_core_game_data_init(&data, argv[1]) != 0) {
        fprintf(stderr, "usage: %s <data root>\n", argv[0]);
        return 1;
    }
    game_data = &data;
    expected_item_table = it_804D6D20;
    for (kind = 0; kind < FTKIND_MAX; ++kind) {
        for (part = 0; part < PART_IDS; ++part) {
            expected_part_flags[kind][part] = ftParts_8007506C(kind, part);
        }
    }
    ok = run_case("default class", interp_loader, spline_loader);
    ok &= run_case("pobj default class", pobj_class_setter, pobj_class_reader);
    ok &= run_case("part flags", match_resetter, part_flag_reader);
    ok &= run_case("archive symbols", match_resetter, archive_symbol_reader);
    return ok ? 0 : 1;
}
