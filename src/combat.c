#include "combat.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "combat_geom.h"
#include "common_params.h"
#include "grab_flow.h"
#include "hit_elements.h"
#include "hitboxes_tables.h"
#include "hit_status_tables.h"
#include "hitlist.h"
#include "laser_params.h"
#include "move_tables.h"
#include "staling.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx,
                                               float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static inline float combat_lbColl_804D7A38(void) {
  // lbColl_8000805C BODY path forwards arg11 = lbColl_804D7A38 * hurt_owner_scale_y.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // refs/melee/src/melee/lb/lbcollision.c (lbColl_804D7A38 = 3)
  return 3.0f;
}

static inline uint8_t combat_body_overlap_lbColl_80006E58_subset_allows(
    const MslBatch* batch, size_t hb_i, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  (void)hb_i;
  // ftColl_800768A0 clear/copy ownership runs on HitCapsule enable/group edges.
  // Enable this lane through edge transitions to exercise lbColl_8000805C/80006E58 continuity
  // using x58/x4C carried by ftColl_8007AD18.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // Pre-hit ownership subset: keep defender-in-hitstun lanes on baseline overlap while enabling
  // decomp-shaped sweep for neutral BODY checks.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  if (batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  // Start with the aerial-victim eligibility branch (x40_b2) from ftColl_80078C70.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  if (batch->state.on_ground[d_idx]) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t combat_body_overlap_lbColl_80006E58_scaffold(
    const MslBatch* batch, int bi, int attacker, int hb_id, float hx, float hy, float hz, float hr,
    float ax, float ay, float az, float bx, float by, float bz, float cr,
    float defender_scale_y) {
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);

  // ftColl_80078C70 forwards HitCapsule.x43_b2 as lbColl_8000805C arg3 (`var_r22`).
  // lbColl_8000805C accepts immediately when arg3 != 0.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  if (batch->state.hitbox_x43_b2[hb_i]) {
    return 1u;
  }

  float px = hx;
  float py = hy;
  float pz = hz;
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    px = batch->state.hitbox_prev_x[hb_i];
    py = batch->state.hitbox_prev_y[hb_i];
    pz = batch->state.hitbox_prev_z[hb_i];
  }

  // lbColl_80006E58 broad envelope:
  //   temp_f3 = (arg10 * arg11) + scl
  // BODY path mapping:
  //   scl=hit radius, arg10=hurt radius, arg11=lbColl_804D7A38*hurt_owner_scale_y.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  float arg11 = 0.0f;
  if (defender_scale_y > 0.0f) {
    arg11 = combat_lbColl_804D7A38() * defender_scale_y;
  }
  const float broad_r = hr + cr * arg11;

  const float hminx = fminf(px, hx);
  const float hmaxx = fmaxf(px, hx);
  const float hminy = fminf(py, hy);
  const float hmaxy = fmaxf(py, hy);
  const float hminz = fminf(pz, hz);
  const float hmaxz = fmaxf(pz, hz);
  const float cminx = fminf(ax, bx);
  const float cmaxx = fmaxf(ax, bx);
  const float cminy = fminf(ay, by);
  const float cmaxy = fmaxf(ay, by);
  const float cminz = fminf(az, bz);
  const float cmaxz = fmaxf(az, bz);
  if (hmaxx + broad_r < cminx || cmaxx + broad_r < hminx || hmaxy + broad_r < cminy ||
      cmaxy + broad_r < hminy || hmaxz + broad_r < cminz || cmaxz + broad_r < hminz) {
    return 0u;
  }

  float d2 = 0.0f;
  combat_segment_segment_dist2(px, py, pz, hx, hy, hz, ax, ay, az, bx, by, bz, &d2, NULL, NULL);
  const float rr = hr + cr;
  return (uint8_t)(d2 <= rr * rr);
}

static inline uint8_t combat_shield_overlap_ftcoll_80007bcc(
    const MslBatch* batch, int bi, int attacker, int hb_id, float hx, float hy, float hz, float hr,
    float shx, float shy, float shz, float shr, float shield_desc_radius,
    float shield_owner_scale_y, uint8_t shield_desc_envelope_ready, float* out_overlap_margin) {
  if (out_overlap_margin != NULL) {
    *out_overlap_margin = 0.0f;
  }
  if (batch == NULL) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  // Decomp geometry owner:
  // - lbColl_80006E58 combines HitCapsule radius (`scl`) with ShieldDesc radius (`arg10`).
  // - Guard path builds ShieldDesc via AbsorbDesc with `x10_size = 1` in ftCo_80092450.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092450
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
  float shield_desc_world_r = shield_desc_radius;
  if (shield_owner_scale_y > 0.0f) {
    shield_desc_world_r *= shield_owner_scale_y;
  }
  // lbColl_80007BCC forwards an extra extent lane (`lbColl_804D7A34 * arg5`) into
  // lbColl_80006E58 (`arg11`) for shield overlap broadphase.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  //
  // This simulator uses a reduced sphere/segment proxy; carry a small equivalent envelope from
  // that extent lane to avoid near-boundary false negatives in the guard shield path.
  // Apply ShieldDesc radius lane when the slot was not recreated at pose_frame, or when the slot
  // is on an enable-edge transition. This matches the ftColl_8007AD18 state ownership split:
  // - state 1/2 edge transitions are fed by ftAction create/copy/clear ownership (ftColl_800768A0),
  // - steady slots with no pose-frame create keep prior collision state.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
  const uint8_t shield_desc_lane_active =
      (shield_desc_envelope_ready &&
       (batch->state.hitbox_enable_edge[hb_i] || !batch->state.hitbox_pose_create[hb_i]))
          ? 1u
          : 0u;
  const float shield_desc_term = shield_desc_lane_active ? shield_desc_world_r : 0.0f;
  const float shield_extent_env_r =
      (shield_desc_envelope_ready && batch->state.hitbox_enable_edge[hb_i])
          ? (shield_desc_world_r * 0.2f)
          : 0.0f;
  const float rr = hr + shr + shield_desc_term + shield_extent_env_r;
  float d2 = 0.0f;

  // Decomp-owned geometry path:
  // - ftColl_8007AD18 carries previous/current hitcapsule centers in x58/x4C.
  // - lbColl_80007BCC consumes that x58->x4C sweep segment for shield overlap tests.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    const float px = batch->state.hitbox_prev_x[hb_i];
    const float py = batch->state.hitbox_prev_y[hb_i];
    const float pz = batch->state.hitbox_prev_z[hb_i];
    combat_point_segment_dist2(shx, shy, shz, px, py, pz, hx, hy, hz, &d2, NULL);
  } else {
    const float dx = hx - shx;
    const float dy = hy - shy;
    const float dz = hz - shz;
    d2 = dx * dx + dy * dy + dz * dz;
  }

  if (out_overlap_margin != NULL) {
    *out_overlap_margin = rr - sqrtf(d2);
  }
  return (uint8_t)(d2 <= rr * rr);
}

static inline float combat_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float combat_trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float combat_trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return combat_trigger_u8_to_unit(m);
}

static inline float combat_lightshield_amount(const MslCommonParams* c, uint16_t buttons, uint8_t l,
                                              uint8_t r) {
  if (c == NULL) {
    return 0.0f;
  }

  // Decomp: fp->lightshield_amount = (x650 - x10)/(1-x10) (clamped) under trigger deadzone.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:333-350.
  const float denom = 1.0f - c->trigger_deadzone;
  if (denom <= 0.0f) {
    return 0.0f;
  }
  const float trig = combat_trigger_unit_from_input(buttons, l, r);
  const float light = (trig - c->trigger_deadzone) / denom;
  return combat_clamp01(light);
}

static inline void combat_state_flags_set_is_hitlag(MslBatch* batch, size_t idx, uint16_t hitlag) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
  //
  // Decomp-first references (GALE01):
  // - `fp->x221A_b2` is toggled with hitlag start/end:
  //   - set when hitlag is applied (Fighter_ProcessHit_8006D1EC),
  //   - cleared when hitlag reaches 0 (Fighter_8006A1BC).
  // refs/melee/src/melee/ft/fighter.c
  // - Bitfield layout at fp+0x221A is documented in refs/melee/src/melee/ft/types.h.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitlag > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline void combat_state_flags_set_x221a_b3(MslBatch* batch, size_t idx) {
  // Decomp: Fighter_ProcessHit sets fp->x221A_b3 = 1 alongside hitlag start under certain
  // knockback/damage paths (see `bool2`), and Fighter_8006A1BC clears it on hitlag end.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  //
  // In GALE01, `bool2` is set to 1 when the hit takes the "forceAppliedOnHit && !no_kb" path
  // (Fighter_ProcessHit_8006D1EC sets `bool2 = 1` shortly before the `if (bool2) fp->x221A_b3 = 1`
  // assignment). In this light sim we do not model the full `forceAppliedOnHit` / `no_kb` plumbing,
  // so callers gate this bit on `hitstun > 0` (derived from knockback), which is a safe proxy for
  // the current suite domain (Fox/Falco) and matches Slippi's observable "KB hit that causes hitstun"
  // cases where this bit is set.
  //
  // Slippi post-frame: this bit lives in the fp+0x221A byte (`state_flags[...,1]`). The isHitlag
  // bit is 0x20 (x221A_b2), so x221A_b3 is the adjacent 0x10 bit under the same packing.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221A_B3;
}

static inline void combat_state_flags_set_is_hitstun(MslBatch* batch, size_t idx,
                                                     uint16_t hitstun) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  // Slippi post-frame: `lbz r3,0x221C(REG_PlayerData)  #0x2 = isHitstun`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //
  // Decomp: `fp->x221C_b6` is set on Damage state entry and cleared when hitstun ends.
  // - set: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (end of function)
  // - clear: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitstun > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline uint8_t combat_is_guard_reflect_frozen_snapshot_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0;
  }
  // Decomp ordering context:
  // - Guard->GuardReflect entry path ftCo_8009388C preserves the current timebase state while
  //   GuardReflect_Anim/ftCo_80093BC0 owns the canonical callback tick.
  // - Under teacher-forced replay snapshots, this boundary appears as frozen no-submotion rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
  //
  // Keep frozen-snapshot detection behavior identical to the existing lane:
  // action_frame <= -2.
  enum { MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX = -2 };
  return (batch->state.action_frame[idx] <= MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX) ? 1u : 0u;
}

uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0;
  }
  // Decomp gate at collision-time is on fp->x221C_b2 directly.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardReflect lane split:
    // - frozen no-submotion snapshot lane keeps x18 (legacy powershield-active lane) so replay-real
    //   frozen rows do not collapse before the first canonical callback-owned transition;
    // - normal GuardReflect lane uses x14 (active reflect callback lane).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    if (combat_is_guard_reflect_frozen_snapshot_idx(batch, idx)) {
      return (batch->state.guard_reflect_timer_x18[idx] != 0u) ? 1u : 0u;
    }
    // For non-frozen GuardReflect frames, gate suppression on the active reflect callback lane.
    return (batch->state.guard_reflect_timer_x14[idx] != 0u) ? 1u : 0u;
  }
  return (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE) ? 1u : 0u;
}

// Combo / last-attack tracking (decomp-first).
//
// Decomp trail (GALE01):
// - Combo update + last-attack id:
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0 (writes fp->x208C, fp->x2090, fp->x2094)
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 (calls ftColl_800763C0(attacker, victim, fp->x2068_attackID))
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C (item->fighter variant; attack id in item domain)
// - Slippi post-frame fields:
//   - last_attack_landed: low byte of lwz 0x208C(fp)
//   - combo_count: low byte of lhz 0x2090(fp)
//   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
static inline void combat_combo_ftColl_800763C0(MslBatch* batch, size_t a_idx, int defender,
                                                size_t d_idx, uint16_t attack_id_u16) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (defender < 0 || defender >= num_players) {
    return;
  }

  const uint8_t attack_id_u8 = (uint8_t)attack_id_u16;  // Slippi stores the low byte.
  const uint8_t cur_victim = batch->state.combo_victim_port[a_idx];
  if (cur_victim == 0xFFu) {
    batch->state.last_attack_landed[a_idx] = attack_id_u8;
    batch->state.combo_count[a_idx] = 1u;
    batch->state.combo_victim_port[a_idx] = (uint8_t)defender;
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
    return;
  }
  // Decomp uses a raw victim GObj pointer for `fp->x2094` equality checks (not an integer ID).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
  //
  // Slippi's `instance_id` (fp+0x2070 union-as-int) is not a stable "fighter object identity"
  // across frames, so do not include it in the x2094-equivalence check. We keep the instance_id
  // snapshot only as a seeded/debuggable overlay.
  // refs/melee/src/melee/ft/types.h (union Struct2070 at fp+0x2070, used as s32 x2070_int)
  if (cur_victim == (uint8_t)defender) {
    if (attack_id_u16 != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
        batch->state.last_attack_landed[a_idx] == attack_id_u8) {
      batch->state.combo_count[a_idx] = (uint8_t)(batch->state.combo_count[a_idx] + 1u);
    } else {
      batch->state.combo_count[a_idx] = 0u;
      batch->state.last_attack_landed[a_idx] = attack_id_u8;
    }
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
  }
}

static inline float combat_hitlag_mul_from_element(const MslCommonParams* c, uint8_t element) {
  if (c == NULL) {
    return 1.0f;
  }
  // Decomp/ASM: ftColl_8007A06C sets `fp->x1960_vibrateMult = p_ftCommonData->x1A4` when hit
  // element is 2 (electric), and Fighter_ProcessHit passes `fp->x1960_vibrateMult` into
  // ftCommon_CalcHitlag as the `mul` argument.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (search for `stfs f0, 0x1960`)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  if (element == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC) {
    return c->hitlag_electric_mul;
  }
  return 1.0f;
}

