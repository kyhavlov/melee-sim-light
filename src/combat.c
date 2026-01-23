#include "combat.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "anim_frame.h"
#include "combat_geom.h"
#include "common_params.h"
#include "hitboxes_tables.h"
#include "hit_status_tables.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline size_t idx_pair(int bi, int attacker, int defender) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_PLAYERS +
         (size_t)defender;
}

static inline uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx,
                                               float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static inline uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg, uint16_t msid) {
  if (c == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Notes for this pass:
  // - We use mul=1.0f (fp->x1960_vibrateMult defaults to 1.0; refs/melee/src/melee/ft/fighter.c).
  // - We do not yet apply the Squat/SquatWait multiplier branch (msid check) because we don't yet
  //   have motion-state id constants in-core; add once we model FtMotionId tables.
  const float tmp_f = (float)dmg * c->hitlag_dmg_mul + c->hitlag_base;
  int tmp = (int)tmp_f;

  // Default vibrate multiplier:
  // refs/melee/src/melee/ft/fighter.c (init sets fp->x1960_vibrateMult = 1).
  const float mul = 1.0f;

  float result_f = (float)tmp * mul;
  // TODO(decomp): apply squat scaling for msid in [ftCo_MS_Squat, ftCo_MS_SquatWait] using
  // p_ftCommonData->x1A0 (extracted as hitlag_squat_mul) once FtMotionId constants are modeled.
  (void)msid;

  int result_i = (int)result_f;
  if (result_i < 0) {
    result_i = 0;
  }
  if (result_i > 0xFFFF) {
    result_i = 0xFFFF;
  }
  return (uint16_t)result_i;
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

  uint8_t hit_status = 0;
  (void)hit_status_get(d_char, d_msid, d_frame, &hit_status);
  return hit_status;
}

// Combat Mutations Pass 1 (BODY-only).
//
// This is the minimal "writeback" set needed for one-step eval:
// - hitlag via decomp ftCommon_CalcHitlag
// - attribution fields compared in-suite (instance_hit_by, last_hit_by)
static inline void combat_mutations_pass1_future_apply_body_hit(MslBatch* batch, size_t a_idx,
                                                                size_t d_idx, int attacker,
                                                                float hitbox_damage_f32,
                                                                uint16_t attacker_msid) {
  if (batch == NULL) {
    return;
  }

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
  // Intentionally NOT set in this pass:
  // - last_attack_landed (Slippi "Last Hitting Attack ID") requires attack-id extraction.
  // - combo_count requires combo tracking logic beyond strict one-step mutation.

  const MslCommonParams* c = msl_common_params();

  const int dmg_i = (hitbox_damage_f32 > 0.0f) ? (int)hitbox_damage_f32 : 0;
  const uint32_t d_msid_u32 = batch->state.animation_index[d_idx];
  const uint16_t d_msid = (d_msid_u32 <= 0xFFFFu) ? (uint16_t)d_msid_u32 : 0u;

  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_msid);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, d_msid);
  batch->state.hitlag[a_idx] = a_hl;
  batch->state.hitlag[d_idx] = d_hl;

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = (uint8_t)attacker;
}

