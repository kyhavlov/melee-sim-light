#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MSL_MAX_PLAYERS = 4 };
enum { MSL_MAX_ITEMS = 15 };
enum { MSL_MAX_HURTCAPS = 32 };
enum { MSL_MAX_HITBOXES = 4 };
// Current target-domain fighter dynamic chains. Fox's ftData.x2C chain has four nodes; Falco has
// no fighter dynamics in the extracted target data. Keep fixed capacity for allocation-free
// runtime ownership while leaving the data loader schema able to reject larger unsupported chains.
enum { MSL_MAX_DYNAMIC_NODES = 4 };
enum { MSL_STATE_FLAGS_BYTES = 5 };
// Decomp: `spawn_hitbox_0.hit_group` is a 3-bit field (0..7).
// refs/melee/src/melee/lb/types.h::spawn_hitbox_0
enum { MSL_HITLIST_GROUPS = 8 };
// Decomp: Player's `StaleMoveTable.StaleMoves[10]` ring buffer (current_index wraps at 9).
// refs/melee/src/melee/pl/types.h::StaleMoveTable and refs/melee/src/melee/pl/plstale.c
enum { MSL_STALE_QUEUE_SIZE = 10 };
// Seeded post-hitlag callback kind lane (`fp->post_hitlag_cb` ownership).
enum {
  MSL_DAMAGE_POST_HITLAG_CB_NONE = 0,
  MSL_DAMAGE_POST_HITLAG_CB_DAMAGE_ON_EXIT = 1,
};

// -----------------------------
// Packed on-disk / wire formats
// -----------------------------
// Note: these structs are designed for stable serialization and C<->Python FFI.
// They are not necessarily the optimal in-memory layout (the simulator core uses SoA).

#pragma pack(push, 1)

typedef struct MslInputPlayer {
  // Bitmask of digital buttons. (Mapping is defined in tooling; keep stable.)
  uint16_t buttons;
  // Raw stick values (e.g., -128..127). Tooling defines exact conventions.
  int8_t main_x;
  int8_t main_y;
  int8_t c_x;
  int8_t c_y;
  uint8_t l;
  uint8_t r;
} MslInputPlayer;

typedef struct MslInput {
  MslInputPlayer p[MSL_MAX_PLAYERS];
} MslInput;

typedef struct MslMatchPlayerConfig {
  // GALE01/Slippi external character id. v1 validates Fox=1 / Falco=22.
  uint8_t char_id;
  uint8_t team_id;
  // 0 = facing left, 1 = facing right. Match-start facing is caller-owned until the stage
  // extraction exposes a decomp-backed per-spawn facing lane.
  uint8_t facing;
  uint8_t _pad0;
} MslMatchPlayerConfig;

typedef struct MslMatchConfig {
  // GALE01/Slippi stage id. v1 validates Final Destination=32.
  uint32_t stage_id;
  int32_t frame_id;
  uint32_t frame_pre_random_seed;
  // Global match damage ratio (decomp: gm_8016B248 -> StartMeleeRules.x30).
  float match_damage_ratio;

  uint8_t num_players;  // Must match the batch config for v1.
  uint8_t is_teams;
  uint8_t stock_count;
  uint8_t _pad0;

  MslMatchPlayerConfig players[MSL_MAX_PLAYERS];
} MslMatchConfig;

typedef struct MslRlPlayerObservation {
  // present=1 for an active source player in this slot. team_relation: 0=self, 1=ally,
  // 2=opponent. Inactive/unused slots are zero-filled with present=0.
  uint8_t present;
  uint8_t source_player;
  uint8_t team_relation;
  uint8_t team_id;

  float pos_x;
  float pos_y;
  float speed_air_x_self;
  float speed_ground_x_self;
  float speed_y_self;
  float speed_x_attack;
  float speed_y_attack;
  float percent;
  float shield_hp;

  uint16_t action_id;
  int16_t action_frame;
  uint16_t hitlag;
  uint16_t hitstun;

  uint8_t char_id;
  uint8_t stocks;
  uint8_t facing;
  uint8_t on_ground;
  uint8_t jumps_left;
  uint8_t hurtbox_state;
  uint8_t _pad0[2];
} MslRlPlayerObservation;

typedef struct MslRlObservation {
  int32_t frame_id;
  uint32_t frame_pre_random_seed;
  uint32_t stage_id;
  uint8_t num_players;
  // 0-based player index. slots[0] is self; then allies by ascending source player index,
  // then opponents by ascending source player index; remaining slots are present=0.
  uint8_t viewpoint_player;
  uint8_t is_teams;
  uint8_t _pad0;

  MslRlPlayerObservation slots[MSL_MAX_PLAYERS];
} MslRlObservation;

typedef struct MslTerminal {
  int32_t frame_id;
  uint32_t stage_id;
  uint8_t done;
  uint8_t match_ended;
  uint8_t stockout;
  uint8_t max_frame_reached;
  uint8_t alive_count;
  uint8_t alive_team_count;
  uint8_t team_alive_mask;
  uint8_t _pad0;
} MslTerminal;

typedef struct MslProcessedInputPlayer {
  // Same layout as MslInputPlayer, but stick axes are post-processed (clamped/UCF snapped).
  uint16_t buttons;
  int8_t main_x;
  int8_t main_y;
  int8_t c_x;
  int8_t c_y;
  uint8_t l;
  uint8_t r;
} MslProcessedInputPlayer;

typedef struct MslProcessedInput {
  MslProcessedInputPlayer p[MSL_MAX_PLAYERS];
} MslProcessedInput;

typedef struct MslItem {
  uint8_t exists;  // 0/1
  uint8_t state;   // item state
  uint16_t type;   // item kind/type id

  int8_t owner;  // -1 if none/unknown
  uint8_t _pad0;
  uint16_t instance_id;

  // Item staling identity (GALE01):
  // - it->xD88_attackID
  // - it->xD8C_attack_instance
  // refs/melee/src/melee/it/types.h
  uint16_t attack_id;
  uint16_t attack_instance;

  float direction;
  float vel_x;
  float vel_y;
  float pos_x;
  float pos_y;
  uint16_t damage;
  uint16_t _pad1;
  float timer;
  uint32_t spawn_id;
  uint8_t misc0;
  uint8_t misc1;
  uint8_t misc2;
  uint8_t misc3;
} MslItem;

