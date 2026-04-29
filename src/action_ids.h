#pragma once

#include <stdint.h>

// GALE01 action ids (aka `FtMotionId` / `ftCommon_MotionState`) for common locomotion.
//
// Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
typedef enum MslActionId {
  // Match flow (suite-present).
  MSL_ACT_DEAD_DOWN = 0x0000,     // ftCo_MS_DeadDown
  MSL_ACT_DEAD_LEFT = 0x0001,     // ftCo_MS_DeadLeft
  MSL_ACT_DEAD_RIGHT = 0x0002,    // ftCo_MS_DeadRight
  MSL_ACT_DEAD_UP_STAR = 0x0004,  // ftCo_MS_DeadUpStar
  MSL_ACT_REBIRTH = 0x000C,       // ftCo_MS_Rebirth
  MSL_ACT_REBIRTH_WAIT = 0x000D,  // ftCo_MS_RebirthWait
  MSL_ACT_DAMAGE_ICE = 0x0145,    // ftCo_MS_DamageIce

  MSL_ACT_WAIT = 0x000E,            // ftCo_MS_Wait
  MSL_ACT_WALK_SLOW = 0x000F,       // ftCo_MS_WalkSlow
  MSL_ACT_WALK_MIDDLE = 0x0010,     // ftCo_MS_WalkMiddle
  MSL_ACT_WALK_FAST = 0x0011,       // ftCo_MS_WalkFast
  MSL_ACT_TURN = 0x0012,            // ftCo_MS_Turn
  MSL_ACT_TURN_RUN = 0x0013,        // ftCo_MS_TurnRun
  MSL_ACT_DASH = 0x0014,            // ftCo_MS_Dash
  MSL_ACT_RUN = 0x0015,             // ftCo_MS_Run
  MSL_ACT_RUN_DIRECT = 0x0016,      // ftCo_MS_RunDirect
  MSL_ACT_RUN_BRAKE = 0x0017,       // ftCo_MS_RunBrake
  MSL_ACT_KNEE_BEND = 0x0018,       // ftCo_MS_KneeBend
  MSL_ACT_JUMP_F = 0x0019,          // ftCo_MS_JumpF
  MSL_ACT_JUMP_B = 0x001A,          // ftCo_MS_JumpB
  MSL_ACT_JUMP_AERIAL_F = 0x001B,   // ftCo_MS_JumpAerialF
  MSL_ACT_JUMP_AERIAL_B = 0x001C,   // ftCo_MS_JumpAerialB
  MSL_ACT_FALL = 0x001D,            // ftCo_MS_Fall
  MSL_ACT_FALL_F = 0x001E,          // ftCo_MS_FallF
  MSL_ACT_FALL_B = 0x001F,          // ftCo_MS_FallB
  MSL_ACT_FALL_AERIAL = 0x0020,     // ftCo_MS_FallAerial
  MSL_ACT_FALL_AERIAL_F = 0x0021,   // ftCo_MS_FallAerialF
  MSL_ACT_FALL_AERIAL_B = 0x0022,   // ftCo_MS_FallAerialB
  MSL_ACT_FALL_SPECIAL = 0x0023,    // ftCo_MS_FallSpecial
  MSL_ACT_FALL_SPECIAL_F = 0x0024,  // ftCo_MS_FallSpecialF
  MSL_ACT_FALL_SPECIAL_B = 0x0025,  // ftCo_MS_FallSpecialB
  MSL_ACT_DAMAGE_FALL = 0x0026,     // ftCo_MS_DamageFall
  // Squat states are consecutive after DamageFall in GALE01.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h:300-330 (ftCommon_MotionState enum).
  MSL_ACT_SQUAT = 0x0027,                 // ftCo_MS_Squat
  MSL_ACT_SQUAT_WAIT = 0x0028,            // ftCo_MS_SquatWait
  MSL_ACT_SQUAT_RV = 0x0029,              // ftCo_MS_SquatRv
  MSL_ACT_LANDING = 0x002A,               // ftCo_MS_Landing
  MSL_ACT_LANDING_FALL_SPECIAL = 0x002B,  // ftCo_MS_LandingFallSpecial

  // Aerial attack landing lag states (ftCo_LandingAir_*).
  MSL_ACT_LANDING_AIR_N = 0x0046,   // ftCo_MS_LandingAirN
  MSL_ACT_LANDING_AIR_F = 0x0047,   // ftCo_MS_LandingAirF
  MSL_ACT_LANDING_AIR_B = 0x0048,   // ftCo_MS_LandingAirB
  MSL_ACT_LANDING_AIR_HI = 0x0049,  // ftCo_MS_LandingAirHi
  MSL_ACT_LANDING_AIR_LW = 0x004A,  // ftCo_MS_LandingAirLw

  // Damage (subset used by the Fox/Falco FD suite).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_DAMAGE_HI_1 = 0x004B,      // ftCo_MS_DamageHi1
  MSL_ACT_DAMAGE_HI_2 = 0x004C,      // ftCo_MS_DamageHi2
  MSL_ACT_DAMAGE_HI_3 = 0x004D,      // ftCo_MS_DamageHi3
  MSL_ACT_DAMAGE_N_1 = 0x004E,       // ftCo_MS_DamageN1
  MSL_ACT_DAMAGE_N_2 = 0x004F,       // ftCo_MS_DamageN2
  MSL_ACT_DAMAGE_N_3 = 0x0050,       // ftCo_MS_DamageN3
  MSL_ACT_DAMAGE_LW_1 = 0x0051,      // ftCo_MS_DamageLw1
  MSL_ACT_DAMAGE_LW_2 = 0x0052,      // ftCo_MS_DamageLw2
  MSL_ACT_DAMAGE_LW_3 = 0x0053,      // ftCo_MS_DamageLw3
  MSL_ACT_DAMAGE_AIR_1 = 0x0054,     // ftCo_MS_DamageAir1
  MSL_ACT_DAMAGE_AIR_2 = 0x0055,     // ftCo_MS_DamageAir2
  MSL_ACT_DAMAGE_AIR_3 = 0x0056,     // ftCo_MS_DamageAir3
  MSL_ACT_DAMAGE_FLY_HI = 0x0057,    // ftCo_MS_DamageFlyHi
  MSL_ACT_DAMAGE_FLY_N = 0x0058,     // ftCo_MS_DamageFlyN
  MSL_ACT_DAMAGE_FLY_LW = 0x0059,    // ftCo_MS_DamageFlyLw
  MSL_ACT_DAMAGE_FLY_TOP = 0x005A,   // ftCo_MS_DamageFlyTop
  MSL_ACT_DAMAGE_FLY_ROLL = 0x005B,  // ftCo_MS_DamageFlyRoll

  // Shield / Guard (subset).
  MSL_ACT_GUARD_ON = 0x00B2,              // ftCo_MS_GuardOn
  MSL_ACT_GUARD = 0x00B3,                 // ftCo_MS_Guard
  MSL_ACT_GUARD_OFF = 0x00B4,             // ftCo_MS_GuardOff
  MSL_ACT_GUARD_SET_OFF = 0x00B5,         // ftCo_MS_GuardSetOff
  MSL_ACT_GUARD_REFLECT = 0x00B6,         // ftCo_MS_GuardReflect
  MSL_ACT_SHIELD_BREAK_FLY = 0x00CD,      // ftCo_MS_ShieldBreakFly
  MSL_ACT_SHIELD_BREAK_FALL = 0x00CE,     // ftCo_MS_ShieldBreakFall
  MSL_ACT_SHIELD_BREAK_DOWN_U = 0x00CF,   // ftCo_MS_ShieldBreakDownU
  MSL_ACT_SHIELD_BREAK_DOWN_D = 0x00D0,   // ftCo_MS_ShieldBreakDownD
  MSL_ACT_SHIELD_BREAK_STAND_U = 0x00D1,  // ftCo_MS_ShieldBreakStandU
  MSL_ACT_SHIELD_BREAK_STAND_D = 0x00D2,  // ftCo_MS_ShieldBreakStandD
  MSL_ACT_FURAFURA = 0x00D3,              // ftCo_MS_Furafura

  // Downed / knockdown (suite-present subset).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_DOWN_BOUND_U = 0x00B7,   // ftCo_MS_DownBoundU
  MSL_ACT_DOWN_WAIT_U = 0x00B8,    // ftCo_MS_DownWaitU
  MSL_ACT_DOWN_DAMAGE_U = 0x00B9,  // ftCo_MS_DownDamageU
  MSL_ACT_DOWN_STAND_U = 0x00BA,   // ftCo_MS_DownStandU
  MSL_ACT_DOWN_ATTACK_U = 0x00BB,  // ftCo_MS_DownAttackU
  MSL_ACT_DOWN_FOWARD_U = 0x00BC,  // ftCo_MS_DownFowardU
  MSL_ACT_DOWN_BACK_U = 0x00BD,    // ftCo_MS_DownBackU
  MSL_ACT_DOWN_BOUND_D = 0x00BF,   // ftCo_MS_DownBoundD
  MSL_ACT_DOWN_WAIT_D = 0x00C0,    // ftCo_MS_DownWaitD
  MSL_ACT_DOWN_DAMAGE_D = 0x00C1,  // ftCo_MS_DownDamageD
  MSL_ACT_DOWN_STAND_D = 0x00C2,   // ftCo_MS_DownStandD
  MSL_ACT_DOWN_ATTACK_D = 0x00C3,  // ftCo_MS_DownAttackD
  MSL_ACT_DOWN_FOWARD_D = 0x00C4,  // ftCo_MS_DownFowardD
  MSL_ACT_DOWN_BACK_D = 0x00C5,    // ftCo_MS_DownBackD

  // Tech / passive (suite-present subset).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_PASSIVE = 0x00C7,            // ftCo_MS_Passive
  MSL_ACT_PASSIVE_STAND_F = 0x00C8,    // ftCo_MS_PassiveStandF
  MSL_ACT_PASSIVE_STAND_B = 0x00C9,    // ftCo_MS_PassiveStandB
  MSL_ACT_PASSIVE_WALL = 0x00CA,       // ftCo_MS_PassiveWall
  MSL_ACT_PASSIVE_WALL_JUMP = 0x00CB,  // ftCo_MS_PassiveWallJump
  MSL_ACT_PASSIVE_CEIL = 0x00CC,       // ftCo_MS_PassiveCeil

  // Shield defensive options (grounded).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_ESCAPE_F = 0x00E9,  // ftCo_MS_EscapeF (roll forward)
  MSL_ACT_ESCAPE_B = 0x00EA,  // ftCo_MS_EscapeB (roll backward)
  MSL_ACT_ESCAPE_N = 0x00EB,  // ftCo_MS_EscapeN (spotdodge)

  // Grab / throw / capture (suite-present subset).
  //
  // Source of truth:
  // - refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`
  // - refs/melee/src/melee/ft/ftmotionstates.c (comments with numeric ids)
  MSL_ACT_CATCH = 0x00D4,              // ftCo_MS_Catch (212)
  MSL_ACT_CATCH_PULL = 0x00D5,         // ftCo_MS_CatchPull (213)
  MSL_ACT_CATCH_DASH = 0x00D6,         // ftCo_MS_CatchDash (214)
  MSL_ACT_CATCH_DASH_PULL = 0x00D7,    // ftCo_MS_CatchDashPull (215)
  MSL_ACT_CATCH_WAIT = 0x00D8,         // ftCo_MS_CatchWait (216)
  MSL_ACT_CATCH_ATTACK = 0x00D9,       // ftCo_MS_CatchAttack (217)
  MSL_ACT_CATCH_CUT = 0x00DA,          // ftCo_MS_CatchCut (218)
  MSL_ACT_THROW_F = 0x00DB,            // ftCo_MS_ThrowF (219)
  MSL_ACT_THROW_B = 0x00DC,            // ftCo_MS_ThrowB (220)
  MSL_ACT_THROW_HI = 0x00DD,           // ftCo_MS_ThrowHi (221)
  MSL_ACT_THROW_LW = 0x00DE,           // ftCo_MS_ThrowLw (222)
  MSL_ACT_CAPTURE_PULLED_HI = 0x00DF,  // ftCo_MS_CapturePulledHi (223)
  MSL_ACT_CAPTURE_WAIT_HI = 0x00E0,    // ftCo_MS_CaptureWaitHi (224)
  MSL_ACT_CAPTURE_DAMAGE_HI = 0x00E1,  // ftCo_MS_CaptureDamageHi (225)
  MSL_ACT_CAPTURE_PULLED_LW = 0x00E2,  // ftCo_MS_CapturePulledLw (226)
  MSL_ACT_CAPTURE_WAIT_LW = 0x00E3,    // ftCo_MS_CaptureWaitLw (227)
  MSL_ACT_CAPTURE_DAMAGE_LW = 0x00E4,  // ftCo_MS_CaptureDamageLw (228)
  MSL_ACT_CAPTURE_CUT = 0x00E5,        // ftCo_MS_CaptureCut (229)
  MSL_ACT_CAPTURE_JUMP = 0x00E6,       // ftCo_MS_CaptureJump (230)
  MSL_ACT_CAPTURE_NECK = 0x00E7,       // ftCo_MS_CaptureNeck (231)
  MSL_ACT_CAPTURE_FOOT = 0x00E8,       // ftCo_MS_CaptureFoot (232)

  // Rebound / clank response.
  // Decomp source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  // (See also: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c)
  MSL_ACT_REBOUND_STOP = 0x00ED,  // ftCo_MS_ReboundStop (237)
  MSL_ACT_REBOUND = 0x00EE,       // ftCo_MS_Rebound (238)

  MSL_ACT_THROWN_F = 0x00EF,          // ftCo_MS_ThrownF (239)
  MSL_ACT_THROWN_B = 0x00F0,          // ftCo_MS_ThrownB (240)
  MSL_ACT_THROWN_HI = 0x00F1,         // ftCo_MS_ThrownHi (241)
  MSL_ACT_THROWN_LW = 0x00F2,         // ftCo_MS_ThrownLw (242)
  MSL_ACT_THROWN_LW_WOMEN = 0x00F3,   // ftCo_MS_ThrownlwWomen (243)
  MSL_ACT_OTTOTTO = 0x00F5,           // ftCo_MS_Ottotto (245)
  MSL_ACT_OTTOTTO_WAIT = 0x00F6,      // ftCo_MS_OttottoWait (246)
  MSL_ACT_FLY_REFLECT_WALL = 0x00F7,  // ftCo_MS_FlyReflectWall (247)
  MSL_ACT_FLY_REFLECT_CEIL = 0x00F8,  // ftCo_MS_FlyReflectCeil (248)
  MSL_ACT_STOP_WALL = 0x00F9,         // ftCo_MS_StopWall (249)
  MSL_ACT_STOP_CEIL = 0x00FA,         // ftCo_MS_StopCeil (250)

  // Cliff / ledge (FD suite-present).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_MISS_FOOT = 0x00FB,           // ftCo_MS_MissFoot (251)
  MSL_ACT_CLIFF_CATCH = 0x00FC,         // ftCo_MS_CliffCatch (252)
  MSL_ACT_CLIFF_WAIT = 0x00FD,          // ftCo_MS_CliffWait (253)
  MSL_ACT_CLIFF_CLIMB_SLOW = 0x00FE,    // ftCo_MS_CliffClimbSlow (254)
  MSL_ACT_CLIFF_CLIMB_QUICK = 0x00FF,   // ftCo_MS_CliffClimbQuick (255)
  MSL_ACT_CLIFF_ATTACK_SLOW = 0x0100,   // ftCo_MS_CliffAttackSlow (256)
  MSL_ACT_CLIFF_ATTACK_QUICK = 0x0101,  // ftCo_MS_CliffAttackQuick (257)
  MSL_ACT_CLIFF_ESCAPE_SLOW = 0x0102,   // ftCo_MS_CliffEscapeSlow (258)
  MSL_ACT_CLIFF_ESCAPE_QUICK = 0x0103,  // ftCo_MS_CliffEscapeQuick (259)
  MSL_ACT_CLIFF_JUMP_SLOW1 = 0x0104,    // ftCo_MS_CliffJumpSlow1 (260)
  MSL_ACT_CLIFF_JUMP_SLOW2 = 0x0105,    // ftCo_MS_CliffJumpSlow2 (261)
  MSL_ACT_CLIFF_JUMP_QUICK1 = 0x0106,   // ftCo_MS_CliffJumpQuick1 (262)
  MSL_ACT_CLIFF_JUMP_QUICK2 = 0x0107,   // ftCo_MS_CliffJumpQuick2 (263)

  // Match start entry states (ft_0C31.c / ftCo_Entry.c).
  MSL_ACT_ENTRY = 0x0142,        // ftCo_MS_Entry
  MSL_ACT_ENTRY_START = 0x0143,  // ftCo_MS_EntryStart
  MSL_ACT_ENTRY_END = 0x0144,    // ftCo_MS_EntryEnd
} MslActionId;

