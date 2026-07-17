#ifndef GALE01_0679B0
#define GALE01_0679B0

#include <placeholder.h>
#include <platform.h>

#include "ft/inlines.h" // IWYU pragma: export
#include "ft/types.h"

#include <baselib/forward.h>

#include <dolphin/mtx.h>
#include <baselib/objalloc.h>

enum {
    FIGHTER_PARTS_ALLOC_COUNT = 0x8C0 / 0x10,
    FIGHTER_DOBJ_ALLOC_COUNT = 0x1F0 / 4,
    FIGHTER_X2040_ALLOC_COUNT = 0x80 / 4,
};

struct Fighter_804D64FC_t {
    u8** cmdscripts; ///< +00 per-character command script arrays
    void** x4;       ///< +04 ground attack tables (per character)
    void** x8;       ///< +08 air attack tables (per character)
    UNK_T* xC;       ///< +0C ranged/projectile attack tables
    void** x10;      ///< +10 smash attack tables (per character)
    void** x14;      ///< +14 special action tables (per character)
    void** x18;      ///< +18 weapon attack tables (per character)
    void** x1C;      ///< +1C edge guard tables (per character)
    float* x20;      ///< +20 distance thresholds (per character)
    void* x24;       ///< +24 weapon reach bonus table
};

struct plAllocInfo;

/* 0679B0 */ void Fighter_800679B0(void);
/* 067A84 */ void Fighter_FirstInitialize_80067A84(void);
/* 067ABC */ void Fighter_LoadCommonData(void);
/* 067BB4 */ void Fighter_UpdateModelScale(Fighter_GObj* gobj);
/* 067C98 */ void Fighter_UnkInitReset_80067C98(Fighter*);
/* 068354 */ void Fighter_UnkProcessDeath_80068354(Fighter_GObj* gobj);
/* 0686E4 */ void Fighter_UnkUpdateCostumeJoint_800686E4(Fighter_GObj* gobj);
/* 06876C */ void Fighter_UnkUpdateVecFromBones_8006876C(Fighter* fp);
/* 068854 */ void Fighter_ResetInputData_80068854(Fighter_GObj* gobj);
/* 068914 */ void Fighter_UnkInitLoad_80068914(Fighter_GObj* gobj,
                                               struct plAllocInfo* argdata);
/* 068E40 */ u32 Fighter_NewSpawn_80068E40(void);
/* 068E64 */ void Fighter_80068E64(Fighter_GObj* gobj);
/* 068E98 */ Fighter_GObj* Fighter_Create(struct plAllocInfo* input);
/* 0693AC */ void Fighter_ChangeMotionState(Fighter_GObj* gobj,
                                            FtMotionId msid, MotionFlags flags,
                                            f32 anim_start, f32 anim_speed,
                                            f32 anim_blend,
                                            Fighter_GObj* arg3);
/* 06A1BC */ void Fighter_8006A1BC(Fighter_GObj* gobj);
/* 06A360 */ void Fighter_8006A360(Fighter_GObj* gobj);
/* 06ABA0 */ void Fighter_8006ABA0(Fighter_GObj* gobj);
/* 06ABEC */ void Fighter_UnkIncrementCounters_8006ABEC(Fighter_GObj* gobj);
/* 06AD10 */ void Fighter_Spaghetti_8006AD10(Fighter_GObj* gobj);
/* 06B82C */ void Fighter_procUpdate(Fighter_GObj* gobj);
/* 06C0F0 */ void Fighter_UnkApplyTransformation_8006C0F0(Fighter_GObj* gobj);
/* 06C27C */ void Fighter_procMap(Fighter_GObj* gobj);
/* 06C5F4 */ void Fighter_8006C5F4(Fighter_GObj* gobj);
/* 06C624 */ void Fighter_CallAcessoryCallbacks_8006C624(Fighter_GObj* gobj);
/* 06C80C */ void Fighter_8006C80C(Fighter_GObj* gobj);
/* 06CA5C */ void Fighter_UnkProcessGrab_8006CA5C(Fighter_GObj* gobj);
/* 06CB94 */ void Fighter_8006CB94(Fighter_GObj* gobj);
/* 06CC30 */ void Fighter_UnkTakeDamage_8006CC30(Fighter* fp,
                                                 float damage_amount);
