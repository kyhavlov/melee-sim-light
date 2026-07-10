#include "falcon_specials.h"

#include <math.h>

#include "action.h"
#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "batch_internal.h"
#include "buttons.h"
#include "char_params.h"
#include "char_registry.h"
#include "coll_env_flags.h"
// combat_internal.h provides idx_hitbox/idx_hurtcap, the hitbox target-flag predicate, and
// combat_geom.h's sphere/capsule overlap for the Raptor Boost inert-contact detect pass.
#include "combat_internal.h"
#include "common_params.h"
#include "dash_iasa.h"
#include "grab_attachment.h"
#include "grab_flow.h"
#include "ftcommon_ecb.h"
#include "ids.h"
#include "locomotion.h"
#include "move_tables.h"
#include "msl_math.h"
#include "state_flags.h"

// Falcon char-special action ids are the shared MSL_ACT_CA_* names (action_ids.h; 347..363,
// MSLMSO01-derived, ftCa_* Anim callbacks). The 341..346 item-swing states are common-owned
// and never enter through this module.

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static inline float fc_stick_unit(int8_t v) { return (float)v * (1.0f / 80.0f); }

static inline float fc_apply_deadzone(float v, float dz) { return (fabsf(v) < dz) ? 0.0f : v; }

static inline uint8_t fc_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(anim_frame_f32) >= end) ? 1u : 0u;
}

static inline void fc_enter_rate(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame,
                                 float rate) {
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (uint32_t)falcon_special_submotion(action_id);
  msl_anim_timebase_enter(batch, idx, start_frame, rate);
}

static inline void fc_enter(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame) {
  fc_enter_rate(batch, idx, action_id, start_frame, 1.0f);
}

// Falcon Kick on-hit friction lane. Entry and replay reseed both install the neutral 1.0 value;
// retain a zero guard for direct/manual seeds.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_Special_Inline_Friction
static inline float fc_speciallw_friction(const MslBatch* batch, size_t idx) {
  const float f = batch->state.falcon_speciallw_friction[idx];
  return (f > 0.0f) ? f : 1.0f;
}

static inline void fc_reset_cmds(MslBatch* batch, size_t idx) {
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

static inline float fc_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}

static inline void fc_apply_ftcommon_8007d60c(MslBatch* batch, size_t idx) {
  // Alternate ground-to-air helper used by Falcon's aerial Raptor Boost and connected Dive:
  // clear gr_vel, consume all jumps, and lock the ECB for five frames. It deliberately leaves
  // self_vel and the carried floor id intact.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
  batch->state.on_ground[idx] = 0u;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.jumps_left[idx] = 0u;
  msl_ftcommon_lock_ecb_8007d60c(batch, idx);
}

static uint8_t fc_transn_delta_85134_f32(const MslBatch* batch, const MslCharParams* ch, size_t idx,
                                         uint16_t msid, float frame, float* out_dx, float* out_dy) {
  if (batch == NULL || ch == NULL || out_dx == NULL || out_dy == NULL) {
    return 0u;
  }
  float t_cur[3];
  float t_prev[3];
  const float safe_frame = msl_anim_frame_sanitize_f32(frame);
  const float prev_frame = (safe_frame > 1.0f) ? (safe_frame - 1.0f) : 0.0f;
  if (anim_pose_get_transn_f32(batch->state.char_id[idx], msid, safe_frame, t_cur) != 0 ||
      anim_pose_get_transn_f32(batch->state.char_id[idx], msid, prev_frame, t_prev) != 0) {
    return 0u;
  }
  *out_dx = (t_cur[2] - t_prev[2]) * ch->model_scaling * fc_facing_dir(batch, idx);
  *out_dy = (t_cur[1] - t_prev[1]) * ch->model_scaling;
  return 1u;
}

static uint8_t fc_try_enter_air_b_special_from_fall_iasa(MslBatch* batch, const MslCommonParams* c,
                                                         const MslCharParams* ch, size_t idx);
static uint8_t fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch, size_t idx,
                                                               uint16_t source_action);

// ---------------------------------------------------------------------------
// Entries (decomp: ftCa_Special*_Enter)
// ---------------------------------------------------------------------------