typedef struct MslSeed {
  int32_t frame_id;
  uint32_t
      frame_pre_random_seed;  // pre-frame RNG seed (per-player seeds also exist; this is frame-level)

  uint32_t stage_id;
  // Global match damage ratio (decomp: gm_8016B248 -> StartMeleeRules.x30).
  //
  // IMPORTANT: In this simulator, this field currently exists only to model the *knockback* multiplier
  // chain feeding ftColl_80079AB0. It is not applied to percent add (or any other damage/percent scaling)
  // elsewhere in the engine, if such scaling exists.
  // refs/melee/src/melee/gm/gm_16AE.c::gm_8016B248
  float match_damage_ratio;
  uint8_t num_players;  // 2 or 4 (<= MSL_MAX_PLAYERS)
  uint8_t is_teams;     // 0/1
  uint8_t _pad0[2];

  uint8_t team_id[MSL_MAX_PLAYERS];
  uint8_t char_id[MSL_MAX_PLAYERS];
  // Player handicap for ftCo_800DA824's grab-timer formula.
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DA824
  // Replay source: game-start player block (`players[*].handicap`); default rules use 9.
  uint8_t handicap[MSL_MAX_PLAYERS];
  // Per-player damage ratios (decomp: Player_GetAttackRatio / Player_GetDefenseRatio).
  // refs/melee/src/melee/pl/player.c::Player_GetAttackRatio
  // refs/melee/src/melee/pl/player.c::Player_GetDefenseRatio
  float attack_ratio[MSL_MAX_PLAYERS];
  float defense_ratio[MSL_MAX_PLAYERS];

  // Kinematics
  float pos_x[MSL_MAX_PLAYERS];
  float pos_y[MSL_MAX_PLAYERS];
  float pos_z[MSL_MAX_PLAYERS];
  // Teacher-forced mpColl floor-sweep previous position.
  //
  // Decomp:
  // - CollData carries prev_pos/cur_pos through mpColl_80043754.
  // - Floor collision checks such as mpCheckFloor consume the segment from prev_pos to cur_pos,
  //   not just the current post-frame position exposed by Slippi.
  //
  // Normal rollouts use the frame-start live position. One-step reseeds set valid=1 and seed the
  // previous replay post-frame position so DamageFly/Fall floor contact can reproduce the engine's
  // sweep. Active-hitlag horizontal-only floor-height rows must still remain airborne unless their
  // decomp callback raises floor contact; see the floorhug negative replay locks.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCheckFloor}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  float floor_sweep_prev_pos_x_f32[MSL_MAX_PLAYERS];
  float floor_sweep_prev_pos_y_f32[MSL_MAX_PLAYERS];
  uint8_t floor_sweep_prev_pos_valid_u8[MSL_MAX_PLAYERS];
  // Velocities as recorded by Slippi post-frame (when available).
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];
  // Hidden Firefox/Firebird `mv.fx.SpecialHi.rotateModel` seed lane.
  //
  // Decomp ownership:
  // - ftFx_SpecialAirHi_Enter writes rotateModel from the launch stick/self_vel direction.
  // - ftFx_SpecialAirHi_Phys reuses the stored rotateModel for reverse acceleration.
  // - ftFx_SpecialAirHi_Coll can rewrite facing and recompute rotateModel from current self_vel.
  // - SpecialHiLanding/Fall/Bound callbacks do not rewrite FtPart_XRotN, so the live pose can
  //   persist into those followups until a non-Firefox motion state owns the model.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Phys,
  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiLanding_Anim,ftFx_SpecialHiFall_Anim,
  //   ftFx_SpecialHiBound_Enter}
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - valid=1: current row is inside a continuous SpecialHi/AirHi/Landing/Fall/Bound pose episode,
  //   and the value is the last source launch/collision rotateModel reconstructed from replay
  //   history.
  // - valid=0: reseed falls back to current visible self_vel/facing.
  float specialhi_rotate_model_f32[MSL_MAX_PLAYERS];
  uint8_t specialhi_rotate_model_valid_u8[MSL_MAX_PLAYERS];
  // Fighter model scale (decomp: fp->x34_scale.y). Slippi exposes this from the game-start block
  // (player.model_scale), but legacy datasets may still default it to 1.0.
  float fighter_scale_y[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];  // 0/1
  // Motion-state facing lane (decomp: fp->facing_dir1).
  //
  // Decomp:
  // - Fighter_UnkInitReset and Fighter_ChangeMotionState copy fp->facing_dir into fp->facing_dir1.
  //   refs/melee/src/melee/ft/fighter.c::{Fighter_UnkInitReset_80067C98,Fighter_ChangeMotionState}
  // - Escape/roll root-motion helper ft_80085030 consumes fp->facing_dir1.
  //   refs/melee/src/melee/ft/ft_081B.c::ft_80085030
  //
  // Seed representation:
  // - Signed lane in {-1,+1}; 0 is treated as invalid and sanitized to sign(facing) at reseed.
  int8_t facing_dir1[MSL_MAX_PLAYERS];
  // Ground friction multiplier lane consumed by grounded knockback decay.
  //
  // Decomp:
  // - Fighter_procUpdate grounded-KB decay uses:
  //     ft_GetGroundFrictionMultiplier(fp) * fp->co_attrs.gr_friction * p_ftCommonData->x200
  //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //   refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
  //
  // Seed representation:
  // - Positive scalar; non-positive values are sanitized to 1.0f at reseed.
  float ground_friction_mul[MSL_MAX_PLAYERS];
  // Smash-charge knockback multiplier gate (decomp: fp->smash_attrs.state == SmashState_Charging).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  uint8_t kb_smashcharge_active[MSL_MAX_PLAYERS];
  uint8_t on_ground[MSL_MAX_PLAYERS];  // 0/1
  uint8_t _pad1[1];

  // State machine
  uint16_t action_id[MSL_MAX_PLAYERS];    // GALE01 action id
  int16_t action_frame[MSL_MAX_PLAYERS];  // action frame (can be negative in pre-start)
  // Replay-true previous action snapshot (t-1 -> t), separate from the runtime step cache so
  // entry-shaped one-step rows can still recover their source motion state.
  uint16_t seed_prev_action_id[MSL_MAX_PLAYERS];
  int16_t seed_prev_action_frame[MSL_MAX_PLAYERS];
  // Hidden Fox/Falco side-special ghost article position lanes
  // (`mv.fx.SpecialS.ghostEffectPos[0..2]`).
  //
  // Decomp ownership:
  // - itFoxillusion_UnkMotion{0,1}_Phys copies the item position from
  //   ftFx_SpecialS_CopyGhostPosIndexed(index=1).
  // - ftFox_SpecialS_SetPhys advances that ring during SpecialS/SpecialAirS/SpecialSEnd/
  //   SpecialAirSEnd Phys callbacks as
  //     `ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos`.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{
  //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys
  // }
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_CopyGhostPosIndexed,ftFox_SpecialS_SetPhys,
  //   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys
  // }
  float illusion_ghost_pos0_x[MSL_MAX_PLAYERS];
  float illusion_ghost_pos0_y[MSL_MAX_PLAYERS];
  float illusion_ghost_pos1_x[MSL_MAX_PLAYERS];
  float illusion_ghost_pos1_y[MSL_MAX_PLAYERS];
  float illusion_ghost_pos2_x[MSL_MAX_PLAYERS];
  float illusion_ghost_pos2_y[MSL_MAX_PLAYERS];
  // Throw projectile pulse-consume seed lane (causal producer; one-step seed ownership).
  //
  // Decomp ownership:
  // - Throw-side blaster shots are one-shot throw_flags_b0 script pulses consumed in
  //   ftFx_Throw_Anim via ftAction command processing.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no throw-pulse stale-latch suppression for this one-step seed row.
  // - 1: pulse was already consumed for this seed-owned throw context (suppress reconstruction).
  //
  // Derivation uses only replay-causal lanes + extracted move/character data:
  // - seed anim_frame_f32 + frame_speed_mul_f32 (upcoming throw pulse crossing)
  // - throw pulse frames from data/moves/{fox,falco}.json
  // - owner shot itkind from data/characters/{fox,falco}.json
  // - current seed items owner/type windows
  uint8_t throw_pulse_consumed[MSL_MAX_PLAYERS];
  // Throw pulse crossing lane for the *previous* replay step (strictly causal).
  //
  // Decomp ownership:
  // - Throw-side projectile pulses are script one-shots in throw_flags_b0, consumed in
  //   ftFx_Throw_Anim.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no projectile pulse crossing in (t-1 -> t) for this seed row.
  // - N: crossed pulse frame number (u8) from data/moves/{fox,falco}.json throw events.
  uint8_t throw_pulse_crossed_prev_frame[MSL_MAX_PLAYERS];
  // Throw command cursor pending pulse for the current teacher-forced step.
  //
  // Decomp ownership:
  // - ftAction_80073354 subtracts frame_speed_mul from the command timer, executes command events
  //   when their timer reaches <=0, and clears `throw_flags` before processing the command list.
  // - ftFx_Throw_Anim then consumes at most one bool `throw_flags_b0` in that Anim callback.
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no command-timer `set_throw_spawn_projectile` pulse should be emitted this step.
  // - N: the command pulse frame that should become the single `throw_flags_b0` consume this step.
  // This is prefix-causal over replay history and extracted command timing.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  uint8_t throw_command_pending_pulse_frame[MSL_MAX_PLAYERS];
  // Source-owner clear countdown (`fp->dmg.x18C8`) with +1 bias.
  //
  // Decomp ownership:
  // - Fighter_ChangeMotionState seeds `dmg.x18C8 = p_ftCommonData->x814` when:
  //     grounded && new_motion_state->x9_b1 && dmg.x18C8 == -1
  // - Fighter_8006A360 decrements `dmg.x18C8` under !hitlag and clears source owner
  //   (`dmg.x18C4_source_ply = 6`) when it reaches -1.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
  // refs/melee/src/melee/ft/types.h::MotionState (x9_b1)
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  //
  // Seed representation:
  // - 0: inactive (decomp internal is -1)
  // - N>0: decomp internal countdown value + 1
  uint8_t source_clear_timer_x18c8[MSL_MAX_PLAYERS];
  // Source-owner set phase lane for active x18C8 runs.
  //
  // Causal ownership model:
  // - Slippi `last_hit_by` mirrors `dmg.x18C4_source_ply`.
  // - Mark an x18C8 run as phase-backed only when a source-owner acquire edge
  //   (6 -> owner) has been observed in the causal prefix before that run.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  //
  // Seed representation:
  // - 0: active run has no observed owner-set edge backing.
  // - 1: active run is backed by owner-set edge context.
  uint8_t source_clear_owner_set_phase[MSL_MAX_PLAYERS];
  // One-step hidden ProcessHit damage-pending source-owner clear bridge.
  //
  // Decomp ownership context:
  // - Fighter_ProcessHit can route source-owner clear through ftCommon_800804FC before the next
  //   post-frame snapshot on grounded damage-pending rows.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no ProcessHit-owned clear override on this row.
  // - 1: consume ProcessHit-owned clear before x18C8 decrement for this one-step row.
  uint8_t source_clear_processhit_damage_pending_phase[MSL_MAX_PLAYERS];
  // Explicit Fighter_8006CDA4 pre-gate RNG stream-phase seed lane for DamageFlyRoll entry.
  //
  // Decomp ownership context:
  // - Fighter_8006CDA4 runs before the HSD_Randf DamageFlyRoll gate in ftCo_8008DCE0 and can
  //   advance the global RNG stream via HSD_Randi consumes.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/types.h
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no seeded pre-gate stream ownership.
  // - 1: consume one pre-gate HSD_Randi before the DamageFlyRoll gate.
  // - 2: consume two pre-gate HSD_Randi calls before the DamageFlyRoll gate.
  // - 3: consume all three decomp-visible pre-gate HSD_Randi calls before the DamageFlyRoll gate.
  // - 4: source-proven zero-consume gate; admit the gate without a pre-gate stream advance.
  // Nonzero DamageFlyTop values may carry across the same segment as hidden held-item/x197C state.
  uint8_t fighter_8006cda4_pre_gate_consume_count[MSL_MAX_PLAYERS];
  // Grounded damage-clear phase bridge for source-owner clear (`ftCommon_800804FC` path).
  //
  // Decomp ownership context:
  // - ftCommon_800804FC clears source-owner (`dmg.x18C4_source_ply = 6`) and disables x18C8
  //   countdown (`dmg.x18C8 = -1`) on grounded paths.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: no grounded clear-phase override on this row.
  // - 1: consume grounded clear before x18C8 decrement for this one-step row.
  uint8_t source_clear_grounded_damage_clear_phase[MSL_MAX_PLAYERS];
  // Terminal source-clear phase bridge for `dmg.x18C8 == 0` rows (one-step transient).
  //
  // Decomp ownership context:
  // - Fighter_8006A360 runs timer ownership + callbacks in the same proc-prio-1 phase.
  // - `dmg.x18C8` expiry clears `dmg.x18C4_source_ply` (Slippi `last_hit_by`) at terminal tick,
  //   but callback-owned damage/ownership ordering can preserve source-owner identity for one
  //   post-frame in specific terminal contexts.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  //
  // Producer (tools/slippi/make_dataset_from_slp.py):
  // - 0: default terminal-clear behavior (clear at `source_clear_timer_x18c8 == 1`).
  // - 1: defer that terminal clear exactly one frame for this seed row.
  uint8_t source_clear_terminal_phase[MSL_MAX_PLAYERS];
  // fp+0x2340 AttackDash lane:
  // - mv.co.attackdash.x0 countdown consumed by ftCo_800D8AE0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
  int16_t attackdash_x0[MSL_MAX_PLAYERS];
  // fp+0x2340 Attack1 lane:
  // - mv.co.attack1.x0 latched jab-chain intent consumed by checkAttack12/checkAttack13.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (misc AS variable @ fp+0x2340)
  uint8_t jab_x0[MSL_MAX_PLAYERS];
  // Reseed-only match-flow countdown timer (teacher-forcing aid).
  // Used for match-start entry / KO / respawn states where Slippi post-frames do not expose a
  // useful per-frame counter (action_frame is often -1).
  //
  // Convention: decomp-shaped countdown value (fp->x2340-style), derived from ftCommonData constants
  // and elapsed-in-state (contiguous run length so far), clamped to 255.
  //
  // NOTE: This is not "remaining until the action ends" in general, because match-flow states can
  // exit early via inputs (IASA), and the replay action_id run length may be shorter than the
  // internal timer.
  uint8_t match_flow_timer[MSL_MAX_PLAYERS];
  // Match-start fighter input lock countdown (`fp->x221D_b4`).
  //
  // Decomp / asset anchors:
  // - Fighter init sets x221D_b4 via ftLib_800867E8.
  // - Fighter_procUpdate blanks current input lanes while x221D_b4 remains set.
  // - VS opening clears x221D_b4 for all fighters from fn_8016B7F8, the ScInfCnt status-overlay
  //   completion callback scheduled by ifStatus_802F6EA4(3, ...).
  // - The VS overlay is IfAll.dat::ScInfCnt_scene_models[3] with AObj end_frame 85.0; with the
  //   standard raw -122 opening seed, that maps to 83 remaining locked simulation steps and
  //   clears before raw -39 inputs are processed.
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800867E8,ftLib_800868A4}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
  // refs/melee/src/melee/gm/gm_16AE.c::{gm_8016E934_OnEnter,fn_8016B7F8}
  // refs/melee/src/melee/if/ifstatus.c::ifStatus_802F6EA4
  // refs/melee/src/melee/if/if_2F72.c::if_802F73C4
  // refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
  //
  // Seed shape:
  // - Stored per player for dataset/FFI stability, but live runtime aggregates to one batch-global
  //   countdown because the VS-opening callback clears x221D_b4 for all fighters together.
  uint8_t opening_input_lock_timer[MSL_MAX_PLAYERS];
  // Legacy compatibility lane from the earlier EntryEnd->Fall investigation.
  // The authoritative opening-control owner is now `opening_input_lock_timer` (`fp->x221D_b4`).
  // This seed lane is retained for dataset/debug compatibility until the old field is pruned.
  uint8_t entry_end_fall_lock[MSL_MAX_PLAYERS];
  // Rebirth / dead-flow camera-box visibility (`fp->x221F_b0`) as an explicit seed lane.
  //
  // Decomp / replay anchors:
  // - ftLib_80086A8C exposes `fp->x221F_b0` from the fighter camera-subject visibility test.
  // - Rebirth_Cam owns the callback path that updates that visibility during respawn flow.
  // - Slippi post-frame emits fp+0x221F as `state_flags[...,4]`, which includes the b0 mask.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //
  // Seed representation:
  // - 0: camera-box visibility clear on the seed row.
  // - 1: camera-box visibility set on the seed row.
  //
  // This duplicates the replay-visible packed state_flags bit as a named semantic lane so future
  // F04 runtime ownership fixes can key on the decomp meaning directly instead of unpacking bytes.
  uint8_t camera_box_visible_x221f_b0[MSL_MAX_PLAYERS];
  // Rebirth camera subject anchor Y (`fp->mv.co.common.x8`) as an explicit seed lane.
  //
  // Decomp / data anchors:
  // - ftCo_Rebirth_Cam writes camera subject Y from `fp->mv.co.common.x8` plus a camera-data offset.
  // - On Final Destination that hidden base lane matches the stage respawn-point Y.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
  // data/stages/final_destination.json: respawn_points
  //
  // Seed representation:
  // - 0.0f outside Rebirth / unsupported stages.
  // - FD respawn-point Y on Rebirth seed rows.
  float rebirth_camera_anchor_y_f32[MSL_MAX_PLAYERS];
  // Fighter camera-subject target point (`camera_box->x1C`) and radius (`camera_box->x34.z`).
  //
  // Decomp / data anchors:
  // - ftLib_800866DC writes the subject point from camera_zoom_target_bone + co_attrs.x170.
  // - ftCamera_80076018 scales camera-box extents by fp->x34_scale.y.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
  // refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
  // data/characters/{fox,falco}.json: camera_zoom_target_bone_part_id,
  //   camera_zoom_target_offset, camera_box_radius
  float camera_target_world_x_f32[MSL_MAX_PLAYERS];
  float camera_target_world_y_f32[MSL_MAX_PLAYERS];
  float camera_target_world_z_f32[MSL_MAX_PLAYERS];
  float camera_box_radius_f32[MSL_MAX_PLAYERS];
  // Current-row Camera_80030CD8-style point-inside-stage-cam predicate.
  //
  // Decomp / data anchors:
  // - ftLib_80086A8C clears fp->x221F_b0 when Camera_80030CD8 reports the subject point is on-screen.
  // - Camera_80030CD8 delegates to Camera_80030BBC against the active camera bounds.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
  // refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
  // data/stages/final_destination.json: cam_bounds_world
  uint8_t camera_target_point_inside_stage_cam_bounds_u8[MSL_MAX_PLAYERS];
  // DownWait countdown timer (seeded; decomp-shaped).
  //
  // Decomp:
  // - init on DownBound->DownWait: fp->mv.co.downwait.x0 = p_ftCommonData->x424
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
  // - decrement + auto-stand in DownWait_Anim:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Anim
  //
  // Slippi post-frame does not expose fp->mv.* unions, so tooling derives this strictly causally
  // from the post-frame action_id sequence.
  int16_t downwait_timer[MSL_MAX_PLAYERS];
  // PassiveWall / PassiveWallJump hidden startup timer (`fp->mv.co.passivewall.timer`).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
  uint8_t passivewall_timer[MSL_MAX_PLAYERS];
  // Generic wall-jump hidden input phase (`fp->wall_jump_input_timer` and
  // `fp->x2110_walljumpWallSide`). Slippi does not expose CollData's persisted walljump phase, so
  // preprocessing seeds the minimal phase needed by ftWallJump_8008169C on one-step rows.
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  uint8_t walljump_input_timer[MSL_MAX_PLAYERS];
  int8_t walljump_wall_side_i8[MSL_MAX_PLAYERS];
  // Teacher-forced CollData wall side/index for one-step reseeds.
  //
  // Decomp:
  // - DamageFly_Coll / DownDamage_Coll consume CollData.env_flags after mpColl wall callbacks.
  // - CollData keeps left/right wall indices across callback phases; public Slippi rows do not
  //   expose those indices directly, so preprocessing may seed the persisted side/index from
  //   replay-prefix position plus extracted stage wall graph.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
  // refs/melee/src/melee/lb/types.h::CollData
  // data/stages/final_destination.json
  uint8_t mpcoll_wall_kind_seed_u8[MSL_MAX_PLAYERS];  // 0 none, 1 left, 2 right
  uint16_t mpcoll_wall_id_seed_u16[MSL_MAX_PLAYERS];  // ISO segment id, 0xFFFF none
  // Decomp-shaped animation/script timebase: fp->cur_anim_frame (float).
  // Slippi post-frame exposes this as `state_age` (float, can be fractional).
  //
  // Source pointers:
  // - refs/melee/src/melee/ft/types.h (Fighter::cur_anim_frame at fp+894)
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm ("send AS frame", loads from 0x894)
  // - refs/melee/src/melee/ft/ftaction.c::ftAction_80073240 (movescript timers use fp->cur_anim_frame)
  float anim_frame_f32[MSL_MAX_PLAYERS];
  // Decomp: fp->frame_speed_mul controls fractional animation advance (HSD AObj rate).
  // Not exposed by Slippi post-frames; we derive/seed it strictly causally in preprocessing.
  //
  // Source pointers:
  // - refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState sets fp->frame_speed_mul)
  // - refs/melee/src/melee/ft/ftanim.c (ftAnim_8006F0FC / ftAnim_SetAnimRate)
  float frame_speed_mul_f32[MSL_MAX_PLAYERS];
  // Capture/grab hidden owner lanes.
  //
  // Decomp ownership:
  // - ftCo_800DA824 initializes the common grab timer (`fp->grab_timer`).
  // - ftCo_CaptureWaitHi_Anim / fn_800DB8A4 own the shared CaptureWait/CaptureDamage timer/counter.
  // - fn_800DC014 owns the deferred XY jump latch (`mv.co.capturewait.xC`).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_800DA824,ftCo_CaptureWaitHi_Anim,fn_800DB8A4,fn_800DC014
  // }
  //
  // Seed representation:
  // - capture_grab_timer_f32: post-frame `fp->grab_timer` while attached, else 0.
  // - capture_wait_counter_f32: post-frame `mv.co.capturewait.x0`.
  // - capture_wait_anim_rate_timer_f32: post-frame `mv.co.capturewait.x4`.
  // - capture_wait_jump_latch_u8: post-frame `mv.co.capturewait.xC`.
  // - capture_breakout_pending_u8: explicit current-frame breakout resolve bit for the shared
  //   CatchWait/CaptureWait owner family.
  float capture_grab_timer_f32[MSL_MAX_PLAYERS];
  float capture_wait_counter_f32[MSL_MAX_PLAYERS];
  float capture_wait_anim_rate_timer_f32[MSL_MAX_PLAYERS];
  uint8_t capture_wait_jump_latch_u8[MSL_MAX_PLAYERS];
  uint8_t capture_breakout_pending_u8[MSL_MAX_PLAYERS];
  // Walk Anim callback source velocity (`mv_x0` in ftWalkCommon_800DFDDC).
  //
  // Decomp ownership:
  // - ftCo_Walk_Anim delegates to ftWalkCommon_800DFDDC, which computes:
  //     if (ft_GetGroundFrictionMultiplier(fp) < 1) mv_x0 = fp->mv.co.walk.x0;
  //     else                                         mv_x0 = fp->gr_vel;
  //   then sets walk anim rate from `mv_x0`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
  //
  // Seed/runtime representation:
  // - Runtime carries the causal callback-owned source velocity.
  // - Replay one-step seeds reconstruct the hidden same-Walk callback source from the next exposed
  //   walk rate when Slippi's post-frame frame-speed lane is one row late for this owner.
  float walk_anim_source_vel_f32[MSL_MAX_PLAYERS];
  // Walk retarget tick source velocity for ftWalkCommon_800DFDDC -> 800DFEC8 rows.
  //
  // This is a narrow non-causal replay-facing lane for the hidden
  // `ft_GetGroundFrictionMultiplier(fp) < 1` source-selection branch on Walk type-change ticks.
  // Runtime leaves the general frame_speed_mul_f32 lane causal and normally uses
  // walk_anim_source_vel_f32; one-step replay seeds fill this only when the Walk retarget row needs
  // current `gr_vel` rather than hidden `mv.co.walk.x0`.
  // refs/melee/src/melee/ft/ftwalkcommon.c::{ftWalkCommon_800DFDDC,ftWalkCommon_800DFEC8}
  float walk_retarget_tick_source_vel_f32[MSL_MAX_PLAYERS];
  // Run Anim callback source velocity (`vel` in ftCo_Run_Anim).
  //
  // Seed/runtime representation mirrors walk_anim_source_vel_f32:
  // - Runtime carries the causal callback-owned source velocity.
  // - Replay one-step seeds may reconstruct the hidden same-Run callback source from the next
  //   exposed Run rate, leaving the general frame_speed_mul_f32 lane causal.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
  float run_anim_source_vel_f32[MSL_MAX_PLAYERS];
  // Narrow replay-facing Turn->KneeBend hidden-facing owner lane.
  //
  // Decomp:
  // - ftCo_Turn_IASA temporarily exposes mv.co.turn.facing_after for early Turn checks, then
  //   ftCo_Jump_CheckInput can enter KneeBend in the same callback.
  // - Slippi post-frames do not expose the transient Turn microphase that decides whether the
  //   KneeBend entry inherits facing_after. Use 0=no override, 1=left, 2=right.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
  uint8_t turn_kneebend_facing_override_u8[MSL_MAX_PLAYERS];
  // Guard (shield) tilt pose state (seeded; decomp-shaped).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
  // - mv.co.guard.x8: "frame-ish" index into the Guard tilt timeline (neutral is 10 in GALE01)
  // - mv.co.guard.x4: stick magnitude smoothing used to blend the pose
  uint16_t guard_tilt_x8[MSL_MAX_PLAYERS];
  float guard_tilt_x4[MSL_MAX_PLAYERS];
  // GuardReflect reflect timer (seeded; strictly causal in preprocessing).
  //
  // Decomp trail:
  // - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  //   sets `mv.co.guard.x14 = p_ftCommonData->x2A4`.
  // - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
  //   decrements `mv.co.guard.x14` and clears `fp->reflecting` when it drops below 0.
  //
  // Seed representation:
  // - Store a non-negative, reseed-friendly countdown that drives the Slippi reflect-active bit
  //   (fp+0x2218 bit4 => state_flags[0] bit 0x10).
  // - Because the decomp timer expires on `x14 < 0`, we represent `x14 + 1` clamped to [0..255].
  //   This allows us to expire cleanly at 0 without carrying negative values in the seed schema.
  uint8_t guard_reflect_timer_x14[MSL_MAX_PLAYERS];
  // GuardReflect powershield-active timer (seeded; strictly causal in preprocessing).
  //
  // Decomp trail:
  // - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  //   sets `mv.co.guard.x18 = p_ftCommonData->x2B4`.
  // - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
  //   decrements `mv.co.guard.x18` and clears `fp->x221C_b2` when it drops below 0.
  //
  // Seed representation:
  // - Store `x18 + 1` clamped to [0..255] (same +1-bias contract as x14).
  uint8_t guard_reflect_timer_x18[MSL_MAX_PLAYERS];
  // GuardReflect entry provenance for the final-x14 ShieldDesc handoff:
  // - 1: entered via ftCo_8009388C from an already-shielding Guard/GuardOn path.
  // - 0: entered via ftCo_80093A50 direct locomotion powershield path.
  uint8_t guard_reflect_origin_guardon_u8[MSL_MAX_PLAYERS];
  // Guard release lockout (seeded; strictly causal in preprocessing).
  //
  // Decomp (GALE01):
  // - mv.co.guard.xC: latched true when `held_inputs & HSD_PAD_LR` becomes false.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
  // - mv.co.guard.x10: initialized on GuardOn/GuardReflect entry from p_ftCommonData->x268, then
  //   decremented while the shield is active, and gates the release transition in Guard IASA.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA (inlineC0)
  //
  // Seed representation:
  // - guard_release_latched_xc: 0/1 (mv.co.guard.xC).
  // - guard_x10: remaining frames, clamped to [0..255] (mv.co.guard.x10).
  uint8_t guard_release_latched_xc[MSL_MAX_PLAYERS];
  uint8_t guard_x10[MSL_MAX_PLAYERS];
  // Lightshield amount latch used for shield HP drain when trigger input drops below the deadzone.
  // Decomp: fp->lightshield_amount and mv.co.guard.x2C in ftCo_800925A4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  float lightshield_amount[MSL_MAX_PLAYERS];
  // GuardSetOff shield-hit int-damage lower bound carried across the active shieldstun segment.
  // Decomp: ftCo_80092F2C reads fp->x19A4 for GuardSetOff anim-rate shaping.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  uint8_t guard_setoff_hitlag_damage_min[MSL_MAX_PLAYERS];
  // GuardSetOff hitlag-exit ownership phase discriminator.
  //
  // Phase meaning:
  // - 0: steady/non-GuardSetOff row
  // - 1: GuardSetOff hitlag carry row with hitlag > 1
  // - 2: GuardSetOff last-hitlag row with hitlag == 1
  // - 3: first non-hitlag GuardSetOff row after a same-segment hitlag row
  //
  // Decomp / ownership anchors:
  // - ftCo_80092F2C shapes GuardSetOff entry anim-rate before the frozen tail.
  // - Fighter_8006A360 advances ftAnim before ftCo_GuardSetOff_Anim resumes callback-owned rate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  uint8_t guard_setoff_hitlag_exit_phase_u8[MSL_MAX_PLAYERS];
  // GuardSetOff post-hitlag owner discriminator on the last-hitlag / first-post-hitlag rows.
  //
  // Meaning:
  // - 0: not a GuardSetOff post-hitlag handoff row
  // - 1: normal GuardSetOff handoff (no powershield-active owner)
  // - 2: powershield-active GuardSetOff handoff (`x221C_b2` still live)
  //
  // Decomp / ownership anchors:
  // - ftCo_GuardSetOff_Anim owns the GuardSetOff handoff after prio-0 hitlag decrement.
  // - ftCo_80093BC0 still owns the powershield-active x18/x221C_b2 lane when active.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
  uint8_t guard_setoff_post_hitlag_owner_u8[MSL_MAX_PLAYERS];
  // GuardSetOff-specific hidden exit anim-rate lane.
  //
  // This is intentionally separate from frame_speed_mul_f32. The general frame_speed_mul_f32 seed
  // remains strictly causal; this field is a narrow replay-facing reconstruction for GuardSetOff
  // last-hitlag rows where Slippi exposes the hidden ftCo_80092F2C x19A4/lightshield-owned rate
  // only on the first future non-hitlag GuardSetOff row.
  //
  // Seed representation:
  // - 0.0: no explicit GuardSetOff exit-rate override.
  // - >0.0: use this rate only when reseeding a GuardSetOff last-hitlag row
  //   (guard_setoff_hitlag_exit_phase_u8 == 2).
  //
  // Decomp / ownership anchors:
  // - ftCo_80092F2C computes the entry rate from fp->x19A4 and fp->lightshield_amount.
  // - Fighter_8006A360 advances ftAnim as hitlag exits before ftCo_GuardSetOff_Anim resumes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  float guard_setoff_exit_frame_speed_mul_f32[MSL_MAX_PLAYERS];
  uint8_t jumps_left[MSL_MAX_PLAYERS];
  uint8_t stocks[MSL_MAX_PLAYERS];

  // Input-history / locomotion internals (seeded from replay history)
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28 and :44-56
  uint8_t kneebend_jump_input[MSL_MAX_PLAYERS];    // fp->mv.co.kneebend.jump_input (ftCo_JumpInput)
  uint8_t kneebend_is_short_hop[MSL_MAX_PLAYERS];  // fp->mv.co.kneebend.is_short_hop (bool)
  // Decomp: refs/melee/src/melee/ft/fighter.c:1908-2008 (x670/x671 updates each frame)
  uint8_t tilt_timer_x[MSL_MAX_PLAYERS];  // fp->x670_timer_lstick_tilt_x
  uint8_t tilt_timer_y[MSL_MAX_PLAYERS];  // fp->x671_timer_lstick_tilt_y
  // Decomp: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
  uint8_t fall_fast[MSL_MAX_PLAYERS];  // fp->fall_fast (bool)
  // Fastfall ownership bridge at immediate hitlag-exit reseed rows (seeded; decomp-shaped).
  //
  // Decomp ordering:
  // - Fighter_8006A1BC decrements hitlag at proc prio 0.
  // - Fighter_8006A360 then runs the non-hitlag callback/physics ownership lane.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
  //
  // Seed representation:
  // - 1: on this seed row, the next step is the immediate hitlag-exit lane for a fastfall-capable
  //   action, so reseed should keep internal fp->fall_fast ownership from the seeded history lane.
  // - 0: use the raw Slippi fp+0x221A isFastFalling snapshot bit as authoritative reseed source.
  uint8_t fall_fast_hitlag_exit_owner[MSL_MAX_PLAYERS];
  // Run IASA lockout countdown (seeded; decomp-shaped).
  //
  // Decomp:
  // - Stored at fp->mv.co.run.x0 (float).
  // - Decremented by 1.0 each frame in ftCo_Run_Anim.
  // - Gates TurnRun/RunBrake in ftCo_Run_IASA (if x0 > 0, return early before those checks).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_Anim,ftCo_Run_IASA}
  //
  // Seed representation:
  // - Store a reseed-friendly u8 countdown (clamped to 0..255) representing (mv.co.run.x0 > 0).
  uint8_t run_x0[MSL_MAX_PLAYERS];
  // RunBrake TurnRun gate (`fp->cmd_vars[0]`) seeded from the common submotion script.
  //
  // Decomp:
  // - ftCo_RunBrake_Enter resets fp->cmd_vars[0] = 0.
  // - ftCo_RunBrake_IASA only reaches fn_800C9CEC (TurnRun enter) when fp->cmd_vars[0] != 0.
  // - fp->cmd_vars[0] is written by the action script via ftAction_80071820.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
  //   ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
  //
  // Source of truth:
  // - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0).
  //
  // Seed representation:
  // - 0: cmd_vars[0] disabled on this seeded post-frame.
  // - 1: cmd_vars[0] enabled on this seeded post-frame.
  uint8_t runbrake_cmd0[MSL_MAX_PLAYERS];
  // Dash IASA branch latch (seeded; decomp-shaped).
  //
  // Decomp:
  // - ftCo_Dash_Enter stores arg1 into fp->mv.co.dash.x4.
  // - ftCo_Dash_IASA uses (x4 != 0 && cur_anim_frame <= p_ftCommonData->x44) to select the
  //   early interrupt branch.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Enter,ftCo_Dash_IASA}
  //
  // Seed representation:
  // - Store a reseed-friendly u8 latch (0/1) for fp->mv.co.dash.x4.
  uint8_t dash_x4[MSL_MAX_PLAYERS];
  // Fox/Falco Shine release internals (seeded; decomp-shaped).
  //
  // Decomp:
  // - ftFox_SpecialLw_SetVars initializes releaseLag/isRelease on SpecialLw enter.
  // - Start/Loop/Turn/Hit anim callbacks tick releaseLag and latch isRelease from held B.
  // - ftFx_SpecialLwHit_Check gates Loop vs End using (releaseLag <= 0 && isRelease).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFox_SpecialLw_SetVars,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Check}
  //
  // Seed representation:
  // - shine_release_lag: non-negative countdown (u8 mirror of mv.fx.SpecialLw.releaseLag).
  // - shine_is_release: 0/1 latch (u8 mirror of mv.fx.SpecialLw.isRelease).
  uint8_t shine_release_lag[MSL_MAX_PLAYERS];
  uint8_t shine_is_release[MSL_MAX_PLAYERS];
  // ECB lock countdown (seeded; decomp-shaped).
  //
  // Decomp:
  // - ftCommon_8007D5D4 / ftCommon_8007D60C set fp->ecb_lock and set CollData_X130_Locked.
  // - Fighter_procMap decrements fp->ecb_lock each map/collision callback and clears the lock at 0.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C,ftCommon_UnlockECB}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
  //
  // Seed representation:
  // - Store a reseed-friendly u8 countdown (clamped to 0..255) for fp->ecb_lock.
  uint8_t ecb_lock_timer[MSL_MAX_PLAYERS];
  // Ledge grab cooldown timer (seeded; decomp-shaped).
  // Decomp: fp->x2064_ledgeCooldown decremented each frame under !hitlag.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // Set on certain cliff releases (e.g., CliffWait drop / timeout) to suppress immediate re-grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
  uint8_t ledge_cooldown[MSL_MAX_PLAYERS];  // fp->x2064_ledgeCooldown (clamped to 0..255)
  // Hidden LandingFallSpecial interrupt permission (`mv.co.landing.allow_interrupt`).
  //
  // Decomp:
  // - EscapeAir_Coll enters LandingFallSpecial with allow_interrupt=false.
  // - FallSpecial_Coll forwards `mv.co.fallspecial.allow_interrupt`.
  // - SpecialS/Hi freefall enters FallSpecial with allow_interrupt=true.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
  //   ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll
  uint8_t landing_fallspecial_allow_interrupt[MSL_MAX_PLAYERS];
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 (ftCo_Turn_Anim_Inner)
  uint8_t turn_frames_to_turn[MSL_MAX_PLAYERS];  // fp->mv.co.turn.frames_to_turn
  uint8_t turn_has_turned[MSL_MAX_PLAYERS];      // fp->mv.co.turn.has_turned
  // Turn dash-out latch (seeded; decomp-shaped).
  //
  // Decomp:
  // - fp->mv.co.turn.x8 is set by fn_800C9C2C when:
  //     (lstick.x * facing_after >= p_ftCommonData->x3C) &&
  //     (x670_timer_lstick_tilt_x < p_ftCommonData->x40)
  // - It is consumed by ftCo_Turn_IASA as a boolean gate to allow Turn->Dash when the turn
  //   completes (`just_turned && x8`).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
  //
  // Seed representation:
  // - Store the sign of x8 as i8: -1/0/+1.
  int8_t turn_x8[MSL_MAX_PLAYERS];
  // Decomp: refs/melee/src/melee/ft/fighter.c:2078-2086 (x67F updates each frame).
  uint8_t lr_press_timer[MSL_MAX_PLAYERS];  // fp->x67F (frames since L/R press; saturates at 0xFF)
  // Decomp: refs/melee/src/melee/ft/fighter.c:2020-2050 (x672 updates each frame).
  uint8_t x672_input_timer[MSL_MAX_PLAYERS];  // fp->x672_input_timer_counter (saturates at 0xFE)
  // Decomp: refs/melee/src/melee/ft/fighter.c:1897-2094 (fighter input counters block).
  // Stick/trigger companion timers + "age since last change" counters (saturate at 0xFE).
  uint8_t x673[MSL_MAX_PLAYERS];    // fp->x673 (lstick x companion)
  uint8_t x674[MSL_MAX_PLAYERS];    // fp->x674 (lstick y companion)
  uint8_t x675[MSL_MAX_PLAYERS];    // fp->x675 (trigger companion)
  uint8_t x676_x[MSL_MAX_PLAYERS];  // fp->x676_x ("age since last change", x)
  // Decomp: fp->x2228_b7 stores the sign of the most recent fresh X-directional stick entry
  // (set to 1 on fresh >=+threshold entry, set to 0 on fresh <=-threshold entry).
  // refs/melee/src/melee/ft/fighter.c:1924,1949
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  uint8_t x2228_b7[MSL_MAX_PLAYERS];  // 0/1
  uint8_t x677_y[MSL_MAX_PLAYERS];    // fp->x677_y ("age since last change", y)
  uint8_t x678[MSL_MAX_PLAYERS];      // fp->x678 ("age since last change", trigger)
  uint8_t x679_x[MSL_MAX_PLAYERS];    // fp->x679_x (lstick x companion)
  uint8_t x67A_y[MSL_MAX_PLAYERS];    // fp->x67A_y (lstick y companion)
  uint8_t x67B[MSL_MAX_PLAYERS];      // fp->x67B (trigger companion)
  // Button timers (saturate at 0xFF, reset to 0 on press).
  uint8_t x67C[MSL_MAX_PLAYERS];  // fp->x67C (A)
  uint8_t x67D[MSL_MAX_PLAYERS];  // fp->x67D (B)
  uint8_t x67E[MSL_MAX_PLAYERS];  // fp->x67E (X/Y)
  uint8_t x680[MSL_MAX_PLAYERS];  // fp->x680 (L or R)
  uint8_t x681[MSL_MAX_PLAYERS];  // fp->x681 (DPad Up)
  uint8_t x682[MSL_MAX_PLAYERS];  // fp->x682 (DPad Down)
  // Button timer capture on press.
  uint8_t x683[MSL_MAX_PLAYERS];  // fp->x683 (captures prev x67C on A press)
  uint8_t x684[MSL_MAX_PLAYERS];  // fp->x684 (captures prev x680 on L/R press)

  // UCF pad buffer (seeded, multi-frame).
  //
  // References:
  // - refs/ucf/include/ucf/pad_buffer.h (UCF_PAD_BUFFER_SIZE=4, index, sdrop_up_frames)
  // - refs/ucf/src/pad_buffer/pad_buffer.cpp (ring-buffer write ordering)
  uint8_t ucf_padbuf_index[MSL_MAX_PLAYERS];
  uint8_t ucf_padbuf_sdrop_up_frames[MSL_MAX_PLAYERS];
  int8_t ucf_padbuf_stick_x[MSL_MAX_PLAYERS][4];
  int8_t ucf_padbuf_stick_y[MSL_MAX_PLAYERS][4];

  // Combat / timers
  float percent[MSL_MAX_PLAYERS];
  // Fighter phantom/tip-log delayed damage state (fp->dmg.x1898 + x189C countdown + source gobj).
  // 0.0 / 0 / 0xFF means no pending phantom damage at the reseed boundary.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007BE3C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float phantom_damage_pending_x1898[MSL_MAX_PLAYERS];
  uint16_t phantom_damage_timer_x189c[MSL_MAX_PLAYERS];
  // Legacy field name: this hidden teacher-forced lane stores a local simulator slot, not a raw
  // Slippi/controller source port. `0xFF` means no pending phantom source. Replay-visible
  // `last_hit_by` remains raw source-port domain and must be mapped through `source_port0` before
  // local-slot indexing.
  uint8_t phantom_damage_source_port[MSL_MAX_PLAYERS];
  // ftColl_80079AB0 percent-term gate bits (seeded).
  //
  // These correspond to Fighter flags at fp+0x2225/fp+0x2224 used by the non-WSK else-branch:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 (0x80079B68..0x80079BA0)
  //
  // - x2225_b7 (fp+0x2225 bit0 / mask 0x01): initialized from Player_GetMoreFlagsBit2, which is set
  //   from PlayerInitData.xC_b7 (stamina-mode flag).
  //   refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC (Player_SetMoreFlagsBit2)
  //   refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitLoad_80068914 (fp->x2225_b7)
  // - x2224_b2 (fp+0x2224 bit5 / mask 0x20): selects between p_ftCommonData->0x6D4 and 0x6D8.
  //   refs/melee/src/melee/ft/ft_0C8C.c::fn_800C8_inline (setter)
  uint8_t dmg_x2225_b7[MSL_MAX_PLAYERS];  // 0/1
  uint8_t dmg_x2224_b2[MSL_MAX_PLAYERS];  // 0/1
  float shield_hp[MSL_MAX_PLAYERS];
  uint16_t hitlag[MSL_MAX_PLAYERS];
  uint16_t hitstun[MSL_MAX_PLAYERS];
  // Damage KB velocity merge timer (decomp: fp->dmg.x18AC_time_since_hit).
  //
  // Decomp:
  // - Fighter init sets x18AC = -1.
  // - Fighter_8006A360 increments x18AC once per non-hitlag frame while active.
  // - ftCo_8008DCE0 sets x18AC = 0 on Damage entry.
  // - ftCo_Damage_CalcVel replaces KB velocity while x18AC < p_ftCommonData->xFC; otherwise it
  //   merges the new KB vector with the existing KB velocity.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkInitReset_80067C98,Fighter_8006A360}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
  int16_t damage_time_since_hit_x18ac[MSL_MAX_PLAYERS];
  // Damage jump-buffer snapshot (decomp: fp->mv.co.damage.x14).
  // - Cleared on damage entry (ftCo_8008DCE0).
  // - Set to hitstun timer (mv.co.damage.x0) on jump-input detect in doIasa.
  // - Gated against p_ftCommonData->x1D0 in Damage_Anim's inlineC0 jump path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,doIasa,ftCo_Damage_Anim}
  uint16_t damage_jump_buffer_x14[MSL_MAX_PLAYERS];
  uint8_t l_cancel[MSL_MAX_PLAYERS];
  uint8_t hurtbox_state[MSL_MAX_PLAYERS];  // 0 vuln, 1 invuln, 2 intangible
  // Collision hit-status internals (decomp fp->x198C / x1990 / x1994 / x2221_b0).
  //
  // Slippi only exposes merged hurtbox_state (x1988 when nonzero else x198C). These internal
  // lanes are seeded explicitly from strictly-causal replay-history derivation so teacher-forced
  // one-step reseed does not have to infer timer ownership from a single frame.
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (x1990/x1994 tick + x198C updates)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398 (x1994 setter)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C (x1990 setter)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag (x1994 setter)
  // - refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_{Anim,IASA}
  //   (RebirthWait -> Fall x1994 setter)
  uint8_t colanim_hit_status_x198c[MSL_MAX_PLAYERS];  // 0/1/2
  uint8_t colanim_lock_x2221_b0[MSL_MAX_PLAYERS];     // 0/1
  uint16_t colanim_timer_x1990[MSL_MAX_PLAYERS];
  uint16_t colanim_timer_x1994[MSL_MAX_PLAYERS];
  uint8_t colanim_rebirth_fall_x1994_seed[MSL_MAX_PLAYERS];  // 0/1 source proof
  uint16_t ground_id[MSL_MAX_PLAYERS];
  uint32_t animation_index[MSL_MAX_PLAYERS];
  uint16_t instance_hit_by[MSL_MAX_PLAYERS];
  uint16_t instance_id[MSL_MAX_PLAYERS];
  // Fighter action-state instance_id compare byte (GALE01 fp+0x2073 within fp->x2070).
  // Used by ft_800895E0 to gate instance_id bumps on motion-state change.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  uint8_t instance_id_x2073[MSL_MAX_PLAYERS];
  // Seeded next value for plAttack_80037B08 (global counter backing fighter/item instance_id).
  // Slippi does not expose this internal directly; preprocessing derives it strictly causally
  // from replay-visible instance_id history (fighters + items).
  // refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
  uint16_t instance_id_counter;
  // Narrow replay-facing same-frame fighter-proc order lane for plAttack_80037B08.
  //
  // Decomp:
  // - Fighter_ChangeMotionState calls ft_800895E0 on motion-state entry, and that may consume the
  //   global plAttack_80037B08 counter.
  // - The global counter is shared across fighters/items; Slippi exposes only the post-frame
  //   per-object ids, not the HSD proc ordering for simultaneous fighter entries. Use 0=no
  //   override; nonzero is the replay-visible fp->x2088 value for this entry.
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
  // refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
  uint16_t motion_entry_instance_id_override_u16[MSL_MAX_PLAYERS];
  // Staling "attack id" (GALE01): fp->x2068_attackID.
  // Slippi post-frames do not expose fp->x2068 directly; preprocessing derives it causally from
  // replay history (see tools/slippi/staling_history.py).
  uint16_t attack_id[MSL_MAX_PLAYERS];
  // Staling "attack instance" (GALE01): fp->x206C_attack_instance.
  // This value is used for stale-queue duplicate suppression as part of the (move_id, attack_instance) key.
  // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0 and ::ft_800892A0 (x206C assignment sites)
  //
  // Slippi post-frames do not expose fp->x206C, so preprocessing derives it causally from replay history.
  uint16_t attack_instance[MSL_MAX_PLAYERS];
  uint8_t last_attack_landed[MSL_MAX_PLAYERS];
  uint8_t combo_count[MSL_MAX_PLAYERS];
  // Combo tracking internals (GALE01 fp->x2094 + fp->x2098).
  //
  // Decomp trail:
  // - Update on hit (writes fp->x208C, fp->x2090, fp->x2094):
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0 and ::ftColl_80076444 and ::ftColl_8007646C
  // - Clear logic (uses fp->x221C_b6 + victim fp->x2098):
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
  // - Victim combo-timer set when hitstun ends:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  //
  // Seed representation:
  // - combo_victim_port: player-slot index in [0..3], 0xFF = none (NULL).
  // - combo_victim_instance_id: victim `instance_id` identity key (respawn-safe).
  uint8_t combo_victim_port[MSL_MAX_PLAYERS];
  uint16_t combo_victim_instance_id[MSL_MAX_PLAYERS];
  uint16_t combo_timer_x2098[MSL_MAX_PLAYERS];
  // Slippi raw source port for each local dataset/sim slot.
  //
  // Decomp/recording boundary:
  // - `last_hit_by` mirrors `dmg.x18C4_source_ply`, which Slippi exports in the raw 0-based
  //   controller-port domain, not in the selected local dataset slot order.
  // - Runtime combo/hitlist ownership still uses local slots; only replay-facing source writes use
  //   this lane.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  // tools/slippi/make_dataset_from_slp.py (`src_ports` selected slot order)
  uint8_t source_port0[MSL_MAX_PLAYERS];
  uint8_t last_hit_by[MSL_MAX_PLAYERS];
  // Grab/throw victim attachment owner identity (seeded; suite-focused).
  //
  // Decomp:
  // - During capture/throw sequences, the victim stores an owner pointer in `fp->victim_gobj`.
  //   Thrown victim position is then driven from an attachment joint plus fp->x1A70 offsets
  //   (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508).
  //
  // Teacher-forced one-step reseed wipes pointers, so we seed only the minimal identity needed:
  // - grab_owner_port: player-slot index in [0..3], 0xFF = none.
  uint8_t grab_owner_port[MSL_MAX_PLAYERS];
  // ftCommon_GrabMash stick-sign latches (`fp->x1A50` / `fp->x1A51`).
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
  int8_t grab_mash_stick_x_sign[MSL_MAX_PLAYERS];
  int8_t grab_mash_stick_y_sign[MSL_MAX_PLAYERS];
  uint8_t _pad2[1];
  uint8_t state_flags[MSL_MAX_PLAYERS][5];

  // Combat hitlist internals (seeded; strictly causal from history in preprocessing).
  //
  // These fields are required for teacher-forced one-step eval: reseeding wipes rollout history,
  // so combat must carry its rehit suppression state through the seed schema.
  //
  // Decomp shape:
  // - per-hitbox victim hitlists live on HitCapsule (`victims_1` + per-entry cooldown),
  // - cooldown is initialized from `HitCapsule.x40_b4` and decremented each frame,
  //   clearing the victim when the countdown reaches 0.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C
  //
  // Simulator representation (minimal, fighter victims only):
  // - A dense cooldown map indexed by (attacker_slot, hit_group, victim_slot).
  // - Value semantics:
  //   - 0: empty (victim not in hitlist => can be hit)
  //   - 1..255: finite cooldown remaining in frames
  //   - 0xFFFF: indefinite latch (decomp: victim present with timer==0; cleared on hitbox-group re-enable)
  //
  // Shape: [attacker][hit_group][victim] in player-slot order.
  uint16_t combat_hitlist_cd[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS];
  // Victim identity key for each cooldown entry: the defender's `instance_id` at the time the entry
  // was created. This matches decomp storing a victim pointer (`HitVictim.victim`), which changes
  // on death/respawn.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  uint16_t combat_hitlist_victim_iid[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS];

  // Authoritative per-HitCapsule seed lane for fighter victims.
  //
  // This mirrors the runtime/decomp owner more closely than the dense group bridge above:
  // - HitCapsule stores victims per hitbox slot, not per hit_group.
  // - ftColl_800768A0 copies or clears a HitCapsule on enable edges, so preserving the per-hitbox
  //   source capsule at the reseed boundary lets runtime copy from the right owner.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
  // refs/melee/src/melee/lb/types.h::HitCapsule
  //
  // Validity:
  // - combat_hitlist_hb_valid[attacker][hb] == 1 means the per-hitbox list is authoritative for
  //   this seed snapshot, including the all-zero/empty case.
  // - valid == 0 falls back to the legacy group map for synthetic tests and older datasets.
  uint8_t combat_hitlist_hb_valid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
  uint16_t combat_hitlist_hb_cd[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];
  uint16_t combat_hitlist_hb_victim_iid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];

  // Teacher-forced per-HitCapsule shield-contact result.
  //
  // Decomp:
  // - ftColl_80078C70 reaches the fighter shield path through lbColl_80007BCC, with the current
  //   HitCapsule and defender ShieldDesc deciding whether ftColl_80076CBC runs.
  // - That ShieldDesc/narrowphase state is hidden at a one-step reseed boundary; replay-visible
  //   GuardSetOff + hitlag proves accepted contacts, and stable non-hitlag shield rows prove misses.
  //
  // Encoding:
  // - 0: unknown; use runtime geometry.
  // - 1: force no shield contact for this attacker/hitbox/defender.
  // - 2: force shield contact for this attacker/hitbox/defender.
  //
  // This is a teacher-forced seed surface only. Normal rollouts leave it zero and use the live
  // ShieldDesc geometry path.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  uint8_t combat_shield_contact_hb_kind[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];

  // Teacher-forced shield-hit max integer damage (`fp->x19A4`) for accepted GuardSetOff entries.
  //
  // Decomp:
  // - ftColl_80076CBC writes the defender's hidden x19A4 from the max getEnvDmg(hit0->damage)
  //   over accepted shield contacts before ftCo_80092F2C consumes it for GuardSetOff hitlag and
  //   shieldstun rate.
  // - One-step reseed may know shield contact occurred without being able to reconstruct the exact
  //   HitCapsule ordering that produced the max; this lane carries that hidden integer owner.
  //
  // Encoding: 0 unknown/use runtime max; N>0 authoritative x19A4 max int damage for this defender.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  uint8_t combat_shield_hit_int_damage[MSL_MAX_PLAYERS];

  // HitCapsule x58 seed lane for teacher-forced one-step replay starts.
  //
  // Decomp shape:
  // - ftColl_8007AD18 carries previous/current HitCapsule centers in x58/x4C.
  // - BODY collision consumes x58->x4C through lbColl_8000805C -> lbColl_80006E58.
  //
  // Normal rollouts populate this state by preserving the previous frame's runtime x4C in
  // src/hitboxes.c. Reseeding wipes that hidden lane, so preprocessing seeds the smallest causal
  // internal: the previous world center per active fighter HitCapsule slot.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  uint8_t combat_hitbox_prev_valid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
  float combat_hitbox_prev_x[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
  float combat_hitbox_prev_y[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
  float combat_hitbox_prev_z[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];

  // Stale-move (staling) internals (seeded; not causally derivable from a single frame).
  //
  // Decomp shape:
  // - Each player maintains a `StaleMoveTable` ring buffer of the last 10 (move_id, attack_instance)
  //   pairs, with `current_index` pointing to the next write slot (wrap at 9).
  // - Staling multiplier consults the previous 9 entries starting from (current_index - 1).
  // refs/melee/src/melee/pl/types.h::StaleMoveTable
  // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  uint8_t stale_queue_index[MSL_MAX_PLAYERS];
  uint16_t stale_move_id[MSL_MAX_PLAYERS][MSL_STALE_QUEUE_SIZE];
  uint16_t stale_attack_instance[MSL_MAX_PLAYERS][MSL_STALE_QUEUE_SIZE];
  // Damage hitlag-exit callback ownership lane (seed bridge).
  //
  // Decomp:
  // - Damage entry sets `fp->post_hitlag_cb = ftCo_Damage_OnExitHitlag`.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // - Hitlag-exit path invokes `post_hitlag_cb`.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
  //
  // Encoding:
  // - 0: no callback
  // - 1: ftCo_Damage_OnExitHitlag
  uint8_t damage_post_hitlag_cb_kind[MSL_MAX_PLAYERS];
  // Grounded attacker-on-shield knockback scalar (`fp->xF4_ground_attacker_shield_kb_vel`).
  //
  // Decomp:
  // - ftColl_80076CBC writes shield-hit internals (`x1928`, `x192C`) on the grounded attacker.
  // - Fighter_ProcessHit_8006D1EC shapes `xF4_ground_attacker_shield_kb_vel` from those internals.
  // - Fighter_procUpdate decays that scalar through ftCommon_8007CE4C and projects it onto the
  //   floor tangent via `x98_atk_shield_kb`.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007CE4C,ftCommon_8007E2A4}
  float attacker_shield_ground_kb_vel[MSL_MAX_PLAYERS];
  // Populated by replay-history preprocessing:
  // - tools/slippi/staling_history.py (derive) and tools/slippi/make_dataset_from_slp.py (wire).
  //
  // Runtime usage:
  // - src/combat.c applies the staling multiplier on damaging BODY hits and updates this queue on
  //   qualifying hits using the seeded `attack_instance` as source-of-truth.
  //
  // Per-item reflected-damage multiplier lane (decomp: item->xC6C).
  //
  // Decomp:
  // - Reflect collision writes `item->xC6C` from fighter ReflectDesc damage multiplier.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // - Item reflect apply rewrites HitCapsule.unk_count as:
  //     (u32)(hit.damage * item->xC6C + 0.99f)
  //   before calling item-vs-fighter apply.
  //   refs/melee/src/melee/it/item.c::Item_80269F14
  //   refs/melee/src/melee/it/itcoll.c::it_80272460
  //
  // Seed representation:
  // - Strictly-causal per-item value aligned to fixed item slots in `items`.
  // - Runtime reseed sanitizes invalid/non-positive values back to 1.0f.
  float item_reflect_damage_mul[MSL_MAX_ITEMS];
  // Item HitCapsule victims_1 seed lane for throw-side laser articles.
  //
  // Decomp ownership:
  // - Item-vs-fighter BODY collision routes through it_8026FAC4 / it_8026FA2C and inserts fighter
  //   victims into the item's HitCapsule victims_1 list via lbColl_80008688.
  // - The dump probe patch in tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch
  //   confirms throw-laser xDA8 and victims_1 cooldown state on replay-real F14 rows.
  //
  // Seed representation:
  // - port 0..3: one fighter victim to pre-latch into the item hitlist at reseed.
  // - port 0xFF: no seeded victim for this item slot.
  // - cooldown stores the HitVictim.x4 value to insert, clamped to u8.
  // - hitbox_mask selects which item HitCapsules receive the seeded victim. Dolphin v10 dumps show
  //   throw-laser victims_1 entries are per hitbox, so a per-item collapse is not authoritative.
  // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  uint8_t item_hitlist_victim_port[MSL_MAX_ITEMS];
  uint8_t item_hitlist_victim_cd[MSL_MAX_ITEMS];
  uint8_t item_hitlist_victim_hitbox_mask[MSL_MAX_ITEMS];
  uint16_t item_hitlist_victim_iid[MSL_MAX_ITEMS];
  // Hidden item callback/collision seed lanes for teacher-forced one-step reseed.
  //
  // Decomp ownership:
  // - ftColl_80077464 writes pending reflect owner/xDA8 into item->xC64/xC8C and Item_80269F14
  //   consumes it before Slippi item post-frame can expose an explicit pending lane.
  // - ftColl_80077688 writes shield-bounce internals item->xC54/xC58/xDCE, then Item_80269DC8
  //   chooses HitShield destroy versus ShieldBounced keepalive.
  // - item BODY callbacks write item->xC34_damageDealt and HitCapsule victims_1; Item_8026A294
  //   consumes xC34 in OnGiveDamageThink on the next item callback phase.
  //
  // Replay seed representation:
  // - reflect_transfer_port: 0..3 forces the pending Item_80269F14 owner transfer, 0xFE means the
  //   seed row is known not to have a pending reflect transfer, 0xFF means unknown/no seed.
  // - shield_bounce_valid plus vx/vy reconstruct the hidden xC58 ShieldBounced result when the
  //   current post-frame exposes only the surviving bounced laser.
  // - hidden_body_hit_victim_port: 0..3 applies a hidden xC34 BODY callback hit before normal item
  //   collision for this one-step seed; 0xFF means no hidden BODY hit.
  // - hidden_callback_flags bit0 clears the item via the seeded OnGiveDamage/dmg_dealt phase.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688,ftColl_80077C60}
  // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8,Item_8026A294}
  // refs/melee/src/melee/it/items/itfoxlaser.c::{
  //   itFoxLaser_Logic94_ShieldBounced,itFoxLaser_Logic94_HitShield}
  uint8_t item_reflect_transfer_port[MSL_MAX_ITEMS];
  uint16_t item_reflect_transfer_iid[MSL_MAX_ITEMS];
  uint8_t item_shield_bounce_valid[MSL_MAX_ITEMS];
  float item_shield_bounce_vel_x[MSL_MAX_ITEMS];
  float item_shield_bounce_vel_y[MSL_MAX_ITEMS];
  uint8_t item_hidden_body_hit_victim_port[MSL_MAX_ITEMS];
  uint8_t item_hidden_body_hit_hurt_height[MSL_MAX_ITEMS];
  uint8_t item_hidden_callback_flags[MSL_MAX_ITEMS];

  MslItem items[MSL_MAX_ITEMS];
} MslSeed;

typedef struct MslCompare {
  int32_t frame_id;
  uint32_t frame_pre_random_seed;

  uint32_t stage_id;
  uint8_t num_players;
  uint8_t is_teams;
  uint8_t _pad0[2];

  uint8_t team_id[MSL_MAX_PLAYERS];
  uint8_t char_id[MSL_MAX_PLAYERS];

  float pos_x[MSL_MAX_PLAYERS];
  float pos_y[MSL_MAX_PLAYERS];
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];
  uint8_t on_ground[MSL_MAX_PLAYERS];
  uint8_t is_dead[MSL_MAX_PLAYERS];  // 0/1 (derived or explicit)
  uint8_t _pad1[1];

  uint16_t action_id[MSL_MAX_PLAYERS];
  int16_t action_frame[MSL_MAX_PLAYERS];
  uint8_t jumps_left[MSL_MAX_PLAYERS];
  uint8_t stocks[MSL_MAX_PLAYERS];

  float percent[MSL_MAX_PLAYERS];
  float shield_hp[MSL_MAX_PLAYERS];
  uint16_t hitlag[MSL_MAX_PLAYERS];
  uint16_t hitstun[MSL_MAX_PLAYERS];
  uint8_t l_cancel[MSL_MAX_PLAYERS];
  uint8_t hurtbox_state[MSL_MAX_PLAYERS];
  uint16_t ground_id[MSL_MAX_PLAYERS];
  uint32_t animation_index[MSL_MAX_PLAYERS];
  uint16_t instance_hit_by[MSL_MAX_PLAYERS];
  uint16_t instance_id[MSL_MAX_PLAYERS];
  uint8_t last_attack_landed[MSL_MAX_PLAYERS];
  uint8_t combo_count[MSL_MAX_PLAYERS];
  uint8_t last_hit_by[MSL_MAX_PLAYERS];
  uint8_t _pad2[1];
  uint8_t state_flags[MSL_MAX_PLAYERS][5];

  MslItem items[MSL_MAX_ITEMS];
} MslCompare;

