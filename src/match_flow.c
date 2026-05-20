#include "match_flow.h"

#include "action_ids.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "attack_identity.h"
#include "blaster.h"
#include "char_params.h"
#include "combat.h"
#include "common_params.h"
#include "input_axis.h"
#include "instance_id.h"
#include "stage_collision.h"
#include "staling.h"
#include "trigger_input.h"

enum { MSL_ANIM_NONE_U32 = 0xFFFFFFFFu };
enum { MSL_STAGE_POKEMON_STADIUM = 3u };

static inline uint8_t match_flow_is_dead_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
    case MSL_ACT_DEAD_UP_STAR:
    case MSL_ACT_DEAD_UP_FALL:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT:
    case MSL_ACT_DEAD_UP_FALL_ICE:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_ICE:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t match_flow_is_entry_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_ENTRY:
    case MSL_ACT_ENTRY_START:
    case MSL_ACT_ENTRY_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t match_flow_is_respawn_action(uint16_t a) {
  return (uint8_t)(a == MSL_ACT_REBIRTH || a == MSL_ACT_REBIRTH_WAIT);
}

static inline uint8_t match_flow_is_dead_up_fall_entry_action(uint16_t a) {
  return (uint8_t)(a == MSL_ACT_DEAD_UP_FALL || a == MSL_ACT_DEAD_UP_FALL_ICE);
}

static inline uint8_t match_flow_is_dead_up_fall_hitcamera_action(uint16_t a) {
  return (uint8_t)(a == MSL_ACT_DEAD_UP_FALL_HIT_CAMERA ||
                   a == MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT ||
                   a == MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_ICE);
}

static inline void match_flow_identity_reset_Fighter_UnkInitReset_80067C98_subset(MslBatch* batch,
                                                                                  size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: Fighter_UnkInitReset_80067C98 reset lane for hit attribution/combo ownership:
  // - fp->dmg.x18c4_source_ply = 6 (Slippi last_hit_by sentinel)
  // - fp->x208C = 0 (Slippi last_attack_landed)
  // - fp->x2090 = 0 (Slippi combo_count)
  // - fp->x2098 = 0 (combo timer)
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
  batch->state.last_hit_by[idx] = 6u;
  // Decomp/asm: Fighter_UnkProcessDeath call-chain invokes ft_800892D4, which clears
  // fp->x18EC (Slippi instance_hit_by) before Rebirth entry.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
  batch->state.instance_hit_by[idx] = 0u;
  batch->state.last_attack_landed[idx] = 0u;
  batch->state.combo_count[idx] = 0u;
  batch->state.combo_timer_x2098[idx] = 0u;
  // Decomp: Fighter_UnkInitReset also clears fp->x2094 (combo victim gobj).
  // Keep the simulator's victim latch aligned so ftColl_800764DC ownership starts from a clean lane.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
  batch->state.combo_victim_port[idx] = 0xFFu;
  batch->state.combo_victim_instance_id[idx] = 0u;
}

static inline void freeze_no_submotion_timebase(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
  msl_anim_timebase_seed(batch, idx, -1.0f, 0.0f);
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = 0;
  // Decomp: ftCo_Fall_Enter calls Fighter_ChangeMotionState with Ft_MF_KeepFastFall, then clamps
  // air drift; it does not reset fp->x671_timer_lstick_tilt_y. Preserve the existing tilt timer so
  // RebirthWait/EntryEnd exits while holding down do not synthesize a fresh fastfall flick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
}

static inline void match_flow_zero_common_velocities(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // ftCommon_8007E2FC clears the common velocity bundle. Match-flow callers use it at discrete
  // phase boundaries, so model only the lanes represented in the runtime state.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
}

static inline float entry_x20(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0.0f;
  }
  const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
  if (phys == NULL) {
    return 0.0f;
  }
  // Decomp: ft_0C31.c::ftCo_800C6408 computes:
  //   temp_f0 = fp->x34_scale.y * fp->co_attrs.trophy_scale;
  //   temp_f0_2 = 1.497345 * temp_f0;
  //   fp->mv.co.entry.x20 = temp_f0_2;
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6408
  const float trophy_scale = phys->trophy_scale;
  const float scale_y = batch->state.fighter_scale_y[idx];
  return (float)(1.497345f * trophy_scale * scale_y);
}

static inline float entry_base_y_from_entry_start(float pos_y, float x20, uint8_t timer,
                                                  int entry_start_frames) {
  if (entry_start_frames <= 0) {
    return pos_y;
  }
  const float denom = (float)entry_start_frames;
  const float t = (float)timer;
  const float frac = (denom - t) / denom;
  return pos_y - x20 * frac;
}

static inline float entry_base_y_from_entry_end(float pos_y, float x20, uint8_t timer,
                                                int entry_start_frames) {
  if (entry_start_frames <= 0) {
    return pos_y;
  }
  const float denom = (float)entry_start_frames;
  const float t = (float)timer;
  const float frac = t / denom;
  return pos_y - x20 * frac;
}

static inline void enter_entry_start(MslBatch* batch, size_t idx, const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const int n = (int)c->entry_start_frames;
  const uint8_t start_timer = (uint8_t)((n > 1) ? (n - 1) : 0);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ENTRY_START;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ENTRY_START;
  batch->state.match_flow_timer[idx] = start_timer;
  batch->state.on_ground[idx] = 0;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  // EntryStart uses a real submotion; start its timebase at 0.0.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Entry->EntryStart transition ownership in decomp lives in Entry_Anim (prio-1 callback), i.e.
  // after the frame's anim advance. This simulator performs match-flow transitions before the shared
  // anim-timebase pass, so seed the pre-tick lane at -1 to preserve the first EntryStart post-frame
  // action_frame=0 snapshot shape.
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_Entry_Anim
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_EntryStart_Anim
  msl_anim_timebase_seed(batch, idx, -1.0f, 1.0f);

  // Position update for the first EntryStart frame after transition:
  // - EntryStart_Anim decrements timer before Phys.
  // - EntryStart_Phys uses:
  //     frac = (x6BC - timer) / x6BC
  //     pos_y = base_y + x20 * frac
  // Here, `start_timer` is already (x6BC - 1), matching the post-frame for the first EntryStart
  // frame in the suite.
  const float x20 = entry_x20(batch, idx);
  if (n > 0) {
    const float denom = (float)n;
    const float frac = (denom - (float)start_timer) / denom;
    batch->state.pos_y[idx] = batch->state.pos_y[idx] + x20 * frac;
  }
}

