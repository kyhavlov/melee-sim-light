#ifndef MSL_CORE_RUNTIME_SOURCE_STATE_H
#define MSL_CORE_RUNTIME_SOURCE_STATE_H

#include <platform.h>

#include "cm/types.h"
#include "ft/ft_0852.h"
#include "ft/ftdevice.h"
#include "gr/types.h"
#include "it/it_3F14.h"
#include "it/itCharItems.h"
#include "lb/types.h"
#include "mp/types.h"
#include "pl/player.h"

#include <baselib/controller.h>
#include <baselib/objalloc.h>

typedef struct MslSourceGameData {
    pl_804D6470_t* player_common;
    MslItemGameData item;
    struct {
        ftData* data_list[FTKIND_MAX];
        struct UnkCostumeList costume_lists[FTKIND_MAX];
        UnkCostumeStruct fox_costumes[4];
        UnkCostumeStruct falcon_costumes[6];
        UnkCostumeStruct sheik_costumes[5];
        UnkCostumeStruct peach_costumes[5];
        UnkCostumeStruct puff_costumes[5];
        UnkCostumeStruct samus_costumes[5];
        UnkCostumeStruct yoshi_costumes[6];
        UnkCostumeStruct mewtwo_costumes[4];
        UnkCostumeStruct gamewatch_costumes[4];
        UnkCostumeStruct ness_costumes[4];
        UnkCostumeStruct link_costumes[5];
        UnkCostumeStruct clink_costumes[5];
        UnkCostumeStruct popo_costumes[4];
        UnkCostumeStruct nana_costumes[4];
        UnkCostumeStruct donkey_costumes[5];
        UnkCostumeStruct ganon_costumes[5];
        UnkCostumeStruct koopa_costumes[4];
        UnkCostumeStruct pikachu_costumes[4];
        UnkCostumeStruct luigi_costumes[4];
        UnkCostumeStruct mario_costumes[5];
        UnkCostumeStruct drmario_costumes[5];
        UnkCostumeStruct marth_costumes[5];
        UnkCostumeStruct roy_costumes[5];
        UnkCostumeStruct pichu_costumes[4];
        UnkCostumeStruct kirby_costumes[6];
        UnkCostumeStruct zelda_costumes[5];
        UnkCostumeStruct falco_costumes[4];
        HSD_Joint* puff_hat_joints[6];
        /* refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ft_80459B88: the
           copy-ability archives and hat structs, loaded at init. */
        struct ft_80459B88_t kirby_copy;
        /* ftkirby.c::ftKb_Init_803C9ED8..803C9F98 and the kind-indexed
           pointer table ftKb_Init_803C9FC8: per-costume hat model records for
           the Donkey Kong, Jigglypuff, Mewtwo, Falco and Game & Watch copies:
           six {joint, matanim} pointer pairs per kind (retail 0x30 bytes of
           u32, pointer-sized here). */
        void* kirby_costume_hats[5][12];
        void* kirby_costume_hat_table[FTKIND_MAX];
        void* common_data[23];
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
        uint32_t part_flags[FTKIND_MAX][UINT8_MAX + 1];
#endif
        /* refs/melee/src/melee/ft/ftdata.c:ftData_Table_Unk0 */
        ftData_UnkCountStruct animation_data[FTKIND_MAX];
    } fighter;
    /* refs/melee/src/melee/lb/lbspdisplay.c::lb_80014534 */
    struct Fighter_804D653C_t* rumble_data;
} MslSourceGameData;

typedef struct MslPlayerState {
    StaticPlayer slots[PL_SLOT_MAX];
    HSD_ObjAllocData alloc_data;
} MslPlayerState;

typedef struct MslSourceCameraState {
    Camera camera;
    CameraDebugMode debug;
    CmSubject* free_subject;
    CmSubject* subject_array;
    CmSubject* first_subject;
    CmSubject* last_subject;
    HSD_CObj* cobj;
} MslSourceCameraState;

typedef struct MslMpCollState {
    struct {
        int right[9];
        int left[9];
        Vec3 normal;
        u8 pad[4];
    } scratch;
    bool is_ecb_tiny;
    bool (*floor_callback)(Fighter_GObj*, int);
    Fighter_GObj* floor_callback_gobj;
    Event event;
    int collision_epoch;
    int right_candidate_count;
    int left_candidate_count;
    float candidate_max_x;
    int candidate_line_id;
    u32 candidate_flags;
} MslMpCollState;

typedef struct MslMpLibState {
    /* refs/melee/src/melee/mp/mpisland.c:mpIsland_80458E88 */
    struct mpIsland_80458E88_t island_root;
    mpCollisionBox collision_boxes[2];
    /* mpLibLoad/mpPruneEmptyLines mutate the loaded MapLine adjacency. */
    MapCollData data_copy;
    bool did_check_bounding;
    MapCollData* data;
    CollVtx* vertices;
    CollLine* lines;
    CollJoint* joints;
    CollJoint* joint_list_start;
    CollJoint* joint_list_end;
    s32 debug_count;
    s32 debug_printed;
    s32 debug_values[5];
    Vec3 draw_vertices[0x80];
} MslMpLibState;