static inline uint8_t msl_action_owns_respawn_collision_skip(uint16_t action_id) {
  // Rebirth/RebirthWait set fp->x2219_b1, and Fighter_8006CB94 skips the common fighter collision
  // pass while that bit is live. Item-vs-fighter collision also rejects x2219_b1 targets.
  // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  return (action_id == (uint16_t)MSL_ACT_REBIRTH || action_id == (uint16_t)MSL_ACT_REBIRTH_WAIT)
             ? 1u
             : 0u;
}

// Additional GALE01 common action ids needed for fastfall gating.
//
// Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
enum {
  MSL_ACT_ATTACK_AIR_N = 0x0041,   // ftCo_MS_AttackAirN
  MSL_ACT_ATTACK_AIR_F = 0x0042,   // ftCo_MS_AttackAirF
  MSL_ACT_ATTACK_AIR_B = 0x0043,   // ftCo_MS_AttackAirB
  MSL_ACT_ATTACK_AIR_HI = 0x0044,  // ftCo_MS_AttackAirHi
  MSL_ACT_ATTACK_AIR_LW = 0x0045,  // ftCo_MS_AttackAirLw
  MSL_ACT_ESCAPE_AIR = 0x00EC,     // ftCo_MS_EscapeAir
};

// Fox/Falco up-special submotions needed for SpecialAirHi -> SpecialHiFall/Landing/Bound
// transitions.
//
// Decomp source:
// - refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion
//   ftFx_SM_SpecialHi        = ftCo_SM_Count + 14
//   ftFx_SM_SpecialHiLanding = ftCo_SM_Count + 15
//   ftFx_SM_SpecialHiFall    = ftCo_SM_Count + 16
//   ftFx_SM_SpecialHiBound   = ftCo_SM_Count + 17
// - refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_Submotion
//   ftCo_SM_Count = 295
enum {
  MSL_SM_FX_SPECIAL_HI = 309,
  MSL_SM_FX_SPECIAL_HI_LANDING = 310,
  MSL_SM_FX_SPECIAL_HI_FALL = 311,
  MSL_SM_FX_SPECIAL_HI_BOUND = 312,
};