static inline void enter_entry_end(MslBatch* batch, size_t idx, const MslCommonParams* c,
                                   float base_y, float x20) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const int n = (int)c->entry_end_frames;
  const uint8_t timer = (uint8_t)(n > 0 ? n : 0);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ENTRY_END;
  batch->state.match_flow_timer[idx] = timer;
  batch->state.on_ground[idx] = 0;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;

  // Decomp: Fighter_ChangeMotionState callsite updates identity fields on EntryEnd entry:
  // - ft_800890D0 (attack_id/attack_instance)
  // - ft_800895E0 (instance_id)
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // EntryEnd is observed with no submotion in the suite (animation_index=0xFFFFFFFF),
  // with action_frame=-1 and state_age=-1.
  freeze_no_submotion_timebase(batch, idx);

  // Decomp: ft_0C31.c::ftCo_800C6B6C sets:
  //   fp->cur_pos.y = fp->mv.co.entry.x4 + fp->mv.co.entry.x20;
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6B6C
  batch->state.pos_y[idx] = base_y + x20;
}

static inline void enter_rebirth(MslBatch* batch, size_t idx, const MslCommonParams* c,
                                 uint32_t stage_id, int port0) {
  if (batch == NULL || c == NULL) {
    return;
  }
  MslStageBounds cam = {0};
  MslStagePoint2 respawn = {0};
  if (!stage_collision_get_cam_bounds_world(stage_id, &cam) ||
      !stage_collision_get_respawn_point(stage_id, port0, &respawn)) {
    return;
  }

  // Decomp: respawn processing calls ft_800890BC (reset to attackID=1, instance=0).
  // and ft_800892D4 (clear instance_id/x2073 gate state) before Rebirth motion-state entry.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354
  //
  // Damage/shield reset ownership:
  // - Fighter_UnkProcessDeath_80068354 calls Fighter_UnkInitReset_80067C98 before Rebirth.
  // - Fighter_UnkInitReset_80067C98 reloads `fp->dmg.x1830_percent` from Player_GetDamage and
  //   clears `fp->dmg.x1838_percentTemp`.
  // - The same reset initializes `fp->shield_health` from ftCommonData.x260 and clears x19A0,
  //   x19A4, and `lightshield_amount`.
  // - This lite sim does not carry a separate Player_GetDamage lane; within the stock/percent
  //   target domain the respawn source is 0, so Dead* -> Rebirth must clear both percent lanes.
  // refs/melee/src/melee/ft/fighter.c::{
  //   Fighter_UnkProcessDeath_80068354,Fighter_UnkInitReset_80067C98
  // }
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
  attack_identity_reset_ft_800890BC(batch, idx);
  match_flow_identity_reset_Fighter_UnkInitReset_80067C98_subset(batch, idx);
  instance_id_reset_ft_800892D4(batch, idx);
  // Decomp: stock reset clears the player's stale-move table before Rebirth resumes gameplay.
  // Leaving the prior stock's table live makes the next-stock first hits stale during rollout.
  // refs/melee/src/melee/pl/plstale.c::plStale_ResetStaleMoveTableForPlayer
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354
  staling_queue_reset_for_player(batch, idx);
  batch->state.percent[idx] = 0.0f;
  batch->state.percent_temp[idx] = 0.0f;
  batch->state.shield_hp[idx] = c->start_shield_health;
  batch->state.lightshield_amount[idx] = 0.0f;
  batch->state.combat_shield_hit_int_damage[idx] = 0u;
  batch->state.combat_shield_damage_taken[idx] = 0u;
  // Seed-bridge (decomp call-chain parity):
  // - Rebirth entry runs Fighter_UnkProcessDeath_80068354 before Fighter_ChangeMotionState(Rebirth).
  // - That call chain includes unmodeled internals (ftCo_800BFFAC / ftCo_800C0074 / ...), and
  //   suite-observed Dead* -> Rebirth transitions show one hidden plAttack_80037B08 consumption
  //   before the fighter's own ft_800895E0 write.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_800D4FF4
  // refs/melee/build/GALE01/asm/melee/ft/fighter.s::Fighter_UnkProcessDeath_80068354
  instance_id_counter_consume_plAttack_80037B08(batch, idx);

  // Decomp: gm_1601.c::fn_8016719C stores two player-slot positions before the fighter enters
  // Rebirth:
  // - Player_SetSpawnPlatformPos receives the resolved spawn-platform target.
  // - Player_80032768 receives the start coordinate loaded later by Fighter_UnkInitReset_80067C98.
  // Most supported stages expose that start Y as the world camera top. Pokemon Stadium's normal
  // stage path uses the source default Stage_GetCamBoundsTopOffset top (120) for the player start
  // coordinate while its active camera bounds remain wider/taller for camera/death ownership.
  // refs/melee/src/melee/gm/gm_1601.c::{fn_8016719C,fn_80167638}
  // refs/melee/src/melee/pl/player.c::{Player_80032768,Player_LoadPlayerCoords}
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
  // refs/melee/src/melee/gr/ground.c::{Ground_801C39C0,Stage_GetCamBoundsTopOffset}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBIRTH;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  {
    const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
    if (ch != NULL) {
      // Decomp/asm: Dead*->Rebirth enter path (ftCo_800D4FF4) calls ftCommon_8007D5D4 before the
      // Rebirth motion callback chain; ftCommon_8007D5D4 sets fp->x1968_jumpsUsed = 1.
      // Slippi post-frame exposes jumps remaining, so Rebirth entry snapshots max_jumps-1.
      // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_800D4FF4
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
      batch->state.jumps_left[idx] = (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
    }
  }
  // Snapshot-order bridge:
  // - In engine order, dead->rebirth/rebirthwait transition callbacks run in the match-flow update
  //   path, and post-frame `state_age` for the first steady Rebirth lane is observed at 0.
  // - This simulator performs match-flow transitions before the shared anim-timebase pass, so
  //   entering at 0 would advance to 1 in the same frame.
  // Seed the pre-tick lane at -1 to preserve first post-transition frame parity (0 after this
  // frame's anim pass), while keeping Fighter_ChangeMotionState identity side effects above.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_Dead{Down,Left,Right,UpStar}_Anim
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_Rebirth_Anim
  msl_anim_timebase_seed(batch, idx, -1.0f, 1.0f);

  batch->state.on_ground[idx] = 0;
  // Rebirth entry is airborne (camera-top spawn). Clear floor index on entry so post-frame
  // ground_id matches the airborne snapshot shape (0xFFFF) instead of inheriting Dead* contact.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D4FF4 (Rebirth enter path)
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (ground ID from fp+0x83C)
  batch->state.ground_id[idx] = 0xFFFFu;
  batch->state.ground_normal_x[idx] = 0.0f;
  batch->state.ground_normal_y[idx] = 1.0f;
  batch->state.ground_contact_x[idx] = 0.0f;
  batch->state.ground_contact_y[idx] = 0.0f;
  // Decomp reset path reinitializes facing on respawn before Rebirth becomes active:
  // - Fighter_UnkInitReset_80067C98 loads player coords, then sets `fp->facing_dir =
  //   Player_GetFacingDirection(fp->player_id)`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
  //
  // Decomp: gm_1601.c::fn_8016719C sets respawn facing from the resolved spawn-platform x:
  //   if (respawn_pos.x >= 0.0f) Player_SetFacingDirection(slot, -1.0f);
  //   else                         Player_SetFacingDirection(slot, +1.0f);
  // refs/melee/src/melee/gm/gm_1601.c::fn_8016719C
  // data/stages/bin/*.bin::MSLSTG01 respawn_points
  batch->state.facing[idx] = (uint8_t)(respawn.x < 0.0f ? 1u : 0u);
  batch->state.pos_x[idx] = respawn.x;
  const float rebirth_start_y =
      (stage_id == (uint32_t)MSL_STAGE_POKEMON_STADIUM) ? 120.0f : cam.top;
  batch->state.pos_y[idx] = rebirth_start_y;

  // Rebirth fall speed is source-derived: ftCo_Rebirth_Phys computes velocity from the
  // Fighter_UnkInitReset-loaded start coordinate toward Player_GetSpawnPlatformPos over the
  // remaining Rebirth timer (p_ftCommonData + 0x5D0).
  // Decomp: refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (Rebirth enter sets fp->x2340 from 0x5D0)
  // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_Rebirth_Phys
  const int frames = (int)c->rebirth_timer_frames;
  float vy = 0.0f;
  if (frames > 0) {
    vy = (respawn.y - rebirth_start_y) / (float)frames;
  }
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = vy;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.match_flow_timer[idx] = (uint8_t)(frames > 0 ? (frames > 255 ? 255 : frames) : 0);
  batch->state.match_flow_pending_rebirth_char_id[idx] = 0u;
}

