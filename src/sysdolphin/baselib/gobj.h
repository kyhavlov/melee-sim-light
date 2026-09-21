#ifndef GALE01_390730
#define GALE01_390730

#include <placeholder.h>
#include <platform.h>

#include "baselib/forward.h" // IWYU pragma: export

#include "baselib/objalloc.h"

#define HSD_GOBJ_GXLINK_NONE ((u8) 0xFF)
#define HSD_GOBJ_OBJ_NONE 0xFF

#define HSD_GOBJ_CLASS_STAGE 0x3
#define HSD_GOBJ_CLASS_FIGHTER 0x4
#define HSD_GOBJ_CLASS_ITEM 0x6

/// Used by chain-type items in-game to link multiple parts together
#define HSD_GOBJ_CLASS_ITEMLINK 0x7

#define HSD_GOBJ_CLASS_EFFECT 0x8
#define HSD_GOBJ_CLASS_LIGHT 0xB
#define HSD_GOBJ_CLASS_UI 0xE
#define HSD_GOBJ_CLASS_CAMERA 0x13

struct HSD_GObj {
    /*  +0 */ u16 classifier;
    /*  +2 */ u8 p_link;
    /*  +3 */ u8 gx_link;
    /*  +4 */ u8 p_priority;
    /*  +5 */ u8 render_priority;
    /*  +6 */ u8 obj_kind;
    /*  +7 */ u8 user_data_kind;
    /*  +8 */ HSD_GObj* next;
    /*  +C */ HSD_GObj* prev;
    /* +10 */ HSD_GObj* next_gx;
    /* +14 */ HSD_GObj* prev_gx;
    /* +18 */ HSD_GObjProc* proc;
    /* +1C */ GObj_RenderFunc render_cb;
    /* +20 */ u64 gxlink_prios;
    /* +28 */ void* hsd_obj;
    /* +2C */ void* user_data;
    /* +30 */ void (*user_data_remove_func)(void* data);
    /* +34 */ void* x34_unk;
};

typedef void (*GObjFunc)(HSD_Obj*);

struct GObjFuncs {
    struct GObjFuncs* next;
    u8 size;
    GObjFunc* funcs;
};

typedef struct _HSD_GObjLibInitDataType {
    u8 p_link_max;    // 804CE380
    u8 gx_link_max;   // 804CE381
    u8 gproc_pri_max; // 804CE382
    GObjFuncs* funcs; // 804CE384
    u64* unk_2;       // 804CE388
} HSD_GObjLibInitDataType;

/// @todo Belongs in `melee/` somewhere
typedef struct _HSD_GObjList {
    /*  +0 */ HSD_GObj* x0;
    /*  +4 */ HSD_GObj* x4;
    /*  +8 */ HSD_GObj* x8;
    /*  +C */ HSD_GObj* xC;
    /* +10 */ HSD_GObj* x10;
    /* +14 */ HSD_GObj* x14;
    /* +18 */ HSD_GObj* x18;
    /* +1C */ HSD_GObj* x1C;
    /* +20 */ HSD_GObj* fighters;
    /* +24 */ HSD_GObj* items;
    /* +28 */ HSD_GObj* x28;
    /* +2C */ HSD_GObj* x2C; // Effects? (See efLib_SetFlags)
    /* +30 */ HSD_GObj* x30; // Effects? (See efLib_SetFlags)
    /* +34 */ HSD_GObj* x34;
    /* +38 */ HSD_GObj* x38;
    /* +3C */ HSD_GObj* x3C;
    /* +40 */ HSD_GObj* x40;
    /* +44 */ HSD_GObj* x44;
    /* +48 */ HSD_GObj* x48;
} HSD_GObjList;

struct _unk_gobj_struct {
    union {
        u32 flags;
        struct {
            u32 b0 : 1;
            u32 b1 : 1;
            u32 b2 : 1;
            u32 b3 : 1;
        };
    };
    u32 type;
    u8 p_link;
    u8 p_prio;
    HSD_GObj* gobj;
};