static void fc_enter_specialn(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t on_ground) {
  // ftCa_SpecialN_Enter / ftCa_SpecialAirN_Enter: clear cmd vars + throw flags and change
  // motion state at frame 0. Unlike Marth's Shield Breaker there is NO entry velocity write;
  // the grounded wind-up is anim-root-motion-owned and the aerial variant keeps its drift
  // until the script's cmd lanes take over.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::{
  //   ftCa_SpecialN_Enter,ftCa_SpecialAirN_Enter}
  (void)ch;
  fc_reset_cmds(batch, idx);
  fc_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_CA_SPECIAL_N : (uint16_t)MSL_ACT_CA_SPECIAL_AIR_N, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void fc_enter_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t on_ground) {
  // ftCa_SpecialLw_Enter / ftCa_SpecialAirLw_Enter: clear cmd vars + throw flags. Only the
  // GROUNDED entry initializes mv.ca.speciallw (x0 hit count, friction 1.0) and installs the
  // deal_dmg_cb slowdown; the friction lanes are only consumed by SpecialLw/SpecialLwEnd,
  // both reachable exclusively from the grounded family.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
  //   ftCa_SpecialLw_Enter,ftCa_SpecialAirLw_Enter}
  (void)ch;
  fc_reset_cmds(batch, idx);
  if (on_ground) {
    batch->state.falcon_speciallw_hits[idx] = 0u;
    batch->state.falcon_speciallw_friction[idx] = 1.0f;
  }
  fc_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_CA_SPECIAL_LW : (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void fc_enter_specials(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t on_ground) {
  // ftCa_SpecialS_Enter / ftCa_SpecialAirS_Enter (via setupAirStart): clear cmd vars, ZERO
  // self velocity (both variants; the ground entry also zeroes gr_vel), and reset the
  // mv.ca.specials.grav accumulator for the air variant. The common SpecialS_CheckInput
  // doEnter xB8 damping is a dead write here because the char entry zeroes velocity anyway.
  // take_dmg/death2 callbacks (ftCa_Init_800E28C8) are gfx cleanup only.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
  //   ftCa_SpecialS_Enter,ftCa_SpecialAirS_Enter}
  (void)ch;
  fc_reset_cmds(batch, idx);
  batch->state.falcon_detect_pending[idx] = 0u;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  if (on_ground) {
    batch->state.speed_ground_x_self[idx] = 0.0f;
  } else {
    // ftCa_SpecialAirS_Enter calls ftCommon_8007D60C after setupAirStart, consuming Falcon's
    // remaining jumps for the aerial Raptor Boost freefall owner.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialAirS_Enter
    batch->state.falcon_specials_grav[idx] = 0.0f;
    fc_apply_ftcommon_8007d60c(batch, idx);
  }
  fc_enter(
      batch, idx,
      on_ground ? (uint16_t)MSL_ACT_CA_SPECIAL_S_START : (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START,
      0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void fc_enter_specialhi(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t on_ground) {
  // ftCa_SpecialHi_Enter / ftCa_SpecialAirHi_Enter: the x21EC mv-reset callback
  // (ftCa_SpecialLw_800E49FC) burns all jumps, clears cmd vars and zeroes the
  // mv.ca.specialhi lanes; ftCommon_8007E2D0(kind=2, ...) arms the command grab (modeled by
  // the falcon action gate in combat's catch-selection pass). cmd_vars[1] = specialhi_unk2
  // and mv.ca.specialhi.x0 = specialhi_air_var have no consumer in msid 307/308 or this
  // module's decomp slice and are not modeled (extraction-only attrs).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
  //   ftCa_SpecialHi_Enter,ftCa_SpecialAirHi_Enter,ftCa_SpecialLw_800E49FC}
  (void)ch;
  fc_reset_cmds(batch, idx);
  batch->state.jumps_left[idx] = 0u;
  batch->state.falcon_specialhi_vel_x[idx] = 0.0f;
  batch->state.falcon_specialhi_vel_y[idx] = 0.0f;
  batch->state.falcon_specialhi_x221b_b7[idx] = 0u;
  fc_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_CA_SPECIAL_HI : (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

// ---------------------------------------------------------------------------
// Anim-end exits
// ---------------------------------------------------------------------------

static void fc_exit_to_wait_or_fall(MslBatch* batch, const MslCommonParams* c,
                                    const MslCharParams* ch, size_t idx) {
  if (batch->state.on_ground[idx]) {
    // Grounded falcon special Anim callbacks exit through ft_8008A2BC -> Wait; the destination
    // Wait_IASA runs in the same Fighter_procUpdate pass.
    // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    const uint16_t source_action = batch->state.action_id[idx];
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    (void)fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, source_action);
  } else {
    // Aerial falcon special Anim callbacks exit through ftCo_Fall_Enter; the destination Fall
    // IASA runs in the same proc, so a same-frame B/jump edge can overwrite Fall immediately.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}
    msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
    if (fc_try_enter_air_b_special_from_fall_iasa(batch, c, ch, idx)) {
      return;
    }
    (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
  }
}

void falcon_specials_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(batch_index, p);
    if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
      continue;
    }
    const uint16_t a = batch->state.action_id[idx];
    // Consume-once cmd-var latch reconstruction (Slippi does not expose fp->cmd_vars): the
    // stored anim frame is the frame the last pre-seed update ran with, so the pulse was
    // already consumed iff the script lane reads latched at that frame. Without this, a
    // reseeded mid-action row re-fires the consume side effects the real game applied once
    // (SpecialAirN's velocity impulse, Special(Air)Hi's frame-13 B-reverse from the CURRENT
    // stick instead of the frame-13 stick, SpecialHiThrow0's x2_b0 phys-blend arm).
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_IASA
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_IASA,
    //   doAirIASA,ftCa_SpecialHiThrow0_Anim}
    if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_N || a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
      batch->state.special_cmd0[idx] = move_tables_special_cmd_var_value_at_frame(
          batch->state.char_id[idx], falcon_special_submotion(a), 0u,
          msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
    } else if (a == (uint16_t)MSL_ACT_CA_SPECIAL_HI || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI) {
      batch->state.special_cmd1[idx] = move_tables_special_cmd_var_value_at_frame(
          batch->state.char_id[idx], falcon_special_submotion(a), 0u,
          msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
    }
    if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S) {
      const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      const uint16_t msid = falcon_special_submotion(a);
      const uint8_t grav_owner = (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S)
                                     ? 1u
                                     : move_tables_special_cmd_var_u8_value_at_frame(
                                           batch->state.char_id[idx], msid, 1u, frame);
      if (grav_owner != 0u) {
        // Slippi exposes self_vel.y but not mv.ca.specials.grav. In aerial Raptor Boost's gravity
        // owner, Phys subtracts from that hidden accumulator and then copies it back to self_vel.y.
        // Seed it from the visible velocity so one-step reseeds continue the accumulator instead
        // of restarting at zero every row.
        // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
        //   ftCa_SpecialAirSStart_Phys,ftCa_SpecialAirS_Phys}
        batch->state.falcon_specials_grav[idx] = batch->state.speed_y_self[idx];
      }
    }
    if (a == (uint16_t)MSL_ACT_CA_SPECIAL_HI || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
        a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
      // SpecialHi_Phys invariant: post-frame self_vel = TransN_delta(frame) + mv.vel, and the
      // replay carries self_vel; recover mv.ca.specialhi.vel by subtracting the seeded frame's
      // TransN delta.
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      const uint16_t msid = falcon_special_submotion(a);
      const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      float anim_dx = 0.0f;
      float anim_dy = 0.0f;
      // ft_80085134 consumes the live AObj TransN offset. Use the SSANIMT1 f32 sampler for seed
      // inversion so the hidden mv.ca.specialhi.vel lane does not drift when the source FObj
      // value differs from the integer SSANIM01 tail.
      // refs/melee/src/melee/ft/ft_084E.c::ft_80085134
      // src/anim_pose.c::anim_pose_get_transn_f32
      (void)fc_transn_delta_85134_f32(batch, ch, idx, msid, frame, &anim_dx, &anim_dy);
      batch->state.falcon_specialhi_vel_x[idx] = batch->state.speed_air_x_self[idx] - anim_dx;
      batch->state.falcon_specialhi_vel_y[idx] = batch->state.speed_y_self[idx] - anim_dy;
    } else if (a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH) {
      // Attach mode (x221B_b7): set when the connect happened against a grounded victim.
      // Recover it from the serialized source bit first, then from the seeded hold linkage +
      // the victim's ground state for rows whose packed flags have not been modeled yet.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialLw_800E5128
      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      uint8_t b7 = (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221B_B7) ? 1u : 0u;
      for (int v = 0; v < num_players; v++) {
        if (b7 != 0u) {
          break;
        }
        if (v == p) {
          continue;
        }
        const size_t vidx = msl_idx_player(batch_index, v);
        if (batch->state.grab_owner_port[vidx] == (uint8_t)p &&
            batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN &&
            batch->state.on_ground[vidx] != 0u) {
          b7 = 1u;
          break;
        }
      }
      batch->state.falcon_specialhi_x221b_b7[idx] = b7;
    }
  }
}

// ---------------------------------------------------------------------------
// Falcon Dive catch resolution (doCatchAnim: SpecialHiCatch end -> SpecialHiThrow + release)
// ---------------------------------------------------------------------------

static inline uint8_t fc_action_is_damage_family(uint16_t action_id_u16) {
  // Same aftermath subset the throw-release owner checks (throw_flow.c).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  return (uint8_t)(msl_damage_owner_is_damage_ground_action(action_id_u16) ||
                   msl_damage_owner_is_damage_air_action(action_id_u16) ||
                   msl_damage_owner_is_damagefly_action(action_id_u16) ||
                   action_id_u16 == (uint16_t)MSL_ACT_DAMAGE_FALL);
}

// doCatchAnim (SpecialHiCatch anim end): the attacker enters SpecialHiThrow(356) with the mv
// vel lanes and x2_b0 cleared (no immediate anim tick: doCatchAnim has no ftAnim_8006EBA4),
// then the SAME throw-release owner ordinary throws use fires in the same callback:
// - ftCo_800DE2A8 (= ftCo_800DDDE4(attacker, victim, true)) applies Throw0's set_throw_hitbox
//   idx=0 (msid 310 frame 0: 12dmg kbg82 angle361 fire) to the victim, places the constrained
//   fighter (fp4 = x221B_b7 ? attacker : victim) at the anchor owner's FtPart_TransN2 world
//   plus its static x1A70 offsets, and sets it airborne (ftCommon_8007D5D4).
// - ftCo_800DE7C0(victim, NULL, false) enters the victim's damage aftermath through the
//   generic ftCo_8008DCE0 machinery (combat_apply_throw_hit models this pair end to end).
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::doCatchAnim
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DE2A8,ftCo_800DDDE4}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
static void fc_specialhi_do_catch_anim_release(MslBatch* batch, size_t idx) {
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int owner_p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  const int num_players = (int)batch->config.num_players;
  const size_t owner_221b_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  const uint8_t b7 =
      (uint8_t)(batch->state.falcon_specialhi_x221b_b7[idx] != 0u ||
                (batch->state.state_flags[owner_221b_i] & (uint8_t)MSL_STATE_FLAG_221B_B7) != 0u);

  batch->state.special_cmd0[idx] = 0u;
  batch->state.falcon_specialhi_vel_x[idx] = 0.0f;
  batch->state.falcon_specialhi_vel_y[idx] = 0.0f;
  fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW, 0.0f);

  // Locate the held victim (decomp: fp->victim_gobj is a single pointer; the linkage lanes
  // carry it). A whiffed-catch seed row without a victim still enters the throw animation.
  int victim_p = -1;
  for (int p = 0; p < num_players; p++) {
    if (p == owner_p) {
      continue;
    }
    const size_t cand = msl_idx_player(bi, p);
    if (batch->state.stocks[cand] == 0u || batch->state.grab_owner_port[cand] != (uint8_t)owner_p ||
        batch->state.action_id[cand] != (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
      continue;
    }
    victim_p = p;
    break;
  }
  if (victim_p < 0) {
    return;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);

  if (!b7) {
    // Airborne-victim hold: the victim hangs from the attacker anchor; apply the same-frame
    // release placement through the shared thrown-anchor owner before detaching.
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
  } else {
    // Grounded-victim hold: the ATTACKER is the constrained fighter. The accessory4 snap
    // (ftCa_SpecialLw_800E550C) keeps Falcon constrained to the victim during the hold, then sets
    // Falcon airborne per ftCommon_8007D5D4 on release.
    // ftCo_800DDDE4 applies the complete D5D4 bundle to the constrained fighter before
    // releasing its XRotN constraint and running the release-local mpColl floor probe.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
    grab_attachment_apply_falcon_dive_ground_release_anchor_now(batch, bi, owner_p, victim_p);
  }
  batch->state.falcon_specialhi_x221b_b7[idx] = 0u;
  {
    const size_t oflags_i =
        idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    const size_t vflags_i =
        vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    batch->state.state_flags[oflags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
    batch->state.state_flags[vflags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
  }

  // Detach immediately. Airborne-victim holds use the ordinary throw-release Fall bridge before
  // damage. Grounded-victim holds keep the victim's pre-release grounded CaptureCaptain branch
  // until the damage-state selector runs; x221B_b7 made Falcon, not the victim, the constrained
  // fighter in ftCa_SpecialLw_800E5128.
  batch->state.attached_victim_port[idx] = 0xFFu;
  batch->state.grab_owner_port[vidx] = 0xFFu;
  if (!b7) {
    batch->state.on_ground[vidx] = 0u;
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_FALL;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_FALL;
    msl_anim_timebase_restart(batch, vidx, 0.0f, 1.0f);
  }

  MslThrowHitboxParams tp = {0};
  if (!move_tables_throw_hitbox_params(batch->state.char_id[idx],
                                       (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW, 0u, &tp)) {
    return;
  }
  const uint8_t applied =
      combat_apply_throw_hit_falcon_dive_release(batch, bi, owner_p, victim_p, &tp, b7);
  if (!applied) {
    if (b7) {
      batch->state.on_ground[vidx] = 0u;
      batch->state.action_id[vidx] = (uint16_t)MSL_ACT_FALL;
      batch->state.animation_index[vidx] = (uint32_t)MSL_SM_FALL;
      msl_anim_timebase_restart(batch, vidx, 0.0f, 1.0f);
    }
    return;
  }
  if (fc_action_is_damage_family(batch->state.action_id[vidx])) {
    // Release damage applies during the owner's callback after the global timer/anim pass, so
    // commit the throw-local percent temp here (Fighter_ProcessHit would consume it later in
    // the same native frame) and, when the owner's callback order precedes the victim's,
    // replay the victim's same-frame Damage callback work (anim tick + hitstun decrement).
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    const float temp = batch->state.percent_temp[vidx];
    if (temp != 0.0f) {
      float percent = batch->state.percent[vidx] + temp;
      if (percent > 999.0f) {
        percent = 999.0f;
      }
      batch->state.percent[vidx] = percent;
      batch->state.percent_temp[vidx] = 0.0f;
    }
    if (owner_p < victim_p) {
      msl_anim_timebase_defer_tick_once(batch, vidx);
      if (batch->state.hitlag[vidx] == 0u) {
        const uint16_t hs = batch->state.hitstun[vidx];
        if (hs > 0u) {
          batch->state.hitstun[vidx] = (uint16_t)(hs - 1u);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Per-action update (anim/IASA/transitions); runs in the action phase
// ---------------------------------------------------------------------------

// ftCaptain_SpecialN_GetAngleVel: |stick.y| clamped to [range_y_neg, range_y_pos], rebased to
// zero at range_y_neg, sign restored from stick.y, scaled to angle_diff degrees across the
// clamp span, in radians. stickGetDir(y, 0) == |y|.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCaptain_SpecialN_GetAngleVel
// refs/melee/src/melee/ft/inlines.h::stickGetDir
static float fc_specialn_angle_rad(const MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float raw_y = fc_stick_unit(batch->state.input_main_y[idx]);
  const float max = ch->falcon_specialn_stick_range_y_pos;
  const float min = ch->falcon_specialn_stick_range_y_neg;
  if (!(max > min)) {
    return 0.0f;
  }
  float sy = fabsf(raw_y);
  if (sy > max) {
    sy = max;
  }
  sy -= min;
  if (sy < 0.0f) {
    sy = 0.0f;
  }
  if (raw_y < 0.0f) {
    sy = -sy;
  }
  return (3.14159265359f / 180.0f) * (sy * ch->falcon_specialn_angle_diff / (max - min));
}

static void fc_update_player(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                             size_t idx) {
  const uint16_t a = batch->state.action_id[idx];
  const uint8_t cid = batch->state.char_id[idx];
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const uint16_t msid = falcon_special_submotion(a);

  switch (a) {
    // ---- Falcon Punch -----------------------------------------------------
    case MSL_ACT_CA_SPECIAL_N:
      // ftCa_SpecialN_IASA is empty; the script's allow_interrupt event at frame 65 has no
      // consumer for this action (the grounded punch is not interruptible). Exit at anim end
      // through ft_8008A2BC.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::{
      //   ftCa_SpecialN_IASA,ftCa_SpecialN_Anim}
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_N: {
      // ftCa_SpecialAirN_IASA: consume the script's cmd_vars[0] pulse (frame 50) once and
      // apply the one-shot punch velocity impulse. special_cmd0 is the consumed-once latch
      // (source sets fp->cmd_vars[0]=0 on consume); it survives ground<->air swaps because
      // the swap transition flags carry cmd state (Ft_MF_UpdateCmd).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_IASA
      // data/moves/falcon.json::specials_by_msid.302 set_cmd_var(idx=0)@50
      const uint8_t cmd0 = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
      if (cmd0 != 0u && batch->state.special_cmd0[idx] == 0u) {
        batch->state.special_cmd0[idx] = 1u;
        const float ang = fc_specialn_angle_rad(batch, ch, idx);
        batch->state.speed_y_self[idx] = ch->falcon_specialn_vel_x * sinf(ang);
        batch->state.speed_air_x_self[idx] =
            ch->falcon_specialn_vel_x * (fc_facing_dir(batch, idx) * cosf(ang));
      }
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    }
    // ---- Falcon Kick ------------------------------------------------------
    case MSL_ACT_CA_SPECIAL_LW:
      // ftCa_SpecialLw_Anim: travel anim end -> grounded SpecialLwEnd at the ground-lag anim
      // rate, or airborne SpecialLwEndAir; both reset cmd vars + throw flags.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
      //   ftCa_SpecialLw_Anim,ftCa_SpecialLw_Anim_inline}
      if (fc_anim_finished(cid, msid, frame)) {
        fc_reset_cmds(batch, idx);
        if (batch->state.on_ground[idx]) {
          // The grounded branch calls ftCommon_8007D7FC before changing state. Its
          // ftCommon_8007D6A4 tail is an absolute grounding write: refresh all jumps, clear
          // fastfall, and unlock the ECB even when a replay reseed carries stale source lanes.
          // The root-motion velocity handoff remains below at the ChangeMotionState boundary.
          // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Anim_inline
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4,
          //   ftCommon_UnlockECB}
          batch->state.jumps_left[idx] = ch->max_jumps;
          batch->state.fall_fast[idx] = 0u;
          msl_ftcommon_unlock_ecb(batch, idx);
          // Fighter_ChangeMotionState snapshots the outgoing SpecialLw root-motion flag. Since
          // SpecialLwEnd is non-root-motion, source clamps gr_vel to dash_run_terminal_velocity
          // before its ft_80084F3C Phys callback applies the normal high-speed friction step.
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
          //   ftCa_SpecialLw_Anim_inline,ftCa_SpecialLwEnd_Phys}
          dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
          fc_enter_rate(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_LW_END, 0.0f,
                        ch->falcon_speciallw_ground_lag_mul > 0.0f
                            ? ch->falcon_speciallw_ground_lag_mul
                            : 1.0f);
        } else {
          // The airborne branch calls ftCommon_8007D5D4 before changing state. Besides the
          // ground/air + ECB bundle, that helper writes jumpsUsed=1, restoring Falcon's one
          // aerial jump when a ground-started kick finishes after leaving the floor.
          // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Anim_inline
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
          combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
          fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR, 0.0f);
        }
      }
      break;
    case MSL_ACT_CA_SPECIAL_LW_END:
    case MSL_ACT_CA_SPECIAL_LW_END_AIR:
      // ftCa_SpecialLwEnd_Anim / ftCa_SpecialLwEndAir_Anim: anim end -> ftCommon_8007D92C
      // (grounded: ft_8008A2BC Wait tail; airborne: ftCo_Fall_Enter).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_LW:
      // ftCa_SpecialAirLw_Anim: anim end -> clear cmds + ftCommon_8007D5D4 + SpecialAirLwEndAir.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLw_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_reset_cmds(batch, idx);
        // Source deliberately calls the common ground-to-air helper even though this action is
        // already airborne. Its jumpsUsed=1 write is Falcon Kick's double-jump refresh; the ECB
        // lock is refreshed at the same callback boundary before the end-air state is entered.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
        fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END_AIR, 0.0f);
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_LW_END:
      // ftCa_SpecialAirLwEnd_Anim: landing-skid anim end -> ft_8008A2BC Wait tail.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLwEnd_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_LW_END_AIR:
    case MSL_ACT_CA_SPECIAL_HI_THROW1:
      // ftCa_SpecialAirLwEndAir_Anim / ftCa_SpecialHiThrow1_Anim: anim end -> ftCo_Fall_Enter
      // (the Wait branch below only fires on a transient grounded row whose Coll transition is
      // already owed).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    // ---- Raptor Boost -----------------------------------------------------
    case MSL_ACT_CA_SPECIAL_S_START:
      // ftCa_SpecialSStart_Anim: grounded miss runs to anim end -> ft_8008A2BC Wait tail.
      // The hit branch is the OnDetect transition (combat inert-contact pass).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_S:
      // ftCa_SpecialS_Anim: grounded hit-punch anim end -> ft_8008A2BC Wait tail.
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_S_START:
      // ftCa_SpecialAirSStart_Anim: air miss anim end -> ftCommon_8007D60C +
      // (miss_landing_lag == 0 ? Fall : ftCo_80096900(1,1,0,1,miss_lag) freefall).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialAirSStart_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_apply_ftcommon_8007d60c(batch, idx);
        if (ch->falcon_specials_miss_landing_lag > 0.0f) {
          msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, /*fallspecial_xc=*/1u,
                                                              ch->falcon_specials_miss_landing_lag,
                                                              /*allow_interrupt=*/0u);
        } else {
          fc_exit_to_wait_or_fall(batch, c, ch, idx);
        }
      }
      break;
    case MSL_ACT_CA_SPECIAL_AIR_S:
      // ftCa_SpecialAirS_Anim: air hit anim end -> same shape with hit_landing_lag.
      if (fc_anim_finished(cid, msid, frame)) {
        fc_apply_ftcommon_8007d60c(batch, idx);
        if (ch->falcon_specials_hit_landing_lag > 0.0f) {
          msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, /*fallspecial_xc=*/1u,
                                                              ch->falcon_specials_hit_landing_lag,
                                                              /*allow_interrupt=*/0u);
        } else {
          fc_exit_to_wait_or_fall(batch, c, ch, idx);
        }
      }
      break;
    // ---- Falcon Dive ------------------------------------------------------
    case MSL_ACT_CA_SPECIAL_HI:
    case MSL_ACT_CA_SPECIAL_AIR_HI: {
      // ftCa_Special(Air)Hi_IASA: consume the script's cmd_vars[0] pulse (frame 13) once —
      // arm x2_b1 (ledge-grab + LandingFallSpecial admission) and B-reverse when the RAW
      // stick magnitude exceeds specialhi_input_var (ftCommon_UpdateFacing; the ftPartSetRotY
      // call is cosmetic).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
      //   ftCa_SpecialHi_IASA,doAirIASA}
      // data/moves/falcon.json::specials_by_msid.{307,308} set_cmd_var(idx=0)@13
      const uint8_t cmd0 = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
      if (cmd0 != 0u && batch->state.special_cmd1[idx] == 0u) {
        batch->state.special_cmd1[idx] = 1u;
        const float raw_sx = fc_stick_unit(batch->state.input_main_x[idx]);
        if (fabsf(raw_sx) > ch->falcon_specialhi_input_var) {
          batch->state.facing[idx] = (uint8_t)(raw_sx > 0.0f);
        }
      }
      if (fc_anim_finished(cid, msid, frame)) {
        // Whiffed dive anim end -> ftCo_80096900(1,1,0,specialhi_freefall_air_spd_mul,
        // specialhi_landing_lag): freefall (xC=1, so FallSpecial_Phys takes the uncapped
        // drift branch and the mobility argument is a dead store).
        //
        // The source fighter is already airborne. ftCo_80096900 changes motion state and consumes
        // jumps, but has no horizontal velocity writer on this branch, so the composite SpecialHi
        // self_vel.x carries into FallSpecial unchanged.
        // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
        //   ftCa_SpecialHi_Anim,ftCa_SpecialAirHi_Anim}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
        msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, /*fallspecial_xc=*/1u,
                                                            ch->falcon_specialhi_landing_lag,
                                                            /*allow_interrupt=*/0u);
      }
      break;
    }
    case MSL_ACT_CA_SPECIAL_HI_CATCH:
      // ftCa_SpecialHiCatch_Anim: IASA/Phys are empty; anim end runs doCatchAnim (throw entry
      // + release). The grab-connect entry is grab_flow_on_catch_connect's falcon branch.
      if (fc_anim_finished(cid, msid, frame)) {
        fc_specialhi_do_catch_anim_release(batch, idx);
      }
      break;
    case MSL_ACT_CA_SPECIAL_HI_THROW: {
      // ftCa_SpecialHiThrow0_Anim: ftCommon_8007D60C keeps the thrower airborne every frame
      // (gr_vel cleared, all jumps burned); the cmd_vars[0] pulse (frame 45) arms x2_b0 (the
      // SpecialHi_Phys + catch-grav blend phase); anim end -> ftCo_Fall_Enter (the dive
      // RENEWS after a connect — not freefall).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Anim
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
      // data/moves/falcon.json::specials_by_msid.310 set_cmd_var(idx=0)@45
      fc_apply_ftcommon_8007d60c(batch, idx);
      const uint8_t cmd0 = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
      if (cmd0 != 0u && batch->state.special_cmd0[idx] == 0u) {
        batch->state.special_cmd0[idx] = 1u;
      }
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Physics (decomp: ftCa_Special*_Phys)
// ---------------------------------------------------------------------------

// ft_80084F3C: grounded friction with the common high-speed multiplier when |gr_vel| exceeds
// walk_max. refs/melee/src/melee/ft/ft_084E.c::ft_80084F3C
static void fc_ground_friction_f3c(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const MslCommonParams* c = msl_common_params();
  float friction = ch->gr_friction;
  const float v = batch->state.speed_ground_x_self[idx];
  if (fabsf(v) > ch->walk_max_vel && c != NULL) {
    friction *= c->high_speed_friction_mul;
  }
  float nv = v;
  if (nv > 0.0f) {
    nv = (nv > friction) ? nv - friction : 0.0f;
  } else if (nv < 0.0f) {
    nv = (nv < -friction) ? nv + friction : 0.0f;
  }
  batch->state.speed_ground_x_self[idx] = nv;
}

// ft_80084FA8 -> ft_80085030: when the animation owns root motion, gr_vel is driven to the
// anim's per-frame TransN z-delta (facing-aligned); otherwise the F3C friction applies.
// refs/melee/src/melee/ft/ft_084E.c::{ft_80084FA8,ft_80085030}
static void fc_ground_anim_vel_fa8(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                   uint16_t msid, float frame) {
  if (msl_anim_uses_root_motion(batch->state.char_id[idx], msid)) {
    float t_cur[3];
    float t_prev[3];
    const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
    const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
    if (anim_pose_get_transn(batch->state.char_id[idx], msid, f_cur, t_cur) == 0 &&
        anim_pose_get_transn(batch->state.char_id[idx], msid, f_prev, t_prev) == 0) {
      const float facing = fc_facing_dir(batch, idx);
      const float dz = (t_cur[2] - t_prev[2]) * ch->model_scaling;
      batch->state.speed_ground_x_self[idx] = dz * facing;
      return;
    }
  }
  fc_ground_friction_f3c(batch, ch, idx);
}

static void fc_fall_step(MslBatch* batch, size_t idx, float grav, float terminal) {
  float vy = batch->state.speed_y_self[idx];
  vy -= grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
}

static void fc_air_friction_step(MslBatch* batch, size_t idx, float friction) {
  float vx = batch->state.speed_air_x_self[idx];
  if (vx > 0.0f) {
    vx -= friction;
    if (vx < 0.0f) {
      vx = 0.0f;
    }
  } else if (vx < 0.0f) {
    vx += friction;
    if (vx > 0.0f) {
      vx = 0.0f;
    }
  }
  batch->state.speed_air_x_self[idx] = vx;
}

// ft_80085134: self_vel = per-frame TransN offset delta (z*facing, y) — the airborne
// anim-root-motion owner (Falcon Kick travel/backflip trajectories are animation-owned).
// refs/melee/src/melee/ft/ft_084E.c::ft_80085134
static void fc_air_anim_vel_85134(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                  uint16_t msid, float frame) {
  float dx = 0.0f;
  float dy = 0.0f;
  if (!fc_transn_delta_85134_f32(batch, ch, idx, msid, frame, &dx, &dy)) {
    return;
  }
  batch->state.speed_air_x_self[idx] = dx;
  batch->state.speed_y_self[idx] = dy;
}

// ftCa_SpecialHi_Phys: (1) self_vel starts from the carried mv.ca.specialhi.vel; (2) the drift
// accel deposits into x74_anim_vel.x — over the specialhi_horz_vel*air_drift_max cap the fixed
// x1FC decel applies (ftCommon_8007D050), otherwise ftCommon_8007D3A8 -> ftCommon_8007D2E8
// computes the stick accel toward stick*cap (neutral stick kills the carried vel outright);
// (3) mv.vel accumulates accel + carried vel; (4) ft_80085134 rebases self_vel from the TransN
// delta and mv.vel is added on top. x74_anim_vel.y is zeroed by this callback every frame and
// has no other writer on this path, so it contributes 0. The same callback runs on grounded
// wind-up frames (0..13, before the script's set_airborne_state) — source integrates grounded
// positions from this self_vel write too, which physics.c's falcon grounded branch mirrors via
// grounded_self_vel_for_frame.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHi_Phys
// refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D050,ftCommon_8007D3A8,ftCommon_8007D2E8}
// refs/melee/src/melee/ft/ft_084E.c::ft_80085134
static void fc_specialhi_phys_core(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                   uint16_t msid, float frame) {
  const MslCommonParams* c = msl_common_params();
  const float cap = ch->falcon_specialhi_horz_vel * ch->air_drift_max;
  const float vel_x = batch->state.falcon_specialhi_vel_x[idx];
  const float vel_y = batch->state.falcon_specialhi_vel_y[idx];
  float accel;
  if (fabsf(vel_x) > cap) {
    // ftCommon_8007D050 over-cap branch: fixed p_ftCommonData->x1FC decel (clamped to -vel).
    accel = (c != NULL) ? c->air_drift_overmax_friction : 0.0f;
    if (accel >= fabsf(vel_x)) {
      accel = -vel_x;
    } else if (vel_x > 0.0f) {
      accel = -accel;
    }
  } else {
    // ftCommon_8007D3A8(p_ftCommonData->x258, air_drift_stick_mul * specialhi_air_friction_mul,
    // air_drift_max * specialhi_horz_vel). The x258 stick gate is modeled with the common
    // deadzone lane (same substitution the Marth port's drift step uses); the accel/target use
    // the RAW stick value like source.
    const float raw_sx = fc_stick_unit(batch->state.input_main_x[idx]);
    const float gate = (c != NULL) ? c->lstick_deadzone_x : 0.2625f;
    float target;
    if (fabsf(raw_sx) >= gate) {
      accel = raw_sx * (ch->air_drift_stick_mul * ch->falcon_specialhi_air_friction_mul);
      target = raw_sx * cap;
    } else {
      accel = 0.0f;
      target = 0.0f;
    }
    // ftCommon_8007D2E8: neutral target snaps the carried vel to zero; same-sign accel is
    // clamped so vel does not overshoot the target.
    if (target == 0.0f) {
      accel = -vel_x;
    } else if (!(vel_x * accel < 0.0f)) {
      if (accel > 0.0f) {
        if (vel_x + accel > target) {
          accel = target - vel_x;
        }
      } else {
        if (vel_x + accel < target) {
          accel = target - vel_x;
        }
      }
    }
  }
  const float new_vel_x = vel_x + accel;
  batch->state.falcon_specialhi_vel_x[idx] = new_vel_x;

  float anim_dx = 0.0f;
  float anim_dy = 0.0f;
  (void)fc_transn_delta_85134_f32(batch, ch, idx, msid, frame, &anim_dx, &anim_dy);
  batch->state.speed_air_x_self[idx] = anim_dx + new_vel_x;
  batch->state.speed_y_self[idx] = anim_dy + vel_y;
}

uint8_t falcon_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (!falcon_action_is_special(a)) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
  const uint16_t msid = falcon_special_submotion(a);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);

  switch (a) {
    case MSL_ACT_CA_SPECIAL_N:
      if (!on_ground) {
        return 0u;  // transient pre-swap frame: generic air handling
      }
      // ftCa_SpecialN_Phys: doPhys (gfx pulses only) + ft_80084FA8 (anim root motion owns
      // the wind-up/step forward; friction otherwise).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialN_Phys
      fc_ground_anim_vel_fa8(batch, ch, idx, msid, frame);
      return 1u;
    case MSL_ACT_CA_SPECIAL_AIR_N: {
      if (on_ground) {
        return 0u;  // transient pre-swap frame: generic grounded handling
      }
      // ftCa_SpecialAirN_Phys switches on the script-owned cmd_vars[1]:
      //   0 -> ft_80084EEC (ordinary gravity + air friction; no drift)
      //   1 -> self_vel *= specialn_vel_mul per frame (post-impulse decay, frames 50..64)
      //   2 -> ft_80084DB0 (common fall + fastfall + drift; handled by the generic air path,
      //        admitted via the falcon branch in msl_action_allows_fastfall)
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Phys
      // refs/melee/src/melee/ft/ft_084E.c::ft_80084EEC
      // data/moves/falcon.json::specials_by_msid.302 set_cmd_var(idx=1)@{50,65}
      const uint8_t cmd1 =
          move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 1u, frame);
      switch (cmd1) {
        case 0u:
          fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
          fc_air_friction_step(batch, idx, ch->aerial_friction);
          return 1u;
        case 1u:
          batch->state.speed_y_self[idx] *= ch->falcon_specialn_vel_mul;
          batch->state.speed_air_x_self[idx] *= ch->falcon_specialn_vel_mul;
          return 1u;
        default:
          // cmd1 == 2: hand ownership to the generic ft_80084DB0-equivalent air path.
          return 0u;
      }
    }
    // ---- Raptor Boost -------------------------------------------------------------------
    case MSL_ACT_CA_SPECIAL_S_START:
    case MSL_ACT_CA_SPECIAL_S:
      if (!on_ground) {
        return 0u;  // transient pre-transition frame: generic air handling
      }
      // ftCa_SpecialSStart_Phys / ftCa_SpecialS_Phys: ft_80084FA8 (anim root motion owns the
      // grounded slide; friction fallback otherwise).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c
      fc_ground_anim_vel_fa8(batch, ch, idx, msid, frame);
      return 1u;
    case MSL_ACT_CA_SPECIAL_AIR_S_START: {
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirSStart_Phys: ft_80085134 (TransN-owned trajectory), then while the
      // script's cmd_vars[1] window is live the private grav accumulator overwrites vy.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialAirSStart_Phys
      // data/moves/falcon.json::specials_by_msid.305 set_cmd_var(idx=1)@30
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      const uint8_t cmd1 =
          move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 1u, frame);
      if (cmd1 == 1u) {
        float g = batch->state.falcon_specials_grav[idx] - ch->falcon_specials_grav;
        if (g < -ch->falcon_specials_terminal_vel) {
          g = -ch->falcon_specials_terminal_vel;
        }
        batch->state.falcon_specials_grav[idx] = g;
        batch->state.speed_y_self[idx] = g;
      }
      return 1u;
    }
    case MSL_ACT_CA_SPECIAL_AIR_S: {
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirS_Phys: ft_80085134 + unconditional grav accumulator on vy.
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      float g = batch->state.falcon_specials_grav[idx] - ch->falcon_specials_grav;
      if (g < -ch->falcon_specials_terminal_vel) {
        g = -ch->falcon_specials_terminal_vel;
      }
      batch->state.falcon_specials_grav[idx] = g;
      batch->state.speed_y_self[idx] = g;
      return 1u;
    }
    // ---- Falcon Kick air phases (grounded kick phys is the physics.c grounded chain) ------
    case MSL_ACT_CA_SPECIAL_LW: {
      if (on_ground) {
        return 0u;  // grounded travel: physics.c grounded chain (85088 + friction scale)
      }
      // ftCa_SpecialLw_Phys air phase: ftPartSetRotZ (cosmetic) + ft_80085134, then
      // Inline_Friction scales the frame's self_vel by the on-hit friction lane (85134
      // rewrites self_vel from TransN each frame, so a per-frame scale does not compound).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Phys
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      const float f = fc_speciallw_friction(batch, idx);
      batch->state.speed_air_x_self[idx] *= f;
      batch->state.speed_y_self[idx] *= f;
      return 1u;
    }
    case MSL_ACT_CA_SPECIAL_LW_END: {
      if (on_ground) {
        return 0u;  // grounded skid: physics.c grounded chain (cmd2 traction / F3C + scale)
      }
      // ftCa_SpecialLwEnd_Phys air phase: ft_80084EEC then Inline_Friction. self_vel persists
      // across frames here, so the per-frame friction scale compounds exactly as source.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEnd_Phys
      fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      fc_air_friction_step(batch, idx, ch->aerial_friction);
      const float f = fc_speciallw_friction(batch, idx);
      batch->state.speed_air_x_self[idx] *= f;
      batch->state.speed_y_self[idx] *= f;
      return 1u;
    }
    case MSL_ACT_CA_SPECIAL_AIR_LW:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirLw_Phys: ft_80085134 only (the dive trajectory is animation-owned; no
      // gravity, no friction lane).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLw_Phys
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      return 1u;
    case MSL_ACT_CA_SPECIAL_AIR_LW_END_AIR:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirLwEndAir_Phys: ft_80084EEC.
      fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      fc_air_friction_step(batch, idx, ch->aerial_friction);
      return 1u;
    case MSL_ACT_CA_SPECIAL_LW_END_AIR: {
      if (on_ground) {
        return 0u;  // grounded: physics.c grounded chain (85088)
      }
      // ftCa_SpecialLwEndAir_Phys air branch: cmd0 ? ft_80084EEC : ft_80085134.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEndAir_Phys
      // data/moves/falcon.json::specials_by_msid.315 set_cmd_var(idx=0)@7
      const uint8_t cmd0 =
          move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 0u, frame);
      if (cmd0 != 0u) {
        fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
        fc_air_friction_step(batch, idx, ch->aerial_friction);
      } else {
        fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      }
      return 1u;
    }
    case MSL_ACT_CA_SPECIAL_HI_THROW1:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialHiThrow1_Phys: ft_80085134 (backflip trajectory is animation-owned).
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      return 1u;
    // ---- Falcon Dive ------------------------------------------------------
    case MSL_ACT_CA_SPECIAL_HI:
    case MSL_ACT_CA_SPECIAL_AIR_HI:
      // ftCa_Special(Air)Hi_Phys runs the same mv-vel/drift/TransN sequence grounded and
      // airborne (grounded wind-up integration uses the resulting self_vel via physics.c's
      // grounded_self_vel_for_frame falcon branch; no ground friction applies).
      fc_specialhi_phys_core(batch, ch, idx, msid, frame);
      return 1u;
    case MSL_ACT_CA_SPECIAL_HI_CATCH:
      // ftCa_SpecialHiCatch_Phys is empty: the catch-time self velocity persists undecayed
      // (no gravity) until doCatchAnim; the grounded-victim variant additionally snaps the
      // attacker to the victim each frame via accessory4 (grab_attachment pre-collision).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiCatch_Phys
      return 1u;
    case MSL_ACT_CA_SPECIAL_HI_THROW: {
      // ftCa_SpecialHiThrow0_Phys: before the cmd0@45 pulse (x2_b0) the trajectory is
      // animation-owned (ft_80085134); after it, SpecialHi_Phys runs and the catch-grav fall
      // step is blended into mv.vel.y (the gravity delta persists across frames).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Phys
      if (batch->state.special_cmd0[idx] != 0u) {
        fc_specialhi_phys_core(batch, ch, idx, msid, frame);
        const float anim_dy =
            batch->state.speed_y_self[idx] - batch->state.falcon_specialhi_vel_y[idx];
        fc_fall_step(batch, idx, ch->falcon_specialhi_catch_grav, ch->terminal_vel);
        batch->state.falcon_specialhi_vel_y[idx] = batch->state.speed_y_self[idx] - anim_dy;
      } else {
        fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      }
      return 1u;
    }
    default:
      return 0u;
  }
}

// ---------------------------------------------------------------------------
// Raptor Boost detect (fp->unk_gobj + hurtbox_detect_cb)
// ---------------------------------------------------------------------------

// ftCa_SpecialS_OnDetect (via fp->hurtbox_detect_cb): fired by Fighter_ProcessHit when
// fp->unk_gobj is set (inert-element hitbox touched a fighter BODY or shield) and the fighter
// neither dealt nor took damage this frame. While the script's cmd_vars[0] window is live and
// the touched gobj is a fighter, Start transitions into the hit punch:
// - grounded: ftCommon_8007D7FC + SpecialS at frame 0; vel.y/z = 0; gr_vel *= specials_gr_vel_x.
// - aerial: SpecialAirS at frame 0; vel.z = 0 (vx/vy kept; the grav accumulator continues).
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
//   ftCa_SpecialS_OnDetect,onDetectGround,onDetectAir}
static void fc_specials_on_detect(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const uint16_t a = batch->state.action_id[idx];
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_S_START) {
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.speed_ground_x_self[idx] *= ch->falcon_specials_gr_vel_x;
    fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_S, 0.0f);
  } else if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START) {
    fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S, 0.0f);
  }
}