static inline void enter_eliminated_dead_slot(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Zero-stock terminal player slots do not auto-enter Rebirth. In teams, gm_16AE only restores a
  // zero-stock teammate through the explicit stock-share path after a donor loses a stock; otherwise
  // the player remains an inactive dead slot. Slippi exposes that inactive slot as a zeroed
  // DeadDown row rather than a respawn platform fighter.
  // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918_inline
  batch->state.char_id[idx] = 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DEAD_DOWN;
  batch->state.animation_index[idx] = 0u;
  batch->state.stocks[idx] = 0u;
  batch->state.percent[idx] = 0.0f;
  batch->state.percent_temp[idx] = 0.0f;
  batch->state.shield_hp[idx] = 0.0f;
  batch->state.lightshield_amount[idx] = 0.0f;
  batch->state.jumps_left[idx] = 0u;
  batch->state.facing[idx] = 0u;
  batch->state.facing_dir1[idx] = -1;
  batch->state.pos_x[idx] = 0.0f;
  batch->state.pos_y[idx] = 0.0f;
  batch->state.pos_z[idx] = 0.0f;
  batch->state.on_ground[idx] = 1u;
  batch->state.ground_id[idx] = 0u;
  batch->state.ground_contact_x[idx] = 0.0f;
  batch->state.ground_contact_y[idx] = 0.0f;
  batch->state.ground_normal_x[idx] = 0.0f;
  batch->state.ground_normal_y[idx] = 1.0f;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.hitlag[idx] = 0u;
  batch->state.hitstun[idx] = 0u;
  batch->state.hurtbox_state[idx] = 0u;
  batch->state.hurtcap_count[idx] = 0u;
  batch->state.hitbox_count[idx] = 0u;
  batch->state.instance_id[idx] = 0u;
  batch->state.instance_hit_by[idx] = 0u;
  batch->state.last_hit_by[idx] = 0u;
  batch->state.last_attack_landed[idx] = 0u;
  batch->state.attack_id[idx] = 0u;
  batch->state.attack_instance[idx] = 0u;
  batch->state.combo_count[idx] = 0u;
  batch->state.combo_timer_x2098[idx] = 0u;
  batch->state.combo_victim_port[idx] = 0xFFu;
  batch->state.combo_victim_instance_id[idx] = 0u;
  batch->state.grab_owner_port[idx] = 0xFFu;
  batch->state.match_flow_timer[idx] = 0u;
  batch->state.match_flow_pending_rebirth_char_id[idx] = 0u;
  for (size_t k = 0; k < (size_t)MSL_STATE_FLAGS_BYTES; k++) {
    batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES + k] = 0u;
  }
  msl_anim_timebase_seed(batch, idx, 0.0f, 0.0f);
}

static inline void enter_rebirth_wait(MslBatch* batch, size_t idx, uint32_t stage_id, int port0) {
  if (batch == NULL) {
    return;
  }
  MslStagePoint2 respawn = {0};
  if (!stage_collision_get_respawn_point(stage_id, port0, &respawn)) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBIRTH_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Same ordering bridge as Rebirth entry above (pre-tick lane -> first steady frame at 0).
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_Rebirth_Anim
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_Anim
  msl_anim_timebase_seed(batch, idx, -1.0f, 1.0f);
  batch->state.on_ground[idx] = 0;
  batch->state.pos_x[idx] = respawn.x;
  batch->state.pos_y[idx] = respawn.y;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  // RebirthWait timer is p_ftCommonData + 0x5D4; it can end early via IASA (inputs).
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_Anim /
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_IASA
  const MslCommonParams* c = msl_common_params();
  const int frames = (c != NULL) ? (int)c->rebirth_wait_timer_frames : 0;
  batch->state.match_flow_timer[idx] = (uint8_t)(frames > 0 ? (frames > 255 ? 255 : frames) : 0);
}

