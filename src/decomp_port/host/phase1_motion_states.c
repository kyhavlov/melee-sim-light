#include "ft/types.h"

#include "ft/ft_0C31.h"
#include "ft/chara/ftCommon/forward.h"
#include "ft/chara/ftCommon/ftCo_Dash.h"
#include "ft/chara/ftCommon/ftCo_Fall.h"
#include "ft/chara/ftCommon/ftCo_FallAerial.h"
#include "ft/chara/ftCommon/ftCo_Jump.h"
#include "ft/chara/ftCommon/ftCo_JumpAerial.h"
#include "ft/chara/ftCommon/ftCo_KneeBend.h"
#include "ft/chara/ftCommon/ftCo_Landing.h"
#include "ft/chara/ftCommon/ftCo_Ottotto.h"
#include "ft/chara/ftCommon/ftCo_Run.h"
#include "ft/chara/ftCommon/ftCo_RunBrake.h"
#include "ft/chara/ftCommon/ftCo_RunDirect.h"
#include "ft/chara/ftCommon/ftCo_Turn.h"
#include "ft/chara/ftCommon/ftCo_TurnRun.h"
#include "ft/chara/ftCommon/ftCo_Wait.h"
#include "ft/chara/ftCommon/ftCo_Walk.h"
#include "ft/ftswing.h"

#define MOVE_STATE(anim, flags, move, anim_cb_, input_cb_, phys_cb_, coll_cb_) \
    {                                                                         \
        (anim), (flags), (move), (anim_cb_), (input_cb_), (phys_cb_),          \
            (coll_cb_), NULL                                                   \
    }