// GALE01 common grounded attack action ids (ftCommon_MotionState).
//
// These are contiguous in GALE01 and (for common fighters) their collision callbacks use
// `ft_80084104` -> `ft_800827A0` -> `mpColl_8004B2DC`, which includes mpColl floor-edge snap
// (`mpColl_8004A45C_Floor`) behavior.
//
// Note: Fox/Falco in the FD suite are assumed to follow the common GALE01 ftCommon_MotionState
// contiguity for this grounded-attack block.
//
// Decomp pointers:
// - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Coll
// - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Coll
// - refs/melee/src/melee/ft/ft_081B.c::ft_80084104 (calls ft_800827A0)
// - refs/melee/src/melee/ft/ft_081B.c::ft_800827A0 (calls mpColl_8004B2DC)
// - refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC (uses mpColl_8004A45C_Floor)
enum {
  MSL_ACT_ATTACK_11 = 0x002C,  // ftCo_MS_Attack11
  MSL_ACT_ATTACK_12 = 0x002D,  // ftCo_MS_Attack12
  MSL_ACT_ATTACK_13 = 0x002E,  // ftCo_MS_Attack13

  MSL_ACT_ATTACK_100_START = 0x002F,  // ftCo_MS_Attack100Start
  MSL_ACT_ATTACK_100_LOOP = 0x0030,   // ftCo_MS_Attack100Loop
  MSL_ACT_ATTACK_100_END = 0x0031,    // ftCo_MS_Attack100End