static inline uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg,
                                                 uint16_t motion_id, float hitlag_mul) {
  if (c == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Notes for this pass:
  // - `hitlag_mul` corresponds to `fp->x1960_vibrateMult` in decomp (see combat_hitlag_mul_from_element).
  const float tmp_f = (float)dmg * c->hitlag_dmg_mul + c->hitlag_base;
  int tmp = (int)tmp_f;

  float mul = hitlag_mul;
  if (!(mul > 0.0f)) {
    mul = 1.0f;
  }

  // Decomp truncates before applying squat scaling:
  // `result = (int)(tmp * mul); if ((unsigned)msid - ftCo_MS_Squat <= 1) result = (int)(result * x1A0);`
  // refs/melee/src/melee/ft/ftcommon.c:653-655 (ftCommon_CalcHitlag)
  int result_i = (int)((float)tmp * mul);
  if (motion_id == (uint16_t)MSL_ACT_SQUAT || motion_id == (uint16_t)MSL_ACT_SQUAT_WAIT) {
    result_i = (int)((float)result_i * c->hitlag_squat_mul);
  }
  if (result_i < 0) {
    result_i = 0;
  }
  if (result_i > 0xFFFF) {
    result_i = 0xFFFF;
  }
  return (uint16_t)result_i;
}

static inline int combat_get_env_dmg(float dmg) {
  // Decomp (GALE01): "getEnvDmg" pattern used by collision when turning a hitbox's float damage into
  // the integer damage used for shield interactions and hitlag inputs.
  // refs/melee/src/melee/ft/ftcoll.c (inlineA0/inlineA1 and ftColl_80076CBC).
  //
  // Behavior:
  // - dmg == 0 -> 0
  // - dmg != 0 and (int)dmg != 0 -> (int)dmg
  // - dmg != 0 and (int)dmg == 0 -> 1
  // Note: this intentionally matches `if (dmg)` rather than `if (dmg > 0)` (so negative nonzero and
  // NaN follow the decomp path).
  if (dmg == 0.0f) {
    return 0;
  }
  const int i = (int)dmg;
  return (i != 0) ? i : 1;
}

static inline uint8_t combat_defender_hit_status_u8(const MslBatch* batch, size_t d_idx) {
  // Debug override (test-only): 0xFF means "use tables".
  if (batch->debug_hit_status_override != NULL) {
    const uint8_t ov = batch->debug_hit_status_override[d_idx];
    if (ov != 0xFFu) {
      return ov;
    }
  }

  const uint8_t d_char = batch->state.char_id[d_idx];

  uint16_t d_msid = 0;
  const uint32_t d_msid_u32 = batch->state.animation_index[d_idx];
  if (d_msid_u32 <= 0xFFFFu) {
    d_msid = (uint16_t)d_msid_u32;
  }

  // Current policy (suite-neutral): negative/NaN anim_frame consults frame 0.
  // If we later want "negative anim_frame => don't consult tables", gate that here.
  const float d_anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t d_frame = msl_anim_frame_floor_u16(d_anim_frame_f32);

  // Decomp timing: move-induced hit status (fp->x1988) is set by movescript opcode 26 while
  // executing ftAction_80073240 inside the prio 1 Anim proc (ftAnim_8006EBA4). If a motion-state
  // transition happens after that Anim tick (e.g. due to input/IASA), the new state's cmd script
  // does not run until next frame, so x1988 should not be treated as active on the entry frame.
  // docs/DECOMP_PROC_ORDER.md (prio 1 vs prio 3)
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (eligibility aggregates x1988/x198C)
  uint8_t hit_status = 0;
  const uint16_t cur_action = batch->state.action_id[d_idx];
  const uint8_t is_shine_start_entry =
      (cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
       cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START)
          ? 1u
          : 0u;
  // Entry-frame x1988 ownership:
  // - Generic post-Anim action transitions should not consume new-state script hit_status until the
  //   next frame's ftAnim_8006EBA4 tick.
  // - Shine Start is a decomp-anchored exception where enter helpers call ftAnim_8006EBA4
  //   immediately after state change, so opcode-26 hit_status is valid on entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  if (!(d_frame == 0u && batch->state.prev_action_id[d_idx] != cur_action &&
        !is_shine_start_entry)) {
    (void)hit_status_get(d_char, d_msid, d_frame, &hit_status);
  }

  // Decomp collision eligibility uses max(fp->x1988, fp->x198C):
  // - x1988: script/hurtcaps-derived hit status (vulnerable/invincible/intangible).
  // - x198C: color-animation hit status lane that can independently elevate collision immunity.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_8006A1BC}
  const uint8_t colanim_status = batch->state.colanim_hit_status_x198c[d_idx];
  if (colanim_status > hit_status) {
    hit_status = colanim_status;
  }
  return hit_status;
}

static inline float combat_deg_to_rad_f32(void) {
  // Decomp uses a global `deg_to_rad` float constant.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcAngle
  return 0.01745329251994329577f;
}

static inline float combat_damage_sakurai_angle_radians(const MslCommonParams* c,
                                                        uint8_t defender_on_ground,
                                                        float kb_applied) {
  if (c == NULL) {
    return 0.0f;
  }

  // Decomp (GALE01): ftCo_Damage_CalcAngle, hitbox angle=361 ("Sakurai angle" sentinel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcAngle
  if (!defender_on_ground) {
    return c->sakurai_air_radians;
  }
  if (kb_applied < c->sakurai_kb_threshold) {
    return 0.0f;
  }

  const float denom = c->sakurai_kb_max - c->sakurai_kb_threshold;
  const float t = (denom > 0.0f) ? ((kb_applied - c->sakurai_kb_threshold) / denom) : 0.0f;
  const float deg = c->sakurai_ground_deg_max * t + 1.0f;
  float rad = combat_deg_to_rad_f32() * deg;

  const float max_rad = combat_deg_to_rad_f32() * c->sakurai_ground_deg_max;
  if (rad > max_rad) {
    rad = max_rad;
  }
  return rad;
}

static inline float combat_damage_calc_angle_radians(const MslCommonParams* c, uint16_t angle_deg,
                                                     uint8_t defender_on_ground, float kb_applied) {
  if (angle_deg != 361u) {
    return combat_deg_to_rad_f32() * (float)angle_deg;
  }
  return combat_damage_sakurai_angle_radians(c, defender_on_ground, kb_applied);
}

static inline uint8_t combat_damage_check_air_motion_kb_mul(const MslCommonParams* c,
                                                            const MslBatch* batch, size_t idx) {
  if (c == NULL || batch == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCo_Damage_CheckAirMotion.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CheckAirMotion
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case (uint16_t)MSL_ACT_JUMP_F:
    case (uint16_t)MSL_ACT_JUMP_B:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_F:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_B:
    case (uint16_t)MSL_ACT_FALL:
    case (uint16_t)MSL_ACT_FALL_F:
    case (uint16_t)MSL_ACT_FALL_B:
    case (uint16_t)MSL_ACT_FALL_AERIAL:
    case (uint16_t)MSL_ACT_FALL_AERIAL_F:
    case (uint16_t)MSL_ACT_FALL_AERIAL_B:
    case (uint16_t)MSL_ACT_FALL_SPECIAL:
    case (uint16_t)MSL_ACT_FALL_SPECIAL_F:
    case (uint16_t)MSL_ACT_FALL_SPECIAL_B:
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
    case (uint16_t)MSL_ACT_ESCAPE_AIR:
      if (batch->state.x680[idx] <= c->air_motion_max_frames &&
          batch->state.x684[idx] >= c->tech_lr_debounce_frames) {
        return 1;
      }
      return 0;
    default:
      return 0;
  }
}

static inline float combat_damage_ftColl_804D82EC_one(void) {
  // Decomp declares this as an extern float constant.
  // refs/melee/src/melee/ft/ftcoll.c (extern float const ftColl_804D82EC)
  //
  // GALE01 definition (not ftCommonData; lives in `.sdata2`):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::.obj ftColl_804D82EC
  // - `.float 1`
  //
  // Used in asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 and ::ftColl_80079EA8
  // - `lfs f8, ftColl_804D82EC@sda21(r0)` then `fadds f0, f8, f1`.
  return 1.0f;
}

static inline float combat_damage_ftColl_804D8314_kbg_mul(void) {
  // Decomp declares this as an extern float constant.
  // refs/melee/src/melee/ft/ftcoll.c (extern float const ftColl_804D8314)
  //
  // GALE01 definition (not ftCommonData; lives in `.sdata2`):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::.obj ftColl_804D8314
  // - `.float 0.01`
  //
  // Used in asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 and ::ftColl_80079EA8
  // - `lfs f8, ftColl_804D8314@sda21(r0)` and `fmuls f6, f8, f6` (kbg / 100).
  return 0.01f;
}

static inline float combat_damage_calc_kb_applied(
    const MslCommonParams* c, const MslCharParams* d, uint16_t defender_action_id,
    float defender_percent_pre, float defender_percent_temp, int hitbox_damage_i,
    uint16_t hitbox_kbg, uint16_t hitbox_wsk, uint16_t hitbox_bkb, float collision_kb_mul,
    uint8_t defender_dmg_x2225_b7, uint8_t defender_dmg_x2224_b2,
    uint8_t defender_kb_smashcharge_active) {
  if (c == NULL) {
    return 0.0f;
  }

  // Knockback magnitude computation comes from collision (ftColl_80079AB0).
  //
  // Decomp entry point:
  // refs/melee/src/melee/ft/ftcoll.h::ftColl_80079AB0(Fighter*, HitCapsule*, int, float, float, float, float)
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80079AB0 (stub)
  //
  // Authoritative asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  //
  // Call chain for fighter-vs-fighter hits:
  // - ftColl_8007A06C computes kb_applied via ftColl_80079AB0 and writes it into fp->dmg.kb_applied.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  //
  // Notes:
  // - The `int` arg corresponds to `HitCapsule.unk_count` (lb/types.h:+8). The hitbox pipeline
  //   stores that integer directly when building the HitCapsule:
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  //   - `stw r0, 0x8(r30)` (unk_count)
  //   - `stfs f1, 0xc(r30)` (damage)
  // - The `float` damage term used in `s = percent_int + dmg_temp` is `fp->dmg.x1838_percentTemp`
  //   (fighter.dmg:+0x1838), which accumulates the float damage this frame:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640 (adds `*dmg` into x1838_percentTemp).
  //
  // Percent-term selection (non-WSK branch):
  //
  // The GALE01 asm has a flag-gated path that replaces the `percent_int = (int)fp->dmg.x1830_percent`
  // term with one of two p_ftCommonData ints (0x6D4/0x6D8):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 (0x80079B68..0x80079BA0)
  // - if (lbz fp+0x2225) & 0x01: choose base from p_ftCommonData, else use (int)fp->dmg.x1830_percent
  // - if (lbz fp+0x2224) & 0x20: choose +0x6D8, else +0x6D4
  //
  // Decomp names for these bits (confirmed by matching bitfield operations in ftCommon_GrabMash asm):
  // - fp->x2225_b7 (mask 0x01) and fp->x2224_b2 (mask 0x20)
  // refs/melee/src/melee/ft/types.h

  float weight = 100.0f;
  if (d != NULL && d->weight > 0.0f) {
    weight = d->weight;
  }

  // Shared prelude in asm (both WSK / non-WSK):
  // - f1 = fp->co_attrs.weight * p_ftCommonData->0xF4
  // - denom = 1.0 + f1
  // - tmp = (f1 * p_ftCommonData->0xF8) / denom
  // - weight_factor = p_ftCommonData->0xF8 - tmp
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  const float one = combat_damage_ftColl_804D82EC_one();
  const float w = weight * c->kb_weight_mul;  // p_ftCommonData->0xF4
  const float denom = one + w;
  const float tmp =
      (denom != 0.0f) ? ((w * c->kb_weight_mul2) / denom) : 0.0f;  // p_ftCommonData->0xF8
  const float weight_factor = c->kb_weight_mul2 - tmp;             // p_ftCommonData->0xF8

  const float kbg_scale = combat_damage_ftColl_804D8314_kbg_mul() * (float)hitbox_kbg;
  const float bkb_f = (float)hitbox_bkb;

  float kb = 0.0f;
  if (hitbox_wsk != 0u) {
    // WSK branch (HitCapsule.x28 != 0):
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079EC8..0x80079F60)
    //
    // Interpreting HitCapsule fields from lb/types.h:
    // - x28 = WSK
    // - x24 = KBG
    // - x2C = BKB
    //
    // The branch replaces the `(s*dmg)/20 + s)/10` part with a WSK-derived term:
    // - wsk_term = p_ftCommonData->0x118 * WSK
    // - t = (p_ftCommonData->0x114 * wsk_term) + (p_ftCommonData->0x118 * p_ftCommonData->0x110)
    // - kb = (kbg/100) * (p_ftCommonData->0x11C * (weight_factor * t) + p_ftCommonData->0x120) + BKB
    const float wsk = (float)hitbox_wsk;
    const float wsk_term = c->kb_wsk_mul * wsk;  // p_ftCommonData->0x118
    const float t =
        c->kb_dmg_mul * wsk_term + (c->kb_wsk_mul * c->kb_base_term);  // 0x114, 0x118, 0x110
    const float inner = c->kb_growth_mul * (weight_factor * t) + c->kb_base_add;  // 0x11C, 0x120
    kb = bkb_f + kbg_scale * inner;

    // The asm multiplies by `ftColl_804D82EC` (1.0) three times before returning.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F54..0x80079F5C)
    kb = one * (one * (one * kb));
  } else {
    // Non-WSK branch:
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F64..0x8007A04C)
    //
    // s = percent_int + dmg_temp, where dmg_temp is fp->dmg.x1838_percentTemp (float).
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F8C..0x8007A00C)
    float percent_int = (float)(int)defender_percent_pre;  // fctiwz
    if (defender_dmg_x2225_b7) {
      // Use p_ftCommonData base ints instead of (int)percent_pre.
      const int32_t base =
          defender_dmg_x2224_b2 ? c->ftcoll_percent_base_x6d8 : c->ftcoll_percent_base_x6d4;
      percent_int = (float)base;
    }
    const float s = percent_int + defender_percent_temp;
    const float dmg = (float)hitbox_damage_i;  // HitCapsule.unk_count analogue

    // term = s * (p_ftCommonData->0x110 + p_ftCommonData->0x114 * dmg)
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A008..0x8007A034)
    const float term = s * (c->kb_base_term + c->kb_dmg_mul * dmg);                  // 0x110, 0x114
    const float inner = c->kb_growth_mul * (weight_factor * term) + c->kb_base_add;  // 0x11C, 0x120
    kb = bkb_f + kbg_scale * inner;

    // The asm multiplies by `ftColl_804D82EC` (1.0) three times before returning.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A044..0x8007A04C)
    kb = one * (one * (one * kb));
  }

  // Collision multiplier chain (post-formula):
  //
  // ftColl_80079AB0 multiplies the computed KB by:
  // - gm_8016B248() (StartMeleeRules.x30),
  // - Player_GetAttackRatio(attacker_slot),
  // - Player_GetDefenseRatio(defender_slot),
  // before clamping to p_ftCommonData->0x108 (kb_applied_max).
  //
  // Call-site evidence (fighter-vs-fighter):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  // - 0x8007A130..0x8007A148: Player_GetDefenseRatio / Player_GetAttackRatio / gm_8016B248
  // - 0x8007A160: call ftColl_80079AB0
  //
  // In-function evidence:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  // - 0x80079B58..0x80079B64 and 0x80079C48..0x80079C50: `fmuls` chain by (f1,f2,f3).
  if (collision_kb_mul != 1.0f) {
    kb *= collision_kb_mul;
  }

  // Collision clamps to p_ftCommonData->0x108 (max KB).
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A050..0x8007A060)
  if (kb > c->kb_applied_max) {
    kb = c->kb_applied_max;
  }

  // Decomp: ftCo_Damage_CalcKnockback applies squat scaling for [Squat, SquatWait].
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (defender_action_id == (uint16_t)MSL_ACT_SQUAT ||
      defender_action_id == (uint16_t)MSL_ACT_SQUAT_WAIT) {
    kb *= c->kb_squat_mul;
  }

  // Decomp: ftCo_Damage_CalcKnockback applies additional state-based KB multipliers:
  // - DamageIce: kb *= p_ftCommonData->kb_ice_mul
  // - Smash charge: kb *= p_ftCommonData->kb_smashcharge_mul (when fp->smash_attrs.state == Charging)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (defender_action_id == (uint16_t)MSL_ACT_DAMAGE_ICE) {
    kb *= c->kb_ice_mul;
  }
  // Seed/runtime lane scaffold (currently non-authoritative in replay preprocessing):
  // - decomp applies kb_smashcharge_mul when fp->smash_attrs.state == SmashState_Charging.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  // - current Slippi suite schemas do not expose smash_attrs.state directly, so keep this lane
  //   wired in seed/runtime but gameplay-neutral until an authoritative extracted signal is available.
  // TODO(decomp/kb-smashcharge-lane): enable kb_smashcharge_mul consumption once replay-real source
  // for SmashState_Charging is extracted and lock-tested.
  (void)defender_kb_smashcharge_active;

  // Decomp: ftCo_Damage_CalcKnockback subtracts armor and clamps to kb_min. We do not model armor yet;
  // keep the kb_min clamp.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (kb < c->kb_min) {
    kb = c->kb_min;
  }
  return kb;
}

