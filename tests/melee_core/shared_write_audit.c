// Shared-write audit: every write, after game data load, to memory that the
// batches of one process share — GameData (its struct and arenas) and the
// process globals. Batches stepped on separate threads race on any such
// write, and one that passes through a transient value (store NULL, look up,
// store) is a crash waiting for the right interleaving.
//
// The shared memory is made read-only, then Matches run through the public
// batch API on one thread with random inputs, resetting finished matches
// with fresh configs as training does. Each faulting write is recorded by
// instruction, let through by single-stepping it, and its page protected
// again, so a run lists every writing site (x86-64 Linux only).
//
//     shared-write-audit <data root> [frames per env]
#include "api.h"
#include "platform/native_dat.h"
#include "runtime/context.h"
#include "runtime/scalar.h"

#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#define ENVS 8
#define MAX_REGIONS 4
#define MAX_SITES 4096
#define MAX_PENDING 8
#define FRAMES 10
#define TRAP_FLAG 0x100
// glibc's x86-64 gregset indices (REG_RIP, REG_EFL); the build's feature
// macros hide the names.
#define GREG_RIP 16
#define GREG_EFL 17

extern uint8_t __data_start[];
extern uint8_t _end[];

typedef struct Region {
    uintptr_t begin;
    uintptr_t end;
    const char* name;
} Region;

typedef struct Site {
    uintptr_t ip;
    uintptr_t first_address;
    int region;
    unsigned long count;
    int frame_count;
    void* frames[FRAMES];
} Site;

// Lives outside the protected memory: the handlers write it.
typedef struct Audit {
    Region regions[MAX_REGIONS];
    int region_count;
    Site sites[MAX_SITES];
    int site_count;
    unsigned long dropped;
    uintptr_t pending[MAX_PENDING];
    int pending_count;
    uintptr_t page;
} Audit;

static Audit* audit;

static int region_of(uintptr_t address)
{
    int r;
    for (r = 0; r < audit->region_count; ++r) {
        if (address >= audit->regions[r].begin &&
            address < audit->regions[r].end)
        {
            return r;
        }
    }
    return -1;
}

static void record(uintptr_t ip, uintptr_t address, int region)
{
    Site* site;
    int s;
    for (s = 0; s < audit->site_count; ++s) {
        if (audit->sites[s].ip == ip) {
            ++audit->sites[s].count;
            return;
        }
    }
    if (audit->site_count == MAX_SITES) {
        ++audit->dropped;
        return;
    }
    site = &audit->sites[audit->site_count++];
    site->ip = ip;
    site->first_address = address;
    site->region = region;
    site->count = 1;
    site->frame_count = backtrace(site->frames, FRAMES);
}

static void on_write_fault(int signal_number, siginfo_t* info, void* context)
{
    ucontext_t* uc = context;
    uintptr_t address = (uintptr_t) info->si_addr;
    uintptr_t page = address & ~(audit->page - 1);
    int region = region_of(address);
    (void) signal_number;
    if (region < 0 || audit->pending_count == MAX_PENDING) {
        signal(SIGSEGV, SIG_DFL);   // a genuine crash: fault again, unhandled
        return;
    }
    record((uintptr_t) uc->uc_mcontext.gregs[GREG_RIP], address, region);
    mprotect((void*) page, audit->page, PROT_READ | PROT_WRITE);
    audit->pending[audit->pending_count++] = page;
    uc->uc_mcontext.gregs[GREG_EFL] |= TRAP_FLAG;
}

static void on_single_step(int signal_number, siginfo_t* info, void* context)
{
    ucontext_t* uc = context;
    int p;
    (void) signal_number;
    (void) info;
    for (p = 0; p < audit->pending_count; ++p) {
        mprotect((void*) audit->pending[p], audit->page, PROT_READ);
    }
    audit->pending_count = 0;
    uc->uc_mcontext.gregs[GREG_EFL] &= ~TRAP_FLAG;
}

static void add_region(const char* name, const void* begin, size_t size)
{
    uintptr_t page = audit->page;
    Region* region = &audit->regions[audit->region_count++];
    region->begin = (uintptr_t) begin & ~(page - 1);
    region->end = ((uintptr_t) begin + size + page - 1) & ~(page - 1);
    region->name = name;
    printf("protect %-10s %#lx..%#lx (%zu KiB)\n", name,
           (unsigned long) region->begin, (unsigned long) region->end,
           (size_t) (region->end - region->begin) / 1024);
}

static uint32_t next_random(uint64_t* state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t) (*state >> 33);
}

static MslMatchConfig random_config(uint64_t* rng)
{
    static const uint32_t stages[] = {
        MSL_STAGE_FOUNTAIN_OF_DREAMS, MSL_STAGE_POKEMON_STADIUM,
        MSL_STAGE_YOSHIS_STORY,       MSL_STAGE_DREAM_LAND_N64,
        MSL_STAGE_BATTLEFIELD,        MSL_STAGE_FINAL_DESTINATION,
    };
    static const uint8_t characters[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    };
    MslMatchConfig config = msl_match_config_default();
    config.stage = stages[next_random(rng) % 6];
    config.random_seed = next_random(rng);
    config.max_frame = 600;
    config.num_players = 2;
    config.players[0].character = characters[next_random(rng) % 26];
    config.players[1].character = characters[next_random(rng) % 26];
    return config;
}