void falcon_specials_on_inert_shield_contact(MslBatch* batch, size_t a_idx) {
  // Inert-element shield overlap also writes the attacker's fp->unk_gobj (alongside the
  // defender's x221C_b5 flag), so Raptor Boost connects on shielding opponents.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 (HitElement_Inert shield branch)
  if (batch == NULL || batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return;
  }
  batch->state.falcon_detect_pending[a_idx] = 1u;
}

void falcon_specials_on_inert_body_contact(MslBatch* batch, size_t a_idx) {
  // The caller has already passed ftColl_80078C70's fighter-pair, x42_b5, ground/air,
  // intangibility, shield-priority, and lbColl_8000805C hurtcapsule gates. Inert contact writes only
  // the attacker's unk_gobj; it does not register a victims_1 entry or apply damage.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 (HitElement_Inert BODY branch)
  if (batch == NULL || batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return;
  }
  batch->state.falcon_detect_pending[a_idx] = 1u;
}

enum {
  FC_PROCESSHIT_PENDING_X1914 = 1u << 0,
  FC_PROCESSHIT_HIGHER_PRIORITY = 1u << 1,
};

void falcon_specials_processhit_note_dealt_x1914(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return;
  }
  batch->state.falcon_speciallw_dealt_x1914_frame[idx] |= (uint8_t)FC_PROCESSHIT_PENDING_X1914;
}