typedef struct MslDatasetHeader {
  char magic[8];  // "MSLDSLT "
  uint32_t record_size;
  uint32_t num_records;
  uint8_t num_players;  // 2 or 4
  uint8_t _pad0[3];
} MslDatasetHeader;

typedef struct MslSample {
  MslSeed seed_t;
  MslInput prev_input_t;
  MslInput input_t;
  MslCompare ref_t1;
} MslSample;

// Debug/validation helper: read a small set of internal locomotion/input-history fields.
// This struct is packed for stable C<->Python inspection in tests.
typedef struct MslDebugInternals {
  uint8_t tilt_timer_x[MSL_MAX_PLAYERS];             // fp->x670_timer_lstick_tilt_x
  uint8_t turn_frames_to_turn[MSL_MAX_PLAYERS];      // fp->mv.co.turn.frames_to_turn
  uint8_t turn_has_turned[MSL_MAX_PLAYERS];          // fp->mv.co.turn.has_turned
  uint8_t guard_reflect_timer_x14[MSL_MAX_PLAYERS];  // mv.co.guard.x14 (+1 bias; see MslSeed)
  uint8_t entry_end_fall_lock[MSL_MAX_PLAYERS];  // hidden EntryEnd -> Fall airborne-control lock
  // Fighter attack identity internals (decomp: fp->x2068 / fp->x206C).
  uint16_t attack_id[MSL_MAX_PLAYERS];
  uint16_t attack_instance[MSL_MAX_PLAYERS];
  uint16_t attack_identity_last_action_id[MSL_MAX_PLAYERS];
  // Fighter action-state instance_id internals (decomp: fp->x2088 + fp->x2073 compare gate).
  uint16_t instance_id[MSL_MAX_PLAYERS];
  uint8_t instance_id_x2073[MSL_MAX_PLAYERS];
  uint16_t instance_identity_last_action_id[MSL_MAX_PLAYERS];
  // Per-environment global counter backing plAttack_80037B08 (unk_804D6480).
  uint16_t instance_id_counter;
  // One-step seed bridge lane for throw_flags_b0 pulse-consume ownership.
  uint8_t throw_pulse_consumed[MSL_MAX_PLAYERS];
  // One-step seed bridge lane carrying previous-step throw pulse crossing frame (0 = none).
  uint8_t throw_pulse_crossed_prev_frame[MSL_MAX_PLAYERS];
  uint8_t throw_pending_victim_port[MSL_MAX_PLAYERS];
  uint8_t throw_pending_hit_idx[MSL_MAX_PLAYERS];
  uint8_t attached_victim_port[MSL_MAX_PLAYERS];
} MslDebugInternals;

