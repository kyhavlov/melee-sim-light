#include "items.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "combat.h"
#include "common_params.h"
#include "char_params.h"
#include "hitlist.h"
#include "laser_params.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "stage_collision.h"
#include "staling.h"

static inline void item_slot_clear(MslBatch* batch, size_t ii) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_exists[ii] = 0;
  batch->state.item_state[ii] = 0;
  batch->state.item_type[ii] = 0;
  batch->state.item_owner[ii] = -1;
  batch->state.item_instance_id[ii] = 0;
  batch->state.item_attack_id[ii] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_attack_instance[ii] = 0;
  batch->state.item_direction[ii] = 0.0f;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_pos_x[ii] = 0.0f;
  batch->state.item_pos_y[ii] = 0.0f;
  batch->state.item_damage[ii] = 0;
  // Decomp default for non-reflected items: item->xC6C starts at identity.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  batch->state.item_reflect_damage_mul[ii] = 1.0f;
  batch->state.item_timer[ii] = 0.0f;
  batch->state.item_hitlag[ii] = 0u;
  batch->state.item_spawn_id[ii] = 0;
  batch->state.item_misc0[ii] = 0;
  batch->state.item_misc1[ii] = 0;
  batch->state.item_misc2[ii] = 0;
  batch->state.item_misc3[ii] = 0;

  // Clear per-item victim rings (hitlist).
  hitlist_capsule_clear(&batch->state.item_hitlist[ii]);
}

static inline void item_slot_swap(MslBatch* batch, size_t a, size_t b) {
  if (batch == NULL || a == b) {
    return;
  }
#define SWAP(T, arr)        \
  do {                      \
    const T tmp = (arr)[a]; \
    (arr)[a] = (arr)[b];    \
    (arr)[b] = tmp;         \
  } while (0)
  SWAP(uint8_t, batch->state.item_exists);
  SWAP(uint8_t, batch->state.item_state);
  SWAP(uint16_t, batch->state.item_type);
  SWAP(int8_t, batch->state.item_owner);
  SWAP(uint16_t, batch->state.item_instance_id);
  SWAP(uint16_t, batch->state.item_attack_id);
  SWAP(uint16_t, batch->state.item_attack_instance);
  SWAP(float, batch->state.item_direction);
  SWAP(float, batch->state.item_vel_x);
  SWAP(float, batch->state.item_vel_y);
  SWAP(float, batch->state.item_pos_x);
  SWAP(float, batch->state.item_pos_y);
  SWAP(uint16_t, batch->state.item_damage);
  SWAP(float, batch->state.item_reflect_damage_mul);
  SWAP(float, batch->state.item_timer);
  SWAP(uint8_t, batch->state.item_hitlag);
  SWAP(uint32_t, batch->state.item_spawn_id);
  SWAP(uint8_t, batch->state.item_misc0);
  SWAP(uint8_t, batch->state.item_misc1);
  SWAP(uint8_t, batch->state.item_misc2);
  SWAP(uint8_t, batch->state.item_misc3);
#undef SWAP

  // Swap per-item hitlist lanes to preserve deterministic item ordering invariants.
  {
    const MslHitlistCapsule tmp = batch->state.item_hitlist[a];
    batch->state.item_hitlist[a] = batch->state.item_hitlist[b];
    batch->state.item_hitlist[b] = tmp;
  }
}

static inline int item_key_lt(MslBatch* batch, size_t a, size_t b) {
  // Sort key matches dataset fixed ordering:
  // tools/slippi/make_dataset_from_slp.py::_fill_items_fixed sorts by (instance_id, spawn_id, type).
  const uint8_t ea = batch->state.item_exists[a] ? 1u : 0u;
  const uint8_t eb = batch->state.item_exists[b] ? 1u : 0u;
  if (ea != eb) {
    return ea > eb;
  }
  if (!ea) {
    return 0;
  }
  const uint16_t ia = batch->state.item_instance_id[a];
  const uint16_t ib = batch->state.item_instance_id[b];
  if (ia != ib) {
    return ia < ib;
  }
  const uint32_t sa = batch->state.item_spawn_id[a];
  const uint32_t sb = batch->state.item_spawn_id[b];
  if (sa != sb) {
    return sa < sb;
  }
  return batch->state.item_type[a] < batch->state.item_type[b];
}

static inline void items_sort(MslBatch* batch, int bi) {
  // Stable in-place insertion sort over 15 slots (deterministic; no allocations).
  for (int j = 1; j < MSL_MAX_ITEMS; j++) {
    int i = j;
    while (i > 0) {
      const size_t a = msl_idx_item(bi, i - 1);
      const size_t b = msl_idx_item(bi, i);
      if (!item_key_lt(batch, b, a)) {
        break;
      }
      item_slot_swap(batch, a, b);
      i--;
    }
  }
  // Normalize empty-slot owner to -1 for deterministic dataset parity.
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      batch->state.item_owner[ii] = -1;
    }
  }
}

static inline int items_alloc_slot(MslBatch* batch, int bi) {
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      return it;
    }
  }
  return -1;
}

static inline uint32_t items_next_spawn_id(const MslBatch* batch, int bi) {
  // Deterministic local monotonic id over currently-live items in this env.
  // - If any items exist: next is (max live spawn_id + 1).
  // - If no items exist: resets to 0.
  //
  // This is acceptable for v1 because spawn_id is only used as a stable secondary ordering key
  // for fixed-slot item sorting (`_fill_items_fixed`: instance_id, spawn_id, type), not as a
  // persistent gameplay identity across itemless gaps.
  uint32_t max_id = 0;
  uint8_t any = 0;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    const uint32_t sid = batch->state.item_spawn_id[ii];
    if (!any || sid > max_id) {
      max_id = sid;
      any = 1;
    }
  }
  return any ? (max_id + 1u) : 0u;
}

static inline int items_find_gun_slot(const MslBatch* batch, int bi, int owner,
                                      uint16_t gun_itkind) {
  if (batch == NULL || owner < 0) {
    return -1;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    if (batch->state.item_type[ii] != gun_itkind) {
      continue;
    }
    if (batch->state.item_owner[ii] != owner) {
      continue;
    }
    // Blaster gun items commonly have spawn_id=0 in Slippi; don't require it here
    // (identity keying is handled elsewhere).
    return it;
  }
  return -1;
}

static inline uint8_t blaster_gun_state_from_action_id(uint16_t action_id_u16) {
  // ftFx_SpecialN_GetBlasterAction returns:
  // - msid = currASID - ftFx_MS_SpecialNStart for SpecialN{Start/Loop/End}/SpecialAirN{Start/Loop/End}
  // - msid = currASID - ftCo_MS_CatchDash for Throw{B/Hi/Lw}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  // The corresponding index enum is decomp-defined as ftFx_SpecialNIndex:
  // refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_SpecialNIndex
  // (0..5 = Start/Loop/End/AirStart/AirLoop/AirEnd; 6..8 = ThrowB/ThrowHi/ThrowLw).
  //
  // IMPORTANT: in Slippi, `action_id` is the fighter's motion state enum (fp->state / GALE01 ft*MS_*),
  // while `animation_index` is a separate "subaction/submotion" id used by scripts/poses.

  // Fox/Falco MotionState ids (GALE01), decomp-backed numeric values:
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  //   (comments: ftFx_MS_SpecialNStart=341 .. ftFx_MS_SpecialAirNEnd=346)
  // - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
  //   (Falco uses the same ftFx_* MotionState ids; comments match Fox)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  //   (msid = currASID - ftFx_MS_SpecialNStart)
  enum {
    MSL_FX_MS_SPECIALN_START = 0x0155,     // ftFx_MS_SpecialNStart
    MSL_FX_MS_SPECIALN_LOOP = 0x0156,      // ftFx_MS_SpecialNLoop
    MSL_FX_MS_SPECIALN_END = 0x0157,       // ftFx_MS_SpecialNEnd
    MSL_FX_MS_SPECIALAIRN_START = 0x0158,  // ftFx_MS_SpecialAirNStart
    MSL_FX_MS_SPECIALAIRN_LOOP = 0x0159,   // ftFx_MS_SpecialAirNLoop
    MSL_FX_MS_SPECIALAIRN_END = 0x015A,    // ftFx_MS_SpecialAirNEnd
  };
  // Throw MotionState ids (GALE01 common), decomp-backed numeric values:
  // - refs/melee/src/melee/ft/ftmotionstates.c (comments: ftCo_MS_CatchDash=214, ftCo_MS_ThrowB=220, ...)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  //   (msid = currASID - ftCo_MS_CatchDash for ThrowB/ThrowHi/ThrowLw)

  if (action_id_u16 >= (uint16_t)MSL_FX_MS_SPECIALN_START &&
      action_id_u16 <= (uint16_t)MSL_FX_MS_SPECIALAIRN_END) {
    return (uint8_t)(action_id_u16 - (uint16_t)MSL_FX_MS_SPECIALN_START);
  }

  // Throw motion states that still "require the blaster item" for Fox/Falco.
  // Decomp enum values (refs/melee/src/melee/ft/ftmotionstates.c comments):
  // - ftCo_MS_CatchDash = 214 (base for throw indices)
  // - ftCo_MS_ThrowB    = 220 -> index 6 (ftFx_SpecialNIndex_ThrowB)
  // - ftCo_MS_ThrowHi   = 221 -> index 7 (ftFx_SpecialNIndex_ThrowHi)
  // - ftCo_MS_ThrowLw   = 222 -> index 8 (ftFx_SpecialNIndex_ThrowLw)
  enum { MSL_FTCO_MS_CATCHDASH = 214 };
  enum { MSL_FTCO_MS_THROWB = 220, MSL_FTCO_MS_THROWHI = 221, MSL_FTCO_MS_THROWLW = 222 };
  if (action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWB ||
      action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWHI ||
      action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWLW) {
    return (uint8_t)(action_id_u16 - (uint16_t)MSL_FTCO_MS_CATCHDASH);
  }

  return 9;
}

static inline uint8_t action_is_blaster_throw(uint16_t action_id_u16) {
  return (action_id_u16 == (uint16_t)MSL_ACT_THROW_B ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_HI ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_LW)
             ? 1u
             : 0u;
}

static inline uint8_t items_action_is_damage_family(uint16_t action_id_u16) {
  switch (action_id_u16) {
    case MSL_ACT_DAMAGE_FALL:
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
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

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t laser_grounded_body_uses_sweep(const MslBatch* batch, size_t d_idx,
                                                     float laser_age_frames) {
  if (batch == NULL) {
    return 0u;
  }
  (void)laser_age_frames;
  return (batch->state.on_ground[d_idx] == 0u) ? 1u : 0u;
}

static inline uint8_t laser_grounded_body_landing_fall_special_aabb_bridge(
    const MslBatch* batch, int bi, int def, float x0, float y0, float x, float y, float sr,
    float laser_age_frames, uint8_t* hit_hurt_height) {
  if (batch == NULL || hit_hurt_height == NULL) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, def);
  if (batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL ||
      batch->state.shield_radius[d_idx] > 0.0f || !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  // Grounded LandingFallSpecial miss-only BODY bridge:
  // - item BODY contact in it_80272460 consumes the projectile travel segment owned by
  //   itFoxlaser_UnkMotion1_Phys / it_8029C4D4.
  // - Our precise grounded probe can still miss replay-real rows in LandingFallSpecial when the
  //   live hurtcap stack spans the traveled segment but no individual capsule sweep is selected.
  // - Keep it off the first post-spawn motion tick; freshly emitted lasers already have explicit
  //   spawn/collision ordering and broadening that window reopens the adjacent no-hit row.
  // - Reconstruct only that geometric coarse overlap using the defender's currently enabled
  //   hurtcaps; keep it miss-only and state-scoped so ordinary grounded/catch/shield families stay
  //   on the precise path.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
  //   ftCo_LandingFallSpecial_Enter,ftCo_LandingFallSpecial_Anim}
  const float seg_min_x = fminf(x0, x) - sr;
  const float seg_max_x = fmaxf(x0, x) + sr;
  const float seg_min_y = fminf(y0, y) - sr;
  const float seg_max_y = fmaxf(y0, y) + sr;
  const float defender_x = batch->state.pos_x[d_idx];
  if (defender_x < seg_min_x || defender_x > seg_max_x) {
    return 0u;
  }
  const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
  for (uint8_t ci = 0; ci < cap_n; ci++) {
    const size_t hc_idx = idx_hurtcap(bi, def, ci);
    if (!batch->state.hurtcap_enabled[hc_idx]) {
      continue;
    }
    // Height-class trim:
    // - The miss-only bridge is only compensating lower/mid BODY overlap that our grounded precise
    //   probe fails to select. High/head-only capsules still stay on the exact lbColl-shaped path;
    //   broadening the bridge to height=2 reopens replay-false head contacts in the adjacent GAT
    //   LandingFallSpecial family.
    // - Decomp exposes hurt capsule height class on HitCapsule/HurtCapsule (`x43_b2`) and forwards
    //   it through lbColl_8000805C acceptance.
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    if (batch->state.hurtcap_height[hc_idx] == (uint8_t)2u) {
      continue;
    }
    const float r = batch->state.hurtcap_radius[hc_idx];
    const float cap_min_x =
        fminf(batch->state.hurtcap_a_x[hc_idx], batch->state.hurtcap_b_x[hc_idx]) - r;
    const float cap_max_x =
        fmaxf(batch->state.hurtcap_a_x[hc_idx], batch->state.hurtcap_b_x[hc_idx]) + r;
    const float cap_min_y =
        fminf(batch->state.hurtcap_a_y[hc_idx], batch->state.hurtcap_b_y[hc_idx]) - r;
    const float cap_max_y =
        fmaxf(batch->state.hurtcap_a_y[hc_idx], batch->state.hurtcap_b_y[hc_idx]) + r;
    if (seg_max_x < cap_min_x || seg_min_x > cap_max_x || seg_max_y < cap_min_y ||
        seg_min_y > cap_max_y) {
      continue;
    }
    *hit_hurt_height = batch->state.hurtcap_height[hc_idx];
    return 1u;
  }
  return 0u;
}

enum {
  // Internal runtime marker in item.misc2 for deferred powershield reflect owner transfer.
  //
  // Decomp ownership split:
  // - ftColl_80077464 writes reflect snapshot ownership/multipliers on overlap.
  // - Item_80269F14 consumes that snapshot in the subsequent item pass.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  MSL_ITEM_MISC2_PENDING_POWERSHIELD_OWNER = 0xFFu,
};

static inline uint8_t item_encode_pending_owner_port(int owner_port) {
  // Encode owner ports [0..MSL_MAX_PLAYERS-1] as [1..MSL_MAX_PLAYERS]; 0 means invalid/none.
  if (owner_port < 0 || owner_port >= MSL_MAX_PLAYERS) {
    return 0u;
  }
  return (uint8_t)(owner_port + 1);
}

enum {
  // GALE01 ItKind enum values for spacie laser shots.
  // refs/melee/src/melee/it/forward.h::ItemKind
  // data/characters/{fox,falco}.json {blaster_shot_itkind,side_special_illusion_item_kind}
  MSL_IT_KIND_FOX_LASER_SHOT = 54,
  MSL_IT_KIND_FALCO_LASER_SHOT = 55,
  MSL_IT_KIND_FOX_ILLUSION = 56,
  MSL_IT_KIND_FALCO_PHANTASM = 57,
};

enum {
  // Throw pulse frames from extracted move scripts:
  // - ThrowHi set_throw_spawn_projectile at 18/20/24 (Fox/Falco).
  // - ThrowB set_throw_spawn_projectile at 15/18/21 (Fox/Falco).
  // - ThrowLw set_throw_spawn_projectile at 23/25/28/31 (Fox/Falco).
  // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"/"ftCo_SM_ThrowB"/"ftCo_SM_ThrowLw"]["events"]
  MSL_THROWHI_PULSE_MID_AF = 20,
  MSL_THROWHI_PREV_PHASE_AF = 18,
  MSL_THROWB_PULSE_START_AF = 15,
  MSL_THROWB_PREV_PHASE_AF = 13,
  MSL_THROWLW_PULSE_ATTACH_AF = 25,
};

static inline uint8_t throw_blaster_pulse_is_seed_stale_latch(uint16_t action_id_u16,
                                                              uint16_t shot_itkind,
                                                              int16_t crossed_pulse_af,
                                                              uint16_t prev_frame_i) {
  // One-step reseed does not carry script command cursor/latch internals.
  // Decomp timing ownership:
  // - ftAction_80071974 emits one-shot throw_flags_b0 script pulses.
  // - ftAction_80073354 advances command timeline state.
  // - ftFx_Throw_Anim consumes throw_flags_b0 once to spawn throw-side laser shots.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  if (action_id_u16 == (uint16_t)MSL_ACT_THROW_HI &&
      shot_itkind == (uint16_t)MSL_IT_KIND_FALCO_LASER_SHOT &&
      crossed_pulse_af == (int16_t)MSL_THROWHI_PULSE_MID_AF &&
      prev_frame_i == (uint16_t)MSL_THROWHI_PREV_PHASE_AF) {
    return 1u;
  }
  if (action_id_u16 == (uint16_t)MSL_ACT_THROW_B &&
      crossed_pulse_af == (int16_t)MSL_THROWB_PULSE_START_AF &&
      prev_frame_i == (uint16_t)MSL_THROWB_PREV_PHASE_AF) {
    return 1u;
  }
  return 0u;
}

enum {
  // Fox/Falco side special "main" submotions that can emit cmd_var[2] ghost spawn pulses.
  // Source mapping:
  // - data/special_msids/{fox,falco}.json side_ground.main=302, side_air.main=305
  // - data/moves/{fox,falco}.json specials_by_msid["302"/"305"].events set_cmd_var(idx=2,value=1)
  MSL_ILLUSION_MAIN_GROUND_MSID = 302,
  MSL_ILLUSION_MAIN_AIR_MSID = 305,
  MSL_ILLUSION_CMDVAR2_SPAWN_ON_AF = 2,
};

static inline uint8_t action_is_illusion_dash(uint16_t action_id_u16) {
  // Ghost article spawn is owned by ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim
  // (main dash states), not Start/End states.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
  return (action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S ||
          action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S)
             ? 1u
             : 0u;
}

static inline uint8_t action_is_illusion_end(uint16_t action_id_u16) {
  return (action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S_END ||
          action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END)
             ? 1u
             : 0u;
}

static inline uint8_t action_is_illusion_setphys(uint16_t action_id_u16) {
  // Illusion/Phantasm ghost position is advanced by ftFox_SpecialS_SetPhys, which is called from
  // the grounded/air main and end Phys callbacks.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys,
  //   ftFox_SpecialS_SetPhys
  // }
  return (action_is_illusion_dash(action_id_u16) || action_is_illusion_end(action_id_u16)) ? 1u
                                                                                           : 0u;
}

static inline uint8_t illusion_spawn_pulse_crossed(uint16_t action_id_u16, uint16_t msid,
                                                   float prev_anim_frame_f32,
                                                   float cur_anim_frame_f32) {
  if (!action_is_illusion_dash(action_id_u16)) {
    return 0u;
  }
  // Cmd-var pulse source is extracted from specials_by_msid command events:
  // - side_ground.main (302): set_cmd_var(idx=2,value=1) at frame=2
  // - side_air.main    (305): set_cmd_var(idx=2,value=1) at frame=2
  // data/special_msids/{fox,falco}.json
  // data/moves/{fox,falco}.json specials_by_msid["302"/"305"].events
  if (msid != (uint16_t)MSL_ILLUSION_MAIN_GROUND_MSID &&
      msid != (uint16_t)MSL_ILLUSION_MAIN_AIR_MSID) {
    return 0u;
  }
  const float on = (float)MSL_ILLUSION_CMDVAR2_SPAWN_ON_AF;
  return (prev_anim_frame_f32 < on && cur_anim_frame_f32 >= on) ? 1u : 0u;
}

static inline int items_find_illusion_slot(const MslBatch* batch, int bi, int owner,
                                           uint16_t illusion_itkind) {
  if (batch == NULL || owner < 0) {
    return -1;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    if (batch->state.item_type[ii] != illusion_itkind) {
      continue;
    }
    if (batch->state.item_owner[ii] != owner) {
      continue;
    }
    return it;
  }
  return -1;
}

static inline float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx);