void falcon_specials_processhit_note_higher_priority(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return;
  }
  batch->state.falcon_speciallw_dealt_x1914_frame[idx] |= (uint8_t)FC_PROCESSHIT_HIGHER_PRIORITY;
}

static void fc_speciallw_apply_deal_dmg_cb(MslBatch* batch, size_t idx) {
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_CA_SPECIAL_LW) {
    return;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return;
  }
  if ((int32_t)batch->state.falcon_speciallw_hits[idx] <= ch->falcon_speciallw_unk2) {
    batch->state.falcon_speciallw_hits[idx] =
        (uint8_t)(batch->state.falcon_speciallw_hits[idx] + 1u);
    batch->state.falcon_speciallw_friction[idx] =
        fc_speciallw_friction(batch, idx) * ch->falcon_speciallw_on_hit_spd_modifier;
  }
}

void falcon_specials_processhit_consume(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
        continue;
      }
      const uint8_t processhit = batch->state.falcon_speciallw_dealt_x1914_frame[idx];
      const uint8_t detected = batch->state.falcon_detect_pending[idx];
      batch->state.falcon_speciallw_dealt_x1914_frame[idx] = 0u;
      batch->state.falcon_detect_pending[idx] = 0u;

      // Fighter_ProcessHit's branch ladder is strict: phantom, own-shield x19A4, and incoming damage
      // all suppress x1914; x1914 in turn suppresses the final unk_gobj detection callback. Producers
      // only OR bits, making the outcome independent of collision iteration order.
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      if ((processhit & (uint8_t)FC_PROCESSHIT_HIGHER_PRIORITY) != 0u) {
        continue;
      }
      if ((processhit & (uint8_t)FC_PROCESSHIT_PENDING_X1914) != 0u) {
        fc_speciallw_apply_deal_dmg_cb(batch, idx);
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      if (!detected || batch->state.stocks[idx] == 0u ||
          (a != (uint16_t)MSL_ACT_CA_SPECIAL_S_START &&
           a != (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START)) {
        continue;
      }
      const uint16_t msid = falcon_special_submotion(a);
      const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      if (move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 0u,
                                                        frame) != 0u) {
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        if (ch != NULL) {
          fc_specials_on_detect(batch, ch, idx);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Falcon Kick on-hit slowdown + wall rebound
// ---------------------------------------------------------------------------

uint8_t falcon_special_try_speciallw_wall_rebound(MslBatch* batch, size_t idx) {
  // Post-collision wall transitions:
  // - ftCa_SpecialLw_Coll rebound: while the kick script's cmd_vars[0] window is live (frame
  //   15+), a wall HUG in the facing direction clears cmd/throw state, goes airborne
  //   (ftCommon_8007D5D4) and enters SpecialHiThrow1 (the backflip; a Falcon Kick state
  //   despite the name). Both phases of the travel action rebound.
  // - ftCa_SpecialSStart_Coll wall stop: while the Raptor Boost start's cmd_vars[0] window is
  //   live, a wall CONTACT (full wall mask) in the facing direction ends the dash through
  //   ft_8008A2BC (Wait + same-proc Wait_IASA tail).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Coll
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Coll
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_CA_SPECIAL_LW && a != (uint16_t)MSL_ACT_CA_SPECIAL_S_START) {
    return 0u;
  }
  const uint16_t msid = falcon_special_submotion(a);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  if (move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 0u, frame) ==
      0u) {
    return 0u;
  }
  const uint32_t env = batch->state.coll_env_flags[idx];
  const uint8_t facing_right = (batch->state.facing[idx] != 0u) ? 1u : 0u;
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_S_START) {
    // Grounded start only (the air start has no wall stop); full wall mask.
    if (batch->state.on_ground[idx] == 0u) {
      return 0u;
    }
    const uint8_t wall_in_front =
        facing_right ? ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u ? 1u : 0u)
                     : ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK) != 0u ? 1u : 0u);
    if (!wall_in_front) {
      return 0u;
    }
    const MslCommonParams* c = msl_common_params();
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    if (c == NULL || ch == NULL) {
      return 0u;
    }
    fc_exit_to_wait_or_fall(batch, c, ch, idx);
    return 1u;
  }
  const uint8_t wall_in_front =
      facing_right ? ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u ? 1u : 0u)
                   : ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u ? 1u : 0u);
  if (!wall_in_front) {
    return 0u;
  }
  fc_reset_cmds(batch, idx);
  // ftCommon_8007D5D4 runs before the wall-rebound state entry, including jumpsUsed=1 and the
  // ten-frame ECB lock. The new state's callbacks first run next frame, so there is no same-frame
  // timebase tick.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Coll
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  const float ground_vel = batch->state.speed_ground_x_self[idx];
  combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
  batch->state.ground_id[idx] = 0xFFFFu;
  batch->state.speed_air_x_self[idx] = ground_vel;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  fc_enter(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW1, 0.0f);
  return 1u;
}