static inline int match_flow_respawn_port0(const MslBatch* batch, size_t idx, int fallback_port0) {
  if (batch == NULL) {
    return fallback_port0;
  }
  const uint8_t source_port0 = batch->state.source_port0[idx];
  return (source_port0 < (uint8_t)MSL_MAX_PLAYERS) ? (int)source_port0 : fallback_port0;
}

static inline void rebirth_wait_apply_exit_colanim(MslBatch* batch, size_t idx,
                                                   const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint16_t frames = c->colanim_rebirth_fall_x1994_frames;
  if (frames == 0u) {
    return;
  }
  // RebirthWait exits, including priority IASA exits into aerial specials, call
  // ftColl_8007B7A4(gobj, p_ftCommonData->x5D8) before the destination motion is visible.
  // This writes x1994/x198C, and Slippi reports x198C through `hurtbox_state` when x1988 is clear.
  // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_RebirthWait_IASA
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B7A4
  // data/common/ft_common_data.json: colanim_rebirth_fall_x1994_frames
  if (frames > batch->state.colanim_timer_x1994[idx]) {
    batch->state.colanim_timer_x1994[idx] = frames;
  }
  batch->state.colanim_hit_status_x198c[idx] =
      (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
}

static inline int dead_up_star_total_timer(const MslCommonParams* c) {
  if (c == NULL) {
    return 0;
  }
  int total = (int)c->dead_up_star_initial_frames;
  total += (int)c->dead_up_star_phase1_frames;
  total += (int)c->dead_up_star_phase2_frames;
  if (total < 0) {
    return 0;
  }
  return total > 255 ? 255 : total;
}

static inline int match_flow_clamp_timer_int(int total) {
  if (total < 0) {
    return 0;
  }
  return total > 255 ? 255 : total;
}

static inline int dead_up_fall_entry_total_timer(const MslCommonParams* c) {
  if (c == NULL) {
    return 0;
  }
  return match_flow_clamp_timer_int((int)c->dead_up_fall_entry_hold_frames +
                                    (int)c->dead_up_fall_lerp_frames);
}

static inline int dead_up_fall_hitcamera_total_timer(const MslCommonParams* c) {
  if (c == NULL) {
    return 0;
  }
  return match_flow_clamp_timer_int((int)c->dead_up_fall_hitcamera_hold_frames +
                                    (int)c->dead_up_fall_phase3_frames +
                                    (int)c->dead_up_fall_phase4_frames);
}

static inline uint16_t dead_up_fall_hitcamera_action(uint16_t from_action) {
  if (from_action == (uint16_t)MSL_ACT_DEAD_UP_FALL_ICE) {
    return (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_ICE;
  }
  return (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA;
}

static inline uint32_t dead_up_fall_hitcamera_submotion(uint16_t action_id) {
  if (action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA) {
    return (uint32_t)MSL_SM_DEAD_UP_FALL_HIT_CAMERA;
  }
  if (action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT) {
    return (uint32_t)MSL_SM_DEAD_UP_FALL_HIT_CAMERA_FLAT;
  }
  return MSL_ANIM_NONE_U32;
}

static inline void dead_up_fall_hidden_pose_init(MslBatch* batch, size_t idx,
                                                 const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  batch->state.dead_up_fall_offset_x[idx] = c->dead_up_fall_lerp_start_x;
  batch->state.dead_up_fall_offset_y[idx] = c->dead_up_fall_lerp_start_y;
  batch->state.dead_up_fall_offset_z[idx] = c->dead_up_fall_lerp_start_z;
  batch->state.dead_up_fall_vel_x[idx] = 0.0f;
  batch->state.dead_up_fall_vel_y[idx] = 0.0f;
  batch->state.dead_up_fall_vel_z[idx] = 0.0f;
}

static inline void enter_dead_up_fall(MslBatch* batch, size_t idx, const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // ftCo_800D4580 initializes DeadUpFall and its hidden pose scratch:
  //   x40=x524, x44=0, x50=*(Vec3*)&p_ftCommonData->x538.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3158,ftCo_800D4580}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DEAD_UP_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DAMAGE_FALL;
  batch->state.match_flow_timer[idx] = (uint8_t)dead_up_fall_entry_total_timer(c);
  batch->state.on_ground[idx] = 0u;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.hitlag[idx] = 0u;
  batch->state.hitstun[idx] = 0u;
  instance_id_reset_ft_800892D4(batch, idx);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_seed(batch, idx, 0.0f, 1.0f);
  dead_up_fall_hidden_pose_init(batch, idx, c);
}

static inline void enter_dead_up_fall_hitcamera(MslBatch* batch, size_t idx,
                                                const MslCommonParams* c, uint16_t from_action) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Source transition: ftCo_DeadUpFall_Anim case 1 calls ftCo_800D481C and selects
  // DeadUpFallHitCamera / DeadUpFallHitCameraIce from the current DeadUpFall variant.
  // The Flat variant depends on hidden scale.z; target Fox/Falco non-metal rows use the normal branch.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D481C}
  const uint16_t next_action = dead_up_fall_hitcamera_action(from_action);
  batch->state.action_id[idx] = next_action;
  batch->state.animation_index[idx] = dead_up_fall_hitcamera_submotion(next_action);
  batch->state.match_flow_timer[idx] = (uint8_t)dead_up_fall_hitcamera_total_timer(c);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_seed(batch, idx, -1.0f, 1.0f);
}

static inline void dead_up_fall_apply_lerp_pose(MslBatch* batch, size_t idx,
                                                const MslCommonParams* c, uint8_t t) {
  if (batch == NULL || c == NULL || c->dead_up_fall_lerp_frames == 0u) {
    return;
  }
  // ftCo_DeadUpFall_Anim case 0 sets x4C to 1/x528, then case 1 increments it once per frame.
  // ftCo_DeadUpFall_Phys case 1 lerps x538 -> x544 by that x4C owner. The shared countdown has
  // already been decremented for this frame, so x528-t+1 is the source x4C numerator.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_DeadUpFall_Phys}
  int elapsed = (int)c->dead_up_fall_lerp_frames - (int)t + 1;
  if (elapsed < 1) {
    elapsed = 1;
  }
  if (elapsed > (int)c->dead_up_fall_lerp_frames) {
    elapsed = (int)c->dead_up_fall_lerp_frames;
  }
  const float frac = (float)elapsed / (float)c->dead_up_fall_lerp_frames;
  batch->state.dead_up_fall_offset_x[idx] =
      c->dead_up_fall_lerp_start_x +
      (c->dead_up_fall_lerp_end_x - c->dead_up_fall_lerp_start_x) * frac;
  batch->state.dead_up_fall_offset_y[idx] =
      c->dead_up_fall_lerp_start_y +
      (c->dead_up_fall_lerp_end_y - c->dead_up_fall_lerp_start_y) * frac;
  batch->state.dead_up_fall_offset_z[idx] =
      c->dead_up_fall_lerp_start_z +
      (c->dead_up_fall_lerp_end_z - c->dead_up_fall_lerp_start_z) * frac;
}

static inline void dead_up_fall_apply_phase3_fall(MslBatch* batch, size_t idx,
                                                  const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // ftCo_DeadUpFall_Phys case 3 applies ftCommon_Fall with p_ftCommonData x554/x558 after
  // ftCo_DeadUpFall_Anim has optionally written self_vel from x550/x55C.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpFall_Phys
  float vy = batch->state.speed_y_self[idx] - c->dead_up_fall_phase3_gravity;
  const float terminal = c->dead_up_fall_phase3_terminal_vel;
  if (terminal > 0.0f && vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
  // ftCo_DeadUpFall_Phys case 3 accumulates self_vel into mv.co.unk_deadup.x5C, then adds x5C into
  // x50 and clears x5C once the ftAnim_80070FD0 release predicate allows it. The current Fox/Falco
  // no-ice runtime path has no separate XRotN comparison state, so carry the source velocity into
  // x50 on each visible phase-3 physics tick and clear the scratch.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpFall_Phys
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_80070FD0
  batch->state.dead_up_fall_vel_y[idx] += vy;
  batch->state.dead_up_fall_offset_y[idx] += batch->state.dead_up_fall_vel_y[idx];
  batch->state.dead_up_fall_vel_x[idx] = 0.0f;
  batch->state.dead_up_fall_vel_y[idx] = 0.0f;
  batch->state.dead_up_fall_vel_z[idx] = 0.0f;
}

static inline uint8_t top_blast_selects_dead_up_fall(MslBatch* batch, int bi, size_t idx,
                                                     const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  (void)idx;
  // ftCo_800D3158 consumes the global HSD RNG stream at the live top-blast callback. Match-init
  // rollouts own the continuous HSD stream; replay rollouts own the Slippi frame-start seed and any
  // prefix consumers this sim has modeled before match_flow_update_post_physics reaches this branch.
  // Do not phase this draw from future DeadUpStar/DeadUpFall labels.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  // refs/melee/src/melee/cm/camera.c::Camera_8003010C
  if (batch->rollout_clock_rng_owned == NULL) {
    return 0u;
  }
  const uint8_t rng_owner = batch->rollout_clock_rng_owned[bi];
  if (rng_owner != (uint8_t)MSL_ROLLOUT_CLOCK_HSD_RAND_STREAM &&
      rng_owner != (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED) {
    return 0u;
  }
  if (rng_owner == (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED) {
    if (batch->replay_rollout_seed_frame_id == NULL ||
        batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi]) {
      return 0u;
    }
  }
  const int32_t roll =
      combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_DEAD_UP_FALL_SELECT, 100) + 1;
  if (batch->camera_mode != NULL && batch->camera_mode[bi] == (uint8_t)MSL_CAMERA_MODE_FREE) {
    return 0u;
  }
  return (uint8_t)((int32_t)c->dead_up_fall_select_percent >= roll);
}

static inline void dead_up_star_try_enter_phase1(MslBatch* batch, size_t idx,
                                                 const MslCommonParams* c, uint8_t prev_t,
                                                 uint32_t stage_id) {
  if (batch == NULL || c == NULL || c->dead_up_star_phase1_frames == 0u) {
    return;
  }
  if ((int)prev_t != dead_up_star_total_timer(c)) {
    return;
  }
  MslStageBounds cam = {0};
  if (!stage_collision_get_cam_bounds_world(stage_id, &cam)) {
    return;
  }
  // DeadUpStar phase 0 -> 1 transition:
  //   fp->self_vel.y = (x514 * Stage_GetCamBoundsTopOffset() - fp->cur_pos.y) / x508
  //   fp->self_vel.z = x510 / x508
  // This simulator has no self_vel.z lane, but Y is rollout-visible through position integration.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
  // data/common/ft_common_data.json:{dead_up_star_phase1_frames,dead_up_star_phase1_cam_top_mul}
  // data/stages/final_destination.json:cam_bounds_world.top
  const float frames = (float)c->dead_up_star_phase1_frames;
  batch->state.speed_y_self[idx] =
      (c->dead_up_star_phase1_cam_top_mul * cam.top - batch->state.pos_y[idx]) / frames;
}

void match_flow_update_pre_anim(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];

      if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
          batch->state.match_flow_pending_rebirth_char_id[idx] != 0u &&
          a == (uint16_t)MSL_ACT_DEAD_DOWN) {
        const uint8_t pending_char = batch->state.match_flow_pending_rebirth_char_id[idx];
        uint8_t t = batch->state.match_flow_timer[idx];
        if (t > 0u) {
          t--;
        }
        if (t == 0u) {
          // Team stock-share / pending-Rebirth rows can be replay-visible zeroed DeadDown slots
          // until the source countdown reaches Rebirth. Restore the hidden fighter kind only at
          // the actual transition.
          // refs/melee/src/melee/gm/gm_16AE.c
          // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D4FF4
          batch->state.char_id[idx] = pending_char;
          batch->state.stocks[idx] = 1u;
          enter_rebirth(batch, idx, c, stage_id, match_flow_respawn_port0(batch, idx, p));
        } else {
          enter_eliminated_dead_slot(batch, idx);
          batch->state.match_flow_timer[idx] = t;
          batch->state.match_flow_pending_rebirth_char_id[idx] = pending_char;
        }
        continue;
      }

      if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
          batch->state.match_flow_pending_rebirth_char_id[idx] == 0u &&
          a == (uint16_t)MSL_ACT_DEAD_DOWN) {
        enter_eliminated_dead_slot(batch, idx);
        continue;
      }

      if (batch->state.entry_end_fall_lock[idx] != 0u &&
          (a != (uint16_t)MSL_ACT_FALL || batch->state.on_ground[idx] != 0u)) {
        batch->state.entry_end_fall_lock[idx] = 0u;
      }

      if (!(match_flow_is_dead_action(a) || match_flow_is_respawn_action(a) ||
            match_flow_is_entry_action(a))) {
        continue;
      }

      // Freeze timebase for suite-observed "no submotion" match-flow states.
      // (Entry, EntryEnd, and most Dead* are observed with animation_index=0xFFFFFFFF.)
      if (a == (uint16_t)MSL_ACT_ENTRY || a == (uint16_t)MSL_ACT_ENTRY_END ||
          a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
          a == (uint16_t)MSL_ACT_DEAD_RIGHT) {
        freeze_no_submotion_timebase(batch, idx);
      }
      if (a == (uint16_t)MSL_ACT_ENTRY_END) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }
      if (a == (uint16_t)MSL_ACT_ENTRY) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }
      if (a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
          a == (uint16_t)MSL_ACT_DEAD_RIGHT) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }

      // Common invariants for match-flow states (suite-observed).
      batch->state.on_ground[idx] = 0;

      uint8_t prev_t = batch->state.match_flow_timer[idx];
      uint8_t t = prev_t;
      if (t > 0) {
        t--;
      }
      batch->state.match_flow_timer[idx] = t;

      // Entry position updates (timer-driven).
      if (a == (uint16_t)MSL_ACT_ENTRY_START || a == (uint16_t)MSL_ACT_ENTRY_END) {
        const int entry_start_frames = (int)c->entry_start_frames;
        const float x20 = entry_x20(batch, idx);
        if (a == (uint16_t)MSL_ACT_ENTRY_START) {
          // Update to the next-frame position using the decremented timer (t).
          const float base_y = entry_base_y_from_entry_start(batch->state.pos_y[idx], x20,
                                                             (uint8_t)(t + 1), entry_start_frames);
          if (t == 0) {
            enter_entry_end(batch, idx, c, base_y, x20);
          } else if (entry_start_frames > 0) {
            const float denom = (float)entry_start_frames;
            const float frac = (denom - (float)t) / denom;
            batch->state.pos_y[idx] = base_y + x20 * frac;
          }
        } else {
          // EntryEnd keeps animation frozen; update position while the timer is active.
          const float base_y = entry_base_y_from_entry_end(batch->state.pos_y[idx], x20,
                                                           (uint8_t)(t + 1), entry_start_frames);
          if (t == 0) {
            // Transition to Fall; preserve the current EntryEnd position so Fall physics can take
            // over from the last EntryEnd pose.
            enter_fall(batch, idx);
            // Hidden EntryEnd -> Fall control lock:
            // - EntryEnd timer expiry transitions through ftCommon_8007D92C -> ftCo_Fall_Enter.
            // - Controlled vanilla playback of the opening EntryEnd descent keeps ordinary Fall
            //   aerial IASA + common air drift suppressed until landing, which is not represented
            //   in public post-frame state.
            // Promote that hidden ownership as a dedicated seed/runtime bit.
            // refs/melee/src/melee/ft/ft_0C31.c::{ftCo_EntryEnd_Anim,ftCo_EntryEnd_IASA}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_IASA,ftCo_Fall_Phys}
            batch->state.entry_end_fall_lock[idx] = 1u;
            // EntryEnd->Fall transition is evaluated here before the shared anim-timebase advance;
            // in decomp this state transition is callback-owned (prio 1), so preserve the first Fall
            // post-frame at action_frame=0 by seeding the pre-tick lane.
            // refs/melee/src/melee/ft/ft_0C31.c::ftCo_EntryEnd_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim
            msl_anim_timebase_seed(batch, idx, -1.0f, 1.0f);
            batch->state.match_flow_timer[idx] = 0;
          } else if (entry_start_frames > 0) {
            const float denom = (float)entry_start_frames;
            const float frac = (float)t / denom;
            batch->state.pos_y[idx] = base_y + x20 * frac;
          }
        }
      } else if (a == (uint16_t)MSL_ACT_ENTRY) {
        // Entry -> EntryStart on timer expiry. Entry keeps the fighter invisible and stationary.
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        if (t == 0) {
          // Enter EntryStart; apply its first-frame position update immediately.
          enter_entry_start(batch, idx, c);
        }
      } else if (a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
                 a == (uint16_t)MSL_ACT_DEAD_RIGHT || a == (uint16_t)MSL_ACT_DEAD_UP_STAR ||
                 match_flow_is_dead_up_fall_entry_action(a) ||
                 match_flow_is_dead_up_fall_hitcamera_action(a)) {
        if (a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          // DeadUpStar stock loss is delayed until late in the animation.
          // Assembly shows the stock-loss event happens when the internal phase-1 timer expires,
          // leaving `dead_up_star_phase2_frames` frames remaining in the DeadUpStar motion.
          // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
          const int phase2 = (int)c->dead_up_star_phase2_frames;
          if (phase2 > 0 && (int)prev_t == phase2 + 1) {
            // DeadUpStar phase 1 -> 2 transition:
            // ftCo_DeadUpStar_Anim calls ftCommon_8007E2FC before the stock-loss/dead-flow side
            // effects, freezing the visible top-blast pose instead of carrying the phase-1
            // vertical velocity for one more post-frame.
            // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
            match_flow_zero_common_velocities(batch, idx);
            if (batch->state.stocks[idx] > 0) {
              batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
            }
          }
        } else if (match_flow_is_dead_up_fall_hitcamera_action(a)) {
          const int phase4 = (int)c->dead_up_fall_phase4_frames;
          const int phase3_phase4 =
              (int)c->dead_up_fall_phase3_frames + (int)c->dead_up_fall_phase4_frames;
          if (phase3_phase4 > 0 && (int)t == phase3_phase4) {
            // ftCo_DeadUpFall_Anim case 2 writes self_vel before same-frame Phys.
            // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpFall_Anim
            batch->state.speed_y_self[idx] = c->dead_up_fall_initial_self_vel_y;
          }
          if ((int)t > phase4 && (int)t <= phase3_phase4) {
            dead_up_fall_apply_phase3_fall(batch, idx, c);
          }
          if (phase4 > 0 && (int)prev_t == phase4 + 1) {
            // ftCo_DeadUpFall_Anim case 3 performs the stock-loss side effect at phase-3 expiry.
            // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D34E0}
            if (batch->state.stocks[idx] > 0) {
              batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
            }
          }
        }
        // Death -> Rebirth.
        if (a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          dead_up_star_try_enter_phase1(batch, idx, c, prev_t, stage_id);
        } else if (match_flow_is_dead_up_fall_entry_action(a)) {
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
          if (t <= c->dead_up_fall_lerp_frames) {
            dead_up_fall_apply_lerp_pose(batch, idx, c, t);
          }
          if (t == 0) {
            enter_dead_up_fall_hitcamera(batch, idx, c, a);
          }
        } else if (match_flow_is_dead_up_fall_hitcamera_action(a)) {
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
        } else {
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
        }
        if (t == 0 && !match_flow_is_dead_up_fall_entry_action(a)) {
          if (batch->state.stocks[idx] == 0u) {
            const uint8_t pending_char = batch->state.match_flow_pending_rebirth_char_id[idx];
            if (pending_char != 0u) {
              // Teams stock-share / pending-Rebirth reconstruction:
              // Slippi can serialize the inter-stock DeadDown slot as char_id=0/stocks=0 while
              // the source match-flow timer is still counting down to Rebirth. The hidden owner is
              // the player/fighter kind selected by gm_16AE's stock-share respawn path, not the
              // zeroed replay-visible slot. Restore it only at the actual Rebirth transition so
              // earlier DeadDown rows remain replay-visible zeroed slots.
              // refs/melee/src/melee/gm/gm_16AE.c
              // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D4FF4
              batch->state.char_id[idx] = pending_char;
              batch->state.stocks[idx] = 1u;
              enter_rebirth(batch, idx, c, stage_id, match_flow_respawn_port0(batch, idx, p));
            } else {
              enter_eliminated_dead_slot(batch, idx);
            }
          } else {
            enter_rebirth(batch, idx, c, stage_id, match_flow_respawn_port0(batch, idx, p));
          }
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH) {
        // Rebirth -> RebirthWait.
        if (t == 0) {
          enter_rebirth_wait(batch, idx, stage_id, match_flow_respawn_port0(batch, idx, p));
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH_WAIT) {
        // RebirthWait -> Fall.
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        if (t == 0) {
          rebirth_wait_apply_exit_colanim(batch, idx, c);
          enter_fall(batch, idx);
          batch->state.match_flow_timer[idx] = 0;
        }
      }
    }
  }
}

