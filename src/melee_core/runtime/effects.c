#include "effects.h"

#include "cm/camera.h"
#include "ef/efasync.h"
#include "ef/efsync.h"
#include "ef/types.h"
#include "lb/lb_00B0.h"
#include "lb/lbarchive.h"

#include <baselib/gobj.h>
#include <baselib/gobjproc.h>
#include <baselib/psstructs.h>
#include <baselib/random.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSL_EFFECT_QUEUE_CAPACITY = 256 };

typedef struct MslEffectQueueNode {
    struct MslEffectQueueNode* next;
    u8 spawn_kind;
    s32 gfx_id;
    HSD_JObj* jobj;
    Vec3 params;
} MslEffectQueueNode;

typedef struct MslEffectGeneratorBank {
    HSD_Archive* archive;
    int* command_bank;
    HSD_PSCmdList** commands;
    s32 count;
} MslEffectGeneratorBank;

static MslEffectQueueNode msl_effect_nodes[MSL_EFFECT_QUEUE_CAPACITY];
static MslEffectQueueNode* msl_effect_free;
static MslEffectGeneratorBank msl_effect_banks[4];

static void msl_effect_load_bank(int bank, const char* filename,
                                 const char* symbol)
{
    void** table = NULL;
    int* command_bank;
    u16 version;

    msl_effect_banks[bank].archive =
        lbArchive_80016DBC(filename, &table, symbol, NULL);
    if (table == NULL || table[0] == NULL) {
        fprintf(stderr, "%s is missing %s command data\n", filename, symbol);
        abort();
    }
    command_bank = table[0];
    msl_effect_banks[bank].command_bank = command_bank;
    version = *(u16*) command_bank;
    switch (version) {
    case 0:
        msl_effect_banks[bank].count = command_bank[1];
        msl_effect_banks[bank].commands =
            (HSD_PSCmdList**) (command_bank + 2);
        break;
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
        msl_effect_banks[bank].count = command_bank[1] + command_bank[2];
        msl_effect_banks[bank].commands =
            (HSD_PSCmdList**) (command_bank + 3 - command_bank[1]);
        break;
    default:
        fprintf(stderr, "%s has unsupported particle command version %x\n",
                filename, version);
        abort();
    }
}

void msl_effect_projection_init(void)
{
    int i;

    memset(msl_effect_banks, 0, sizeof(msl_effect_banks));
    msl_effect_load_bank(0, "/EfCoData.dat", "effCommonDataTable");
    msl_effect_load_bank(3, "/EfFxData.dat", "effFoxDataTable");

    msl_effect_free = NULL;
    for (i = MSL_EFFECT_QUEUE_CAPACITY - 1; i >= 0; --i) {
        msl_effect_nodes[i].next = msl_effect_free;
        msl_effect_free = &msl_effect_nodes[i];
    }
}

static MslEffectQueueNode* msl_effect_alloc(void)
{
    MslEffectQueueNode* node = msl_effect_free;
    if (node == NULL) {
        fprintf(stderr, "headless effect queue exhausted (%d nodes)\n",
                MSL_EFFECT_QUEUE_CAPACITY);
        abort();
    }
    msl_effect_free = node->next;
    memset(node, 0, sizeof(*node));
    return node;
}

static void msl_effect_release(MslEffectQueueNode* node)
{
    node->next = msl_effect_free;
    msl_effect_free = node;
}

static void msl_effect_consume_generator_rng(s32 generator_id)
{
    int bank = generator_id / 1000;
    MslEffectGeneratorBank* data;
    HSD_PSCmdList* command;

    if (bank < 0 || bank >= (int) (sizeof(msl_effect_banks) /
                                   sizeof(msl_effect_banks[0])))
    {
        return;
    }
    data = &msl_effect_banks[bank];
    if (generator_id < 0 || generator_id >= data->count ||
        data->commands == NULL)
    {
        return;
    }
    // Particle command entries are bank-relative offsets, not ordinary DAT
    // relocations. psInitDataBankLocate adds the command-bank base in place;
    // keep the immutable archive bytes and perform the same relocation here.
    // refs/melee/src/sysdolphin/baselib/particle.c::psInitDataBankLocate
    command = data->commands[generator_id];
    if (command != NULL) {
        command = (HSD_PSCmdList*) ((u8*) data->command_bank + (u32) command);
    }
    if (command != NULL && !(command->kind & 0x100) && command->random >= 0.0F)
    {
        // refs/melee/src/sysdolphin/baselib/particle.c::hsd_8039F05C
        (void) HSD_Randf();
    }
}