// ---------------------------------------------------------------------------
// Entry dispatch (B-press routing)
// ---------------------------------------------------------------------------

// Grounded/aerial B-special admission per common action, mirroring the ftCo IASA dispatch
// chains. This mask logic is common-action-owned (identical decomp chains to the Marth port);
// see marth_specials.c::ms_b_entry_mask for the per-case decomp anchors.
enum {
  FC_B_SIDE = 1u << 0,
  FC_B_UP = 1u << 1,
  FC_B_NEUTRAL = 1u << 2,
  FC_B_DOWN = 1u << 3,
  FC_B_ALL = 0xFu,
};

static uint8_t fc_b_entry_mask(const MslBatch* batch, size_t idx, uint16_t a, uint8_t on_ground) {
  if (on_ground) {
    switch (a) {
      case MSL_ACT_WAIT:
      case MSL_ACT_WALK_SLOW:
      case MSL_ACT_WALK_MIDDLE:
      case MSL_ACT_WALK_FAST:
      case MSL_ACT_SQUAT:
      case MSL_ACT_RUN:
      case MSL_ACT_RUN_DIRECT:
      case MSL_ACT_OTTOTTO:
      case MSL_ACT_OTTOTTO_WAIT:
        return FC_B_ALL;
      case MSL_ACT_LANDING: {
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        return (ch != NULL && cur >= (float)ch->landing_lag_frames) ? (uint8_t)FC_B_ALL : 0u;
      }
      case MSL_ACT_SQUAT_WAIT:
      case MSL_ACT_SQUAT_RV:
        return (uint8_t)(FC_B_UP | FC_B_DOWN);
      case MSL_ACT_TURN:
        return (uint8_t)(FC_B_SIDE | FC_B_UP | FC_B_DOWN);
      case MSL_ACT_DASH:
        return (uint8_t)FC_B_SIDE;
      case MSL_ACT_KNEE_BEND:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND) ? (uint8_t)FC_B_UP
                                                                                 : 0u;
      case MSL_ACT_GUARD_OFF:
        return (batch->state.guard_special_enable_timer_x1c[idx] != 0u) ? (uint8_t)FC_B_ALL : 0u;
      case MSL_ACT_ATTACK_13:
      case MSL_ACT_ATTACK_DASH:
      case MSL_ACT_ATTACK_S3_HI:
      case MSL_ACT_ATTACK_S3_HI_S:
      case MSL_ACT_ATTACK_S3_S:
      case MSL_ACT_ATTACK_S3_LW_S:
      case MSL_ACT_ATTACK_S3_LW:
      case MSL_ACT_ATTACK_HI3:
      case MSL_ACT_ATTACK_S4_HI:
      case MSL_ACT_ATTACK_S4_HI_S:
      case MSL_ACT_ATTACK_S4_S:
      case MSL_ACT_ATTACK_S4_LW_S:
      case MSL_ACT_ATTACK_S4_LW:
      case MSL_ACT_ATTACK_HI4:
      case MSL_ACT_ATTACK_LW4:
        return move_tables_grounded_attack_allow_interrupt(
                   batch->state.char_id[idx], a,
                   msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]))
                   ? (uint8_t)FC_B_ALL
                   : 0u;
      case MSL_ACT_DAMAGE_HI_1:
      case MSL_ACT_DAMAGE_HI_1 + 1:
      case MSL_ACT_DAMAGE_HI_1 + 2:
      case MSL_ACT_DAMAGE_N_1:
      case MSL_ACT_DAMAGE_N_1 + 1:
      case MSL_ACT_DAMAGE_N_1 + 2:
      case MSL_ACT_DAMAGE_LW_1:
      case MSL_ACT_DAMAGE_LW_1 + 1:
      case MSL_ACT_DAMAGE_LW_1 + 2:
        return FC_B_ALL;
      case MSL_ACT_RUN_BRAKE:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
                batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT)
                   ? (uint8_t)FC_B_ALL
                   : 0u;
      default:
        return 0u;
    }
  }
  switch (a) {
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
    case MSL_ACT_PASS:
    case MSL_ACT_DAMAGE_FALL:
      return FC_B_ALL;
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_1 + 1:
    case MSL_ACT_DAMAGE_AIR_1 + 2:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return FC_B_ALL;
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
      return (batch->state.passivewall_timer[idx] != 0u) ? 0u : (uint8_t)FC_B_ALL;
    default:
      return 0u;
  }
}