void match_flow_update_post_anim(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ENTRY_START) {
        continue;
      }
      // EntryStart animation is short; state remains active beyond the submotion end. Clamp the
      // timebase at end_frame and freeze rate to match Slippi state_age/action_frame behavior.
      const float end_frame =
          msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_ENTRY_START);
      if (end_frame > 0.0f && batch->state.anim_frame_f32[idx] >= end_frame) {
        msl_anim_timebase_seed(batch, idx, end_frame, 0.0f);
      }
    }
  }
}

void match_flow_update_post_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBIRTH_WAIT) {
        continue;
      }

      // RebirthWait can exit early via IASA (inputs) long before its timer expires (x2340).
      // Suite note: RebirthWait runs only a few frames in many replays.
      //
      // Decomp pointers:
      // - Timer decrement -> Fall when x2340 reaches 0:
      //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_Anim
      // - Early exit decision logic:
      //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_IASA
      //
      // We only model the *Fall-enter* subset of the IASA chain here (not the full state machine
      // of Specials/Attacks/Jump/etc while on the angel platform). The key is to avoid treating
      // low analog trigger noise (common in Slippi inputs) or small stick drift as an immediate
      // "exit RebirthWait" signal.
      //
      // Decomp anchors for the Fall-enter subset:
      // - Shield held: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A2C
      // - Taunt (D-Pad Up): refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_800DE9B8
      // - Crouch input: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::fn_800D5F84
      // - Turn input: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_800C97A8
      // - Walk input: refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFC70
      const uint16_t buttons = batch->state.input_buttons[idx];
      const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
      const uint8_t l = batch->state.input_l[idx];
      const uint8_t r = batch->state.input_r[idx];
      const float stick_x =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);

      const float trig_unit = msl_trigger_unit_from_input(buttons, l, r);
      const uint8_t shield_held = (uint8_t)(trig_unit >= c->trigger_deadzone);

      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      const float stick_f = stick_x * facing_dir;

      const uint8_t want_fall =
          (uint8_t)(shield_held || ((buttons_pressed & (uint16_t)MSL_BUTTON_D_UP) != 0) ||
                    (stick_y < -c->crouch_stick_threshold) ||
                    (stick_f <= c->turn_stick_x_threshold) || (stick_f >= c->walk_stick_threshold));

      if (!want_fall) {
        // RebirthWait priority IASA can still enter aerial specials before the fallback Fall
        // branches. The common helper checks SpecialAir before Fall/guard/walk-like exits.
        // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_RebirthWait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
        if (blaster_try_enter_air_from_iasa_subset(batch, c, idx) != 0u) {
          rebirth_wait_apply_exit_colanim(batch, idx, c);
          batch->state.match_flow_timer[idx] = 0;
        }
        continue;
      }

      if (blaster_try_enter_air_from_iasa_subset(batch, c, idx) != 0u) {
        rebirth_wait_apply_exit_colanim(batch, idx, c);
        batch->state.match_flow_timer[idx] = 0;
        continue;
      }

      rebirth_wait_apply_exit_colanim(batch, idx, c);
      enter_fall(batch, idx);
      batch->state.match_flow_timer[idx] = 0;
    }
  }
}