static inline uint16_t combat_damage_hitstun_from_kb(const MslCommonParams* c, float kb_applied) {
  // Decomp: hitstun frames left are `mv.co.damage.x0 = (int)(kb_applied * p_ftCommonData->x154)`,
  // with a minimum of 1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_ScaleBy154
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (c == NULL) {
    return 1;
  }
  int hs = (int)(kb_applied * c->damage_hitstun_mul);  // p_ftCommonData->0x154
  if (hs <= 0) {
    hs = 1;
  }
  if (hs > 0xFFFF) {
    hs = 0xFFFF;
  }
  return (uint16_t)hs;
}

static inline uint8_t combat_damage_severity_u8_from_kb(const MslCommonParams* c,
                                                        float kb_applied) {
  // Decomp: ftCo_8008DCE0 derives severity by comparing `kb_applied * x154` against thresholds:
  // - < x158 => 0
  // - < x15C => 1
  // - < x160 => 2
  // - else   => 3
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008D8E8
  if (c == NULL) {
    return 0;
  }
  const float f = kb_applied * c->damage_hitstun_mul;  // p_ftCommonData->0x154
  if (f < c->damage_severity_x158) {                   // p_ftCommonData->0x158
    return 0;
  }
  if (f < c->damage_severity_x15c) {  // p_ftCommonData->0x15C
    return 1;
  }
  if (f < c->damage_severity_x160) {  // p_ftCommonData->0x160
    return 2;
  }
  return 3;
}

static inline void combat_damage_enter_state(const MslCommonParams* c, MslBatch* batch,
                                             size_t d_idx, uint8_t defender_on_ground_before,
                                             uint8_t defender_on_ground_after, uint8_t hurt_height,
                                             float kb_applied, float kb_angle_rad) {
  if (batch == NULL) {
    return;
  }

  if (hurt_height > 2u) {
    hurt_height = 2u;
  }

  const uint8_t sev = combat_damage_severity_u8_from_kb(c, kb_applied);

  uint16_t act = (uint16_t)MSL_ACT_WAIT;
  uint32_t sm = (uint32_t)MSL_SM_WAIT1_0;

  if (sev == 3u) {
    // High-knockback / tumble-style damage states.
    //
    // Decomp: ftCo_8008DCE0 chooses DamageFly* for var_r28==3 and later conditionally overrides
    // to DamageFlyTop based on angle.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    //
    // Decomp: ftCo_8008DCE0 includes an additional RNG-gated DamageFlyRoll path when airborne,
    // not in the DamageFlyTop window, and percent >= a common-data threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
    //
    // This sim does not currently model the global RNG stream (HSD_Randf consumers). Keep the
    // DamageFlyRoll branch disabled until we have a decomp-backed RNG site/stream.
    // Decomp order in ftCo_8008DCE0:
    // - Ground-vs-air KB handling can call ftCommon_8007D5D4 (blocks 21-28), which flips
    //   `ground_or_air` to Air for launched grounded victims.
    // - DamageFlyTop window check runs later and gates on the *current* `ground_or_air` (block_36).
    //
    // Sim mapping for `defender_on_ground_after`:
    // - this is sampled immediately after this contact's KB application in combat pass 1
    //   (i.e. after we may clear `state.on_ground[d_idx]` on launch in the same damage apply),
    // - before later frame systems (stage collision/physics integration of resulting velocity).
    //
    // Intentional scope: only this tumble DamageFlyTop window uses post-KB grounded state. The
    // broader low/med airborne-vs-grounded damage state split below continues to use the pre-hit
    // grounded flag to avoid changing non-tumble behavior.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    if (!defender_on_ground_after) {
      // DamageFlyTop window (radians).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
      // refs/melee/src/melee/ft/types.h (ftCommonData offsets 0x234/0x238)
      if (c != NULL && kb_angle_rad > c->damagefly_top_angle_min_radians &&
          kb_angle_rad < c->damagefly_top_angle_max_radians) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_TOP;
      }
    }

    if (act == (uint16_t)MSL_ACT_WAIT) {
      if (hurt_height == 2u) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_HI;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_HI;
      } else if (hurt_height == 1u) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_N;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_N;
      } else {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_LW;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_LW;
      }
    }
  } else if (!defender_on_ground_before) {
    // Airborne low/med damage states (DamageAir1/2/3).
    if (sev == 0u) {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_1;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_1;
    } else if (sev == 1u) {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_2;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_2;
    } else {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_3;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_3;
    }
  } else {
    // Grounded low/med damage states select Hi/N/Lw group by the hit hurt height.
    // refs/melee/src/melee/ft/chara/ftCommon/forward.h (DamageHi*/DamageN*/DamageLw* ids)
    if (hurt_height == 2u) {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_HI_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_HI_2
                                     : MSL_ACT_DAMAGE_HI_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_HI_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_HI_2
                                    : MSL_SM_DAMAGE_HI_3);
    } else if (hurt_height == 1u) {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_N_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_N_2
                                     : MSL_ACT_DAMAGE_N_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_N_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_N_2
                                    : MSL_SM_DAMAGE_N_3);
    } else {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_LW_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_LW_2
                                     : MSL_ACT_DAMAGE_LW_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_LW_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_LW_2
                                    : MSL_SM_DAMAGE_LW_3);
    }
  }

  batch->state.action_id[d_idx] = act;
  batch->state.animation_index[d_idx] = sm;
  // Fighter_ChangeMotionState reset clears fp->x221B_b0 (shield descriptor active) on damage
  // entry, so do not carry seeded Guard no-submotion shield-active bits into Damage* states.
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState reset block)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221B_INDEX = 2 };
  enum { MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE = 0x80 };
  {
    const size_t flags_i = d_idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
  }
  // Decomp: ftCo_8008DCE0 clears mv.co.damage.x14 on damage entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.damage_jump_buffer_x14[d_idx] = 0;
  // Decomp: ftCo_8008DCE0 performs Fighter_ChangeMotionState then immediate ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  msl_anim_timebase_enter_with_policy(batch, d_idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline void combat_mutations_pass1_future_apply_body_hit_invincible(
    MslBatch* batch, size_t a_idx, size_t hb_i, uint16_t attacker_motion_id) {
  if (batch == NULL) {
    return;
  }

  // "Invincible BODY contact" (no damage / no KB / no hitstun), but attacker still experiences hitlag.
  //
  // Decomp-first evidence (GALE01):
  // - Collision performs hurtcapsule checks if `x1988 != 2 && x198C != 2` ("not intangible"):
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
  // - Even when the defender is invincible (x1988/x198C != 0), the hit handler still computes
  //   attacker-side max int damage (`fp0->dmg.x1914 = max(..., getEnvDmg(dmg))`) before returning
  //   without applying percentTemp/KB to the defender:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  // - Fighter_ProcessHit consumes `fp->dmg.x1914` (deal-dmg path) to drive hitlag via ftCommon_CalcHitlag:
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // Selection-side rehit suppression is handled at the call-site (hitlists).

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Match the BODY stale-move ordering: apply staling to float damage before getEnvDmg, then use the
  // resulting int as hitlag input.
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);

  float dmg_f = batch->state.hitbox_damage[hb_i];
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }

  const int dmg_i = combat_get_env_dmg(dmg_f);
  if (dmg_i <= 0) {
    return;
  }

  // Decomp/ASM: electric hitlag multiplier is written to the *victim* fighter's fp+0x1960 in
  // ftColl_8007A06C when element==HitElement_Electric; attacker-side CalcHitlag uses default 1.0.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C (stfs ... 0x1960(r25))
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (ftCommon_CalcHitlag(..., x1960))
  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id, 1.0f);
  if (a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
}