// Debug/test-only helper: write per-player stage collision contact metadata.
//
// Not a stable API: intended for suite debugging and synthetic tests only, and may change/remove
// fields without a seed/compare schema version bump.
//
// Mirrors the subset of CollData tracked by this lite sim's mpColl substrates.
typedef struct MslDebugCollisionContacts {
  uint8_t wall_kind[MSL_MAX_PLAYERS];  // 0=none, 1=left_wall, 2=right_wall
  uint8_t _pad0[MSL_MAX_PLAYERS];
  uint16_t wall_id[MSL_MAX_PLAYERS];  // ISO-derived segment index, or 0xFFFF
  float wall_contact_x[MSL_MAX_PLAYERS];
  float wall_contact_y[MSL_MAX_PLAYERS];
  float wall_normal_x[MSL_MAX_PLAYERS];
  float wall_normal_y[MSL_MAX_PLAYERS];

  uint16_t ceiling_id[MSL_MAX_PLAYERS];  // ISO-derived segment index, or 0xFFFF
  uint16_t _pad1[MSL_MAX_PLAYERS];
  float ceiling_contact_x[MSL_MAX_PLAYERS];
  float ceiling_contact_y[MSL_MAX_PLAYERS];
  float ceiling_normal_x[MSL_MAX_PLAYERS];
  float ceiling_normal_y[MSL_MAX_PLAYERS];

  uint32_t coll_env_flags[MSL_MAX_PLAYERS];
  uint32_t coll_prev_env_flags[MSL_MAX_PLAYERS];
  uint8_t damage_hitlag_wall_asdi_latch[MSL_MAX_PLAYERS];
} MslDebugCollisionContacts;