static void illusion_spawn_from_fighter(MslBatch* batch, int bi, int owner) {
  if (batch == NULL) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t char_id = batch->state.char_id[o_idx];
  uint16_t illusion_itkind = 0u;
  // Slippi char IDs: Fox=1, Falco=22 (owner-gated to spacies only).
  // data/characters/{fox,falco}.json (character payloads and side-special item kinds)
  // refs/melee/src/melee/it/forward.h::ItemKind
  if (char_id == 1u) {
    illusion_itkind = (uint16_t)MSL_IT_KIND_FOX_ILLUSION;
  } else if (char_id == 22u) {
    illusion_itkind = (uint16_t)MSL_IT_KIND_FALCO_PHANTASM;
  } else {
    return;
  }
  if (items_find_illusion_slot(batch, bi, owner, illusion_itkind) >= 0) {
    return;
  }

  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint16_t prev_action_id_u16 = batch->state.prev_action_id[o_idx];
  const uint32_t anim_u32 = batch->state.animation_index[o_idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float af = items_cur_anim_frame_f32(batch, o_idx);
  const int32_t prev_fp =
      batch->state.anim_frame_fp_q16_16[o_idx] - batch->state.frame_speed_mul_fp_q16_16[o_idx];
  const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
  uint8_t spawn_pulse = illusion_spawn_pulse_crossed(action_id_u16, msid, af_prev, af);
  if (!spawn_pulse && action_is_illusion_end(action_id_u16) &&
      batch->state.action_frame[o_idx] == 0 && batch->state.prev_action_frame[o_idx] >= 1 &&
      ((prev_action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S &&
        action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S_END) ||
       (prev_action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
        action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END))) {
    // One-step ordering bridge for side-special ghost spawn:
    // - ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim invokes ftFox_SpecialS_CreateGhostItem during
    //   main dash anim callbacks, while locomotion state progression can advance to End in the same
    //   step under teacher-forced one-step execution.
    // - When main->end transition occurs on this frame, preserve the cmd_var[2] spawn pulse
    //   ownership by accepting end-entry (action_frame==0) if previous action was the matching main
    //   and the previous action-frame already advanced into the cmd_var[2] pulse-crossing window.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim}
    // data/special_msids/{fox,falco}.json side_ground.main/side_air.main
    // data/moves/{fox,falco}.json specials_by_msid["302"/"305"].events
    spawn_pulse = 1u;
  }
  if (!spawn_pulse) {
    return;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  const MslCharParams* chp = msl_char_params(char_id);
  if (chp == NULL) {
    return;
  }

  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = illusion_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  // Spawn motion state in it_8029CFF0 is chosen by ftLib_800865CC(owner)->ground_or_air:
  // ground(0)=>state0, air(1)=>state1.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{it_8029CFF0}
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800865CC
  batch->state.item_state[ii] = batch->state.on_ground[o_idx] ? 0u : 1u;
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  batch->state.item_pos_x[ii] = batch->state.pos_x[o_idx];
  batch->state.item_pos_y[ii] = batch->state.pos_y[o_idx];
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  // Ghost article spawn initializes lifeTimer from special attrs[0].
  // data/characters/{fox,falco}.json illusion_item_lifetime_state01_frames
  // refs/melee/src/melee/it/items/itfoxillusion.c::it_8029CFF0
  batch->state.item_timer[ii] = (float)chp->illusion_item_lifetime_state01_frames;
}

static inline float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx) {
  // Item/fighter cmd/script timing consults runtime `fp->cur_anim_frame`.
  // In this sim the authoritative live lane is `anim_frame_fp_q16_16`:
  // - advanced by `anim_timebase_update_pre_input()` at proc-prio-1 ordering, and
  // - frozen under hitlag via `msl_anim_timebase_tick_once()` when `hitlag_started_frame!=0`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //
  // `anim_frame_f32` is a seed snapshot lane and can be stale mid-step, so do not use it for
  // callback/script-time cmd_var gating.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  return msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]));
}

static uint8_t blaster_gun_update_from_fighter(MslBatch* batch, int bi, int owner,
                                               const MslLaserParams* lp) {
  if (batch == NULL || lp == NULL) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t owner_char = batch->state.char_id[o_idx];
  const uint16_t prev_action_id_u16 = batch->state.prev_action_id[o_idx];
  const uint8_t prev_state = blaster_gun_state_from_action_id(prev_action_id_u16);
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint8_t want_state = blaster_gun_state_from_action_id(action_id_u16);
  uint8_t want_gun = (want_state != 9) ? 1u : 0u;

  const int existing_slot = items_find_gun_slot(batch, bi, owner, lp->gun_itkind);
  const uint8_t is_throw = action_is_blaster_throw(action_id_u16);
  if (is_throw) {
    const float af_cur = items_cur_anim_frame_f32(batch, o_idx);
    // `cmd1_cur` proxies ftFx_Throw_Anim's switch on `fp->cmd_vars[1]`:
    // - case 1: maintain/spawn gun-side ownership path
    // - case 2/0: clear ownership path
    //
    // Source of truth for this proxy is extracted move script data:
    // - `data/moves/{fox,falco}.json` set_cmd_var(idx=1) events
    // - parsed into move_tables_throw_cmd1_active()
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    const uint8_t cmd1_cur = move_tables_throw_cmd1_active(owner_char, action_id_u16, af_cur);

    // Throw-side blaster ownership in ftFx_Throw_Anim:
    // - case 1 (cmd_vars[1]==1): spawn/update gun flow
    // - case 2 / case 0: clear fighter pointer path
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    //
    // Keep throw-side gun ownership only while cmd1 is active.
    // When cmd1 turns off, clear immediately (do not apply SpecialNEnd-style linger).
    if (!cmd1_cur) {
      if (existing_slot < 0) {
        return 0u;
      }
      const size_t ii = msl_idx_item(bi, existing_slot);
      item_slot_clear(batch, ii);
      return 0u;
    }
    want_gun = 1u;
  }
  if (!want_gun) {
    if (existing_slot < 0) {
      return 0u;
    }
    const size_t ii = msl_idx_item(bi, existing_slot);

    // 1-frame linger (transition-based), decomp-shaped:
    // - The fighter clears its blaster pointer without directly destroying the item:
    //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
    //   -> ftFox_SpecialN_RemoveBlasterNULL (sets fp->fv.fx.x222C_blasterGObj = NULL)
    // - The item then detects the cleared pointer via ftFx_SpecialN_CheckRemoveBlaster and destroys itself:
    //   refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
    //   -> ftFx_SpecialN_CheckRemoveBlaster -> clear_blaster / it_802AEAB4
    //
    // NOTE: it_802AEAB4 calls Item_8026A8EC (refs/melee/src/melee/it/items/itfoxblaster.c::it_802AEAB4),
    // which schedules item destruction; it does not necessarily remove the item from the global item
    // list immediately. In the suite, this manifests as a single frame where the fighter's action_id
    // is no longer SpecialN but the gun item still exists (e.g. AttachedGoodNaturedGuanaco seed_frame=-14
    // has action_id=42 with gun present; next frame it is gone).
    // TODO: Re-check the exact frame ordering using ftFx_SpecialN_RemoveBlaster / it_802AEAB4 and Slippi
    // Recording/SendItemInfo.s once we model full fighter+item update ordering.
    //
    // Implementation:
    // - Keep a 1-frame linger only for non-throw blaster exits (SpecialN* family), matching the
    //   SpecialNEnd -> item-callback remove ordering above.
    // - Do not apply that linger to Throw* exits; ftFx_Throw_Anim has an explicit cmd_vars[1]
    //   switch (case 0/2 clear path) and does not use the SpecialNEnd helper.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    if (prev_state != 9 && !action_is_blaster_throw(prev_action_id_u16)) {
      // Keep as-is (do not overwrite item_state/pos); this is the linger frame.
      batch->state.item_exists[ii] = 1;
      batch->state.item_type[ii] = lp->gun_itkind;
      batch->state.item_owner[ii] = (int8_t)owner;
      return 0u;
    }

    item_slot_clear(batch, ii);
    return 0u;
  }

  int slot = existing_slot;
  uint8_t spawned = 0u;
  if (slot < 0) {
    slot = items_alloc_slot(batch, bi);
    if (slot < 0) {
      return 0u;
    }
    const size_t ii = msl_idx_item(bi, slot);
    item_slot_clear(batch, ii);
    batch->state.item_exists[ii] = 1;
    batch->state.item_type[ii] = lp->gun_itkind;
    batch->state.item_owner[ii] = (int8_t)owner;

    // Slippi records item instance_id from item->xDA8_short.
    // On item spawn with a fighter parent, xDA8_short is copied from the fighter's x2074.x2088.
    // refs/melee/src/melee/it/it_2725.c::it_8027B070
    batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];

    // Slippi item `spawn_id` is item->x1C. Item spawn assigns x1C from a global incrementing
    // counter (`it_804D6D10++`), so use our deterministic next-id allocator for new spawns.
    // refs/melee/src/melee/it/item.c::Item_80267AA8
    batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);

    // GALE01 ItemCommonData->xF8 is used as a default lifeTimer in item code:
    // refs/melee/src/melee/it/it_2725.c::it_8027518C (sets item->xD44_lifeTimer = it_804D6D28->xF8)
    // Slippi shows blaster gun timer=1400.0f in the suite; treat 1400.0 as the GALE01 xF8 value.
    batch->state.item_timer[ii] = 1400.0f;
    spawned = 1;
  }

  const size_t ii = msl_idx_item(bi, slot);
  batch->state.item_exists[ii] = 1;
  batch->state.item_type[ii] = lp->gun_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_state[ii] = want_state;

  // Gun is attached to a fighter part and does not have projectile physics (vel stays 0 in Slippi).
  // refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8 (spawn.vel=0, attach via Item_8026AB54)
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;

  // Position:
  // In v1, avoid introducing a new (potentially wrong) bone-space attachment transform that can
  // amplify overall item MAEs. Only initialize position on spawn; otherwise preserve the seeded
  // item position and let the one-step eval measure the residual delta.
  if (spawned) {
    batch->state.item_pos_x[ii] = batch->state.pos_x[o_idx];
    batch->state.item_pos_y[ii] = batch->state.pos_y[o_idx];
    batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  }

  // Maintain timer parity for already-seeded gun items (lifetime does not tick down in our v1).
  if (!(batch->state.item_timer[ii] > 0.0f)) {
    batch->state.item_timer[ii] = 1400.0f;
  }
  return spawned;
}

static inline uint8_t laser_should_shoot_on_frame(const MslLaserParams* lp, uint16_t msid,
                                                  uint16_t frame) {
  if (lp == NULL) {
    return 0;
  }
  if (msid == lp->ground_loop_msid) {
    const uint8_t n = lp->shoot_frame_count_ground;
    for (uint8_t i = 0; i < n && i < (uint8_t)MSL_LASER_MAX_SHOOT_FRAMES; i++) {
      if (lp->shoot_frames_ground[i] == frame) {
        return 1;
      }
    }
  }
  if (msid == lp->air_loop_msid) {
    const uint8_t n = lp->shoot_frame_count_air;
    for (uint8_t i = 0; i < n && i < (uint8_t)MSL_LASER_MAX_SHOOT_FRAMES; i++) {
      if (lp->shoot_frames_air[i] == frame) {
        return 1;
      }
    }
  }
  return 0;
}

static inline float item_segment_segment_dist2(float p0x, float p0y, float p0z, float p1x,
                                               float p1y, float p1z, float q0x, float q0y,
                                               float q0z, float q1x, float q1y, float q1z) {
  // Closest distance between two 3D segments (projectile sweep segment vs hurt capsule segment).
  // Decomp shape for fox/falco lasers:
  // - itFoxlaser_UnkMotion1_Phys snapshots prev_pos before velocity integration.
  // - it_8029C4D4 resolves collision across (prev_pos -> cur_pos), not a point probe at cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;

  const float a = ux * ux + uy * uy + uz * uz;
  const float b = ux * vx + uy * vy + uz * vz;
  const float c = vx * vx + vy * vy + vz * vz;
  const float d = ux * wx + uy * wy + uz * wz;
  const float e = vx * wx + vy * wy + vz * wz;
  const float D = a * c - b * b;
  const float eps = 1e-8f;

  float sN = 0.0f;
  float sD = D;
  float tN = 0.0f;
  float tD = D;

  if (D < eps) {
    sN = 0.0f;
    sD = 1.0f;
    tN = e;
    tD = c;
  } else {
    sN = b * e - c * d;
    tN = a * e - b * d;
    if (sN < 0.0f) {
      sN = 0.0f;
      tN = e;
      tD = c;
    } else if (sN > sD) {
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0f) {
    tN = 0.0f;
    if (-d < 0.0f) {
      sN = 0.0f;
    } else if (-d > a) {
      sN = sD;
    } else {
      sN = -d;
      sD = a;
    }
  } else if (tN > tD) {
    tN = tD;
    if ((-d + b) < 0.0f) {
      sN = 0.0f;
    } else if ((-d + b) > a) {
      sN = sD;
    } else {
      sN = -d + b;
      sD = a;
    }
  }

  const float sc = (fabsf(sN) < eps) ? 0.0f : (sN / sD);
  const float tc = (fabsf(tN) < eps) ? 0.0f : (tN / tD);

  const float dx = wx + sc * ux - tc * vx;
  const float dy = wy + sc * uy - tc * vy;
  const float dz = wz + sc * uz - tc * vz;
  return dx * dx + dy * dy + dz * dz;
}

static inline uint8_t item_swept_sphere_capsule_intersects(const MslBatch* batch, int bi,
                                                           int defender, float sx0, float sy0,
                                                           float sx1, float sy1, float sr,
                                                           int cap_i, uint8_t* out_hurt_height) {
  // Swept sphere segment (sx0,sy0,0)->(sx1,sy1,0) vs hurt capsule segment AB.
  // Decomp item-vs-fighter collision sweep uses prev_pos -> cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0;
  }
  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)defender) * (size_t)MSL_MAX_HURTCAPS +
      (size_t)cap_i;
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0;
  }
  const float ax = batch->state.hurtcap_a_x[hi];
  const float ay = batch->state.hurtcap_a_y[hi];
  const float az = batch->state.hurtcap_a_z[hi];
  const float bx = batch->state.hurtcap_b_x[hi];
  const float by = batch->state.hurtcap_b_y[hi];
  const float bz = batch->state.hurtcap_b_z[hi];
  const float cr = batch->state.hurtcap_radius[hi];
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > (rr * rr)) {
    return 0;
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1;
}