// Combat Mutations Pass 1 (BODY-only).
//
// This is the minimal "writeback" set needed for one-step eval:
// - hitlag via decomp ftCommon_CalcHitlag
// - attribution fields compared in-suite (instance_hit_by, last_hit_by)
static inline void combat_mutations_pass1_future_apply_body_hit(MslBatch* batch, size_t a_idx,
                                                                size_t d_idx, int attacker,
                                                                int defender, size_t hb_i,
                                                                size_t cap_i, int int_dmg,
                                                                uint16_t attacker_motion_id) {
  if (batch == NULL) {
    return;
  }

  // === GALE01 Fighter_ProcessHit/TakDamage ordering (write-site checklist) ===
  //
  // Decomp sources:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // - refs/melee/src/melee/ft/fighter.c::Fighter_TakeDamage_8006CC7C
  //
  // Inputs (collision-produced, per-frame accumulators):
  // 1) float damage accumulator: fp->dmg.x1838_percentTemp (sum of applied float damage this frame)
  // 2) int damage (hitlag input): fp->dmg.x183C_applied (max of getEnvDmg(applied_float_damage))
  // 3) knockback magnitude: fp->dmg.kb_applied (float; 0.0 means "no KB path")
  // 4) element/flags: fp->dmg.x1860_element and other collision-written fields (angle/facing/etc.)
  //
  // Consume / apply ordering (victim side):
  // 5) If fp->dmg.kb_applied != 0.0:
  //    a) Fighter_UnkTakeDamage(fp, fp->dmg.x1838_percentTemp)  // percent add (gated inside TakeDamage)
  //    b) ftCo_Damage_CalcKnockback(fp)                         // scales/clamps kb_applied, subtracts armor
  //    c) enter Damage state / write KB velocity / hitstun
  // 6) Else if fp->dmg.kb_applied == 0.0 and fp->dmg.x1838_percentTemp != 0.0:
  //    a) Fighter_UnkTakeDamage(fp, fp->dmg.x1838_percentTemp)  // percent add only (no KB path)
  //
  // Damage gate inside Fighter_TakeDamage_8006CC7C:
  // 7) If (!fp->x2226_b4 || fp->x2226_b3): percent += damage_amount; clamp to 999.
  //
  // Hitlag (both sides, driven by per-side "max int dmg this frame"):
  // 8) If the resolved `bool1` is nonzero, set fp->dmg.x195c_hitlag_frames = ftCommon_CalcHitlag(...)
  //    and start hitlag (x221A_b2).
  //
  // End-of-frame cleanup:
  // 9) Reset fp->dmg.x1838_percentTemp to 0 and clear per-hit accumulators/flags.

  // - Set hitlag for both attacker and defender using decomp ftCommon_CalcHitlag.
  // - Update seeded/compared attribution fields that are decomp-backed:
  //   - instance_hit_by: fighter.dmg.x18ec_instancehitby (Slippi post offset 0x51)
  //     refs/melee/src/melee/ft/types.h
  //     refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //     refs/slippi-wiki/SPEC.md ("Instance Hit By")
  //   - last_hit_by: fighter.dmg.x18c4_source_ply (Slippi post offset 0x20)
  //     refs/melee/src/melee/ft/types.h
  //     refs/slippi-wiki/SPEC.md ("Last Hit By")
  //
  // NOTE (writeback reshaping pass):
  // This BODY path aims to be GALE01-shaped for the "damage/KB writeback" fields that the one-step
  // suite compares (percent, hitlag, hitstun, KB vel, damage-state entry). It is not yet a full
  // collision+damage pipeline replica (e.g. armor/reflect/absorb), but it does model the collision
  // multiplier chain that feeds ftColl_80079AB0 (attack/defense ratios and gm_8016B248()).
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  // refs/melee/src/melee/gm/gm_16AE.c::gm_8016B248
  //
  // Intentionally NOT set in this pass:
  // - last_attack_landed (Slippi "Last Hitting Attack ID") requires attack-id extraction.
  // - combo_count requires combo tracking logic beyond strict one-step mutation.

  const MslCommonParams* c = msl_common_params();

  // BODY damage (staling):
  //
  // Decomp (GALE01): collision applies stale-move multiplier to hitbox->damage (float) before
  // converting to int via getEnvDmg and before Fighter_ProcessHit consumes the values for
  // percent/hitlag/knockback.
  // - Stale multiplier: refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  // - getEnvDmg pattern: refs/melee/src/melee/ft/ftcoll.c
  // - Hitlag/percent consumption: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // Simulator policy: apply staling to the float damage that drives percent/KB, and compute the
  // int damage input for hitlag from the staled float (decomp-shaped).
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);

  float hb_dmg = batch->state.hitbox_damage[hb_i];
  if (stale_mult != 1.0f) {
    hb_dmg *= stale_mult;
  }

  // Applied damage (float) drives percent and the KB magnitude computation inputs.
  //
  // Decomp trail (GALE01):
  // - Collision accumulates per-frame float damage into fp->dmg.x1838_percentTemp via ftColl_80076640.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640
  // - Fighter_ProcessHit consumes fp->dmg.x1838_percentTemp for percent add.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // In this BODY-only path, `hb_dmg` is already stale-adjusted; additional modifiers/gates (armor,
  // reflect/absorb, etc.) are handled elsewhere or require seed fields we do not yet represent.
  const float dmg_f = hb_dmg;

  // Decomp: collision converts float damage -> env int via getEnvDmg, and Fighter_ProcessHit uses
  // a nonzero fp->dmg.x183C_applied (`bool1`) as ftCommon_CalcHitlag's `dmg` input.
  // refs/melee/src/melee/ft/ftcoll.c::getEnvDmg + ftColl_80076640 (x183C_applied update)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (hitlag calc under `if (bool1)`)
  //
  // Keep two integer damage lanes:
  // - `dmg_i`: env damage from applied float (drives hitlag path via x183C_applied)
  // - `int_dmg`: collision HitCapsule integer lane (drives ftColl_80079AB0 KB formula)
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8
  const int dmg_i = combat_get_env_dmg(dmg_f);
  if (dmg_i <= 0) {
    return;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  //
  // Decomp trail (GALE01):
  // - Collision accumulates per-frame float damage into fp->dmg.x1838_percentTemp via ftColl_80076640.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640
  // - Fighter_ProcessHit consumes it for percent add, then resets it to 0 at end of the frame.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  uint16_t d_motion_id = batch->state.action_id[d_idx];
  if (d_motion_id == (uint16_t)MSL_ACT_FALL &&
      msl_action_is_thrown_victim(batch->state.prev_action_id[d_idx])) {
    // Deferred throw-release bridge ownership:
    // - This sim may transiently place the victim in FALL before deferred throw-hit consume.
    // - In decomp, set_throw_flags consume + throw-hit apply run while victim is still in Thrown*;
    //   there is no intermediate FALL state feeding Damage calc inputs.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    d_motion_id = batch->state.prev_action_id[d_idx];
  }

  const uint8_t element = batch->state.hitbox_element[hb_i];
  // Decomp/ASM: fp->x1960_vibrateMult electric override is victim-owned (ftColl_8007A06C writes
  // 0x1960 on the fighter being processed as the collision victim), so attacker-side hitlag should
  // use mul=1.0 while defender-side uses the element-derived multiplier.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id, 1.0f);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, d_motion_id, d_hitlag_mul);
  if (a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  if (d_hl > d_hl_prev) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
  }

  // Grabbed/thrown victims are driven by an attachment joint and have empty Phys/Coll callbacks in
  // decomp; do not force a Damage* transition from a collision-confirmed hit while the victim is
  // still attached to the thrower/grab-owner.
  //
  // Decomp anchors (GALE01):
  // - Thrown victim pos driver: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - Thrown* Phys/Coll are empty (attachment-driven loop):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
  // - Throw scripts can apply damage/hitlag via set_throw_hitbox while the victim is still Thrown*:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  if (d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
      msl_action_is_grabbed_victim(d_motion_id)) {
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
    batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

    // Decomp: Fighter_ProcessHit can set fp->x221A_b3 alongside hitlag start under KB/damage paths.
    // For ThrowF/ThrownF style attached hits, Slippi observes x221A_b3 set even though the victim
    // does not enter Damage* (hitstun/misc-as remains non-damage).
    //
    // Evidence (suite dataset):
    // - datasets/.../AttachedGoodNaturedGuanaco.msl record=215 p0 ref_t1 has:
    //   hitstun=0, hitlag=4, state_flags[1]=0x30 (0x20 isHitlag | 0x10 x221A_b3).
    //
    // Decomp anchor:
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    if (d_hl > d_hl_prev) {
      combat_state_flags_set_x221a_b3(batch, d_idx);
    }

    // Attacker-side staling/combo tracking still updates on the confirmed hit.
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
    return;
  }

  // Knockback velocity + hitstun + damage-state entry (BODY).
  //
  // Decomp entry:
  // - Fighter_ProcessHit consumes `fp->dmg.kb_applied` (computed by collision) and then:
  //   - ftCo_Damage_CalcKnockback (scales/clamps kb_applied),
  //   - ftCo_8008EC90 / ftCo_8008E908 -> ftCo_8008DCE0 (damage state entry, kb vel, hitstun).
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  const uint16_t hb_angle = batch->state.hitbox_angle[hb_i];
  const uint16_t hb_kbg = batch->state.hitbox_kbg[hb_i];
  const uint16_t hb_wsk = batch->state.hitbox_wsk[hb_i];
  const uint16_t hb_bkb = batch->state.hitbox_bkb[hb_i];

  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const uint8_t hurt_height = batch->state.hurtcap_height[cap_i];

  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
  const size_t bi = a_idx / (size_t)MSL_MAX_PLAYERS;
  float coll_kb_mul = batch->state.match_damage_ratio[bi];
  coll_kb_mul *= batch->state.attack_ratio[a_idx];
  coll_kb_mul *= batch->state.defense_ratio[d_idx];
  // NOTE: This multiplier chain is applied only to kb_applied (ftColl_80079AB0 output), not to
  // percent add. Keep any percent/damage scaling tasks separate and decomp-backed.
  if (!(coll_kb_mul > 0.0f)) {
    coll_kb_mul = 1.0f;
  }
  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, int_dmg, hb_kbg, hb_wsk, hb_bkb, coll_kb_mul,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, hb_angle, defender_on_ground, kb_applied);

  // Decomp (GALE01): Fighter_ProcessHit_8006D1EC only enters the damage/KB path when
  // `fp->dmg.kb_applied` is nonzero via the exact conditional:
  // - `forceAppliedOnHit = fp->dmg.kb_applied;`  (fighter.c:2839)
  // - `if (forceAppliedOnHit) {`                 (fighter.c:2840)
  // which then does `Fighter_UnkTakeDamage_8006CC30` + `ftCo_Damage_CalcKnockback` + damage state entry.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (kb_applied == 0.0f) {
    batch->state.speed_x_attack[d_idx] = 0.0f;
    batch->state.speed_y_attack[d_idx] = 0.0f;
    batch->state.hitstun[d_idx] = 0;
    combat_state_flags_set_is_hitstun(batch, d_idx, 0);
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
    batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

    // Stale-move queue update on successful damaging BODY hit (attacker-side).
    // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);

    // Combo count + last-attack tracking still happens on the collision-confirmed hit, even if the
    // later damage-state path is skipped due to `fp->dmg.kb_applied == 0`.
    // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 -> ftColl_800763C0(fp->x2068_attackID).
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
    return;
  }

  // KB velocity + grounded/airborne interaction.
  //
  // Decomp entry: ftCo_8008DCE0 computes:
  // - var_f31 = kb_applied * p_ftCommonData->x100 (kb_vel_mul),
  // - x = var_f31 * cos(kb_angle), y = var_f31 * sin(kb_angle),
  // - then applies sign via `-x * fp->facing_dir` after setting `fp->facing_dir = fp->dmg.facing_dir_1`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  const float x = kb_vel_mag * cosf(kb_angle_rad);
  const float y = kb_vel_mag * sinf(kb_angle_rad);

  // Horizontal sign for knockback:
  //
  // Decomp:
  // - ftCo_8008DCE0 uses `fp->dmg.facing_dir_1` (set by collision) to set `fp->facing_dir`, then flips
  //   the KB x component via `-x * fp->facing_dir`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Decomp (GALE01): collision writes fp->dmg.facing_dir_1 as a +/-1 sign based on relative X
  // position between the victim and the source (fighter/item), e.g.:
  // - If victim_pos.x > src_pos.x => facing_dir_1 = -1
  // - Else                        => facing_dir_1 = +1
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C (0x8007A74C..0x8007A77C sets f24)
  const float one = combat_damage_ftColl_804D82EC_one();
  const float defender_facing_dir_1 =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -one : one;
  batch->state.facing[d_idx] = (uint8_t)(defender_facing_dir_1 > 0.0f);

  const float kb_x = -x * defender_facing_dir_1;
  const float kb_y = y;

  // Ground-vs-air handling for KB velocity:
  // - When grounded, ftCo_8008DCE0 compares the floor normal to the KB vector (lbVector_Angle) and:
  //   - if angle < 90°, launches with full (kb_x, kb_y) and clears grounded state (ftCommon_8007D5D4),
  //   - else (angle >= 90°) and low/med damage: keeps grounded and projects along the floor normal,
  //   - else tumble (sev==3): launches regardless.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (blocks 21-28)
  if (!defender_on_ground) {
    batch->state.speed_x_attack[d_idx] = kb_x;
    batch->state.speed_y_attack[d_idx] = kb_y;
  } else {
    const float nx = batch->state.ground_normal_x[d_idx];
    const float ny = batch->state.ground_normal_y[d_idx];
    const float dot = nx * kb_x + ny * kb_y;
    const uint8_t sev = combat_damage_severity_u8_from_kb(c, kb_applied);
    if (dot > 0.0f || sev == 3u) {
      batch->state.on_ground[d_idx] = 0;
      batch->state.speed_x_attack[d_idx] = kb_x;
      batch->state.speed_y_attack[d_idx] = kb_y;
    } else {
      batch->state.speed_x_attack[d_idx] = ny * kb_x;
      batch->state.speed_y_attack[d_idx] = -nx * kb_x;
    }
  }

  // Decomp: after setting KB velocity, ftCo_8008DCE0 clears self velocity (self_vel and gr_vel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_28)
  batch->state.speed_air_x_self[d_idx] = 0.0f;
  batch->state.speed_ground_x_self[d_idx] = 0.0f;
  batch->state.speed_y_self[d_idx] = 0.0f;

  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  // Decomp: Fighter_ProcessHit can set fp->x221A_b3 alongside hitlag start under KB/damage paths
  // (see `bool2`). The stable latch point we can model without additional hidden state is
  // "hitlag started this frame" (hitlag increased), because x221A_b3 is:
  // - set at hitlag start by Fighter_ProcessHit_8006D1EC, and
  // - cleared when hitlag ends by Fighter_8006A1BC.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  //
  // This intentionally excludes throw-release damage entry (ftCo_800DDDE4) where Slippi observes
  // hitstun without hitlag and x221A_b3 unset.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  if (d_hl > d_hl_prev) {
    combat_state_flags_set_x221a_b3(batch, d_idx);
  }

  const uint8_t defender_on_ground_after = batch->state.on_ground[d_idx] ? 1u : 0u;
  combat_damage_enter_state(c, batch, d_idx, defender_on_ground, defender_on_ground_after,
                            hurt_height, kb_applied, kb_angle_rad);

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

  // Stale-move queue update on successful damaging BODY hit (attacker-side).
  // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
  const uint16_t attack_instance = batch->state.attack_instance[a_idx];
  staling_queue_update(batch, a_idx, move_id, attack_instance);

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 -> ftColl_800763C0(fp->x2068_attackID).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
}