// Debug/validation helper: record a single hitbox-vs-hurtcap contact candidate.
// This is a compact snapshot of the world-space primitives used in combat pass 1.
typedef struct MslDebugCombatContact {
  uint8_t attacker;    // player index
  uint8_t defender;    // player index
  uint8_t hitbox_id;   // 0..MSL_MAX_HITBOXES-1
  uint8_t hurtcap_id;  // 0..MSL_MAX_HURTCAPS-1 (world array index)

  uint16_t attacker_msid;  // Slippi post-frame `animation_index` truncated to u16
  int16_t attacker_action_frame;

  float hitbox_x;
  float hitbox_y;
  float hitbox_z;
  float hitbox_radius;
  float hitbox_damage;

  float hurtcap_ax;
  float hurtcap_ay;
  float hurtcap_az;
  float hurtcap_bx;
  float hurtcap_by;
  float hurtcap_bz;
  float hurtcap_radius;
} MslDebugCombatContact;

// Debug/validation helper: record a single "would-hit" contact candidate, classified as BODY or
// SHIELD without mutating gameplay state.
//
// contact_kind:
//  0 = BODY   (hitbox intersects any hurtcap, and does NOT intersect shield bubble)
//  1 = SHIELD (hitbox intersects defender shield bubble)
typedef struct MslDebugCombatContactClassified {
  uint8_t attacker;      // player index
  uint8_t defender;      // player index
  uint8_t hitbox_id;     // 0..MSL_MAX_HITBOXES-1
  uint8_t contact_kind;  // 0=BODY, 1=SHIELD

  // BODY: 0..MSL_MAX_HURTCAPS-1 (world array index)
  // SHIELD: 0xFF
  uint8_t hurtcap_id;
  uint8_t _pad0[3];

  uint16_t attacker_msid;  // Slippi post-frame `animation_index` truncated to u16
  int16_t attacker_action_frame;

  float hitbox_x;
  float hitbox_y;
  float hitbox_z;
  float hitbox_radius;
  float hitbox_damage;

  // BODY payload (zeroed for SHIELD).
  float hurtcap_ax;
  float hurtcap_ay;
  float hurtcap_az;
  float hurtcap_bx;
  float hurtcap_by;
  float hurtcap_bz;
  float hurtcap_radius;

  // SHIELD payload (zeroed when defender shield bubble inactive).
  float shield_x;
  float shield_y;
  float shield_z;
  float shield_radius;
} MslDebugCombatContactClassified;