static inline uint8_t item_sphere_sphere_intersects_2d(float ax, float ay, float ar, float bx,
                                                       float by, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float rr = ar + br;
  return (dx * dx + dy * dy) <= (rr * rr);
}

static inline uint8_t item_sphere_sphere_intersects_3d(float ax, float ay, float az, float ar,
                                                       float bx, float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static inline uint8_t item_prev_action_is_guard_reflect_locomotion_pose_source(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_WAIT:
    case (uint16_t)MSL_ACT_WALK_SLOW:
    case (uint16_t)MSL_ACT_WALK_MIDDLE:
    case (uint16_t)MSL_ACT_WALK_FAST:
    case (uint16_t)MSL_ACT_TURN:
    case (uint16_t)MSL_ACT_DASH:
    case (uint16_t)MSL_ACT_RUN:
    case (uint16_t)MSL_ACT_RUN_DIRECT:
    case (uint16_t)MSL_ACT_SQUAT:
    case (uint16_t)MSL_ACT_SQUAT_WAIT:
    case (uint16_t)MSL_ACT_SQUAT_RV:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t item_try_guard_fresh_shield_center(const MslBatch* batch, size_t d_idx,
                                                         float* out_x, float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  const uint16_t prev_action_id = batch->state.seed_prev_action_id[d_idx];
  const uint8_t fresh_guard_on_entry =
      (action_id == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       batch->state.guard_on_entered_this_frame[d_idx] != 0u)
          ? 1u
          : 0u;
  const uint8_t fresh_locomotion_guard_reflect_entry =
      (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
       item_prev_action_is_guard_reflect_locomotion_pose_source(prev_action_id) &&
       prev_action_id != (uint16_t)MSL_ACT_GUARD_ON && prev_action_id != (uint16_t)MSL_ACT_GUARD &&
       prev_action_id != (uint16_t)MSL_ACT_GUARD_REFLECT &&
       prev_action_id != (uint16_t)MSL_ACT_GUARD_SET_OFF)
          ? 1u
          : 0u;
  if (!fresh_guard_on_entry && !fresh_locomotion_guard_reflect_entry) {
    return 0u;
  }

  // Fresh frozen GuardOn / locomotion GuardReflect projectile-shield bridge:
  // - ftCo_800921DC zeroes the shield-joint translate on GuardOn entry,
  // - locomotion GuardReflect entry (ftCo_80093A50) also calls ftCo_80092450 then ftCo_800921DC,
  // - ftCo_80091E78(0) preserves that live current pose on the first entry frame,
  // - later teacher-forced frozen GuardOn / GuardReflect rows are not the same owner and must keep
  //   the stable baseline shield bubble path.
  // Scope this to the grounded locomotion states that actually delegate to ftCo_80091A4C before
  // shield entry and whose pose family matches the extracted GuardOn current-pose table. Landing
  // IASA also calls ftCo_80091A4C, but the repo only extracts Guard/GuardOn pose ownership in
  // data/shields/*.bin; applying that GuardOn current-pose table to Landing-origin GuardReflect
  // rows creates unrelated shield-contact drift.
  // Restrict this to same-step entry in item shield precedence only; broadening the current-pose
  // bridge to seeded frozen guard snapshots regresses replay-real shield-hit rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_{Wait,Walk,Turn,Dash,Run,RunDirect,Squat,SquatWait,SquatRv,Landing}.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800921DC,ftCo_80091E78,ftCo_800924C0,ftCo_80093A50}
  // data/shields/{fox,falco}.bin: Guard / GuardOn shield center tables only (MSLSHLD1 v3)
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 ||
      tv.guard_on_xyz == NULL || tv.guard_on_frame_count == 0u) {
    return 0u;
  }

  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float gx = tv.guard_on_xyz[0];
  const float gy = tv.guard_on_xyz[1];
  const float gz = tv.guard_on_xyz[2];
  const float glx = gx * scale_y;
  const float gly = gy * scale_y;
  const float glz = gz * scale_y;
  *out_x = batch->state.pos_x[d_idx] + (facing_dir * glz);
  *out_y = batch->state.pos_y[d_idx] + gly;
  *out_z = batch->state.pos_z[d_idx] + (-facing_dir * glx);
  return 1u;
}

static inline uint8_t item_swept_sphere_sphere_intersects_3d(float ax0, float ay0, float az0,
                                                             float ax1, float ay1, float az1,
                                                             float ar, float bx, float by, float bz,
                                                             float br) {
  // Segment-point closest distance for swept item sphere vs shield sphere center.
  const float vx = ax1 - ax0;
  const float vy = ay1 - ay0;
  const float vz = az1 - az0;
  const float wx = bx - ax0;
  const float wy = by - ay0;
  const float wz = bz - az0;
  const float vv = vx * vx + vy * vy + vz * vz;
  float t = 0.0f;
  if (vv > 0.0f) {
    t = (wx * vx + wy * vy + wz * vz) / vv;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float cx = ax0 + t * vx;
  const float cy = ay0 + t * vy;
  const float cz = az0 + t * vz;
  return item_sphere_sphere_intersects_3d(cx, cy, cz, ar, bx, by, bz, br);
}

typedef struct MslIllusionItemHitParams {
  float radius;
  float damage;
  int8_t shield_damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  float hitbox_y_offset;
} MslIllusionItemHitParams;

enum {
  // Item script extraction uses -128 as the "no shield-damage delta" sentinel for some lanes.
  // Source: data/characters/{fox,falco}.json illusion_item_state{0,1}_shield_damage.
  MSL_ILLUSION_SHIELD_DAMAGE_UNSET = INT8_C(-128),
};

static inline uint8_t item_type_is_spacie_illusion(uint16_t type) {
  // GALE01 ItKind enum order:
  // - It_Kind_Fox_Illusion
  // - It_Kind_Falco_Phantasm
  // refs/melee/src/melee/it/forward.h::ItemKind
  return (type == (uint16_t)MSL_IT_KIND_FOX_ILLUSION ||
          type == (uint16_t)MSL_IT_KIND_FALCO_PHANTASM)
             ? 1u
             : 0u;
}

static inline uint8_t illusion_item_hit_params_from_state(const MslCharParams* chp,
                                                          uint8_t item_state,
                                                          MslIllusionItemHitParams* out) {
  if (chp == NULL || out == NULL) {
    return 0u;
  }
  out->radius = chp->illusion_item_hitbox_size;
  if (item_state == 0u) {
    out->damage = chp->illusion_item_state0_damage;
    out->shield_damage = chp->illusion_item_state0_shield_damage;
    out->angle = chp->illusion_item_state0_angle;
    out->kbg = chp->illusion_item_state0_kbg;
    out->wsk = chp->illusion_item_state0_wsk;
    out->bkb = chp->illusion_item_state0_bkb;
    out->element = chp->illusion_item_state0_element;
    out->hitbox_y_offset = chp->illusion_item_state0_hitbox_y_offset;
  } else if (item_state == 1u) {
    out->damage = chp->illusion_item_state1_damage;
    out->shield_damage = chp->illusion_item_state1_shield_damage;
    out->angle = chp->illusion_item_state1_angle;
    out->kbg = chp->illusion_item_state1_kbg;
    out->wsk = chp->illusion_item_state1_wsk;
    out->bkb = chp->illusion_item_state1_bkb;
    out->element = chp->illusion_item_state1_element;
    out->hitbox_y_offset = chp->illusion_item_state1_hitbox_y_offset;
  } else {
    // State 2 uses a visual-only callback lane (no hit callback ownership here).
    // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxillusion_UnkMotion2_Coll
    return 0u;
  }
  return 1u;
}

static inline uint8_t illusion_owner_motion_is_active(const MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Illusion article lifetime is owned by ftFx_SpecialS_CheckGhostRemove(owner):
  // active while motion_id is within [ftFx_MS_SpecialSStart .. ftFx_MS_SpecialAirSEnd].
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CheckGhostRemove
  const uint16_t owner_action = batch->state.action_id[owner_idx];
  return (owner_action >= (uint16_t)MSL_ACT_FX_SPECIAL_S_START &&
          owner_action <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END)
             ? 1u
             : 0u;
}

static inline uint8_t illusion_item_anim_step(MslBatch* batch, size_t ii, const MslCharParams* chp,
                                              uint8_t owner_motion_active) {
  if (batch == NULL || chp == NULL) {
    return 0u;
  }
  if (!owner_motion_active) {
    item_slot_clear(batch, ii);
    return 0u;
  }

  // Item animation callback ownership:
  // - state0/state1 share anim: decrement timer; on expiry enter state2 and reset timer.
  // - state2 anim: decrement timer; on expiry destroy.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{
  //   itFoxillusion_UnkMotion0_Anim,itFoxillusion_UnkMotion1_Anim,
  //   itFoxillusion_UnkMotion2_Anim,it_8029D798}
  const uint8_t state = batch->state.item_state[ii];
  float timer = batch->state.item_timer[ii];
  if (state <= 1u) {
    timer -= 1.0f;
    if (timer <= 0.0f) {
      if (chp->illusion_item_lifetime_state2_frames == 0u) {
        item_slot_clear(batch, ii);
        return 0u;
      }
      batch->state.item_state[ii] = 2u;
      batch->state.item_timer[ii] = (float)chp->illusion_item_lifetime_state2_frames;
      return 1u;
    }
    batch->state.item_timer[ii] = timer;
    return 1u;
  }

  timer -= 1.0f;
  if (timer <= 0.0f) {
    item_slot_clear(batch, ii);
    return 0u;
  }
  batch->state.item_timer[ii] = timer;
  return 1u;
}

static inline float item_reflected_damage_lane(const MslBatch* batch, size_t item_idx,
                                               float base_damage) {
  if (batch == NULL || !(base_damage > 0.0f)) {
    return base_damage;
  }
  // Decomp reflected-item damage lane is item-owned (`item->xC6C`), not owner-action-derived:
  // - reflect overlap writes `item->xC6C = ReflectDesc.damage_mul`.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // - apply path uses `(u32)(hit.damage * item->xC6C + 0.99f)`.
  //   refs/melee/src/melee/it/item.c::Item_80269F14
  //   refs/melee/src/melee/it/itcoll.c::it_80272460
  float mul = batch->state.item_reflect_damage_mul[item_idx];
  if (!(mul > 0.0f)) {
    mul = 1.0f;
  }
  float tmp = base_damage * mul + 0.99f;
  uint32_t dmg_i = 0u;
  if (tmp > 0.0f) {
    dmg_i = (uint32_t)tmp;
  }
  if (dmg_i == 0u) {
    dmg_i = 1u;
  }
  return (float)dmg_i;
}

static inline void item_apply_pending_powershield_reflect_speed(MslBatch* batch, size_t ii) {
  if (batch == NULL) {
    return;
  }
  if (batch->state.item_misc2[ii] == (uint8_t)MSL_ITEM_MISC2_PENDING_POWERSHIELD_OWNER) {
    const uint8_t enc = batch->state.item_misc3[ii];
    if (enc > 0u && enc <= (uint8_t)MSL_MAX_PLAYERS) {
      batch->state.item_owner[ii] = (int8_t)(enc - 1u);
    }
    batch->state.item_misc2[ii] = 0u;
    batch->state.item_misc3[ii] = 0u;
  }
  // Decomp ownership split:
  // - overlap path can commit reflected orientation (`facing_dir` / angle lane) immediately,
  // - velocity lane is consumed by item logic after reflect snapshot ownership transfer.
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
  // refs/melee/src/melee/it/item.c::Item_80269F14
  const float vx = batch->state.item_vel_x[ii];
  if (!(vx > 0.0f || vx < 0.0f)) {
    return;
  }
  const float dir = batch->state.item_direction[ii];
  const uint8_t pending_reflect =
      ((vx > 0.0f && dir < 0.0f) || (vx < 0.0f && dir > 0.0f)) ? 1u : 0u;
  if (!pending_reflect) {
    return;
  }
  // Narrowed temporary behavior (kept):
  // - apply deferred reflect with identity speed multiplier (1.0f) on the next item pass,
  //   keyed by reflected orientation mismatch.
  // TODO(decomp/powershield-reflect-speed-mul): validate authoritative x2B0 ownership/timing
  // for these rows and replace this identity speed-mul bridge.
  const float mul = 1.0f;
  const float new_vx = -vx * mul;
  const float new_vy = -batch->state.item_vel_y[ii] * mul;
  batch->state.item_vel_x[ii] = new_vx;
  batch->state.item_vel_y[ii] = new_vy;
  batch->state.item_direction[ii] = (new_vx >= 0.0f) ? 1.0f : -1.0f;
}

static inline void item_apply_powershield_reflect_snapshot(MslBatch* batch, size_t ii,
                                                           int reflector_port, float damage_mul) {
  if (batch == NULL) {
    return;
  }
  // Reflect snapshot ownership:
  // - overlap writes owner/xDA8_short and multipliers to item-owned reflect snapshot.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // - item logic consumes that snapshot in Item_80269F14.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  //
  // Runtime model: capture pending transfer owner in internal misc lanes and commit ownership in
  // the next item pass (item_apply_pending_powershield_reflect_speed), matching snapshot->consume
  // ordering above.
  batch->state.item_misc2[ii] = (uint8_t)MSL_ITEM_MISC2_PENDING_POWERSHIELD_OWNER;
  batch->state.item_misc3[ii] = item_encode_pending_owner_port(reflector_port);

  const float dmg_mul = (damage_mul > 0.0f) ? damage_mul : 1.0f;
  // Decomp visual reflect lane flips facing/angle on overlap (`it_2725_Logic94_Reflected`) even when
  // velocity update is consumed later by item logic (`Item_80269F14`).
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
  // refs/melee/src/melee/it/item.c::Item_80269F14
  const float reflect_vx = -batch->state.item_vel_x[ii];
  if (reflect_vx > 0.0f) {
    batch->state.item_direction[ii] = 1.0f;
  } else if (reflect_vx < 0.0f) {
    batch->state.item_direction[ii] = -1.0f;
  } else {
    const float cur_dir = batch->state.item_direction[ii];
    batch->state.item_direction[ii] = (cur_dir >= 0.0f) ? -1.0f : 1.0f;
  }
  // Keep damage ownership immediately item-owned; deferred speed apply is signaled by the
  // reflected orientation lane (`item_direction`) and consumed in item_update_lasers().
  //
  // TODO(decomp/powershield-reflect-ownership-timing): transfer-frame xDA8_short
  // (Slippi item.instance_id) remains seed-latched in this lane until the authoritative
  // transfer ordering is extracted.
  batch->state.item_reflect_damage_mul[ii] = dmg_mul;
}

typedef enum MslLaserCollisionSpaceLane {
  MSL_LASER_COLLISION_SPACE_SHIELD = 0,
  MSL_LASER_COLLISION_SPACE_REFLECT = 1,
  MSL_LASER_COLLISION_SPACE_BODY = 2,
} MslLaserCollisionSpaceLane;

static inline float laser_collision_offset_scale(const MslLaserParams* lp, uint8_t laser_state,
                                                 float laser_scale_z,
                                                 MslLaserCollisionSpaceLane lane,
                                                 uint8_t body_shield_adjacent,
                                                 uint8_t shield_cap_enabled) {
  (void)lp;
  (void)laser_state;
  // Collision-space policy (decomp/data-owned, shared ownership lanes):
  // - Hitbox offsets are extracted authored lanes from the article script (`create_hitbox` x_offset):
  //   tools/extraction/extract_lasers.py
  // - Laser motion anim applies a per-frame model scaleZ ramp:
  //   refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
  // - Shield overlap, BODY overlap, and reflector overlap all consume the same item-owned collision
  //   space (hitcapsule world positions/radii) under fighter/item collision ownership:
  //   refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //   refs/melee/src/melee/it/itcoll.c::it_80272460
  //
  // Decomp-backed policy: apply one transform chain for all overlap lanes; do not inject
  // shield/body/reflect-specific clamps.
  float s = laser_scale_z;
  if (!(s > 0.0f)) {
    // Narrowed temporary: reseeded rows can carry invalid/missing per-item visual scale history.
    // Keep identity fallback until authoritative item hitcapsule scale ownership is extracted.
    // Decomp anim floor remains 1e-3 in itFoxlaser_UnkMotion1_Anim.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    s = 1.0f;
  }
  // Narrowed temporary (replay-guarded):
  // - until item-hit capsule transform ownership is extracted end-to-end for shield-adjacent rows,
  //   keep identity cap for SHIELD lane and shield-adjacent BODY lane.
  // - This preserves replay-real GuardOn/Guard no-submotion control rows while reflect/body rows
  //   still consume the same decomp-owned scaleZ lane.
  // TODO(narrowed_temporary/decomp-items-collision-space-shield-adjacent-cap): replace this cap
  // with direct item hitcapsule transform ownership once extracted (it_8027137C / lbColl path).
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  if ((lane == MSL_LASER_COLLISION_SPACE_SHIELD ||
       (lane == MSL_LASER_COLLISION_SPACE_BODY && body_shield_adjacent)) &&
      shield_cap_enabled && s > 1.0f) {
    s = 1.0f;
  }
  return s;
}

static inline uint8_t laser_try_shield_bounce_velocity(float vx, float vy, float shield_x,
                                                       float shield_y, float contact_x,
                                                       float contact_y, float* out_vx,
                                                       float* out_vy) {
  if (out_vx == NULL || out_vy == NULL) {
    return 0u;
  }

  const float nx = contact_x - shield_x;
  const float ny = contact_y - shield_y;
  const float n2 = (nx * nx) + (ny * ny);
  if (!(n2 > 0.0f)) {
    return 0u;
  }

  const float inv_n = 1.0f / sqrtf(n2);
  const float unx = nx * inv_n;
  const float uny = ny * inv_n;
  if (!(ny > fabsf(nx))) {
    return 0u;
  }
  const float dot = (vx * unx) + (vy * uny);
  if (!(dot < 0.0f)) {
    return 0u;
  }

  const float rvx = vx - (2.0f * dot * unx);
  const float rvy = vy - (2.0f * dot * uny);

  // Shield-bounce ownership:
  // - Item_80269DC8 routes eligible shield contacts through the per-item `shield_bounced`
  //   callback instead of `hit_shield`.
  // - Fox laser `itFoxLaser_Logic94_ShieldBounced` mirrors velocity over `item->xC58`,
  //   keeps the projectile alive, and updates the laser angle from the mirrored velocity.
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
  //
  // This lite sim does not yet seed the authoritative `xDCE_flag.b5/xDCE_flag.b4/xC54/xC58`
  // shield-bounce internals. Narrow approximation: only keep the item alive when the shield-sphere
  // contact normal is upper-hemisphere dominant and yields an upward mirrored travel vector,
  // matching the replay-real glancing upper-shield bounce families while leaving front-side
  // shield hits on the destroy path below.
  if (!(rvy > 0.0f)) {
    return 0u;
  }

  *out_vx = rvx;
  *out_vy = rvy;
  return 1u;
}

static inline void item_apply_reflect_transfer(MslBatch* batch, size_t ii, size_t reflector_idx,
                                               int reflector_port, float damage_mul,
                                               float speed_mul) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_misc2[ii] = 0u;
  batch->state.item_misc3[ii] = 0u;
  batch->state.item_owner[ii] = (int8_t)reflector_port;
  // Slippi item.instance_id is item->xDA8_short; reflect apply rewrites it from the reflecting
  // fighter snapshot (fp->x2088 / instance_id).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  batch->state.item_instance_id[ii] = batch->state.instance_id[reflector_idx];
  batch->state.item_reflect_damage_mul[ii] = (damage_mul > 0.0f) ? damage_mul : 1.0f;
  const float mul = (speed_mul > 0.0f) ? speed_mul : 1.0f;
  const float new_vx = -batch->state.item_vel_x[ii] * mul;
  const float new_vy = -batch->state.item_vel_y[ii] * mul;
  batch->state.item_vel_x[ii] = new_vx;
  batch->state.item_vel_y[ii] = new_vy;
  batch->state.item_direction[ii] = (new_vx >= 0.0f) ? 1.0f : -1.0f;
}

static void laser_spawn_from_fighter(MslBatch* batch, int bi, int owner, const MslLaserParams* lp,
                                     uint8_t spawn_state, uint8_t use_velocity_override,
                                     float override_vx, float override_vy,
                                     uint8_t apply_spawn_motion_step,
                                     uint8_t throw_lw_late_pulse_transn_y) {
  if (batch == NULL || lp == NULL) {
    return;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);

  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t char_id = batch->state.char_id[o_idx];
  const uint32_t anim_u32 = batch->state.animation_index[o_idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float anim_frame_f32 = items_cur_anim_frame_f32(batch, o_idx);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  const MslCharParams* chp = msl_char_params(char_id);

  // Spawn point: lb_8000B1CC(bone_joint, offset, out) (decomp), approximated with pose matrices.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  //
  // IMPORTANT (bone index domain):
  // - Decomp uses ftParts_GetBoneIndex(fp, FtPart_RThumbNb) to pick an index into fp->parts[].
  //   refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
  // - Our SSANIM01 pose tables are keyed by these fp->parts[] indices.
  //
  // Source of truth:
  // - ISO-extracted character attr key `laser_spawn_joint_part_id`, derived from:
  //   `_iso/PlCo.dat` ftPartsTable[ftkind].part_to_joint[FtPart_RThumbNb].
  //   tools/extraction/extract_character_attrs.py::_extract_ftparts_rthumb_joint_index
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  uint16_t spawn_part_id = lp->spawn_bone_part_id;
  if (chp != NULL && chp->laser_spawn_joint_part_id != 0u) {
    spawn_part_id = chp->laser_spawn_joint_part_id;
  }
  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, spawn_part_id, m) != 0) {
    return;
  }
  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  msl_mtx34_mul_point(m, lp->spawn_off_xyz, &lx, &ly, &lz);

  const float facing_dir = batch->state.facing[o_idx] ? 1.0f : -1.0f;

  // Decomp: fighter facing is applied as a root Y rotation by +/-90°, which mixes X/Z.
  // For blaster shot spawn, local Z contributes meaningfully to stage X, so apply the same
  // decomp-shaped rotation here.
  // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
  const float rx = facing_dir * lz;
  const float rz = -facing_dir * lx;
  lx = rx;
  lz = rz;

  // Decomp: runtime joint matrices include ftCommon_GetModelScale(fp) (co_attrs.model_scaling) and
  // fp->x34_scale.y. Slippi seeds fp->x34_scale.y into fighter_scale_y; apply model_scaling here.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800869D4 (ftCommon_GetModelScale)
  const float scale_y = batch->state.fighter_scale_y[o_idx];
  float model_scaling = 1.0f;
  if (chp != NULL && chp->model_scaling > 0.0f) {
    model_scaling = chp->model_scaling;
  }
  const float model_scale = scale_y * model_scaling;
  lx *= model_scale;
  ly *= model_scale;
  (void)lz;  // 2.5D: keep stage Z at 0.0f

  float pos_x = batch->state.pos_x[o_idx] + lx;
  float pos_y = batch->state.pos_y[o_idx] + ly;
  if (throw_lw_late_pulse_transn_y) {
    // ThrowLw later-pulse persistent shot subset:
    // - SSANIM01 baked joint matrices strip TransN/root translation into the v3 tail.
    // - The later ThrowLw pulses (after the first non-persistent pulse) keep a live carried state1
    //   laser article in vanilla, so restore the root Y translation for this persistent subset.
    // - Keep the first pulse on the existing non-persistent owner path.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C6CC}
    // data/anims/{fox,falco}.bin SSANIM01 v3 TransN tail, read by anim_pose_get_transn()
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
    float transn_xyz[3] = {0.0f, 0.0f, 0.0f};
    if (anim_pose_get_transn(char_id, msid, frame, transn_xyz) == 0) {
      pos_y += transn_xyz[1];
    }
  }
  float ang = lp->blaster_angle;
  if (action_is_blaster_throw(batch->state.action_id[o_idx])) {
    // Throw-side launch angle is owned by ftFx_Throw_Anim for Throw{B,Hi,Lw}:
    //   atan2f(FtGetHoldJoint.y - ItGetHoldJoint.y,
    //          FtGetHoldJoint.x - ItGetHoldJoint.x)
    // rather than the SpecialN constant blaster angle.
    // FtGetHoldJoint uses the extracted `laser_spawn_joint_part_id` + `lp->spawn_off_xyz`;
    // ItGetHoldJoint uses the same RThumbNb joint with the decomp-local offset below.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    //   ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,ftFx_Throw_Anim}
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    const float it_hold_off_xyz[3] = {
        0.0f,
        1.2325000762939453f,
        0.013600001111626625f,
    };
    float ilx = 0.0f, ily = 0.0f, ilz = 0.0f;
    msl_mtx34_mul_point(m, it_hold_off_xyz, &ilx, &ily, &ilz);
    const float irx = facing_dir * ilz;
    const float irz = -facing_dir * ilx;
    ilx = irx;
    ilz = irz;
    (void)ilz;
    ilx *= model_scale;
    ily *= model_scale;
    const float it_hold_x = batch->state.pos_x[o_idx] + ilx;
    float it_hold_y = batch->state.pos_y[o_idx] + ily;
    if (throw_lw_late_pulse_transn_y) {
      float transn_xyz[3] = {0.0f, 0.0f, 0.0f};
      if (anim_pose_get_transn(char_id, msid, frame, transn_xyz) == 0) {
        it_hold_y += transn_xyz[1];
      }
    }
    ang = atan2f(pos_y - it_hold_y, pos_x - it_hold_x);
  } else {
    // Launch angle: if facing left, use (pi - base_angle).
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
    if (facing_dir < 0.0f) {
      ang = MSL_PI_F - ang;
    }
  }
  const float spd = lp->blaster_speed;
  float vx = spd * cosf(ang);
  float vy = spd * sinf(ang);
  if (use_velocity_override) {
    // Throw-side shot vector ownership (decomp-backed + seed bridge):
    // - ftFx_Throw_Anim launches throw shots from `atan2f(FtHoldJoint - ItHoldJoint)`, not the
    //   SpecialN constant blaster angle.
    // - Slippi seed does not expose the gun hold-joint pose/cmd cursor directly; use the latest
    //   seeded throw-side shot velocity as a narrow deterministic proxy and normalize to authored
    //   blaster speed.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6A4,it_8029C6CC}
    const float mag2 = (override_vx * override_vx) + (override_vy * override_vy);
    if (mag2 > 1e-8f) {
      const float inv_mag = 1.0f / sqrtf(mag2);
      vx = spd * (override_vx * inv_mag);
      vy = spd * (override_vy * inv_mag);
    }
  }
  if (apply_spawn_motion_step) {
    // Throw-side intra-frame order bridge:
    // - Throw shots are emitted by ftFx_Throw_Anim and then consume item motion callbacks in-frame.
    // - For reseeded one-step bridge rows where throw pulse ownership is seed-driven, apply one
    //   immediate motion tick to align spawned projectile t+1 placement.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Phys
    pos_x += vx;
    pos_y += vy;
  }
  const float dir = (vx >= 0.0f) ? 1.0f : -1.0f;

  batch->state.item_exists[ii] = 1;
  // Decomp (itfoxlaser.c):
  // - it_8029C6A4 spawns with msid=0
  // - it_8029C6CC spawns with msid=1 (used by ftFx_Throw_Anim for Throw{B,Hi,Lw})
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6A4,it_8029C6CC}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  batch->state.item_state[ii] = spawn_state;
  batch->state.item_type[ii] = lp->shot_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  // Staling identity: items copy the owner's fighter-side (attack_id, attack_instance) at spawn:
  // `item->xD88_attackID = fighter->x2068_attackID; item->xD8C_attack_instance = fighter->x206C_attack_instance;`
  // refs/melee/src/melee/it/it_2725.c::it_8027B070
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  // Slippi item.instance_id is item->xDA8_short. On spawn with a fighter parent, xDA8_short copies
  // fighter->x2088 (fighter instance_id) in the generic item spawn path.
  // refs/melee/src/melee/it/it_2725.c::it_8027B070
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = dir;
  batch->state.item_vel_x[ii] = vx;
  batch->state.item_vel_y[ii] = vy;
  batch->state.item_pos_x[ii] = pos_x;
  batch->state.item_pos_y[ii] = pos_y;
  batch->state.item_timer[ii] = (float)lp->lifetime_frames;
}