  MSL_ACT_ATTACK_DASH = 0x0032,  // ftCo_MS_AttackDash

  MSL_ACT_ATTACK_S3_HI = 0x0033,            // ftCo_MS_AttackS3Hi
  MSL_ACT_ATTACK_S3_HI_S = 0x0034,          // ftCo_MS_AttackS3HiS
  MSL_ACT_ATTACK_S3_S = 0x0035,             // ftCo_MS_AttackS3S
  MSL_ACT_ATTACK_S3 = MSL_ACT_ATTACK_S3_S,  // ftCo_MS_AttackS3 (side tilt)
  MSL_ACT_ATTACK_S3_LW_S = 0x0036,          // ftCo_MS_AttackS3LwS
  MSL_ACT_ATTACK_S3_LW = 0x0037,            // ftCo_MS_AttackS3Lw
  MSL_ACT_ATTACK_HI3 = 0x0038,              // ftCo_MS_AttackHi3
  MSL_ACT_ATTACK_LW3 = 0x0039,              // ftCo_MS_AttackLw3

  MSL_ACT_ATTACK_S4_HI = 0x003A,    // ftCo_MS_AttackS4Hi
  MSL_ACT_ATTACK_S4_HI_S = 0x003B,  // ftCo_MS_AttackS4HiS
  MSL_ACT_ATTACK_S4_S = 0x003C,     // ftCo_MS_AttackS4S
  MSL_ACT_ATTACK_S4_LW_S = 0x003D,  // ftCo_MS_AttackS4LwS
  MSL_ACT_ATTACK_S4_LW = 0x003E,    // ftCo_MS_AttackS4Lw
  MSL_ACT_ATTACK_HI4 = 0x003F,      // ftCo_MS_AttackHi4
  MSL_ACT_ATTACK_LW4 = 0x0040,      // ftCo_MS_AttackLw4
};

