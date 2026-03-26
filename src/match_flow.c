#include "match_flow.h"

#include "action_ids.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "attack_identity.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "instance_id.h"
#include "stage_collision.h"
#include "trigger_input.h"

enum { MSL_ANIM_NONE_U32 = 0xFFFFFFFFu };

static inline uint8_t match_flow_is_dead_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
    case MSL_ACT_DEAD_UP_STAR:
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
  batch->state.tilt_timer_y[idx] = 0;
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
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
  attack_identity_reset_ft_800890BC(batch, idx);
  match_flow_identity_reset_Fighter_UnkInitReset_80067C98_subset(batch, idx);
  instance_id_reset_ft_800892D4(batch, idx);
  // Seed-bridge (decomp call-chain parity):
  // - Rebirth entry runs Fighter_UnkProcessDeath_80068354 before Fighter_ChangeMotionState(Rebirth).
  // - That call chain includes unmodeled internals (ftCo_800BFFAC / ftCo_800C0074 / ...), and
  //   suite-observed Dead* -> Rebirth transitions show one hidden plAttack_80037B08 consumption
  //   before the fighter's own ft_800895E0 write.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_800D4FF4
  // refs/melee/build/GALE01/asm/melee/ft/fighter.s::Fighter_UnkProcessDeath_80068354
  instance_id_counter_consume_plAttack_80037B08(batch, idx);

  // Decomp: respawn starts at camera top (world) and falls to the spawn platform.
  // refs/melee/src/melee/gr/stage.c::Stage_GetCamBoundsTopOffset
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
  // Current extracted stage contract does not expose the per-port facing table directly, but on
  // Final Destination the respawn points face toward the stage camera midpoint. Reconstruct that
  // decomp-owned spawn-facing from the extracted camera bounds already loaded for this stage.
  // data/stages/final_destination.json: cam_bounds_world, respawn_points
  {
    const float cam_mid_x = 0.5f * (cam.left + cam.right);
    batch->state.facing[idx] = (uint8_t)(respawn.x <= cam_mid_x ? 1u : 0u);
  }
  batch->state.pos_x[idx] = respawn.x;
  batch->state.pos_y[idx] = cam.top;

  // Rebirth fall speed is stage-derived: linear fall from camera top to platform Y over the
  // Rebirth timer length (p_ftCommonData + 0x5D0).
  // Decomp: refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (Rebirth enter sets fp->x2340 from 0x5D0)
  const int frames = (int)c->rebirth_timer_frames;
  float vy = 0.0f;
  if (frames > 0) {
    vy = (respawn.y - cam.top) / (float)frames;
  }
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = vy;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.match_flow_timer[idx] = (uint8_t)(frames > 0 ? (frames > 255 ? 255 : frames) : 0);
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
                 a == (uint16_t)MSL_ACT_DEAD_RIGHT || a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        if (a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          // DeadUpStar stock loss is delayed until late in the animation.
          // Assembly shows the stock-loss event happens when the internal phase-1 timer expires,
          // leaving `dead_up_star_phase2_frames` frames remaining in the DeadUpStar motion.
          // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
          const int phase2 = (int)c->dead_up_star_phase2_frames;
          if (phase2 > 0 && (int)prev_t == phase2 + 1) {
            if (batch->state.stocks[idx] > 0) {
              batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
            }
          }
        }
        // Death -> Rebirth.
        if (a != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
        }
        if (t == 0) {
          enter_rebirth(batch, idx, c, stage_id, p);
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH) {
        // Rebirth -> RebirthWait.
        if (t == 0) {
          enter_rebirth_wait(batch, idx, stage_id, p);
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH_WAIT) {
        // RebirthWait -> Fall.
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        if (t == 0) {
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
        continue;
      }

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
        // Decomp: top KO requires being grounded, or a sufficient upward KB velocity.
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
        const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
        if (on_ground || (batch->state.speed_y_attack[idx] > c->dead_up_kb_vel_threshold)) {
          death = (uint16_t)MSL_ACT_DEAD_UP_STAR;
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
      if (death != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        if (batch->state.stocks[idx] > 0) {
          batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
        }
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
        const int phase1 = (int)c->dead_up_star_phase1_frames;
        const int phase2 = (int)c->dead_up_star_phase2_frames;
        int total = (int)c->dead_up_star_initial_frames;
        if (phase1 > 0) {
          total += phase1;
        }
        if (phase2 > 0) {
          total += phase2;
        }
        if (total < 0) {
          total = 0;
        }
        if (total > 255) {
          total = 255;
        }
        batch->state.match_flow_timer[idx] = (uint8_t)total;
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