// Direction resolution shared by the dispatcher paths. `up_b_no_stick` admits the Up zone
// without a stick/B-edge requirement: grounded Attack100_CheckInput fires on x686 presence,
// and the airborne Damage/DamageFly doIasa path (ftCo_800D69C4) is presence+freshness-owned;
// the ordinary aerial chain (ftCo_SpecialAir_CheckInput) needs a live B edge plus stick-up.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Attack100_CheckInput,
//   ftCo_800D69C4}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
static uint8_t fc_resolve_and_enter(MslBatch* batch, const MslCommonParams* c,
                                    const MslCharParams* ch, size_t idx, uint8_t mask,
                                    uint8_t on_ground, uint8_t b_edge, uint8_t up_b_no_stick) {
  const float sx =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float sy =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float ax = fabsf(sx);
  if (on_ground) {
    // Grounded chain order: SpecialS -> Attack100(up) -> D6824(neutral) -> D68C0(down).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    if ((mask & FC_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
      if ((sx > 0.0f) != (batch->state.facing[idx] != 0u)) {
        batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
      }
      fc_enter_specials(batch, ch, idx, 1u);
      return 1u;
    }
    if ((mask & FC_B_UP) != 0u && up_b_no_stick) {
      fc_enter_specialhi(batch, ch, idx, 1u);
      return 1u;
    }
    if ((mask & FC_B_NEUTRAL) != 0u && b_edge && ax < c->special_stick_x_threshold_side &&
        sy < c->special_stick_y_threshold && sy > -c->special_stick_y_threshold) {
      fc_enter_specialn(batch, ch, idx, 1u);
      return 1u;
    }
    if ((mask & FC_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
      fc_enter_speciallw(batch, ch, idx, 1u);
      return 1u;
    }
    return 0u;
  }
  // Aerial chain order: Up -> Down -> Side -> Neutral.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  if ((mask & FC_B_UP) != 0u && (up_b_no_stick || (b_edge && sy >= c->special_stick_y_threshold))) {
    fc_enter_specialhi(batch, ch, idx, 0u);
    return 1u;
  }
  if ((mask & FC_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
    fc_enter_speciallw(batch, ch, idx, 0u);
    return 1u;
  }
  if ((mask & FC_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
    if ((sx > 0.0f) != (batch->state.facing[idx] != 0u)) {
      batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
    }
    fc_enter_specials(batch, ch, idx, 0u);
    return 1u;
  }
  if ((mask & FC_B_NEUTRAL) != 0u && b_edge && ax < c->special_stick_x_threshold_side &&
      sy < c->special_stick_y_threshold) {
    // ftCo_SpecialAir_CheckInput neutral-B turnaround: a fresh horizontal flick opposite to
    // facing (x676_x < p_ftCommonData->x224 with the x2228_b7 side latch) flips facing before
    // the Enter (aerial chain only — the grounded dispatch has no turnaround clause). Same
    // model as the fox blaster / sheik dispatch sites.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    if ((float)batch->state.x676_x[idx] < c->special_neutral_reverse_threshold) {
      const uint8_t facing = batch->state.facing[idx] ? 1u : 0u;
      const uint8_t x2228_b7 = batch->state.x2228_b7[idx] ? 1u : 0u;
      if ((facing == 0u && x2228_b7 == 1u) || (facing == 1u && x2228_b7 == 0u)) {
        batch->state.facing[idx] = facing ? 0u : 1u;
        batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
      }
    }
    fc_enter_specialn(batch, ch, idx, 0u);
    return 1u;
  }
  return 0u;
}

