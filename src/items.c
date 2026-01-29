#include "items.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "combat.h"
#include "char_params.h"
#include "laser_params.h"
#include "msl_math.h"
#include "mtx34.h"
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
  batch->state.item_timer[ii] = 0.0f;
  batch->state.item_spawn_id[ii] = 0;
  batch->state.item_misc0[ii] = 0;
  batch->state.item_misc1[ii] = 0;
  batch->state.item_misc2[ii] = 0;
  batch->state.item_misc3[ii] = 0;

  // Clear per-item victim cooldowns (hitlist).
  const size_t cd_base = ii * (size_t)MSL_MAX_PLAYERS;
  for (int v = 0; v < MSL_MAX_PLAYERS; v++) {
    batch->state.item_hitlist_cd[cd_base + (size_t)v] = 0;
    batch->state.item_hitlist_victim_iid[cd_base + (size_t)v] = 0;
  }
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
  SWAP(float, batch->state.item_timer);
  SWAP(uint32_t, batch->state.item_spawn_id);
  SWAP(uint8_t, batch->state.item_misc0);
  SWAP(uint8_t, batch->state.item_misc1);
  SWAP(uint8_t, batch->state.item_misc2);
  SWAP(uint8_t, batch->state.item_misc3);
#undef SWAP

  // Swap per-item hitlist lanes to preserve deterministic item ordering invariants.
  for (int v = 0; v < MSL_MAX_PLAYERS; v++) {
    const size_t ia = a * (size_t)MSL_MAX_PLAYERS + (size_t)v;
    const size_t ib = b * (size_t)MSL_MAX_PLAYERS + (size_t)v;
    const uint16_t tmp = batch->state.item_hitlist_cd[ia];
    batch->state.item_hitlist_cd[ia] = batch->state.item_hitlist_cd[ib];
    batch->state.item_hitlist_cd[ib] = tmp;
    const uint16_t tmp_iid = batch->state.item_hitlist_victim_iid[ia];
    batch->state.item_hitlist_victim_iid[ia] = batch->state.item_hitlist_victim_iid[ib];
    batch->state.item_hitlist_victim_iid[ib] = tmp_iid;
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

static inline uint16_t items_find_gun_instance_id(const MslBatch* batch, int bi, int owner,
                                                  uint16_t gun_itkind) {
  if (owner < 0) {
    return 0;
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
    return batch->state.item_instance_id[ii];
  }
  return 0;
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

static void blaster_gun_update_from_fighter(MslBatch* batch, int bi, int owner,
                                            const MslLaserParams* lp) {
  if (batch == NULL || lp == NULL) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint16_t prev_action_id_u16 = batch->state.prev_action_id[o_idx];
  const uint8_t prev_state = blaster_gun_state_from_action_id(prev_action_id_u16);
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint8_t want_state = blaster_gun_state_from_action_id(action_id_u16);
  const uint8_t want_gun = (want_state != 9) ? 1u : 0u;

  const int existing_slot = items_find_gun_slot(batch, bi, owner, lp->gun_itkind);
  // Throw states (6..8) do not *spawn* the gun; they only keep it alive if it already exists.
  // Decomp includes throws in ftFx_SpecialN_GetBlasterAction for "moves that require the blaster item",
  // but the gun item itself is spawned on SpecialN enter (it_802AE8A8) and persisted via owner linkage.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter
  // refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8
  if (want_gun && want_state >= 6 && want_state <= 8 && existing_slot < 0) {
    return;
  }
  if (!want_gun) {
    if (existing_slot < 0) {
      return;
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
    // Implementation: keep the gun only on the blaster->non-blaster transition (prev_state!=9), and
    // clear it immediately if the previous frame was already non-blaster (prev_state==9).
    if (prev_state != 9) {
      // Keep as-is (do not overwrite item_state/pos); this is the linger frame.
      batch->state.item_exists[ii] = 1;
      batch->state.item_type[ii] = lp->gun_itkind;
      batch->state.item_owner[ii] = (int8_t)owner;
      return;
    }

    item_slot_clear(batch, ii);
    return;
  }

  int slot = existing_slot;
  uint8_t spawned = 0;
  if (slot < 0) {
    slot = items_alloc_slot(batch, bi);
    if (slot < 0) {
      return;
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

    // Blaster gun items commonly have spawn_id=0 (Slippi field at item+0x1C).
    // Observed in suite replay "AttachedGoodNaturedGuanaco" at frame -26 (type=75 spawn_id=0).
    batch->state.item_spawn_id[ii] = 0;

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
}

static inline const MslLaserParams* laser_params_for_item_type(uint16_t type) {
  const MslLaserParams* fox = laser_params_get(1);
  const MslLaserParams* falco = laser_params_get(22);
  if (fox && fox->shot_itkind == type) {
    return fox;
  }
  if (falco && falco->shot_itkind == type) {
    return falco;
  }
  return NULL;
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

static inline uint8_t item_sphere_capsule_intersects(const MslBatch* batch, int bi, int defender,
                                                     float sx, float sy, float sr, int cap_i,
                                                     uint8_t* out_hurt_height) {
  // Sphere (sx,sy,0,sr) vs capsule defined by endpoints (a,b,radius) in world space.
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

  // Closest point on segment AB to point S (sx,sy,0).
  const float abx = bx - ax;
  const float aby = by - ay;
  const float abz = bz - az;
  const float asx = sx - ax;
  const float asy = sy - ay;
  const float asz = 0.0f - az;
  const float ab2 = abx * abx + aby * aby + abz * abz;
  float t = 0.0f;
  if (ab2 > 0.0f) {
    t = (asx * abx + asy * aby + asz * abz) / ab2;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float cx = ax + t * abx;
  const float cy = ay + t * aby;
  const float cz = az + t * abz;
  const float dx = sx - cx;
  const float dy = sy - cy;
  const float dz = 0.0f - cz;
  if ((dx * dx + dy * dy + dz * dz) > (rr * rr)) {
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

static void laser_spawn_from_fighter(MslBatch* batch, int bi, int owner, const MslLaserParams* lp) {
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
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[o_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);

  // Spawn point: lb_8000B1CC(bone_joint, offset, out) (decomp), approximated with pose matrices.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, lp->spawn_bone_part_id, m) != 0) {
    return;
  }
  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  msl_mtx34_mul_point(m, lp->spawn_off_xyz, &lx, &ly, &lz);

  const float scale_y = batch->state.fighter_scale_y[o_idx];
  const float facing_dir = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  lx *= (scale_y * facing_dir);
  ly *= scale_y;
  // lz is ignored in the 2D light sim, but affects lx/ly via the bone matrix.

  const float pos_x = batch->state.pos_x[o_idx] + lx;
  const float pos_y = batch->state.pos_y[o_idx] + ly;

  // Launch angle: if facing left, use (pi - base_angle).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
  float ang = lp->blaster_angle;
  if (facing_dir < 0.0f) {
    ang = MSL_PI_F - ang;
  }
  const float spd = lp->blaster_speed;
  const float vx = spd * cosf(ang);
  const float vy = spd * sinf(ang);
  const float dir = (vx >= 0.0f) ? 1.0f : -1.0f;

  batch->state.item_exists[ii] = 1;
  batch->state.item_state[ii] = 0;  // it_8029C6A4 uses msid=0 (itfoxlaser.c::it_8029C6A4)
  batch->state.item_type[ii] = lp->shot_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  // Staling identity: items copy the owner's fighter-side (attack_id, attack_instance) at spawn:
  // `item->xD88_attackID = fighter->x2068_attackID; item->xD8C_attack_instance = fighter->x206C_attack_instance;`
  // refs/melee/src/melee/it/it_2725.c::it_8027B070
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  // Slippi fixed ordering key uses instance_id; for blaster shots, it commonly matches the owner's gun item.
  // (Dataset ordering source: tools/slippi/make_dataset_from_slp.py::_fill_items_fixed.)
  const uint16_t gun_instance_id = items_find_gun_instance_id(batch, bi, owner, lp->gun_itkind);
  batch->state.item_instance_id[ii] =
      (gun_instance_id != 0) ? gun_instance_id : batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = dir;
  batch->state.item_vel_x[ii] = vx;
  batch->state.item_vel_y[ii] = vy;
  batch->state.item_pos_x[ii] = pos_x;
  batch->state.item_pos_y[ii] = pos_y;
  batch->state.item_timer[ii] = (float)lp->lifetime_frames;
}

static void lasers_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  const uint32_t stage_id = batch->state.stage_id[bi];

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

    // Collision: laser hitbox script defines multiple hitboxes along the beam; approximate as
    // multiple spheres along X using hitbox_offsets_x.
    //
    // Source of offsets: `data/items/lasers.bin` hitbox_offsets_x[] (MSLLASR1 v2), extracted
    // from the laser article state script in Pl*.dat by tools/extraction/extract_lasers.py.
    // docs/DATA_CONTRACT.md documents the binary layout and decomp pointers.
    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= num_players) {
      continue;
    }
    const float dir = batch->state.item_direction[ii];
    const float sr = lp->size;

    for (int def = 0; def < num_players; def++) {
      if (def == owner) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, def);
      const size_t cd_i = ii * (size_t)MSL_MAX_PLAYERS + (size_t)def;
      const uint16_t def_iid = batch->state.instance_id[d_idx];
      if (batch->state.item_hitlist_cd[cd_i] != 0) {
        // Item hitlists: do not rehurt the same fighter repeatedly while the item persists.
        //
        // Decomp identity key is a victim pointer (`HitVictim.victim`), which changes on
        // death/respawn. Use the fighter's `instance_id` as the stable key in the sim.
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
        if (batch->state.item_hitlist_victim_iid[cd_i] == def_iid) {
          continue;
        }
        batch->state.item_hitlist_cd[cd_i] = 0;
        batch->state.item_hitlist_victim_iid[cd_i] = 0;
      }

      // SHIELD precedence: if the item intersects the defender shield bubble, resolve as a shield
      // contact and do not take the BODY path.
      //
      // Authority:
      // - itfoxlaser.c Logic94 callbacks: it_2725_Logic94_HitShield / it_2725_Logic94_Reflected
      // - collision shield precedence: ftColl_80076CBC / lbColl_80007BCC
      // refs/melee/src/melee/it/items/itfoxlaser.c and refs/melee/src/melee/ft/ftcoll.c
      const float shr = batch->state.shield_radius[d_idx];
      if (shr > 0.0f) {
        // Use derived shield bubble center from shields_refresh() (same geometry used by the
        // fighter-vs-fighter combat pass).
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        if (!isfinite(shx) || !isfinite(shy)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
        }
        uint8_t shield_hit = 0;
        const uint8_t off_n = lp->hitbox_offsets_x_count;
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !shield_hit;
             oi++) {
          const float sx = x + (dir * lp->hitbox_offsets_x[oi]);
          const float sy = y;
          if (item_sphere_sphere_intersects_2d(sx, sy, sr, shx, shy, shr)) {
            shield_hit = 1;
          }
        }
        if (!shield_hit && off_n == 0) {
          shield_hit = item_sphere_sphere_intersects_2d(x, y, sr, shx, shy, shr);
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
          // TODO(reflect): This gate models *powershield reflect only* (reflect-active + powershield-active).
          // It will NOT reflect lasers off special-move reflect bubbles (reflect-active without the
          // powershield bit). Implement the item-vs-reflect-bubble overlap path (ftColl_CreateReflectHit
          // bubble) to cover those cases.
          //
          // Note: item attack_id / attack_instance remain spawn-latched for lasers in v1 (do not
          // transfer on reflect here).
          enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
          enum { MSL_STATE_FLAGS_2218_INDEX = 0 };
          // Reflect-active bit in fp+0x2218 as packed by Slippi: 0x10.
          enum { MSL_STATE_FLAG_2218_IS_REFLECT_ACTIVE = 0x10 };
          const uint8_t flags_2218 =
              batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_STRIDE +
                                       (size_t)MSL_STATE_FLAGS_2218_INDEX];
          enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
          enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
          const uint8_t flags_221c =
              batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_STRIDE +
                                       (size_t)MSL_STATE_FLAGS_221C_INDEX];
          if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_IS_REFLECT_ACTIVE) &&
              (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE)) {
            batch->state.item_owner[ii] = (int8_t)def;
            batch->state.item_vel_x[ii] = -batch->state.item_vel_x[ii];
            batch->state.item_vel_y[ii] = -batch->state.item_vel_y[ii];
            batch->state.item_direction[ii] = -dir;
            break;
          }

          // Regular shield hit: apply defender-side shield effects and despawn the laser.
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], lp->damage,
                                       lp->shield_damage);
          batch->state.item_hitlist_cd[cd_i] = 0xFFFFu;
          batch->state.item_hitlist_victim_iid[cd_i] = def_iid;
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
          const uint8_t off_n = lp->hitbox_offsets_x_count;
          for (uint8_t oi = 0;
               oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !reflect_hit; oi++) {
            const float sx = x + (dir * lp->hitbox_offsets_x[oi]);
            const float sy = y;
            if (item_sphere_sphere_intersects_2d(sx, sy, sr, rx, ry, rr)) {
              reflect_hit = 1;
            }
          }
          if (!reflect_hit && off_n == 0) {
            reflect_hit = item_sphere_sphere_intersects_2d(x, y, sr, rx, ry, rr);
          }

          if (reflect_hit) {
            batch->state.item_owner[ii] = (int8_t)def;
            // Slippi item.instance_id is item->xDA8_short; reflect path updates it from the reflecting
            // fighter's snapshot (fp->x2088 / instance_id).
            batch->state.item_instance_id[ii] = batch->state.instance_id[d_idx];

            const float mul = rch->reflector_speed_mul;
            const float new_vx = -batch->state.item_vel_x[ii] * mul;
            const float new_vy = -batch->state.item_vel_y[ii] * mul;
            batch->state.item_vel_x[ii] = new_vx;
            batch->state.item_vel_y[ii] = new_vy;
            batch->state.item_direction[ii] = (new_vx >= 0.0f) ? 1.0f : -1.0f;
            break;
          }
        }
      }

      uint8_t hit_hurt_height = 0;
      uint8_t hit = 0;
      // Deterministic order: offsets (script order) then capsule slots.
      const uint8_t off_n = lp->hitbox_offsets_x_count;
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit; oi++) {
        const float sx = x + (dir * lp->hitbox_offsets_x[oi]);
        const float sy = y;
        for (uint8_t ci = 0; ci < cap_n; ci++) {
          if (item_sphere_capsule_intersects(batch, bi, def, sx, sy, sr, (int)ci,
                                             &hit_hurt_height)) {
            hit = 1;
            break;
          }
        }
      }
      // Fallback: if no offsets extracted, test the item origin.
      if (!hit && off_n == 0 && cap_n > 0) {
        for (uint8_t ci = 0; ci < cap_n; ci++) {
          if (item_sphere_capsule_intersects(batch, bi, def, x, y, sr, (int)ci, &hit_hurt_height)) {
            hit = 1;
            break;
          }
        }
      }

      if (!hit) {
        continue;
      }

      // Apply BODY hit to defender and despawn the laser.
      // Decomp-first references for BODY apply:
      // - Fighter_ProcessHit_8006D1EC (percent add, hitlag, hitstun, damage-state entry)
      // - ftColl_80076CBC (getEnvDmg pattern)
      // refs/melee/src/melee/ft/fighter.c and refs/melee/src/melee/ft/ftcoll.c
      combat_apply_item_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                            batch->state.item_attack_instance[ii], lp->damage, lp->angle, lp->kbg,
                            lp->wsk, lp->bkb, hit_hurt_height);
      batch->state.item_hitlist_cd[cd_i] = 0xFFFFu;
      batch->state.item_hitlist_victim_iid[cd_i] = def_iid;
      item_slot_clear(batch, ii);
      break;
    }
  }
}

void items_update(MslBatch* batch) {
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

  // Spawn lasers from fighter loop scripts (cmd_var[2] pulses).
  // Decomp: ftFx_SpecialNLoop_Anim / ftFx_SpecialAirNLoop_Anim call ftFx_SpecialN_CreateBlasterShot,
  // which checks fp->cmd_vars[2] and spawns via it_8029C6A4.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Update gun items first so laser spawns can inherit the correct instance_id/spawn_id ordering.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL || lp->gun_itkind == 0) {
        continue;
      }
      blaster_gun_update_from_fighter(batch, bi, p, lp);
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] != 0) {
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
      const float af = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      const uint16_t frame = msl_anim_frame_floor_u16(af);
      if (!laser_should_shoot_on_frame(lp, msid, frame)) {
        continue;
      }
      laser_spawn_from_fighter(batch, bi, p, lp);
    }

    // Motion + collision/hit apply for existing lasers.
    lasers_update_and_collide(batch, bi);

    // Keep item ordering stable for fixed-slot comparisons.
    items_sort(batch, bi);
  }
}