static void combat_select_body_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      // Approximate decomp ClearHitboxes: if there are no active hitboxes, clear rehit latches for
      // this attacker.
      for (int defender = 0; defender < MSL_MAX_PLAYERS; defender++) {
        const size_t pair = idx_pair(bi, attacker, defender);
        batch->state.combat_rehit_active[pair] = 0;
      }
      continue;
    }

    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status eligibility gate (movescript-derived; opcode 26).
      //
      // Decomp pointers:
      // - refs/melee/src/melee/ft/ftaction.c:539 (ftAction_80071A14)
      // - refs/melee/src/melee/ft/ftcoll.c (hit status affects collision eligibility)
      //
      // Current policy: only treat hit_status==0 as eligible for BODY contacts.
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      if (hit_status != 0) {
        continue;
      }

      // Hurtbox state eligibility gate.
      //
      // Decomp pointers:
      // - refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 returns a composite "hurt state" based on
      //   fp->x221D_b6 and fp->x1988/x198C.
      // - refs/melee/src/melee/ft/ftcoll.c (main collision loop) gates hurtbox checks on
      //   this_fp->x1988/x198C (e.g. `!= 2` branch around hitbox-vs-hurtcapsule checks).
      //
      // We treat nonzero seeded `hurtbox_state` as not eligible for BODY hits for now.
      if (batch->state.hurtbox_state[d_idx] != 0) {
        continue;
      }

      // Hitlag gating: when either fighter is in hitlag, do not generate new BODY hits.
      if (batch->state.hitlag[a_idx] || batch->state.hitlag[d_idx]) {
        continue;
      }

      const size_t pair = idx_pair(bi, attacker, defender);
      // Rehit latch maintenance: clear stale latch entries on attacker msid change or defender
      // instance change.
      //
      // Note (approximation): the pass-1 rehit policy intentionally ignores hitbox_id (see
      // suppression check below), and the latch does NOT clear when the latched hitbox_id is
      // disabled. Only full hitbox clear (hitbox_count==0), msid change, or defender instance
      // change clears the latch.
      uint8_t rehit_active = batch->state.combat_rehit_active[pair];
      if (rehit_active) {
        const uint16_t latched_msid = batch->state.combat_rehit_attacker_msid[pair];
        const uint16_t defender_iid = batch->state.instance_id[d_idx];
        if (latched_msid != msid ||
            batch->state.combat_rehit_defender_instance_id[pair] != defender_iid) {
          rehit_active = 0;
          batch->state.combat_rehit_active[pair] = 0;
        }
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1 : 0;

      // Combat collision uses world-space hitbox/hurtcap primitives that are derived from:
      // - current pose matrices (anim timebase), and
      // - fighter translation (pos_x/pos_y/pos_z).
      //
      // Decomp ordering note (GALE01, approximate):
      // - Animation advancement and the per-motion-state `anim_cb` run before `phys_cb`
      //   (refs/melee/src/melee/ft/fighter.c::Fighter_8006ABEC vs Fighter_procUpdate).
      // - Our step() currently integrates physics before refreshing hitboxes/hurtboxes.
      //
      // Step-order workaround (current sim):
      // - We currently run physics integration before hitbox/hurtcap refresh + combat_resolve(),
      //   so the refreshed world primitives use the *post-physics* translation.
      // - To approximate "pose at this frame, translation before phys", shift the already-computed
      //   world primitives back by the per-fighter translation delta captured at the start of
      //   physics_integrate(): prev_pos_* is the pre-integration translation for this frame.
      const float a_shift_x = batch->state.prev_pos_x[a_idx] - batch->state.pos_x[a_idx];
      const float a_shift_y = batch->state.prev_pos_y[a_idx] - batch->state.pos_y[a_idx];
      const float d_shift_x = batch->state.prev_pos_x[d_idx] - batch->state.pos_x[d_idx];
      const float d_shift_y = batch->state.prev_pos_y[d_idx] - batch->state.pos_y[d_idx];

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

        const float hx = batch->state.hitbox_x[hb_i] + a_shift_x;
        const float hy = batch->state.hitbox_y[hb_i] + a_shift_y;
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

        // SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
        // and do not apply BODY selection for this hitbox.
        if (shield_active &&
            sphere_sphere_intersects(hx, hy, hz, hr, shx + d_shift_x, shy + d_shift_y, shz, shr)) {
          continue;
        }

        // Rehit suppression: if we latched a hit for this (attacker, defender) with this msid,
        // suppress repeats for this attacker→defender pair until hitboxes clear or msid changes.
        //
        // Note: Melee tracks per-hitbox hitlists (more granular than our reseeded pair latch). This
        // simplified policy is intentionally conservative: it latches the pair and ignores
        // hitbox_id, suppressing repeats until hitboxes clear (hitbox_count==0) or msid changes.
        if (rehit_active &&
            batch->state.combat_rehit_attacker_msid[pair] == msid &&
            batch->state.combat_rehit_defender_instance_id[pair] ==
                batch->state.instance_id[d_idx]) {
          continue;
        }

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          if (!batch->state.hurtcap_enabled[cap_i]) {
            continue;
          }
          const float ax = batch->state.hurtcap_a_x[cap_i] + d_shift_x;
          const float ay = batch->state.hurtcap_a_y[cap_i] + d_shift_y;
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i] + d_shift_x;
          const float by = batch->state.hurtcap_b_y[cap_i] + d_shift_y;
          const float bz = batch->state.hurtcap_b_z[cap_i];
          const float cr = batch->state.hurtcap_radius[cap_i];

          if (!combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
            continue;
          }

          // Combat Mutations Pass 1 (BODY-only).
          combat_mutations_pass1_future_apply_body_hit(batch, a_idx, d_idx, attacker, hdmg, msid);

          batch->state.combat_rehit_active[pair] = 1;
          batch->state.combat_rehit_hitbox_id[pair] = (uint8_t)hb_id;
          batch->state.combat_rehit_attacker_msid[pair] = msid;
          batch->state.combat_rehit_defender_instance_id[pair] = batch->state.instance_id[d_idx];

          did_hit = 1;
          break;
        }
      }
    }
  }
}