// Source entries 14..34 and 42..43 from ftmotionstates.c. Camera callbacks
// are deliberately NULL in the headless runtime; every gameplay callback and
// its original state/flag tuple remains source-owned.
MotionState ftData_MotionStateList[ftCo_MS_Count] = {
    [ftCo_MS_Wait] = MOVE_STATE(
        ftCo_SM_Wait1_0, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Wait_Anim,
        ftCo_Wait_IASA, ftCo_Wait_Phys, ftCo_Wait_Coll),
    [ftCo_MS_WalkSlow] = MOVE_STATE(
        ftCo_SM_WalkSlow, ftCo_MF_Walk,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Walk_Anim,
        ftCo_Walk_IASA, ftCo_Walk_Phys, ftCo_Walk_Coll),
    [ftCo_MS_WalkMiddle] = MOVE_STATE(
        ftCo_SM_WalkMiddle, ftCo_MF_Walk,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Walk_Anim,
        ftCo_Walk_IASA, ftCo_Walk_Phys, ftCo_Walk_Coll),
    [ftCo_MS_WalkFast] = MOVE_STATE(
        ftCo_SM_WalkFast, ftCo_MF_Walk,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Walk_Anim,
        ftCo_Walk_IASA, ftCo_Walk_Phys, ftCo_Walk_Coll),
    [ftCo_MS_Turn] = MOVE_STATE(
        ftCo_SM_Turn, ftCo_MF_Turn,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Turn_Anim,
        ftCo_Turn_IASA, ftCo_Turn_Phys, ftCo_Turn_Coll),
    [ftCo_MS_TurnRun] = MOVE_STATE(
        ftCo_SM_TurnRun, ftCo_MF_Turn,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_TurnRun_Anim,
        ftCo_TurnRun_IASA, ftCo_TurnRun_Phys, ftCo_TurnRun_Coll),
    [ftCo_MS_Dash] = MOVE_STATE(
        ftCo_SM_Dash, ftCo_MF_Dash,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_Dash_Anim,
        ftCo_Dash_IASA, ftCo_Dash_Phys, ftCo_Dash_Coll),
    [ftCo_MS_Run] = MOVE_STATE(
        ftCo_SM_Run, ftCo_MF_Run,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Run_Anim, ftCo_Run_IASA,
        ftCo_Run_Phys, ftCo_Run_Coll),
    [ftCo_MS_RunDirect] = MOVE_STATE(
        ftCo_SM_Run, ftCo_MF_Run,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_RunDirect_Anim,
        ftCo_RunDirect_IASA, ftCo_RunDirect_Phys, ftCo_RunDirect_Coll),
    [ftCo_MS_RunBrake] = MOVE_STATE(
        ftCo_SM_RunBrake, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_RunBrake_Anim,
        ftCo_RunBrake_IASA, ftCo_RunBrake_Phys, ftCo_RunBrake_Coll),
    [ftCo_MS_KneeBend] = MOVE_STATE(
        ftCo_SM_Kneebend, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 22) | (1 << 23), ftCo_KneeBend_Anim,
        ftCo_KneeBend_IASA, ftCo_KneeBend_Phys, ftCo_KneeBend_Coll),
    [ftCo_MS_JumpF] = MOVE_STATE(
        ftCo_SM_JumpF, ftCo_MF_Jump,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Jump_Anim,
        ftCo_Jump_IASA, ftCo_Jump_Phys, ftCo_Jump_Coll),
    [ftCo_MS_JumpB] = MOVE_STATE(
        ftCo_SM_JumpB, ftCo_MF_Jump,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Jump_Anim,
        ftCo_Jump_IASA, ftCo_Jump_Phys, ftCo_Jump_Coll),
    [ftCo_MS_JumpAerialF] = MOVE_STATE(
        ftCo_SM_JumpAerialF, ftCo_MF_JumpAir,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_JumpAerial_Anim,
        ftCo_JumpAerial_IASA, ftCo_JumpAerial_Phys, ftCo_JumpAerial_Coll),
    [ftCo_MS_JumpAerialB] = MOVE_STATE(
        ftCo_SM_JumpAerialB, ftCo_MF_JumpAir,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_JumpAerial_Anim,
        ftCo_JumpAerial_IASA, ftCo_JumpAerial_Phys, ftCo_JumpAerial_Coll),
    [ftCo_MS_Fall] = MOVE_STATE(
        ftCo_SM_Fall, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Fall_Anim,
        ftCo_Fall_IASA, ftCo_Fall_Phys, ftCo_Fall_Coll),
    [ftCo_MS_FallF] = MOVE_STATE(
        ftCo_SM_None, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Fall_Anim,
        ftCo_Fall_IASA, ftCo_Fall_Phys, ftCo_Fall_Coll),
    [ftCo_MS_FallB] = MOVE_STATE(
        ftCo_SM_None, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Fall_Anim,
        ftCo_Fall_IASA, ftCo_Fall_Phys, ftCo_Fall_Coll),
    [ftCo_MS_FallAerial] = MOVE_STATE(
        ftCo_SM_FallAerial, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_FallAerial_Anim,
        ftCo_FallAerial_IASA, ftCo_FallAerial_Phys, ftCo_FallAerial_Coll),
    [ftCo_MS_FallAerialF] = MOVE_STATE(
        ftCo_SM_FallAerialF, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Fall_Anim,
        ftCo_FallAerial_IASA, ftCo_Fall_Phys, ftCo_Fall_Coll),
    [ftCo_MS_FallAerialB] = MOVE_STATE(
        ftCo_SM_FallAerialB, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 23), ftCo_Fall_Anim,
        ftCo_FallAerial_IASA, ftCo_Fall_Phys, ftCo_Fall_Coll),
    [ftCo_MS_Landing] = MOVE_STATE(
        ftCo_SM_Landing, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 22), ftCo_Landing_Anim,
        ftCo_Landing_IASA, ftCo_Landing_Phys, ftCo_Landing_Coll),
    [ftCo_MS_LandingFallSpecial] = MOVE_STATE(
        ftCo_SM_LandingFallSpecial, Ft_MF_None,
        (FtMoveId_Default << 24) | (1 << 22), ftCo_Landing_Anim,
        ftCo_Landing_IASA, ftCo_Landing_Phys, ftCo_Landing_Coll),
    [ftCo_MS_Ottotto] = MOVE_STATE(
        ftCo_SM_Ottotto, Ft_MF_SkipMetalB, FtMoveId_Default << 24,
        ftCo_Ottotto_Anim, ftCo_Ottotto_IASA, ftCo_Ottotto_Phys,
        ftCo_Ottotto_Coll),
    [ftCo_MS_OttottoWait] = MOVE_STATE(
        ftCo_SM_OttottoWait, ftCo_MF_OttottoWait, FtMoveId_Default << 24,
        ftCo_OttottoWait_Anim, ftCo_OttottoWait_IASA,
        ftCo_OttottoWait_Phys, ftCo_OttottoWait_Coll),
    [ftCo_MS_Entry] = MOVE_STATE(
        ftCo_SM_None, ftCo_MF_Rebirth, FtMoveId_Default << 24,
        ftCo_Entry_Anim, ftCo_Entry_IASA, ftCo_Entry_Phys,
        ftCo_Entry_Coll),
    [ftCo_MS_EntryStart] = MOVE_STATE(
        ftCo_SM_EntryStart, ftCo_MF_Rebirth, FtMoveId_Default << 24,
        ftCo_EntryStart_Anim, ftCo_EntryStart_IASA, ftCo_EntryStart_Phys,
        ftCo_EntryStart_Coll),
    [ftCo_MS_EntryEnd] = MOVE_STATE(
        ftCo_SM_None, ftCo_MF_Rebirth, FtMoveId_Default << 24,
        ftCo_EntryEnd_Anim, ftCo_EntryEnd_IASA, ftCo_EntryEnd_Phys,
        ftCo_EntryEnd_Coll),
};
