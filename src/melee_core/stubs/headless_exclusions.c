#include <stddef.h>
#include <stdlib.h>

#include <baselib/random.h>
#include <baselib/pobj.h>
#include "ft/types.h"
#include "ftCommon/forward.h"

// These owners belong to unsupported fighter/UI/audio domains. All common and
// Fox gameplay predicates are linked from their copied source units.
#define EXCLUDED_CHECK(name) int name() { return 0; }

EXCLUDED_CHECK(ftPe_8011BA54)
EXCLUDED_CHECK(ftPe_8011BAD8)
EXCLUDED_CHECK(un_80322258)
EXCLUDED_CHECK(un_803224DC)
EXCLUDED_CHECK(ftCo_800A2040)

void gm_80167470(void) {}

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

int lbAudioAx_80023870() { return 0; }

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
// runtime. Gameplay script timing remains in ftaction/lbcommand.
#define PRESENTATION_NOOP(name) void name() {}

PRESENTATION_NOOP(efLib_DestroyAll)
PRESENTATION_NOOP(efLib_PauseAll)
PRESENTATION_NOOP(efLib_ResumeAll)
PRESENTATION_NOOP(efLib_SetParamAlpha)
PRESENTATION_NOOP(HSD_PadRumbleRemoveId)
PRESENTATION_NOOP(lbAudioAx_80024304)
PRESENTATION_NOOP(grDisplay_801C5DB0)
PRESENTATION_NOOP(lbBgFlash_80021C48)
PRESENTATION_NOOP(lbBgFlash_80020E38)
PRESENTATION_NOOP(lbBgFlash_80021410)
PRESENTATION_NOOP(lbRefract_80022BB8)
PRESENTATION_NOOP(psInitDataBank)
PRESENTATION_NOOP(psInitDataBankLoad)

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

#define AUDIO_ZERO(name) int name() { return 0; }
AUDIO_ZERO(lbAudioAx_800230C8)
AUDIO_ZERO(lbAudioAx_80023130)
AUDIO_ZERO(lbAudioAx_80023220)
AUDIO_ZERO(lbAudioAx_800233EC)
AUDIO_ZERO(lbAudioAx_800237A8)
AUDIO_ZERO(lbAudioAx_80024184)
AUDIO_ZERO(lbAudioAx_80024B94)
AUDIO_ZERO(lbAudioAx_80024DC4)
AUDIO_ZERO(lbAudioAx_80024FDC)
AUDIO_ZERO(lbAudioAx_80024FF4)
AUDIO_ZERO(lbAudioAx_8002500C)
AUDIO_ZERO(lbAudioAx_80025038)
AUDIO_ZERO(lbAudioAx_80026510)
AUDIO_ZERO(lbAudioAx_800265C4)
AUDIO_ZERO(lbAudioAx_800236B8)

int ifMagnify_802FB6E8(int slot)
{
    (void) slot;
    return 0;
}
PRESENTATION_NOOP(ifStatus_802F69C0)
PRESENTATION_NOOP(ifStatus_802F6AF8)
PRESENTATION_NOOP(ifStatus_802F6C04)
PRESENTATION_NOOP(ifStatus_802F6D10)
PRESENTATION_NOOP(db_80225D64)
PRESENTATION_NOOP(db_80225DD8)
int db_ShowItemPickupRange(void) { return 0; }

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
// for refraction rendering (refs/melee/src/melee/lb/lbrefract.c:493).  Keep the
// model construction and exclude only that renderer-owned rewrite.
s32 lbRefract_PObjLoad(HSD_PObj* pobj, HSD_PObjDesc* desc)
{
    return hsdPObj.load(pobj, desc);
}

// Fighter_procMap calls this after every collision callback, but the source
// body is entirely guarded by FTKIND_KIRBY. It is exactly empty for Fox.
// refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_SpecialN_800F1D24
void ftKb_SpecialN_800F1D24(Fighter_GObj* gobj) { (void) gobj; }