// Debug-only shield candidate observability for decomp-shaped pre-combat triage.
//
// This struct captures one candidate decision in the fighter shield-contact path owned by:
// - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
// - refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
//
// source_kind:
//  0 = fighter hitbox candidate (one row per attacker/defender/hitbox slot)
//  1 = pair gate (attacker/defender-level early reject; hitbox_id=0xFF)
//
// reject_reason:
//  0  = ACCEPT_SHIELD
//  1  = REJECT_ATTACKER_STOCKS_ZERO
//  2  = REJECT_DEFENDER_STOCKS_ZERO
//  3  = REJECT_TEAMS_FRIENDLY
//  4  = REJECT_HITLAG_GATE
//  5  = REJECT_SHIELD_INACTIVE
//  6  = REJECT_HITBOX_DISABLED
//  7  = REJECT_GROUND_AIR_FLAGS
//  8  = REJECT_HITLIST_CONTAINS
//  9  = REJECT_SHIELD_GEOM_NO_OVERLAP
//  10 = REJECT_INERT_ELEMENT
//  11 = REJECT_NONPOS_DAMAGE
//  12 = REJECT_EARLIER_BODY_HITCAPSULE
enum {
  MSL_DEBUG_SHIELD_SOURCE_FIGHTER_HITBOX = 0,
  MSL_DEBUG_SHIELD_SOURCE_PAIR_GATE = 1,
};