static void msl_effect_consume_common_dispatch_generator(s32 gfx_id)
{
    s32 generator_id = -1;

    // Direct projection of the generator-producing cases in
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch. Model-backed
    // effects do not touch HSD RNG during creation and are absent headlessly.
    switch (gfx_id) {
    case 0x3E9: generator_id = 0xC; break;
    case 0x3EA: generator_id = 0x14; break;
    case 0x3ED:
        msl_effect_consume_generator_rng(0x50);
        generator_id = 0x54;
        break;
    case 0x3EF:
    case 0x3F0: generator_id = 0x42; break;
    case 0x3F1:
    case 0x3F2: generator_id = 0x14B; break;
    case 0x3F3: generator_id = 0xB; break;
    case 0x3F4: generator_id = 0x48; break;
    case 0x3FE: generator_id = 0x107; break;
    case 0x400:
    case 0x401: generator_id = 0x5A; break;
    case 0x402: generator_id = 0x59; break;
    case 0x403: generator_id = 0x5E; break;
    case 0x405: generator_id = 0x2C; break;
    case 0x407: generator_id = 0x3C; break;
    case 0x408: generator_id = 0x3E; break;
    case 0x409: generator_id = 0xE2; break;
    case 0x40A: generator_id = 0x241; break;
    case 0x40B: generator_id = 0x242; break;
    case 0x40C:
    case 0x40D: generator_id = 0x19; break;
    case 0x40E: generator_id = 0x43; break;
    case 0x40F: generator_id = 0xE3; break;
    case 0x410: generator_id = 0x22A; break;
    case 0x411: generator_id = 0x4B; break;
    case 0x412: generator_id = 0x13; break;
    case 0x413: generator_id = 0x37; break;
    case 0x414: generator_id = 0xE1; break;
    case 0x416: generator_id = 0x196; break;
    case 0x41B: generator_id = 0x31; break;
    case 0x41C: generator_id = 0x5D; break;
    case 0x41E: generator_id = 0x55; break;
    case 0x41F: generator_id = 0x5C; break;
    case 0x420: generator_id = 0x159; break;
    case 0x421: generator_id = 0x3F; break;
    case 0x422: generator_id = 0x5B; break;
    case 0x425: generator_id = 0x7E; break;
    case 0x426: generator_id = 0x7F; break;
    case 0x428:
    case 0x43F: generator_id = 0xCA; break;
    case 0x429: generator_id = 0xCE; break;
    case 0x42A: generator_id = 0xCF; break;
    case 0x42D: generator_id = 0x121; break;
    case 0x42E: generator_id = 0x13C; break;
    case 0x430: generator_id = 0x140; break;
    case 0x432: generator_id = 0x145; break;
    case 0x433: generator_id = 0x115; break;
    case 0x434: generator_id = 0x14D; break;
    case 0x435: generator_id = 0x14E; break;
    case 0x436: generator_id = 0x153; break;
    case 0x437: generator_id = 0x156; break;
    case 0x43A: generator_id = 0x193; break;
    case 0x43B: generator_id = 0x192; break;
    case 0x43C: generator_id = 0x1A0; break;
    case 0x43D: generator_id = 0x1AF; break;
    case 0x440: generator_id = 0x1D8; break;
    case 0x441: generator_id = 0x1FB; break;
    case 0x442: generator_id = 0x1DC; break;
    case 0x443: generator_id = 0x1F1; break;
    case 0x444: generator_id = 0x1FF; break;
    case 0x445: generator_id = 0x209; break;
    case 0x446: generator_id = 0x1B; break;
    case 0x448: generator_id = 0x92; break;
    case 0x44A:
    case 0x44B:
    case 0x44C: generator_id = 0x237; break;
    case 0x44D: generator_id = 0x48; break;
    case 0x44E:
    case 0x457: generator_id = 0xDA; break;
    case 0x44F:
    case 0x458: generator_id = 0xDB; break;
    case 0x450:
    case 0x459: generator_id = 0xDC; break;
    case 0x451:
    case 0x45A: generator_id = 0xDD; break;
    case 0x452: generator_id = 0x21E; break;
    case 0x453: generator_id = 0x234; break;
    case 0x454: generator_id = 0x235; break;
    case 0x455: generator_id = 0x236; break;
    case 0x456: generator_id = 0x23D; break;
    case 0x45B: generator_id = 0x8C; break;
    case 0x45C: generator_id = 0x8D; break;
    case 0x45D: generator_id = 0x23F; break;
    case 0x45E: generator_id = 0x240; break;
    case 0x45F: generator_id = 0x8E; break;
    case 0x460: generator_id = 0x99; break;
    case 0x461: generator_id = 0x95; break;
    case 0x462: generator_id = 0x219; break;
    case 0x465: generator_id = 0x88; break;
    case 0x466: generator_id = 0x89; break;
    case 0x467: generator_id = 0x8A; break;
    case 0x46A: generator_id = 0xAC; break;
    case 0x46C: generator_id = 0x9E; break;
    case 0x46E: generator_id = 0xAE; break;
    case 0x46F: generator_id = 0xA0; break;
    case 0x470: generator_id = 0x21B; break;
    case 0x471: generator_id = 0x220; break;
    case 0x472: generator_id = 0x131; break;
    case 0x473: generator_id = 0x82; break;
    case 0x474: generator_id = 0xF7; break;
    case 0x475: generator_id = 0xFC; break;
    case 0x476: generator_id = 0xFF; break;
    case 0x477: generator_id = 0x7918; break;
    }
    if (generator_id >= 0) {
        msl_effect_consume_generator_rng(generator_id);
    }
}

