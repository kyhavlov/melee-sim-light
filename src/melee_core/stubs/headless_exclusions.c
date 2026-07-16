#include "db/db.h"
#include "ft/types.h"

#include "ftCommon/forward.h"

#include "lb/lbaudio_ax.h"

#include <stddef.h>
#include <stdlib.h>
#include <baselib/pobj.h>
#include <baselib/random.h>

// These owners belong to unsupported fighter/UI/audio domains. All common and
// Fox gameplay predicates are linked from their copied source units.
bool un_80322258(float position)
{
    (void) position;
    return false;
}

bool un_803224DC(s32 spawn_id, f32 pos_x, f32 kb_mag)
{
    (void) spawn_id;
    (void) pos_x;
    (void) kb_mag;
    return false;
}

bool ftCo_800A2040(Fighter* fp)
{
    (void) fp;
    return false;
}

void gm_80167470(long arg0, long arg1)
{
    (void) arg0;
    (void) arg1;
}

// Versus result/bonus bookkeeping does not feed gameplay state. Shy Guy's
// source destruction callback records this statistic after committing its
// item state transition.
// refs/melee/src/melee/{it/itzako.c::it_8027CE18,
// pl/plbonuslib.c::pl_8004049C,gm/gm_16AE.c::gm_8016B6E8}
void gm_8016B6E8(int player, int value)
{
    (void) player;
    (void) value;
}

// The supported rules disable random items, leaving the source ambient-item
// mask at zero. Heiho therefore cannot spawn its optional food decoration.
// refs/melee/src/melee/it/itspawn.c::it_8026D324
// refs/melee/src/melee/it/items/itfoods.c::it_8028FAF4
bool it_8026D324(int kind)
{
    (void) kind;
    return false;
}

int lbAudioAx_80023870(int sfx_id, int sfx_vol, int sfx_pan, int arg3)
{
    (void) sfx_id;
    (void) sfx_vol;
    (void) sfx_pan;
    (void) arg3;
    return 0;
}

// Pokemon Stadium asks the scene DVD owner for an optional preloaded 0x50000
// transformation scratch archive, then allocates the same-size scratch block
// when it is absent. The scalar bootstrap has no asynchronous scene preloader,
// so select that exact source fallback during initialization.
// refs/melee/src/melee/gr/grpstadium.c::grStadium_801D13E0
void* lbDvd_GetPreloadedArchive(int entry)
{
    (void) entry;
    return NULL;
}

// Crowd recovery-call bookkeeping and audio only; callers do not consume the
// result (refs/melee/src/melee/sfx/crowdsfx.c::un_80322598).
int un_80322598(int spawn_id, float pos_y)
{
    (void) spawn_id;
    (void) pos_y;
    return 0;
}

// Presentation/audio/effect owners are side-effect-free in the headless
// runtime. Gameplay script timing remains in ftaction/lbcommand. Preserve the
// real prototypes because Wasm validates every direct-call signature.
void efLib_DestroyAll(HSD_GObj* gobj)
{
    (void) gobj;
}
void efLib_PauseAll(HSD_GObj* gobj)
{
    (void) gobj;
}
void efLib_ResumeAll(HSD_GObj* gobj)
{
    (void) gobj;
}
void efLib_SetParamAlpha(HSD_GObj* gobj, u8 alpha)
{
    (void) gobj;
    (void) alpha;
}
void HSD_PadRumbleRemoveId(u8 port, int id)
{
    (void) port;
    (void) id;
}
void grDisplay_801C5DB0(HSD_GObj* gobj, int render_pass)
{
    (void) gobj;
    (void) render_pass;
}
void lbBgFlash_80021C48(u32 type, u32 duration)
{
    (void) type;
    (void) duration;
}
void lbRefract_80022BB8(void) {}
void psInitDataBank(int bank, int* commands, int* textures, u32* references,
                    int* forms)
{
    (void) bank;
    (void) commands;
    (void) textures;
    (void) references;
    (void) forms;
}
void psInitDataBankLoad(int bank, int* commands, int* textures,
                        u32* references, int* forms)
{
    (void) bank;
    (void) commands;
    (void) textures;
    (void) references;
    (void) forms;
}
// Transform-state copying only asks the crowd owner to arm a gasp. Its two
// source writes are audio scheduling and are not read by gameplay.
// refs/melee/src/melee/{ft/ftcommon.c::ftCommon_8007EFC8,
// sfx/crowdsfx.c::un_80322314}
void un_80322314(void) {}