static void illusion_items_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    const uint16_t type = batch->state.item_type[ii];
    if (!item_type_is_spacie_illusion(type)) {
      continue;
    }
    if (batch->state.item_hitlag[ii] > 0u) {
      batch->state.item_hitlag[ii]--;
      continue;
    }

    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= num_players) {
      continue;
    }
    const size_t o_idx = msl_idx_player(bi, owner);
    const MslCharParams* chp = msl_char_params(batch->state.char_id[o_idx]);
    const uint8_t owner_motion_active = illusion_owner_motion_is_active(batch, o_idx);
    if (!illusion_item_anim_step(batch, ii, chp, owner_motion_active)) {
      continue;
    }
    if (batch->state.item_state[ii] >= 2u) {
      continue;
    }
    MslIllusionItemHitParams hp = {0};
    if (!illusion_item_hit_params_from_state(chp, batch->state.item_state[ii], &hp)) {
      continue;
    }

    // Decomp owner lane for Phys callback:
    // - state0/state1 copy item->pos from owner ghostEffectPos[1].
    // - ghostEffectPos[1] is advanced by ftFox_SpecialS_SetPhys from the prior frame's owner
    //   world position during SpecialS/SpecialAirS/SpecialSEnd/SpecialAirSEnd Phys.
    // refs/melee/src/melee/it/items/itfoxillusion.c::{
    //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_CopyGhostPosIndexed,ftFox_SpecialS_SetPhys,
    //   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys
    // }
    //
    // Runtime bridge:
    // - consume the seeded post-frame `ghostEffectPos[1]` lane for the current step,
    // - keep the paired `ghostEffectPos[0]` lane in state so rollout can advance the ring as
    //   `ghost1 = ghost0; ghost0 = cur_pos` instead of clobbering `ghost1` with current position.
    // tools/slippi/make_dataset_from_slp.py::derive_illusion_ghost_pos01
    //
    // Known remaining Side-B/Illusion decomp lanes not yet modeled here:
    // - ghostEffectPos[2]/ghostEffectPos[3]
    // - blendFrames[0..3]
    // - ghostGObj
    // - fp->x2222_b2 side effects
    // Current judgment is that those owners are accessory/ghost-visual oriented rather than part
    // of the gameplay-critical article position/hit flow, but they remain the next decomp surface
    // to port if a late ghost-display/state2 Side-B bug shows up.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFox_SpecialS_SetPhys,ftFox_SpecialS_SetVars,ftFox_SpecialSEnd_SetVars}
    // refs/melee/src/melee/it/items/itfoxillusion.c::{
    //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys,itFoxillusion_UnkMotion2_Phys}
    float base_x0 = batch->state.item_pos_x[ii];
    float base_y0 = batch->state.item_pos_y[ii];
    if (action_is_illusion_setphys(batch->state.action_id[o_idx])) {
      base_x0 = batch->state.illusion_ghost_pos1_x[o_idx];
      base_y0 = batch->state.illusion_ghost_pos1_y[o_idx];
      batch->state.item_pos_x[ii] = base_x0;
      batch->state.item_pos_y[ii] = base_y0;
    }
    const float base_x1 = base_x0;
    const float base_y1 = base_y0;
    const float x0 = base_x0;
    const float y0 = base_y0 + hp.hitbox_y_offset;
    const float x1 = base_x1;
    const float y1 = base_y1 + hp.hitbox_y_offset;

    uint8_t consumed_item = 0u;
    for (int def = 0; def < num_players; def++) {
      if (def == owner) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, def);
      // Hitlag gating: item collision acceptance is frozen while either participant is in hitlag.
      // Decomp ordering applies hitlag before collision callbacks in the per-frame fighter/item loop.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/it/item.c::Item_80268F7C
      if (batch->state.hitlag[o_idx] != 0u || batch->state.hitlag[d_idx] != 0u) {
        continue;
      }
      const uint16_t def_iid = batch->state.instance_id[d_idx];
      if (!hitlist_allows_item_fighter(batch, bi, it, def, def_iid)) {
        continue;
      }

      // Shield precedence mirrors fighter/item collision ownership: resolve shield overlap before
      // BODY intake.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
      const float shr = batch->state.shield_radius[d_idx];
      if (shr > 0.0f) {
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }
        // Decomp shield overlap helper consumes ShieldDesc radius with the shield bubble.
        // In GALE01 ShieldDesc radius is 1.0f and scales by fighter scale.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
        float shield_desc_world_r = 1.0f;
        if (batch->state.fighter_scale_y[d_idx] > 0.0f) {
          shield_desc_world_r *= batch->state.fighter_scale_y[d_idx];
        }
        if (item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x1, y1, 0.0f, hp.radius, shx, shy,
                                                   shz, shr + shield_desc_world_r)) {
          const float dmg = item_reflected_damage_lane(batch, ii, hp.damage);
          int8_t shield_damage = hp.shield_damage;
          // Data extraction uses -128 as the unset sentinel for shield-damage delta in this lane.
          if (shield_damage == (int8_t)MSL_ILLUSION_SHIELD_DAMAGE_UNSET) {
            shield_damage = 0;
          }
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], dmg, shield_damage,
                                       hp.element, batch->state.item_pos_x[ii]);
          // Generic item hitlag owner:
          // - item collision processing raises item->xCBC_hitlagFrames after a successful item
          //   shield/body contact, and Item_802697D4 skips item Phys/movement while the item
          //   remains in hitlag (`xDC8_word.flags.x9 != 0`).
          // - Illusion/Phantasm articles persist through shield hits, so freeze the article at
          //   the shield contact point for the defender hitlag window instead of continuing to
          //   consume ghostEffectPos[1].
          // refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          if (batch->state.hitlag[d_idx] > batch->state.item_hitlag[ii]) {
            batch->state.item_hitlag[ii] = batch->state.hitlag[d_idx];
          }
          const uint16_t def_iid_post = batch->state.instance_id[d_idx];
          hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                        (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          break;
        }
      }

      if (batch->state.hurtbox_state[d_idx] != 0u) {
        continue;
      }

      uint8_t hit_hurt_height = 0u;
      uint8_t hit = 0u;
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      for (uint8_t ci = 0; ci < cap_n; ci++) {
        if (item_swept_sphere_capsule_intersects(batch, bi, def, x0, y0, x1, y1, hp.radius, (int)ci,
                                                 &hit_hurt_height)) {
          hit = 1u;
          break;
        }
      }
      if (!hit) {
        continue;
      }

      const float dmg = item_reflected_damage_lane(batch, ii, hp.damage);
      const MslItemHitResult res = combat_apply_item_hit(
          batch, bi, owner, def, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
          batch->state.item_type[ii], batch->state.item_state[ii], dmg, hp.angle, hp.kbg, hp.wsk,
          hp.bkb, hit_hurt_height, hp.element);
      if (res == MSL_ITEM_HIT_NONE) {
        continue;
      }
      if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
        item_slot_clear(batch, ii);
        consumed_item = 1u;
        break;
      }
      if (res == MSL_ITEM_HIT_APPLIED_DONT_CONSUME) {
        // Generic item hitlag owner:
        // - successful item BODY contact raises item->xCBC_hitlagFrames, and Item_802697D4 then
        //   skips item Phys/movement while the item remains in hitlag (`xDC8_word.flags.x9 != 0`).
        // - Illusion/Phantasm body hits persist (see combat_apply_item_hit), so freeze the article
        //   at the body-contact point for the defender hitlag window instead of immediately
        //   continuing to consume ghostEffectPos[1].
        // refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
        // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
        if (batch->state.hitlag[d_idx] > batch->state.item_hitlag[ii]) {
          batch->state.item_hitlag[ii] = batch->state.hitlag[d_idx];
        }
      }
      const uint16_t def_iid_post = batch->state.instance_id[d_idx];
      hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                    (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      break;
    }

    if (consumed_item) {
      continue;
    }
  }
}