static inline uint8_t match_flow_should_check_blastzone(uint16_t a) {
  return (uint8_t)(!(match_flow_is_dead_action(a) || match_flow_is_respawn_action(a) ||
                     match_flow_is_entry_action(a)));
}

uint8_t match_flow_should_stage_collide(uint16_t action_id) {
  // Decomp shape: match-flow actions use motion-state-specific (or NULL) collision callbacks, not
  // the normal grounded/air collision pass.
  //
  // - Dead* motion states have coll_cb=NULL, so Fighter_procMap does not stage-collide them.
  //   refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_DeadDown et al.)
  // - Entry has an empty coll_cb.
  //   refs/melee/src/melee/ft/ft_0C31.c::ftCo_Entry_Coll
  // - Rebirth/EntryStart/EntryEnd run dedicated mpColl/ECB collision entrypoints.
  //   refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Coll
  //   refs/melee/src/melee/ft/ft_0C31.c::ftCo_EntryStart_Coll
  //
  // Until those match-flow collision paths (respawn platform, special ECB gates) are modeled, skip
  // the generic stage collision pass for these actions to avoid double-colliding.
  return (uint8_t)(!(match_flow_is_dead_action(action_id) ||
                     match_flow_is_respawn_action(action_id) ||
                     match_flow_is_entry_action(action_id)));
}