void* efSync_Spawn(s32 gfx_id, HSD_GObj* gobj, ...)
{
    (void) gobj;
    // Visual objects are excluded, but immediate generator initialization and
    // explicit dispatch randomness share the gameplay HSD stream. The lookup
    // above reads `kind` and `random` directly from EfCoData/EfFxData and
    // applies hsd_8039F05C's source gate.
    // refs/melee/src/melee/ef/{efsync.c,efasync.c,efalt.c}
    // refs/melee/src/sysdolphin/baselib/particle.c::hsd_8039F05C
    if (gfx_id < 0x250 || gfx_id / 1000 == 0x1E) {
        msl_effect_consume_generator_rng(gfx_id);
    } else if (gfx_id < 0x478) {
        msl_effect_consume_common_dispatch_generator(gfx_id);
    } else if (gfx_id == 0x48D) {
        // refs/melee/src/melee/ef/efalt.c::efAlt_Spawn Fox reflector pulse.
        msl_effect_consume_generator_rng(0xBC0);
    } else {
        switch (gfx_id) {
        case 0x4BD: msl_effect_consume_generator_rng(0x1B58); break;
        case 0x4BE: msl_effect_consume_generator_rng(0x1B5C); break;
        case 0x4BF:
            msl_effect_consume_generator_rng(0x1B5D);
            msl_effect_consume_generator_rng(0x5F);
            break;
        case 0x4C3: msl_effect_consume_generator_rng(0x24C); break;
        case 0x4CE: msl_effect_consume_generator_rng(0x2328); break;
        case 0x4D1: msl_effect_consume_generator_rng(0x64); break;
        case 0x4D3:
            msl_effect_consume_generator_rng(0x172);
            msl_effect_consume_generator_rng(0x173);
            break;
        case 0x4D4: msl_effect_consume_generator_rng(0x11E); break;
        case 0x4D8: msl_effect_consume_generator_rng(0x61); break;
        case 0x4DF: msl_effect_consume_generator_rng(0x143); break;
        case 0x4E3: msl_effect_consume_generator_rng(0x18A); break;
        case 0x4E4: msl_effect_consume_generator_rng(0x194); break;
        case 0x4E5: msl_effect_consume_generator_rng(0x17D); break;
        case 0x4E6: msl_effect_consume_generator_rng(0x17E); break;
        case 0x4E7: msl_effect_consume_generator_rng(0x196); break;
        case 0x4F8: msl_effect_consume_generator_rng(0x6E); break;
        case 0x4F9: msl_effect_consume_generator_rng(0x1C8); break;
        case 0x4FA: msl_effect_consume_generator_rng(0x166); break;
        case 0x4FB: msl_effect_consume_generator_rng(0x71); break;
        case 0x502: msl_effect_consume_generator_rng(0x1A6); break;
        case 0x503: msl_effect_consume_generator_rng(0x6A); break;
        case 0x504: msl_effect_consume_generator_rng(0x6D); break;
        case 0x505: msl_effect_consume_generator_rng(0x79); break;
        case 0x50A: msl_effect_consume_generator_rng(0x5F); break;
        }
    }

    switch (gfx_id) {
    case 0x3E8:
        (void) HSD_Randi(8);
        break;
    case 0x3EC:
    case 0x3EE:
        (void) HSD_Randf();
        break;
    case 0x427: {
        int i;
        for (i = 0; i < 6; ++i) {
            (void) HSD_Randf();
            (void) HSD_Randf();
        }
        break;
    }
    case 0x4CF:
    case 0x4D0: {
        int i;
        for (i = 0; i < 12; ++i) {
            (void) HSD_Randf();
            (void) HSD_Randf();
            (void) HSD_Randf();
        }
        break;
    }
    }
    return NULL;
}