#ifndef MSL_CORE_HOSTED
extern struct _unk_gobj_struct HSD_GObj_804CE3E4;
extern GObjFunc* HSD_GObj_804D7810;
extern HSD_GObj* HSD_GObj_804D7814;
extern HSD_GObj* HSD_GObj_804D7818;
extern HSD_GObj* HSD_GObj_804D781C;
extern HSD_GObj** HSD_GObj_804D7820;
extern HSD_GObj** HSD_GObjGXLinkHead;
extern HSD_GObj** plinklow_gobjs;
/// @todo GObjList is a fake type, this is just a double pointer
/// (pointer to array of HSD_GObj*, indexed by p_link)
extern HSD_GObjList* HSD_GObj_Entities;
extern HSD_GObjProc* HSD_GObj_804D7830;
extern s32 HSD_GObj_804D7834;
extern HSD_GObjProc* HSD_GObj_804D7838;
extern s32 HSD_GObj_804D783C;
extern HSD_GObjProc** HSD_GObj_804D7840;
extern HSD_GObjProc** HSD_GObj_804D7844;
extern s8 HSD_GObj_804D7848;
extern u8 HSD_GObj_804D7849;
extern s8 HSD_GObj_804D784A;
extern u8 HSD_GObj_804D784B;

extern HSD_GObjLibInitDataType HSD_GObjLibInitData;
extern HSD_ObjAllocData gobjproc_alloc_data;
extern HSD_ObjAllocData gobj_alloc_data;
#else
typedef struct HSD_GObjContext {
    struct _unk_gobj_struct pending;
    HSD_ObjAllocData proc_alloc;
    HSD_ObjAllocData gobj_alloc;
    HSD_GObjLibInitDataType init_data;
    GObjFunc* funcs;
    HSD_GObj* current_render;
    HSD_GObj* current_render_max;
    HSD_GObj* current_proc_gobj;
    HSD_GObj** gx_link_tail;
    HSD_GObj** gx_link_head;
    HSD_GObj** p_link_tail;
    HSD_GObjList* entities;
    HSD_GObjProc* next_proc;
    s32 current_proc_priority;
    HSD_GObjProc* current_proc;
    HSD_GObjProc* scheduled_proc;
    s32 proc_epoch;
    u64 proc_link_mask;
    HSD_GObjProc** proc_heads;
    HSD_GObjProc** proc_tails;
    s8 obj_none;
    u8 jobj_kind;
    s8 lobj_kind;
    u8 cobj_kind;
} HSD_GObjContext;

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_gobj_context() (msl_core_context_gobj)
#else
HSD_GObjContext* msl_core_gobj_context(void);
#endif

#define HSD_GObj_804CE3E4 (msl_core_gobj_context()->pending)
#define gobjproc_alloc_data (msl_core_gobj_context()->proc_alloc)
#define gobj_alloc_data (msl_core_gobj_context()->gobj_alloc)
#define HSD_GObjLibInitData (msl_core_gobj_context()->init_data)
#define HSD_GObj_804D7810 (msl_core_gobj_context()->funcs)
#define HSD_GObj_804D7814 (msl_core_gobj_context()->current_render)
#define HSD_GObj_804D7818 (msl_core_gobj_context()->current_render_max)
#define HSD_GObj_804D781C (msl_core_gobj_context()->current_proc_gobj)
#define HSD_GObj_804D7820 (msl_core_gobj_context()->gx_link_tail)
#define HSD_GObjGXLinkHead (msl_core_gobj_context()->gx_link_head)
#define plinklow_gobjs (msl_core_gobj_context()->p_link_tail)
#define HSD_GObj_Entities (msl_core_gobj_context()->entities)
#define HSD_GObj_804D7830 (msl_core_gobj_context()->next_proc)
#define HSD_GObj_804D7834 (msl_core_gobj_context()->current_proc_priority)
#define HSD_GObj_804D7838 (msl_core_gobj_context()->current_proc)
#define HSD_GObj_804D783C (msl_core_gobj_context()->proc_epoch)
#define HSD_GObj_804D7840 (msl_core_gobj_context()->proc_heads)
#define HSD_GObj_804D7844 (msl_core_gobj_context()->proc_tails)
#define HSD_GObj_804D7848 (msl_core_gobj_context()->obj_none)
#define HSD_GObj_804D7849 (msl_core_gobj_context()->jobj_kind)
#define HSD_GObj_804D784A (msl_core_gobj_context()->lobj_kind)
#define HSD_GObj_804D784B (msl_core_gobj_context()->cobj_kind)
#endif