static uint8_t fc_try_enter_grounded_b_special_from_wait_iasa(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.on_ground[idx] == 0u ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
  const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
  if (!b_edge && !up_b_present) {
    return 0u;
  }
  const uint8_t mask = fc_b_entry_mask(batch, idx, (uint16_t)MSL_ACT_WAIT, 1u);
  if (mask == 0u) {
    return 0u;
  }
  return fc_resolve_and_enter(batch, c, ch, idx, mask, 1u, b_edge, up_b_present);
}

static uint8_t fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch, size_t idx,
                                                               uint16_t source_action) {
  if (batch == NULL || c == NULL || ch == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT || batch->state.on_ground[idx] == 0u) {
    return 0u;
  }

  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float stick_x =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float facing_dir = fc_facing_dir(batch, idx);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  // Same source owner as the Marth/Sheik ports: ft_8008A2BC enters Wait through ft_8008A348 and
  // the destination Wait_IASA runs in the same Fighter_procUpdate pass, ordered
  // specials -> catch -> grounded attacks -> spotdodge-before-guard -> guard -> locomotion.
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  if (fc_try_enter_grounded_b_special_from_wait_iasa(batch, c, ch, idx)) {
    return 1u;
  }
  if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
    return 1u;
  }
  if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                            stick_y, tilt_timer_x, tilt_timer_y,
                                                            facing_dir)) {
    return 1u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
    return 1u;
  }
  guard_update_grounded(batch, c, idx, /*allow_entry=*/1u);
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT) {
    return 1u;
  }
  return locomotion_wait_iasa_locomotion_subset_try_enter(
      batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y,
      facing_dir, source_action);
}

static uint8_t fc_aerial_up_b_uses_presence_gate(uint16_t action_id) {
  switch (action_id) {
    // Airborne Damage/DamageFly doIasa calls ftCo_800D69C4 (x686/x68B presence-owned Up-B).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_1 + 1:
    case MSL_ACT_DAMAGE_AIR_1 + 2:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

static uint8_t fc_try_enter_air_b_special_from_fall_iasa(MslBatch* batch, const MslCommonParams* c,
                                                         const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.on_ground[idx] != 0u ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
  if (!b_edge) {
    return 0u;
  }
  const uint8_t mask = fc_b_entry_mask(batch, idx, (uint16_t)MSL_ACT_FALL, 0u);
  if (mask == 0u) {
    return 0u;
  }
  return fc_resolve_and_enter(batch, c, ch, idx, mask, 0u, b_edge, 0u);
}

void falcon_specials_update_pre_physics(MslBatch* batch) {
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
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
          batch->state.stocks[idx] == 0u) {
        continue;
      }
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // Clear the Falcon-specific ProcessHit packet ledger before item/fighter collision producers.
      // combat_resolve consumes it once after every source packet has been accumulated.
      batch->state.falcon_speciallw_dealt_x1914_frame[idx] = 0u;

      // Falcon Dive orphaned-hold release: when the captor is knocked out of SpecialHiCatch
      // (only reachable via items in 1v1) the held CaptureCaptain victim is cut loose. Source:
      // the holder's damage aftermath releases constrained victims into CaptureCut.
      // refs/melee/src/melee/ft/ft_0C8C.c (getFtVictim -> ftCo_CaptureCut_Enter)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_CaptureCut_Enter
      if (a != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH &&
          batch->state.attached_victim_port[idx] != 0xFFu &&
          (int)batch->state.attached_victim_port[idx] < num_players) {
        const size_t vidx = msl_idx_player(bi, (int)batch->state.attached_victim_port[idx]);
        if (batch->state.grab_owner_port[vidx] == (uint8_t)p &&
            batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
          batch->state.attached_victim_port[idx] = 0xFFu;
          batch->state.grab_owner_port[vidx] = 0xFFu;
          batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_CUT;
          batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_CUT;
          if (batch->state.on_ground[vidx] != 0u) {
            batch->state.speed_ground_x_self[vidx] =
                -(float)batch->state.facing_dir1[vidx] * c->capture_cut_escape_speed;
          } else {
            batch->state.speed_air_x_self[vidx] =
                -(float)batch->state.facing_dir1[vidx] * c->capture_cut_escape_speed;
          }
          msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
        }
      }

      if (falcon_action_is_special(a)) {
        fc_update_player(batch, c, ch, idx);
        continue;
      }

      if (batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
        continue;
      }
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
      const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
      const uint8_t air_up_b_fresh =
          (uint8_t)(up_b_present && batch->state.x68B[idx] >= c->tech_lr_debounce_frames);
      const uint8_t air_up_b_presence_gate =
          (uint8_t)((!on_ground) && fc_aerial_up_b_uses_presence_gate(a));
      if (on_ground) {
        if (!b_edge && !up_b_present) {
          continue;
        }
      } else if (!b_edge && !(air_up_b_presence_gate && up_b_present)) {
        continue;
      }
      const uint8_t mask = fc_b_entry_mask(batch, idx, a, on_ground);
      if (mask == 0u) {
        continue;
      }
      if (on_ground) {
        (void)fc_resolve_and_enter(batch, c, ch, idx, mask, 1u, b_edge, up_b_present);
      } else {
        // Airborne Damage/DamageFly doIasa admits Up-B on x686 presence + x68B freshness
        // (no stick check); every other aerial zone needs a live B edge.
        (void)fc_resolve_and_enter(batch, c, ch, idx, mask, 0u, b_edge,
                                   (uint8_t)(air_up_b_presence_gate && air_up_b_fresh));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Ground <-> air variant swaps (collision callbacks; preserve animation frame)
// ---------------------------------------------------------------------------

// Decomp collision handling per family:
// - Falcon Punch: frame-preserving swap 347 <-> 348 (ftCa_SpecialN_Coll / ftCa_SpecialAirN_Coll).
// - Falcon Kick travel/end (357/358/362): SAME-action ground<->air phase flips
//   (ftCommon_8007D5D4 / ftCommon_8007D7FC inside ftCa_SpecialLw{,End,EndAir}_Coll) — the
//   handlers below return 1 without changing the action so the caller applies the bundle.
// - Air kick + backflip descent (359/361): landing enters SpecialAirLwEnd at frame 0 with the
//   speciallw_landing_lag_mul anim rate (ftCa_SpecialAirLw_Coll / ftCa_SpecialAirLwEndAir_Coll
//   doColl). SpecialHiThrow1 landing uses the generic ftCo_AirCatchHit_Coll basic-Landing path.
static uint16_t falcon_special_air_variant(uint16_t a) {
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_N) {
    return (uint16_t)MSL_ACT_CA_SPECIAL_AIR_N;
  }
  return 0u;
}

static uint16_t falcon_special_ground_variant(uint16_t a) {
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_N) {
    return (uint16_t)MSL_ACT_CA_SPECIAL_N;
  }
  return 0u;
}

static inline uint8_t falcon_speciallw_action_phase_flips(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
                   a == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END ||
                   a == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR);
}