static void random_input(MslInput* input, uint64_t* rng)
{
    static const uint16_t buttons[] = {
        MSL_BUTTON_A, MSL_BUTTON_B, MSL_BUTTON_X, MSL_BUTTON_Y,
        MSL_BUTTON_Z, MSL_BUTTON_L, MSL_BUTTON_R, MSL_BUTTON_D_UP,
    };
    int p;
    memset(input, 0, sizeof(*input));
    for (p = 0; p < 2; ++p) {
        MslInputPlayer* player = &input->players[p];
        uint32_t bits = next_random(rng);
        int b;
        for (b = 0; b < 8; ++b) {
            if ((bits >> b) % 5 == 0) {
                player->buttons |= buttons[b];
            }
        }
        player->main_x = (int8_t) (next_random(rng) % 255 - 127);
        player->main_y = (int8_t) (next_random(rng) % 255 - 127);
        player->c_x = (int8_t) ((bits >> 12) % 4 == 0 ? next_random(rng) % 255 - 127 : 0);
        player->c_y = (int8_t) ((bits >> 14) % 4 == 0 ? next_random(rng) % 255 - 127 : 0);
        player->l = (uint8_t) ((bits >> 16) % 8 == 0 ? 255 : 0);
    }
}

int main(int argc, char** argv)
{
    MslBatch* batch;
    const MslCoreGameData* game_data;
    MslMatchConfig* configs = calloc(ENVS, sizeof(*configs));
    MslInput* inputs = calloc(ENVS, sizeof(*inputs));
    MslObservation* observations = calloc(ENVS, sizeof(*observations));
    MslTerminal* terminals = calloc(ENVS, sizeof(*terminals));
    uint8_t* reset_mask = calloc(ENVS, 1);
    long frames = argc > 2 ? atol(argv[2]) : 30000;
    void* warm[1];
    uint64_t rng = 1;
    unsigned long resets = 0;
    struct sigaction action;
    long f;
    int i;
    int s;

    if (argc < 2 || msl_game_data_acquire(argv[1]) != MSL_OK ||
        msl_batch_create(argv[1], ENVS, &batch) != MSL_OK)
    {
        fprintf(stderr, "usage: %s <data root> [frames per env]\n", argv[0]);
        return 1;
    }
    game_data = msl_core_context_game_data;
    if (game_data == NULL) {
        fprintf(stderr, "game data is not bound after acquire\n");
        return 1;
    }
    backtrace(warm, 1);   // loads the unwinder before any handler needs it

    audit = mmap(NULL, sizeof(*audit), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    audit->page = (uintptr_t) sysconf(_SC_PAGESIZE);
    add_region("globals", __data_start, (size_t) (_end - __data_start));
    add_region("gamedata", game_data, sizeof(*game_data));
    add_region("arena", game_data->memory.arena, game_data->memory.capacity);
    add_region("native-dat", game_data->native_dat.arena,
               game_data->native_dat.arena_used);

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    action.sa_sigaction = on_write_fault;
    sigaction(SIGSEGV, &action, NULL);
    action.sa_sigaction = on_single_step;
    sigaction(SIGTRAP, &action, NULL);
    for (i = 0; i < audit->region_count; ++i) {
        mprotect((void*) audit->regions[i].begin,
                 audit->regions[i].end - audit->regions[i].begin, PROT_READ);
    }

    for (i = 0; i < ENVS; ++i) {
        configs[i] = random_config(&rng);
    }
    if (msl_batch_reset(batch, configs, NULL, observations) != MSL_OK) {
        fprintf(stderr, "initial reset failed\n");
        return 1;
    }
    for (f = 0; f < frames; ++f) {
        int any = 0;
        for (i = 0; i < ENVS; ++i) {
            random_input(&inputs[i], &rng);
        }
        if (msl_batch_step(batch, inputs, observations, terminals) != MSL_OK) {
            fprintf(stderr, "step failed at frame %ld\n", f);
            return 1;
        }
        for (i = 0; i < ENVS; ++i) {
            reset_mask[i] = terminals[i].done;
            if (terminals[i].done) {
                configs[i] = random_config(&rng);
                any = 1;
                ++resets;
            }
        }
        if (any && msl_batch_reset(batch, configs, reset_mask, observations) != MSL_OK) {
            fprintf(stderr, "reset failed at frame %ld\n", f);
            return 1;
        }
    }

    for (i = 0; i < audit->region_count; ++i) {
        mprotect((void*) audit->regions[i].begin,
                 audit->regions[i].end - audit->regions[i].begin,
                 PROT_READ | PROT_WRITE);
    }
    printf("%ld frames x %d envs, %lu match resets: %d writing sites%s\n",
           frames, ENVS, resets, audit->site_count,
           audit->dropped ? " (table full, some dropped)" : "");
    for (s = 0; s < audit->site_count; ++s) {
        Site* site = &audit->sites[s];
        const Region* region = &audit->regions[site->region];
        int k;
        printf("site ip=%#lx region=%s offset=%#lx writes=%lu frames=",
               (unsigned long) site->ip, region->name,
               (unsigned long) (site->first_address - region->begin),
               site->count);
        for (k = 0; k < site->frame_count; ++k) {
            printf("%s%#lx", k ? "," : "", (unsigned long) site->frames[k]);
        }
        printf("\n");
    }
    return 0;
}