/* 06CC7C */ void Fighter_TakeDamage_8006CC7C(Fighter*, float);
/* 06CDA4 */ void Fighter_8006CDA4(Fighter* fp, s32 arg1);
/* 06CF5C */ void Fighter_8006CF5C(Fighter* fp, s32 arg1);
/* 06CFBC */ void Fighter_UnkSetFlag_8006CFBC(Fighter_GObj* gobj);
/* 06CFE0 */ void Fighter_8006CFE0(Fighter_GObj* gobj);
/* 06D044 */ void Fighter_UnkRecursiveFunc_8006D044(Fighter_GObj* gobj);
/* 06D10C */ void Fighter_8006D10C(Fighter_GObj* gobj);
/* 06D1EC */ void Fighter_ProcessHit_8006D1EC(Fighter_GObj* gobj);
/* 06D9AC */ void Fighter_8006D9AC(Fighter_GObj* gobj);
/* 06D9EC */ void Fighter_UnkCallCameraCallback_8006D9EC(Fighter_GObj* gobj);
/* 06DA4C */ void Fighter_8006DA4C(Fighter_GObj* gobj);
/* 06DABC */ void Fighter_Unload_8006DABC(void* user_data);
/* 458FD0 */ extern HSD_ObjAllocData fighter_alloc_data;
/* 458FFC */ extern HSD_ObjAllocData fighter_dat_attrs_alloc_data;
/* 459028 */ extern HSD_ObjAllocData fighter_parts_alloc_data;
/* 459054 */ extern HSD_ObjAllocData fighter_dobj_list_alloc_data;
/* 459080 */ extern HSD_ObjAllocData fighter_x2040_alloc_data;
/* 4590AC */ extern HSD_ObjAllocData fighter_x59C_alloc_data;
#ifndef MSL_CORE_HOSTED
/* 4598B8 */ extern ftData* gFtDataList[FTKIND_MAX];
#endif
/* 4D6518 */ struct Fighter_804D6518_t {
    f32 x0; ///< gravity mult
    f32 x4; ///< weight mult
};

/* 4D651C */ struct Fighter_804D651C_t {
    f32 x0;
    f32 x4;  ///< jump y impulse fullhop
    f32 x8;  ///< jump y impulse shorthop
    f32 xC;  ///< gravity
    f32 x10; ///< fall speed
    f32 x14; ///< fast fall speed
    f32 x18; ///< weight
    f32 x1C; ///< ledge jump y impulse
    f32 x20; ///< wall jump y impulse
}; ///< metal modifiers - used in 0x800d105c

/* 4D6520 */ struct Fighter_804D6520_t {
    f32 x0;  ///< walk speed scale
    f32 x4;  ///< dash accel a
    f32 x8;  ///< dash accel b
    f32 xC;  ///< dash max speed
    f32 x10; ///< jump x impulse
    f32 x14; ///< jump y impulse fullhop
    f32 x18; ///< jump y impulse shorthop
    f32 x1C; ///< jump x max speed
    f32 x20; ///< gravity
    f32 x24; ///< fall speed
    f32 x28; ///< fast fall speed
    f32 x2C; ///< ledge jump x impulse
    f32 x30; ///< ledge jump y impulse
    f32 x34; ///< wall jump x impulse
    f32 x38; ///< wall jump y impulse
}; ///< bunnyhood modifiers - used in 0x800d105c

/* 4D6524 */ struct Fighter_804D6524_t {
    /// @warning not all comments not confirmed - from altimors ghidra db
    float x0;  ///< knockback recieved mult
    float x4;  ///< damage dealt mult
    float x8;  ///<
    float xC;  ///<
    float x10; ///< walk anim scale
    float x14; ///< walk middle speed
    float x18; ///< walk fast speed mult
    float x1C; ///< dash max speed
    float x20; ///<
    float x24; ///< jump squat mult
    float x28; ///< jump y impulse fullhop mult
    float x2C; ///< jump y impulse shorthop mult
    float x30; ///< gravity mult
    float x34; ///< fall speed mult
    float x38; ///< air accel a mult
    float x3C; ///< air accel b mult
    float x40; ///< air speed mult
    float x44; ///< fast fall speed mult
    float x48; ///< weight mult
    float x4C; ///< shield break y vel mult
    float x50; ///<
    float x54; ///<
    float x58; ///<
    float x5C; ///<
    float x60; ///<
    float x64; ///<
    float x68; ///<
    float x6C; ///< empty land lag mult
    float x70; ///< nair land lag mult
    float x74; ///< fair land lag mult
    float x78; ///< bair land lag mult
    float x7C; ///< uair land lag mult
    float x80; ///< dair land lag mult
    float x84; ///<
    float x88; ///<
    float x8C; ///<
    float x90; ///<
    float x94; ///< cmsubject offset mult
    float x98; ///<
}; ///< fighter scale modifiers - used in 0x800d105c
/* 4D6528 */ struct Fighter_804D6528_t {
    Vec2* x0;
    int x4;
};
struct Fighter_804D652C_t {
    Vec2* x0;
    s32 x4;
};
// refs/melee/src/melee/ft/ft_0D4D.c::ftCo_800D4FF4
/* 4D6534 */ struct Fighter_804D6534_t {
    HSD_Joint* joint;
    HSD_AnimJoint* anim_joint;
};
/* 4D6540 */ struct Fighter_804D6540_t {
    struct Fighter_804D6540_x0_t {
        u8 x0;
        u8 x1;
        u8 x2;
        u8 x3;
    }* x0;
    int x4;
};