HSD_GObj* lbAudioAx_800263E8(float direction, HSD_GObj* entity,
                             int behavior, int sfx_id, int start_value,
                             int end_value, int pan_left, int pan_right,
                             int end_frame, int channel, int arg10)
{
    (void) entity;
    (void) behavior;
    (void) sfx_id;
    (void) start_value;
    (void) end_value;
    (void) pan_left;
    (void) pan_right;
    (void) end_frame;
    (void) channel;
    (void) arg10;

    // The AX/GObj result is presentation-only, but fn_80025FAC chooses a
    // random left/right direction for every source request whose explicit
    // direction is zero. This immediate draw shares the HSD stream with later
    // gameplay consumers such as DamageFlyRoll, so retain it headlessly.
    // refs/melee/src/melee/{ft/ftaction.c::ftAction_80072320,
    // lb/lbaudio_ax.c::{lbAudioAx_800263E8,fn_80025FAC}}
    if (direction == 0.0F) {
        (void) HSD_Randi(2);
    }
    return NULL;
}

bool lbAudioAx_800264E4(void* data)
{
    (void) data;
    // Source returns -1 for the absent audio owner. Fighter fields receiving
    // the handle remain in their canonical inactive state.
    // refs/melee/src/melee/lb/lbaudio_ax.c::lbAudioAx_800264E4
    return -1;
}

int lbAudioAx_800236B8(int arg0)
{
    return (void) arg0, 0;
}
int lbAudioAx_800237A8(enum_t id, int volume, int pan)
{
    (void) id;
    (void) volume;
    (void) pan;
    return 0;
}
int lbAudioAx_80024184(int arg0, int arg1, int arg2, int arg3)
{
    (void) arg0;
    (void) arg1;
    (void) arg2;
    (void) arg3;
    return 0;
}
int lbAudioAx_80024304(int arg0)
{
    return (void) arg0, 0;
}
int lbAudioAx_80024B94(int arg0, int arg1)
{
    (void) arg0;
    (void) arg1;
    return 0;
}
void lbAudioAx_80024DC4(int arg0)
{
    (void) arg0;
}
void lbAudioAx_80024FDC(void) {}
void lbAudioAx_80024FF4(void) {}
void lbAudioAx_8002500C(int arg0)
{
    (void) arg0;
}
void lbAudioAx_80025038(int arg0)
{
    (void) arg0;
}
bool lbAudioAx_80026510(HSD_GObj* gobj)
{
    return (void) gobj, false;
}
bool lbAudioAx_800265C4(HSD_GObj* gobj, int sfx)
{
    (void) gobj;
    (void) sfx;
    return false;
}

int ifMagnify_802FB6E8(int slot)
{
    (void) slot;
    return 0;
}
void ifStatus_802F69C0(s32 recipient, s32 donor)
{
    (void) recipient;
    (void) donor;
}
void ifStatus_802F6AF8(s32 slot)
{
    (void) slot;
}
void ifStatus_802F6C04(s32 slot)
{
    (void) slot;
}
void ifStatus_802F6D10(s32 slot)
{
    (void) slot;
}
// The native ABI tolerates the old unspecified-argument stubs, but Wasm
// enforces indirect-call signatures. Keep these reached debug-only item
// owners precisely typed; their source effects only toggle debug overlays.
// refs/melee/src/melee/db/dbitem.c::{db_80225D64,db_80225DD8}
void db_80225D64(Item_GObj* item, Fighter_GObj* owner)
{
    (void) item;
    (void) owner;
}
void db_80225DD8(Item_GObj* item, Fighter_GObj* owner)
{
    (void) item;
    (void) owner;
}
u32 db_ShowItemPickupRange(void)
{
    return 0;
}

int HSD_PadRumbleAdd(u8 no, int id, int frame, int pri, void* listp)
{
    (void) no;
    (void) id;
    (void) frame;
    (void) pri;
    (void) listp;
    return 0;
}

void ftCo_800B3900(Fighter_GObj* gobj)
{
    (void) gobj;
    abort();
}

// Fighter PObjs use the refraction subclass's loader even for ordinary model
// geometry (refs/melee/src/melee/ft/ftparts.c:327).  The source loader first
// performs the base PObj load, then rewrites GX display-list vertex data only
// for refraction rendering (refs/melee/src/melee/lb/lbrefract.c:493).  Keep
// the model construction and exclude only that renderer-owned rewrite.
s32 lbRefract_PObjLoad(HSD_PObj* pobj, HSD_PObjDesc* desc)
{
    return hsdPObj.load(pobj, desc);
}

// Fighter_procMap calls this after every collision callback, but the source
// body is entirely guarded by FTKIND_KIRBY. It is exactly empty for Fox.
// refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_SpecialN_800F1D24
void ftKb_SpecialN_800F1D24(Fighter_GObj* gobj) { (void) gobj; }
