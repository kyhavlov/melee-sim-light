/* What the decomp's lbaudio_ax.c (1f0499b) names that the simulator's older headers (pin 91b9789) name otherwise, and the sound core's
 * stand-ins for the HSD objects it makes (src/sound/core.c: sound objects live in the sound core's own pool, never in the game's GObj
 * lists: see src/sound/README.md). Included after snd_rename.h, before the decomp file. */
#ifndef MSL_SOUND_LBAUDIO_PRELUDE_H
#define MSL_SOUND_LBAUDIO_PRELUDE_H
#include <melee/ft/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/objalloc.h>
typedef int StKind;
typedef int GrKind;
#define ChKind_Max CHKIND_MAX
#define ABS(x) ((x) < 0 ? -(x) : (x))
enum { LANG_JP, LANG_US };
/* the sound objects' class and p_link (decomp melee/lb/forward.h); the p_link list is the sound core's pool */
#define HSD_GOBJ_CLASS_SOUND 23
#define HSD_GOBJ_PLINK_AUDIO_AX 0
#define HSD_GObjPLinkHead msl_snd_plink_head
extern HSD_GObj* msl_snd_plink_head[1];
HSD_GObj* msl_snd_GObj_Create(u16 classifier, u8 p_link, u8 priority);
void msl_snd_GObj_InitUserData(HSD_GObj* gobj, u8 kind, void (*remove_func)(void*), void* data);
void msl_snd_GObjFree(HSD_GObj* gobj);
HSD_GObjProc* msl_snd_GObj_SetupProc(HSD_GObj* gobj, HSD_GObjEvent func, u8 pri);
void* msl_snd_ObjAlloc(HSD_ObjAllocData* data);
void msl_snd_ObjFree(HSD_ObjAllocData* data, void* obj);
s32 msl_snd_Randi(s32 max_val);
/* the disc is the US one (the simulator reads the English files): the game's /audio/us/ banks and macro table */
#define lbLang_IsSavedLanguageUS msl_snd_lang_us
/* never called in a VS match (core.c) */
#define gm_80169370 msl_snd_gm_80169370
u8 msl_snd_gm_80169370(s32 slot);
#endif