MslItemHitResult combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                       uint16_t item_attack_id, uint16_t item_attack_instance,
                                       uint16_t item_instance_id, uint16_t item_type,
                                       uint8_t item_state, float damage, uint16_t angle,
                                       uint16_t kbg, uint16_t wsk, uint16_t bkb,
                                       uint8_t defender_hurt_height, uint8_t element) {
  if (batch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return MSL_ITEM_HIT_NONE;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return MSL_ITEM_HIT_NONE;
  }

  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return MSL_ITEM_HIT_NONE;
  }

  // Decomp (GALE01): item-vs-fighter BODY apply stores both:
  // - `HitCapsule.unk_count` (raw/base integer lane from it_80272460),
  // - `HitCapsule.damage` (staled/adjusted float lane).
  //
  // For ProcessHit ownership, `fp->dmg.x183C_applied` (hitlag/KB input lane) is sourced from
  // getEnvDmg(HitCapsule.damage), i.e. the staled float converted to int.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB2}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // The item apply path receives the base hitbox damage (integer-valued for Fox/Falco blasters),
  // but hitlag/KB lanes below follow decomp ownership via getEnvDmg(staled_damage).
  const int dmg_raw_i = (int)damage;
  if (dmg_raw_i <= 0) {
    return MSL_ITEM_HIT_NONE;
  }

  // Decomp (GALE01): item collision applies staling to the item's hitbox damage before the
  // float->int getEnvDmg conversion and before Fighter_ProcessHit consumes the values.
  // refs/melee/src/melee/it/itcoll.c::it_80272460 (calls ft_80089228)
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  float dmg_f_base = damage;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f_base *= stale_mult;
  }

  const uint16_t d_motion_id = batch->state.action_id[d_idx];

  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  const uint8_t d_is_attached_grabbed_victim =
      (d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
       msl_action_is_grabbed_victim(d_motion_id))
          ? 1u
          : 0u;

  float dmg_f = dmg_f_base;
  if (d_is_attached_grabbed_victim) {
    // Damage scalar (ftCommonData.x128) applied by ftColl under certain victim/attacker gobj
    // relationships. In the grabbed-victim + item-hit edge case, the defender is attached to a
    // fighter gobj while the collision attacker is the item gobj, so this multiplier applies.
    // refs/melee/src/melee/ft/ftcoll.c::inlineB3
    // refs/melee/src/melee/ft/types.h (ftCommonData +0x128)
    //
    // Data contract: `data/common/ft_common_data.json` -> `ftcoll_damage_mul_x128`.
    // Suite anchor: required for bitwise-f32 percent parity on the attached-victim laser records
    // (e.g. QuerulousGrandDinosaur.msl records 448/8116) where ref shows a 0.91 damage increment.
    dmg_f *= c->ftcoll_damage_mul_x128;
  }

  const int dmg_env_i = combat_get_env_dmg(dmg_f);
  if (dmg_env_i <= 0) {
    return MSL_ITEM_HIT_NONE;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  // Special-case: non-flinch laser hits.
  //
  // Scope gate: only apply this rule to item kinds that are known (via ISO-extracted MSLLASR1
  // lasers.bin) to be Fox/Falco blaster shots.
  const MslLaserParams* lp = laser_params_for_item_type(item_type);
  const uint8_t non_flinch_hit =
      (lp != NULL && ((item_state == 0u) ? lp->non_flinch : lp->state1_non_flinch) != 0u) ? 1u
                                                                                             : 0u;
  if (non_flinch_hit) {
    // Extracted proxy lane ownership:
    // - non_flinch is extracted into MSLLASR1 from article hitbox kbg/wsk/bkb terms
    //   (tools/extraction/extract_lasers.py), then consumed directly here.
    // - This narrows runtime proxy logic to data ownership instead of recomputing the triplet test
    //   in combat, but the source signal is still derived from KB terms.
    // TODO(decomp/non-flinch-authoritative-signal): replace this KB-triplet-derived lane with a
    // truly authoritative decomp/data-owned no-flinch signal once identified.
    batch->state.instance_hit_by[d_idx] = item_instance_id;
    batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

    // Stale-move queue update on successful damaging BODY hit (attacker-side).
    // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);

    // Combo count + last-attack tracking (attacker-side).
    // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  const float hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_env_i, d_motion_id, hitlag_mul);
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  if (d_hl > d_hl_prev) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
  }

  // Grabbed/thrown victims are driven by an attachment joint and have empty Phys/Coll callbacks in
  // decomp; when a projectile owned by the grabber/thrower hits the attached victim, Slippi
  // commonly shows percent+hitlag but no forced Damage* entry on the victim (hitstun remains 0 and
  // action_id stays in Thrown*/Capture* while the attachment is active).
  //
  // Decomp anchors (GALE01):
  // - Thrown victim pos driver: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - Thrown* Phys/Coll are empty (attachment-driven loop):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
  // - Grab-owner identity is tracked via fp->x1064_thrownHitbox.owner and is set/cleared by:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B8CC and ::ftColl_8007B8E8
  // - Thrower release sequencing (context for when the attachment is removed):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  //
  // Simulator policy (seed-minimal, replay-parity):
  // - If the defender is a grabbed/thrown victim attached to the attacker (grab_owner_port),
  //   apply percent/hitlag attribution but do not enter Damage* (no hitstun/KB state change).
  if (d_is_attached_grabbed_victim) {
    // Victim-side x221A_b3 can be set alongside hitlag start even without Damage* entry.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    if (d_hl > d_hl_prev) {
      combat_state_flags_set_x221a_b3(batch, d_idx);
    }

    // Apply hitlag to the grab owner as well (victim is attached to the grabber during the
    // grabbed/thrown window, and the suite observes grabber hitlag on these attached hits).
    //
    // NOTE: for non-attached projectile hits, Melee applies hitlag to the item object, not the
    // owning fighter. This path is intentionally scoped to (attacker == grab_owner_port).
    const uint16_t a_motion_id = batch->state.action_id[a_idx];
    // Decomp ordering for hitlag `dmg` input uses the getEnvDmg(int) derived from the *applied*
    // staled float damage (see combat_mutations_pass1_future_apply_body_hit for the fighter-vs-fighter
    // equivalent). For attached projectile hits, suite refs observe grab-owner hitlag=3 when
    // victim hitlag=4 for Falco lasers (damage=3 => d_hl=4; getEnvDmg(staled_damage)=1 => a_hl=3).
    //
    // Victim-side hitlag_mul is driven by fp->x1960_vibrateMult (electric hits), but attacker-side
    // hitlag in this case is observed to *not* apply the electric multiplier.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (fp->x1960_vibrateMult set site)
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (passes vibrateMult into CalcHitlag)
    const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_env_i, a_motion_id, 1.0f);
    if (a_hl > batch->state.hitlag[a_idx]) {
      batch->state.hitlag[a_idx] = a_hl;
      combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
    }

    return MSL_ITEM_HIT_SUPPRESSED_DONT_CONSUME;
  }

  // Knockback velocity + hitstun + damage-state entry (BODY), following the same helper chain as
  // fighter-vs-fighter hits.
  //
  // Decomp chain:
  // - Fighter_ProcessHit_8006D1EC consumes kb_applied computed by collision and enters damage
  //   states via ftCo_8008DCE0.
  // refs/melee/src/melee/ft/fighter.c and refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }

  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, dmg_env_i, kbg, wsk, bkb, 1.0f,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, angle, defender_on_ground, kb_applied);

  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  // Horizontal sign: away from attacker fighter (owner), consistent with combat BODY apply.
  const float away = (batch->state.pos_x[d_idx] >= batch->state.pos_x[a_idx]) ? 1.0f : -1.0f;
  batch->state.speed_x_attack[d_idx] = away * (kb_vel_mag * cosf(kb_angle_rad));
  batch->state.speed_y_attack[d_idx] = kb_vel_mag * sinf(kb_angle_rad);

  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  // Mirror Fighter_ProcessHit's x221A_b3 update shape (gate on hitlag start).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  if (d_hl > d_hl_prev) {
    combat_state_flags_set_x221a_b3(batch, d_idx);
  }

  // Decomp: ftCo_8008DCE0 can clear grounded state (ftCommon_8007D5D4) before selecting the
  // damage motion state. Use the post-KB on_ground value for state entry.
  const uint8_t defender_on_ground_after = batch->state.on_ground[d_idx] ? 1u : 0u;
  combat_damage_enter_state(c, batch, d_idx, defender_on_ground, defender_on_ground_after,
                            defender_hurt_height, kb_applied, kb_angle_rad);

  batch->state.instance_hit_by[d_idx] = item_instance_id;
  batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

  // Stale-move queue update on successful damaging BODY hit (attacker-side).
  // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
  staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C -> ftColl_800763C0(item attack id domain).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
  return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
}

uint8_t combat_apply_throw_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                               const MslThrowHitboxParams* p) {
  if (batch == NULL || p == NULL) {
    return 0;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return 0;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return 0;
  }

  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return 0;
  }

  // Hit status / hurtbox-state eligibility gate (movescript-derived; opcode 26 + Slippi passthrough).
  //
  // Decomp pointers (GALE01):
  // - set_throw_flags triggers throw hit application (ftCo_800DD724), which then gates the throw
  //   damage float on ftColl_8007B868(victim):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // - Intangible blocks hurtcapsule collision checks entirely:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (guards the hurtcapsule loop on `x1988 != 2 && x198C != 2`)
  // - Invincible still allows a "contact" that can contribute to attacker hitlag, but suppresses
  //   defender damage/KB/state entry:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
  uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
  if (hit_status > hurt_state) {
    hurt_state = hit_status;
  }
  if (hurt_state == 2u) {
    return 0;
  }
  const uint8_t defender_no_damage = (hurt_state != 0u) ? 1u : 0u;

  // Decomp (GALE01):
  // - set_throw_hitbox (ftAction_80071E04) writes the raw integer damage into HitCapsule.unk_count
  //   and the staled float into HitCapsule.damage via ftColl_8007ABD0 -> ft_80089228(fp->x2068, fp->x206c).
  //   refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  //
  // In this lite sim, the throw hitbox params come from extracted move script data (base integer
  // damage). Apply staling to the float percent write (HitCapsule.damage analog), but keep the
  // raw integer damage (HitCapsule.unk_count analog) for hitlag/KB inputs.
  const int dmg_raw_i = (int)p->damage;
  if (dmg_raw_i <= 0) {
    return 0;
  }

  // Throws participate in staling (decomp: fp->x2068/x206c are used by ft_80089228).
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);

  float dmg_f = p->damage;
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  const int dmg_env_i = combat_get_env_dmg(dmg_f);
  if (dmg_env_i <= 0) {
    return 0;
  }

  if (defender_no_damage) {
    return 0;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  const uint16_t d_motion_id = batch->state.action_id[d_idx];

  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return 0;
  }

  // Throw hits are treated as airborne damage entry (victim is detached from the throw joint).
  // Decomp: throw release clears grounded state via ftCommon_8007D5D4 on the thrown fighter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  batch->state.on_ground[d_idx] = 0;
  const uint8_t defender_on_ground = 0u;

  // Knockback magnitude (ftColl_80079AB0) + damage angle (ftCo_Damage_CalcAngle).
  const size_t bi = a_idx / (size_t)MSL_MAX_PLAYERS;
  float coll_kb_mul = batch->state.match_damage_ratio[bi];
  coll_kb_mul *= batch->state.attack_ratio[a_idx];
  coll_kb_mul *= batch->state.defense_ratio[d_idx];
  if (!(coll_kb_mul > 0.0f)) {
    coll_kb_mul = 1.0f;
  }

  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, dmg_raw_i, p->kbg, p->wsk, p->bkb, coll_kb_mul,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, p->angle, defender_on_ground, kb_applied);

  if (kb_applied == 0.0f) {
    batch->state.speed_x_attack[d_idx] = 0.0f;
    batch->state.speed_y_attack[d_idx] = 0.0f;
    batch->state.hitstun[d_idx] = 0;
    combat_state_flags_set_is_hitstun(batch, d_idx, 0);
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
    batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
    return 1;
  }

  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  const float x = kb_vel_mag * cosf(kb_angle_rad);
  const float y = kb_vel_mag * sinf(kb_angle_rad);

  // Horizontal sign for throw KB uses the thrower's facing, not relative X position.
  //
  // Decomp:
  // - ftCo_800DDDE4 sets fp2->dmg.facing_dir_1 = -(fp->facing_dir) for the thrown fighter.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // - ftCo_8008DCE0 applies `kb_x = -x * fp->facing_dir` after setting facing_dir from
  //   fp->dmg.facing_dir_1.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  const float one = combat_damage_ftColl_804D82EC_one();
  const float thrower_facing_dir = batch->state.facing[a_idx] ? one : -one;
  const float defender_facing_dir_1 = -thrower_facing_dir;
  batch->state.facing[d_idx] = (uint8_t)(defender_facing_dir_1 > 0.0f);
  const float kb_x = -x * defender_facing_dir_1;
  const float kb_y = y;

  batch->state.speed_x_attack[d_idx] = kb_x;
  batch->state.speed_y_attack[d_idx] = kb_y;

  // Decomp: after setting KB velocity, ftCo_8008DCE0 clears self velocity (self_vel and gr_vel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_28)
  batch->state.speed_air_x_self[d_idx] = 0.0f;
  batch->state.speed_ground_x_self[d_idx] = 0.0f;
  batch->state.speed_y_self[d_idx] = 0.0f;

  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  // Throw-release hits do not apply hitlag in the suite (Slippi hitlag stays 0), and `x221A_b3`
  // is observed unset. Do not set it here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4

  // Throw hits mark the damaged hurtbox as "mid" in decomp (x184c_damaged_hurtbox = 1).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  const uint8_t hurt_height = 1u;
  combat_damage_enter_state(c, batch, d_idx, defender_on_ground, defender_on_ground, hurt_height,
                            kb_applied, kb_angle_rad);
  // Throw-release ordering:
  // - ftCo_800DDDE4 routes into Fighter_ProcessHit damage entry, and ftCo_8008DCE0 already performs
  //   an immediate ftAnim_8006EBA4 on state change.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Deferred throw-hit apply in this simulator happens post-items; keep only the damage-entry
  // immediate tick here (no extra deferred tick) so release rows do not over-advance action_frame.

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = (uint8_t)attacker;

  const uint16_t attack_instance = batch->state.attack_instance[a_idx];
  staling_queue_update(batch, a_idx, move_id, attack_instance);
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);

  return 1;
}

void combat_apply_item_shield_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                  uint16_t item_attack_id, uint16_t item_attack_instance,
                                  float damage, int8_t hitbox_shield_damage) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return;
  }

  const size_t d_idx = msl_idx_player(batch_index, defender);
  const size_t a_idx = msl_idx_player(batch_index, attacker);

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Decomp (GALE01): item stale multiplier is applied to item hitbox damage before collision
  // consumes it for shieldstun/hitlag as well.
  // refs/melee/src/melee/it/itcoll.c::it_80272460 (calls ft_80089228)
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  (void)item_attack_instance;
  float dmg_f = damage;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }

  const int int_dmg = combat_get_env_dmg(dmg_f);
  if (int_dmg <= 0) {
    return;
  }

  // Powershield active flag: items are reflected elsewhere (items.c); do not apply shield HP /
  // GuardSetOff / hitlag here.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  if (combat_is_powershield_active_idx(batch, d_idx)) {
    return;
  }

  const int shield_damage_taken =
      (int_dmg + (int)hitbox_shield_damage > 0) ? (int_dmg + (int)hitbox_shield_damage) : 0;

  const float light =
      combat_lightshield_amount(c, batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                batch->state.input_r[d_idx]);
  const float ls = (light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min)) +
                   c->shield_hit_lightshield_min;
  const float depletion = c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - ls)) +
                          c->shield_hit_damage_base;

  float hp = batch->state.shield_hp[d_idx];
  hp -= depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;

  // Capture defender motion id before we transition into GuardSetOff.
  const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

  // Shieldstun (GuardSetOff) entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  batch->state.animation_index[d_idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
  batch->state.tilt_timer_x[d_idx] = 0xFEu;

  const float ls_stun =
      (light * (c->shield_stun_lightshield_max - c->shield_stun_lightshield_min)) +
      c->shield_stun_lightshield_min;
  float stun_frames =
      c->shield_stun_mul * ((float)int_dmg * (1.0f - ls_stun)) + c->shield_stun_base;
  if (!(stun_frames > 0.0f)) {
    stun_frames = 1.0f;
  }
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
  float anim_rate = 1.0f;
  if (end_frame > 0.0f) {
    anim_rate = (end_frame + 0.1f) / stun_frames;
  }
  msl_anim_timebase_enter(batch, d_idx, 0.0f, anim_rate);

  // Hitlag (defender only): the "attacker" for projectiles is the item, not the owning fighter.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t d_hl = combat_calc_hitlag_frames(c, int_dmg, d_motion_id_pre, 1.0f);
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);

  // Track the owner as the source for shield state (Slippi instance_hit_by/last_hit_by are BODY-only).
  (void)a_idx;
}