static void lasers_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  const uint32_t stage_id = batch->state.stage_id[bi];
  MslStageBounds blast_bounds = {0};
  const uint8_t has_blast_bounds = stage_collision_get_blast_bounds_world(stage_id, &blast_bounds);

  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    const uint16_t type = batch->state.item_type[ii];
    const MslLaserParams* lp = laser_params_for_item_type(type);
    if (lp == NULL) {
      continue;
    }
    // Scope guard: all collision logic in this function is laser-only.
    // `laser_params_for_item_type` returns NULL for non-MSLLASR1-backed item kinds, so the swept
    // overlap path below cannot affect bombs/turnips/etc.
    // Source: data/items/lasers.bin (MSLLASR1), loaded by laser_params_for_item_type().

    // Item msid/state is recorded by Slippi as u8 from Item+0x24 (enum_t msid).
    // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    // refs/melee/src/melee/it/types.h::Item (msid at +0x24)
    //
    // Blaster shots (itfoxlaser.c) can be spawned with msid 0 or 1:
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6A4 and ::it_8029C6CC
    const uint8_t laser_state = (batch->state.item_state[ii] != 0) ? 1u : 0u;
    item_apply_pending_powershield_reflect_speed(batch, ii);

    // Motion: item->pos += item->vel (generic add in Item_802697D4), and lifetime counts down.
    // Decomp refs:
    // - itfoxlaser.c::itFoxlaser_UnkMotion1_Anim (computes item->x40_vel from speed/angle)
    // - itfoxlaser.c::it_8029C504 (it_80275158 sets lifetime)
    const float x0 = batch->state.item_pos_x[ii];
    const float y0 = batch->state.item_pos_y[ii];
    const float x = x0 + batch->state.item_vel_x[ii];
    const float y = y0 + batch->state.item_vel_y[ii];
    batch->state.item_pos_x[ii] = x;
    batch->state.item_pos_y[ii] = y;

    // Blast-zone cull after motion integration.
    //
    // Decomp shape:
    // - Item_802697D4 integrates position, then calls Item_802696CC (when cull flags are enabled).
    // - Item_802696CC destroys items that cross left/right/bottom blast boundaries.
    // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
    //
    // Sim scope: laser articles are always culled by left/right/bottom blast bounds in-suite.
    // Top-bound cull in decomp uses a large constant gate (10000.0f) under a separate flag path and
    // is intentionally left to lifetime/collision ownership until a full item-flag model is seeded.
    if (has_blast_bounds &&
        (x > blast_bounds.right || x < blast_bounds.left || y < blast_bounds.bottom)) {
      item_slot_clear(batch, ii);
      continue;
    }

    float t = batch->state.item_timer[ii];
    t -= 1.0f;
    batch->state.item_timer[ii] = t;
    if (!(t > 0.0f)) {
      item_slot_clear(batch, ii);
      continue;
    }

    // Decomp: itFoxlaser_UnkMotion1_Coll calls it_8029C4D4 (stage collision) and, on hit, sets
    // lifetime to 1 and restores pre-collision position.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Coll
    if (stage_collision_item_line_hits_floor(stage_id, x0, y0, x, y)) {
      if (t > 1.0f) {
        batch->state.item_timer[ii] = 1.0f;
      }
      batch->state.item_pos_x[ii] = x0;
      batch->state.item_pos_y[ii] = y0;
      continue;
    }

    // Collision: laser hitbox script defines multiple hitboxes along the beam axis. Approximate as
    // multiple spheres sampled along the projectile's velocity direction using hitbox_offsets_x.
    //
    // Decomp: the laser model is rotated to match its velocity direction (rotY depends on
    // facing_dir=sign(vel.x), rotX depends on atan2(vel.y, vel_x)), and scaleZ ramps over time.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    //
    // Source of offsets: `data/items/lasers.bin` hitbox_offsets_x[] (MSLLASR1 v2), extracted
    // from the laser article state script in Pl*.dat by tools/extraction/extract_lasers.py.
    // docs/DATA_CONTRACT.md documents the binary layout and decomp pointers.
    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= num_players) {
      continue;
    }
    const size_t o_idx = msl_idx_player(bi, owner);

    // Beam axis in world space. Use velocity direction so angled shots do not erroneously collide
    // as if they were perfectly horizontal.
    const float vx = batch->state.item_vel_x[ii];
    const float vy = batch->state.item_vel_y[ii];
    float ux = 0.0f;
    float uy = 0.0f;
    {
      const float sp2 = vx * vx + vy * vy;
      if (sp2 > 0.0f) {
        const float inv_sp = 1.0f / sqrtf(sp2);
        ux = vx * inv_sp;
        uy = vy * inv_sp;
      } else {
        // Degenerate safety fallback; lasers should always have nonzero speed in normal play.
        ux = (batch->state.item_direction[ii] >= 0.0f) ? 1.0f : -1.0f;
        uy = 0.0f;
      }
    }
    const float sr = (laser_state == 0u) ? lp->size : lp->state1_size;

    // Beam length visual scaling: fox/falco blaster shots ramp model scaleZ over time.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    float laser_scale_z = 1.0f;
    float laser_age_frames = 0.0f;
    {
      const size_t o2_idx = msl_idx_player(bi, owner);
      const uint8_t ocid = batch->state.char_id[o2_idx];
      const MslCharParams* chp = msl_char_params(ocid);
      const float cap = (chp && chp->laser_scale_max > 0.0f) ? chp->laser_scale_max : 1.0f;

      const float speed = sqrtf(vx * vx + vy * vy);
      const float lifetime = (float)lp->lifetime_frames;
      float age = lifetime - t;
      if (age < 0.0f) {
        age = 0.0f;
      }
      laser_age_frames = age;

      float s = (age * speed) /
                11.25f;  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
      if (s > cap) {
        s = cap;
      }
      // Decomp clamps very small scale to 1e-3 (avoids degenerates).
      if (s < 1e-5f) {  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
        s = 1e-3f;
      }
      laser_scale_z = s;
    }
    (void)laser_scale_z;

    for (int def = 0; def < num_players; def++) {
      if (def == owner) {
        continue;
      }

      const size_t d_idx = msl_idx_player(bi, def);

      const uint16_t def_iid = batch->state.instance_id[d_idx];
      // Rehit suppression (HitCapsule victim rings): do not rehurt the same fighter repeatedly
      // while the item persists.
      if (!hitlist_allows_item_fighter(batch, bi, it, def, def_iid)) {
        continue;
      }

      // SHIELD precedence: if the item intersects the defender shield bubble, resolve as a shield
      // contact and do not take the BODY path.
      //
      // Authority:
      // - itfoxlaser.c Logic94 callbacks: it_2725_Logic94_HitShield / it_2725_Logic94_Reflected
      // - collision shield precedence: ftColl_80076CBC / lbColl_80007BCC
      // refs/melee/src/melee/it/items/itfoxlaser.c and refs/melee/src/melee/ft/ftcoll.c
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t guard_on_no_submotion_snapshot =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == 0xFFFFFFFFu)
              ? 1u
              : 0u;
      const MslCommonParams* common = msl_common_params();
      // Full-shield Dash guard-admission owner gate:
      // - Dash IASA only reaches ftCo_80091A4C / ftCo_80092450 through the late branch after
      //   `cur_anim_frame > x4C`; earlier Dash snapshots can still carry the untouched
      //   `start_shield_health` while no shield descriptor is yet live for item shield precedence.
      // - Restrict this owner gate to the fresh GuardReflect no-submotion admission context that
      //   teacher-forced reseed exposes on the repaired TBK rows; unrelated Dash rows must still
      //   resolve normal shield precedence.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_80092450}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      const uint16_t seed_prev_action = batch->state.seed_prev_action_id[d_idx];
      const uint8_t shield_fresh_dash_full_shield_snapshot =
          (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           // Dash IASA late-branch shield admission can enter GuardOn through
           // ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0 (analog hold) or GuardReflect through
           // ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50 (digital powershield path). Keep the
           // full-shield no-hit snapshot suppression aligned to both fresh entry owners.
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
           //   ftCo_80091A4C,ftCo_800923B4,ftCo_800924C0,ftCo_800939B4,ftCo_80093A50}
           (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
             batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u)) &&
           common != NULL && batch->state.shield_hp[d_idx] >= common->start_shield_health)
              ? 1u
              : 0u;
      // GuardOn_Anim -> GuardOn_IASA -> GuardReflect followup:
      // - GuardOn_Anim owns shield drain/x10 before the LR edge enters GuardReflect through
      //   ftCo_8009388C,
      // - ftCo_8009388C clears shield desc before installing the reflect descriptor,
      // - if the projectile then misses the reflect descriptor and hits BODY, regular shield
      //   precedence must not consume the contact first.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
      const uint8_t shield_dash_guardon_followup_guard_reflect_snapshot =
          (seed_prev_action == (uint16_t)MSL_ACT_DASH &&
           batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u)
              ? 1u
              : 0u;
      // Active GuardReflect window:
      // - ftCo_GuardReflect_Anim ticks x14 before chaining GuardOn_Anim; while the seeded frozen
      //   snapshot still has more than one x14 tick left, the reflect-window owner has not yet
      //   handed the row to the regular shield-hit / GuardSetOff path.
      // - When x14 reaches the final visible tick, the next row can enter GuardSetOff through the
      //   normal shield-hit owner.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
      const uint8_t guard_reflect_active_window_no_submotion_snapshot =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] > 0u)
              ? 1u
              : 0u;
      const uint16_t prev_action = batch->state.seed_prev_action_id[d_idx];
      const uint8_t guard_on_entry_from_landing =
          (guard_on_no_submotion_snapshot && (prev_action == (uint16_t)MSL_ACT_LANDING)) ? 1u : 0u;
      if (shr > 0.0f && !guard_on_entry_from_landing && !shield_fresh_dash_full_shield_snapshot &&
          !shield_dash_guardon_followup_guard_reflect_snapshot) {
        // Use derived shield bubble center from shields_refresh() (same geometry used by the
        // fighter-vs-fighter combat pass).
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        (void)item_try_guard_fresh_shield_center(batch, d_idx, &shx, &shy, &shz);
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }

        uint8_t shield_hit = 0;
        uint8_t shield_bounce_contact_found = 0;
        float shield_bounce_best_vy = -INFINITY;
        float shield_hit_contact_x = x;
        float shield_hit_contact_y = y;
        float shield_bounce_contact_x = x;
        float shield_bounce_contact_y = y;
        // Decomp call-chain anchors:
        // - itFoxlaser_UnkMotion1_Phys snapshots pre-move position (`foxlaser.pos = item->pos`).
        // - it_8029C4D4 runs collision using (prev_pos, cur_pos) and dispatches the hit callback.
        // - it_2725_Logic94_HitShield is the laser shield-hit resolution callback.
        // refs/melee/src/melee/it/items/itfoxlaser.c::{
        //   itFoxlaser_UnkMotion1_Phys,it_8029C4D4,it_2725_Logic94_HitShield}
        const uint8_t defender_guard_reflect_no_submotion_snapshot =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.action_frame[d_idx] < 0 &&
             batch->state.animation_index[d_idx] == 0xFFFFFFFFu &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u)
                ? 1u
                : 0u;
        // Keep the early geometry lane ownership-consistent with the later GuardSetOff owner gate by
        // sharing the same collision-time powershield-active predicate and stale-x18 suppression
        // before we narrow the late locomotion snapshot.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
        uint8_t guard_reflect_geom_can_powershield_reflect =
            combat_is_powershield_active_idx(batch, d_idx) ? 1u : 0u;
        if (guard_reflect_geom_can_powershield_reflect &&
            defender_guard_reflect_no_submotion_snapshot &&
            batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
            batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u) {
          guard_reflect_geom_can_powershield_reflect = 0u;
        }
        const uint8_t defender_guard_reflect_late_locomotion_snapshot_geom =
            (guard_reflect_geom_can_powershield_reflect &&
             defender_guard_reflect_no_submotion_snapshot &&
             batch->state.action_frame[d_idx] == -1 &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                ? 1u
                : 0u;
        float bounce_shx = shx;
        float bounce_shy = shy;
        if (defender_guard_reflect_late_locomotion_snapshot_geom && laser_state == 0u) {
          // Late locomotion->GuardReflect shield-bounce keepalive:
          // - overlap still resolves against the live shield bubble,
          // - but the first frozen state0 snapshot mirrors the shot against the grounded fighter
          //   origin plus shield radius before the full tilted shield bubble settles.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80091A4C,ftCo_800939B4,ftCo_GuardReflect_Anim}
          // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C4D4,itFoxLaser_Logic94_ShieldBounced}
          bounce_shx = batch->state.pos_x[d_idx];
          bounce_shy = batch->state.pos_y[d_idx] + shr;
        }
        const uint8_t defender_guard_hold_no_submotion_snapshot =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
             batch->state.action_frame[d_idx] < 0 &&
             batch->state.animation_index[d_idx] == UINT32_MAX &&
             batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u)
                ? 1u
                : 0u;
        // Steady Guard no-submotion snapshot split:
        // - ftCo_GuardOn_Anim can already have settled into Guard through ftCo_800928CC before the
        //   replay-visible post-frame, leaving a frozen Guard snapshot (`af=-1`, `anim=-1`) whose
        //   shield owner is already the settled Guard hold for this step.
        // - Keep laser shield acceptance on that steady Guard subset on the settled point sample
        //   rather than inventing a new same-frame sweep across the full prev->cur segment.
        // - This patch intentionally stays scoped to the steady Guard subset; other GuardOn /
        //   fresh-entry shield-contact owners remain separate replay-locked work.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_GuardOn_Anim,ftCo_800928CC,ftCo_Guard_IASA}
        // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
        const float shield_probe_x = defender_guard_hold_no_submotion_snapshot ? x0 : x;
        const float shield_probe_y = defender_guard_hold_no_submotion_snapshot ? y0 : y;

        uint8_t off_n =
            (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
        if (defender_guard_hold_no_submotion_snapshot) {
          off_n = 0u;
        }
        // Decomp consumes one shared scaleZ transform chain for laser collision spaces
        // (shield/body/reflect) via item collision callbacks. Keep the narrowed shield cap off for
        // steady GuardReflect no-submotion snapshots, but preserve the identity cap for fresh
        // late-locomotion state0 shots so the first shield-hit sample can still reach the
        // shield-bounce keepalive normal.
        // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
        // refs/melee/src/melee/it/itcoll.c::it_8027137C
        const uint8_t shield_cap_enabled =
            (defender_guard_reflect_no_submotion_snapshot &&
             !(defender_guard_reflect_late_locomotion_snapshot_geom && laser_state == 0u))
                ? 0u
                : 1u;
        const float laser_offset_scale =
            laser_collision_offset_scale(lp, laser_state, laser_scale_z,
                                         MSL_LASER_COLLISION_SPACE_SHIELD, 0u, shield_cap_enabled);
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
          const float off_x =
              (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
          const float s = off_x * laser_offset_scale;
          const float sx0 = x0 + (ux * s);
          const float sy0 = y0 + (uy * s);
          const float sx = shield_probe_x + (ux * s);
          const float sy = shield_probe_y + (uy * s);
          uint8_t this_hit = 0u;
          // Decomp (GALE01): shield overlap uses 3D collision (z is not ignored), and item
          // collision callbacks consume prev->cur segment ownership (`it_8029C4D4`).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (lbColl_80007BCC(..., cur_pos.z))
          // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
          if (item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx, sy, 0.0f, sr, shx, shy,
                                                     shz, shr)) {
            this_hit = 1u;
          }
          if (!this_hit) {
            continue;
          }
          shield_hit = 1u;
          if (shield_hit_contact_x == x && shield_hit_contact_y == y) {
            shield_hit_contact_x = sx;
            shield_hit_contact_y = sy;
          }
          if (!shield_bounce_contact_found ||
              (defender_guard_reflect_late_locomotion_snapshot_geom && laser_state == 0u)) {
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity(vx, vy, bounce_shx, bounce_shy, sx, sy,
                                                 &trial_bounce_vx, &trial_bounce_vy) &&
                (!shield_bounce_contact_found ||
                 !(defender_guard_reflect_late_locomotion_snapshot_geom && laser_state == 0u) ||
                 trial_bounce_vy > shield_bounce_best_vy)) {
              shield_bounce_contact_found = 1u;
              shield_bounce_best_vy = trial_bounce_vy;
              shield_bounce_contact_x = sx;
              shield_bounce_contact_y = sy;
            }
          }
        }
        // If no scripted offsets exist, fall back to the projectile origin on the same prev->cur
        // owner segment used by it_8029C4D4.
        if (!shield_hit && off_n == 0) {
          shield_hit = item_swept_sphere_sphere_intersects_3d(
              x0, y0, 0.0f, shield_probe_x, shield_probe_y, 0.0f, sr, shx, shy, shz, shr);
          if (shield_hit) {
            shield_hit_contact_x = shield_probe_x;
            shield_hit_contact_y = shield_probe_y;
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity(vx, vy, bounce_shx, bounce_shy, shield_probe_x,
                                                 shield_probe_y, &trial_bounce_vx,
                                                 &trial_bounce_vy) &&
                (!shield_bounce_contact_found ||
                 !(defender_guard_reflect_late_locomotion_snapshot_geom && laser_state == 0u) ||
                 trial_bounce_vy > shield_bounce_best_vy)) {
              shield_bounce_contact_found = 1u;
              shield_bounce_best_vy = trial_bounce_vy;
              shield_bounce_contact_x = shield_probe_x;
              shield_bounce_contact_y = shield_probe_y;
            }
          }
        }
        if (shield_hit) {
          // Powershield reflect: on a reflected hit, reverse the velocity vector and transfer owner.
          //
          // Decomp:
          // - Laser reflect visual logic: it_2725_Logic94_Reflected flips angle (+= pi) and sets facing_dir.
          //   refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
          // - GuardReflect sets fp->reflecting and powershield flags:
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C (ftColl_CreateReflectHit)
          //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_CreateReflectHit (sets 0x2218 reflect bit => 0x10)
          //   Note: fp+0x2218:3 in refs/melee/src/melee/ft/types.h uses the game's bit numbering (MSB-first),
          //   so this reflects as 0x10 (not 0x08).
          // - Slippi `state_flags` packs these fighter bytes:
          //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (lbz r3,0x2218 / 0x221C)
          //
          // Note: item attack_id / attack_instance remain spawn-latched for lasers in v1 (do not
          // transfer on reflect here).
          // In v1 we do not yet derive the full GuardReflect/powershield flag bytes (fp+0x2218 /
          // fp+0x221C) at combat-time. Gate reflect off the decomp-shaped GuardReflect action +
          // timer lanes owned by ftCo_80093A50/ftCo_80093BC0:
          // - x14: reflect window (`fp->reflecting` ownership window),
          // - x18: powershield-active window (x221C_b2 lifetime).
          // This keeps powershield reflect ownership coupled to the same timer gates used by
          // GuardReflect callback timing under teacher-forced reseed.
          //
          // Decomp anchors:
          // - GuardReflect reflect window timer: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
          //   (mv.co.guard.x14 = p_ftCommonData->x2A4; tick in ftCo_80093BC0).
          // - Powershield-active lane: fp->x221C_b2 at collision-time:
          //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          //
          // Keep item reflect gating aligned with combat collision-time powershield semantics by
          // sharing combat_is_powershield_active_idx().
          uint8_t can_powershield_reflect =
              combat_is_powershield_active_idx(batch, d_idx) ? 1u : 0u;
          // No-submotion GuardReflect stale-x18 lane:
          // - Reflect ownership is callback-gated by the active reflect window (mv.co.guard.x14).
          // - Some frozen snapshots carry x18 from the previous frame while x14 was already 0 in
          //   the seed snapshot; those rows should resolve through normal shield-hit ownership
          //   (GuardSetOff/hitlag), not item reflect.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          if (can_powershield_reflect && defender_guard_reflect_no_submotion_snapshot &&
              batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
              batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u) {
            can_powershield_reflect = 0u;
          }
          // One-frame-late locomotion->GuardReflect frozen snapshot:
          // - AttackDash / Wait-style shield admission can enter GuardReflect through ftCo_80091A4C ->
          //   ftCo_800939B4, and the following frozen action_frame==-1 snapshot can already be on the
          //   projectile shield-hit / GuardSetOff owner lane rather than the reflect-desc setup lane.
          // - Keep this restricted to frozen GuardReflect rows whose previous action was not already
          //   shield-owned; steady GuardReflect rows still use the timer-gated reflect path.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          const uint8_t defender_guard_reflect_late_locomotion_snapshot =
              (can_powershield_reflect && defender_guard_reflect_no_submotion_snapshot &&
               batch->state.action_frame[d_idx] == -1 &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                  ? 1u
                  : 0u;
          if (defender_guard_reflect_late_locomotion_snapshot) {
            can_powershield_reflect = 0u;
          }
          // Fresh locomotion->GuardReflect snapshot bridge:
          // - Grounded guard admission can enter GuardReflect directly from locomotion
          //   (`ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50`) before the first canonical
          //   GuardReflect callback tick seeds x14/x18 into replay-visible lanes.
          // - On these same-frame no-submotion snapshots, replay still resolves projectile shield
          //   contact through the normal shield-hit/GuardSetOff path rather than powershield
          //   reflect ownership.
          // - Keep this bridge restricted to fresh GuardReflect entries whose seed timers are both
          //   zero, whose previous action was not already shield-owned, and whose shield is already
          //   below the untouched x260 start-health lane. Full-shield Dash rows are handled by the
          //   shield-precedence bridge above.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_8009370C}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          const MslCommonParams* c = msl_common_params();
          const uint8_t defender_guard_reflect_fresh_locomotion_snapshot =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.animation_index[d_idx] == UINT32_MAX &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF && c != NULL &&
               batch->state.shield_hp[d_idx] < c->start_shield_health)
                  ? 1u
                  : 0u;
          if (can_powershield_reflect && defender_guard_reflect_fresh_locomotion_snapshot) {
            can_powershield_reflect = 0u;
          }
          if (can_powershield_reflect) {
            // Decomp reflect snapshot ownership:
            // - GuardReflect reflect setup path initializes ReflectDesc lanes.
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
            //   refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
            // - overlap writes owner/xDA8_short and reflect multipliers to item snapshot.
            //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
            // - item logic consumes snapshot in Item_80269F14; speed update may be deferred to that
            //   pass via item_apply_pending_powershield_reflect_speed().
            //   refs/melee/src/melee/it/item.c::Item_80269F14
            const float dmg_mul = (c != NULL && c->powershield_reflect_damage_mul > 0.0f)
                                      ? c->powershield_reflect_damage_mul
                                      : 1.0f;
            item_apply_powershield_reflect_snapshot(batch, ii, def, dmg_mul);
            // TODO(decomp/powershield-reflect-ownership-timing): transfer-frame xDA8_short
            // (Slippi item.instance_id) is intentionally left seed-latched in this lane until the
            // exact authoritative transfer ordering is extracted for non-regressive rollout parity.
            // TODO(decomp/powershield-reflect-speed-mul): validate whether powershield reflect uses
            // ftCommonData x2B0 directly (or identity) for these rows.
            break;
          }
          if (guard_reflect_active_window_no_submotion_snapshot) {
            // The active reflect window owns this frozen GuardReflect row, but the reflect geometry
            // above may reject the projectile. In that case do not fall through into regular
            // GuardSetOff shield-hit ownership until the final x14 handoff tick.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
            break;
          }

          float shield_bounce_vx = 0.0f;
          float shield_bounce_vy = 0.0f;
          // Spawn-frame keepalive gate:
          // - Laser spawn initializes `scale=0` in it_8029C504 and the same-frame motion callback
          //   performs the first scale ramp in itFoxlaser_UnkMotion1_Anim before collision.
          // - Replay-real shield-bounce keepalive rows in-suite are already-aged lasers; same-frame
          //   gun-spawn shield contacts still resolve through the destroy path until the
          //   authoritative `xDCE/xC54/xC58` bounce internals are promoted into seed/runtime state.
          // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
          // refs/melee/src/melee/it/item.c::Item_80269DC8
          const uint8_t can_shield_bounce =
              shield_bounce_contact_found &&
              // Late locomotion->GuardReflect frozen snapshots can already resolve projectile
              // contact through the regular shield-hit / GuardSetOff owner lane while the shot is
              // only one frame old. Allow the normal shield-bounce keepalive there instead of
              // forcing the generic spawn-frame destroy path.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
              //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim}
              // refs/melee/src/melee/it/items/itfoxlaser.c::{
              //   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_ShieldBounced}
              (laser_age_frames > 1.0f || defender_guard_reflect_late_locomotion_snapshot) &&
              laser_try_shield_bounce_velocity(vx, vy, bounce_shx, bounce_shy,
                                               shield_bounce_contact_x, shield_bounce_contact_y,
                                               &shield_bounce_vx, &shield_bounce_vy);
          // Regular shield hit: apply defender-side shield effects and despawn the laser.
          float dmg = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          dmg = item_reflected_damage_lane(batch, ii, dmg);
          const int8_t shd = (laser_state == 0u) ? lp->shield_damage : lp->state1_shield_damage;
          const uint8_t element = (laser_state == 0u) ? lp->element : lp->state1_element;
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], dmg, shd, element,
                                       batch->state.item_pos_x[ii]);
          // Rehit suppression latch for this item: insert the post-mutation victim identity so
          // teacher-forced reseed sees the same proxy at t+1.
          const uint16_t def_iid_post = batch->state.instance_id[d_idx];
          hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                        (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          if (can_shield_bounce) {
            batch->state.item_vel_x[ii] = shield_bounce_vx;
            batch->state.item_vel_y[ii] = shield_bounce_vy;
            batch->state.item_direction[ii] = (shield_bounce_vx >= 0.0f) ? 1.0f : -1.0f;
            break;
          }
          item_slot_clear(batch, ii);
          break;
        }
      }

      // Minimal eligibility: skip if hurtbox_state is nonzero (invincible/intangible/etc).
      // Slippi post-frame: `hurtbox_state` is seeded; movescript hit status can overwrite it.
      if (batch->state.hurtbox_state[d_idx] != 0) {
        continue;
      }

      // Special-move reflector bubble (Shine): if the item intersects the reflector bubble, reflect
      // it and do not take the BODY path.
      //
      // Decomp trail:
      // - ftColl_CreateReflectHit initializes fp->reflect_hit (bone/offset/size) and reflect multipliers:
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
      // - The item overlap path writes reflect snapshot fields (owner, damage/speed mul, xDA8_short source):
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
      // - Item_80269F14 applies reflect by transferring owner/xDA8_short (Slippi item.instance_id):
      //   refs/melee/src/melee/it/item.c::Item_80269F14
      const float rr = batch->state.reflector_radius[d_idx];
      if (rr > 0.0f) {
        const float rx = batch->state.reflector_x[d_idx];
        const float ry = batch->state.reflector_y[d_idx];
        const MslCharParams* rch = msl_char_params(batch->state.char_id[d_idx]);
        if (isfinite(rx) && isfinite(ry) && rch != NULL) {
          uint8_t reflect_hit = 0;
          const uint8_t off_n =
              (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
          for (uint8_t oi = 0;
               oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !reflect_hit; oi++) {
            const float off_x =
                (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
            const float s =
                off_x * laser_collision_offset_scale(lp, laser_state, laser_scale_z,
                                                     MSL_LASER_COLLISION_SPACE_REFLECT, 0u, 1u);
            const float sx = x + (ux * s);
            const float sy = y + (uy * s);
            if (item_sphere_sphere_intersects_2d(sx, sy, sr, rx, ry, rr)) {
              reflect_hit = 1;
            }
          }
          // If no scripted offsets exist, fall back to the projectile origin.
          if (!reflect_hit && off_n == 0) {
            reflect_hit = item_sphere_sphere_intersects_2d(x, y, sr, rx, ry, rr);
          }

          if (reflect_hit) {
            // Decomp reflect snapshot ownership:
            // - ftColl_CreateReflectHit stores ReflectDesc.damage_mul / speed_mul.
            // - ftColl_80077464 writes both multipliers to item reflect snapshot (`item->xC6C` et al).
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
            item_apply_reflect_transfer(batch, ii, d_idx, def, rch->reflector_damage_mul,
                                        rch->reflector_speed_mul);
            break;
          }
        }
      }

      uint8_t hit_hurt_height = 0;
      uint8_t hit = 0;
      // Deterministic order: offsets (script order) then capsule slots.
      const uint8_t off_n =
          (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      const uint8_t body_shield_adjacent = (batch->state.shield_radius[d_idx] > 0.0f) ? 1u : 0u;
      const float laser_offset_scale = laser_collision_offset_scale(
          lp, laser_state, laser_scale_z, MSL_LASER_COLLISION_SPACE_BODY, body_shield_adjacent, 1u);
      // Laser BODY overlap parity:
      // - Decomp computes collision over projectile travel in-frame (prev_pos -> cur_pos), so a
      //   current-point-only probe can miss replay-causal same-frame hits.
      // - Current implementation keeps the precise swept probe on the airborne path only; grounded
      //   replay gaps are handled by the narrow miss-only LandingFallSpecial bridge below.
      // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
      // TODO(decomp/items-grounded-body-gating): mirror full grounded BODY hurt-status/collision
      // gating from it_80272460 + ftColl callbacks, then remove the narrow miss-only bridge below.
      // refs/melee/src/melee/it/itcoll.c::it_80272460
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
      const uint8_t use_swept_body = laser_grounded_body_uses_sweep(batch, d_idx, laser_age_frames);
      for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit; oi++) {
        const float off_x =
            (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
        const float s = off_x * laser_offset_scale;
        const float sx0 = x0 + (ux * s);
        const float sy0 = y0 + (uy * s);
        const float sx = x + (ux * s);
        const float sy = y + (uy * s);
        for (uint8_t ci = 0; ci < cap_n; ci++) {
          const float hx0 = use_swept_body ? sx0 : sx;
          const float hy0 = use_swept_body ? sy0 : sy;
          if (item_swept_sphere_capsule_intersects(batch, bi, def, hx0, hy0, sx, sy, sr, (int)ci,
                                                   &hit_hurt_height)) {
            hit = 1;
            break;
          }
        }
      }
      // If no scripted offsets exist, fall back to the projectile origin.
      if (!hit && cap_n > 0 && off_n == 0) {
        for (uint8_t ci = 0; ci < cap_n; ci++) {
          const float hx0 = use_swept_body ? x0 : x;
          const float hy0 = use_swept_body ? y0 : y;
          if (item_swept_sphere_capsule_intersects(batch, bi, def, hx0, hy0, x, y, sr, (int)ci,
                                                   &hit_hurt_height)) {
            hit = 1;
            break;
          }
        }
      }
      if (!hit && laser_grounded_body_landing_fall_special_aabb_bridge(
                      batch, bi, def, x0, y0, x, y, sr, laser_age_frames, &hit_hurt_height)) {
        hit = 1;
      }
      if (!hit && laser_state != 0u &&
          batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          batch->state.throw_pulse_consumed[o_idx] == 0u &&
          batch->state.throw_pulse_crossed_prev_frame[o_idx] ==
              (uint8_t)MSL_THROWLW_PULSE_ATTACH_AF &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_THROWN_LW) {
        // ThrowLw attached-victim pulse bridge (miss-only):
        // - ThrowLw throw-side pulses are script-owned one-shots (23/25/28/31) consumed in
        //   ftFx_Throw_Anim; seed lane `throw_pulse_crossed_prev_frame` carries prior-step crossing.
        // - In attached ThrownLw contexts, missing same-step BODY overlap at the 25-frame pulse
        //   leaves replay-causal hitlag/state-flags deltas; bridge only when geometry probe missed.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
        hit = 1;
        hit_hurt_height = 1u;
      }
      if (!hit) {
        continue;
      }

      // Seed-bridge approximation for ThrowLw late blaster pulses:
      // - ftFx_Throw_Anim emits multiple throw_flags_b0 pulses during ThrowLw (data/moves
      //   set_throw_spawn_projectile @ 23/25/28/31) and spawns with it_8029C6CC (msid=1).
      // - In reseeded one-step frames deep in ThrowLw, replay refs can carry the spawned item at
      //   t+1 without a new same-frame BODY hit while the victim is still attached.
      // - Slippi does not expose the consumed per-pulse latch for this path, so suppress the
      //   late attached BODY re-hit in this narrow window.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
      int16_t last_throw_pulse_af = 0;
      const uint8_t has_last_throw_pulse = move_tables_throw_projectile_last_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &last_throw_pulse_af);
      if (laser_state != 0u && batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          has_last_throw_pulse &&
          items_cur_anim_frame_f32(batch, o_idx) >= ((float)last_throw_pulse_af + 1.0f) &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          msl_action_is_grabbed_victim(batch->state.action_id[d_idx])) {
        continue;
      }
      if (laser_state != 0u && batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          msl_action_is_grabbed_victim(batch->state.action_id[d_idx]) &&
          batch->state.hitlag_pre_timer[d_idx] != 0u) {
        // Seed-bridge discriminator for ThrowLw attached victim collisions:
        // - In decomp, thrown victims are attachment-driven (Thrown* Phys/Coll are empty), and
        //   throw-side projectile pulses are script-time one-shots consumed in ftFx_Throw_Anim.
        // - On reseeded mid-hitlag snapshots, replay rows can carry victim hitlag>0 at t while
        //   the consumed throw pulse latch is not exposed by Slippi; re-applying attached BODY
        //   contact in that window spuriously re-extends hitlag at t+1.
        // - Gate on seed-visible pre-hitlag only (hitlag_pre_timer) to keep the suppression narrow.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
        continue;
      }

      // BODY contact: attempt to apply an item hit.
      //
      // Laser lifetime policy (suite-stable):
      // - Shield hit => despawn (handled above; itfoxlaser Logic94 HitShield).
      // - Real BODY hit applied => despawn (matches in-game behavior; prevents item-slot drift).
      // - BODY hit suppressed because the defender is still an attached grabbed/thrown victim of the
      //   attacker => do not despawn (victim stays in Thrown*/Capture*; see combat_apply_item_hit()).
      //
      // Decomp-first reference for item-vs-fighter BODY intake: refs/melee/src/melee/it/itcoll.c::it_80272460.
      // Decomp-first references for BODY apply:
      // - Fighter_ProcessHit_8006D1EC (percent add, hitlag, hitstun, damage-state entry)
      // - ftColl_80076CBC (getEnvDmg pattern)
      // refs/melee/src/melee/ft/fighter.c and refs/melee/src/melee/ft/ftcoll.c
      float dmg = (laser_state == 0u) ? lp->damage : lp->state1_damage;
      dmg = item_reflected_damage_lane(batch, ii, dmg);
      const uint16_t angle = (laser_state == 0u) ? lp->angle : lp->state1_angle;
      const uint16_t kbg = (laser_state == 0u) ? lp->kbg : lp->state1_kbg;
      const uint16_t wsk = (laser_state == 0u) ? lp->wsk : lp->state1_wsk;
      const uint16_t bkb = (laser_state == 0u) ? lp->bkb : lp->state1_bkb;
      const uint8_t element = (laser_state == 0u) ? lp->element : lp->state1_element;
      const MslItemHitResult res =
          combat_apply_item_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                batch->state.item_attack_instance[ii],
                                batch->state.item_instance_id[ii], batch->state.item_type[ii],
                                laser_state, dmg, angle, kbg, wsk, bkb, hit_hurt_height, element);
      if (res == MSL_ITEM_HIT_NONE) {
        continue;
      }
      if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
        item_slot_clear(batch, ii);
        break;
      }
      // Rehit suppression latch for this item (runtime): insert the post-mutation victim identity.
      const uint16_t def_iid_post = batch->state.instance_id[d_idx];
      hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                    (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      break;
    }
  }
}