static void fc_swap_preserving_frame(MslBatch* batch, size_t idx, uint16_t next_action) {
  const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  batch->state.action_id[idx] = next_action;
  batch->state.animation_index[idx] = (uint32_t)falcon_special_submotion(next_action);
  msl_anim_timebase_enter(batch, idx, cur, 1.0f);
  // ChangeMotionState without Ft_MF_Unk24 clears fp->x221C_u16_y; opcode-52 levels whose source
  // event is at or before the preserved entry frame stay cleared until the script crosses its
  // next event (state_flags.c consumer). The grounded punch script has set_state_flags events
  // at frames 52/77.
  batch->state.x221c_y_event_floor[idx] = (uint16_t)(msl_anim_frame_floor_u16(cur) + 1u);
}

uint8_t falcon_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (falcon_speciallw_action_phase_flips(a)) {
    // Falcon Kick travel/end landing: ftCommon_8007D7FC phase flip only — same action, same
    // frame; the caller's grounding bundle applies.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll,ftCa_SpecialLwEndAir_Coll}
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW ||
      a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END_AIR) {
    // Air kick / backflip-descent landing: clear cmds + SpecialAirLwEnd from frame 0 at the
    // landing-lag anim rate (doColl).
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialAirLw_Coll,ftCa_SpecialAirLwEndAir_Coll}
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    fc_reset_cmds(batch, idx);
    fc_enter_rate(batch, idx, (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END, 0.0f,
                  (ch != NULL && ch->falcon_speciallw_landing_lag_mul > 0.0f)
                      ? ch->falcon_speciallw_landing_lag_mul
                      : 1.0f);
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_HI || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
      a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH || a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
    // Falcon Dive landings:
    // - Special(Air)Hi doAirColl: with x2_b1 armed -> ftCo_LandingFallSpecial_Enter
    //   (specialhi_landing_lag); before the arm pulse -> ft_80083B68 (stage collision only,
    //   the dive continues grounded: phase flip).
    // - SpecialHiCatch Coll: ft_80083B68 when not victim-snapped (phase flip).
    // - SpecialHiThrow0 Coll: ft_80081D0C ground contact -> LandingFallSpecial.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
    //   doAirColl,ftCa_SpecialHiCatch_Coll,ftCa_SpecialHiThrow0_Coll}
    const uint8_t lands_special = (uint8_t)(a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW ||
                                            ((a == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                                              a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI) &&
                                             batch->state.special_cmd1[idx] != 0u));
    if (!lands_special) {
      return 1u;  // phase flip only; the caller applies the grounding bundle
    }
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    const float lag = (ch != NULL) ? ch->falcon_specialhi_landing_lag : 0.0f;
    // SpecialHi is a TransN-root-motion owner (x594_b0). Although doAirColl passes the current
    // composite self_vel.x to ftCo_LandingFallSpecial_Enter, ftCommon_8007D6A4 replaces it with
    // x6A4_transNOffset.z * facing before publishing gr_vel. MSL stores that composite as the
    // replay-visible lane plus mv.ca.specialhi.vel.x, so recover the TransN slice here.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{doAirColl,ftCa_SpecialHi_Phys}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    const float landing_root_x =
        batch->state.speed_air_x_self[idx] - batch->state.falcon_specialhi_vel_x[idx];
    batch->state.speed_air_x_self[idx] = landing_root_x;
    batch->state.speed_ground_x_self[idx] = landing_root_x;
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
    const float ef =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
    msl_anim_timebase_enter(batch, idx, 0.0f,
                            (lag > 0.0f && ef > 0.0f) ? ((ef + 0.1f) / lag) : 1.0f);
    batch->state.fallspecial_landing_lag[idx] = lag;
    batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S) {
    // Raptor Boost air start/hit landing: ftCo_LandingFallSpecial_Enter with the miss/hit
    // landing lag; the hit variant first syncs gr_vel from self_vel.x (caller bundle does).
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
    //   ftCa_SpecialAirSStart_Coll,ftCa_SpecialAirS_Coll}
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    const float lag = (ch == NULL) ? 0.0f
                      : (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START)
                          ? ch->falcon_specials_miss_landing_lag
                          : ch->falcon_specials_hit_landing_lag;
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
    // ftCo_LandingFallSpecial_Enter -> ftCo_Landing_Enter: fixed submotion at
    // (end + 0.1) / lag so the animation spans the landing lag.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    //   ftCo_LandingFallSpecial_Enter,ftCo_Landing_Enter}
    const float ef =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
    msl_anim_timebase_enter(batch, idx, 0.0f,
                            (lag > 0.0f && ef > 0.0f) ? ((ef + 0.1f) / lag) : 1.0f);
    batch->state.fallspecial_landing_lag[idx] = lag;
    batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
    return 1u;
  }
  const uint16_t next = falcon_special_ground_variant(a);
  if (next == 0u) {
    return 0u;
  }
  // ftCa_SpecialAirN_Coll: ft_80081D0C ground contact -> ftCommon_8007D7FC + grounded variant
  // at the preserved frame. The grounding bundle (gr_vel sync, jumps refresh) is caller-owned.
  // The special_cmd0 impulse-consumed latch intentionally survives (Ft_MF_UpdateCmd carries
  // cmd state).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Coll
  const float preserved_frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  fc_swap_preserving_frame(batch, idx, next);
  if (next == (uint16_t)MSL_ACT_CA_SPECIAL_N) {
    float transn_x = 0.0f;
    float transn_y = 0.0f;
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    if (fc_transn_delta_85134_f32(batch, ch, idx, falcon_special_submotion(next), preserved_frame,
                                  &transn_x, &transn_y)) {
      // The preserved-frame ChangeMotionState selects grounded SpecialN's FigaTree. Its TransN
      // slice is the root-motion value published for the destination state; sampling the outgoing
      // SpecialAirN tree instead incorrectly zeros the landing-frame ground velocity.
      // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Coll
      batch->state.speed_air_x_self[idx] = transn_x;
      batch->state.speed_ground_x_self[idx] = transn_x;
    }
  }
  return 1u;
}

uint8_t falcon_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END) {
    // The grounded landing-skid state has a different collision owner from the other Falcon Kick
    // phases: ftCa_SpecialAirLwEnd_Coll delegates to ft_80084104, which enters Fall through
    // ftCo_Fall_Enter when ft_800827A0 loses the floor. ftCo_Fall_Enter changes motion, clamps air
    // drift, then calls ftCommon_8007D5D4 because the source fighter was grounded. Stage collision
    // has already published on_ground=0 in MSL, so apply that source-ordered absolute helper here.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLwEnd_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084104
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
    combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
    batch->state.ground_id[idx] = 0xFFFFu;
    batch->state.speed_ground_x_self[idx] = 0.0f;
    return 1u;
  }
  if (falcon_speciallw_action_phase_flips(a)) {
    // Falcon Kick travel/end floor loss: ftCommon_8007D5D4 phase flip only — same action,
    // same frame; the caller syncs speed_air from gr_vel.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll,ftCa_SpecialLwEndAir_Coll}
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_HI || a == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
      a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH || a == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
    // Falcon Dive floor loss: grounded SpecialHi_Coll runs ftCommon_8007D5D4 (same action,
    // phase flip only); HiCatch/Throw0 grounded rows are transient and take the same flip.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHi_Coll
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_CA_SPECIAL_S_START || a == (uint16_t)MSL_ACT_CA_SPECIAL_S) {
    // Raptor Boost grounded start/hit floor loss: ftCommon_ClampAirDrift + FallSpecial with
    // the miss/hit landing lag (freefall).
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
    //   ftCa_SpecialSStart_Coll,ftCa_SpecialS_Coll}
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    const float lag = (ch == NULL) ? 0.0f
                      : (a == (uint16_t)MSL_ACT_CA_SPECIAL_S_START)
                          ? ch->falcon_specials_miss_landing_lag
                          : ch->falcon_specials_hit_landing_lag;
    float air_x = batch->state.speed_ground_x_self[idx];
    if (ch != NULL) {
      if (air_x > ch->air_drift_max) {
        air_x = ch->air_drift_max;
      } else if (air_x < -ch->air_drift_max) {
        air_x = -ch->air_drift_max;
      }
    }
    batch->state.speed_air_x_self[idx] = air_x;
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.jumps_left[idx] = 0u;
    msl_ftcommon_lock_ecb_8007d60c(batch, idx);
    if (lag > 0.0f) {
      msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, /*fallspecial_xc=*/1u, lag,
                                                          /*allow_interrupt=*/0u);
    } else {
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    }
    return 1u;
  }
  const uint16_t next = falcon_special_air_variant(a);
  if (next == 0u) {
    return 0u;
  }
  // ftCa_SpecialN_Coll: floor loss (!ft_800827A0) -> ftCommon_8007D5D4 + aerial variant at the
  // preserved frame + ftCommon_ClampAirDrift (caller-owned floor-loss bundle).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialN_Coll
  fc_swap_preserving_frame(batch, idx, next);
  return 1u;
}