// Additional GALE01 Fox/Falco action ids needed for fastfall gating.
//
// Decomp:
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
//   (ftFx_MS_SpecialNStart=341 .. ftFx_MS_SpecialAirNEnd=346)
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirN{Start,Loop,End}_Phys
//   (all call `ft_80084DB0`)
enum {
  MSL_ACT_FX_SPECIAL_N_START = 0x0155,      // ftFx_MS_SpecialNStart
  MSL_ACT_FX_SPECIAL_N_LOOP = 0x0156,       // ftFx_MS_SpecialNLoop
  MSL_ACT_FX_SPECIAL_N_END = 0x0157,        // ftFx_MS_SpecialNEnd
  MSL_ACT_FX_SPECIAL_AIR_N_START = 0x0158,  // ftFx_MS_SpecialAirNStart
  MSL_ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159,   // ftFx_MS_SpecialAirNLoop
  MSL_ACT_FX_SPECIAL_AIR_N_END = 0x015A,    // ftFx_MS_SpecialAirNEnd
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  //   (ftFx_MS_SpecialSStart=347 .. ftFx_MS_SpecialAirSEnd=352)
  MSL_ACT_FX_SPECIAL_S_START = 0x015B,      // ftFx_MS_SpecialSStart
  MSL_ACT_FX_SPECIAL_S = 0x015C,            // ftFx_MS_SpecialS
  MSL_ACT_FX_SPECIAL_S_END = 0x015D,        // ftFx_MS_SpecialSEnd
  MSL_ACT_FX_SPECIAL_AIR_S_START = 0x015E,  // ftFx_MS_SpecialAirSStart
  MSL_ACT_FX_SPECIAL_AIR_S = 0x015F,        // ftFx_MS_SpecialAirS
  MSL_ACT_FX_SPECIAL_AIR_S_END = 0x0160,    // ftFx_MS_SpecialAirSEnd
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  //   (ftFx_MS_SpecialHiHold=353 .. ftFx_MS_SpecialHiBound=359)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c (collision callbacks call cliff check)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Phys
  //   (calls `ft_80084DB0` for fall-like phys)
  MSL_ACT_FX_SPECIAL_HI_HOLD = 0x0161,      // ftFx_MS_SpecialHiHold
  MSL_ACT_FX_SPECIAL_HI_HOLD_AIR = 0x0162,  // ftFx_MS_SpecialHiHoldAir
  MSL_ACT_FX_SPECIAL_HI = 0x0163,           // ftFx_MS_SpecialHi
  MSL_ACT_FX_SPECIAL_AIR_HI = 0x0164,       // ftFx_MS_SpecialAirHi
  MSL_ACT_FX_SPECIAL_HI_LANDING = 0x0165,   // ftFx_MS_SpecialHiLanding
  MSL_ACT_FX_SPECIAL_HI_FALL = 0x0166,      // ftFx_MS_SpecialHiFall
  MSL_ACT_FX_SPECIAL_HI_BOUND = 0x0167,     // ftFx_MS_SpecialHiBound
  // Decomp:
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  //   (ftFx_MS_SpecialLwStart=360 .. ftFx_MS_SpecialAirLwTurn=369)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c (physics callbacks)
  //   - Aerial SpecialAirLw* use ftCommon_Fall + ftCommon_8007CF58 (x74_anim_vel.x friction/clamp).
  MSL_ACT_FX_SPECIAL_LW_START = 0x0168,      // ftFx_MS_SpecialLwStart
  MSL_ACT_FX_SPECIAL_LW_LOOP = 0x0169,       // ftFx_MS_SpecialLwLoop
  MSL_ACT_FX_SPECIAL_LW_HIT = 0x016A,        // ftFx_MS_SpecialLwHit
  MSL_ACT_FX_SPECIAL_LW_END = 0x016B,        // ftFx_MS_SpecialLwEnd
  MSL_ACT_FX_SPECIAL_LW_TURN = 0x016C,       // ftFx_MS_SpecialLwTurn
  MSL_ACT_FX_SPECIAL_AIR_LW_START = 0x016D,  // ftFx_MS_SpecialAirLwStart
  MSL_ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E,   // ftFx_MS_SpecialAirLwLoop
  MSL_ACT_FX_SPECIAL_AIR_LW_HIT = 0x016F,    // ftFx_MS_SpecialAirLwHit
  MSL_ACT_FX_SPECIAL_AIR_LW_END = 0x0170,    // ftFx_MS_SpecialAirLwEnd
  MSL_ACT_FX_SPECIAL_AIR_LW_TURN = 0x0171,   // ftFx_MS_SpecialAirLwTurn
};

