#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api.h"
#include "hitlist_types.h"

// Hot SoA state owned by a batch. All arrays are sized for MAX_PLAYERS/ITEMS.
typedef struct MslStateSoA {
  // Meta
  int32_t* frame_id;
  uint32_t* frame_pre_random_seed;
  uint32_t* stage_id;  // [batch]
  // Match-start fighter input lock (`fp->x221D_b4`) countdown, one per environment.
  //
  // Decomp / asset anchors:
  // - Fighter init sets x221D_b4 via ftLib_800867E8, and Fighter_procUpdate blanks current input
  //   lanes while that bit remains set.
  // - VS opening clears x221D_b4 for all fighters from the ScInfCnt status-overlay completion
  //   callback (gm_16AE.c::fn_8016B7F8), scheduled by ifStatus_802F6EA4(3, ...).
  // - The VS overlay uses IfAll.dat::ScInfCnt_scene_models[3]; its joint/material AObj end frame
  //   is 85.0, which maps to 83 remaining locked simulation steps from the standard raw -122
  //   opening seed until the callback clears the lock before processing raw -39 inputs.
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800867E8,ftLib_800868A4}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
  // refs/melee/src/melee/gm/gm_16AE.c::{gm_8016E934_OnEnter,fn_8016B7F8}
  // refs/melee/src/melee/if/ifstatus.c::ifStatus_802F6EA4
  // refs/melee/src/melee/if/if_2F72.c::if_802F73C4
  // refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
  uint8_t* opening_input_lock_timer;  // [batch]
  // Global stale-attack-instance counter (decomp: plStale_IncrementAttackInstance).
  // One per environment in the batch (per-match global counter).
  // refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance
  uint16_t* stale_attack_instance_counter;  // [batch]
  // Global action-state instance_id counter (decomp: plAttack_80037B08 uses unk_804D6480).
  // One per environment in the batch (per-match global counter).
  // refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
  uint16_t* instance_id_counter;  // [batch]
  float* match_damage_ratio;      // [batch] (decomp: gm_8016B248 -> StartMeleeRules.x30)
  uint8_t* is_teams;              // [batch]
  uint8_t* team_id;               // [batch * MSL_MAX_PLAYERS]
  uint8_t* char_id;               // [batch * MSL_MAX_PLAYERS]
  float* attack_ratio;            // [batch * MSL_MAX_PLAYERS] (decomp: Player_GetAttackRatio)
  float* defense_ratio;           // [batch * MSL_MAX_PLAYERS] (decomp: Player_GetDefenseRatio)

  // Kinematics
  float* pos_x;
  float* pos_y;
  float* pos_z;
  float* prev_pos_x;  // Position at start of current frame (pre-integration).
  float* prev_pos_y;  // Position at start of current frame (pre-integration).
  // Frame-start Y snapshot for mpColl floor sweeps. Kept separate from prev_pos_* because
  // existing grounded rollback helpers use prev_pos_* as a pre-physics integration snapshot.
  float* floor_sweep_prev_pos_y;
  // Collision-stage prev/cur position snapshots used for mpColl-shaped ledge-grab AABB checks.
  //
  // Decomp: the ledge-grab block consumes CollData.prev_pos / CollData.cur_pos as managed inside
  // mpColl_80043754's collision substep loop ("previous substep", not "previous frame").
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  //
  // This simulator does not yet substep collision. We approximate a single collision "substep" by
  // capturing:
  // - coll_stage_prev_pos: fighter position immediately before stage_collision_apply() this frame
  // - coll_stage_cur_pos: fighter position immediately after stage_collision_apply() this frame
  float* coll_stage_prev_pos_x;
  float* coll_stage_prev_pos_y;
  float* coll_stage_cur_pos_x;
  float* coll_stage_cur_pos_y;
  float* speed_air_x_self;
  float* speed_ground_x_self;
  float* speed_y_self;
  float* speed_x_attack;
  float* speed_y_attack;
  // Fighter model scale (decomp: fp->x34_scale.y). This is an external multiplier applied to
  // various collision/visual calculations; default is 1.0 in normal matches.
  float* fighter_scale_y;
  uint8_t* facing;
  // Motion-state facing lane (decomp: fp->facing_dir1).
  int8_t* facing_dir1;
  // Grounded knockback friction multiplier lane (decomp: ft_GetGroundFrictionMultiplier(fp)).
  float* ground_friction_mul;
  // Smash charge lane (decomp: fp->smash_attrs.state == SmashState_Charging).
  uint8_t* kb_smashcharge_active;
  // Live grounded-smash charge internals (`fp->smash_attrs`) for current-sim / rollout ownership.
  //
  // Decomp:
  // - opcode 56 / ftAction_80073008 seeds SmashState_PreCharge via ftCo_800DEE84.
  // - the later fighter input proc (ftCo_800DF0D0) promotes PreCharge -> Charging on held A and
  //   restores the saved anim rate on release.
  // - the anim proc (ftCo_800DEF38) advances the charging frame counter and auto-releases at the
  //   script-provided hold limit.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
  // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DEF38,ftCo_800DF0D0}
  uint8_t* smash_charge_state;                 // 0=None, 1=PreCharge, 2=Charging
  uint8_t* smash_charge_frames;                // elapsed Charging frames
  uint8_t* smash_charge_hold_frames_max;       // ftCo_800DEE84 arg2 / x211C_holdFrame
  int32_t* smash_charge_saved_rate_fp_q16_16;  // x2124_frameSpeedMul
  uint8_t* on_ground;
  uint8_t* frame_start_on_ground;  // on_ground value captured before current-frame callbacks.
  uint8_t* prev_on_ground;         // on_ground value before stage_collision_apply().
  // Collision contact metadata owned by mpColl ground contact substrate.
  float* ground_contact_x;
  float* ground_contact_y;
  float* ground_normal_x;
  float* ground_normal_y;
  // Collision contact metadata owned by mpColl wall substrate (FD-only v1).
  // - wall_kind: 0 = none, 1 = left_wall, 2 = right_wall (mplib/mpColl naming).
  float* wall_contact_x;
  float* wall_contact_y;
  float* wall_normal_x;
  float* wall_normal_y;
  uint16_t* wall_id;   // ISO-derived segment index (stable id).
  uint8_t* wall_kind;  // 0/1/2
  // Collision contact metadata owned by mpColl ceiling substrate (FD-only v1).
  float* ceiling_contact_x;
  float* ceiling_contact_y;
  float* ceiling_normal_x;
  float* ceiling_normal_y;
  uint16_t* ceiling_id;  // ISO-derived segment index (stable id).
  uint32_t* coll_env_flags;
  uint32_t* coll_prev_env_flags;

  // State machine
  uint16_t* action_id;
  // Replay-true previous action snapshot (t-1 -> t), seeded from dataset history for entry-shaped
  // one-step rows. Separate from the runtime cache below.
  uint16_t* seed_prev_action_id;
  int16_t* seed_prev_action_frame;
  // Previous frame's action_id (captured at step start; internal-only).
  // Used for transition-based mechanics that depend on (t -> t+1) action changes while keeping
  // the seed schema minimal for one-step reseeding.
  uint16_t* prev_action_id;
  // Previous frame's action_frame (captured at step start; internal-only).
  int16_t* prev_action_frame;
  int16_t* action_frame;
  // Throw projectile pulse-consume seed lane (decomp owner: ftFx_Throw_Anim consumes
  // ftAction throw_flags_b0 one-shot pulses).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  uint8_t* throw_pulse_consumed;
  // Throw projectile pulse crossing lane from the previous replay step (0 = none).
  // Producer is strictly causal in tools/slippi/make_dataset_from_slp.py.
  uint8_t* throw_pulse_crossed_prev_frame;
  // Source-owner clear countdown (`fp->dmg.x18C8`) with +1 bias.
  //
  // Decomp:
  // - Fighter_ChangeMotionState seeds x18C8 from p_ftCommonData->x814 under grounded + x9_b1.
  // - Fighter_8006A360 decrements x18C8 under !hitlag and clears x18C4_source_ply at expiry.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
  // refs/melee/src/melee/ft/types.h::MotionState (x9_b1)
  uint8_t* source_clear_timer_x18c8;
  // Source-owner set phase lane for active x18C8 runs (causal seed lane).
  // 0: active run has no observed source-owner set edge backing.
  // 1: active run is backed by source-owner set edge context (6 -> owner).
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  uint8_t* source_clear_owner_set_phase;
  // One-step hidden ProcessHit damage-pending source-owner clear bridge.
  // 0: no ProcessHit-owned clear override.
  // 1: consume source-owner clear before x18C8 decrement for this one-step row.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  uint8_t* source_clear_processhit_damage_pending_phase;
  // One-step hidden pre-gate Fighter_8006CDA4 RNG-consume bridge for DamageFlyRoll entry.
  // 0: no seeded pre-gate consume ownership.
  // 1: consume one pre-gate HSD_Randi before ftCo_8008DCE0 block_33.
  // 2: consume two pre-gate HSD_Randi calls before ftCo_8008DCE0 block_33.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  uint8_t* damageflyroll_fighter_8006cda4_phase_hint;
  // Grounded source-owner clear phase bridge (`ftCommon_800804FC` path).
  // 0: no grounded clear-phase override.
  // 1: consume grounded clear before x18C8 decrement for this one-step row.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  uint8_t* source_clear_grounded_damage_clear_phase;
  // Terminal clear-phase bridge for source-owner identity (`dmg.x18C4_source_ply`) on
  // `source_clear_timer_x18c8 == 1` rows. One-step transient lane produced in dataset tooling.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  uint8_t* source_clear_terminal_phase;
  // Internal-only throw flow latch: when a throw release flag fires, we detach the victim during
  // motion-state Anim (pre-physics), then optionally apply the throw hit later in the frame
  // (post-items) if no other hit interrupted the victim.
  //
  // Stored on the thrower (not the victim) so we can clear it by iterating over players each frame.
  // Value: 0xFF = none, else victim port in [0..MSL_MAX_PLAYERS).
  uint8_t* throw_pending_victim_port;
  // Internal-only throw flow latch: the throw hitbox idx to apply when throw_pending_victim_port is
  // set. Value: 0xFF = none, else hitbox idx in [0..MSL_THROW_HITBOX_IDX_MAX).
  uint8_t* throw_pending_hit_idx;
  // Grab/throw victim attachment internals.
  //
  // Decomp:
  // - Thrown victims update position each frame from an attachment joint plus fp->x1A70 offsets
  //   (ftCo_Thrown.c::ftCo_800DE508).
  // - The "who is the thrower/grab-owner" identity is represented by fp->victim_gobj.
  //
  // Seed representation:
  // - grab_owner_port is seeded from replay data as a player-slot index in [0..3], 0xFF = none.
  //
  // Simulator representation:
  // - grab_offset_{y,z} store the decomp-shaped fp->x1A70.{y,z} (unscaled) inferred at reseed-time.
  uint8_t* grab_owner_port;        // [batch * players]
  int8_t* grab_mash_stick_x_sign;  // [batch * players] fp->x1A50
  int8_t* grab_mash_stick_y_sign;  // [batch * players] fp->x1A51
  float* grab_offset_y;            // [batch * players]
  float* grab_offset_z;            // [batch * players]
  uint8_t* match_flow_timer;
  // Legacy compatibility lane from the earlier EntryEnd->Fall investigation.
  // The authoritative opening-control owner is now opening_input_lock_timer (`fp->x221D_b4`).
  uint8_t* entry_end_fall_lock;
  // Rebirth / dead-flow camera-box visibility (`fp->x221F_b0`) promoted as a named SoA lane for
  // future F04 ownership fixes. Seeded from replay-visible `state_flags[...,4] & 0x80`.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t* camera_box_visible_x221f_b0;
  // Rebirth camera subject anchor Y (`fp->mv.co.common.x8`) seeded from ISO respawn-point data.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
  // data/stages/final_destination.json: respawn_points
  float* rebirth_camera_anchor_y_f32;
  // Fighter camera-subject target point (`camera_box->x1C`) and radius (`camera_box->x34.z`).
  // Seeded from replay-visible pose plus ISO-derived character camera metadata.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
  // refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
  float* camera_target_world_x_f32;
  float* camera_target_world_y_f32;
  float* camera_target_world_z_f32;
  float* camera_box_radius_f32;
  // Current-row Camera_80030CD8-style point-inside-stage-cam predicate, seeded from the promoted
  // camera target point plus ISO stage camera bounds.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
  uint8_t* camera_target_point_inside_stage_cam_bounds_u8;
  int16_t* downwait_timer;  // fp->mv.co.downwait.x0 (seeded; decomp: ftCo_DownWait_Anim)
  float* anim_frame_f32;    // decomp fp->cur_anim_frame (float; Slippi `state_age`)
  // Decomp-shaped internal animation/script timebase with deterministic fractional carry.
  // - anim_frame_fp_q16_16 mirrors fp->cur_anim_frame (float) as signed Q16.16 fixed-point.
  // - frame_speed_mul_fp_q16_16 mirrors fp->frame_speed_mul (float) as signed Q16.16 fixed-point.
  // refs/melee/src/melee/ft/fighter.c (cur_anim_frame, frame_speed_mul init / Fighter_ChangeMotionState)
  // refs/melee/src/melee/ft/ftanim.c (ftAnim_8006F0FC / ftAnim_SetAnimRate)
  int32_t* anim_frame_fp_q16_16;
  int32_t* frame_speed_mul_fp_q16_16;
  // Walk Anim callback source velocity (`mv_x0` in ftWalkCommon_800DFDDC).
  //
  // Decomp:
  // - ftWalkCommon_800DFDDC selects `mv_x0` from either fp->mv.co.walk.x0 or fp->gr_vel, then
  //   writes fp->frame_speed_mul via ftAnim_SetAnimRate.
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
  float* walk_anim_source_vel;
  // CaptureWait Anim-rate ownership bridge:
  // - ftCo_CaptureWaitHi_Anim updates fp->frame_speed_mul via ftAnim_SetAnimRate in Anim callback.
  // - Fighter_8006A360 advances ftAnim before callback-owned rate writes each frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  //
  // Teacher-forced reseed stores post-frame snapshots, so CaptureWait lanes can need a one-frame
  // prior-rate bridge to preserve callback ownership ordering.
  int32_t* capture_wait_prev_rate_fp_q16_16;           // previous seeded frame_speed_mul snapshot
  int32_t* capture_wait_seed_rate_snapshot_fp_q16_16;  // current seeded frame_speed_mul snapshot
  uint8_t* capture_wait_prev_rate_valid;  // 1 when previous snapshot continuity applies
  // ThrowLw Anim-rate ownership bridge (narrow, continuity-gated):
  // - Throw script flags are consumed in ThrowLw Anim callback (ftCo_800DD724), which runs under
  //   Fighter_8006A360 after prio-0 hitlag decrement in Fighter_8006A1BC.
  // - Teacher-forced reseed can snapshot a post-hitlag ThrowLw row with frame_speed_mul==0 even
  //   when the prior continuous row had a nonzero ThrowLw callback-owned rate; carry the previous
  //   seeded rate only on strict (frame_id,action,instance) continuity for the first post-hitlag row.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
  int32_t* throw_lw_prev_rate_fp_q16_16;  // previous seeded frame_speed_mul snapshot
  uint8_t* throw_lw_prev_rate_valid;      // 1 when ThrowLw continuity bridge applies
  // Transient: defer a single ftAnim_8006EBA4-shaped timebase tick until after combat resolves.
  //
  // Rationale: some motion-state entry paths in decomp call ftAnim_8006EBA4 immediately after
  // Fighter_ChangeMotionState (e.g. Blaster/AttackAir enter). Hits can occur during that
  // within-frame advance interval even when the post-frame pose_frame is the next integer.
  //
  // This lite sim's hitbox materialization is currently pose_frame-sampled; deferring the tick keeps
  // hitbox evaluation on the entry pose_frame while still producing the correct post-frame
  // action_frame/state_age after the step.
  uint8_t* anim_defer_tick_once;
  uint8_t* jumps_left;
  uint8_t* stocks;
  // Guard (shield) tilt pose state (seeded; decomp-shaped).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
  // - ftCo_800921DC: mv.co.guard.x8 init (neutral frame, e.g. 10) and x4=0
  // - ftCo_80091BC4: per-frame update from L-stick direction/magnitude
  uint16_t*
      guard_tilt_x8;     // mv.co.guard.x8 (frame-ish index; neutral is shield table neutral_frame)
  float* guard_tilt_x4;  // mv.co.guard.x4 (stick magnitude smoothing; 0..1)
  // Runtime-only transient: set when enter_guard_on() runs during the current step so item
  // projectile shield precedence can distinguish same-step GuardOn entry from teacher-forced
  // frozen GuardOn seeds that merely replay as `animation_index==-1, action_frame==-1`.
  uint8_t* guard_on_entered_this_frame;
  // Runtime-only owner bit for GuardOn entered through a same-frame `... -> Wait -> GuardOn`
  // callback handoff. This survives the following frozen GuardOn snapshot row, where
  // prev_action_id no longer identifies the callback source.
  uint8_t* guard_entry_via_wait_callback;
  // GuardReflect reflect timer (decomp: mv.co.guard.x14; seed uses +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x14;  // [batch * players]
  // GuardReflect powershield-active timer (decomp: mv.co.guard.x18; +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x18;  // [batch * players]
  // Per-step pre-tick GuardReflect timer snapshots (captured at frame start, before timer/callback
  // ownership updates). Used by collision/item lanes that need seed-lifetime ownership boundaries.
  uint8_t* guard_reflect_timer_x14_seed;  // [batch * players]
  uint8_t* guard_reflect_timer_x18_seed;  // [batch * players]
  // Guard release lockout + shield-drain latch (seeded; decomp-shaped).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC and ::ftCo_800925A4.
  uint8_t* guard_release_latched_xc;  // mv.co.guard.xC (0/1)
  uint8_t* guard_x10;                 // mv.co.guard.x10 (frames remaining; clamped to 0..255)
  float* lightshield_amount;          // fp->lightshield_amount (0..1)
  // GuardSetOff shield-hit int-damage lower bound for future GuardSetOff ownership fixes.
  // Decomp consumer: fp->x19A4 in ftCo_80092F2C.
  uint8_t* guard_setoff_hitlag_damage_min;
  // GuardSetOff hitlag-exit ownership phase discriminator for future F02 runtime fixes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  uint8_t* guard_setoff_hitlag_exit_phase_u8;
  // GuardSetOff post-hitlag owner discriminator for future F02 runtime fixes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
  uint8_t* guard_setoff_post_hitlag_owner_u8;
  // Locomotion/input-history internals.
  // - `tilt_timer_*`, `turn_*`, and KneeBend internals are seeded from replay history (MslSeed).
  uint8_t*
      kneebend_jump_input;  // ftCo_JumpInput (refs/melee/src/melee/ft/chara/ftCommon/forward.h)
  uint8_t* kneebend_is_short_hop;  // latched during KneeBend IASA (ftCo_KneeBend_Check_ShortHop)
  uint8_t* tilt_timer_x;  // fp->x670_timer_lstick_tilt_x (refs/melee/src/melee/ft/fighter.c)
  uint8_t* tilt_timer_y;  // fp->x671_timer_lstick_tilt_y (refs/melee/src/melee/ft/fighter.c)
  uint8_t* fall_fast;     // fp->fall_fast (refs/melee/src/melee/ft/ftcommon.c:505-520)
  // fp+0x2340 AttackDash lane:
  // - mv.co.attackdash.x0 countdown consumed by ftCo_800D8AE0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
  int16_t* attackdash_x0;  // [batch * players]
  // fp+0x2340 Attack1 lane:
  // - mv.co.attack1.x0 latched intent consumed by checkAttack12/checkAttack13.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
  uint8_t* jab_x0;  // [batch * players], 0/1
  // Run IASA lockout countdown (decomp: fp->mv.co.run.x0).
  // - Decremented in Run_Anim.
  // - Gates TurnRun/RunBrake in Run_IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_Anim,ftCo_Run_IASA}
  uint8_t* run_x0;  // [batch * players], clamped to 0..255
  // RunBrake TurnRun gate (`fp->cmd_vars[0]`) derived from the common RunBrake script.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
  //   ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  uint8_t* runbrake_cmd0;  // [batch * players], 0/1
  // Dash IASA branch latch (decomp: fp->mv.co.dash.x4).
  // - Set by ftCo_Dash_Enter(arg1).
  // - ftCo_Dash_IASA uses (x4 != 0 && cur_anim_frame <= x44) for early-branch gating.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c
  uint8_t* dash_x4;  // [batch * players], 0/1
  // Fox/Falco Shine release internals (decomp: fp->mv.fx.SpecialLw.{releaseLag,isRelease}).
  // - Set on SpecialLw enter by ftFox_SpecialLw_SetVars.
  // - Ticked in Start/Loop/Turn/Hit anim callbacks and consulted by ftFx_SpecialLwHit_Check.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c
  uint8_t* shine_release_lag;  // [batch * players], clamped to 0..255
  uint8_t* shine_is_release;   // [batch * players], 0/1
  // ECB lock countdown (decomp: fp->ecb_lock) used with CollData_X130_Locked.
  // - Set by ftCommon_8007D5D4 / ftCommon_8007D60C.
  // - Decremented once per map/collision callback in Fighter_procMap and clears lock at 0.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C,ftCommon_UnlockECB}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
  uint8_t* ecb_lock_timer;  // [batch * players], clamped to 0..255
  // Cliff / ledge internals (FD v1).
  // - ledge_side: -1 = none, 0 = left, 1 = right.
  // - stage_ledge_occupant_*: per-env occupant port, or -1.
  // - ledge_cooldown: per-fighter ledge grab cooldown timer (fp->x2064_ledgeCooldown).
  int8_t* ledge_side;                  // [batch * players]
  int8_t* stage_ledge_occupant_left;   // [batch]
  int8_t* stage_ledge_occupant_right;  // [batch]
  uint8_t* ledge_cooldown;             // [batch * players]
  // FallSpecial internals (seeded/derived).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
  uint8_t* fallspecial_xc;       // fp->mv.co.fallspecial.xC (arg1 to ftCo_80096900)
  uint8_t* turn_has_turned;      // fp->mv.co.turn.has_turned (refs/melee/.../ftCo_Turn.c:39-44)
  uint8_t* turn_frames_to_turn;  // fp->mv.co.turn.frames_to_turn (refs/melee/.../ftCo_Turn.c:39-44)
  // Turn dash-out latch (decomp: fp->mv.co.turn.x8).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
  int8_t* turn_x8;          // -1/0/+1
  uint8_t* lr_press_timer;  // fp->x67F (refs/melee/src/melee/ft/fighter.c:2078-2086)
  uint8_t*
      x672_input_timer;  // fp->x672_input_timer_counter (refs/melee/src/melee/ft/fighter.c:2020-2050)
  // Fighter input counters block: refs/melee/src/melee/ft/fighter.c:1897-2094.
  uint8_t* x673;    // fp->x673
  uint8_t* x674;    // fp->x674
  uint8_t* x675;    // fp->x675
  uint8_t* x676_x;  // fp->x676_x
  // Decomp: fp->x2228_b7 tracks most-recent fresh X stick-entry sign (1 right / 0 left).
  // refs/melee/src/melee/ft/fighter.c:1924,1949
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  uint8_t* x2228_b7;  // fp->x2228_b7
  uint8_t* x677_y;    // fp->x677_y
  uint8_t* x678;      // fp->x678
  uint8_t* x679_x;    // fp->x679_x
  uint8_t* x67A_y;    // fp->x67A_y
  uint8_t* x67B;      // fp->x67B
  uint8_t* x67C;      // fp->x67C
  uint8_t* x67D;      // fp->x67D
  uint8_t* x67E;      // fp->x67E
  uint8_t* x680;      // fp->x680
  uint8_t* x681;      // fp->x681
  uint8_t* x682;      // fp->x682
  uint8_t* x683;      // fp->x683
  uint8_t* x684;      // fp->x684

  // UCF pad buffer (seeded, multi-frame).
  // refs/ucf/include/ucf/pad_buffer.h
  //
  // `ucf_padbuf_stick_{x,y}` store the raw PAD bytes written by UCF's pad-buffer hook
  // (PADStatus.stick.{x,y}), with a fixed 4-entry ring buffer per player:
  // refs/ucf/src/pad_buffer/pad_buffer.cpp (buffer->index = (index+1)&3; entries[index] = status.stick)
  uint8_t* ucf_padbuf_index;            // [batch * players]
  uint8_t* ucf_padbuf_sdrop_up_frames;  // [batch * players]
  int8_t* ucf_padbuf_stick_x;           // [batch * players * 4]
  int8_t* ucf_padbuf_stick_y;           // [batch * players * 4]

  // Combat/timers
  float* percent;
  // Per-frame float damage accumulator (decomp: fp->dmg.x1838_percentTemp).
  //
  // Decomp:
  // - Accumulated by collision via ftColl_80076640 (adds *dmg into x1838_percentTemp).
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640
  // - Consumed/reset by Fighter_ProcessHit_8006D1EC each frame.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float* percent_temp;
  // Damage KB velocity merge timer (decomp: fp->dmg.x18AC_time_since_hit).
  //
  // Decomp:
  // - Fighter init seeds -1; Damage entry sets 0.
  // - Fighter_8006A360 increments this once per non-hitlag frame while active.
  // - ftCo_Damage_CalcVel replaces or merges `x8c_kb_vel` based on p_ftCommonData->xFC.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkInitReset_80067C98,Fighter_8006A360}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
  int16_t* damage_time_since_hit_x18ac;
  // Damage pipeline gates used by ftColl_80079AB0 (non-WSK else-branch) to select the base term
  // for `s = base + percent_temp`.
  //
  // Flags:
  // - fp+0x2225 bit0 (LSB) => decomp name fp->x2225_b7 (stamina-mode gate via PlayerInitData.xC_b7).
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 (0x80079B68..0x80079B90)
  //   refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitLoad_80068914 (x2225_b7 init)
  //   refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC (Player_SetMoreFlagsBit2 from PlayerInitData.xC_b7)
  // - fp+0x2224 bit5 (mask 0x20) => decomp name fp->x2224_b2.
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 (0x80079B74..0x80079B88)
  uint8_t* dmg_x2225_b7;  // [batch * players] (0/1)
  uint8_t* dmg_x2224_b2;  // [batch * players] (0/1)
  float* shield_hp;
  uint16_t* hitlag;
  // Internal-only helper: latched "hitlag > 0 at frame start" (before prio-0 decrement).
  //
  // Decomp ordering anchor:
  // - Fighter_8006A1BC decrements hitlag at proc prio 0 before anim/input callbacks.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //
  // This preserves the pre-decrement hitlag lane for seed-bridge pulse reconstruction that must
  // avoid re-emitting one-shot script flags when reseeded inside an already-active hitlag window.
  uint8_t* hitlag_pre_timer;
  // Internal-only helper: per-frame hitlag gate (0/1).
  //
  // Semantics: latched once per frame immediately after the decomp-shaped hitlag decrement step
  // (Fighter_8006A1BC) and used to gate "frozen under hitlag" behavior for the rest of that frame.
  //
  // Why this exists:
  // - In decomp, hitlag frames are decremented at proc prio 0 (Fighter_8006A1BC), then the main
  //   per-fighter update block (Fighter_8006A360 / Fighter_procUpdate) runs under `if (!fp->x2219_b5)`.
  // - Hitlag can be *applied* later in the frame (Fighter_ProcessHit_8006D1EC at proc prio 0xE),
  //   but that should not retroactively suppress earlier prio stages in the same frame.
  //
  // Using a per-frame latch ensures mid-frame hitlag application (e.g., from item/projectile hits)
  // doesn't change which callbacks run later in our single-pass step ordering.
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC (hitlag decrement / end)
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (update gate on `!fp->x2219_b5`)
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (hitlag start)
  uint8_t* hitlag_started_frame;
  // Runtime-only collision continuation latch for damage hitlag floor-hug handoff.
  //
  // Decomp shape:
  // - allow_sdi rows route through ft_80081DD4 -> mpColl_800477E0, where
  //   mpColl_80044628_Floor / mpColl_80044948_Floor can raise FloorPush|FloorHug while keeping
  //   the fighter airborne.
  // - On the immediate hitlag-exit frame, Damage_Coll / DamageFly_Coll then resolve the
  //   non-allow_sdi floor handoff via ft_80081DD4 -> mpColl_800473CC.
  //
  // This carries only that transient runtime-owned continuation across rollout steps. It is
  // intentionally not seeded; teacher-forced rows do not expose CollData env state.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor,mpColl_800473CC}
  uint8_t* damage_hitlag_floorhug_latch;
  uint16_t* hitstun;
  // Damage jump-buffer snapshot (decomp: fp->mv.co.damage.x14, set from x0 on jump input while in
  // hitstun; used by Damage_Anim inlineC0 gate vs p_ftCommonData->x1D0).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_Anim}
  uint16_t* damage_jump_buffer_x14;  // [batch * players]
  // Post-hitlag callback ownership lane (`fp->post_hitlag_cb`).
  // 0 = none, 1 = ftCo_Damage_OnExitHitlag (decomp: ftCo_8008DCE0 sets callback pointer).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  uint8_t* damage_post_hitlag_cb_kind;  // [batch * players]
  uint8_t* l_cancel;
  uint8_t* hurtbox_state;
  // Collision hit-status internals (decomp fp->x198C / x1990 / x1994 / x2221_b0).
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B760,ftColl_8007B7A4}
  uint8_t* colanim_hit_status_x198c;  // [batch * players] (0/1/2)
  uint16_t* colanim_timer_x1990;      // [batch * players]
  uint16_t* colanim_timer_x1994;      // [batch * players]
  uint8_t* colanim_lock_x2221_b0;     // [batch * players] (0/1)
  // Pose-driven world-space hurt capsule endpoints (computed each frame in hurtboxes_refresh).
  uint8_t* hurtcap_count;  // [batch * players]
  float* hurtcap_a_x;      // [batch * players * caps]
  float* hurtcap_a_y;
  float* hurtcap_a_z;
  float* hurtcap_b_x;
  float* hurtcap_b_y;
  float* hurtcap_b_z;
  float* hurtcap_radius;
  uint8_t* hurtcap_enabled;  // 0/1 per capsule slot (world array); used by combat eligibility
  uint8_t* hurtcap_is_grabbable;
  uint8_t* hurtcap_height;
  // Pose-driven world-space hitbox centers (computed each frame in hitboxes_refresh).
  // Used by combat_resolve() (Pass 1) for hitbox-vs-hurtcap intersection.
  uint8_t* hitbox_count;    // [batch * players]
  uint8_t* hitbox_enabled;  // [batch * players * MSL_MAX_HITBOXES]
  // Previous-frame world-space hitbox centers and enabled flags.
  //
  // Decomp ownership:
  // - HitCapsule stores previous/current centers as x58/x4C, updated once per frame by
  //   ftColl_8007AD18.
  // - Shield/body geometry helpers consume the x58->x4C sweep segment (lbColl_80007BCC /
  //   lbColl_8000805C).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000805C}
  uint8_t* hitbox_prev_enabled;  // [batch * players * MSL_MAX_HITBOXES]
  float* hitbox_prev_x;          // [batch * players * MSL_MAX_HITBOXES]
  float* hitbox_prev_y;
  float* hitbox_prev_z;
  // Pose-frame create-event marker (1 if a create event affected the slot at pose_frame).
  // Mirrors ftAction script writes consumed by ftColl_800768A0 / ftColl_8007AD18.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  uint8_t* hitbox_pose_create;  // [batch * players * MSL_MAX_HITBOXES]
  // Pose-frame enable-edge proxy (1 when slot is newly enabled or hit_group changes this frame).
  // Mirrors ftColl_800768A0 clear/copy ownership trigger used by ftColl_8007AD18 state transitions.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
  uint8_t* hitbox_enable_edge;  // [batch * players * MSL_MAX_HITBOXES]
  // HitCapsule.x43_b2 runtime lane (ftColl_80078C70 var_r22 -> lbColl_8000805C arg3).
  //
  // Decomp ownership:
  // - Spawn/create initializes x43_b2=0 in ftAction_8007121C.
  // - ftColl_80078C70 reads x43_b2 and forwards it to lbColl_8000805C arg3.
  // - lbColl_8000805C short-circuits acceptance when arg3!=0.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  uint8_t* hitbox_x43_b2;  // [batch * players * MSL_MAX_HITBOXES]
  // First-frame bootstrap flag for x58 previous centers after teacher-forced reseed.
  // 1 => bootstrap hitbox_prev_* once in hitboxes_refresh(), then clear to 0.
  uint8_t* hitbox_prev_bootstrap;  // [batch * players]
  float* hitbox_x;
  float* hitbox_y;
  float* hitbox_z;
  float* hitbox_radius;
  float* hitbox_damage;
  uint16_t* hitbox_bone_part_id;
  uint16_t* hitbox_u16_0;
  uint16_t* hitbox_u16_1;
  uint16_t* hitbox_u16_2;
  uint16_t* hitbox_u16_3;
  uint16_t* hitbox_u16_4;
  uint16_t* hitbox_u16_5;
  uint16_t* hitbox_u16_6;
  uint16_t* hitbox_u16_7;
  // Decoded per-hitbox attributes (from MSLHITB1 u16 tail; see docs/DATA_CONTRACT.md).
  uint16_t* hitbox_angle;  // degrees; 361 used as Sakurai angle sentinel
  uint16_t* hitbox_kbg;    // knockback growth
  uint16_t* hitbox_wsk;    // weight set knockback
  uint16_t* hitbox_bkb;    // base knockback
  uint8_t* hitbox_element;
  int8_t* hitbox_shield_damage;  // signed 8-bit
  uint8_t* hitbox_sfx_severity;
  uint8_t* hitbox_sfx_kind;
  uint16_t* hitbox_flags;  // bitfield (hit_grounded/hit_aerial/clank/rebound/etc.)
  // Pose/guard-derived world-space shield bubble parameters (computed each frame in shields_refresh).
  float* shield_x;       // [batch * players]
  float* shield_y;       // [batch * players]
  float* shield_z;       // [batch * players]
  float* shield_radius;  // [batch * players]
  // Pose-derived world-space reflector (SpecialLw) bubble parameters (computed each frame in
  // reflector_bubbles_refresh).
  float* reflector_x;       // [batch * players]
  float* reflector_y;       // [batch * players]
  float* reflector_radius;  // [batch * players]
  uint16_t* ground_id;
  uint32_t* animation_index;
  uint16_t* instance_hit_by;
  uint16_t* instance_id;
  // Internal-only: low 8 bits of fp->x2070 (the byte at fp+0x2073) used by ft_800895E0 to decide
  // whether to bump fp->x2088 on motion-state change.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz fp+0x2073; compare to flags)
  uint8_t* instance_id_x2073;  // [batch * players]
  // Internal-only: last action_id for which instance_id update logic ran.
  // Used to avoid bumping fp->x2088 on animation restarts (msl_anim_timebase_enter without
  // a motion-state change).
  uint16_t* instance_identity_last_action_id;  // [batch * players]
  uint16_t* attack_id;  // GALE01 fp->x2068_attackID (seeded; replay-history derived)
  uint16_t* attack_instance;
  // Internal-only: last action_id for which attack_id/attack_instance were updated.
  // Used to avoid incorrectly bumping x206C on animation restarts (msl_anim_timebase_enter without
  // a motion-state change).
  uint16_t* attack_identity_last_action_id;  // [batch * players]
  uint8_t* last_attack_landed;
  uint8_t* combo_count;
  // Combo tracking internals (GALE01 fp->x2094 + fp->x2098).
  //
  // Decomp trail:
  // - fp->x2094 victim pointer ("combo victim"): refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
  // - fp->x2098 combo-window timer: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  // - decrement + clear rule: refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
  //
  // Simulator representation:
  // - combo_victim_port: player-slot index in [0..3], or 0xFF for none.
  // - combo_victim_instance_id: victim `instance_id` identity key to avoid respawn pointer reuse.
  uint8_t* combo_victim_port;
  uint16_t* combo_victim_instance_id;
  uint16_t* combo_timer_x2098;
  uint8_t* last_hit_by;
  uint8_t* state_flags;  // [batch * players * 5]

  // Combat hitlists / rehit eligibility.
  //
  // Decomp shape:
  // - Each HitCapsule stores two victim rings (`victims_1`, `victims_2`) with per-entry cooldowns
  //   and ring insertion pointers (HitCapsule.x44 / x45).
  // - Rehit gate checks membership in victims_1: lbColl_8000ACFC(victim, hitbox).
  // - Insertion/refresh: lbColl_80008688 (victims_1) and lbColl_80008820 (victims_2).
  // - Decrement + expiry clear: lbColl_80008A5C.
  // refs/melee/src/melee/lb/types.h::HitCapsule
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688,lbColl_80008820,lbColl_80008A5C}
  //
  // Seed schema bridge (teacher-forced one-step):
  // - The seed schema carries an authoritative per-(attacker, hitbox, victim_port) cooldown map
  //   plus a legacy dense (attacker, hit_group, victim_port) map.
  // - Runtime combat consumes decomp-shaped per-hitbox victim rings (fighter_hitlist).
  // - On first use after reseed, active hitboxes materialize `victims_1` from the per-hitbox map
  //   when valid, else from the legacy group map for synthetic/old seeds.
  //
  // IMPORTANT (rollout contract, v1):
  // - `combat_hitlist_{cd,victim_iid}` are treated as seed-only inputs and are not maintained during
  //   rollouts. The authoritative runtime state lives in `fighter_hitlist` / `item_hitlist`.
  // - Serializing hitlist state mid-rollout (e.g., exporting to the dense map) is not supported yet.
  //   TODO: add an explicit export path if/when rollout save-states are needed.
  //
  // Dense legacy map layout: [batch * MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS]
  uint16_t* combat_hitlist_cd;
  uint16_t* combat_hitlist_victim_iid;
  // Authoritative per-hitbox seed map layout:
  // - valid: [batch * MSL_MAX_PLAYERS * MSL_MAX_HITBOXES]
  // - cd/iid: [batch * MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS]
  uint8_t* combat_hitlist_hb_valid;
  uint16_t* combat_hitlist_hb_cd;
  uint16_t* combat_hitlist_hb_victim_iid;
  // Reseed generation counter (incremented on reseed_seed).
  uint32_t* hitlist_reseed_gen;  // [batch]
  // Per fighter hitbox victim rings (x914[4] analogue).
  // Layout: [batch * MSL_MAX_PLAYERS * MSL_MAX_HITBOXES]
  MslHitlistCapsule* fighter_hitlist;
  // Per-hitbox init generation marker for seed materialization.
  // Layout: [batch * MSL_MAX_PLAYERS * MSL_MAX_HITBOXES]
  uint32_t* fighter_hitlist_init_gen;

  // Stale-move (staling) internals.
  //
  // Decomp shape (GALE01):
  // - Player maintains a `StaleMoveTable` ring buffer of the last 10 (move_id, attack_instance)
  //   pairs, with `current_index` pointing to the next write slot (wrap at 9).
  // - Staling multiplier consults the previous 9 entries starting from (current_index - 1).
  // refs/melee/src/melee/pl/types.h::StaleMoveTable
  // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  uint8_t* stale_queue_index;       // [batch * players]
  uint16_t* stale_move_id;          // [batch * players * MSL_STALE_QUEUE_SIZE]
  uint16_t* stale_attack_instance;  // [batch * players * MSL_STALE_QUEUE_SIZE]

  // Inputs (processed, per-frame) written by input_apply.
  uint16_t* input_buttons;           // [batch * players]
  uint16_t* prev_input_buttons;      // [batch * players]
  uint16_t* input_buttons_pressed;   // [batch * players] (rising edge)
  uint16_t* input_buttons_released;  // [batch * players] (falling edge)
  int8_t* input_main_x;              // [batch * players] (legalized/clamped; -80..80)
  int8_t* input_main_y;              // [batch * players] (legalized/clamped; -80..80)
  int8_t* prev_input_main_x;         // [batch * players] (processed from prev_input_bytes)
  int8_t* prev_input_main_y;         // [batch * players] (processed from prev_input_bytes)
  int8_t* input_c_x;                 // [batch * players] (legalized/clamped; -80..80)
  int8_t* input_c_y;                 // [batch * players] (legalized/clamped; -80..80)
  int8_t* prev_input_c_x;            // [batch * players] (processed from prev_input_bytes)
  int8_t* prev_input_c_y;            // [batch * players] (processed from prev_input_bytes)
  uint8_t* prev_input_l;             // [batch * players] (0..255)
  uint8_t* prev_input_r;             // [batch * players] (0..255)
  uint8_t* input_l;                  // [batch * players] (0..255)
  uint8_t* input_r;                  // [batch * players] (0..255)

  // Items (fixed-capacity, per-batch)
  uint8_t* item_exists;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_state;         // [batch * MSL_MAX_ITEMS]
  uint16_t* item_type;         // [batch * MSL_MAX_ITEMS]
  int8_t* item_owner;          // [batch * MSL_MAX_ITEMS]
  uint16_t* item_instance_id;  // [batch * MSL_MAX_ITEMS]
  // Item attack identity for staling attribution:
  // - attack_id: it->xD88_attackID (typically copied from owner fp->x2068_attackID at spawn)
  // - attack_instance: it->xD8C_attack_instance (typically copied from owner fp->x206C_attack_instance)
  //
  // Decomp refs:
  // - Spawn copy from fighter: refs/melee/src/melee/it/it_2725.c::it_8027B070
  // - Item damage staling: refs/melee/src/melee/it/itcoll.c::it_80272460 (calls ft_80089228)
  // - Stale queue update: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
  uint16_t* item_attack_id;        // [batch * MSL_MAX_ITEMS]
  uint16_t* item_attack_instance;  // [batch * MSL_MAX_ITEMS]
  float* item_direction;           // [batch * MSL_MAX_ITEMS]
  float* item_vel_x;               // [batch * MSL_MAX_ITEMS]
  float* item_vel_y;               // [batch * MSL_MAX_ITEMS]
  float* item_pos_x;               // [batch * MSL_MAX_ITEMS]
  float* item_pos_y;               // [batch * MSL_MAX_ITEMS]
  uint16_t* item_damage;           // [batch * MSL_MAX_ITEMS]
  // Per-item reflected damage multiplier lane (decomp: item->xC6C).
  //
  // Decomp trail:
  // - Reflect collision writes item->xC6C from fighter ReflectDesc damage multiplier.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // - Item reflect apply rewrites HitCapsule.unk_count as
  //   (u32)(hit.damage * item->xC6C + 0.99f), then routes through it_80272460.
  //   refs/melee/src/melee/it/item.c::Item_80269F14
  //   refs/melee/src/melee/it/itcoll.c::it_80272460
  float* item_reflect_damage_mul;  // [batch * MSL_MAX_ITEMS]
  float* item_timer;               // [batch * MSL_MAX_ITEMS]
  uint32_t* item_spawn_id;         // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc0;             // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc1;
  uint8_t* item_misc2;
  uint8_t* item_misc3;

  // Item hitbox victim rings (HitCapsule victim lists per item slot).
  // Decomp anchor (tick): refs/melee/src/melee/it/itcoll.c::it_8027146C
  // Layout: [batch * MSL_MAX_ITEMS]
  MslHitlistCapsule* item_hitlist;
} MslStateSoA;

int state_alloc(MslStateSoA* state, int batch_size);
void state_free(MslStateSoA* state);
