#include "combat.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "buttons.h"
#include "combat_geom.h"
#include "common_params.h"
#include "hit_elements.h"
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
    f &= (uint8_t)~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg,
                                                 uint16_t motion_id) {
  if (c == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Notes for this pass:
  // - We use mul=1.0f (fp->x1960_vibrateMult defaults to 1.0; refs/melee/src/melee/ft/fighter.c).
  const float tmp_f = (float)dmg * c->hitlag_dmg_mul + c->hitlag_base;
  int tmp = (int)tmp_f;

  // Default vibrate multiplier:
  // refs/melee/src/melee/ft/fighter.c (init sets fp->x1960_vibrateMult = 1).
  const float mul = 1.0f;

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
                                                                int int_dmg,
                                                                uint16_t attacker_motion_id) {
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

  // Decomp (GALE01): collision converts hitbox float damage -> int via getEnvDmg, and
  // Fighter_ProcessHit uses a nonzero int damage (`bool1`) as the dmg input to ftCommon_CalcHitlag.
  // refs/melee/src/melee/ft/ftcoll.c::inlineA0/inlineA1 (getEnvDmg pattern)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (hitlag calc under `if (bool1)`)
  //
  // Note (approximation): GALE01 also maintains `fp->dmg.int_value` as a max across contacts during
  // collision. In pass 1 we instead pass the selected contact's `getEnvDmg(damage)` here.
  const int dmg_i = (int_dmg > 0) ? int_dmg : 0;
  if (dmg_i == 0) {
    return;
  }
  const uint16_t d_motion_id = batch->state.action_id[d_idx];

  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, d_motion_id);
  batch->state.hitlag[a_idx] = a_hl;
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = (uint8_t)attacker;
}

static inline void combat_mutations_pass1_future_apply_shield_hit(MslBatch* batch, size_t a_idx,
                                                                  size_t d_idx, int int_dmg,
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
  if (int_dmg < 0) {
    int_dmg = 0;
  }
  if (shield_damage_taken < 0) {
    shield_damage_taken = 0;
  }

  // Powershield gating: collision does not accumulate shieldDamageTaken when the "powershield
  // active" flag is set (x221C_b2).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (`if (!fp1->x221C_b2) { ...shieldDamageTaken... }`)
  // Slippi post-frame: `lbz r3,0x221C(REG_PlayerData)  #0x20 = Powershield Active Bool`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
  const uint8_t flags_221c =
      batch->state.state_flags[d_idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE) {
    shield_damage_taken = 0;
  }

  const float light = combat_lightshield_amount(c, batch->state.input_buttons[d_idx],
                                                batch->state.input_l[d_idx],
                                                batch->state.input_r[d_idx]);
  const float ls =
      (light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min)) +
      c->shield_hit_lightshield_min;
  const float depletion =
      c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - ls)) +
      c->shield_hit_damage_base;

  float hp = batch->state.shield_hp[d_idx];
  hp -= depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;

  // Shieldstun (GuardSetOff) entry.
  // Decomp entry: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  // In our replay-derived datasets, shield states frequently have `animation_index == -1`.
  batch->state.animation_index[d_idx] = 0xFFFFFFFFu;
  batch->state.action_frame[d_idx] = 0;
  batch->state.anim_frame_f32[d_idx] = 0.0f;

  // Hitlag on shield contact uses the same decomp ftCommon_CalcHitlag path as BODY, but with
  // shield-collision inputs:
  // - attacker uses fp->dmg.x1924 (max int_dmg over shield contacts this frame),
  // - defender uses fp->x19A4 (max int_dmg over shield contacts this frame),
  // both computed from hit0->damage via getEnvDmg.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t a_hl = combat_calc_hitlag_frames(c, int_dmg, attacker_motion_id);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, int_dmg, d_motion_id_pre);
  batch->state.hitlag[a_idx] = a_hl;
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
}

static void combat_select_body_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Slippi post-frame `state_flags` includes fp+0x221C bits at byte index 3.
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };

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
    const uint16_t a_motion_id = batch->state.action_id[a_idx];

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
        uint8_t sel_hb_id = 0;

        // Capture defender motion id for hitlag inputs before any shield-state mutations.
        const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
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

          // Rehit suppression: if we latched a hit for this (attacker, defender) with this msid,
          // suppress repeats for this attacker→defender pair until hitboxes clear or msid changes.
          if (rehit_active &&
              batch->state.combat_rehit_attacker_msid[pair] == msid &&
              batch->state.combat_rehit_defender_instance_id[pair] ==
                  batch->state.instance_id[d_idx]) {
            continue;
          }

          if (!sphere_sphere_intersects(hx, hy, hz, hr, shx + d_shift_x, shy + d_shift_y, shz, shr)) {
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

          const float hdmg = batch->state.hitbox_damage[hb_i];
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
            sel_hb_id = (uint8_t)hb_id;
          }
        }

        if (sel_int_dmg > 0) {
          int tmp_dmg = sel_int_dmg + (int)sel_shield_dmg_s8;
          if (tmp_dmg < 0) {
            tmp_dmg = 0;
          }

          // Combat Mutations Pass 1 (SHIELD-only).
          combat_mutations_pass1_future_apply_shield_hit(
              batch, a_idx, d_idx, sel_int_dmg, tmp_dmg, a_motion_id);

          batch->state.combat_rehit_active[pair] = 1;
          batch->state.combat_rehit_hitbox_id[pair] = sel_hb_id;
          batch->state.combat_rehit_attacker_msid[pair] = msid;
          batch->state.combat_rehit_defender_instance_id[pair] = batch->state.instance_id[d_idx];

          did_hit = 1;

          if (max_int_dmg > sel_int_dmg) {
            const MslCommonParams* c = msl_common_params();
            const uint16_t a_hl = combat_calc_hitlag_frames(c, max_int_dmg, a_motion_id);
            const uint16_t d_hl = combat_calc_hitlag_frames(c, max_int_dmg, d_motion_id_pre);
            batch->state.hitlag[a_idx] = a_hl;
            batch->state.hitlag[d_idx] = d_hl;
            combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
            combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
          }
        }
      }

      if (did_hit) {
        continue;
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
      // We treat nonzero `hurtbox_state` (seeded and/or sim-owned) as not eligible for BODY hits for now.
      if (batch->state.hurtbox_state[d_idx] != 0) {
        continue;
      }

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

        const int int_dmg = combat_get_env_dmg(hdmg);
        if (int_dmg <= 0) {
          continue;
        }

        // Shield precedence (BODY path): if the hitbox intersects the defender shield bubble, do
        // not apply BODY selection for this hitbox. The shield-hit selection above handles
        // (hitbox_id)-order shield resolution; this check is a conservative fallback.
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
          combat_mutations_pass1_future_apply_body_hit(batch, a_idx, d_idx, attacker, int_dmg,
                                                       a_motion_id);

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
      // We treat nonzero `hurtbox_state` (seeded and/or sim-owned) as not eligible for BODY hits for now.
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
          (uint8_t)~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
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