#ifdef MSL_CORE_HOSTED
// PlCo.dat's 23 published symbols are immutable GameData. Preserve the source
// names at use sites while resolving them through the active shared owner.
// refs/melee/src/melee/ft/fighter.c::Fighter_LoadCommonData
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_fighter_common_data() (msl_core_context_fighter_common_data)
#else
void** msl_core_fighter_common_data(void);
#endif
#define MSL_FIGHTER_COMMON(type, index)                                       \
    ((type) msl_core_fighter_common_data()[index])
#define p_ftCommonData MSL_FIGHTER_COMMON(ftCommonData*, 0)
#define Fighter_804D6550 MSL_FIGHTER_COMMON(int**, 1)
#define Fighter_804D654C MSL_FIGHTER_COMMON(float(*)[5], 2)
#define Fighter_804D6548 MSL_FIGHTER_COMMON(float*, 3)
#define ftPartsTable MSL_FIGHTER_COMMON(FighterPartsTable**, 4)
#define Fighter_804D6540 MSL_FIGHTER_COMMON(struct Fighter_804D6540_t**, 5)
#define Fighter_804D653C MSL_FIGHTER_COMMON(struct Fighter_804D653C_t*, 6)
#define Fighter_804D6538 MSL_FIGHTER_COMMON(struct Fighter_804D653C_t*, 7)
#define Fighter_804D6534 MSL_FIGHTER_COMMON(struct Fighter_804D6534_t*, 8)
#define Fighter_804D6530 MSL_FIGHTER_COMMON(Vec2**, 9)
#define Fighter_804D652C MSL_FIGHTER_COMMON(struct Fighter_804D652C_t*, 10)
#define Fighter_804D6528 MSL_FIGHTER_COMMON(struct Fighter_804D6528_t*, 11)
#define Fighter_804D6524 MSL_FIGHTER_COMMON(struct Fighter_804D6524_t*, 12)
#define Fighter_804D6520 MSL_FIGHTER_COMMON(struct Fighter_804D6520_t*, 13)
#define Fighter_804D651C MSL_FIGHTER_COMMON(struct Fighter_804D651C_t*, 14)
#define Fighter_804D6518 MSL_FIGHTER_COMMON(struct Fighter_804D6518_t*, 15)
#define Fighter_804D6514 MSL_FIGHTER_COMMON(HSD_Joint*, 16)
#define Fighter_804D6510 MSL_FIGHTER_COMMON(UNK_T, 17)
#define Fighter_804D650C MSL_FIGHTER_COMMON(u8*, 18)
#define Fighter_804D6508 MSL_FIGHTER_COMMON(u8*, 19)
#define Fighter_804D6504 MSL_FIGHTER_COMMON(HSD_Joint*, 20)
#define gCrowdConfig MSL_FIGHTER_COMMON(CrowdConfig*, 21)
#define Fighter_804D64FC MSL_FIGHTER_COMMON(struct Fighter_804D64FC_t*, 22)
#else
extern struct Fighter_804D64FC_t* Fighter_804D64FC;
extern CrowdConfig* gCrowdConfig;
extern HSD_Joint* Fighter_804D6504;
extern u8* Fighter_804D6508;
extern u8* Fighter_804D650C;
extern UNK_T Fighter_804D6510;
extern HSD_Joint* Fighter_804D6514;
extern struct Fighter_804D6518_t* Fighter_804D6518;
extern struct Fighter_804D651C_t* Fighter_804D651C;
extern struct Fighter_804D6520_t* Fighter_804D6520;
extern struct Fighter_804D6524_t* Fighter_804D6524;
extern struct Fighter_804D6528_t* Fighter_804D6528;
extern struct Fighter_804D652C_t* Fighter_804D652C;
extern Vec2** Fighter_804D6530;
extern struct Fighter_804D6534_t* Fighter_804D6534;
extern struct Fighter_804D653C_t* Fighter_804D6538;
extern struct Fighter_804D653C_t* Fighter_804D653C;
extern struct Fighter_804D6540_t** Fighter_804D6540;
extern FighterPartsTable** ftPartsTable;
extern float* Fighter_804D6548;
extern float (*Fighter_804D654C)[5];
extern int** Fighter_804D6550;
extern ftCommonData* p_ftCommonData;
#endif

#endif