void match_flow_update_post_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    MslStageBounds blast = {0};
    if (!stage_collision_get_blast_bounds_world(stage_id, &blast)) {
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (!match_flow_should_check_blastzone(a)) {
        continue;
      }
      const uint8_t stocks = batch->state.stocks[idx];
      if (stocks == 0) {
        continue;
      }

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];

      uint16_t death = 0xFFFFu;
      if (x > blast.right) {
        death = (uint16_t)MSL_ACT_DEAD_RIGHT;
      } else if (x < blast.left) {
        death = (uint16_t)MSL_ACT_DEAD_LEFT;
      } else if (y > blast.top) {
        // Decomp: top KO requires being grounded, x2222_b3, or sufficient upward KB velocity. The
        // lite sim does not currently expose x2222_b3, so the supported Fox/Falco path uses
        // ground/KB lanes. Once admitted, normal camera mode consumes HSD_Randi(100)+1 and selects
        // DeadUpFall when p_ftCommonData->x520 is at least the roll; camera-free mode forces
        // DeadUpStar. Camera_8003010C is live CObj/camera-mode state, so replay reseeds do not
        // approximate it from camera bounds.
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
        const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
        if (on_ground || (batch->state.speed_y_attack[idx] > c->dead_up_kb_vel_threshold)) {
          if (top_blast_selects_dead_up_fall(batch, bi, idx, c)) {
            death = (uint16_t)MSL_ACT_DEAD_UP_FALL;
          } else {
            death = (uint16_t)MSL_ACT_DEAD_UP_STAR;
          }
        }
      } else if (y < blast.bottom) {
        death = (uint16_t)MSL_ACT_DEAD_DOWN;
      }

      if (death == 0xFFFFu) {
        continue;
      }

      // Enter Dead*:
      // - Decrement stock once (except DeadUpStar, which delays stock loss).
      // - Freeze motion/physics.
      // Decomp pointers:
      // - Blastzone check: refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
      // - Stock loss: refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D34E0
      if (death != (uint16_t)MSL_ACT_DEAD_UP_STAR && death != (uint16_t)MSL_ACT_DEAD_UP_FALL) {
        if (batch->state.stocks[idx] > 0) {
          batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
        }
      }

      if (death == (uint16_t)MSL_ACT_DEAD_UP_FALL) {
        enter_dead_up_fall(batch, idx, c);
        continue;
      }

      batch->state.action_id[idx] = death;
      batch->state.on_ground[idx] = 0;
      batch->state.speed_air_x_self[idx] = 0.0f;
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.speed_y_self[idx] = 0.0f;
      batch->state.speed_x_attack[idx] = 0.0f;
      batch->state.speed_y_attack[idx] = 0.0f;
      batch->state.hitlag[idx] = 0;
      batch->state.hitstun[idx] = 0;

      if (death == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_DAMAGE_FALL;
        // Decomp: death processing calls ft_800892D4(fp), which clears fp->x2088 (instance_id) to 0,
        // but motion-state entry then runs ft_800895E0 which may bump it back to a non-zero value.
        // Do this on Dead* entry, not continuously during the Dead* action, so post-frame instance_id
        // matches Slippi (and the decomp-shaped motion-state entry bundle owns the bump logic).
        // refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354
        // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
        instance_id_reset_ft_800892D4(batch, idx);
        // Decomp: motion-state entry uses Fighter_ChangeMotionState (identity updates + timebase reset).
        // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        // DeadUpStar is observed with action_frame starting at 0 and advancing; override timebase to 0.
        msl_anim_timebase_seed(batch, idx, 0.0f, 1.0f);
        // DeadUpStar enter initializes an internal timer from p_ftCommonData->x504, then runs two
        // phases (x508/x50C). Model this with a single countdown.
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D40B8
        batch->state.match_flow_timer[idx] = (uint8_t)dead_up_star_total_timer(c);
      } else {
        // See note in the DeadUpStar branch above: clear instance_id once on death entry.
        instance_id_reset_ft_800892D4(batch, idx);
        // Decomp: motion-state entry uses Fighter_ChangeMotionState (identity updates + timebase reset).
        // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        // DeadDown/Left/Right are observed with no submotion and frozen state_age=-1.
        freeze_no_submotion_timebase(batch, idx);
        batch->state.match_flow_timer[idx] =
            (uint8_t)(c->dead_timer_frames > 255 ? 255 : c->dead_timer_frames);
      }
    }
  }
}
enum { MSL_SIM_INIT_OPENING_INPUT_LOCK_TIMER = 83 };

