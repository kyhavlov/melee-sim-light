#ifndef GALE01_0C0658
#define GALE01_0C0658

#include <placeholder.h>
#include <platform.h>

#include "ft/types.h"

#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>

#ifdef MSL_CORE_HOSTED
// These registries contain live Ground GObj pointers and callbacks installed
// by the selected stage. They are match state, not a process-wide service.
// refs/melee/src/melee/ft/ftdevice.c::{ftCo_800C06C0,ftCo_800C06E8,
//   ftCo_800C0764,ftCo_800C07F8}
typedef struct MslFtDeviceState {
    struct ftDeviceUnk3 entries[4];
    struct ftDeviceUnk5 bury_entries[2];
    struct ftDeviceUnk3 collision_entry;
    struct ftDeviceUnk4 counts;
    int bury_count;
    int collision_count;
} MslFtDeviceState;

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_ft_device_state() (msl_core_context_ft_device)
#else
MslFtDeviceState* msl_core_ft_device_state(void);
#endif
#define ft_80459A68 (msl_core_ft_device_state()->entries)
#define ftDevice_BuryThings (msl_core_ft_device_state()->bury_entries)
#define ft_80459A8C (msl_core_ft_device_state()->collision_entry)
#define ft_804D6578 (msl_core_ft_device_state()->counts)
#define ftDevice_BuryThingCount (msl_core_ft_device_state()->bury_count)
#define ft_804D6570 (msl_core_ft_device_state()->collision_count)
#else
/* 459A68 */ extern struct ftDeviceUnk3 ft_80459A68[4];
/* 459A74 */ extern struct ftDeviceUnk5 ftDevice_BuryThings[2];
/* 459A8C */ extern struct ftDeviceUnk3 ft_80459A8C;
/* 4D6570 */ extern int ft_804D6570;
/* 4D6574 */ extern int ftDevice_BuryThingCount;
/* 4D6578 */ extern struct ftDeviceUnk4 ft_804D6578;
#endif

/* 0C0658 */ ColorOverlay* ftCo_800C0658(Fighter* fp);
/* 0C0674 */ ColorOverlay* ftCo_800C0674(Fighter_GObj* gobj);
/* 0C0694 */ enum_t ftCo_800C0694(Fighter* fp);
/* 0C06B4 */ int ftCo_800C06B4(Fighter* fp);
/* 0C06C0 */ void ftCo_800C06C0(void);
/* 0C06E8 */ void ftCo_800C06E8(Ground_GObj*, int, void*);
/* 0C0764 */ void ftCo_800C0764(Ground_GObj*, u32, void*);
/* 0C07F8 */ void ftCo_800C07F8(Ground_GObj*, u32, void*);
#endif