static void combat_select_body_hits_one_debug(const MslBatch* batch, int bi,
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

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status eligibility gate (movescript-derived; opcode 26).
      //
      // Decomp pointers:
      // - refs/melee/src/melee/ft/ftaction.c:539 (ftAction_80071A14)
      // - refs/melee/src/melee/ft/ftcoll.c (hit status affects collision eligibility)
      //
      // Current policy: only treat hit_status==0 as eligible for BODY contacts.
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      if (hit_status != 0) {
        continue;
      }

      // Hurtbox state eligibility gate.
      //
      // Decomp pointers:
      // - refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 returns a composite "hurt state" based on
      //   fp->x221D_b6 and fp->x1988/x198C.
      // - refs/melee/src/melee/ft/ftcoll.c (main collision loop) gates hurtbox checks on
      //   this_fp->x1988/x198C (e.g. `!= 2` branch around hitbox-vs-hurtcapsule checks).
      //
      // We treat nonzero seeded `hurtbox_state` as not eligible for BODY hits for now.
      if (batch->state.hurtbox_state[d_idx] != 0) {
        continue;
      }

      // Hitlag gating: when either fighter is in hitlag, do not generate new BODY hits.
      if (batch->state.hitlag[a_idx] || batch->state.hitlag[d_idx]) {
        continue;
      }

      const size_t pair = idx_pair(bi, attacker, defender);
      const uint16_t defender_iid = batch->state.instance_id[d_idx];
      const uint8_t rehit_active =
          (batch->state.combat_rehit_active[pair] &&
           batch->state.combat_rehit_attacker_msid[pair] == msid &&
           batch->state.combat_rehit_defender_instance_id[pair] == defender_iid)
              ? 1
              : 0;

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1 : 0;

      const float a_shift_x = batch->state.prev_pos_x[a_idx] - batch->state.pos_x[a_idx];
      const float a_shift_y = batch->state.prev_pos_y[a_idx] - batch->state.pos_y[a_idx];
      const float d_shift_x = batch->state.prev_pos_x[d_idx] - batch->state.pos_x[d_idx];
      const float d_shift_y = batch->state.prev_pos_y[d_idx] - batch->state.pos_y[d_idx];

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

        const float hx = batch->state.hitbox_x[hb_i] + a_shift_x;
        const float hy = batch->state.hitbox_y[hb_i] + a_shift_y;
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        if (!(hdmg > 0.0f)) {
          continue;
        }

        // SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
        // and do not apply BODY selection for this hitbox.
        if (shield_active &&
            sphere_sphere_intersects(hx, hy, hz, hr, shx + d_shift_x, shy + d_shift_y, shz, shr)) {
          continue;
        }

        // Rehit suppression (debug view): if the pair latch is active, suppress repeats.
        if (rehit_active) {
          continue;
        }

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          if (!batch->state.hurtcap_enabled[cap_i]) {
            continue;
          }
          const float ax = batch->state.hurtcap_a_x[cap_i] + d_shift_x;
          const float ay = batch->state.hurtcap_a_y[cap_i] + d_shift_y;
          const float az = batch->state.hurtcap_a_z[cap_i];
          const float bx = batch->state.hurtcap_b_x[cap_i] + d_shift_x;
          const float by = batch->state.hurtcap_b_y[cap_i] + d_shift_y;
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

void combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    combat_select_body_hits_one_mutating(batch, bi);
  }
}

int combat_debug_select_body_hits(MslBatch* batch, int batch_index, MslDebugCombatContact* out_contacts,
                                  uint16_t max_contacts, uint16_t* out_count) {
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