uint8_t match_flow_sim_init_opening_input_lock_timer(void) {
  // Live init-match helper only:
  // - replay-seeded teacher-forced/rollout paths seed the same owner through
  //   MslSeed::opening_input_lock_timer,
  // - this helper exists only to bridge fresh init_match episodes onto the decomp-backed owner.
  // Real opening-control owner:
  // - Fighter init sets fp->x221D_b4 via ftLib_800867E8.
  // - Fighter_procUpdate blanks current input lanes while x221D_b4 remains set.
  // - VS opening schedules fn_8016B7F8 as the ScInfCnt status-overlay completion callback, and
  //   that callback clears x221D_b4 for all fighters via ftLib_800868A4.
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800867E8,ftLib_800868A4}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
  // refs/melee/src/melee/gm/gm_16AE.c::{gm_8016E934_OnEnter,fn_8016B7F8}
  // refs/melee/src/melee/if/ifstatus.c::ifStatus_802F6EA4
  // refs/melee/src/melee/if/if_2F72.c::if_802F73C4
  //
  // Timing source:
  // - VS uses status type 3, backed by IfAll.dat::ScInfCnt_scene_models[3].
  // - That model's joint/material AObj end frame is 85.0.
  // - With the standard live init-match opening seed aligned to raw frame -122, the callback
  //   clears x221D_b4 before processing raw -39 inputs, i.e. after 83 remaining locked steps.
  // refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
  return (uint8_t)MSL_SIM_INIT_OPENING_INPUT_LOCK_TIMER;
}