// GALE01 "submotion" ids (aka `anim_id` / `ftCo_Submotion`) for common locomotion.
//
// Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
typedef enum MslSubmotionId {
  MSL_SM_WAIT1_0 = 2,                // ftCo_SM_Wait1_0
  MSL_SM_WALK_SLOW = 7,              // ftCo_SM_WalkSlow
  MSL_SM_WALK_MIDDLE = 8,            // ftCo_SM_WalkMiddle
  MSL_SM_WALK_FAST = 9,              // ftCo_SM_WalkFast
  MSL_SM_TURN = 10,                  // ftCo_SM_Turn
  MSL_SM_TURN_RUN = 11,              // ftCo_SM_TurnRun
  MSL_SM_DASH = 12,                  // ftCo_SM_Dash
  MSL_SM_RUN = 13,                   // ftCo_SM_Run
  MSL_SM_RUN_BRAKE = 14,             // ftCo_SM_RunBrake
  MSL_SM_KNEE_BEND = 15,             // ftCo_SM_Kneebend
  MSL_SM_JUMP_F = 16,                // ftCo_SM_JumpF
  MSL_SM_JUMP_B = 17,                // ftCo_SM_JumpB
  MSL_SM_JUMP_AERIAL_F = 18,         // ftCo_SM_JumpAerialF
  MSL_SM_JUMP_AERIAL_B = 19,         // ftCo_SM_JumpAerialB
  MSL_SM_FALL = 20,                  // ftCo_SM_Fall
  MSL_SM_FALL_F = 21,                // ftCo_SM_FallF
  MSL_SM_FALL_B = 22,                // ftCo_SM_FallB
  MSL_SM_FALL_AERIAL = 23,           // ftCo_SM_FallAerial
  MSL_SM_FALL_AERIAL_F = 24,         // ftCo_SM_FallAerialF
  MSL_SM_FALL_AERIAL_B = 25,         // ftCo_SM_FallAerialB
  MSL_SM_FALL_SPECIAL = 26,          // ftCo_SM_FallSpecial
  MSL_SM_FALL_SPECIAL_F = 27,        // ftCo_SM_FallSpecialF
  MSL_SM_FALL_SPECIAL_B = 28,        // ftCo_SM_FallSpecialB
  MSL_SM_DAMAGE_FALL = 29,           // ftCo_SM_DamageFall
  MSL_SM_SQUAT = 30,                 // ftCo_SM_Squat
  MSL_SM_SQUAT_WAIT = 31,            // ftCo_SM_SquatWait
  MSL_SM_SQUAT_UNK032 = 32,          // ftCo_SM_Unk032
  MSL_SM_SQUAT_WAIT_ITEM = 33,       // ftCo_SM_SquatWaitItem
  MSL_SM_SQUAT_RV = 34,              // ftCo_SM_SquatRv
  MSL_SM_LANDING = 35,               // ftCo_SM_Landing
  MSL_SM_LANDING_FALL_SPECIAL = 36,  // ftCo_SM_LandingFallSpecial

  MSL_SM_GUARD_ON = 37,               // ftCo_SM_GuardOn
  MSL_SM_GUARD = 38,                  // ftCo_SM_Guard
  MSL_SM_GUARD_OFF = 39,              // ftCo_SM_GuardOff
  MSL_SM_GUARD_DAMAGE = 40,           // ftCo_SM_GuardDamage
  MSL_SM_OTTOTTO = 210,               // ftCo_SM_Ottotto
  MSL_SM_OTTOTTO_WAIT = 211,          // ftCo_SM_OttottoWait
  MSL_SM_FURAFURA = 205,              // ftCo_SM_FuraFura
  MSL_SM_SHIELD_BREAK_FLY = 286,      // ftCo_SM_ShieldBreakFly
  MSL_SM_SHIELD_BREAK_FALL = 287,     // ftCo_SM_ShieldBreakFall
  MSL_SM_SHIELD_BREAK_DOWN_U = 288,   // ftCo_SM_ShieldBreakDownU
  MSL_SM_SHIELD_BREAK_DOWN_D = 289,   // ftCo_SM_ShieldBreakDownD
  MSL_SM_SHIELD_BREAK_STAND_U = 290,  // ftCo_SM_ShieldBreakStandU
  MSL_SM_SHIELD_BREAK_STAND_D = 291,  // ftCo_SM_ShieldBreakStandD
  MSL_SM_ESCAPE_N = 41,               // ftCo_SM_EscapeN
  MSL_SM_ESCAPE_F = 42,               // ftCo_SM_EscapeF
  MSL_SM_ESCAPE_B = 43,               // ftCo_SM_EscapeB
  MSL_SM_ESCAPE_AIR = 44,             // ftCo_SM_EscapeAir
  MSL_SM_REBOUND = 45,                // ftCo_SM_Rebound
  MSL_SM_ATTACK_11 = 46,              // ftCo_SM_Attack11
  MSL_SM_ATTACK_12 = 47,              // ftCo_SM_Attack12
  MSL_SM_ATTACK_13 = 48,              // ftCo_SM_Attack13
  // Grounded attacks (subset) used by the grounded A-attack selector.
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_ATTACK_DASH = 52,     // ftCo_SM_AttackDash
  MSL_SM_ATTACK_S3_HI = 53,    // ftCo_SM_AttackS3Hi
  MSL_SM_ATTACK_S3_HI_S = 54,  // ftCo_SM_AttackS3HiS
  MSL_SM_ATTACK_S3 = 55,       // ftCo_SM_AttackS3
  MSL_SM_ATTACK_S3_LW_S = 56,  // ftCo_SM_AttackS3LwS
  MSL_SM_ATTACK_S3_LW = 57,    // ftCo_SM_AttackS3Lw
  // Contiguous ftCo_Submotion ordering in GALE01:
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_Submotion
  // refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* motion-state table entries)
  MSL_SM_ATTACK_HI3 = 58,      // ftCo_SM_AttackHi3
  MSL_SM_ATTACK_LW3 = 59,      // ftCo_SM_AttackLw3
  MSL_SM_ATTACK_S4_HI = 60,    // ftCo_SM_AttackS4Hi
  MSL_SM_ATTACK_S4_HI_S = 61,  // ftCo_SM_AttackS4HiS
  MSL_SM_ATTACK_S4 = 62,       // ftCo_SM_AttackS4
  MSL_SM_ATTACK_S4_LW_S = 63,  // ftCo_SM_AttackS4LwS
  MSL_SM_ATTACK_S4_LW = 64,    // ftCo_SM_AttackS4Lw
  MSL_SM_ATTACK_HI4 = 66,      // ftCo_SM_AttackHi4
  MSL_SM_ATTACK_LW4 = 67,      // ftCo_SM_AttackLw4

  // Aerial attacks (ftCo_AttackAir*).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_ATTACK_AIR_N = 68,   // ftCo_SM_AttackAirN
  MSL_SM_ATTACK_AIR_F = 69,   // ftCo_SM_AttackAirF
  MSL_SM_ATTACK_AIR_B = 70,   // ftCo_SM_AttackAirB
  MSL_SM_ATTACK_AIR_HI = 71,  // ftCo_SM_AttackAirHi
  MSL_SM_ATTACK_AIR_LW = 72,  // ftCo_SM_AttackAirLw

  MSL_SM_LANDING_AIR_N = 73,   // ftCo_SM_LandingAirN
  MSL_SM_LANDING_AIR_F = 74,   // ftCo_SM_LandingAirF
  MSL_SM_LANDING_AIR_B = 75,   // ftCo_SM_LandingAirB
  MSL_SM_LANDING_AIR_HI = 76,  // ftCo_SM_LandingAirHi
  MSL_SM_LANDING_AIR_LW = 77,  // ftCo_SM_LandingAirLw

  // Damage (subset used by the Fox/Falco FD suite).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_DAMAGE_HI_1 = 165,      // ftCo_SM_DamageHi1
  MSL_SM_DAMAGE_HI_2 = 166,      // ftCo_SM_DamageHi2
  MSL_SM_DAMAGE_HI_3 = 167,      // ftCo_SM_DamageHi3
  MSL_SM_DAMAGE_N_1 = 168,       // ftCo_SM_DamageN1
  MSL_SM_DAMAGE_N_2 = 169,       // ftCo_SM_DamageN2
  MSL_SM_DAMAGE_N_3 = 170,       // ftCo_SM_DamageN3
  MSL_SM_DAMAGE_LW_1 = 171,      // ftCo_SM_DamageLw1
  MSL_SM_DAMAGE_LW_2 = 172,      // ftCo_SM_DamageLw2
  MSL_SM_DAMAGE_LW_3 = 173,      // ftCo_SM_DamageLw3
  MSL_SM_DAMAGE_AIR_1 = 174,     // ftCo_SM_DamageAir1
  MSL_SM_DAMAGE_AIR_2 = 175,     // ftCo_SM_DamageAir2
  MSL_SM_DAMAGE_AIR_3 = 176,     // ftCo_SM_DamageAir3
  MSL_SM_DAMAGE_FLY_HI = 177,    // ftCo_SM_DamageFlyHi
  MSL_SM_DAMAGE_FLY_N = 178,     // ftCo_SM_DamageFlyN
  MSL_SM_DAMAGE_FLY_LW = 179,    // ftCo_SM_DamageFlyLw
  MSL_SM_DAMAGE_FLY_TOP = 180,   // ftCo_SM_DamageFlyTop
  MSL_SM_DAMAGE_FLY_ROLL = 181,  // ftCo_SM_DamageFlyRoll

  // Downed / knockdown (suite-present subset).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_DOWN_BOUND_U = 183,   // ftCo_SM_DownBoundU
  MSL_SM_DOWN_WAIT_U = 184,    // ftCo_SM_DownWaitU
  MSL_SM_DOWN_DAMAGE_U = 185,  // ftCo_SM_DownDamageU
  MSL_SM_DOWN_STAND_U = 186,   // ftCo_SM_DownStandU
  MSL_SM_DOWN_ATTACK_U = 187,  // ftCo_SM_DownAttackU
  MSL_SM_DOWN_FOWARD_U = 188,  // ftCo_SM_DownFowardU
  MSL_SM_DOWN_BACK_U = 189,    // ftCo_SM_DownBackU
  MSL_SM_DOWN_BOUND_D = 191,   // ftCo_SM_DownBoundD
  MSL_SM_DOWN_WAIT_D = 192,    // ftCo_SM_DownWaitD
  MSL_SM_DOWN_DAMAGE_D = 193,  // ftCo_SM_DownDamageD
  MSL_SM_DOWN_STAND_D = 194,   // ftCo_SM_DownStandD
  MSL_SM_DOWN_ATTACK_D = 195,  // ftCo_SM_DownAttackD
  MSL_SM_DOWN_FOWARD_D = 196,  // ftCo_SM_DownFowardD
  MSL_SM_DOWN_BACK_D = 197,    // ftCo_SM_DownBackD

  // Tech / passive (suite-present subset).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_PASSIVE = 199,            // ftCo_SM_Passive
  MSL_SM_PASSIVE_STAND_F = 200,    // ftCo_SM_PassiveStandF
  MSL_SM_PASSIVE_STAND_B = 201,    // ftCo_SM_PassiveStandB
  MSL_SM_PASSIVE_WALL = 202,       // ftCo_SM_PassiveWall
  MSL_SM_PASSIVE_WALL_JUMP = 203,  // ftCo_SM_PassiveWallJump
  MSL_SM_PASSIVE_CEIL = 204,       // ftCo_SM_PassiveCeil
  MSL_SM_WALL_DAMAGE = 212,        // ftCo_SM_WallDamage
  MSL_SM_STOP_WALL = 213,          // ftCo_SM_StopWall
  MSL_SM_STOP_CEIL = 214,          // ftCo_SM_StopCeil

  // Grab / throw / capture / thrown (suite-present subset).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_CATCH = 242,              // ftCo_SM_Catch
  MSL_SM_CATCH_DASH = 243,         // ftCo_SM_CatchDash
  MSL_SM_CATCH_WAIT = 244,         // ftCo_SM_CatchWait
  MSL_SM_CATCH_ATTACK = 245,       // ftCo_SM_CatchAttack
  MSL_SM_CATCH_CUT = 246,          // ftCo_SM_CatchCut
  MSL_SM_THROW_F = 247,            // ftCo_SM_ThrowF
  MSL_SM_THROW_B = 248,            // ftCo_SM_ThrowB
  MSL_SM_THROW_HI = 249,           // ftCo_SM_ThrowHi
  MSL_SM_THROW_LW = 250,           // ftCo_SM_ThrowLw
  MSL_SM_CAPTURE_PULLED_HI = 251,  // ftCo_SM_CapturePulledHi
  MSL_SM_CAPTURE_WAIT_HI = 252,    // ftCo_SM_CaptureWaitHi
  MSL_SM_CAPTURE_DAMAGE_HI = 253,  // ftCo_SM_CaptureDamageHi
  MSL_SM_CAPTURE_PULLED_LW = 254,  // ftCo_SM_CapturePulledLw
  MSL_SM_CAPTURE_WAIT_LW = 255,    // ftCo_SM_CaptureWaitLw
  MSL_SM_CAPTURE_DAMAGE_LW = 256,  // ftCo_SM_CaptureDamageLw
  MSL_SM_CAPTURE_CUT = 257,        // ftCo_SM_CaptureCut
  MSL_SM_CAPTURE_JUMP = 258,       // ftCo_SM_CaptureJump
  MSL_SM_CAPTURE_NECK = 259,       // ftCo_SM_CaptureNeck
  MSL_SM_CAPTURE_FOOT = 260,       // ftCo_SM_CaptureFoot
  MSL_SM_THROWN_F = 262,           // ftCo_SM_ThrownF
  MSL_SM_THROWN_B = 263,           // ftCo_SM_ThrownB
  MSL_SM_THROWN_HI = 264,          // ftCo_SM_ThrownHi
  MSL_SM_THROWN_LW = 265,          // ftCo_SM_ThrownLw

  // Cliff / ledge (subset used by the FD suite).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  MSL_SM_MISS_FOOT = 215,           // ftCo_SM_MissFoot
  MSL_SM_CLIFF_CATCH = 216,         // ftCo_SM_CliffCatch
  MSL_SM_CLIFF_WAIT = 217,          // ftCo_SM_CliffWait
  MSL_SM_CLIFF_CLIMB_SLOW = 219,    // ftCo_SM_CliffClimbSlow
  MSL_SM_CLIFF_CLIMB_QUICK = 220,   // ftCo_SM_CliffClimbQuick
  MSL_SM_CLIFF_ATTACK_SLOW = 221,   // ftCo_SM_CliffAttackSlow
  MSL_SM_CLIFF_ATTACK_QUICK = 222,  // ftCo_SM_CliffAttackQuick
  MSL_SM_CLIFF_ESCAPE_SLOW = 223,   // ftCo_SM_CliffEscapeSlow
  MSL_SM_CLIFF_ESCAPE_QUICK = 224,  // ftCo_SM_CliffEscapeQuick
  MSL_SM_CLIFF_JUMP_SLOW1 = 225,    // ftCo_SM_CliffJumpSlow1
  MSL_SM_CLIFF_JUMP_SLOW2 = 226,    // ftCo_SM_CliffJumpSlow2
  MSL_SM_CLIFF_JUMP_QUICK1 = 227,   // ftCo_SM_CliffJumpQuick1
  MSL_SM_CLIFF_JUMP_QUICK2 = 228,   // ftCo_SM_CliffJumpQuick2

  // Match-start entry: ftCo_Submotion::ftCo_SM_EntryStart (suite present).
  MSL_SM_ENTRY_START = 238,
} MslSubmotionId;