void items_update(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const int num_players = (int)batch->config.num_players;

    // Motion + collision/hit apply for Illusion/Phantasm ghost items.
    illusion_items_update_and_collide(batch, bi);

    // Motion + collision/hit apply for existing lasers.
    lasers_update_and_collide(batch, bi);

    // ThrowLw stale-latch carry trim (post-collision, context-narrow):
    // - Throw-side pulses are one-shot throw_flags_b0 events consumed in ftFx_Throw_Anim.
    // - On one-step reseed around the first ThrowLw projectile pulse crossing while victim is still
    //   attached in ThrownLw, the pulse can leave a stale carried state1 laser item at t+1 even when
    //   replay ref has no item.
    // - Keep spawn/collision ownership unchanged (to preserve hitlag parity), then clear only this
    //   stale carried state1 item after collision resolution in the same frame.
    // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
    for (int p = 0; p < num_players; p++) {
      const size_t o_idx = msl_idx_player(bi, p);
      if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_LW) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[o_idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL || lp->shot_itkind != (uint16_t)MSL_IT_KIND_FALCO_LASER_SHOT) {
        continue;
      }
      const float af_cur = items_cur_anim_frame_f32(batch, o_idx);
      const int32_t prev_fp =
          batch->state.anim_frame_fp_q16_16[o_idx] - batch->state.frame_speed_mul_fp_q16_16[o_idx];
      const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
      int16_t crossed_pulse_af = -1;
      if (!move_tables_throw_cmd1_active(cid, (uint16_t)MSL_ACT_THROW_LW, af_cur) ||
          !move_tables_throw_crossed_projectile_pulse_frame(cid, (uint16_t)MSL_ACT_THROW_LW,
                                                            af_prev, af_cur, &crossed_pulse_af)) {
        continue;
      }
      int16_t first_pulse_af = -1;
      if (!move_tables_throw_projectile_first_pulse_frame(cid, (uint16_t)MSL_ACT_THROW_LW,
                                                          &first_pulse_af) ||
          crossed_pulse_af != first_pulse_af) {
        continue;
      }
      uint8_t stale_context = 0u;
      for (int vp = 0; vp < num_players; vp++) {
        if (vp == p) {
          continue;
        }
        const size_t v_idx = msl_idx_player(bi, vp);
        if (batch->state.grab_owner_port[v_idx] == (uint8_t)p &&
            batch->state.action_id[v_idx] == (uint16_t)MSL_ACT_THROWN_LW &&
            batch->state.hitlag_pre_timer[v_idx] == 0u && batch->state.hitstun[v_idx] == 0u) {
          stale_context = 1u;
          break;
        }
      }
      if (!stale_context) {
        continue;
      }
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii]) {
          continue;
        }
        if (batch->state.item_type[ii] != lp->shot_itkind || batch->state.item_owner[ii] != p ||
            batch->state.item_state[ii] != (uint8_t)1u) {
          continue;
        }
        item_slot_clear(batch, ii);
      }
    }
    // Refresh the seeded `ghostEffectPos[0..1]` gameplay lanes for the next frame.
    // Decomp: ftFox_SpecialS_SetPhys advances the live ring as
    // `ghost1 = ghost0; ghost0 = cur_pos`.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetPhys
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (!action_is_illusion_setphys(batch->state.action_id[idx])) {
        continue;
      }
      batch->state.illusion_ghost_pos1_x[idx] = batch->state.illusion_ghost_pos0_x[idx];
      batch->state.illusion_ghost_pos1_y[idx] = batch->state.illusion_ghost_pos0_y[idx];
      batch->state.illusion_ghost_pos0_x[idx] = batch->state.pos_x[idx];
      batch->state.illusion_ghost_pos0_y[idx] = batch->state.pos_y[idx];
    }

    // Keep item ordering stable for fixed-slot comparisons.
    items_sort(batch, bi);
  }
}

