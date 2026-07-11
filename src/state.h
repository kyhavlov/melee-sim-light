#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api.h"
#include "hitlist_types.h"

enum {
  MSL_DAMAGE_HITLAG_ECB_SOURCE_NONE = 0,
  MSL_DAMAGE_HITLAG_ECB_SOURCE_THROWN_NEEDLE = 1,
};

// Hot SoA state owned by a batch. All arrays are sized for MAX_PLAYERS/ITEMS.
typedef struct MslStateSoA {
  // Meta
  int32_t* frame_id;
  uint32_t* frame_pre_random_seed;
  uint32_t* stage_id;  // [batch]
  // Fountain of Dreams dynamic platform heights, one pair per environment.
  // Platform id domain matches Slippi/grIzumi: 0=right, 1=left.
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  float* stage_fod_platform_height;                      // [batch * 2]
  uint8_t* stage_fod_platform_valid;                     // [batch * 2]
  float* stage_fod_platform_velocity;                    // [batch * 2]
  uint8_t* stage_fod_platform_velocity_valid;            // [batch * 2]
  float* stage_fod_platform_deferred_velocity;           // [batch * 2]
  uint8_t* stage_fod_platform_deferred_velocity_valid;   // [batch * 2]
  uint8_t* stage_fod_platform_height_source;             // [batch * 2]
  uint8_t* stage_fod_platform_scheduler_phase;           // [batch * 2]
  uint16_t* stage_fod_platform_scheduler_timer;          // [batch * 2]
  float* stage_fod_platform_scheduler_target;            // [batch * 2]
  uint8_t* stage_fod_platform_scheduler_wait_origin;     // [batch * 2]
  uint8_t* stage_fod_platform_scheduler_next_frame_rng;  // [batch * 2]
  uint8_t* stage_fod_platform_scheduler_valid;           // [batch * 2]
  uint16_t* stage_fod_platform_visible_choice_timer;     // [batch * 2]
  uint32_t* stage_fod_platform_visible_choice_rng_seed;  // [batch * 2]
  uint8_t* stage_fod_platform_visible_choice_valid;      // [batch * 2]
  // Yoshi's Story Shy Guy stage-object scheduler.
  // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
  uint16_t* stage_yoshi_shyguy_timer;   // [batch]
  uint8_t* stage_yoshi_shyguy_pattern;  // [batch]
  uint8_t* stage_yoshi_shyguy_valid;    // [batch]
  // Replay-rollout seed lane for the global HSD RNG stream consumed by the zero-timer Shy Guy
  // spawn callback. This is not a free-running gameplay clock; it only reconstructs the hidden
  // source stream for replay-seeded no-live countdown windows.
  // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
  uint32_t* stage_yoshi_shyguy_spawn_rng_seed;  // [batch]
  uint8_t* stage_yoshi_shyguy_spawn_rng_valid;  // [batch]
  // Dream Land Whispy current hidden wind state (`grOldPupupu` xDC), prefix-causal in eval and
  // source-scheduled in live/new-match runtime.
  // refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4}
  uint8_t* stage_dream_whispy_wind_dir;     // [batch]
  uint8_t* stage_dream_whispy_wind_valid;   // [batch]
  uint16_t* stage_dream_whispy_wind_timer;  // [batch]
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
  // Global six-slot respawn-platform cooldown table (`FighterMatchInfo[i].x8`).
  // refs/melee/src/melee/gm/gm_1601.c::{fn_8016758C,fn_80167638}
  uint8_t* match_flow_respawn_slot_cooldown;  // [batch * MSL_RESPAWN_PLATFORM_SLOT_COUNT]
  // Global stale-attack-instance counter (decomp: plStale_IncrementAttackInstance).
  // One per environment in the batch (per-match global counter).
  // refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance
  uint16_t* stale_attack_instance_counter;  // [batch]
  // Global action-state instance_id counter (decomp: plAttack_80037B08 uses unk_804D6480).
  // One per environment in the batch (per-match global counter).
  // refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
  uint16_t* instance_id_counter;  // [batch]
  // Global item spawn-id counter (`it_804D6D10`, copied to item->x1C on spawn).
  // One per environment in the batch. Slippi exposes item->x1C as item spawn_id.
  // refs/melee/src/melee/it/item.c::Item_80267AA8
  uint32_t* item_spawn_id_counter;  // [batch]
  float* match_damage_ratio;        // [batch] (decomp: gm_8016B248 -> StartMeleeRules.x30)
  uint8_t* is_teams;                // [batch]
  uint8_t* team_id;                 // [batch * MSL_MAX_PLAYERS]
  uint8_t* char_id;                 // [batch * MSL_MAX_PLAYERS]
  // Live MotionState Coll callback lanes. The row callback is installed on motion entry and may
  // subsequently be replaced by source callback writes; map dispatch never re-derives ownership
  // from the current action id.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Escape.c,ftCo_ItemThrow.c}
  uint16_t* live_coll_callback_id;
  uint8_t* live_coll_handler_kind;
  // Set when Fighter_procMap dispatched a migrated callback. A collision-owned motion change may
  // install a legacy destination callback, but that destination must not run recursively in the
  // same map phase.
  uint8_t* live_coll_migrated_ran;
  uint8_t* handicap;     // [batch * MSL_MAX_PLAYERS] (decomp: Player_GetHandicap)
  float* attack_ratio;   // [batch * MSL_MAX_PLAYERS] (decomp: Player_GetAttackRatio)
  float* defense_ratio;  // [batch * MSL_MAX_PLAYERS] (decomp: Player_GetDefenseRatio)

  // Kinematics
  float* pos_x;
  float* pos_y;
  float* pos_z;
  // Source xF8_playerNudgeVel.x, produced before Phys integration and consumed again by grounded
  // map wrappers such as ft_80084280.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  float* player_nudge_x;
  float* illusion_ghost_pos0_x;
  float* illusion_ghost_pos0_y;
  float* illusion_ghost_pos1_x;
  float* illusion_ghost_pos1_y;
  float* illusion_ghost_pos2_x;
  float* illusion_ghost_pos2_y;
  float* prev_pos_x;  // Position at start of current frame (pre-integration).
  float* prev_pos_y;  // Position at start of current frame (pre-integration).
  // Previous collision-sweep root for mpColl floor sweeps. Kept separate from prev_pos_* because
  // existing grounded rollback helpers use prev_pos_* as the current frame's pre-physics snapshot;
  // floor sweeps consume the previous promoted sweep root and only promote prev_pos_* after frame
  // simulation.
  float* floor_sweep_prev_pos_x;
  float* floor_sweep_prev_pos_y;
  // Seed-only override for the first floor-sweep snapshot after reseed. Normal rollouts clear the
  // valid bit, consume floor_sweep_prev_pos_{x,y}, then promote this frame's pre-physics root for
  // the next frame.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCheckFloor}
  float* floor_sweep_seed_prev_pos_x;
  float* floor_sweep_seed_prev_pos_y;
  uint8_t* floor_sweep_seed_prev_valid;
  uint8_t* floor_sweep_prev_source_owned;
  // Subset of floor_sweep_prev_source_owned produced by normal runtime post-frame promotion or a
  // live source callback. Teacher-forced reseed can supply a source-owned sweep endpoint for one
  // frame, but it must not prove current callback authority for hard-body floor publication.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
  uint8_t* floor_sweep_prev_runtime_owned;
  // Runtime CollData.cur_pos snapshot carried between wall/ceiling map callbacks. Source
  // `ft_CheckGroundAndLedge` calls `mpCollPrev` before replacing CollData.cur_pos with the
  // fighter's current root, so wall/ceiling `mpColl_80046904` sweeps from the previous callback's
  // published root rather than from this frame's pre-physics root. This is runtime-produced
  // callback state: teacher-forced reseed rows do not synthesize it from visible position.
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80046904}
  float* coll_wall_ceil_prev_pos_x;
  float* coll_wall_ceil_prev_pos_y;
  uint8_t* coll_wall_ceil_prev_pos_valid;
  // Hidden CollData ECB lifetime state. Source mpColl keeps current, prev, and desired ECB points
  // across `mpColl_LoadECB_inline` / `mpCollInterpolateECB`; floor sweeps consume this hidden
  // lifetime rather than resampling every endpoint from the visible action row.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80043754}
  float* coll_ecb_bottom_rel_y;
  float* coll_ecb_top_rel_y;
  float* coll_ecb_left_rel_x;
  float* coll_ecb_right_rel_x;
  float* coll_ecb_side_rel_y;
  float* coll_prev_ecb_bottom_rel_y;
  float* coll_prev_ecb_top_rel_y;
  float* coll_prev_ecb_left_rel_x;
  float* coll_prev_ecb_right_rel_x;
  float* coll_prev_ecb_side_rel_y;
  // Source `x64_ecb` saved by mpCollSqueezeHorizontal/Vertical while x34_flags.b6 is set.
  // The next mpCollInterpolateECB restores this ECB after copying the squeezed current ECB into
  // prev_ecb, then clears b6.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCollSqueezeHorizontal,mpCollSqueezeVertical}
  float* coll_squeeze_restore_ecb_bottom_rel_y;
  float* coll_squeeze_restore_ecb_top_rel_y;
  float* coll_squeeze_restore_ecb_left_rel_x;
  float* coll_squeeze_restore_ecb_right_rel_x;
  float* coll_squeeze_restore_ecb_side_rel_y;
  float* coll_desired_ecb_bottom_rel_y;
  float* coll_desired_ecb_top_rel_y;
  float* coll_desired_ecb_left_rel_x;
  float* coll_desired_ecb_right_rel_x;
  float* coll_desired_ecb_side_rel_y;
  uint8_t* coll_ecb_bottom_valid;
  uint8_t* coll_prev_ecb_bottom_valid;
  uint8_t* coll_squeeze_restore_ecb_valid;
  uint8_t* coll_desired_ecb_bottom_valid;
  uint8_t* coll_desired_ecb_bottom_locked_owner;
  uint8_t* coll_common_fall_blended_ecb_seed_valid;
  // Current CollData ECB is a frozen active-hitlag Damage envelope rather than the replay-visible
  // Damage pose. Runtime writes this on live Damage entry; replay reseed initializes it from
  // MslSeed::damage_hitlag_ecb_*.
  uint8_t* coll_damage_hitlag_ecb_valid;
  // Runtime provenance for the frozen active-hitlag Damage ECB. Source can keep the victim's
  // Damage hitlag CollData envelope live after a thrown Needle BODY hit even when the article is
  // consumed/destroyed by its callback; consumers must not rediscover that ownership by scanning
  // currently live items.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
  uint8_t* coll_damage_hitlag_ecb_source_kind;
  // Runtime-only CollData.floor/contact/env provenance produced by a live active-hitlag Damage
  // map callback. Source writes this through `ftCo_Damage_Coll -> ft_80081DD4 ->
  // mpColl_800477E0 -> mpColl_80044628_Floor/mpColl_80044948_Floor`, then later hitlag map
  // callbacks consume the carried CollData floor/contact state before hitlag exits. Unlike
  // coll_damage_hitlag_ecb_valid, this is not initialized from teacher-forced seed rows; reseed
  // rows may seed the hidden ECB envelope but not runtime-produced floor contact authority.
  uint8_t* coll_damage_hitlag_floor_contact_runtime;
  // Runtime-only EscapeAir floor producer authority. Source writes this only when the live
  // `EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8` floor path accepts the
  // current carried floor; teacher-forced/reseed rows may restore CollData.floor/ECB state but
  // cannot seed this callback-local authority.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  uint8_t* coll_escapeair_floor_producer_runtime;
  // Callback-local floor result scratch from the latest mpColl-shaped map callback. Source
  // `mpColl_80043754` owns this as per-callback state: it interpolates ECB/root substeps, calls a
  // floor helper, then the wrapper callback consumes the result immediately.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80043754,mpColl_8004A908_Floor,mpColl_80044628_Floor}
  uint8_t* coll_floor_result_valid;
  uint8_t* coll_floor_result_source;
  uint8_t* coll_floor_result_mode;
  // Runtime-only proof that the current map callback took mpColl_8004A678_Floor's edge-release
  // branch. The wrapper consumes its floor result before locomotion resolves Ottotto, so carry this
  // one callback result explicitly instead of reconstructing it from replay/history state.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A678_Floor,mpColl_8004B4B0}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  uint8_t* coll_a678_edge_runtime;
  uint16_t* coll_floor_result_segment_id;
  float* coll_floor_result_contact_x;
  float* coll_floor_result_contact_y;
  float* coll_floor_result_normal_x;
  float* coll_floor_result_normal_y;
  // Debug-only mpColl floor-owner probe. These fields are written from existing collision
  // decision points and exported only by debug_write_colldata_ecb; they are not gameplay authority.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_800477E0,mpColl_80047E14}
  uint8_t* coll_floor_probe_valid;
  uint8_t* coll_floor_probe_owner;
  uint8_t* coll_floor_probe_reject_reason;
  uint8_t* coll_floor_probe_raw_bottom_sweep_hit;
  uint8_t* coll_floor_probe_projection_hit;
  uint8_t* coll_floor_probe_carried_source_owned;
  uint8_t* coll_floor_probe_carried_runtime_owned;
  uint64_t* coll_floor_probe_reject_bits;
  uint32_t* coll_floor_probe_source_phases;
  uint16_t* coll_floor_probe_carried_segment_id;
  uint16_t* coll_floor_probe_candidate_segment_id;
  uint16_t* coll_floor_probe_projected_segment_id;
  int16_t* coll_floor_probe_candidate_line_idx;
  int16_t* coll_floor_probe_projected_line_idx;
  float* coll_floor_probe_prev_bottom_x;
  float* coll_floor_probe_prev_bottom_y;
  float* coll_floor_probe_cur_bottom_x;
  float* coll_floor_probe_cur_bottom_y;
  // Wall-pass diagnostic probe lanes (mirror of the floor probe family; written by
  // mpcoll_wall_ceil candidate collectors + commit, debug-only consumers).
  uint8_t* coll_wall_probe_valid;
  uint8_t* coll_wall_probe_side;         // MSL_WALL_LEFT / MSL_WALL_RIGHT of the last record
  uint8_t* coll_wall_probe_commit_kind;  // MSL_MPCOLL_WALL_RESULT_* of the last commit (0 none)
  uint8_t* coll_wall_probe_candidate_count;
  int16_t* coll_wall_probe_segment_id;  // committed wall segment id (-1 none)
  float* coll_wall_probe_corr_x;        // committed push dx
  float* coll_substep_prev_pos_x;
  float* coll_substep_prev_pos_y;
  float* coll_substep_cur_pos_x;
  float* coll_substep_cur_pos_y;
  // Source CollData.last_pos root for the current/last map callback. mpColl wrappers snapshot
  // CollData.cur_pos into last_pos before writing the fighter root to cur_pos; mpCollEnd uses it
  // for the floor callback dy argument.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCollEnd}
  float* coll_last_pos_x;
  float* coll_last_pos_y;
  // Collision-stage prev/cur position snapshots used for mpColl-shaped ledge-grab AABB checks.
  //
  // Decomp: the ledge-grab block consumes CollData.prev_pos / CollData.cur_pos as managed inside
  // mpColl_80043754's collision substep loop ("previous substep", not "previous frame").
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  //
  // The frame-level collision-stage interval is captured as:
  // - coll_stage_prev_pos: fighter position immediately before stage_collision_apply() this frame
  // - coll_stage_cur_pos: fighter position immediately after stage_collision_apply() this frame
  // Source-shaped callback substep owners that stop earlier publish coll_substep_* above; ledge
  // AABB consumers use this interval when no earlier source packet exists.
  float* coll_stage_prev_pos_x;
  float* coll_stage_prev_pos_y;
  float* coll_stage_cur_pos_x;
  float* coll_stage_cur_pos_y;
  uint16_t* coll_stage_prev_ground_id;
  float* speed_air_x_self;
  float* speed_ground_x_self;
  float* speed_y_self;
  float* speed_x_attack;
  float* speed_y_attack;
  // Hidden ReboundStop xE8_ground_accel_2 carry from ftCo_80099D9C -> ftCommon_800804A0.
  // Runtime clank entry writes it; physics consumes it on the first Rebound frame after hitlag.
  float* rebound_ground_accel_2;
  // Hidden ReboundStop `mv.co.rebound.anim_start` rate from the same ftCo_80099D9C owner.
  // ReboundStop_Anim consumes it when entering Rebound after hitlag.
  int32_t* rebound_anim_rate_fp_q16_16;
  // Internal Firefox/Firebird launch pose owner (`mv.fx.SpecialHi.rotateModel`).
  // Decomp writes this on launch entry and collision continuation; Phys and FtPart_XRotN pose
  // consumers reuse the stored value instead of deriving a new angle from decelerated self_vel.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Phys,
  //   ftFx_SpecialAirHi_Coll}
  float* specialhi_rotate_model;
  uint8_t* specialhi_rotate_model_valid;
  // Fighter model scale (decomp: fp->x34_scale.y). This is an external multiplier applied to
  // various collision/visual calculations; default is 1.0 in normal matches.
  float* fighter_scale_y;
  uint8_t* facing;
  // Motion-state facing lane (decomp: fp->facing_dir1).
  int8_t* facing_dir1;
  // Marth Counter facing lane (`fp->specialn_facing_dir`).
  // ftColl writes this on Counter descriptor contact, and ftMs_SpecialLw_80139140 copies it to
  // fp->facing_dir when entering CounterHit. It is not derived from the hit callback position.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
  int8_t* specialn_facing_dir1;
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
  uint8_t* smash_charge_state;                 // 0=None, 1=PreCharge, 2=Charging, 3=Release
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
  // Internal DamageFly hitlag-exit ASDI provenance. Set only by a hitlag-time wall collision
  // callback; raw wall_id persists after detach and is not sufficient on its own.
  uint8_t* damage_hitlag_wall_asdi_latch;
  // Collision contact metadata owned by mpColl ceiling substrate (FD-only v1).
  float* ceiling_contact_x;
  float* ceiling_contact_y;
  float* ceiling_normal_x;
  float* ceiling_normal_y;
  uint16_t* ceiling_id;  // ISO-derived segment index (stable id).
  uint32_t* coll_env_flags;
  uint32_t* coll_prev_env_flags;
  // Hidden CollData joint filters. Source mpLib skips all lines owned by joint_id_skip and, when
  // joint_id_only is set, scans only that joint. -1 disables each filter.
  // refs/melee/src/melee/lb/types.h::CollData::{joint_id_skip,joint_id_only}
  int16_t* mpcoll_joint_id_skip;
  int16_t* mpcoll_joint_id_only;

  // State machine
  uint16_t* action_id;
  // Internal-only frame-start action snapshot. Some callback-local owners publish a later action
  // before item/fighter collision still consumes the frame-start source episode.
  uint16_t* frame_start_action_id;
  // Internal-only frame-start Damage hitstun snapshot. DamageFlyRoll_Anim decrements and tests
  // this motion-var lane inside its Anim callback, before later shared timer/IASA consumers.
  uint16_t* frame_start_hitstun;
  // Replay-true previous action snapshot (t-1 -> t), seeded from validation history for entry-shaped
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
  // Producer is strictly causal in validation replay-buffer seed derivation.
  uint8_t* throw_pulse_crossed_prev_frame;
  // Current teacher-forced step's command-timer pending throw projectile pulse (0 = none).
  // This seed-owned lane is authoritative for one step after reseed; rollout falls back to runtime
  // frame-crossing after `throw_command_pending_seed_valid` is cleared at end-of-frame.
  uint8_t* throw_command_pending_pulse_frame;
  uint8_t* throw_command_pending_seed_valid;
  // Internal command-cursor carry for source frames where ftAction reaches a projectile command but
  // the matching article is serialized by the next Anim callback. Unlike the seed-owned pending
  // lane, this persists across the end-of-frame transient clear until the carried pulse is spawned.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  uint8_t* throw_command_deferred_pulse_frame;
  // Internal runtime producer for the current frame's throw pulse crossing.
  // The frame scheduler promotes this to `throw_pulse_crossed_prev_frame` at end-of-frame so
  // rollout can carry the same throw-side pulse ownership that one-step seeds expose directly.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  uint8_t* throw_pulse_crossed_curr_frame;
  // Runtime-only source marker for same-step SpecialN gun creation. Fighter Anim callbacks can
  // enter SpecialN and create the attached gun before a later item/combat pass damages the owner
  // out of the action; post-combat gun lifetime must then see the same ftFx_SpecialN_Enter article
  // episode even though frame-start prev_action_id was not a blaster action.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialN_Enter,ftFx_SpecialN_GetBlasterAction}
  // refs/melee/src/melee/it/items/itfoxblaster.c::{it_802AE8A8,itFoxblaster_UnkMotion8_Anim}
  uint8_t* blaster_gun_spawned_this_frame;
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
  // Explicit Fighter_8006CDA4 pre-gate RNG stream-phase seed lane for DamageFlyRoll entry.
  // 0: no seeded pre-gate stream ownership.
  // 1: consume one pre-gate HSD_Randi before ftCo_8008DCE0 block_33.
  // 2: consume two pre-gate HSD_Randi calls before ftCo_8008DCE0 block_33.
  // 3: consume all three decomp-visible pre-gate HSD_Randi calls before ftCo_8008DCE0 block_33.
  // 4: source-proven zero-consume gate; admit the gate without a pre-gate stream advance.
  // Nonzero DamageFlyTop values may carry across the same segment as hidden held-item/x197C state.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/types.h
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  uint8_t* fighter_8006cda4_pre_gate_consume_count;
  // Grounded source-owner clear phase bridge (`ftCommon_800804FC` path).
  // 0: no grounded clear-phase override.
  // 1: consume grounded clear before x18C8 decrement for this one-step row.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  uint8_t* source_clear_grounded_damage_clear_phase;
  // Terminal source-owner phase for `source_clear_timer_x18c8 == 1` rows. One-step transient lane
  // produced in validation tooling; matching runtime owners may also park the source lane while
  // retiring the countdown.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  uint8_t* source_clear_terminal_phase;
  // Internal-only compatibility/seed throw flow latch. Normal runtime release damage is owned by
  // the thrower's motion-state Anim callback; this lane remains for teacher-forced one-step/reseed
  // states that expose a detached pending-release victim after item collision.
  //
  // Stored on the thrower (not the victim) so we can clear it by iterating over players each frame.
  // Value: 0xFF = none, else victim port in [0..MSL_MAX_PLAYERS).
  uint8_t* throw_pending_victim_port;
  // Internal-only compatibility/seed throw flow latch: the throw hitbox idx to apply when
  // throw_pending_victim_port is set. Value: 0xFF = none, else hitbox idx in
  // [0..MSL_THROW_HITBOX_IDX_MAX).
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
  // - attached_victim_port is the owner-side `fp->victim_gobj` analog for the currently attached
  //   grabbed/thrown victim (0xFF = none).
  // - grab_offset_{y,z} store the decomp-shaped fp->x1A70.{y,z} (unscaled) inferred at reseed-time.
  uint8_t* attached_victim_port;  // [batch * players]
  uint8_t* grab_owner_port;       // [batch * players]
  // ftColl catch contract: attacker descriptor kind (fp->x1A68) and victim rejection mask
  // (fp->x1A6A). The latter also protects active carriers from third-party catches in doubles.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  uint16_t* catch_kind_x1a68;
  uint16_t* catch_target_mask_x1a6a;
  int8_t* grab_mash_stick_x_sign;  // [batch * players] fp->x1A50
  int8_t* grab_mash_stick_y_sign;  // [batch * players] fp->x1A51
  float* grab_offset_y;            // [batch * players]
  float* grab_offset_z;            // [batch * players]
  // Attached Thrown* victims are callback-owned and should not let stage collision/physics mutate
  // their grounded state during the attached window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
  //   ftCo_800DE508,ftCo_ThrownF_Phys,ftCo_ThrownF_Coll,ftCo_ThrownB_Phys,ftCo_ThrownB_Coll,
  //   ftCo_ThrownHi_Phys,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Phys,ftCo_ThrownLw_Coll
  // }
  uint8_t* thrown_attached_prev_on_ground;   // [batch * players]
  uint16_t* thrown_attached_prev_ground_id;  // [batch * players]
  uint8_t* match_flow_timer;
  uint8_t* match_flow_pending_rebirth_char_id;
  // Hidden fp+0x2218 byte preserved while team stock-share exposes a zeroed DeadDown slot.
  // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkInitReset_80067C98,Fighter_ChangeMotionState}
  uint8_t* match_flow_pending_rebirth_state_flags_2218;
  // DeadUpFall hidden offset/velocity owner (`mv.co.unk_deadup.x50/x5C`).
  //
  // These lanes are not Slippi-visible by themselves, but they are the source-owned pose scratch
  // behind DeadUpFall/HitCamera camera/effect positioning. Runtime initializes them from
  // p_ftCommonData x538..x54C and advances them through ftCo_DeadUpFall_Phys.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D4580,ftCo_DeadUpFall_Phys}
  float* dead_up_fall_offset_x;
  float* dead_up_fall_offset_y;
  float* dead_up_fall_offset_z;
  float* dead_up_fall_vel_x;
  float* dead_up_fall_vel_y;
  float* dead_up_fall_vel_z;
  // Legacy compatibility lane from the earlier EntryEnd->Fall investigation.
  // The authoritative opening-control owner is now opening_input_lock_timer (`fp->x221D_b4`).
  uint8_t* entry_end_fall_lock;
  // Rebirth / dead-flow camera-box visibility (`fp->x221F_b0`) promoted as a named SoA lane for
  // future F04 ownership fixes. Seeded from replay-visible `state_flags[...,4] & 0x80`.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t* camera_box_visible_x221f_b0;
  // Replay-playback current-row rising edge for fp->x221F_b0. This is not seeded gameplay state;
  // it is set only by the combined replay-frame step before state_flags_refresh consumes it.
  uint8_t* camera_box_visible_x221f_b0_replay_rise;
  uint8_t* camera_box_visible_x221f_b0_replay_prev;
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
  // Runtime proof that the camera target/radius and inside-bounds predicate were refreshed from
  // current fighter pose this frame.
  // refs/melee/src/melee/ft/ftcamera.c::ftCamera_UpdateCameraBox
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800866DC,ftLib_80086A8C}
  uint8_t* camera_target_live_pose_valid;
  // Runtime-owned magnifying-glass visibility admission from ftLib_80086A8C. This is distinct from
  // the replay-visible seed bit so timers can start counters only from source-owned live camera
  // state, not stale teacher-forced snapshots.
  uint8_t* magnify_damage_runtime_visibility_owner;
  uint8_t* magnify_damage_seed_episode_active;
  uint8_t* magnify_damage_local_episode_kind;
  // Current-row Camera_80030CD8-style point-inside-stage-cam predicate, seeded from the promoted
  // camera target point plus ISO stage camera bounds.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
  uint8_t* camera_target_point_inside_stage_cam_bounds_u8;
  // Hidden magnifying-glass/offscreen damage counter (`fp->dmg.x1910`).
  // Seeded from replay-visible camera/magnify history; runtime updates it with the source-owned
  // Fighter_procUpdate interval damage path.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  uint16_t* magnify_damage_counter_x1910;
  int16_t* downwait_timer;  // fp->mv.co.downwait.x0 (seeded; decomp: ftCo_DownWait_Anim)
  // PassiveWall / PassiveWallJump hidden startup timer (`fp->mv.co.passivewall.timer`).
  // Slippi post-frame keeps action_frame at 0 through the frozen wall-tech startup, so this owner
  // must be seeded from replay history for one-step parity.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
  uint8_t* passivewall_timer;
  // PassiveWall startup jump latch (`fp->mv.co.passivewall.x8`). While timer>0,
  // PassiveWall_IASA sets this latch when ftCo_800C1E0C succeeds; PassiveWall_Anim consumes it
  // on timer expiry by re-entering PassiveWallJump through Fighter_ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_PassiveWall_IASA,inlineA0}
  uint8_t* passivewall_jump_latch;
  // Consecutive ordinary walljump count (`fp->x1969_walljumpUsed`) and the pre-increment count
  // copied into the active PassiveWall episode (`fp->mv.co.passivewall.vel_y_exponent`).
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
  uint8_t* walljump_used_count;
  uint8_t* passivewall_vel_y_exponent;
  // Generic wall-jump hidden input phase (`fp->wall_jump_input_timer`, `fp->x2110_walljumpWallSide`).
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  uint8_t* walljump_input_timer;
  int8_t* walljump_wall_side_i8;
  // One-step replay bridge for CollData wall-hug phase hidden from Slippi. Runtime clears this
  // after the seeded step; live rollouts must use current mpColl WallHug bits.
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  uint8_t* walljump_seed_phase_valid;
  float* anim_frame_f32;  // decomp fp->cur_anim_frame (float; Slippi `state_age`)
  // Decomp-shaped internal animation/script timebase with deterministic fractional carry.
  // - anim_frame_fp_q16_16 mirrors fp->cur_anim_frame (float) as signed Q16.16 fixed-point.
  // - frame_speed_mul_fp_q16_16 mirrors fp->frame_speed_mul (float) as signed Q16.16 fixed-point.
  // refs/melee/src/melee/ft/fighter.c (cur_anim_frame, frame_speed_mul init / Fighter_ChangeMotionState)
  // refs/melee/src/melee/ft/ftanim.c (ftAnim_8006F0FC / ftAnim_SetAnimRate)
  int32_t* anim_frame_fp_q16_16;
  int32_t* frame_speed_mul_fp_q16_16;
  // GuardSetOff hidden exit anim-rate seed/provenance lane.
  //
  // Slippi can expose the source `fp->frame_speed_mul` for GuardSetOff only on the first
  // non-hitlag post-frame, after Fighter_8006A1BC has ended hitlag and before
  // Fighter_8006A360 advances animation. Runtime uses this field only when seeded/provenanced,
  // applies it on that hitlag-exit frame, and clears it immediately afterward; natural
  // free-running gameplay leaves it zero and uses the live ftCo_80092F2C entry rate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
  int32_t* guard_setoff_exit_rate_fp_q16_16;
  // Walk Anim callback source velocity (`mv_x0` in ftWalkCommon_800DFDDC).
  //
  // Decomp:
  // - ftWalkCommon_800DFDDC selects `mv_x0` from either fp->mv.co.walk.x0 or fp->gr_vel, then
  //   writes fp->frame_speed_mul via ftAnim_SetAnimRate.
  // - Runtime updates this causally from the modeled Walk_Anim callback; replay one-step seeds may
  //   carry a same-Walk lookahead reconstruction because Slippi exposes that callback-owned rate
  //   one row after the anim tick that consumed it.
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
  float* walk_anim_source_vel;
  // Replay-facing Walk retarget tick source for the hidden `ft_GetGroundFrictionMultiplier` branch.
  // This is consumed only on WalkSlow/Middle/Fast type-change rows; runtime normally keeps it zero
  // and uses the causal walk_anim_source_vel lane.
  // refs/melee/src/melee/ft/ftwalkcommon.c::{ftWalkCommon_800DFDDC,ftWalkCommon_800DFEC8}
  float* walk_retarget_tick_source_vel;
  // Run Anim callback source velocity (`vel` in ftCo_Run_Anim).
  //
  // Decomp:
  // - ftCo_Run_Anim selects `vel` from either fp->mv.co.run.x4 or fp->gr_vel, then writes
  //   fp->frame_speed_mul via ftAnim_SetAnimRate.
  // - Runtime updates this causally from the modeled Run_Anim callback; replay one-step seeds may
  //   carry a same-Run hidden-owner reconstruction without weakening frame_speed_mul_f32.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
  float* run_anim_source_vel;
  // Replay-facing Turn->KneeBend hidden-facing owner lane. 0=no override, 1=left, 2=right.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
  uint8_t* turn_kneebend_facing_override;  // [batch * players]
  // Capture/grab hidden owner lanes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_800DA824,ftCo_CaptureWaitHi_Anim,fn_800DB8A4,fn_800DC014
  // }
  float* capture_grab_timer;            // fp->grab_timer
  float* capture_wait_counter;          // mv.co.capturewait.x0
  float* capture_wait_anim_rate_timer;  // mv.co.capturewait.x4
  uint8_t* capture_wait_jump_latch;     // mv.co.capturewait.xC
  uint8_t* capture_breakout_pending;    // explicit CatchWait/CaptureWait breakout resolve bit
  // Shared throw/thrown entry anim-speed cache (`ftCo_800DD4B0` / `ftCo_800DD398`).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398}
  int32_t* throw_anim_rate_fp_q16_16;
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
  // Runtime-only frame-start snapshots of the tilt lanes, captured by shields_refresh before its
  // per-step ftCo_80091BC4 update. Item collision consumes the pre-update values: the source item
  // pass reads mv.co.guard.{x4,x8} as left by the PREVIOUS frame's guard anim callback (MAJ:7384
  // rollout needs x4=0.501, not the same-step 0.75 post-update value).
  uint16_t* guard_tilt_x8_frame_start;
  float* guard_tilt_x4_frame_start;
  // Runtime-only transient: set when enter_guard_on() runs during the current step so item
  // projectile shield precedence can distinguish same-step GuardOn entry from teacher-forced
  // frozen GuardOn seeds that merely replay as `animation_index==-1, action_frame==-1`.
  uint8_t* guard_on_entered_this_frame;
  // Runtime-only owner countdown for GuardOn entered through a same-frame
  // `... -> Wait -> GuardOn` callback handoff. This survives the immediate frozen
  // GuardOn/spotdodge handoff window, where prev_action_id no longer identifies the callback
  // source, then is consumed so later GuardOn rows cannot stale-carry entry provenance.
  uint8_t* guard_entry_via_wait_callback;
  // Runtime-only source marker for CliffClimb/Attack/Escape option end -> Wait_IASA -> GuardOn.
  // Fighter shield pair-order can keep the fresh no-submotion ShieldDesc out of a later-slot
  // same-frame grounded Reflector pass for this cliff callback owner only; ordinary GuardOn entry
  // sources stay on the normal ShieldDesc path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
  uint8_t* guard_on_cliff_end_source;
  // Runtime-only marker for GuardOn entered through Dash_IASA's `dash.x4 != 0` handoff into the
  // mid `ftCo_80091AD8` helper. Projectile item collision for that handoff frame has already
  // passed before the ShieldDesc is eligible in vanilla, so same-step item shield precedence must
  // not consume the newly created GuardOn shield descriptor.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_800923B4}
  uint8_t* guard_entry_via_dash_91ad8;
  // Runtime-only countdown for Landing -> GuardOn entries whose callback source can feed the
  // next-frame GuardOn -> GuardReflect item ReflectDesc owner. This is a source-entry latch, not a
  // replay row key; it decays across the immediate frozen GuardOn handoff window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::*_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_8009388C}
  uint8_t* guard_on_entry_reflect_source_latch;
  // Runtime-only snapshot of mv.co.guard.x10 at the beginning of the current fighter action
  // callback. Shield contact can enter GuardSetOff later in the same frame; that contact preserves
  // the pre-GuardOn/Guard hold-tick x10 owner rather than the post-callback decremented value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_80092F2C}
  uint8_t* guard_x10_frame_start;
  // Runtime-only marker for the Dash IASA locomotion -> GuardReflect entry slice that also reaches
  // Dash's terminal gr_vel scalar in the same callback. Item reflect ownership uses this to keep
  // same-frame xDA8 transfer on the source callback phase that exposed it, without replay ids.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80093A50}
  uint8_t* guard_reflect_entry_dash_terminal_scalar;
  // Runtime-only marker for GuardReflect entered during the current input-callback pass. Source
  // calls ftCo_80093A50/ftCo_8009388C from IASA after the frame's GuardReflect Anim callback phase,
  // so destination GuardReflect must not run an extra same-frame GuardOn_Anim shield drain.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_80093A50,ftCo_GuardReflect_Anim}
  uint8_t* guard_reflect_entered_this_frame;
  // Seed snapshot of `fp+0x221B_b0` / Slippi `isShieldActive` before current-step shield
  // descriptor callbacks mutate state_flags. Item GuardReflect ownership uses this to distinguish
  // aged descriptor-present rows from timer carry rows without replay ids.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077464}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t* guard_seed_shield_desc_active;
  // Runtime-only frame-start snapshot of raw fp+0x2218 / Slippi state_flags[0].
  // Item callbacks can need the row-start ReflectDesc behavior byte after reflector refresh has
  // updated the live post-frame state_flags value.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t* state_flags_2218_frame_start;
  // Runtime-only frame-start snapshot of raw fp+0x221C / Slippi state_flags[3].
  // State-flag publication can need to distinguish frame-start carries from same-frame source
  // owners after callbacks have already mutated the live post-frame byte.
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield layout)
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t* state_flags_221c_frame_start;
  // Runtime-only marker for Guard/GuardOn/GuardReflect/GuardOff IASA entering KneeBend through
  // ftCo_800CB024 in the current step. The decomp input callback runs once per frame, so a fresh
  // Guard -> KneeBend handoff must not also consume KneeBend_IASA before the next frame.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
  uint8_t* guard_jump_oos_entered_this_frame;
  // Runtime-only marker for SpecialLw Loop/Turn/End IASA entering JumpAerial in the current step.
  // The decomp input callback does not then run destination JumpAerial/Fall special dispatch again
  // in the same frame, so the later generic B-special pass must not consume the same B/up edge.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwTurn_IASA,ftFx_SpecialAirLwEnd_Anim}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  uint8_t* shine_jump_iasa_entered_this_frame;
  // Runtime/source provenance for a nonzero CollData_X130 desired-bottom packet preserved by
  // SpecialLw -> JumpAerial. EscapeAir platform sweeps may consume the packet, but non-platform
  // publication still needs the source bottom-crossing phase.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
  uint8_t* shine_jump_preserved_desired_bottom;
  // GuardReflect reflect timer (decomp: mv.co.guard.x14; seed uses +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x14;  // [batch * players]
  // GuardReflect powershield-active timer (decomp: mv.co.guard.x18; +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x18;  // [batch * players]
  // Per-step pre-tick GuardReflect timer snapshots (captured at frame start, before timer/callback
  // ownership updates). Used by collision/item lanes that need seed-lifetime ownership boundaries.
  uint8_t* guard_reflect_timer_x14_seed;  // [batch * players]
  uint8_t* guard_reflect_timer_x18_seed;  // [batch * players]
  // GuardReflect entry provenance:
  // - 1: ftCo_8009388C path from GuardOn/Guard (already shielding; ReflectDesc-only until expiry)
  // - 0: ftCo_80093A50 direct locomotion powershield path.
  uint8_t* guard_reflect_origin_guardon;
  // GuardOff special/attack enable timer (decomp: mv.co.guard.x1C).
  //
  // Runtime owner:
  // - Fighter-vs-fighter shield collision calls ftCo_80094138 when powershield-active x221C_b2 is
  //   live, arming x1C from p_ftCommonData->x2B8 and clearing x10.
  // - GuardOn/Guard/GuardReflect inlineC0 decrements x1C when it does not exit to GuardOff.
  // - GuardOff_IASA only routes the special/attack chain while x1C is non-zero.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_80094138,ftCo_GuardOff_IASA}
  uint8_t* guard_special_enable_timer_x1c;
  // Guard release lockout + shield-drain latch (seeded; decomp-shaped).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC and ::ftCo_800925A4.
  uint8_t* guard_release_latched_xc;  // mv.co.guard.xC (0/1)
  uint8_t* guard_x10;                 // mv.co.guard.x10 (frames remaining; clamped to 0..255)
  float* lightshield_amount;          // fp->lightshield_amount (0..1)
  // GuardSetOff shield-hit int-damage lower bound for future GuardSetOff ownership fixes.
  // Decomp consumer: fp->x19A4 in ftCo_80092F2C.
  uint8_t* guard_setoff_hitlag_damage_min;
  // Per-defender teacher-forced shield-hit x19A4 max int damage for accepted GuardSetOff entries.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  uint8_t* combat_shield_hit_int_damage;
  // Per-defender teacher-forced shield-hit x19A0 shieldDamageTaken for accepted GuardSetOff entries.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  uint8_t* combat_shield_damage_taken;
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
  // Transient callback/collision-visible x671 copy captured before input.c advances tilt_timer_y.
  // Active-hitlag Damage floor projection uses this to distinguish a carried Y-window owner from a
  // same-frame downward edge.
  uint8_t* tilt_timer_y_frame_start;
  uint8_t* fall_fast;  // fp->fall_fast (refs/melee/src/melee/ft/ftcommon.c:505-520)
  // Frame-start fp->fall_fast, before same-frame callbacks may clear the mutable latch.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  uint8_t* fall_fast_frame_start;
  // One-step internal fastfall seed provenance. Slippi's raw fp+0x221A bit can disagree with the
  // derived internal fp->fall_fast lane, so source-owned collision audits consume this for the
  // reseeded frame only.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
  uint8_t* fall_fast_seed_frame_start;
  uint8_t* fall_fast_seed_frame_start_valid;
  // Hidden mv.co.{fall,fallaerial,fallspecial}.x4 live JObj blend scalar and selected smid.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
  float* common_fall_blend_x4;
  uint16_t* common_fall_blend_msid;
  // Hidden mv.co.squat.x0/x4 platform-pass latch/countdown. ftCo_80099F9C arms it while down is
  // held on a platform; ftCo_Squat_IASA_inline later decrements x4 and enters Pass without
  // rechecking stick down.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline
  uint8_t* squat_pass_x0;
  uint8_t* squat_pass_x4;
  // fp+0x2340 AttackDash lane:
  // - mv.co.attackdash.x0 countdown consumed by ftCo_800D8AE0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
  int16_t* attackdash_x0;  // [batch * players]
  // fp+0x2340 Attack1 lane:
  // - mv.co.attack1.x0 latched intent consumed by checkAttack12/checkAttack13.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
  uint8_t* jab_x0;  // [batch * players], 0/1
  // fp+0x1A54 Attack100 mash counter:
  // - incremented by ftCo_Attack_800D6A50 while A is pressed/released during Attack11/12/13.
  // - compared against co_attrs.rapid_jab_window when x2218_b2 is set.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
  uint8_t* jab_rapid_count;  // [batch * players], clamped to 0..255
  // mv.co.attack100.x4 continue-loop latch, set by Attack100Loop_IASA and consumed by
  // Attack100Loop_Anim's throw_flags_b3 checkpoint.
  // mv.co.attack100.x0 loop-start latch, set by Attack100Loop_Anim before item pickup and
  // checkpoint handling.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_Attack100Loop_IASA,ftCo_Attack100Loop_Anim}
  uint8_t* attack100_x0;  // [batch * players], 0/1
  uint8_t* attack100_x4;  // [batch * players], 0/1
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
  // RunBrake animation-freeze latch (`mv.co.runbrake.x0`).
  // - Reset on RunBrake entry.
  // - ftCo_RunBrake_Anim sets it when cmd_vars[1] freezes the AObj at |gr_vel| >= x42C.
  // - While set, the action resumes only when |gr_vel| <= x42C.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
  //   ftCo_RunBrake_Enter,ftCo_RunBrake_Anim}
  // data/common/ft_common_data.json::runbrake_anim_freeze_speed_threshold
  uint8_t* runbrake_freeze_x0;  // [batch * players], 0=clear, 1=frozen, 3=cmd1 consumed
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
  uint8_t* shine_is_release;
  // Marth-family special machinery (runtime-internal; reconstructed/zeroed at reseed):
  // fp->cmd_vars[0..2] equivalents for char specials, the SpecialN charge counter
  // (mv.ms.specialn.cur_frame), and the Counter-stashed incoming damage (mv.ms.speciallw.x0).
  // refs/melee/src/melee/ft/chara/ftMars/types.h::ftMars_MotionVars
  uint8_t* special_cmd0;
  uint8_t* special_cmd1;
  uint8_t* special_cmd2;
  // Fox/Falco `mv.fx.SpecialN.isBlasterLoop`: IASA sets this hidden latch from cmd_vars[0] + B
  // edge, and the Loop Anim callback consumes it before current-frame input can set a new latch.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,
  //   ftFx_SpecialNLoop_IASA,ftFx_SpecialAirNLoop_IASA}
  uint8_t* specialn_blaster_loop_requested;
  uint16_t* specialn_charge_frames;
  uint16_t* speciallw_countered_damage;
  // Sheik/Zelda special hidden state. Needle count and the generic latch are live runtime lanes in
  // this slice; the Sheik Vanish / Zelda Farore travel timer is seed-reconstructed by
  // sheik_vanish_travel_timer_u8 because Special(Air)HiStart_1 freezes its animation after its
  // explicit entry tick.
  // refs/melee/src/melee/ft/chara/ftSeak/types.h::ftSeak_FighterVars/ftSeak_MotionVars
  // refs/melee/src/melee/ft/chara/ftZelda/types.h::ftZelda_MotionVars
  uint8_t* sheik_needle_count;   // fv.sk.x0, clamped 0..6
  uint8_t* sheik_special_timer;  // mv.sk.special{n,s,hi}.x0 compact timer
  uint8_t* sheik_special_timer_frame_start;
  uint8_t* sheik_special_latch;  // release / per-action latch
  uint8_t* sheik_vanish_smoke_accessory_pending;
  // Hidden Zelda twin fp+0x2218 byte for bounded Sheik/Zelda transform support. Source transform
  // activates the same-player hidden twin through ftCommon_8007EFC8; Slippi only exposes the
  // currently visible fighter, so one-step reseed carries the inactive Zelda twin byte explicitly.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007EFC8
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c::fn_8011412C
  uint8_t* zelda_twin_state_flags_2218;
  float* sheik_chain_pose_angle;  // mv.sk.specials.x18 (ftSk_SpecialS_80110490)
  float* sheik_chain_pose_mag;    // mv.sk.specials.x14 (ftSk_SpecialS_80110490)
  // fp->lstick_angle for special launch tilt (Dolphin Slash); ftMars_FighterVars.x222C
  // air-side-special freshness; FallSpecial mobility/lag overrides from ftCo_80096900 args
  // (0 = use defaults); Counter intercept window (script cmd1; 2 = armed descriptor).
  float* special_stick_angle;
  uint8_t* specials_air_used;
  // Falcon Kick hidden lanes (mv.ca.speciallw): x0 on-hit count, cumulative on-hit slowdown
  // multiplier (Inline_Friction), and the once-per-frame deal_dmg_cb firing flag.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c
  uint8_t* falcon_speciallw_hits;
  float* falcon_speciallw_friction;
  uint8_t* falcon_speciallw_dealt_x1914_frame;
  // Falcon Dive: mv.ca.specialhi.vel carried velocity + attacker x221B_b7 attach-mode flag.
  // grab_constraint_x2226_b2 mirrors the constrained fighter's fp->x2226_b2 bit installed by
  // ftCo_800DB368. Exactly one side of a live Dive hold owns it: Falcon for a grounded victim,
  // CaptureCaptain for an airborne victim.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  float* falcon_specialhi_vel_x;
  float* falcon_specialhi_vel_y;
  uint8_t* falcon_specialhi_x221b_b7;
  uint8_t* grab_constraint_x2226_b2;
  // Raptor Boost: mv.ca.specials.grav accumulator + fp->unk_gobj inert-contact detect flag.
  float* falcon_specials_grav;
  uint8_t* falcon_detect_pending;
  float* fallspecial_mobility_mul;
  uint8_t* speciallw_counter_window;
  // Marth Counter descriptor hitlag floor provenance: Anim creation writes MarsAttributes::x60 to
  // shield_unk0/1, but ground/air swap descriptor recreation only calls ftColl_8007B1B8 and sets
  // x221B_b1. Keep this as hidden source state rather than deriving from action id or descriptor bit.
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{ftMs_SpecialLw_Anim,
  // ftMs_SpecialAirLw_Anim,ftMs_SpecialLw_80138D38,ftMs_SpecialLw_80138DD0}
  uint8_t* speciallw_counter_hitlag_floor_active;
  // Frame-preserving motion transitions without Ft_MF_Unk24 clear fp->x221C_u16_y; opcode-52
  // events at frames <= this floor are suppressed until the next crossing (0 = no floor).
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  uint16_t* x221c_y_event_floor;          // [batch * players], 0/1
  uint8_t* coll_wall_commit_runtime;      // [batch * players], wall contact committed by the live
                                          // collision pass this run (cleared on reseed)
  float* coll_effective_bottom_rel_prev;  // [batch * players], last frame's converged
                                          // CollData ecb.bottom rel (locked-preserved or
                                          // pose; mpCollInterpolateECB converges within
                                          // the frame's substeps)
  uint8_t* coll_effective_bottom_rel_prev_valid;  // [batch * players]
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
  int8_t* ledge_side;                          // [batch * players]
  int8_t* stage_ledge_occupant_left;           // [batch]
  int8_t* stage_ledge_occupant_right;          // [batch]
  uint8_t* ledge_cooldown;                     // [batch * players]
  uint16_t* ledge_drop_floor_skip_segment_id;  // [batch * players], 0xFFFF = none
  // Source cliff ledge floor owner (`mv.co.cliff.ledge_id` / CollData floor owner). Set while
  // CliffCatch/CliffWait/Cliff* state is live and carried through immediate cliff exits until
  // grounded collision consumes or clears the owner.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
  uint16_t* cliff_ledge_floor_segment_id;  // [batch * players], 0xFFFF = none
  // Runtime provenance for the above owner: 1 only when restored from a teacher-forced seed lane,
  // 0 when populated by live CliffCatch/CliffWait runtime ownership.
  uint8_t* cliff_ledge_floor_segment_seeded;  // [batch * players]
  // CliffWait climb/drop stick latch (decomp: fp->mv.co.cliff.x8).
  // - ftCo_8009A804 initializes x8=0 on CliffWait entry.
  // - ftCo_8009AA0C sets x8=1 when no main/c-stick option input is present.
  // - ftCo_8009AAFC admits CliffClimb/drop only after x8 has latched.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{ftCo_8009AA0C,ftCo_8009AAFC}
  uint8_t* cliff_option_stick_latch_x8;  // [batch * players], 0/1
  // Runtime-only CliffWait hang timer (decomp: fp->mv.co.cliff.x4).
  // - ftCo_8009A804 initializes x4 from p_ftCommonData->x48C/x490 on live CliffWait entry.
  // - ftCo_CliffWait_Anim decrements it once per frame before CliffWait_IASA can route timeout.
  // - Direct replay reseeds inside CliffWait do not expose this mv union field, so
  //   cliff_wait_timer_x4_runtime_valid remains 0 until runtime observes a real CliffWait entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::{
  //   ftCo_8009A804,ftCo_CliffWait_Anim,ftCo_8009A9AC}
  float* cliff_wait_timer_x4;                  // [batch * players]
  uint8_t* cliff_wait_timer_x4_runtime_valid;  // [batch * players], 0/1
  // FallSpecial internals (seeded/derived).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
  uint8_t* fallspecial_xc;  // fp->mv.co.fallspecial.xC (arg1 to ftCo_80096900)
  // fp->mv.co.fallspecial.landing_lag, forwarded by FallSpecial_Coll into
  // ftCo_LandingFallSpecial_Enter.
  float* fallspecial_landing_lag;
  // LandingFallSpecial carries mv.co.landing.allow_interrupt from ftCo_LandingFallSpecial_Enter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
  uint8_t* landing_fallspecial_allow_interrupt;
  uint8_t* turn_has_turned;      // fp->mv.co.turn.has_turned (refs/melee/.../ftCo_Turn.c:39-44)
  uint8_t* turn_frames_to_turn;  // fp->mv.co.turn.frames_to_turn (refs/melee/.../ftCo_Turn.c:39-44)
  // Runtime-only one-frame walk physics owner for Wait_IASA rows that only reach Walk on the raw
  // stick lane from ftWalkCommon_800DFC70.
  uint8_t* walk_use_raw_input_once;
  // Runtime-only Dash_Enter marker. Dash can re-enter Dash from Dash_IASA without changing
  // action_id, but ftCo_Dash_Phys still consumes the entry x0 lane on that frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
  //   ftCo_Dash_CheckInput,ftCo_Dash_Enter,ftCo_Dash_Phys}
  uint8_t* dash_entered_this_frame;
  // Turn dash-out latch (decomp: fp->mv.co.turn.x8).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
  int8_t* turn_x8;          // -1/0/+1
  uint8_t* lr_press_timer;  // fp->x67F (refs/melee/src/melee/ft/fighter.c:2078-2086)
  uint8_t*
      x672_input_timer;  // fp->x672_input_timer_counter (refs/melee/src/melee/ft/fighter.c:2020-2050)
  uint8_t*
      x672_input_timer_frame_start;  // transient callback-visible x672 before input.c updates it
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
  // Transient callback-visible copies captured before input.c advances x679/x67A for the current
  // frame. Used by same-frame callbacks that need the previous Fighter_Spaghetti source phase.
  uint8_t* x679_x_frame_start;
  uint8_t* x67A_y_frame_start;
  uint8_t* x67B;  // fp->x67B
  uint8_t* x67C;  // fp->x67C
  uint8_t* x67D;  // fp->x67D
  uint8_t* x67E;  // fp->x67E
  uint8_t* x680;  // fp->x680
  uint8_t* x681;  // fp->x681
  uint8_t* x682;  // fp->x682
  uint8_t* x683;  // fp->x683
  uint8_t* x684;  // fp->x684
  // fp->x686/x68B: up+B input-presence timer pair. x686 = frames since up+B was last
  // PRESENT (held B with stick.y >= p_ftCommonData->x21C), zeroed every present frame;
  // x68B captures the prior gap on the first frame of a present period (the source
  // aerial up-special freshness gate: ftCo_800D69C4 admits x686 == 0 && x68B >= x1C).
  // refs/melee/src/melee/ft/fighter.c (input history block), ftCo_Attack100.c::ftCo_800D6928
  uint8_t* x686;  // fp->x686
  uint8_t* x68B;  // fp->x68B

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
  // Fighter phantom/tip-log delayed damage lane (fp->dmg.x1898 + x189C countdown).
  //
  // Decomp:
  // - ftColl_80076ED8 stores a phantom/tip-log damage amount without entering the full damage/KB
  //   path immediately.
  // - Fighter_ProcessHit starts hitlag and x189C, then ftColl_8007BE3C applies x1898 percent,
  //   stale queue, and combo bookkeeping when x189C expires.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007BE3C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float* phantom_damage_pending_x1898;
  uint16_t* phantom_damage_timer_x189c;
  // Legacy field name: local simulator slot or 0xFF, not raw Slippi/controller source-port domain.
  uint8_t* phantom_damage_source_port;
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
  // Explicit Damage hitlag SDI owner bit (`allow_sdi`, fp+0x221A:2 in decomp comments).
  // Runtime sets this when ProcessHit starts damage hitlag and clears it when hitlag exits.
  // Teacher-forced reseed initializes it from strictly visible ProcessHit provenance; after the
  // seeded frame, free-running gameplay uses this internal lane instead of re-reading Slippi's
  // hitlag-active byte.
  // refs/melee/src/melee/ft/types.h
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  uint8_t* damage_allow_sdi;
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
  // Runtime one-frame owner bit set when ftCo_Damage_OnEveryHitlag consumed a downward SDI input
  // before the collision callback. The x670/x671 tilt timers are reset by that consume before
  // mpColl runs, so collision cannot rediscover this from the post-callback timer values.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  uint8_t* damage_hitlag_downward_sdi_consumed;
  uint16_t* hitstun;
  // Damage jump-buffer snapshot (decomp: fp->mv.co.damage.x14, set from x0 on jump input while in
  // hitstun; used by Damage_Anim inlineC0 gate vs p_ftCommonData->x1D0).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_Anim}
  uint16_t* damage_jump_buffer_x14;  // [batch * players]
  // Meteor-cancel eligibility bit (decomp: fp->mv.co.damage.x1A).
  // Runtime damage entry sets it from the source hit angle before DI can rotate the visible KB
  // vector; teacher-forced reseed unpacks it from MslSeed::damage_post_hitlag_cb_kind bit 7.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AC68
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
  uint8_t* damage_meteor_cancel_eligible_x1a;  // [batch * players]
  // Post-hitlag callback ownership lane (`fp->post_hitlag_cb`).
  // 0 = none, 1 = ftCo_Damage_OnExitHitlag (decomp: ftCo_8008DCE0 sets callback pointer).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  uint8_t* damage_post_hitlag_cb_kind;  // [batch * players]
  // Grounded attacker-on-shield knockback scalar (`fp->xF4_ground_attacker_shield_kb_vel`).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007CE4C,ftCommon_8007E2A4}
  float* attacker_shield_ground_kb_vel;  // [batch * players]
  uint8_t* l_cancel;
  uint8_t* hurtbox_state;
  // Collision hit-status internals (decomp fp->x198C / x1990 / x1994 / x2221_b0).
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B760,ftColl_8007B7A4}
  uint8_t* colanim_hit_status_x198c;  // [batch * players] (0/1/2)
  uint16_t* colanim_timer_x1990;      // [batch * players]
  uint16_t* colanim_timer_x1994;      // [batch * players]
  uint8_t* colanim_lock_x2221_b0;     // [batch * players] (0/1)
  // Runtime ProcessHit provenance for DamageFlyRoll hitlag-exit x1994.
  // Set only by a live ftCo_8008DCE0 DamageFlyRoll entry that starts hitlag; consumed by
  // ftCo_Damage_OnExitHitlag's ftColl_8007B7A4 x1994 producer.
  uint8_t* damageflyroll_runtime_x1994_on_exit;  // [batch * players]
  // Explicit seed-only proof that replay-history extraction saw x198C=1/x1994 under active
  // hitstun while the visible merged hurtbox_state stayed vulnerable. This is consumed only by
  // narrow collision-owner bridges that need the hidden provenance; it must not raise generic
  // BODY hit-status on its own.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  uint8_t* colanim_hitstun_x198c1_seed;  // [batch * players] (0/1)
  // One-frame item BODY ownership for terminal x1990 expiry:
  // Fighter_8006A360 decrements x1990 and may clear visible x198C before the later item BODY pass;
  // ftColl_8007925C still gates BODY on x1988/x198C collision status for the frame's item pass.
  // 0 = none, 1 = terminal x1990/no x1994 guard, 2 = terminal x1990/x1994 carry allows BODY.
  uint8_t* colanim_terminal_x1990_item_body_guard;  // [batch * players] (0/1/2)
  // Pose-driven world-space hurt capsule endpoints (computed each frame in hurtboxes_refresh).
  uint8_t* hurtcap_count;  // [batch * players]
  uint8_t* hurtcap_geometry_valid;
  float* hurtcap_a_x;  // [batch * players * caps]
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
  // Internal HitCapsule state/x4 lane for the x914 lifecycle. This is separate from
  // `hitbox_enabled`, which only means the current frame has collision geometry to test.
  //
  // Decomp ownership:
  // - Fighter_ChangeMotionState skips ftColl_8007AFF8 when Ft_MF_SkipHit is set, so x914 capsule
  //   state and victims_1 can persist into a new motion before the next create command.
  // - ftAction_8007121C only calls ftColl_800768A0 when the slot is disabled or x4 changes.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  uint8_t* hitbox_capsule_enabled;  // [batch * players * MSL_MAX_HITBOXES]
  uint8_t* hitbox_capsule_group;    // [batch * players * MSL_MAX_HITBOXES]
  // First-frame bootstrap flag for x58 previous centers after teacher-forced reseed.
  // 1 => bootstrap hitbox_prev_* once in hitboxes_refresh(), then clear to 0.
  uint8_t* hitbox_prev_bootstrap;  // [batch * players]
  float* hitbox_x;
  float* hitbox_y;
  float* hitbox_z;
  float* hitbox_radius;
  float* hitbox_damage;
  // Source-owned stale multiplier for HitCapsule.damage.
  //
  // Decomp ownership:
  // - ftAction_8007121C / ftAction_8007162C call ftColl_8007ABD0 when create/set-damage commands
  //   fire.
  // - ftColl_8007ABD0 writes HitCapsule.damage after ft_80089228 applies the current stale table.
  // - Later stale-queue inserts from the same live HitCapsule must not retroactively restale it.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_8007121C,ftAction_8007162C}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  uint8_t* hitbox_stale_damage_valid;
  float* hitbox_stale_damage_mul;
  uint16_t* hitbox_bone_part_id;
  uint16_t* hitbox_u16_0;
  uint16_t* hitbox_u16_1;
  uint16_t* hitbox_u16_2;
  uint16_t* hitbox_u16_3;
  uint16_t* hitbox_u16_4;
  uint16_t* hitbox_u16_5;
  uint16_t* hitbox_u16_6;
  uint16_t* hitbox_u16_7;
  // Decoded per-hitbox attributes (from MSLHITB1 u16 tail; see agent_docs/DATA_CONTRACT.md).
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
  // Hidden CollData.floor_skip carry. Pass/shield-drop writes the current platform floor.index via
  // mpUpdateFloorSkip; mpColl floor checks reject that same platform until source clears/overwrites
  // the skip. This is causal runtime state only, not a replay future lane.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::mpUpdateFloorSkip callers
  // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip,mpColl_80044628_Floor}
  uint16_t* floor_skip_segment_id;
  uint32_t* animation_index;
  // Live fighter dynamic-node pose owner (`ftData.x2C` -> `lb_8001044C`).
  //
  // Runtime-only carry state for the dynamic JObj chain that feeds `lb_8000B1CC` collision
  // primitives. It is initialized from extracted descriptors/local SRT at reseed or on
  // non-sequential motion, then updated before BODY hit/hurt primitive refresh.
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CF84,ftCo_8009DD94}
  // refs/melee/src/melee/lb/lb_00F9.c::{lb_8000FD48,lb_80011710,lb_8001044C}
  uint8_t* dynamic_pose_state_valid;             // [batch * players]
  uint8_t* dynamic_pose_apply_collision_matrix;  // [batch * players]
  uint8_t* dynamic_pose_node_count;              // [batch * players]
  uint8_t* dynamic_pose_char_id;                 // [batch * players]
  uint16_t* dynamic_pose_msid;                   // [batch * players]
  uint16_t* dynamic_pose_frame;                  // [batch * players]
  float* dynamic_pose_rot_x;                     // [batch * players * MSL_MAX_DYNAMIC_NODES]
  float* dynamic_pose_rot_y;
  float* dynamic_pose_rot_z;
  float* dynamic_pose_pos_x;
  float* dynamic_pose_pos_y;
  float* dynamic_pose_pos_z;
  float* dynamic_pose_axis_x;
  float* dynamic_pose_axis_y;
  float* dynamic_pose_axis_z;
  float* dynamic_pose_angle;
  uint16_t* instance_hit_by;
  uint16_t* instance_id;
  // Internal-only: low 8 bits of fp->x2070 (the byte at fp+0x2073) used by ft_800895E0 to decide
  // whether to bump fp->x2088 on motion-state change.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz fp+0x2073; compare to flags)
  uint8_t* instance_id_x2073;  // [batch * players]
  // Replay-facing same-frame fighter-proc order lane for plAttack_80037B08 consumers.
  // 0=no override; nonzero is the post-entry fp->x2088 instance_id.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
  uint16_t* motion_entry_instance_id_override;  // [batch * players]
  // Internal-only: last action_id for which instance_id update logic ran.
  // Used to avoid bumping fp->x2088 on animation restarts (msl_anim_timebase_enter without
  // a motion-state change).
  uint16_t* instance_identity_last_action_id;  // [batch * players]
  uint16_t* attack_id;  // GALE01 fp->x2068_attackID (seeded; replay-history derived)
  uint16_t* attack_instance;
  // Internal-only frame-start submotion. Same-callback item/fighter collision can consume the
  // source collision pose after a later action publication has already replaced animation_index.
  uint32_t* frame_start_animation_index;  // [batch * players]
  // Internal-only frame-start copy of x2068/x206C. Item spawn callbacks use this to recover the
  // live Blaster Loop attack identity when the simulator has already applied a same-frame
  // motion-state exit before spawning the shot article.
  uint16_t* frame_start_attack_id;        // [batch * players]
  uint16_t* frame_start_attack_instance;  // [batch * players]
  // Internal-only frame-start fighter instance (fp+0x2070 union-as-int low half in Slippi terms).
  // Same-frame reciprocal BODY hits can leave a frame-start HitCapsule live after the attacker has
  // already entered Damage*; collision source attribution still points at the pre-entry fighter.
  uint16_t* frame_start_instance_id;  // [batch * players]
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
  uint16_t* combo_push_timer_x2092;
  // Raw Slippi 0-based controller port for each local sim slot. Source-owner compare lanes
  // (`last_hit_by`) are recorded in this raw-port domain, while gameplay ownership keeps local
  // slot indices.
  uint8_t* source_port0;
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
  // Teacher-forced per-HitCapsule shield-contact tri-state.
  // Layout: [batch * MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS]
  uint8_t* combat_shield_contact_hb_kind;
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
  // Item HitCapsule.damage stale scalar captured when it_80272460 builds the item hitbox.
  // Slippi does not expose this float lane; runtime-created projectiles keep it source-owned here
  // so later stale queue writes do not retroactively change already-created item HitCapsules.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  uint8_t* item_stale_damage_valid;  // [batch * MSL_MAX_ITEMS]
  float* item_stale_damage_mul;      // [batch * MSL_MAX_ITEMS]
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
  // Runtime-only reflected BODY damage attribution. Slippi keeps item->xD88/xD8C spawn-latched,
  // but Item_80269F14 rebuilds the reflected HitCapsule damage product under the reflector's
  // current attack identity before a later BODY hit consumes it.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  uint8_t* item_reflect_body_owner_port;        // [batch * MSL_MAX_ITEMS], 0xFF = none
  uint16_t* item_reflect_body_attack_id;        // [batch * MSL_MAX_ITEMS]
  uint16_t* item_reflect_body_attack_instance;  // [batch * MSL_MAX_ITEMS]
  uint8_t* item_reflect_body_damage_valid;      // [batch * MSL_MAX_ITEMS]
  float* item_timer;                            // [batch * MSL_MAX_ITEMS]
  uint8_t* item_hitlag;                         // [batch * MSL_MAX_ITEMS]
  uint32_t* item_spawn_id;                      // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc0;                          // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc1;
  uint8_t* item_misc2;
  uint8_t* item_misc3;
  // Hidden item reflect snapshot owner copied by ftColl_80077464 and consumed by Item_80269F14.
  // This is runtime-only state; Slippi item misc bytes stay replay-visible item fields.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  uint8_t* item_pending_reflect_owner_port;    // [batch * MSL_MAX_ITEMS], 0xFF = none
  uint16_t* item_pending_reflect_instance_id;  // [batch * MSL_MAX_ITEMS], item->xC8C
  uint8_t* item_reflect_transfer_seed_port;    // [batch * MSL_MAX_ITEMS], 0xFE known none
  uint16_t* item_reflect_transfer_seed_iid;    // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shield_bounce_seed_valid;      // [batch * MSL_MAX_ITEMS]
  float* item_shield_bounce_seed_vel_x;        // [batch * MSL_MAX_ITEMS]
  float* item_shield_bounce_seed_vel_y;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_hidden_body_hit_victim_port;   // [batch * MSL_MAX_ITEMS], 0xFF = none
  uint8_t* item_hidden_body_hit_hurt_height;   // [batch * MSL_MAX_ITEMS]
  uint8_t* item_hidden_callback_flags;         // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_callback_bounce_vel_y_index;       // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_callback_bounce_vel_x_index_sign;  // [batch * MSL_MAX_ITEMS]
  // One-step/replay seed bridge for SetupBounce's hidden xDDC/xDE0 samples on the same source
  // Logic109 callback that publishes a bounced Needle. This does not alter free-running gameplay:
  // live callbacks still consume the source RNG sites, and these lanes are consumed/cleared with
  // item_hidden_callback_flags in the matching item step.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   it_2725_Logic109_DmgDealt,it_2725_Logic109_DmgReceived,it_2725_Logic109_HitShield,
  //   itSeakNeedleThrown_SetupBounce}
  uint8_t* item_sheik_needle_callback_bounce_motion_valid;     // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_callback_bounce_gravity_index;    // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_callback_bounce_min_vel_y_index;  // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_stage_hit_seed_kind;              // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_stage_hit_vel_y_index;            // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_needle_stage_hit_vel_x_index_sign;       // [batch * MSL_MAX_ITEMS]
  // Sheik take-damage-dropped Needle hidden itemVar motion lanes (xDDC terminal min-y, xDE0
  // gravity). Live ftSk_SpecialN_80111FBC drops seed these from itSeakNeedleThrown_SetupDrop; replay
  // seeds of pre-existing state-1/4 Needles leave valid=0 and use visible-velocity reconstruction.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   itSeakNeedleThrown_SetupDrop,itSeakneedlethrown_UnkMotion1_Phys}
  uint8_t* item_sheik_needle_hidden_drop_valid;    // [batch * MSL_MAX_ITEMS]
  float* item_sheik_needle_hidden_drop_min_vel_y;  // [batch * MSL_MAX_ITEMS]
  float* item_sheik_needle_hidden_drop_gravity;    // [batch * MSL_MAX_ITEMS]
  float* item_sheik_needle_hidden_drop_vel_x;      // [batch * MSL_MAX_ITEMS]
  // Zelda Din's Fire itemVar state. The fire article uses xDD8/xDDC/xDE8/xDEC/xDF0; the explosion
  // article uses xDD4 and xDD8 for charge and base hitbox size.
  // refs/melee/src/melee/it/items/itzeldadinfire.c
  // refs/melee/src/melee/it/items/itzeldadinfireexplode.c
  float* item_zelda_din_charge;             // [batch * MSL_MAX_ITEMS]
  float* item_zelda_din_angle_offset;       // [batch * MSL_MAX_ITEMS]
  float* item_zelda_din_base_angle;         // [batch * MSL_MAX_ITEMS]
  float* item_zelda_din_speed;              // [batch * MSL_MAX_ITEMS]
  float* item_zelda_din_explode_base_size;  // [batch * MSL_MAX_ITEMS]
  // Sheik Side-B Chain Verlet link state (runtime-only, not serialized; reset on reseed).
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BAF2C
  uint8_t* item_sheik_chain_links_valid;      // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_chain_link_active;      // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  uint8_t* item_sheik_chain_hitbox_link_idx;  // [batch * MSL_MAX_ITEMS * MSL_MAX_HITBOXES]
  uint8_t* item_sheik_chain_hitcaps_active;   // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_chain_hit_cooldown;     // [batch * MSL_MAX_ITEMS] (mv.sk.specials.x1C)
  uint8_t* item_sheik_chain_hit_grace;        // [batch * MSL_MAX_ITEMS] (mv.sk.specials.x20)
  uint8_t* item_sheik_chain_hit_reset_prev;   // [batch * MSL_MAX_ITEMS] (ZeroHitboxPositions edge)
  uint8_t* item_sheik_chain_hit_prev_valid;   // [batch * MSL_MAX_ITEMS]
  uint8_t* item_sheik_chain_stale_damage_valid;  // [batch * MSL_MAX_ITEMS]
  float* item_sheik_chain_stale_damage_mul;      // [batch * MSL_MAX_ITEMS]
  uint32_t* item_sheik_chain_env_flags;          // [batch * MSL_MAX_ITEMS] (seakchain.x10)
  float* item_sheik_chain_hit_prev_x;            // [batch * MSL_MAX_ITEMS * MSL_MAX_HITBOXES]
  float* item_sheik_chain_hit_prev_y;            // [batch * MSL_MAX_ITEMS * MSL_MAX_HITBOXES]
  uint8_t* item_sheik_chain_target_valid;        // [batch * MSL_MAX_ITEMS]
  float* item_sheik_chain_target_x;              // [batch * MSL_MAX_ITEMS]
  float* item_sheik_chain_target_y;              // [batch * MSL_MAX_ITEMS]
  float* item_sheik_chain_target_z;              // [batch * MSL_MAX_ITEMS]
  float* item_sheik_chain_link_pos_x;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_link_pos_y;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_link_pos_z;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_link_vel_x;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_link_vel_y;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_link_vel_z;    // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_MAX_LINKS]
  float* item_sheik_chain_history_x;     // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_HISTORY_LEN]
  float* item_sheik_chain_history_y;     // [batch * MSL_MAX_ITEMS * MSL_SHEIK_CHAIN_HISTORY_LEN]
  float* item_sheik_chain_prev_stick_x;  // [batch * MSL_MAX_ITEMS] (owner lstick1 analogue)
  float* item_sheik_chain_prev_stick_y;  // [batch * MSL_MAX_ITEMS]
  // Prefix-causal Shy Guy dynamic-bone velocity scratch.
  // refs/melee/src/melee/it/items/itheiho.c::it_802D98C4
  float* item_shyguy_prev_vel_y;           // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_prev_vel_y_valid;   // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_dyn_y_phase;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_dyn_y_phase_valid;  // [batch * MSL_MAX_ITEMS]
  // Shy Guy itemVar internals.
  // refs/melee/src/melee/it/items/itheiho.c::{
  //   it_802D8618,itHeiho_UnkMotion0_Phys,itHeiho_UnkMotion3_Phys,itHeiho_UnkMotion*_Coll}
  // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_802697D4}
  uint8_t* item_shyguy_speed_index;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_speed_index_valid;  // [batch * MSL_MAX_ITEMS]
  uint16_t* item_shyguy_delay;             // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_delay_valid;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_hitlag;             // [batch * MSL_MAX_ITEMS]
  uint8_t* item_shyguy_hitlag_valid;       // [batch * MSL_MAX_ITEMS]

  // Item hitbox victim rings (HitCapsule victim lists per item slot and hitbox).
  // Decomp anchor (tick): refs/melee/src/melee/it/itcoll.c::it_8027146C
  // Layout: [batch * MSL_MAX_ITEMS * MSL_MAX_HITBOXES]
  MslHitlistCapsule* item_hitlist;
} MslStateSoA;

int state_alloc(MslStateSoA* state, int batch_size);
int state_copy_lanes(MslStateSoA* dst, const MslStateSoA* src, const int32_t* dst_lanes,
                     const int32_t* src_lanes, int32_t count, int32_t dst_batch_size,
                     int32_t src_batch_size);
void state_free(MslStateSoA* state);
