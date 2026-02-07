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
  uint8_t* on_ground;
  uint8_t* prev_on_ground;  // on_ground value before stage_collision_apply().
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
  // Previous frame's action_id (captured at step start; internal-only).
  // Used for transition-based mechanics that depend on (t -> t+1) action changes while keeping
  // the seed schema minimal for one-step reseeding.
  uint16_t* prev_action_id;
  int16_t* action_frame;
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
  uint8_t* grab_owner_port;  // [batch * players]
  float* grab_offset_y;      // [batch * players]
  float* grab_offset_z;      // [batch * players]
  uint8_t* match_flow_timer;
  int16_t* downwait_timer;  // fp->mv.co.downwait.x0 (seeded; decomp: ftCo_DownWait_Anim)
  float* anim_frame_f32;    // decomp fp->cur_anim_frame (float; Slippi `state_age`)
  // Decomp-shaped internal animation/script timebase with deterministic fractional carry.
  // - anim_frame_fp_q16_16 mirrors fp->cur_anim_frame (float) as signed Q16.16 fixed-point.
  // - frame_speed_mul_fp_q16_16 mirrors fp->frame_speed_mul (float) as signed Q16.16 fixed-point.
  // refs/melee/src/melee/ft/fighter.c (cur_anim_frame, frame_speed_mul init / Fighter_ChangeMotionState)
  // refs/melee/src/melee/ft/ftanim.c (ftAnim_8006F0FC / ftAnim_SetAnimRate)
  int32_t* anim_frame_fp_q16_16;
  int32_t* frame_speed_mul_fp_q16_16;
  // Transient: defer a single ftAnim_8006EBA4-shaped timebase tick until after combat resolves.
  //
  // Rationale: some motion-state entry paths in decomp call ftAnim_8006EBA4 immediately after
  // Fighter_ChangeMotionState (e.g. Shine/Blaster/AttackAir enter). Hits can occur during that
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
  // GuardReflect reflect timer (decomp: mv.co.guard.x14; seed uses +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x14;  // [batch * players]
  // GuardReflect powershield-active timer (decomp: mv.co.guard.x18; +1 bias, expires at 0).
  uint8_t* guard_reflect_timer_x18;  // [batch * players]
  // Guard release lockout + shield-drain latch (seeded; decomp-shaped).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC and ::ftCo_800925A4.
  uint8_t* guard_release_latched_xc;  // mv.co.guard.xC (0/1)
  uint8_t* guard_x10;                 // mv.co.guard.x10 (frames remaining; clamped to 0..255)
  float* lightshield_amount;          // fp->lightshield_amount (0..1)
  // Locomotion/input-history internals.
  // - `tilt_timer_*`, `turn_*`, and KneeBend internals are seeded from replay history (MslSeed).
  uint8_t*
      kneebend_jump_input;  // ftCo_JumpInput (refs/melee/src/melee/ft/chara/ftCommon/forward.h)
  uint8_t* kneebend_is_short_hop;  // latched during KneeBend IASA (ftCo_KneeBend_Check_ShortHop)
  uint8_t* tilt_timer_x;  // fp->x670_timer_lstick_tilt_x (refs/melee/src/melee/ft/fighter.c)
  uint8_t* tilt_timer_y;  // fp->x671_timer_lstick_tilt_y (refs/melee/src/melee/ft/fighter.c)
  uint8_t* fall_fast;     // fp->fall_fast (refs/melee/src/melee/ft/ftcommon.c:505-520)
  // Run IASA lockout countdown (decomp: fp->mv.co.run.x0).
  // - Decremented in Run_Anim.
  // - Gates TurnRun/RunBrake in Run_IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_Anim,ftCo_Run_IASA}
  uint8_t* run_x0;  // [batch * players], clamped to 0..255
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
  uint8_t* x677_y;  // fp->x677_y
  uint8_t* x678;    // fp->x678
  uint8_t* x679_x;  // fp->x679_x
  uint8_t* x67A_y;  // fp->x67A_y
  uint8_t* x67B;    // fp->x67B
  uint8_t* x67C;    // fp->x67C
  uint8_t* x67D;    // fp->x67D
  uint8_t* x67E;    // fp->x67E
  uint8_t* x680;    // fp->x680
  uint8_t* x681;    // fp->x681
  uint8_t* x682;    // fp->x682
  uint8_t* x683;    // fp->x683
  uint8_t* x684;    // fp->x684

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
  uint16_t* hitstun;
  // Damage jump-buffer snapshot (decomp: fp->mv.co.damage.x14, set from x0 on jump input while in
  // hitstun; used by Damage_Anim inlineC0 gate vs p_ftCommonData->x1D0).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_Anim}
  uint16_t* damage_jump_buffer_x14;  // [batch * players]
  uint8_t* l_cancel;
  uint8_t* hurtbox_state;
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
  // - The seed schema carries a dense (attacker, hit_group, victim_port) cooldown map.
  // - Runtime combat consumes decomp-shaped per-hitbox victim rings (fighter_hitlist).
  // - On first use after reseed, active hitboxes materialize `victims_1` from this dense map.
  //
  // IMPORTANT (rollout contract, v1):
  // - `combat_hitlist_{cd,victim_iid}` are treated as seed-only inputs and are not maintained during
  //   rollouts. The authoritative runtime state lives in `fighter_hitlist` / `item_hitlist`.
  // - Serializing hitlist state mid-rollout (e.g., exporting to the dense map) is not supported yet.
  //   TODO: add an explicit export path if/when rollout save-states are needed.
  //
  // Dense map layout: [batch * MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS]
  uint16_t* combat_hitlist_cd;
  uint16_t* combat_hitlist_victim_iid;
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