void HSD_GObj_80390C5C(HSD_GObj* gobj);
void HSD_GObj_80390C84(HSD_GObj* gobj);
void HSD_GObj_80390CAC(HSD_GObj* gobj);
u32 HSD_GObj_80390EB8(s32 i);
void HSD_GObj_803910D8(HSD_GObj*, int);
u8 HSD_GObj_803912A8(HSD_GObjLibInitDataType*, GObjFuncs*);
HSD_GObj* GObj_Create(u16 classifier, u8 p_link, u8 priority);
void HSD_GObj_JObjCallback(HSD_GObj* gobj, int arg1);
void HSD_GObj_80390CD4(HSD_GObj* gobj);
void HSD_GObj_80390CFC(void);
#ifdef MSL_CORE_HOSTED
void msl_hsd_gobj_run_procs_begin(void);
void msl_hsd_gobj_run_procs_priority_begin(s32 priority);
HSD_GObjEvent msl_hsd_gobj_run_procs_next_owner(void);
void msl_hsd_gobj_run_procs_invoke(void);
#endif
void render_gobj(HSD_GObj* cur, int i);
void HSD_GObj_80390FC0(void);
void HSD_GObj_LObjCallback(HSD_GObj* gobj, int unused);
void HSD_GObj_FogCallback(HSD_GObj* gobj, int unused);
void HSD_GObj_80391120(HSD_Obj* obj);
void HSD_GObj_803911C0(HSD_Obj* obj);
void HSD_GObj_80391260(HSD_GObjLibInitDataType*);
void HSD_GObj_803912E0(HSD_GObjLibInitDataType* arg0);
void HSD_GObj_80390ED0(HSD_GObj* gobj, u32 mask);
void HSD_GObj_80391304(HSD_GObjLibInitDataType*);

#ifdef MSL_CORE_NATIVE
// These source accessors are single field reads. Preserve one evaluation of
// the object while removing their call layer from native gameplay consumers.
#define HSD_GObjGetUserData(gobj) \
    ((void*) ((HSD_GObj*) (gobj))->user_data)
#define HSD_GObjGetHSDObj(gobj) \
    ((void*) ((HSD_GObj*) (gobj))->hsd_obj)
#else
static inline void* HSD_GObjGetUserData(HSD_GObj* gobj)
{
    return gobj->user_data;
}

static inline void* HSD_GObjGetHSDObj(HSD_GObj* gobj)
{
    return gobj->hsd_obj;
}
#endif

static inline u16 HSD_GObjGetClassifier(HSD_GObj* gobj)
{
    return gobj->classifier;
}

static inline HSD_GObj* HSD_GObjGetNext(HSD_GObj* gobj)
{
    return gobj->next;
}

#define GET_COBJ(gobj) ((HSD_CObj*) HSD_GObjGetHSDObj(gobj))
#define GET_FOG(gobj) ((HSD_Fog*) HSD_GObjGetHSDObj(gobj))
#define GET_JOBJ(gobj) ((HSD_JObj*) HSD_GObjGetHSDObj(gobj))
#define GET_LOBJ(gobj) ((HSD_LObj*) HSD_GObjGetHSDObj(gobj))

#endif