static inline void combat_mutations_pass1_future_apply_shield_hit(MslBatch* batch, size_t a_idx,
                                                                  size_t d_idx, int max_int_dmg,
                                                                  int shield_damage_taken,
                                                                  uint16_t attacker_motion_id) {
  if (batch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Capture defender motion id before we transition into GuardSetOff.
  const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

  // Shield HP depletion:
  //
  // Decomp collision accumulates `shieldDamageTaken` as Σ max(0, int_dmg + hitbox_shield_damage):
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //
  // This pass is intentionally simplified: we resolve at most one SHIELD contact per
  // (attacker, defender) per frame (deterministic hitbox_id order), and pass that contact's
  // max(0, int_dmg + hitbox_shield_damage) as `shield_damage_taken`.
  //
  // Fighter_ProcessHit applies the per-frame shield health reduction:
  // shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (max_int_dmg < 0) {
    max_int_dmg = 0;
  }
  if (shield_damage_taken < 0) {
    shield_damage_taken = 0;
  }

  // Powershield gating: collision does not accumulate shieldDamageTaken when the "powershield
  // active" flag is set (x221C_b2).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (`if (!fp1->x221C_b2) { ...shieldDamageTaken... }`)
  if (combat_is_powershield_active_idx(batch, d_idx)) {
    shield_damage_taken = 0;
  }

  const float light =
      combat_lightshield_amount(c, batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                batch->state.input_r[d_idx]);
  const float ls = (light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min)) +
                   c->shield_hit_lightshield_min;
  const float depletion = c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - ls)) +
                          c->shield_hit_damage_base;

  float hp = batch->state.shield_hp[d_idx];
  hp -= depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;

  // Shieldstun (GuardSetOff) entry.
  //
  // Decomp entry: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // - Changes motion state to ftCo_MS_GuardSetOff.
  // - Sets x670_timer_lstick_tilt_x = -2.
  // - Computes shieldstun duration f (float) and sets anim rate to (0.1 + end_frame) / f.
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  // Decomp: GuardSetOff uses ftCo_SM_GuardDamage as its submotion (msid=40).
  // refs/melee/src/melee/ft/ftmotionstates.c (GuardSetOff motion-state entry uses ftCo_SM_GuardDamage)
  batch->state.animation_index[d_idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;

  // Decomp: fp->x670_timer_lstick_tilt_x = -2.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  batch->state.tilt_timer_x[d_idx] = 0xFEu;

  // Shieldstun duration f (float) and anim rate.
  //
  // Decomp:
  // f = x28C*(x19A4*(1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290
  // anim_rate = (0.1 + lbGetJObjEndFrame(GET_JOBJ(gobj))) / f
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  const float ls_stun =
      (light * (c->shield_stun_lightshield_max - c->shield_stun_lightshield_min)) +
      c->shield_stun_lightshield_min;
  float stun_frames =
      c->shield_stun_mul * ((float)max_int_dmg * (1.0f - ls_stun)) + c->shield_stun_base;
  if (!(stun_frames > 0.0f)) {
    stun_frames = 1.0f;
  }
  // GuardSetOff uses ftCo_SM_GuardDamage as the underlying animation timeline (submotion id 40).
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_Submotion)
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
  float anim_rate = 1.0f;
  if (end_frame > 0.0f) {
    anim_rate = (end_frame + 0.1f) / stun_frames;
  }
  msl_anim_timebase_enter(batch, d_idx, 0.0f, anim_rate);

  // Hitlag on shield contact uses the same decomp ftCommon_CalcHitlag path as BODY, but with
  // shield-collision inputs:
  // - attacker uses fp->dmg.x1924 (max int_dmg over shield contacts this frame),
  // - defender uses fp->x19A4 (max int_dmg over shield contacts this frame),
  // both computed from hit0->damage via getEnvDmg.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t a_hl = combat_calc_hitlag_frames(c, max_int_dmg, attacker_motion_id, 1.0f);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, max_int_dmg, d_motion_id_pre, 1.0f);
  batch->state.hitlag[a_idx] = a_hl;
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
}

static void combat_select_catch_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitlag_started_frame[a_idx] != 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint16_t a_motion_id = batch->state.action_id[a_idx];
    if (a_motion_id != (uint16_t)MSL_ACT_CATCH && a_motion_id != (uint16_t)MSL_ACT_CATCH_DASH) {
      continue;
    }

    // Decomp shape: ftColl_80078A2C keeps nearest victim by X distance (ftGrabDist), then runs the
    // catch connect transition once for that selected victim.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    int best_victim = -1;
    float best_abs_dx = 0.0f;
    uint8_t best_hit_group = 0u;
    uint8_t best_rehit_frames = 0u;

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }

      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }
      if (batch->state.hitlag_started_frame[d_idx] != 0) {
        continue;
      }
      if (batch->state.grab_owner_port[d_idx] != 0xFFu) {
        continue;
      }
      if (batch->state.is_teams[bi] && batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
        continue;
      }

      // Catch eligibility mirrors decomp vulnerable gate (x1988==0 && x198C==0): unlike BODY hits,
      // invincible victims are not catch-selectable.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state != 0u) {
        continue;
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }
      const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        if (batch->state.hitbox_element[hb_i] != (uint8_t)MSL_HIT_ELEMENT_CATCH) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        // Catch-select path is not identical to our BODY-hit suppression pipeline.
        //
        // Decomp:
        // - ftColl_80078A2C does run lbColl_8000ACFC(this_hit, victim) plus the victim mask gate
        //   `(victim_fp->x1A6A & this_fp->x1A68)` before overlap tests.
        //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        //   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
        //
        // Seed-bridge note:
        // - Our reseed bridge reconstructs HitVictim rings from the dense seed cooldown map
        //   (`combat_hitlist_cd`/`combat_hitlist_victim_iid`). On catch frames this can over-latch
        //   stale victims relative to decomp runtime pointers/masks and block replay-real connect.
        // - Intentional v1 approximation here: keep decomp-shaped catch eligibility gates above and
        //   defer HitVictim insertion to the selected catch connect below.
        //
        // This is scoped to catch selection only; BODY hits still use hitlist_allows_fighter.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];

        uint8_t found_grab_contact = 0u;
        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          if (!batch->state.hurtcap_enabled[cap_i] || !batch->state.hurtcap_is_grabbable[cap_i]) {
            continue;
          }
          const float ax = batch->state.hurtcap_a_x[cap_i];
          const float ay = batch->state.hurtcap_a_y[cap_i];
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i];
          const float by = batch->state.hurtcap_b_y[cap_i];
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          if (!combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
            continue;
          }

          const float abs_dx = fabsf(batch->state.pos_x[d_idx] - batch->state.pos_x[a_idx]);
          if (best_victim < 0 || abs_dx < best_abs_dx ||
              (abs_dx == best_abs_dx && defender < best_victim)) {
            best_victim = defender;
            best_abs_dx = abs_dx;
            best_hit_group = hit_group;
            best_rehit_frames = rehit_frames;
          }
          found_grab_contact = 1u;
          break;
        }
        if (found_grab_contact) {
          // Decomp shape: after finding a valid grabbable overlap for this defender, advance to the
          // next defender candidate (ftColl_80078A2C uses a goto next_gobj path).
          break;
        }
      }
    }

    if (best_victim < 0) {
      continue;
    }

    grab_flow_on_catch_connect(batch, bi, attacker, best_victim);
    const size_t d_idx = msl_idx_player(bi, best_victim);
    const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
    // Decomp catch path insert type is 0 via ftColl_80076808(..., type=0, ...).
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    hitlist_register_fighter_group(batch, bi, attacker, best_hit_group, best_victim,
                                   defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_CATCH,
                                   best_rehit_frames);
  }
}