void items_update_post_combat(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const MslLaserParams* lp = laser_params_get(batch->state.char_id[idx]);
      if (lp == NULL || lp->gun_itkind == 0u) {
        continue;
      }

      const int gun_slot = items_find_gun_slot(batch, bi, p, lp->gun_itkind);
      if (gun_slot < 0) {
        continue;
      }

      const uint16_t action_id_u16 = batch->state.action_id[idx];
      const uint16_t prev_action_id_u16 = batch->state.prev_action_id[idx];
      const uint8_t prev_requires_gun =
          (blaster_gun_state_from_action_id(prev_action_id_u16) != 9u) ? 1u : 0u;
      const uint8_t cur_requires_gun =
          (blaster_gun_state_from_action_id(action_id_u16) != 9u) ? 1u : 0u;

      if (!prev_requires_gun || cur_requires_gun) {
        continue;
      }
      if (!items_action_is_damage_family(action_id_u16)) {
        continue;
      }
      if (batch->state.on_ground[idx] != 0u) {
        continue;
      }

      // Combat-owned blaster gun clear on same-frame damage exits:
      // - SpecialNEnd linger ownership is specific to non-combat exits where
      //   ftFx_SpecialNEnd_Anim clears the fighter pointer and item callback consumes it.
      // - On airborne Fighter_ProcessHit damage entry from SpecialAirN states, the fighter exits the
      //   blaster action family through damage callbacks (not SpecialNEnd), so this lane should not
      //   persist as a carried SpecialNEnd linger row.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
      //   ftFx_SpecialAirNStart_Phys,ftFx_SpecialAirNLoop_Phys,ftFx_SpecialAirNEnd_Phys}
      // refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      const size_t ii = msl_idx_item(bi, gun_slot);
      item_slot_clear(batch, ii);
    }
    items_sort(batch, bi);
  }
}