enum {
  MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD = 0,
  MSL_DEBUG_SHIELD_REJECT_ATTACKER_STOCKS_ZERO = 1,
  MSL_DEBUG_SHIELD_REJECT_DEFENDER_STOCKS_ZERO = 2,
  MSL_DEBUG_SHIELD_REJECT_TEAMS_FRIENDLY = 3,
  MSL_DEBUG_SHIELD_REJECT_HITLAG_GATE = 4,
  MSL_DEBUG_SHIELD_REJECT_SHIELD_INACTIVE = 5,
  MSL_DEBUG_SHIELD_REJECT_HITBOX_DISABLED = 6,
  MSL_DEBUG_SHIELD_REJECT_GROUND_AIR_FLAGS = 7,
  MSL_DEBUG_SHIELD_REJECT_HITLIST_CONTAINS = 8,
  MSL_DEBUG_SHIELD_REJECT_SHIELD_GEOM_NO_OVERLAP = 9,
  MSL_DEBUG_SHIELD_REJECT_INERT_ELEMENT = 10,
  MSL_DEBUG_SHIELD_REJECT_NONPOS_DAMAGE = 11,
  MSL_DEBUG_SHIELD_REJECT_EARLIER_BODY_HITCAPSULE = 12,
};

typedef struct MslDebugShieldCandidateDecision {
  uint8_t source_kind;    // MSL_DEBUG_SHIELD_SOURCE_*
  uint8_t attacker;       // player index
  uint8_t defender;       // player index
  uint8_t hitbox_id;      // 0..3, or 0xFF for pair gate rows
  uint8_t reject_reason;  // MSL_DEBUG_SHIELD_* reason enum

  uint8_t attacker_hitlag_started_frame;
  uint8_t defender_hitlag_started_frame;
  uint8_t shield_active;
  uint8_t hitbox_enabled;
  uint8_t defender_on_ground;
  uint8_t hitlist_allows;
  uint8_t overlap_shield;
  uint8_t element;

  uint16_t hb_flags;
  uint16_t attacker_msid;
  int16_t attacker_action_frame;

  float hitbox_damage;
  float hitbox_x;
  float hitbox_y;
  float hitbox_z;
  float hitbox_radius;

  float shield_x;
  float shield_y;
  float shield_z;
  float shield_radius;
  // Positive means overlap by this margin; negative means separation.
  float shield_overlap_margin;
} MslDebugShieldCandidateDecision;

#pragma pack(pop)

// Debug-only: decomp-shaped HitCapsule victim list dump for one fighter hitbox slot.
//
// Purpose:
// - Triage rehit-suppression and enable-edge copy/clear semantics (ftColl_800768A0 /
//   lbColl_CopyHitCapsule / lbColl_80008440) under teacher-forced reseed.
//
// Decomp anchors:
// - HitCapsule victim lists: refs/melee/src/melee/lb/types.h::HitCapsule
// - Clear: refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
// - Copy: refs/melee/src/melee/lb/lbcollision.c::lbColl_CopyHitCapsule
// - Gate check: refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC (uses victims_1)
//
// NOTE: This struct is debug-only and not part of the stable dataset/compare schema.
#pragma pack(push, 1)
typedef struct MslDebugHitlistVictimEntry {
  uint32_t id32;
  uint16_t id16;
  uint8_t kind_slot;
  uint8_t cd;
} MslDebugHitlistVictimEntry;

typedef struct MslDebugHitlistCapsule {
  uint8_t ring_1;
  uint8_t ring_2;
  uint8_t _pad0[2];

  MslDebugHitlistVictimEntry victims_1[12];  // decomp: HitCapsule.victims_1[12]
  MslDebugHitlistVictimEntry victims_2[12];  // decomp: HitCapsule.victims_2[12]
} MslDebugHitlistCapsule;
#pragma pack(pop)

// Debug-only: hitbox event timing snapshot for one fighter + hitbox slot.
//
// This is intended for triage of "enable-edge-only" contacts where a hitbox overlaps a hurtcap on
// the single frame it becomes enabled, and we need to inspect whether our per-frame event gating
// matches decomp-shaped cmd script scheduling.
//
// Decomp anchor for float-driven command scheduling:
// refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
// refs/melee/src/melee/lb/lbcommand.c::{Command_02,Command_08}
//
// Value semantics:
// - kind: 0=create (spawn/refresh), 1=clear (despawn), 0xFF=none
// - start_frame/end_frame: active window for hb_id at the sampled pose_frame. end_frame is the
//   next clear frame affecting hb_id or clear-all, or -1 if none found.
//
// NOTE: This struct is debug-only and not part of the stable dataset/compare schema.
#pragma pack(push, 1)
typedef struct MslDebugHitboxEventTiming {
  uint8_t attacker;
  uint8_t hb_id;
  uint8_t char_id;
  uint8_t _pad0;

  uint16_t msid;
  uint16_t pose_frame;

  float anim_frame_f32;
  float frame_speed_mul_f32;

  int16_t start_frame;
  int16_t end_frame;

  uint8_t enabled_prev;
  uint8_t enabled_cur;
  uint8_t prev_hit_group;
  uint8_t cur_hit_group;

  uint8_t pose_create_count;
  uint8_t pose_clear_count;
  uint8_t pose_clear_all_count;
  uint8_t enable_edge;

  uint8_t last_affect_kind_le;
  uint8_t last_affect_kind_eq;
  uint16_t last_affect_frame_le;
  uint16_t last_affect_frame_eq;
  uint16_t last_affect_u16_7_le;
  uint16_t last_affect_u16_7_eq;
} MslDebugHitboxEventTiming;
#pragma pack(pop)

// Debug-only: hurtcap slot eligibility snapshot for one fighter + cap id.
//
// This captures the runtime world-slot state (enabled/height/grabbable) plus the
// movescript-derived hurtbox mode mask bit for the sampled (char_id, msid, frame).
//
// Decomp anchors:
// - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 (fighter-vs-fighter path checks hurt state)
// - refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C (collision helper consumes hurt capsules)
#pragma pack(push, 1)
typedef struct MslDebugHurtcapSlotFlags {
  uint8_t enabled;           // world slot enabled flag after hurtboxes_refresh()
  uint8_t height;            // hurtcap height class (0/1/2)
  uint8_t is_grabbable;      // hurtcap grabbable flag
  uint8_t mode_can_hit_bit;  // bit from hurtbox_modes_can_hit_mask()

  uint8_t char_id;
  uint8_t _pad0;
  uint16_t msid;
  uint16_t frame;
  uint16_t cap_id;
  uint32_t can_hit_mask;
} MslDebugHurtcapSlotFlags;
#pragma pack(pop)

// Debug-only: hitbox sweep proxy for one fighter + hitbox slot.
//
// Purpose:
// - Approximate decomp lbColl_80006E58 inputs that use hit sweep endpoints (hit->x58, hit->x4C),
//   by exposing a best-effort previous/current center pair in world space.
//
// Modeling notes:
// - `cur_*` is sampled at pose_frame=floor(cur_anim_frame) from extracted create_hitbox def.
// - `prev_*` is sampled at pose_frame=floor(cur_anim_frame - frame_speed_mul) using the same
//   event table/effective slot def at that previous integer frame.
// - This is instrumentation-only and may diverge from engine-internal per-frame x58/x4C when
//   runtime-only flags/state not represented in extracted tables are involved.
//
// Runtime-only gating note:
// - Decomp ftColl_80078C70 sets arg3 (`var_r22`) from HitCapsule.x43_b2 and lbColl_8000805C
//   short-circuits acceptance when arg3 != 0.
// - Runtime carries a minimal x43_b2 lane for combat ownership, but extracted hitbox tables
//   (MSLHITB1) still do not encode this bit.
#pragma pack(push, 1)
typedef struct MslDebugHitboxSweepProxy {
  uint8_t attacker;
  uint8_t hb_id;

  uint8_t enabled_prev;
  uint8_t enabled_cur;
  uint8_t prev_valid;
  uint8_t cur_valid;
  uint8_t _pad0[2];

  uint16_t msid;
  uint16_t pose_prev;
  uint16_t pose_cur;
  uint8_t char_id;
  uint8_t _pad1;

  float anim_frame_f32;
  float prev_anim_frame_f32;
  float frame_speed_mul_f32;

  float prev_x;
  float prev_y;
  float prev_z;
  float prev_radius;

  float cur_x;
  float cur_y;
  float cur_z;
  float cur_radius;

  uint16_t u16_6_prev;
  uint16_t u16_7_prev;
  uint16_t u16_6_cur;
  uint16_t u16_7_cur;

  uint8_t arg3_var_r22_known;            // 1=runtime model carries the lane
  uint8_t arg3_var_r22_from_extracted;   // 0=not present in MSLHITB1
  uint8_t arg3_var_r22_gates_collision;  // 1=decomp says it gates lbColl_8000805C acceptance
  uint8_t _pad2;
} MslDebugHitboxSweepProxy;

// Debug-only: current dynamic-chain collision pose state for one fighter.
//
// This exists to prove the reseed contract for `SSDYNN01`: non-sequential teacher-forced seeds
// rebuild action-local dynamic state by replaying the deterministic update from frame 0, while
// normal rollout carries this state frame-to-frame. It is debug/test tooling only.
typedef struct MslDebugDynamicPoseState {
  uint8_t player;
  uint8_t char_id;
  uint8_t state_valid;
  uint8_t apply_collision_matrix;
  uint8_t node_count;
  uint8_t _pad0[3];
  uint16_t msid;
  uint16_t frame;
  float rot_x[MSL_MAX_DYNAMIC_NODES];
  float rot_y[MSL_MAX_DYNAMIC_NODES];
  float rot_z[MSL_MAX_DYNAMIC_NODES];
  float pos_x[MSL_MAX_DYNAMIC_NODES];
  float pos_y[MSL_MAX_DYNAMIC_NODES];
  float pos_z[MSL_MAX_DYNAMIC_NODES];
  float axis_x[MSL_MAX_DYNAMIC_NODES];
  float axis_y[MSL_MAX_DYNAMIC_NODES];
  float axis_z[MSL_MAX_DYNAMIC_NODES];
  float angle[MSL_MAX_DYNAMIC_NODES];
} MslDebugDynamicPoseState;
#pragma pack(pop)

// -------------
// Simulator core
// -------------

typedef struct MslBatch MslBatch;

// Allocates a batched simulator handle and all internal SoA buffers.
// This is the only place allocations are permitted.
MslBatch* msl_batch_create(int batch_size, int num_players);

void msl_batch_destroy(MslBatch* batch);

int msl_batch_batch_size(const MslBatch* batch);
int msl_batch_num_players(const MslBatch* batch);

// Mutate small runtime toggles. Safe to call after create; does not allocate.
int msl_batch_set_ucf_enabled(MslBatch* batch, int enabled);
int msl_batch_set_ucf_cardinals_1_0_enabled(MslBatch* batch, int enabled);

// Initialize each environment from a match config, without replay seed data.
// configs length is batch_size; config_stride_bytes must be >= sizeof(MslMatchConfig).
int msl_batch_init_match(MslBatch* batch, const uint8_t* config_bytes, size_t config_stride_bytes);
// Masked variant for vector-env reset. mask entries are uint8_t 0/1; rows with 0 are untouched.
int msl_batch_init_match_masked(MslBatch* batch, const uint8_t* config_bytes,
                                size_t config_stride_bytes, const uint8_t* mask_bytes,
                                size_t mask_stride_bytes);

// Reseed from packed MslSeed array of length batch_size.
// seed_stride_bytes must be >= sizeof(MslSeed).
int msl_batch_reseed_seed(MslBatch* batch, const uint8_t* seed_bytes, size_t seed_stride_bytes);
// Reseed for validation rollouts that need simulator-owned replay frame-clock metadata.
// Normal teacher-forced one-step callers should use msl_batch_reseed_seed().
int msl_batch_reseed_seed_rollout(MslBatch* batch, const uint8_t* seed_bytes,
                                  size_t seed_stride_bytes);

// Step one frame using packed inputs. The current "empty sim" stub ignores inputs.
// input_stride_bytes must be >= sizeof(MslInput).
int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes);

// Write packed compare outputs (length batch_size).
// out_stride_bytes must be >= sizeof(MslCompare).
int msl_batch_write_compare(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes);
// Write one compact RL observation per batch row. viewpoint_players are 0-based player indices.
int msl_batch_write_rl_observation(const MslBatch* batch, const uint8_t* viewpoint_player_bytes,
                                   size_t viewpoint_player_stride_bytes, uint8_t* out_bytes,
                                   size_t out_stride_bytes);
