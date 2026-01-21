#include "combat.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "combat_geom.h"
#include "common_params.h"
#include "hitboxes_tables.h"

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

static void combat_select_body_hits_one(MslBatch* batch, int bi, MslDebugCombatContact* out_contacts,
                                        uint16_t max_contacts, uint16_t* inout_written) {
  const int num_players = (int)batch->config.num_players;
  uint16_t written = inout_written ? *inout_written : 0;
  const uint8_t write_out = (out_contacts != NULL && inout_written != NULL && max_contacts > 0);

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
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
    const int16_t action_frame = batch->state.action_frame[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      if (hurtcap_count == 0) {
        continue;
      }

      // Hitlag gating: when either fighter is in hitlag, do not generate new BODY hits.
      if (batch->state.hitlag[a_idx] || batch->state.hitlag[d_idx]) {
        continue;
      }

      const size_t pair = idx_pair(bi, attacker, defender);
      if (batch->state.combat_rehit_active[pair]) {
        // Clear stale latch entries on msid change, defender instance change, or latched hitbox
        // being disabled (hitbox clear).
        const uint16_t latched_msid = batch->state.combat_rehit_attacker_msid[pair];
        const uint16_t defender_iid = batch->state.instance_id[d_idx];
        if (latched_msid != msid ||
            batch->state.combat_rehit_defender_instance_id[pair] != defender_iid) {
          batch->state.combat_rehit_active[pair] = 0;
        } else {
          const uint8_t latched_hb = batch->state.combat_rehit_hitbox_id[pair];
          if (latched_hb != 0xFFu) {
            const size_t hb_i = idx_hitbox(bi, attacker, (int)latched_hb);
            if (!batch->state.hitbox_enabled[hb_i]) {
              batch->state.combat_rehit_active[pair] = 0;
            }
          }
        }
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1 : 0;

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

        // SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
        // and do not apply BODY selection for this hitbox.
        if (shield_active && sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr)) {
          continue;
        }

        // Rehit suppression: if we latched a hit for this (attacker, defender) with this msid,
        // suppress repeats for the same hitbox_id until hitboxes clear or msid changes.
        if (batch->state.combat_rehit_active[pair] &&
            batch->state.combat_rehit_attacker_msid[pair] == msid &&
            batch->state.combat_rehit_defender_instance_id[pair] ==
                batch->state.instance_id[d_idx]) {
          const uint8_t latched_hb = batch->state.combat_rehit_hitbox_id[pair];
          if (latched_hb == 0xFFu || latched_hb == (uint8_t)hb_id) {
            continue;
          }
        }

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
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

          // NOTE: combat_resolve is non-mutating again (suite stability).
          // We keep deterministic selection + latch scaffolding, but do not write percent/hitlag/
          // hitstun/attribution yet.

          if (write_out && written < max_contacts) {
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

          batch->state.combat_rehit_active[pair] = 1;
          batch->state.combat_rehit_hitbox_id[pair] = (uint8_t)hb_id;
          batch->state.combat_rehit_attacker_msid[pair] = msid;
          batch->state.combat_rehit_defender_instance_id[pair] = batch->state.instance_id[d_idx];

          did_hit = 1;
          break;
        }
      }

      if (write_out && written >= max_contacts) {
        *inout_written = written;
        return;
      }
    }
  }

  if (inout_written) {
    *inout_written = written;
  }
}

void combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    combat_select_body_hits_one(batch, bi, NULL, 0, NULL);
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
  combat_select_body_hits_one(batch, batch_index, out_contacts, max_contacts, &written);
  *out_count = written;
  return 0;
}