static inline uint8_t msl_action_is_ground_locomotion(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_TURN:
    case MSL_ACT_TURN_RUN:
    case MSL_ACT_DASH:
    case MSL_ACT_RUN:
    case MSL_ACT_RUN_DIRECT:
    case MSL_ACT_RUN_BRAKE:
    case MSL_ACT_KNEE_BEND:
    case MSL_ACT_SQUAT:
    case MSL_ACT_SQUAT_WAIT:
    case MSL_ACT_SQUAT_RV:
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL:
    case MSL_ACT_LANDING_AIR_N:
    case MSL_ACT_LANDING_AIR_F:
    case MSL_ACT_LANDING_AIR_B:
    case MSL_ACT_LANDING_AIR_HI:
    case MSL_ACT_LANDING_AIR_LW:
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
    case MSL_ACT_ESCAPE_F:
    case MSL_ACT_ESCAPE_B:
    case MSL_ACT_ESCAPE_N:
    case MSL_ACT_ATTACK_DASH:
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
    case MSL_ACT_ATTACK_HI3:
    case MSL_ACT_ATTACK_LW3:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_air_locomotion(uint16_t action_id) {
  // Semantics: "air locomotion" here is used as a broad gate for locomotion input/state-machine
  // behavior (Jump/Fall/FallSpecial families), not as a proxy for "any airborne state that uses
  // ft_80084DB0".
  //
  // Note: DamageFall is intentionally excluded from this helper to avoid leaking damage-state
  // behavior into locomotion gates (e.g. special-move entry). DamageFall still uses the common
  // airborne fall helper in its phys callback, so it is included explicitly in
  // msl_action_allows_fastfall() instead.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Phys
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  switch (action_id) {
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_allows_fastfall(uint16_t action_id) {
  // The ftCommon_CheckFallFast + ftCommon_Fall/FallFast helper (`ft_80084DB0`) is used by many
  // common airborne action states, not just the basic Fall motions.
  //
  // Decomp refs:
  // - Check: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
  // - Common helper: refs/melee/src/melee/ft/ft_081B.c:1347-1359 (ft_80084DB0)
  // - Example callers: refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c and
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c
  if (msl_action_is_air_locomotion(action_id)) {
    return 1;
  }
  switch (action_id) {
    // DamageFall uses the common airborne fall helper in its phys callback.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Phys
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    case MSL_ACT_DAMAGE_FALL:
    // FlyReflect Phys delegates to ft_80084DB0 after the wall/ceiling bounce entry.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::ftCo_FlyReflect_Phys
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_F:
    case MSL_ACT_ATTACK_AIR_B:
    case MSL_ACT_ATTACK_AIR_HI:
    case MSL_ACT_ATTACK_AIR_LW:
    // Decomp: MissFoot_Phys calls the common airborne helper.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Phys
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    case MSL_ACT_MISS_FOOT:
    // Decomp: `ftFx_SpecialAirN{Start,Loop,End}_Phys` call `ft_80084DB0`.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Phys
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Phys
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNEnd_Phys
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
    // Decomp: `ftFx_SpecialHiFall_Phys` calls `ft_80084DB0`.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Phys
    case MSL_ACT_FX_SPECIAL_HI_FALL:
    case MSL_ACT_ESCAPE_AIR:
    // Decomp: both CliffJump2 variants call `ft_80084DB0` after the first-frame x0 gate.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Phys
    case MSL_ACT_CLIFF_JUMP_SLOW2:
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_grabbed_victim(uint16_t action_id) {
  // Grab/capture/thrown victim states where the engine drives `fp->cur_pos` from an attachment joint.
  // Decomp example: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508.
  switch (action_id) {
    case MSL_ACT_CAPTURE_PULLED_HI:
    case MSL_ACT_CAPTURE_WAIT_HI:
    case MSL_ACT_CAPTURE_DAMAGE_HI:
    case MSL_ACT_CAPTURE_PULLED_LW:
    case MSL_ACT_CAPTURE_WAIT_LW:
    case MSL_ACT_CAPTURE_DAMAGE_LW:
    case MSL_ACT_CAPTURE_NECK:
    case MSL_ACT_CAPTURE_FOOT:
    case MSL_ACT_THROWN_F:
    case MSL_ACT_THROWN_B:
    case MSL_ACT_THROWN_HI:
    case MSL_ACT_THROWN_LW:
    case MSL_ACT_THROWN_LW_WOMEN:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_thrown_victim(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_THROWN_F:
    case MSL_ACT_THROWN_B:
    case MSL_ACT_THROWN_HI:
    case MSL_ACT_THROWN_LW:
    case MSL_ACT_THROWN_LW_WOMEN:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_throw_owner(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_THROW_F:
    case MSL_ACT_THROW_B:
    case MSL_ACT_THROW_HI:
    case MSL_ACT_THROW_LW:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_capture_pulled_wait_damage_victim(uint16_t action_id) {
  // CapturePulled*/CaptureWait*/CaptureDamage* (common grabbed victim loop).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18.
  switch (action_id) {
    case MSL_ACT_CAPTURE_PULLED_HI:
    case MSL_ACT_CAPTURE_WAIT_HI:
    case MSL_ACT_CAPTURE_DAMAGE_HI:
    case MSL_ACT_CAPTURE_PULLED_LW:
    case MSL_ACT_CAPTURE_WAIT_LW:
    case MSL_ACT_CAPTURE_DAMAGE_LW:
      return 1;
    default:
      return 0;
  }
}
