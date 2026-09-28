// Matches stepped on different threads share only immutable GameData; each
// case runs one thread through a source path that used to write shared state
// while a second thread reads what that write disturbed.
//
// default class: ftParts_8007482C selects ftIntpJObj as the default JObj class
// around its load. A spline joint loaded on another thread meanwhile came back
// as ftIntpJObj without its spline, and its path animation then failed the
// jobj.c jp->u.spline assert (or read a NULL class and crashed).
//
// part flags: every Match reset reran Fighter_LoadCommonData, which cleared
// and refilled GameData's fighter part flags. A Fighter_Create on another
// thread meanwhile counted the wrong skeleton nodes and failed the filtered
// JObj load's node-count assert.
//
// archive symbols: every Match reset reloads ItCo's symbol table into
// GameData's item pointers through lbArchive_80017040, which stored NULL
// before each lookup. it_8027870C on another thread meanwhile read the NULL
// table and crashed on its first field.
#include "ft/forward.h"
#include "ft/ftparts.h"
#include "it/it_3F14.h"
#include "runtime/scalar.h"

#include <baselib/jobj.h>
#include <baselib/spline.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RUN_SECONDS 2
#define PART_IDS (UINT8_MAX + 1)

static const MslCoreGameData* game_data;
static pthread_barrier_t start;
static volatile int stop;

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
    pthread_barrier_wait(&start);
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
    while (ok && !stop) {
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
    while (ok && !stop) {
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

static void* match_resetter(void* arg)
{
    Worker* worker = arg;
    MslCoreInput input = { 0 };
    int ok = worker_begin(worker);
    while (ok && !stop) {
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
    static u32 expected[FTKIND_MAX][PART_IDS];
    Worker* worker = arg;
    int ok = worker_begin(worker);
    int kind;
    int part;
    for (kind = 0; kind < FTKIND_MAX; ++kind) {
        for (part = 0; part < PART_IDS; ++part) {
            expected[kind][part] = ftParts_8007506C(kind, part);
        }
    }
    while (ok && !stop) {
        for (kind = 0; kind < FTKIND_MAX; ++kind) {
            for (part = 0; part < PART_IDS; ++part) {
                worker->wrong +=
                    ftParts_8007506C(kind, part) != expected[kind][part];
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
    it_804D6D20_t* expected = it_804D6D20;
    while (ok && !stop) {
        worker->wrong +=
            __atomic_load_n(&it_804D6D20, __ATOMIC_RELAXED) != expected;
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
    struct timespec run = { RUN_SECONDS, 0 };

    memset(&writer_worker, 0, sizeof(writer_worker));
    memset(&reader_worker, 0, sizeof(reader_worker));
    stop = 0;
    pthread_barrier_init(&start, NULL, 3);
    pthread_create(&writer_thread, NULL, writer, &writer_worker);
    pthread_create(&reader_thread, NULL, reader, &reader_worker);
    pthread_barrier_wait(&start);
    nanosleep(&run, NULL);
    stop = 1;
    pthread_join(writer_thread, NULL);
    pthread_join(reader_thread, NULL);
    pthread_barrier_destroy(&start);

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

    if (argc != 2 || msl_core_game_data_init(&data, argv[1]) != 0) {
        fprintf(stderr, "usage: %s <data root>\n", argv[0]);
        return 1;
    }
    game_data = &data;
    ok = run_case("default class", interp_loader, spline_loader);
    ok &= run_case("part flags", match_resetter, part_flag_reader);
    ok &= run_case("archive symbols", match_resetter, archive_symbol_reader);
    return ok ? 0 : 1;
}