static void combat_select_body_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Slippi post-frame `state_flags` includes fp+0x221C bits at byte index 3.
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };

  // Clank bookkeeping:
  // - resolve clank hitlag/rebound once per unordered pair,
  // - suppress only the clanked hitboxes (not the entire fighter pair).
  //
  // Decomp ownership:
  // - ftColl_80078C70 evaluates clank per victim hitbox branch, and only that branch skips
  //   shield/body follow-up when ftColl_8007699C confirms a clank.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
  uint8_t clank_pair_done[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS] = {{0}};
  uint8_t clank_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{{0}}};

  // Process HitElement_Catch fighter-vs-fighter contacts before shield/body damage selection.
  // Decomp shape: refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  combat_select_catch_hits_one_mutating(batch, bi);

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint16_t a_motion_id = batch->state.action_id[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }

      // Clank / rebound (hitbox-vs-hitbox).
      //
      // Decomp: clanks are resolved as part of the fighter-vs-fighter collision pass and can
      // trigger ReboundStop/Rebound transitions depending on the hitbox flags (rebound/clank).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C (enter ReboundStop)
      // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_ReboundStop=237, Rebound=238)
      //
      // Bounded v1 policy (decomp-shaped ordering):
      // - Resolve clank hitlag/rebound once per unordered fighter pair.
      // - Suppress only the clanked attacker hitboxes on each directional pass (attacker->defender).
      // - Apply per-fighter hitlag using decomp ftCommon_CalcHitlag inputs derived from each
      //   fighter's max int damage among the clanking hitboxes.
      // - If a fighter has any clanking hitbox with the `rebound` flag set, enter ReboundStop for
      //   that fighter (animation_index is -1 in-suite for ReboundStop).
      const int p0 = attacker < defender ? attacker : defender;
      const int p1 = attacker < defender ? defender : attacker;
      if (!clank_pair_done[p0][p1]) {
        clank_pair_done[p0][p1] = 1u;
        clank_pair_done[p1][p0] = 1u;
        const size_t p0_idx = msl_idx_player(bi, p0);
        const size_t p1_idx = msl_idx_player(bi, p1);
        // Decomp: hitbox-vs-hitbox clank check in ftColl_80079AB0 is gated to both fighters being
        // grounded (`this_fp->ground_or_air == GA_Ground && victim_fp->ground_or_air == GA_Ground`).
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80079AB0
        const uint8_t p0_grounded = batch->state.on_ground[p0_idx] != 0 ? 1u : 0u;
        const uint8_t p1_grounded = batch->state.on_ground[p1_idx] != 0 ? 1u : 0u;
        if (p0_grounded && p1_grounded && batch->state.hitbox_count[p0_idx] != 0 &&
            batch->state.hitbox_count[p1_idx] != 0) {
          // Clank damage-delta threshold (ftCommonData.x3CC) consumed by ftColl_8007699C.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
          // refs/melee/src/melee/ft/types.h (ftCommonData +0x3CC)
          const int clank_damage_diff_threshold = c->clank_damage_diff_threshold;
          int max_int_dmg[2] = {0, 0};
          uint8_t max_elem[2] = {0, 0};
          uint8_t want_rebound_stop[2] = {0, 0};
          uint8_t did_clank = 0;

          for (int hb0 = 0; hb0 < MSL_MAX_HITBOXES; hb0++) {
            const size_t hb0_i = idx_hitbox(bi, p0, hb0);
            if (!batch->state.hitbox_enabled[hb0_i]) {
              continue;
            }
            const uint16_t p1_iid = batch->state.instance_id[p1_idx];
            // Decomp clank candidate gating includes lbColl_8000ACFC(victim, hitbox)==0 for both
            // sides before entering ftColl_8007699C.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
            if (!hitlist_allows_fighter(batch, bi, p0, hb0, p1, p1_iid)) {
              continue;
            }
            const uint16_t f0 = batch->state.hitbox_flags[hb0_i];
            if ((f0 & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0) {
              continue;
            }
            const uint8_t e0 = batch->state.hitbox_element[hb0_i];
            if (e0 == (uint8_t)MSL_HIT_ELEMENT_INERT) {
              continue;
            }
            const float d0 = batch->state.hitbox_damage[hb0_i];
            if (!(d0 > 0.0f)) {
              continue;
            }
            const float x0 = batch->state.hitbox_x[hb0_i];
            const float y0 = batch->state.hitbox_y[hb0_i];
            const float z0 = batch->state.hitbox_z[hb0_i];
            const float r0 = batch->state.hitbox_radius[hb0_i];

            for (int hb1 = 0; hb1 < MSL_MAX_HITBOXES; hb1++) {
              const size_t hb1_i = idx_hitbox(bi, p1, hb1);
              if (!batch->state.hitbox_enabled[hb1_i]) {
                continue;
              }
              const uint16_t p0_iid = batch->state.instance_id[p0_idx];
              if (!hitlist_allows_fighter(batch, bi, p1, hb1, p0, p0_iid)) {
                continue;
              }
              const uint16_t f1 = batch->state.hitbox_flags[hb1_i];
              if ((f1 & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0) {
                continue;
              }
              const uint8_t e1 = batch->state.hitbox_element[hb1_i];
              if (e1 == (uint8_t)MSL_HIT_ELEMENT_INERT) {
                continue;
              }
              const float d1 = batch->state.hitbox_damage[hb1_i];
              if (!(d1 > 0.0f)) {
                continue;
              }

              const float x1 = batch->state.hitbox_x[hb1_i];
              const float y1 = batch->state.hitbox_y[hb1_i];
              const float z1 = batch->state.hitbox_z[hb1_i];
              const float r1 = batch->state.hitbox_radius[hb1_i];
              if (!sphere_sphere_intersects(x0, y0, z0, r0, x1, y1, z1, r1)) {
                continue;
              }
              // Decomp clank confirmation requires reciprocal x3CC comparisons.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
              const int raw0 = (int)d0;
              const int raw1 = (int)d1;
              if (!(((raw0 - clank_damage_diff_threshold) < raw1) &&
                    ((raw1 - clank_damage_diff_threshold) < raw0))) {
                continue;
              }

              did_clank = 1u;
              clank_skip_hb[p0][p1][hb0] = 1u;
              clank_skip_hb[p1][p0][hb1] = 1u;

              const int int0 = combat_get_env_dmg(d0);
              if (int0 > max_int_dmg[0]) {
                max_int_dmg[0] = int0;
                max_elem[0] = e0;
              }
              if ((f0 & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0) {
                want_rebound_stop[0] = 1u;
              }

              const int int1 = combat_get_env_dmg(d1);
              if (int1 > max_int_dmg[1]) {
                max_int_dmg[1] = int1;
                max_elem[1] = e1;
              }
              if ((f1 & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0) {
                want_rebound_stop[1] = 1u;
              }
            }
          }

          if (did_clank) {
            // Apply hitlag per fighter using each side's max int damage among clanking hitboxes.
            // Decomp: ftCommon_CalcHitlag, used by Fighter_ProcessHit_8006D1EC.
            // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            const uint16_t m0 = batch->state.action_id[p0_idx];
            const uint16_t m1 = batch->state.action_id[p1_idx];
            if (max_int_dmg[0] > 0) {
              const float mul0 = combat_hitlag_mul_from_element(c, max_elem[0]);
              const uint16_t hl0 = combat_calc_hitlag_frames(c, max_int_dmg[0], m0, mul0);
              if (hl0 > batch->state.hitlag[p0_idx]) {
                batch->state.hitlag[p0_idx] = hl0;
                combat_state_flags_set_is_hitlag(batch, p0_idx, hl0);
              }
            }
            if (max_int_dmg[1] > 0) {
              const float mul1 = combat_hitlag_mul_from_element(c, max_elem[1]);
              const uint16_t hl1 = combat_calc_hitlag_frames(c, max_int_dmg[1], m1, mul1);
              if (hl1 > batch->state.hitlag[p1_idx]) {
                batch->state.hitlag[p1_idx] = hl1;
                combat_state_flags_set_is_hitlag(batch, p1_idx, hl1);
              }
            }

            // ReboundStop transitions for hitboxes that request rebound on clank.
            if (want_rebound_stop[0]) {
              batch->state.action_id[p0_idx] = (uint16_t)MSL_ACT_REBOUND_STOP;
              batch->state.animation_index[p0_idx] = 0xFFFFFFFFu;
              msl_anim_timebase_enter(batch, p0_idx, 0.0f, 1.0f);
            }
            if (want_rebound_stop[1]) {
              batch->state.action_id[p1_idx] = (uint16_t)MSL_ACT_REBOUND_STOP;
              batch->state.animation_index[p1_idx] = 0xFFFFFFFFu;
              msl_anim_timebase_enter(batch, p1_idx, 0.0f, 1.0f);
            }
          }
        }
      }
      const uint16_t defender_iid = batch->state.instance_id[d_idx];

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      // Hitlag gating (attacker-owned):
      // - Decomp collision pass ftColl_80078C70 does not gate BODY/SHIELD candidate evaluation on
      //   victim hitlag state; each fighter is processed independently as collision owner.
      // - Keep only the attacker-side gate in this simulator lane.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      if (batch->state.hitlag_started_frame[a_idx]) {
        continue;
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      // GuardReflect no-submotion entry (`action_frame<0`, sentinel anim index) is the ambiguous
      // ordering frame between ftCo_8009388C clear and ftCo_80092450 recreate.
      // Keep shield-active ownership from the live ShieldDesc radius (x221B_b0 lane) and only
      // suppress ShieldDesc envelope expansion lanes on that entry snapshot.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      const uint8_t guard_reflect_entry_no_submotion =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14[d_idx] != 0u)
              ? 1u
              : 0u;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;

      // Combat collision consumes world-space hitbox/hurtcap primitives derived from:
      // - pose matrices driven by fp->cur_anim_frame (prio 1, ftAnim_8006EBA4), and
      // - post-Phys fighter translation (prio 4), applied to the model at prio 6/9 before
      //   the prio 13 fighter-vs-fighter collision pass.
      //
      // Decomp-backed ordering summary: docs/DECOMP_PROC_ORDER.md ("Implications for sim step order").
      // In particular, collision uses post-integration translation; do not shift primitives by
      // (prev_pos - pos) here.

      // Shield precedence (non-inert): if a hitbox intersects the defender shield bubble and
      // `element != HitElement_Inert`, resolve the shield hit (HP depletion, GuardSetOff, hitlag)
      // and do not apply BODY selection for this attacker→defender pair this frame.
      //
      // Decomp pointer (GALE01): refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 uses
      // `lbColl_80007BCC(..., &this_fp->shield_hit, ...)` as the overlap test and splits on
      // `hit->element`:
      // - non-inert calls `ftColl_80076CBC(...)` (shield hit handling),
      // - inert sets `victim_fp->x221C_b5 = true` (detection hitbox touching shield bubble) and
      //   does NOT enter the normal shield-hit effects path.
      uint8_t did_hit = 0;
      if (shield_active) {
        // Decomp (GALE01): shield collision consumes the staled HitCapsule.damage lane.
        // - Collision writes staled float damage via ft_80089228 when building HitCapsule.
        // - ftColl_80076CBC then derives max int damage with getEnvDmg(hit0->damage).
        // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
        // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        const uint16_t shield_move_id = staling_move_id_from_state(batch, a_idx);
        const float shield_stale_mult = staling_multiplier_for_move(batch, a_idx, shield_move_id);

        // Decomp (GALE01): shield collision accumulates max int damage for hitlag as:
        // - attacker: `fp0->dmg.x1924 = max(fp0->dmg.x1924, getEnvDmg(hit0->damage))`
        // - defender: `fp1->x19A4 = max(fp1->x19A4, getEnvDmg(hit0->damage))`
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        //
        // Fighter_ProcessHit then computes hitlag from these max int damage values.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        //
        // Our shield pass is simplified (one GuardSetOff entry per pair per frame), but we still
        // match the decomp "hitlag uses max int damage over shield overlaps" rule by:
        // - selecting the first eligible shield overlap for shield HP / GuardSetOff, and
        // - computing hitlag using the max int damage over all eligible shield overlaps.
        int max_int_dmg = 0;
        int sel_int_dmg = 0;
        int8_t sel_shield_dmg_s8 = 0;
        uint8_t sel_hit_group = 0;
        uint8_t sel_rehit_frames = 0;

        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
          if (!batch->state.hitbox_enabled[hb_i]) {
            continue;
          }
          if (clank_skip_hb[attacker][defender][hb_id]) {
            continue;
          }

          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              continue;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              continue;
            }
          }

          const float hx = batch->state.hitbox_x[hb_i];
          const float hy = batch->state.hitbox_y[hb_i];
          const float hz = batch->state.hitbox_z[hb_i];
          const float hr = batch->state.hitbox_radius[hb_i];

          // Rehit suppression (hitlists): decomp splits "shield overlap geometry" from "hit
          // acceptance gating".
          //
          // - Geometry only (no hitlist logic inside): lbColl_80007BCC(...)
          //   refs/melee/src/melee/ft/ftcoll.c (shield path around lbColl_80007BCC)
          //   refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          // - Rehit/hitlist gate outside geometry: lbColl_8000ACFC(victim_fp, hitcapsule)
          //   refs/melee/src/melee/ft/ftcoll.c (eligible hitcapsule predicate includes lbColl_8000ACFC(...)==0)
          //   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
          //
          // Mirror that ordering here: gate before the shield sphere overlap test.
          const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid)) {
            continue;
          }

          if (!combat_shield_overlap_ftcoll_80007bcc(
                  batch, bi, attacker, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
                  /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                  shield_desc_envelope_ready, NULL)) {
            continue;
          }

          const uint8_t element = batch->state.hitbox_element[hb_i];
          if (element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            // Slippi post-frame bit 0x221C:0x04 (GALE01): detection hitbox touching shield bubble.
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            //
            // Decomp (GALE01) sets this on inert (HitElement_Inert) shield overlaps only:
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            //   `victim_fp->x221C_b5 = true;`
            //
            // Set on the victim/defender (the fighter whose shield bubble was overlapped).
            const size_t d_flags_i =
                d_idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
            batch->state.state_flags[d_flags_i] |=
                (uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;

            // Decomp does not take the normal shield-hit path for inert hitboxes:
            // `if (hit->element != HitElement_Inert) ftColl_80076CBC(...); else victim_fp->x221C_b5=true`.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            //
            // So this overlap should NOT apply the normal "damaging block" mutations:
            // - no shield HP depletion (Fighter_ProcessHit_8006D1EC),
            // - no GuardSetOff entry,
            // - no hitlag application.
            continue;
          }

          float hdmg = batch->state.hitbox_damage[hb_i];
          if (shield_stale_mult != 1.0f) {
            hdmg *= shield_stale_mult;
          }
          if (!(hdmg > 0.0f)) {
            continue;
          }

          // Decomp (GALE01) uses getEnvDmg(hit0->damage) to compute the int damage used for shield
          // interactions and hitlag inputs.
          // refs/melee/src/melee/ft/ftcoll.c::getEnvDmg and ftColl_80076CBC
          const int int_dmg = combat_get_env_dmg(hdmg);
          if (int_dmg > max_int_dmg) {
            max_int_dmg = int_dmg;
          }

          if (sel_int_dmg == 0) {
            sel_int_dmg = int_dmg;
            sel_shield_dmg_s8 = batch->state.hitbox_shield_damage[hb_i];
            sel_hit_group = hit_group;
            sel_rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          }
        }

        if (sel_int_dmg > 0) {
          int tmp_dmg = sel_int_dmg + (int)sel_shield_dmg_s8;
          if (tmp_dmg < 0) {
            tmp_dmg = 0;
          }

          // Combat Mutations Pass 1 (SHIELD-only).
          //
          // Decomp (GALE01): shield hitlag + shieldstun duration use the max int damage over shield
          // overlaps for this frame (fp->dmg.x1924 / fp->x19A4), while shieldDamageTaken is accumulated
          // separately by collision.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
          combat_mutations_pass1_future_apply_shield_hit(batch, a_idx, d_idx, max_int_dmg, tmp_dmg,
                                                         a_motion_id);

          // Hitlist register: decomp hitlists store a `victim` pointer inside the HitCapsule, so
          // the victim identity is stable across motion-state changes (e.g. GuardSetOff entry).
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (HitVictim.victim)
          //
          // Our hitlist uses `instance_id` as a proxy for victim identity. A shield hit applies
          // GuardSetOff by calling the decomp-shaped Fighter_ChangeMotionState bundle
          // (msl_anim_timebase_enter()), which can bump `instance_id` on motion-state entry
          // (src/instance_id.c::instance_id_on_motion_state_change_ft_800895E0).
          //
          // Register using the post-mutation instance_id so that, in teacher-forced one-step
          // reseed, the hitlist identity key matches the victim identity that the reference
          // post-frame uses at t+1 (after the GuardSetOff transition), preventing spurious shield
          // re-hits / GuardSetOff re-entry on the next step.
          const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
          // Decomp insertion type on shield hit path: ftColl_80076CBC calls ftColl_80076808(..., type=1, ...).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          hitlist_register_fighter_group(batch, bi, attacker, sel_hit_group, defender,
                                         defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_SHIELD,
                                         sel_rehit_frames);

          did_hit = 1;
        }
      }

      if (did_hit) {
        continue;
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status / hurtbox-state eligibility gate (movescript-derived; opcode 26 + Slippi passthrough).
      //
      // Decomp pointers (GALE01):
      // - Hit status is driven by movescript opcode 26; decomp entry:
      //   refs/melee/src/melee/ft/ftaction.c::ftAction_80071A14
      // - Intangible blocks hurtcapsule collision checks entirely:
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (guards the hurtcapsule loop on `x1988 != 2 && x198C != 2`)
      // - Invincible (x1988/x198C != 0) still allows a "contact" that contributes to attacker-side max int damage
      //   (hitlag driver), but the defender percentTemp write is gated on vulnerability:
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8 (`if (fp1->x1988 == 0 && fp1->x198C == 0 ...) { inlineB2(...) }`)
      //
      // Policy (bounded v1, Slippi-seedable):
      // - state==2 ("intangible"): no BODY contacts selected.
      // - state==1 ("invincible"): allow contact selection but suppress defender percent/KB/hitstun writes (attacker hitlag only).
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state == 2u) {
        continue;
      }
      const uint8_t defender_no_damage = (hurt_state != 0u) ? 1u : 0u;

      // BODY contacts (pass 1): deterministic "first overlap wins" selection in (hitbox_id,
      // hurtcap_id) order.
      //
      // Note (approximation): GALE01 maintains additional per-hitbox bookkeeping and also tracks
      // `fp->dmg.int_value` as a max of getEnvDmg(damage) across eligible contacts during the
      // collision pass. We intentionally do NOT attempt to mirror the max-accumulation behavior
      // until we model more of collision ordering/priority/clank semantics, because max-accumulation
      // in this simplified pass can bias one-step timing when multiple hitboxes overlap.
      // refs/melee/src/melee/ft/ftcoll.c::inlineA0/inlineA1
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES && !did_hit; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        if (clank_skip_hb[attacker][defender][hb_id]) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        // Hitlag mutations are only applied when the resolved damage is nonzero.
        //
        // Decomp pointer (GALE01):
        // - Fighter_ProcessHit_8006D1EC sets `fp->dmg.x195c_hitlag_frames` only under `if (bool1)`
        //   (where `bool1` is the resolved nonzero damage int for the collision path):
        //   refs/melee/src/melee/ft/fighter.c:2952-2978.
        //
        // In our BODY-only pass (no percent/KB yet), treat hitboxes with nonpositive extracted
        // damage as non-damaging contacts and do not select them for hitlag/attribution writes.
        if (!(hdmg > 0.0f)) {
          continue;
        }

        const int int_dmg = combat_get_env_dmg(hdmg);
        if (int_dmg <= 0) {
          continue;
        }

        // Shield precedence (BODY path): if the hitbox intersects the defender shield bubble, do
        // not apply BODY selection for this hitbox. The shield-hit selection above handles
        // (hitbox_id)-order shield resolution; this check is a conservative fallback.
        if (shield_active && combat_shield_overlap_ftcoll_80007bcc(
                                 batch, bi, attacker, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
                                 /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                                 shield_desc_envelope_ready, NULL)) {
          continue;
        }

        // Rehit suppression (hitlists): suppress repeats while the victim is present in the
        // hitbox's victims_1 list (HitCapsule victim rings shared across same hit_group).
        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid)) {
          continue;
        }
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          if (!batch->state.hurtcap_enabled[cap_i]) {
            continue;
          }
          const float ax = batch->state.hurtcap_a_x[cap_i];
          const float ay = batch->state.hurtcap_a_y[cap_i];
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i];
          const float by = batch->state.hurtcap_b_y[cap_i];
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          uint8_t overlaps =
              combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
          if (combat_body_overlap_lbColl_80006E58_subset_allows(batch, hb_i, d_idx)) {
            overlaps = combat_body_overlap_lbColl_80006E58_scaffold(
                batch, bi, attacker, hb_id, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                batch->state.fighter_scale_y[d_idx]);
          }
          if (!overlaps) {
            continue;
          }
          // Combat Mutations Pass 1 (BODY-only).
          if (defender_no_damage) {
            combat_mutations_pass1_future_apply_body_hit_invincible(batch, a_idx, hb_i,
                                                                    a_motion_id);
          } else {
            combat_mutations_pass1_future_apply_body_hit(batch, a_idx, d_idx, attacker, defender,
                                                         hb_i, cap_i, int_dmg, a_motion_id);
          }
          // Hitlist register: decomp hitlists store a victim pointer inside HitCapsule
          // (HitVictim.victim), so the victim identity is stable across the defender's damage-state
          // entry and other motion-state changes.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          //
          // Our hitlist uses `instance_id` as a proxy identity key. BODY hits can enter a damage
          // motion state within this step, which can bump `instance_id` via ft_800895E0 on the
          // decomp-shaped ChangeMotionState path (msl_anim_timebase_enter()).
          // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
          //
          // Register using the post-mutation instance_id so that the seeded identity key at t+1
          // matches teacher-forced reseed (ref post-frame) and we don't spuriously treat the same
          // victim as "new" on the next step.
          const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
          // Decomp insertion type on BODY hit path: ftColl_80076ED8 calls inlineB0(..., type=0, cb=lbColl_80008688).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
          hitlist_register_fighter_group(batch, bi, attacker, hit_group, defender,
                                         defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                         rehit_frames);

          did_hit = 1;
          break;
        }
      }
    }
  }
}