typedef struct MslFtCollState {
    DmgLogEntry damage_log0[20];
    DmgLogEntry damage_log1[20];
    int damage_log0_count;
    int damage_log1_count;
    s8 contact_scratch[8];
} MslFtCollState;

typedef struct MslFtAnimScratch {
    /* refs/melee/src/melee/ft/ftanim.c:ftAnim_804590D8 */
    HSD_AnimJoint* anim_joints[30];
    HSD_MatAnimJoint* mat_anim_joints[30];
    HSD_Joint* joints[30];
} MslFtAnimScratch;

typedef struct MslLbSpDisplayState {
    /* refs/melee/src/melee/lb/lbspdisplay.static.h:lb_804D63A0..B8 */
    struct lb_804D63A0_t* dynamics_pool;
    struct DynamicsData* dynamics_free;
    struct lb_804D63A8_t* effect_pool;
    struct lb_80011A50_t* effect_free;
    struct lb_80011A50_t* effect_active;
    enum_t effect_state;
    u8 effect_flag;
} MslLbSpDisplayState;

enum {
    MSL_STAGE_POINTER_FOUNTAIN_PARAMS,
    MSL_STAGE_POINTER_BATTLEFIELD_PARAMS,
    MSL_STAGE_POINTER_YOSHI_PARAMS,
    MSL_STAGE_POINTER_STADIUM_PARAMS,
    MSL_STAGE_POINTER_DREAMLAND_PARAMS,
    MSL_STAGE_POINTER_DREAMLAND_EFFECT,
    MSL_STAGE_POINTER_COUNT,
};

typedef struct MslGroundState {
    UnkStage6B0 params;
    u8* map_flags;
    s16 map_epoch;
    grDynamicAttr_UnkStruct dynamic_pool[4];
    grDynamicAttr_UnkStruct* dynamic_active;
    grDynamicAttr_UnkStruct* dynamic_free;
    void* stage_pointers[MSL_STAGE_POINTER_COUNT];
    /* refs/melee/src/melee/gr/grlib.c:grLib_8049EF58 */
    Vec3 stage_positions[4];
    /* refs/melee/src/melee/gr/grzakogenerator.static.h:lbl_8049F030 */
    struct {
        grZakoGenerator_SpawnDesc* x0;
        grZakoGenerator_Data* x4;
        s16 x8;
        u8 xA_b0 : 1;
        u8 xA_b1 : 1;
        u8 xA_b2 : 1;
        u8 xA_b3 : 1;
        u8 xA_b4 : 1;
        u8 xA_b5 : 1;
        u8 xA_b6 : 1;
        u8 xA_b7 : 1;
    } zako_generator;
    /* refs/melee/src/melee/gr/grdatfiles.c:grDatFiles_8049EE10 */
    UnkArchiveStruct dat_files[4];
} MslGroundState;

typedef struct MslSourceMatchState {
    MslPlayerState player;
    HSD_PadStatus pad_master[4];
    HSD_PadStatus pad_game[4];
    HSD_PadStatus pad_copy[4];
    StageInfo stage;
    MslItemState item;
    u32 fighter_spawn_counter;
    /* Isolated joint copies for Kirby hat accessory loads made after the
       match arena seals (ftparts.c::ftParts_800753D4): eight per Kirby
       (the Mewtwo copy inserts seven accessories, mask 0x7F0), taken
       round-robin; a copy's accessories are removed before the next hat
       loads, so the older keys are free again by the time they are reused. */
    HSD_Joint* kirby_iso_joints[8 * 4]; /* 8 * MSL_CORE_MAX_PLAYERS */
    u8 kirby_iso_count;
    u8 kirby_iso_next;
    u16 attack_instance_counter;
    u16 stale_attack_instance_counter;
    MslSourceCameraState camera;
    MslMpCollState mp_coll;
    MslMpLibState mp_lib;
    MslFtDeviceState ft_device;
    MslFtCollState ft_coll;
    MslFtAnimScratch ft_anim;
    MslGroundState ground;
    MslLbSpDisplayState lb_sp_display;
    struct {
        /* refs/melee/src/melee/ft/ft_0852.c::gFtDataList */
        ftData* data_list[FTKIND_MAX];
        ft_8045993C_t state[6];
        int reference_counts[FTKIND_MAX];
    } fighter;
    struct {
        // itsamusgrapple.c::it_802B75FC recomputes the x34..x58 lanes of the
        // grapple article's special attributes in place at every grapple
        // spawn: retail treats the DAT blob as scratch. Hosted GameData is
        // shared and immutable, so each match owns a writable mirror seeded
        // from the article on first use.
        itSamusGrappleAttributes attrs;
        u8 seeded;
    } samus_grapple;
    struct {
        // itlinkhookshot.c::it_link_attr_math recomputes the x2C..x48 lanes
        // of the hookshot article's special attributes in place at every
        // hookshot spawn (it_802A2568): retail treats the DAT blob as
        // scratch. Hosted GameData is shared and immutable, so each match
        // owns one writable mirror per owning kind (the Link and Young Link
        // DAT blobs are distinct), seeded from the article on first use.
        itLinkHookshotAttributes attrs[2];
        u8 seeded[2];
    } link_hookshot;
} MslSourceMatchState;

#endif
