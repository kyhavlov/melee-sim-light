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
enum { MSL_STATE_FLAGS_BYTES = 5 };
// Decomp: `spawn_hitbox_0.hit_group` is a 3-bit field (0..7).
// refs/melee/src/melee/lb/types.h::spawn_hitbox_0
enum { MSL_HITLIST_GROUPS = 8 };
// Decomp: Player's `StaleMoveTable.StaleMoves[10]` ring buffer (current_index wraps at 9).
// refs/melee/src/melee/pl/types.h::StaleMoveTable and refs/melee/src/melee/pl/plstale.c
enum { MSL_STALE_QUEUE_SIZE = 10 };

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
  // Per-player damage ratios (decomp: Player_GetAttackRatio / Player_GetDefenseRatio).
  // refs/melee/src/melee/pl/player.c::Player_GetAttackRatio
  // refs/melee/src/melee/pl/player.c::Player_GetDefenseRatio
  float attack_ratio[MSL_MAX_PLAYERS];
  float defense_ratio[MSL_MAX_PLAYERS];

  // Kinematics
  float pos_x[MSL_MAX_PLAYERS];
  float pos_y[MSL_MAX_PLAYERS];
  float pos_z[MSL_MAX_PLAYERS];
  // Velocities as recorded by Slippi post-frame (when available).
  float speed_air_x_self[MSL_MAX_PLAYERS];
  float speed_ground_x_self[MSL_MAX_PLAYERS];
  float speed_y_self[MSL_MAX_PLAYERS];
  float speed_x_attack[MSL_MAX_PLAYERS];
  float speed_y_attack[MSL_MAX_PLAYERS];
  // Fighter model scale (decomp: fp->x34_scale.y). Slippi exposes this from the game-start block
  // (player.model_scale), but legacy datasets may still default it to 1.0.
  float fighter_scale_y[MSL_MAX_PLAYERS];

  uint8_t facing[MSL_MAX_PLAYERS];     // 0/1
  uint8_t on_ground[MSL_MAX_PLAYERS];  // 0/1
  uint8_t _pad1[2];

  // State machine
  uint16_t action_id[MSL_MAX_PLAYERS];    // GALE01 action id
  int16_t action_frame[MSL_MAX_PLAYERS];  // action frame (can be negative in pre-start)
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
  uint8_t x677_y[MSL_MAX_PLAYERS];  // fp->x677_y ("age since last change", y)
  uint8_t x678[MSL_MAX_PLAYERS];    // fp->x678 ("age since last change", trigger)
  uint8_t x679_x[MSL_MAX_PLAYERS];  // fp->x679_x (lstick x companion)
  uint8_t x67A_y[MSL_MAX_PLAYERS];  // fp->x67A_y (lstick y companion)
  uint8_t x67B[MSL_MAX_PLAYERS];    // fp->x67B (trigger companion)
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
  // Damage jump-buffer snapshot (decomp: fp->mv.co.damage.x14).
  // - Cleared on damage entry (ftCo_8008DCE0).
  // - Set to hitstun timer (mv.co.damage.x0) on jump-input detect in doIasa.
  // - Gated against p_ftCommonData->x1D0 in Damage_Anim's inlineC0 jump path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,doIasa,ftCo_Damage_Anim}
  uint16_t damage_jump_buffer_x14[MSL_MAX_PLAYERS];
  uint8_t l_cancel[MSL_MAX_PLAYERS];
  uint8_t hurtbox_state[MSL_MAX_PLAYERS];  // 0 vuln, 1 invuln, 2 intangible
  uint16_t ground_id[MSL_MAX_PLAYERS];
  uint32_t animation_index[MSL_MAX_PLAYERS];
  uint16_t instance_hit_by[MSL_MAX_PLAYERS];
  uint16_t instance_id[MSL_MAX_PLAYERS];
  // Fighter action-state instance_id compare byte (GALE01 fp+0x2073 within fp->x2070).
  // Used by ft_800895E0 to gate instance_id bumps on motion-state change.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  uint8_t instance_id_x2073[MSL_MAX_PLAYERS];
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
  // Populated by replay-history preprocessing:
  // - tools/slippi/staling_history.py (derive) and tools/slippi/make_dataset_from_slp.py (wire).
  //
  // Runtime usage:
  // - src/combat.c applies the staling multiplier on damaging BODY hits and updates this queue on
  //   qualifying hits using the seeded `attack_instance` as source-of-truth.

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
// - We currently do not carry x43_b2 in runtime state/extracted tables for this sim path.
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

  uint8_t arg3_var_r22_known;            // 0=unknown in current runtime model
  uint8_t arg3_var_r22_from_extracted;   // 0=not present in MSLHITB1
  uint8_t arg3_var_r22_gates_collision;  // 1=decomp says it gates lbColl_8000805C acceptance
  uint8_t _pad2;
} MslDebugHitboxSweepProxy;
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

// Reseed from packed MslSeed array of length batch_size.
// seed_stride_bytes must be >= sizeof(MslSeed).
int msl_batch_reseed_seed(MslBatch* batch, const uint8_t* seed_bytes, size_t seed_stride_bytes);

// Step one frame using packed inputs. The current "empty sim" stub ignores inputs.
// input_stride_bytes must be >= sizeof(MslInput).
int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes);

// Write packed compare outputs (length batch_size).
// out_stride_bytes must be >= sizeof(MslCompare).
int msl_batch_write_compare(const MslBatch* batch, uint8_t* out_bytes, size_t out_stride_bytes);

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

// Debug-only helper: inspect one hurtcap slot's runtime eligibility for a fighter on this step.
int msl_batch_debug_hurtcap_slot_flags(const MslBatch* batch, int batch_index, int player_index,
                                       int cap_id, MslDebugHurtcapSlotFlags* out_flags);

// Debug/validation helper: read pose-driven world-space hurt capsules for a single fighter.
// Writes `MSL_MAX_HURTCAPS * 7` floats into out_caps_7 as rows:
//   [ax, ay, az, bx, by, bz, radius]
// and returns the active capsule count in out_count.
int msl_batch_debug_hurtcaps_world(const MslBatch* batch, int batch_index, int player_index,
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