static void msl_effect_process(HSD_GObj* gobj, MslEffectQueueNode* node)
{
    if (node->spawn_kind == EF_SPAWN_CAMERA_SHAKE) {
        Vec3 position;
        lb_8000B1CC(node->jobj, &node->params, &position);
        Camera_80030E44(node->gfx_id, &position);
    } else {
        efSync_Spawn(node->gfx_id, gobj);
    }
}

void efAsync_QueueFlush(HSD_GObj* gobj, void* queue_head)
{
    MslEffectQueueNode* node = ((MslEffectQueueNode*) queue_head)->next;
    while (node != NULL) {
        MslEffectQueueNode* next = node->next;
        msl_effect_process(gobj, node);
        msl_effect_release(node);
        node = next;
    }
    ((MslEffectQueueNode*) queue_head)->next = NULL;
}

void efAsync_QueueClear(void* queue_head)
{
    MslEffectQueueNode* node = ((MslEffectQueueNode*) queue_head)->next;
    while (node != NULL) {
        MslEffectQueueNode* next = node->next;
        msl_effect_release(node);
        node = next;
    }
    ((MslEffectQueueNode*) queue_head)->next = NULL;
}

void efAsync_Spawn(HSD_GObj* gobj, void* queue_head, u32 spawn_kind,
                   u32 gfx_id, HSD_JObj* jobj, ...)
{
    MslEffectQueueNode* node = msl_effect_alloc();

    node->spawn_kind = spawn_kind;
    node->gfx_id = gfx_id;
    node->jobj = jobj;
    if (spawn_kind == EF_SPAWN_CAMERA_SHAKE) {
        va_list args;
        va_start(args, jobj);
        node->params = *va_arg(args, Vec3*);
        va_end(args);
    }

    // Source inserts at the head while a lower-priority GObj process is
    // running, then Fighter's queue owner flushes in that reverse order.
    // refs/melee/src/melee/ef/efasync.c::{efAsync_Spawn,efAsync_QueueFlush}
    if (HSD_GObj_804D7838 != NULL && HSD_GObj_804D7838->s_link < 9U) {
        node->next = ((MslEffectQueueNode*) queue_head)->next;
        ((MslEffectQueueNode*) queue_head)->next = node;
    } else {
        msl_effect_process(gobj, node);
        msl_effect_release(node);
    }
}