void items_spawn_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Ensure the blaster "gun" item (ItKind 74/75) exists and stays linked to its fighter.
  // This stabilizes item ordering/keys when lasers coexist with the gun.
  //
  // Decomp-first trail:
  // - Spawn: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter calls it_802AE8A8
  // - Item spawn/attach: refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8 (Item_8026AB54 attach)
  // - Per-frame: refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
  // - Slippi fields: refs/slippi-ssbm-asm/Recording/SendItemInfo.s (state=0x24, instance_id=0xDA8)
  //
  // Spawn lasers from fighter loop scripts (cmd_var[2] pulses).
  // Decomp: ftFx_SpecialNLoop_Anim / ftFx_SpecialAirNLoop_Anim check fp->cmd_vars[2] and spawn via
  // it_8029C6A4.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
  // Note: This sim uses a precomputed shoot-frame list from `data/items/lasers.bin` as a proxy for
  // cmd_vars[2] pulses.
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Stack-local per-step/per-batch-row scratch: reset once each row iteration.
    // This does not persist in SoA state across frames/reseed.
    uint8_t gun_spawned_this_frame[MSL_MAX_PLAYERS] = {0};
    float throw_seed_shot_vx[MSL_MAX_PLAYERS] = {0.0f};
    float throw_seed_shot_vy[MSL_MAX_PLAYERS] = {0.0f};
    uint8_t throw_seed_shot_valid[MSL_MAX_PLAYERS] = {0u};
    uint8_t throw_seed_shot_count[MSL_MAX_PLAYERS] = {0u};

    // Seed-visible throw-shot vector snapshot (pre-spawn):
    // - Throw-side shot direction in ftFx_Throw_Anim is gun-joint relative (`atan2f(sp50-sp44)`),
    //   which is not directly seed-exposed.
    // - Snapshot latest throw-side (state1) shot velocity per owner as a deterministic bridge for
    //   narrow throw-pulse reconstruction rows.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    for (int p = 0; p < num_players; p++) {
      const size_t p_idx = msl_idx_player(bi, p);
      const MslLaserParams* p_lp = laser_params_get(batch->state.char_id[p_idx]);
      if (p_lp == NULL || p_lp->shot_itkind == 0u) {
        continue;
      }
      float best_timer = -1.0f;
      for (int it = 0; it < MSL_MAX_ITEMS; it++) {
        const size_t ii = msl_idx_item(bi, it);
        if (!batch->state.item_exists[ii] || batch->state.item_owner[ii] != (int8_t)p ||
            batch->state.item_type[ii] != p_lp->shot_itkind ||
            batch->state.item_state[ii] != (uint8_t)1u) {
          continue;
        }
        if (throw_seed_shot_count[p] < 0xFFu) {
          throw_seed_shot_count[p]++;
        }
        const float vx = batch->state.item_vel_x[ii];
        const float vy = batch->state.item_vel_y[ii];
        if ((vx * vx) + (vy * vy) <= 1e-8f) {
          continue;
        }
        const float timer = batch->state.item_timer[ii];
        if (timer > best_timer) {
          best_timer = timer;
          throw_seed_shot_vx[p] = vx;
          throw_seed_shot_vy[p] = vy;
          throw_seed_shot_valid[p] = 1u;
        }
      }
    }

    // Spawn Illusion/Phantasm ghost article from side-special cmd_var[2] pulse.
    // Decomp ownership:
    // - ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim call ftFox_SpecialS_CreateGhostItem.
    // - ftFox_SpecialS_CreateGhostItem spawns via it_8029CEB4 when cmd_vars[2]==1.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id_u16 = batch->state.action_id[idx];
      const uint16_t prev_action_id_u16 = batch->state.prev_action_id[idx];
      // Hitlag-start exception for same-step main->end side-special ghost spawn ownership:
      // - Ghost spawn is driven from main anim callbacks (ftFx_SpecialS_Anim / AirS_Anim).
      // - Under one-step ordering, main->end transition can occur before this spawn pass; preserve
      //   the spawn bridge on end-entry even when `hitlag_started_frame` is set, but only when the
      //   previous action-frame already entered the cmd_var[2] pulse-crossing window.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim}
      const uint8_t illusion_end_entry_bridge =
          (batch->state.action_frame[idx] == 0 && batch->state.prev_action_frame[idx] >= 1 &&
           ((prev_action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S &&
             action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S_END) ||
            (prev_action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
             action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END)))
              ? 1u
              : 0u;
      if (batch->state.hitlag_started_frame[idx] != 0u && !illusion_end_entry_bridge) {
        continue;
      }
      illusion_spawn_from_fighter(batch, bi, p);
    }

    // Update gun items first so laser spawns can inherit the correct instance_id/spawn_id ordering.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL || lp->gun_itkind == 0) {
        continue;
      }
      gun_spawned_this_frame[p] = blaster_gun_update_from_fighter(batch, bi, p, lp);
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL) {
        continue;
      }
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        continue;
      }
      const uint16_t msid = (uint16_t)anim_u32;
      const float af = items_cur_anim_frame_f32(batch, idx);
      const uint16_t frame = msl_anim_frame_floor_u16(af);
      uint8_t should_shoot = laser_should_shoot_on_frame(lp, msid, frame);
      uint8_t shoot_spawn_state = 0u;
      uint8_t shoot_use_velocity_override = 0u;
      float shoot_override_vx = 0.0f;
      float shoot_override_vy = 0.0f;
      uint8_t shoot_apply_motion_step = 0u;
      uint8_t shoot_throw_lw_late_pulse_transn_y = 0u;
      // Throw-side blaster shots are driven by throw_flags_b0 pulses consumed in ftFx_Throw_Anim,
      // not by SpecialN loop cmd_vars[2].
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      const uint16_t action_id = batch->state.action_id[idx];
      const uint8_t is_blaster_throw = action_is_blaster_throw(action_id);
      if (is_blaster_throw) {
        // ftFx_Throw_Anim case 1 returns immediately when the gun pointer is NULL and must spawn,
        // so throw-side shots are only possible when a gun already exists before the shoot branch.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        if (gun_spawned_this_frame[p]) {
          continue;
        }
        if (items_find_gun_slot(batch, bi, p, lp->gun_itkind) < 0) {
          continue;
        }
      }
      if (!should_shoot && is_blaster_throw) {
        // Seed-bridge guard for one-shot throw_flags_b0 reconstruction:
        // - Throw projectile pulses are script-time one-shot flags (set by ftAction_80071974 and
        //   consumed by ftFx_Throw_Anim).
        // - On one-step reseed in the middle of a throw/hitlag exchange, the consumed-latch is not
        //   exposed by Slippi. Reconstructing pulses purely from anim-frame crossing can re-emit a
        //   pulse while the fighter was already in hitlag at frame start.
        // - Gate pulse reconstruction on "no pre-timer hitlag this frame" to avoid that stale-latch
        //   replay artifact.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        if (batch->state.hitlag_pre_timer[idx] != 0u) {
          continue;
        }
        // Seed-owned ThrowHi pulse reconstruction bridge:
        // - Seed derivation marks the one-step throw_flags_b0 pulse ownership window.
        // - For ongoing throw-damage contexts attributed to this thrower, emit the throw-side shot
        //   directly from seed ownership rather than relying only on anim-frame pulse crossing.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by / hitstun lanes)
        if (action_id == (uint16_t)MSL_ACT_THROW_HI &&
            batch->state.throw_pulse_consumed[idx] != 0u) {
          uint8_t ongoing_throwhi_context = 0u;
          uint8_t throwhi_override_allowed = 0u;
          for (int vp = 0; vp < num_players; vp++) {
            if (vp == p) {
              continue;
            }
            const size_t v_idx = msl_idx_player(bi, vp);
            if (batch->state.hitstun[v_idx] > 0u && batch->state.last_hit_by[v_idx] == (uint8_t)p) {
              ongoing_throwhi_context = 1u;
              if (batch->state.last_attack_landed[v_idx] != 0u) {
                throwhi_override_allowed = 1u;
              }
              break;
            }
          }
          if (ongoing_throwhi_context) {
            should_shoot = 1u;
            // Throw-side spawn path in ftFx_Throw_Anim uses it_8029C6CC (msid=1).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            shoot_spawn_state = 1u;
            // Keep seed velocity override scoped to contexts where replay seed also reports a
            // concrete landed-attack identity; broadened ongoing-hitstun bridge contexts without
            // last_attack_landed ownership still emit the pulse but keep native throw kinematics.
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_attack_landed lane)
            if (throw_seed_shot_valid[p] &&
                (throwhi_override_allowed || batch->state.facing[idx] != 0u)) {
              shoot_use_velocity_override = 1u;
              shoot_override_vx = throw_seed_shot_vx[p];
              shoot_override_vy = throw_seed_shot_vy[p];
            }
          }
        }
        if (!should_shoot) {
          const int32_t prev_fp =
              batch->state.anim_frame_fp_q16_16[idx] - batch->state.frame_speed_mul_fp_q16_16[idx];
          const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
          int16_t crossed_pulse_af = -1;
          if (move_tables_throw_cmd1_active(cid, action_id, af) &&
              move_tables_throw_crossed_projectile_pulse_frame(cid, action_id, af_prev, af,
                                                               &crossed_pulse_af)) {
            int16_t first_pulse_af = -1;
            const uint8_t has_first_pulse =
                move_tables_throw_projectile_first_pulse_frame(cid, action_id, &first_pulse_af);
            const uint16_t prev_frame_i = msl_anim_frame_floor_u16(af_prev);
            // Throw-side stale-latch suppressors (context-owned, non-record-keyed):
            // - Throw pulse flags are one-shot script events (`throw_flags_b0`) owned by the command
            //   timeline and consumed by ftFx_Throw_Anim.
            // - Under teacher-forced reseed, command cursor ownership is not seeded; in specific
            //   attached/ongoing-damage contexts, pure frame-crossing can replay a stale pulse that
            //   does not exist at t+1.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
            // data/moves/{fox,falco}.json set_throw_spawn_projectile pulse frames
            uint8_t stale_throw_pulse_context = 0u;
            int stale_throw_pulse_victim = -1;
            for (int vp = 0; vp < num_players; vp++) {
              if (vp == p) {
                continue;
              }
              const size_t v_idx = msl_idx_player(bi, vp);
              // ThrowB stale-latch context:
              // - defender already in ongoing hitstun from this same projectile kind.
              if (action_id == (uint16_t)MSL_ACT_THROW_B && batch->state.hitstun[v_idx] > 0u &&
                  batch->state.last_attack_landed[v_idx] == lp->shot_itkind) {
                stale_throw_pulse_context = 1u;
                if (stale_throw_pulse_victim >= 0) {
                  stale_throw_pulse_victim = -1;
                  break;
                }
                stale_throw_pulse_victim = vp;
              }
            }
            if (stale_throw_pulse_context) {
              if (action_id == (uint16_t)MSL_ACT_THROW_B && stale_throw_pulse_victim >= 0) {
                // ThrowB suppressed-pulse combo bookkeeping bridge:
                // - Throw-side projectile pulses are one-shot script events consumed in
                //   ftFx_Throw_Anim, and confirmed item hits route combo tracking through the item
                //   domain variant ftColl_8007646C -> ftColl_800763C0.
                // - When a stale pulse is suppressed because the victim is already in ongoing
                //   throw-laser hitstun from this owner, keep the attacker-side combo bookkeeping
                //   synchronized for the uniquely-owned victim without broadening projectile life.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                const size_t v_idx = msl_idx_player(bi, stale_throw_pulse_victim);
                const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_throw_pulse_victim) &&
                    batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                    batch->state.last_attack_landed[idx] == attack_id_u8 &&
                    batch->state.combo_count[idx] != 0u) {
                  batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                  if (cur_victim == 0xFFu) {
                    batch->state.combo_victim_port[idx] = (uint8_t)stale_throw_pulse_victim;
                  }
                  batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                }
              }
              continue;
            }
            // Seed-owned ThrowB stale pulse suppressor:
            // - `throw_pulse_consumed` bridges throw_flags_b0 pulse ownership from seed derivation.
            // - Keep suppression narrow to ongoing-hitstun contexts attributed to this thrower.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by / hitstun lanes)
            if (action_id == (uint16_t)MSL_ACT_THROW_B &&
                batch->state.throw_pulse_consumed[idx] != 0u) {
              uint8_t ongoing_throwb_context = 0u;
              for (int vp = 0; vp < num_players; vp++) {
                if (vp == p) {
                  continue;
                }
                const size_t v_idx = msl_idx_player(bi, vp);
                if (batch->state.hitstun[v_idx] > 0u &&
                    batch->state.last_hit_by[v_idx] == (uint8_t)p) {
                  ongoing_throwb_context = 1u;
                  break;
                }
              }
              if (ongoing_throwb_context) {
                continue;
              }
            }
            // ThrowB non-terminal stale-crossing suppressor (hitstun-window gated):
            // - ThrowB projectile pulses are script-owned one-shots (15/18/21 in move data) consumed
            //   in ftFx_Throw_Anim.
            // - Under one-step reseed, command consume-latch ownership is absent; in late ongoing
            //   throw-hitstun windows, non-terminal frame-crossing can replay a stale pulse.
            // - Gate suppression to non-terminal pulses and only when victim hitstun has already decayed
            //   below the script-owned pulse-window threshold.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (hitstun / last_hit_by lanes)
            // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"]["events"]
            if (action_id == (uint16_t)MSL_ACT_THROW_B &&
                batch->state.throw_pulse_consumed[idx] == 0u &&
                batch->state.throw_pulse_crossed_prev_frame[idx] == 0u) {
              int16_t throwb_last_pulse_af = 0;
              if (move_tables_throw_projectile_last_pulse_frame(cid, action_id,
                                                                &throwb_last_pulse_af) &&
                  crossed_pulse_af >= 0 && crossed_pulse_af < throwb_last_pulse_af) {
                const uint16_t stale_hitstun_thresh =
                    (uint16_t)(crossed_pulse_af + (int16_t)MSL_THROWB_PULSE_START_AF);
                uint8_t stale_throwb_context = 0u;
                int stale_throwb_victim = -1;
                for (int vp = 0; vp < num_players; vp++) {
                  if (vp == p) {
                    continue;
                  }
                  const size_t v_idx = msl_idx_player(bi, vp);
                  if (batch->state.hitstun[v_idx] > 0u &&
                      batch->state.last_hit_by[v_idx] == (uint8_t)p &&
                      batch->state.hitstun[v_idx] < stale_hitstun_thresh) {
                    stale_throwb_context = 1u;
                    if (stale_throwb_victim >= 0) {
                      stale_throwb_victim = -1;
                      break;
                    }
                    stale_throwb_victim = vp;
                  }
                }
                if (stale_throwb_context) {
                  if (stale_throwb_victim >= 0) {
                    // ThrowB non-terminal stale-crossing bookkeeping bridge:
                    // - This branch already suppresses replay-false non-terminal throw pulses in the
                    //   late ongoing-hitstun window owned by ftFx_Throw_Anim's one-shot pulse
                    //   script.
                    // - When that suppressed pulse belongs to a unique same-owner victim, Melee
                    //   still advances attacker-side item-domain combo bookkeeping
                    //   (ftColl_8007646C -> ftColl_800763C0) without a fresh hitlag/hitstun event.
                    // - Keep the bridge inside the existing hitstun-threshold suppressor only.
                    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                    const size_t v_idx = msl_idx_player(bi, stale_throwb_victim);
                    const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                    const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                    if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_throwb_victim) &&
                        batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                        batch->state.last_attack_landed[idx] == attack_id_u8 &&
                        batch->state.combo_count[idx] != 0u) {
                      batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                      if (cur_victim == 0xFFu) {
                        batch->state.combo_victim_port[idx] = (uint8_t)stale_throwb_victim;
                      }
                      batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                    }
                  }
                  continue;
                }
              }
            }
            // Seed-bridge stale-latch suppressors for throw projectile pulses:
            // - Throw script pulses are one-shot `throw_flags_b0` events consumed in ftFx_Throw_Anim.
            // - With one-step reseed, command-timer/cursor ownership is not seeded; reconstructing by
            //   raw frame crossing can re-emit specific startup/mid pulse windows that were already
            //   consumed in the source frame.
            // - Keep suppression scoped to the observed pulse windows and action-frame phases that are
            //   decomp-owned by ftAction command timing.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // data/moves/{fox,falco}.json set_throw_spawn_projectile pulse frames
            if (throw_blaster_pulse_is_seed_stale_latch(action_id, lp->shot_itkind,
                                                        crossed_pulse_af, prev_frame_i)) {
              if (action_id == (uint16_t)MSL_ACT_THROW_B) {
                // ThrowB startup stale-latch combo bookkeeping bridge:
                // - The first throw-side blaster pulse (frame 15) can be suppressed by the
                //   seed-stale-latch guard when one-step reseed lacks throw_flags_b0 cursor state.
                // - In Fox startup rows, that suppressed pulse still advances attacker-side combo
                //   bookkeeping when the victim remains uniquely in same-owner hitstun.
                // - Keep the bridge on this exact stale-latch suppressor path only; later ThrowB
                //   pulses remain owned by the existing hitstun-window suppressors above.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                int stale_victim_p = -1;
                for (int vp = 0; vp < num_players; vp++) {
                  if (vp == p) {
                    continue;
                  }
                  const size_t v_idx = msl_idx_player(bi, vp);
                  if (batch->state.hitstun[v_idx] == 0u ||
                      batch->state.last_hit_by[v_idx] != (uint8_t)p) {
                    continue;
                  }
                  if (stale_victim_p >= 0) {
                    stale_victim_p = -1;
                    break;
                  }
                  stale_victim_p = vp;
                }
                if (stale_victim_p >= 0) {
                  const size_t v_idx = msl_idx_player(bi, stale_victim_p);
                  const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                  const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                  if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_victim_p) &&
                      batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                      batch->state.last_attack_landed[idx] == attack_id_u8 &&
                      batch->state.combo_count[idx] != 0u) {
                    batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                    if (cur_victim == 0xFFu) {
                      batch->state.combo_victim_port[idx] = (uint8_t)stale_victim_p;
                    }
                    batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                  }
                }
              }
              continue;
            }
            if (crossed_pulse_af >= 0 && crossed_pulse_af <= 0xFF) {
              batch->state.throw_pulse_crossed_curr_frame[idx] = (uint8_t)crossed_pulse_af;
            }
            should_shoot = 1u;
            // Throw-side spawn path in ftFx_Throw_Anim uses it_8029C6CC (msid=1).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            shoot_spawn_state = 1u;
            if (action_id == (uint16_t)MSL_ACT_THROW_LW && has_first_pulse &&
                crossed_pulse_af >= first_pulse_af) {
              if (crossed_pulse_af == first_pulse_af) {
                // Common Throw-side intra-frame order:
                // - ftFx_Throw_Anim spawns the laser during the fighter Anim callback.
                // - the newly spawned shot can then consume its item motion callback later in the
                //   same frame before attached-victim collision is evaluated.
                // - Keep this on the first ThrowLw pulse only; later persistent pulses stay on the
                //   separate carried state1 owner below.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Phys
                shoot_apply_motion_step = 1u;
              } else {
                shoot_throw_lw_late_pulse_transn_y = 1u;
              }
            }
            // ThrowHi pulse-crossing velocity bridge:
            // - Throw-side launch direction in ftFx_Throw_Anim is hold-joint vector driven
            //   (`atan2f`), not the fixed SpecialN blaster angle.
            // - On mid/late ThrowHi pulse crossings, seed-visible latest throw-shot velocity is a
            //   tighter proxy than the default angle lane; keep this scoped to ThrowHi pulse
            //   crossings at/after the script mid pulse.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
            // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"]["events"]
            if (action_id == (uint16_t)MSL_ACT_THROW_HI && !shoot_use_velocity_override &&
                throw_seed_shot_valid[p] && crossed_pulse_af >= (int16_t)MSL_THROWHI_PULSE_MID_AF &&
                (batch->state.throw_pulse_crossed_prev_frame[idx] >=
                     (uint8_t)MSL_THROWHI_PREV_PHASE_AF ||
                 throw_seed_shot_count[p] != 0u)) {
              shoot_use_velocity_override = 1u;
              shoot_override_vx = throw_seed_shot_vx[p];
              shoot_override_vy = throw_seed_shot_vy[p];
            }
          }
        }
      }
      if (!should_shoot) {
        continue;
      }
      laser_spawn_from_fighter(batch, bi, p, lp, shoot_spawn_state, shoot_use_velocity_override,
                               shoot_override_vx, shoot_override_vy, shoot_apply_motion_step,
                               shoot_throw_lw_late_pulse_transn_y);
    }

    // Keep item ordering stable for fixed-slot comparisons.
    items_sort(batch, bi);
  }
}