// Write terminal flags. max_frame_id < 0 disables the max-frame done condition.
int msl_batch_write_terminal(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes,
                             int32_t max_frame_id);

// Debug/validation helper: write current processed input values.
// out_stride_bytes must be >= sizeof(MslProcessedInput).
int msl_batch_debug_write_processed_input(const MslBatch* batch, uint8_t* out_bytes,
                                          size_t out_stride_bytes);

// Debug/validation helper: write selected internal fields.
// out_stride_bytes must be >= sizeof(MslDebugInternals).
int msl_batch_debug_write_internals(const MslBatch* batch, uint8_t* out_bytes,
                                    size_t out_stride_bytes);

// Debug/test-only helper: write stage collision contacts (length batch_size).
// out_stride_bytes must be >= sizeof(MslDebugCollisionContacts).
int msl_batch_debug_write_collision_contacts(const MslBatch* batch, uint8_t* out_bytes,
                                             size_t out_stride_bytes);

// Debug/validation helper: force an animation timebase reset for a single fighter without
// changing action_id (test-only). This is useful to validate that mechanics keyed to action
// transitions (e.g., fighter attack identity) are not accidentally updated by "anim restarts".
int msl_batch_debug_force_anim_timebase_enter(MslBatch* batch, int batch_index, int player_index,
                                              float anim_start_f32, float anim_speed_f32);

// Debug-only helper: run one frame up to (but excluding) combat_resolve().
//
// IMPORTANT CONTRACT:
// - This is for instrumentation/triage only; do not use for training/rollouts.
// - It intentionally excludes combat_resolve(), so outputs are not comparable to `step_input()`.
// - It still advances and mutates batch state through all earlier frame stages (anim/input/action/
//   physics/collision/hitbox/hurtbox/item updates), exactly as a normal frame would before combat.
int msl_batch_debug_step_input_pre_combat(MslBatch* batch, const uint8_t* prev_input_bytes,
                                          size_t prev_input_stride_bytes,
                                          const uint8_t* input_bytes, size_t input_stride_bytes);

// Debug-only branch-isolation helper: run only the knockdown/damage pre-physics callback slice on
// the current reseeded state, without timers/input/physics.
//
// IMPORTANT CONTRACT:
// - This is for tests/triage only; do not use for training/rollouts.
// - It intentionally bypasses earlier timer/state-flag maintenance so branch-local ownership can be
//   asserted in isolation.
int msl_batch_debug_knockdown_update_pre_physics(MslBatch* batch);

// Debug-only helper: recompute pose-driven combat geometry from current state (hurtcaps + hitboxes)
// without advancing frame stages.
//
// This mutates debug geometry arrays in-place from the current anim/action state and is intended
// only for triage (e.g., comparing floor/ceil pose samples on the same seeded snapshot).
int msl_batch_debug_refresh_combat_geometry(MslBatch* batch);

// Debug-only helper: read per-player animation/script timebase + pose sampling frame for a batch
// element.
//
// Writes `MSL_MAX_PLAYERS * 8` floats into out_rows_8p as rows:
//   [action_id, animation_index, action_frame, anim_frame_f32_sanitized, frame_speed_mul_f32,
//    pose_frame, hitlag_started_frame, hurtbox_state]
int msl_batch_debug_timebase(const MslBatch* batch, int batch_index, float* out_rows_8p);

// Debug-only helper: inspect extracted hitbox events and the runtime's pose_frame gating result for
// a single fighter hitbox slot on this step.
int msl_batch_debug_hitbox_event_timing(const MslBatch* batch, int batch_index, int attacker,
                                        int hb_id, MslDebugHitboxEventTiming* out_timing);

// Debug-only helper: inspect one hitbox slot's sweep proxy inputs (`prev`/`cur`) for decomp-shaped
// collision triage.
int msl_batch_debug_hitbox_sweep_proxy(const MslBatch* batch, int batch_index, int attacker,
                                       int hb_id, MslDebugHitboxSweepProxy* out_proxy);

// Debug-only helper: inspect current dynamic-chain state for reseed/rollout equivalence tests.
int msl_batch_debug_dynamic_pose_state(const MslBatch* batch, int batch_index, int player_index,
                                       MslDebugDynamicPoseState* out_state);

// Debug-only helper: inspect one hurtcap slot's runtime eligibility for a fighter on this step.
int msl_batch_debug_hurtcap_slot_flags(const MslBatch* batch, int batch_index, int player_index,
                                       int cap_id, MslDebugHurtcapSlotFlags* out_flags);
int msl_batch_debug_hurtcap_geometry_valid(const MslBatch* batch, int batch_index, int player_index,
                                           uint8_t* out_valid);
// Debug/testing helper: return the current exact AttackAirB continuation overlap amount for one
// attacker hitbox / defender hurtcap pair.
int msl_batch_debug_attackairb_continuation_overlap(const MslBatch* batch, int batch_index,
                                                    int attacker, int hb_id, int defender,
                                                    int cap_id, float* out_overlap);
int msl_batch_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                        int hb_id, int defender, int cap_id, float* out_overlap);

// Debug/validation helper: read pose-driven world-space hurt capsules for a single fighter.
//
// Normal runtime may skip endpoint sampling for players with no current collision demand. This
// debug helper materializes skipped hurtcap geometry on demand before reading it, so it mutates the
// batch and is not logically const.
// Writes `MSL_MAX_HURTCAPS * 7` floats into out_caps_7 as rows:
//   [ax, ay, az, bx, by, bz, radius]
// and returns the active capsule count in out_count.
int msl_batch_debug_hurtcaps_world(MslBatch* batch, int batch_index, int player_index,
                                   float* out_caps_7, uint8_t* out_count);

// Debug/validation helper: read pose-driven world-space hitbox centers for a single fighter.
// Writes `MSL_MAX_HITBOXES * 10` floats into out_hitboxes_10 as rows:
//   [x, y, z, radius, damage, u16_0, u16_1, u16_3, bone_part_id, enabled]
// and returns the active hitbox count in out_count.
int msl_batch_debug_hitboxes_world(const MslBatch* batch, int batch_index, int player_index,
                                   float* out_hitboxes_10, uint8_t* out_count);

// Debug/validation helper: read pose-driven world-space hitboxes with full decoded attributes.
// Writes `MSL_MAX_HITBOXES * 16` floats into out_hitboxes_16 as rows:
//   [x, y, z, radius, damage, angle, kbg, wsk, bkb, element, shield_damage, sfx_severity, sfx_kind,
//    flags, bone_part_id, enabled]
// and returns the active hitbox count in out_count.
int msl_batch_debug_hitboxes_world_full(const MslBatch* batch, int batch_index, int player_index,
                                        float* out_hitboxes_16, uint8_t* out_count);

// Debug/validation helper: compute and dump hitbox-vs-hurtcap contacts for one batch element.
// Writes up to max_contacts entries into out_contacts and returns the number written in out_count.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// hitbox_id 0..3, hurtcap_id 0..count-1.
int msl_batch_debug_combat_contacts(const MslBatch* batch, int batch_index,
                                    MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                    uint16_t* out_count);

// Debug/validation helper: compute hitbox-vs-hurtcap contacts with decomp-shaped gating.
//
// This is a debug-only filter pass: it does not mutate validated gameplay state. The intent is to
// keep the overlap diagnostics closer to "real combat" without enabling percent/hitlag/hitstun.
//
// Current filters:
// - Victim ground/air eligibility: HIT_GROUNDED / HIT_AERIAL from extracted hitbox flags
//   (MSLHITB1 u16_6; decoded as state.hitbox_flags).
//
// Deterministic ordering matches msl_batch_debug_combat_contacts; filters only skip/keep.
int msl_batch_debug_combat_contacts_filtered(const MslBatch* batch, int batch_index,
                                             MslDebugCombatContact* out_contacts,
                                             uint16_t max_contacts, uint16_t* out_count);

// Debug/validation helper: compute hitbox-vs-shield and hitbox-vs-hurtcap contacts for one batch
// element, classifying each as BODY or SHIELD.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// hitbox_id 0..3, and within a hitbox: SHIELD first, then BODY (if applicable).
int msl_batch_debug_combat_contacts_classified(const MslBatch* batch, int batch_index,
                                               MslDebugCombatContactClassified* out_contacts,
                                               uint16_t max_contacts, uint16_t* out_count);

// Deterministic ordering matches msl_batch_debug_combat_contacts_classified; filters only skip/keep.
int msl_batch_debug_combat_contacts_classified_filtered(
    const MslBatch* batch, int batch_index, MslDebugCombatContactClassified* out_contacts,
    uint16_t max_contacts, uint16_t* out_count);

// Debug/validation helper: emit decomp-shaped shield-candidate gate decisions (non-mutating).
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// first one pair-gate row (hitbox_id=0xFF), then hitbox rows hitbox_id 0..3 when pair gates pass.
int msl_batch_debug_shield_candidate_decisions(MslBatch* batch, int batch_index,
                                               MslDebugShieldCandidateDecision* out_rows,
                                               uint16_t max_rows, uint16_t* out_count);

// Debug/validation helper: write per-player shield bubble world params for a batch element.
// Writes `MSL_MAX_PLAYERS * 4` floats into out_xyzw_4p as rows: [x, y, z, radius].
int msl_batch_debug_shield_bubbles_world(const MslBatch* batch, int batch_index,
                                         float* out_xyzw_4p);

// Debug/testing helper: allow unit tests to write world-space primitives directly and invoke combat
// without touching upstream pose systems.
int msl_batch_debug_clear_hitboxes_world(MslBatch* batch, int batch_index, int player_index);
int msl_batch_debug_set_hitbox_world(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, float x, float y, float z, float radius,
                                     float damage, int enabled);
int msl_batch_debug_set_hitbox_flags(MslBatch* batch, int batch_index, int player_index,
                                     int hitbox_id, uint16_t hitbox_flags);
int msl_batch_debug_set_hitbox_element(MslBatch* batch, int batch_index, int player_index,
                                       int hitbox_id, uint8_t element);
int msl_batch_debug_set_hitbox_kb_params(MslBatch* batch, int batch_index, int player_index,
                                         int hitbox_id, uint16_t angle_deg, uint16_t kbg,
                                         uint16_t wsk, uint16_t bkb);
int msl_batch_debug_clear_hurtcaps_world(MslBatch* batch, int batch_index, int player_index);
int msl_batch_debug_set_hurtcap_world(MslBatch* batch, int batch_index, int player_index,
                                      int hurtcap_id, float ax, float ay, float az, float bx,
                                      float by, float bz, float radius);
int msl_batch_debug_set_hurtcap_height(MslBatch* batch, int batch_index, int player_index,
                                       int hurtcap_id, uint8_t height);
int msl_batch_debug_set_hurtcap_enabled(MslBatch* batch, int batch_index, int player_index,
                                        int hurtcap_id, int enabled);
int msl_batch_debug_set_hitlag(MslBatch* batch, int batch_index, int player_index,
                               uint16_t hitlag_frames);
// Debug/testing helper: override movescript-derived hit status (opcode 26) eligibility.
// - Pass status=-1 to clear the override (use extracted tables).
// - Otherwise status must fit in u8 (0=normal, 1=invincible, 2=intangible in current decomp domain).
int msl_batch_debug_set_hit_status_override(MslBatch* batch, int batch_index, int player_index,
                                            int status);
int msl_batch_debug_combat_resolve(MslBatch* batch);

// Debug/testing only: run combat pass-1 BODY-hit selection (non-mutating) and return the chosen
// contacts in deterministic order (at most 1 per attacker→defender per call).
int msl_batch_debug_combat_select_body_hits(MslBatch* batch, int batch_index,
                                            MslDebugCombatContact* out_contacts,
                                            uint16_t max_contacts, uint16_t* out_count);

// Debug/testing helper: inspect decomp-shaped fighter HitCapsule victim lists.
// Returns `*out_present = 1` if victim port is present in victims_1 for (attacker, hb_id), else 0.
int msl_batch_debug_hitlist_fighter_contains(const MslBatch* batch, int batch_index, int attacker,
                                             int hb_id, int victim, int* out_present);

// Debug/testing helper: dump a fighter hitbox slot's HitCapsule victim lists and ring indices.
int msl_batch_debug_hitlist_fighter_capsule(const MslBatch* batch, int batch_index, int attacker,
                                            int hb_id, MslDebugHitlistCapsule* out_capsule);

// Debug/testing helper: pure geometry routine for unit tests.
int msl_debug_point_segment_dist2(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz, float* out_d2, float* out_t);

// Test/debug helper: reset selected global init-time tables that depend on MSL_DATA_DIR so they can
// be reloaded within the same process. This exists for synthetic tests; do not call while any live
// batches exist.
int msl_debug_reset_pose_and_hitboxes_tables(void);

#ifdef __cplusplus
}
#endif
