#pragma once

#include <stdint.h>

// GALE01 action ids (aka `FtMotionId` / `ftCommon_MotionState`) for common locomotion.
//
// Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
typedef enum MslActionId {
  MSL_ACT_WAIT = 0x000E,                  // ftCo_MS_Wait
  MSL_ACT_WALK_SLOW = 0x000F,             // ftCo_MS_WalkSlow
  MSL_ACT_WALK_MIDDLE = 0x0010,           // ftCo_MS_WalkMiddle
  MSL_ACT_WALK_FAST = 0x0011,             // ftCo_MS_WalkFast
  MSL_ACT_TURN = 0x0012,                  // ftCo_MS_Turn
  MSL_ACT_TURN_RUN = 0x0013,              // ftCo_MS_TurnRun
  MSL_ACT_DASH = 0x0014,                  // ftCo_MS_Dash
  MSL_ACT_RUN = 0x0015,                   // ftCo_MS_Run
  MSL_ACT_RUN_DIRECT = 0x0016,            // ftCo_MS_RunDirect
  MSL_ACT_RUN_BRAKE = 0x0017,             // ftCo_MS_RunBrake
  MSL_ACT_KNEE_BEND = 0x0018,             // ftCo_MS_KneeBend
  MSL_ACT_JUMP_F = 0x0019,                // ftCo_MS_JumpF
  MSL_ACT_JUMP_B = 0x001A,                // ftCo_MS_JumpB
  MSL_ACT_JUMP_AERIAL_F = 0x001B,         // ftCo_MS_JumpAerialF
  MSL_ACT_JUMP_AERIAL_B = 0x001C,         // ftCo_MS_JumpAerialB
  MSL_ACT_FALL = 0x001D,                  // ftCo_MS_Fall
  MSL_ACT_FALL_F = 0x001E,                // ftCo_MS_FallF
  MSL_ACT_FALL_B = 0x001F,                // ftCo_MS_FallB
  MSL_ACT_FALL_AERIAL = 0x0020,           // ftCo_MS_FallAerial
  MSL_ACT_FALL_AERIAL_F = 0x0021,         // ftCo_MS_FallAerialF
  MSL_ACT_FALL_AERIAL_B = 0x0022,         // ftCo_MS_FallAerialB
  MSL_ACT_FALL_SPECIAL = 0x0023,          // ftCo_MS_FallSpecial
  MSL_ACT_FALL_SPECIAL_F = 0x0024,        // ftCo_MS_FallSpecialF
  MSL_ACT_FALL_SPECIAL_B = 0x0025,        // ftCo_MS_FallSpecialB
  MSL_ACT_DAMAGE_FALL = 0x0026,           // ftCo_MS_DamageFall
  MSL_ACT_LANDING = 0x002A,               // ftCo_MS_Landing
  MSL_ACT_LANDING_FALL_SPECIAL = 0x002B,  // ftCo_MS_LandingFallSpecial

  // Aerial attack landing lag states (ftCo_LandingAir_*).
  MSL_ACT_LANDING_AIR_N = 0x0046,   // ftCo_MS_LandingAirN
  MSL_ACT_LANDING_AIR_F = 0x0047,   // ftCo_MS_LandingAirF
  MSL_ACT_LANDING_AIR_B = 0x0048,   // ftCo_MS_LandingAirB
  MSL_ACT_LANDING_AIR_HI = 0x0049,  // ftCo_MS_LandingAirHi
  MSL_ACT_LANDING_AIR_LW = 0x004A,  // ftCo_MS_LandingAirLw

  // Shield / Guard (subset).
  MSL_ACT_GUARD_ON = 0x00B2,       // ftCo_MS_GuardOn
  MSL_ACT_GUARD = 0x00B3,          // ftCo_MS_Guard
  MSL_ACT_GUARD_OFF = 0x00B4,      // ftCo_MS_GuardOff
  MSL_ACT_GUARD_SET_OFF = 0x00B5,  // ftCo_MS_GuardSetOff
  MSL_ACT_GUARD_REFLECT = 0x00B6,  // ftCo_MS_GuardReflect

  // Shield defensive options (grounded).
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`.
  MSL_ACT_ESCAPE_F = 0x00E9,  // ftCo_MS_EscapeF (roll forward)
  MSL_ACT_ESCAPE_B = 0x00EA,  // ftCo_MS_EscapeB (roll backward)
  MSL_ACT_ESCAPE_N = 0x00EB,  // ftCo_MS_EscapeN (spotdodge)
} MslActionId;

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
  MSL_SM_LANDING = 35,               // ftCo_SM_Landing
  MSL_SM_LANDING_FALL_SPECIAL = 36,  // ftCo_SM_LandingFallSpecial

  MSL_SM_GUARD_ON = 37,      // ftCo_SM_GuardOn
  MSL_SM_GUARD = 38,         // ftCo_SM_Guard
  MSL_SM_GUARD_OFF = 39,     // ftCo_SM_GuardOff
  MSL_SM_GUARD_DAMAGE = 40,  // ftCo_SM_GuardDamage
  MSL_SM_ESCAPE_N = 41,      // ftCo_SM_EscapeN
  MSL_SM_ESCAPE_F = 42,      // ftCo_SM_EscapeF
  MSL_SM_ESCAPE_B = 43,      // ftCo_SM_EscapeB
  MSL_SM_ESCAPE_AIR = 44,    // ftCo_SM_EscapeAir

  MSL_SM_LANDING_AIR_N = 73,   // ftCo_SM_LandingAirN
  MSL_SM_LANDING_AIR_F = 74,   // ftCo_SM_LandingAirF
  MSL_SM_LANDING_AIR_B = 75,   // ftCo_SM_LandingAirB
  MSL_SM_LANDING_AIR_HI = 76,  // ftCo_SM_LandingAirHi
  MSL_SM_LANDING_AIR_LW = 77,  // ftCo_SM_LandingAirLw
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
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t msl_action_is_air_locomotion(uint16_t action_id) {
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
    case MSL_ACT_DAMAGE_FALL:
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
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_F:
    case MSL_ACT_ATTACK_AIR_B:
    case MSL_ACT_ATTACK_AIR_HI:
    case MSL_ACT_ATTACK_AIR_LW:
    case MSL_ACT_ESCAPE_AIR:
      return 1;
    default:
      return 0;
  }
}
