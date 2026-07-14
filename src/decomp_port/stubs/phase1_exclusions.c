#include <stddef.h>

#include <baselib/pobj.h>
#include "ft/types.h"
#include "ftCommon/forward.h"

// Source IASA callbacks call these predicates in priority order. The bounded
// Phase 1 input contract excludes attacks, defense, grabs, items, specials,
// taunts, and crouch, so their projected predicates are always false.
#define EXCLUDED_CHECK(name) int name() { return 0; }

EXCLUDED_CHECK(ftCo_SpecialS_CheckInput)
EXCLUDED_CHECK(ftCo_SpecialS_HasInput)
EXCLUDED_CHECK(ftCo_SpecialAir_CheckInput)
EXCLUDED_CHECK(ftCo_Attack100_CheckInput)
EXCLUDED_CHECK(ftCo_Attack1_CheckInput)
EXCLUDED_CHECK(ftCo_AttackAir_CheckItemThrowInput)
EXCLUDED_CHECK(ftCo_AttackDash_CheckInput)
EXCLUDED_CHECK(ftCo_AttackHi3_CheckInput)
EXCLUDED_CHECK(ftCo_AttackHi4_CheckInput)
EXCLUDED_CHECK(ftCo_AttackHi4_CheckInputNoD0)
EXCLUDED_CHECK(ftCo_AttackLw3_CheckInput)
EXCLUDED_CHECK(ftCo_AttackLw4_CheckInput)
EXCLUDED_CHECK(ftCo_AttackS3_CheckInput)
EXCLUDED_CHECK(ftCo_AttackS4_8008C114)
EXCLUDED_CHECK(ftCo_AttackS4_CheckInput)
EXCLUDED_CHECK(ftCo_Catch_CheckInput)
EXCLUDED_CHECK(ftCo_80091A4C)
EXCLUDED_CHECK(ftCo_80091AD8)
EXCLUDED_CHECK(ftCo_80099264)
EXCLUDED_CHECK(ftCo_80099794)
EXCLUDED_CHECK(ftCo_800D5FB0)
EXCLUDED_CHECK(ftCo_800D6824)
EXCLUDED_CHECK(ftCo_800D68C0)
EXCLUDED_CHECK(ftCo_800D8A38)
EXCLUDED_CHECK(ftCo_800DE9D8)
EXCLUDED_CHECK(ftCo_800C5240)
EXCLUDED_CHECK(ftCo_800C5A50)
EXCLUDED_CHECK(ftFx_AppealS_CheckInput)
EXCLUDED_CHECK(ftCo_800A2040)
EXCLUDED_CHECK(ftCo_800D3158)
EXCLUDED_CHECK(ftCo_80095328)
EXCLUDED_CHECK(ftCo_80099A58)
EXCLUDED_CHECK(ftCo_800C3B10)
EXCLUDED_CHECK(ftCo_800CEE70)
EXCLUDED_CHECK(ftCo_800D67C4)
EXCLUDED_CHECK(ftCo_800D688C)
EXCLUDED_CHECK(ftCo_800D6928)
EXCLUDED_CHECK(ftCo_800D705C)
EXCLUDED_CHECK(ftCo_800D7100)
EXCLUDED_CHECK(ftCo_800D730C)
EXCLUDED_CHECK(ftPe_8011BA54)
EXCLUDED_CHECK(ftPe_8011BAD8)
EXCLUDED_CHECK(un_80322258)
EXCLUDED_CHECK(un_803224DC)
EXCLUDED_CHECK(ftCo_SquatWait_CheckInput)
EXCLUDED_CHECK(ft_800D2D0C)
EXCLUDED_CHECK(gm_80167470)
EXCLUDED_CHECK(ifMagnify_802FC998)
EXCLUDED_CHECK(plAttack_80037B08)
EXCLUDED_CHECK(plStale_IncrementAttackInstance)

float Camera_80031144(void) { return 0.0F; }
int lbAudioAx_80023870() { return 0; }

// Crowd recovery-call bookkeeping and audio only; callers do not consume the
// result (refs/melee/src/melee/sfx/crowdsfx.c::un_80322598).
int un_80322598(int spawn_id, float pos_y)
{
    (void) spawn_id;
    (void) pos_y;
    return 0;
}

void ftCo_AttackDash_SetMv0(void) {}

// Presentation/audio/effect owners are side-effect-free in the headless
// runtime. Gameplay script timing remains in ftaction/lbcommand.
#define PRESENTATION_NOOP(name) void name() {}

PRESENTATION_NOOP(efAsync_QueueFlush)
PRESENTATION_NOOP(efAsync_Spawn)
PRESENTATION_NOOP(efLib_DestroyAll)
PRESENTATION_NOOP(ft_PlaySFX)
PRESENTATION_NOOP(HSD_PadRumbleRemoveId)
PRESENTATION_NOOP(lbAudioAx_800263E8)
PRESENTATION_NOOP(lbAudioAx_800264E4)
PRESENTATION_NOOP(lbBgFlash_80021C48)

int HSD_PadRumbleAdd(u8 no, int id, int frame, int pri, void* listp)
{
    (void) no;
    (void) id;
    (void) frame;
    (void) pri;
    (void) listp;
    return 0;
}

// Audio-only SFX remapping.  The returned value is consumed only by the
// excluded AX calls; retaining the script callback itself preserves command
// timing (refs/melee/src/melee/ft/ft_0877.c:306 and ftaction.c:883).
s32 ft_80087D0C(Fighter* fighter, s32 sfx_id)
{
    (void) fighter;
    return sfx_id;
}

// Exact Hammer-state predicate used while resetting color animation.  Keeping
// it source-shaped avoids classifying ordinary fighter initialization as a
// hammer dependency (refs/melee/src/melee/ft/chara/ftCommon/ftCo_HammerWait.c).
bool ftCo_800C53E4(Fighter* fp)
{
    return fp->motion_id >= ftCo_MS_HammerWait &&
           fp->motion_id <= ftCo_MS_HammerLanding;
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

// Article registration is the only reached part of Fox's source OnLoad that
// belongs to the explicitly excluded item/article subsystem.
PRESENTATION_NOOP(it_8026B3F8)
PRESENTATION_NOOP(ftCo_8009F834)
PRESENTATION_NOOP(ftCo_800C0A98)
PRESENTATION_NOOP(ftCo_800D71D8)
PRESENTATION_NOOP(ftCo_800DB500)
PRESENTATION_NOOP(ftKb_SpecialN_800F1D24)

// Per-player match statistics are not observable gameplay state in Phase 1.
PRESENTATION_NOOP(pl_80037C60)
PRESENTATION_NOOP(pl_80037ECC)
PRESENTATION_NOOP(pl_8003EC9C)
PRESENTATION_NOOP(pl_8003FC44)
PRESENTATION_NOOP(pl_8003FE1C)
PRESENTATION_NOOP(pl_8003FFDC)
PRESENTATION_NOOP(pl_80040048)
PRESENTATION_NOOP(pl_800402D0)
PRESENTATION_NOOP(pl_80040330)
PRESENTATION_NOOP(pl_80040460)
PRESENTATION_NOOP(pl_80040B8C)
PRESENTATION_NOOP(pl_800411C4)
PRESENTATION_NOOP(pl_80041280)