static void combat_select_body_hits_one_debug(MslBatch* batch, int bi,
                                              MslDebugCombatContact* out_contacts,
                                              uint16_t max_contacts, uint16_t* inout_written) {
  if (batch == NULL || inout_written == NULL) {
    return;
  }
  if (out_contacts == NULL || max_contacts == 0) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  uint16_t written = *inout_written;
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }
      const uint16_t defender_iid = batch->state.instance_id[d_idx];

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status / hurtbox-state eligibility gate (matches combat_select_body_hits_one policy).
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state == 2u) {
        continue;
      }

      // Hitlag gating (attacker-owned): mirror combat_select_body_hits_one.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      if (batch->state.hitlag_started_frame[a_idx]) {
        continue;
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;

      // Deterministic selection: pick the first BODY overlap in (hitbox_id, hurtcap_id) order.
      uint8_t did_hit = 0;
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES && !did_hit; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        if (!(hdmg > 0.0f)) {
          continue;
        }

        // SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
        // and do not apply BODY selection for this hitbox.
        if (shield_active && sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr)) {
          continue;
        }

        // Rehit suppression (debug view): suppress repeats while the victim is present in the
        // hitbox's victims_1 list.
        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        (void)hit_group;
        if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid)) {
          continue;
        }

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          if (!batch->state.hurtcap_enabled[cap_i]) {
            continue;
          }
          const float ax = batch->state.hurtcap_a_x[cap_i];
          const float ay = batch->state.hurtcap_a_y[cap_i];
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i];
          const float by = batch->state.hurtcap_b_y[cap_i];
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          if (!combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
            continue;
          }

          if (written < max_contacts) {
            MslDebugCombatContact* out = &out_contacts[written];
            memset(out, 0, sizeof(*out));
            out->attacker = (uint8_t)attacker;
            out->defender = (uint8_t)defender;
            out->hitbox_id = (uint8_t)hb_id;
            out->hurtcap_id = cap_id;
            out->attacker_msid = msid;
            out->attacker_action_frame = action_frame;
            out->hitbox_x = hx;
            out->hitbox_y = hy;
            out->hitbox_z = hz;
            out->hitbox_radius = hr;
            out->hitbox_damage = hdmg;
            out->hurtcap_ax = ax;
            out->hurtcap_ay = ay;
            out->hurtcap_az = az;
            out->hurtcap_bx = bx;
            out->hurtcap_by = by;
            out->hurtcap_bz = bz;
            out->hurtcap_radius = cr;
            written++;
          }

          did_hit = 1;
          break;
        }
      }

      if (written >= max_contacts) {
        *inout_written = written;
        return;
      }
    }
  }

  *inout_written = written;
}

void combat_processhit_consume(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Clear the Slippi `state_flags` bit for "detection hitbox touching shield bubble" once per
  // fighter at a decomp-shaped "ProcessHit" consume point.
  //
  // Decomp-first references (GALE01):
  // - Set site (inert shield-overlap branch): refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  //     `victim_fp->x221C_b5 = true;` under `temp_r23->element == HitElement_Inert` and
  //     `lbColl_80007BCC(..., &this_fp->shield_hit, ...) != false`.
  // - Clear site: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //     clears `fp->x221C_b5 = 0`.
  // - Bitfield layout at fp+0x221C is documented in refs/melee/src/melee/ft/types.h.
  //
  // In this simulator, collision detection (combat_resolve) can set this bit on inert shield
  // overlaps; this consume stage clears it on the next frame, matching the intent that the bit
  // represents overlaps observed in the most recent collision pass.
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      batch->state.state_flags[flags_i] &=
          (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
      // fp->dmg.x1838_percentTemp is a per-frame accumulator consumed/reset by Fighter_ProcessHit.
      // We don't simulate the full Fighter_ProcessHit pipeline; clear it at the start of each frame
      // to ensure deterministic intra-frame accumulation during items_update/combat_resolve.
      batch->state.percent_temp[idx] = 0.0f;
    }
  }
}

void combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    combat_select_body_hits_one_mutating(batch, bi);
  }

  // Consume fp->dmg.x1838_percentTemp into percent and reset it, matching the end-of-frame cleanup
  // in Fighter_ProcessHit_8006D1EC.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (x1838_percentTemp reset)
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const float temp = batch->state.percent_temp[idx];
      if (temp != 0.0f) {
        float percent = batch->state.percent[idx] + temp;
        if (percent > 999.0f) {
          percent = 999.0f;
        }
        batch->state.percent[idx] = percent;
      }
      batch->state.percent_temp[idx] = 0.0f;
    }
  }
}

int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count) {
  if (batch == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (max_contacts == 0) {
    *out_count = 0;
    return 0;
  }
  if (out_contacts == NULL) {
    return EINVAL;
  }
  uint16_t written = 0;
  combat_select_body_hits_one_debug(batch, batch_index, out_contacts, max_contacts, &written);
  *out_count = written;
  return 0;
}

int combat_debug_shield_candidate_decisions(MslBatch* batch, int batch_index,
                                            MslDebugShieldCandidateDecision* out_rows,
                                            uint16_t max_rows, uint16_t* out_count) {
  if (batch == NULL || out_count == NULL) {
    return EINVAL;
  }
  *out_count = 0;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (max_rows == 0) {
    return 0;
  }
  if (out_rows == NULL) {
    return EINVAL;
  }

  const int num_players = (int)batch->config.num_players;
  uint16_t written = 0;
  const int bi = batch_index;
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];
    const uint8_t attacker_stock_zero = (batch->state.stocks[a_idx] == 0u) ? 1u : 0u;

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      const uint8_t defender_stock_zero = (batch->state.stocks[d_idx] == 0u) ? 1u : 0u;
      const uint8_t teams_friendly =
          (batch->state.is_teams[bi] && batch->state.team_id[a_idx] == batch->state.team_id[d_idx])
              ? 1u
              : 0u;
      const uint8_t attacker_hitlag_started = batch->state.hitlag_started_frame[a_idx] ? 1u : 0u;
      const uint8_t defender_hitlag_started = batch->state.hitlag_started_frame[d_idx] ? 1u : 0u;
      const uint8_t hitlag_gate = attacker_hitlag_started ? 1u : 0u;
      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t guard_reflect_entry_no_submotion =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14[d_idx] != 0u)
              ? 1u
              : 0u;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      // GuardReflect no-submotion entry snapshots (action_frame<0, msid sentinel) carry
      // ambiguous ordering between ftCo_8009388C clear and ftCo_80092450 recreate.
      // Keep shield-active ownership from x221B_b0, but disable ShieldDesc envelope expansion
      // lanes only for that entry frame.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;
      const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

      uint8_t pair_reason = (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD;
      if (attacker_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_ATTACKER_STOCKS_ZERO;
      } else if (defender_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_DEFENDER_STOCKS_ZERO;
      } else if (teams_friendly) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_TEAMS_FRIENDLY;
      } else if (hitlag_gate) {
        // Decomp gate shape: ftColl_80078C70 collision ownership is attacker-centric; keep this
        // debug pair gate aligned to attacker-side hitlag only.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITLAG_GATE;
      } else if (!shield_active) {
        // Decomp shield overlap path is only reached when the defender shield descriptor is active.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_INACTIVE;
      }

      {
        MslDebugShieldCandidateDecision* out = &out_rows[written];
        memset(out, 0, sizeof(*out));
        out->source_kind = (uint8_t)MSL_DEBUG_SHIELD_SOURCE_PAIR_GATE;
        out->attacker = (uint8_t)attacker;
        out->defender = (uint8_t)defender;
        out->hitbox_id = 0xFFu;
        out->reject_reason = pair_reason;
        out->attacker_hitlag_started_frame = attacker_hitlag_started;
        out->defender_hitlag_started_frame = defender_hitlag_started;
        out->shield_active = shield_active;
        out->defender_on_ground = defender_on_ground;
        out->attacker_msid = msid;
        out->attacker_action_frame = action_frame;
        out->shield_x = shx;
        out->shield_y = shy;
        out->shield_z = shz;
        out->shield_radius = shr;
        written++;
      }

      if (written >= max_rows) {
        *out_count = written;
        return 0;
      }

      if (pair_reason != (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
        continue;
      }

      const uint16_t defender_iid = batch->state.instance_id[d_idx];
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        MslDebugShieldCandidateDecision* out = &out_rows[written];
        memset(out, 0, sizeof(*out));
        out->source_kind = (uint8_t)MSL_DEBUG_SHIELD_SOURCE_FIGHTER_HITBOX;
        out->attacker = (uint8_t)attacker;
        out->defender = (uint8_t)defender;
        out->hitbox_id = (uint8_t)hb_id;
        out->attacker_hitlag_started_frame = attacker_hitlag_started;
        out->defender_hitlag_started_frame = defender_hitlag_started;
        out->shield_active = shield_active;
        out->defender_on_ground = defender_on_ground;
        out->attacker_msid = msid;
        out->attacker_action_frame = action_frame;
        out->shield_x = shx;
        out->shield_y = shy;
        out->shield_z = shz;
        out->shield_radius = shr;

        const uint8_t enabled = batch->state.hitbox_enabled[hb_i] ? 1u : 0u;
        out->hitbox_enabled = enabled;
        out->hb_flags = batch->state.hitbox_flags[hb_i];
        out->element = batch->state.hitbox_element[hb_i];
        out->hitbox_damage = batch->state.hitbox_damage[hb_i];
        out->hitbox_x = batch->state.hitbox_x[hb_i];
        out->hitbox_y = batch->state.hitbox_y[hb_i];
        out->hitbox_z = batch->state.hitbox_z[hb_i];
        out->hitbox_radius = batch->state.hitbox_radius[hb_i];

        uint8_t reason = (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD;

        if (!enabled) {
          reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITBOX_DISABLED;
        } else {
          // Ground/air eligibility gate (decomp hitcapsule x40_b2/x40_b3).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_GROUND_AIR_FLAGS;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_GROUND_AIR_FLAGS;
            }
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          // Decomp ownership: rehit suppression gate (lbColl_8000ACFC) is evaluated outside
          // shield geometry helper lbColl_80007BCC.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          const uint8_t allows =
              hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
          out->hitlist_allows = allows ? 1u : 0u;
          if (!allows) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITLIST_CONTAINS;
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          float overlap_margin = 0.0f;
          const uint8_t overlaps = combat_shield_overlap_ftcoll_80007bcc(
              batch, bi, attacker, hb_id, out->hitbox_x, out->hitbox_y, out->hitbox_z,
              out->hitbox_radius, shx, shy, shz, shr, /*shield_desc_radius=*/1.0f,
              batch->state.fighter_scale_y[d_idx], shield_desc_envelope_ready, &overlap_margin);
          out->overlap_shield = overlaps ? 1u : 0u;
          out->shield_overlap_margin = overlap_margin;
          if (!overlaps) {
            // Decomp shield geometry test helper:
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_GEOM_NO_OVERLAP;
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          if (out->element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            // Decomp split: inert overlaps set x221C_b5 and do not enter ftColl_80076CBC.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_INERT_ELEMENT;
          } else if (!(out->hitbox_damage > 0.0f)) {
            // Decomp shield-hit effects consume damaging hitcapsules (ftColl_80076CBC path).
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_NONPOS_DAMAGE;
          }
        }

        out->reject_reason = reason;
        written++;
        if (written >= max_rows) {
          *out_count = written;
          return 0;
        }
      }
    }
  }

  *out_count = written;
  return 0;
}
