#include "fighter_script.h"

#include <math.h>
#include <stdint.h>

#include "anim_timebase.h"
#include "combat.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "char_params.h"
#include "common_params.h"
#include "ftcommon_ecb.h"
#include "hurtcaps_tables.h"
#include "motion_state_owners.h"
#include "script_events.h"
#include "staling.h"
#include "state_flags.h"

enum {
  MSL_HITCAPSULE_DISABLED = 0,
  MSL_HITCAPSULE_ENABLED = 1,
  MSL_HITCAPSULE_NEW = 2,
  MSL_HITCAPSULE_ACTIVE = 3,
};

static inline size_t fighter_script_hitbox_index(size_t idx, uint8_t hitbox_id) {
  return idx * (size_t)MSL_MAX_HITBOXES + (size_t)hitbox_id;
}

static inline size_t fighter_script_cmd_index(size_t idx, uint8_t cmd_var) {
  return idx * 4u + (size_t)cmd_var;
}

static inline size_t fighter_script_throw_index(size_t idx, uint8_t throw_id) {
  return idx * 2u + (size_t)throw_id;
}

static inline size_t fighter_script_hurtcap_index(size_t idx, uint8_t cap_id) {
  return idx * (size_t)MSL_MAX_HURTCAPS + (size_t)cap_id;
}

static void fighter_script_set_airborne_state(MslBatch* batch, size_t idx, uint8_t state) {
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const uint8_t max_jumps = ch != NULL ? ch->max_jumps : batch->state.jumps_left[idx];
  switch (state) {
    case 0u:
      msl_ftcommon_8007d6a4(batch, ch, idx);
      if (ch == NULL) {
        batch->state.jumps_left[idx] = max_jumps;
      }
      break;
    case 1u:
      batch->state.on_ground[idx] = 0u;
      batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.jumps_left[idx] = max_jumps > 0u ? (uint8_t)(max_jumps - 1u) : 0u;
      batch->state.pos_z[idx] = 0.0f;
      msl_ftcommon_lock_ecb_8007d5d4(batch, idx);
      break;
    case 2u:
      batch->state.on_ground[idx] = 0u;
      batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.jumps_left[idx] = 0u;
      msl_ftcommon_lock_ecb_8007d60c(batch, idx);
      break;
    default:
      break;
  }
}

static void fighter_script_set_all_hurtcaps(MslBatch* batch, size_t idx, uint8_t state) {
  for (uint8_t cap_id = 0u; cap_id < (uint8_t)MSL_MAX_HURTCAPS; cap_id++) {
    batch->state.script_hurtcap_state[fighter_script_hurtcap_index(idx, cap_id)] = state;
  }
}

static void fighter_script_set_hurtcap_bone(MslBatch* batch, size_t idx, uint8_t bone,
                                            uint8_t state) {
  const MslHurtCap* caps = NULL;
  uint16_t count = 0u;
  if (hurtcaps_get(batch->state.char_id[idx], &caps, &count) != 0 || caps == NULL) {
    return;
  }
  if (count > (uint16_t)MSL_MAX_HURTCAPS) {
    count = (uint16_t)MSL_MAX_HURTCAPS;
  }
  for (uint16_t cap_id = 0u; cap_id < count; cap_id++) {
    if (caps[cap_id].bone_part_id == (uint16_t)bone) {
      batch->state.script_hurtcap_state[fighter_script_hurtcap_index(idx, (uint8_t)cap_id)] = state;
    }
  }
}

static void fighter_script_set_throw_hitbox(MslBatch* batch, size_t idx,
                                            const MslScriptThrowHitboxPayload* hit) {
  if (hit->idx >= 2u) {
    return;
  }
  const size_t ti = fighter_script_throw_index(idx, hit->idx);
  batch->state.script_throw_hitbox_valid[ti] = 1u;
  batch->state.script_throw_hitbox_damage[ti] = hit->damage;
  batch->state.script_throw_hitbox_angle[ti] = hit->angle;
  batch->state.script_throw_hitbox_kbg[ti] = hit->kbg;
  batch->state.script_throw_hitbox_wsk[ti] = hit->wsk;
  batch->state.script_throw_hitbox_bkb[ti] = hit->bkb;
  batch->state.script_throw_hitbox_element[ti] = hit->element;
  batch->state.script_throw_hitbox_sfx_kind[ti] = hit->sfx_kind;
  batch->state.script_throw_hitbox_sfx_severity[ti] = hit->sfx_severity;
}

static void fighter_script_reseed_restore_carried_throw_hitboxes(MslBatch* batch, size_t idx) {
  // xDF4 is fighter state, not destination-script state: Fighter_ChangeMotionState does not clear
  // it. Falcon Dive's SpecialHi script writes xDF4[1] before the catch connects, and
  // SpecialHiCatch later consumes that carried payload from DCFD4/DE854 even though its own script
  // has no set-throw-hitbox command. Slippi does not serialize xDF4, so a replay reseed must recover
  // the real hidden owner from the extracted source script rather than treating the destination
  // script as a fresh zeroed interpreter.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071E04
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DCFD4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE854
  // data/moves/falcon.json::{307,309}
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH) {
    return;
  }
  const uint16_t source_msid =
      msl_motion_state_submotion_id((uint8_t)MSL_CHAR_ID_FALCON, (uint16_t)MSL_ACT_CA_SPECIAL_HI);
  const MslScriptEventRange range = script_events_range((uint8_t)MSL_CHAR_ID_FALCON, source_msid);
  for (uint32_t i = 0u; i < range.count; i++) {
    const MslScriptEvent* event = &range.events[i];
    if (event->kind_id == (uint16_t)MSL_SCRIPT_EVENT_SET_THROW_HITBOX) {
      fighter_script_set_throw_hitbox(batch, idx, &event->payload.throw_hitbox);
    }
  }
}

static inline uint16_t fighter_script_hitbox_flags(uint32_t flags) {
  uint16_t out = (uint16_t)(MSL_HITBOX_FLAG_X42_B5 | MSL_HITBOX_FLAG_X42_B7 |
                            MSL_HITBOX_FLAG_X42_INTERACTION_VALID);
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_HIT_GROUNDED) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_HIT_AERIAL) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_ITEM_HIT_INTERACTION) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_IGNORE_FIGHTER_SCALE) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_CLANK) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_CLANK;
  }
  if ((flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_REBOUND) != 0u) {
    out |= (uint16_t)MSL_HITBOX_FLAG_REBOUND;
  }
  return out;
}

static inline void fighter_script_refresh_hitbox_count(MslBatch* batch, size_t idx) {
  uint8_t count = 0u;
  for (uint8_t hitbox_id = 0; hitbox_id < (uint8_t)MSL_MAX_HITBOXES; hitbox_id++) {
    const size_t hi = fighter_script_hitbox_index(idx, hitbox_id);
    if (batch->state.hitbox_capsule_state[hi] != (uint8_t)MSL_HITCAPSULE_DISABLED) {
      count++;
    }
  }
  batch->state.hitbox_count[idx] = count;
}

static inline void fighter_script_disable_hitbox(MslBatch* batch, size_t hi) {
  batch->state.hitbox_capsule_state[hi] = (uint8_t)MSL_HITCAPSULE_DISABLED;
  batch->state.hitbox_capsule_enabled[hi] = 0u;
  batch->state.hitbox_enabled[hi] = 0u;
  batch->state.hitbox_enable_edge[hi] = 0u;
  batch->state.hitbox_pose_create[hi] = 0u;
}

void fighter_script_disable_hitcapsules(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  for (uint8_t hitbox_id = 0; hitbox_id < (uint8_t)MSL_MAX_HITBOXES; hitbox_id++) {
    fighter_script_disable_hitbox(batch, fighter_script_hitbox_index(idx, hitbox_id));
  }
  batch->state.hitbox_count[idx] = 0u;
}

static inline void fighter_script_prepare_enable_edge(MslBatch* batch, size_t idx, size_t hi,
                                                      uint8_t group) {
  for (uint8_t source_id = 0; source_id < (uint8_t)MSL_MAX_HITBOXES; source_id++) {
    const size_t source_hi = fighter_script_hitbox_index(idx, source_id);
    if (source_hi == hi ||
        batch->state.hitbox_capsule_state[source_hi] == (uint8_t)MSL_HITCAPSULE_DISABLED ||
        batch->state.hitbox_capsule_group[source_hi] != group) {
      continue;
    }
    hitlist_capsule_copy(&batch->state.fighter_hitlist[source_hi],
                         &batch->state.fighter_hitlist[hi]);
    return;
  }
  hitlist_capsule_clear(&batch->state.fighter_hitlist[hi]);
}

static inline uint8_t fighter_script_apply_create(MslBatch* batch, size_t idx,
                                                  const MslScriptCreateHitboxPayload* hit) {
  if (hit->hitbox_id >= (uint8_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t hi = fighter_script_hitbox_index(idx, hit->hitbox_id);
  const uint8_t group = (uint8_t)(hit->hit_group & 7u);
  const uint8_t enable_edge =
      (uint8_t)(batch->state.hitbox_capsule_state[hi] == (uint8_t)MSL_HITCAPSULE_DISABLED ||
                batch->state.hitbox_capsule_group[hi] != group);
  if (enable_edge != 0u) {
    batch->state.hitbox_capsule_group[hi] = group;
    batch->state.hitbox_capsule_state[hi] = (uint8_t)MSL_HITCAPSULE_ENABLED;
    fighter_script_prepare_enable_edge(batch, idx, hi, group);
  }

  // ftAction_8007121C stores offsets in HitCapsule.b_offset component order.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  batch->state.hitbox_offset_x[hi] = hit->z_offset;
  batch->state.hitbox_offset_y[hi] = hit->y_offset;
  batch->state.hitbox_offset_z[hi] = hit->x_offset;
  batch->state.hitbox_bone_part_id[hi] = hit->bone;
  batch->state.hitbox_radius[hi] = hit->size;
  batch->state.hitbox_damage[hi] = hit->damage;
  batch->state.hitbox_angle[hi] = hit->angle;
  batch->state.hitbox_kbg[hi] = hit->kbg;
  batch->state.hitbox_wsk[hi] = hit->wsk;
  batch->state.hitbox_bkb[hi] = hit->bkb;
  batch->state.hitbox_element[hi] = hit->element;
  batch->state.hitbox_shield_damage[hi] = hit->shield_damage;
  batch->state.hitbox_sfx_severity[hi] = hit->sfx_severity;
  batch->state.hitbox_sfx_kind[hi] = hit->sfx_kind;

  const uint16_t flags = fighter_script_hitbox_flags(hit->flags);
  const uint16_t hitlist_meta = (uint16_t)((uint16_t)hit->rehit_frames | ((uint16_t)group << 8));
  batch->state.hitbox_flags[hi] = flags;
  batch->state.hitbox_u16_0[hi] = hit->angle;
  batch->state.hitbox_u16_1[hi] = hit->kbg;
  batch->state.hitbox_u16_2[hi] = hit->wsk;
  batch->state.hitbox_u16_3[hi] = hit->bkb;
  batch->state.hitbox_u16_4[hi] =
      (uint16_t)((uint16_t)hit->element | ((uint16_t)(uint8_t)hit->shield_damage << 8));
  batch->state.hitbox_u16_5[hi] =
      (uint16_t)((uint16_t)hit->sfx_severity | ((uint16_t)hit->sfx_kind << 8));
  batch->state.hitbox_u16_6[hi] = flags;
  batch->state.hitbox_u16_7[hi] = hitlist_meta;
  batch->state.hitbox_x43_b2[hi] = 0u;
  batch->state.hitbox_only_hit_grabbed[hi] =
      (hit->flags & (uint32_t)MSL_SCRIPT_CREATE_HITBOX_FLAG_ONLY_HIT_GRABBED) != 0u ? 1u : 0u;
  batch->state.hitbox_capsule_enabled[hi] = 1u;
  batch->state.hitbox_enable_edge[hi] = enable_edge;
  batch->state.hitbox_pose_create[hi] = 1u;
  batch->state.hitbox_stale_damage_valid[hi] = 1u;
  batch->state.hitbox_stale_damage_mul[hi] =
      staling_multiplier_for_move(batch, idx, staling_move_id_from_state(batch, idx));
  return enable_edge;
}

static inline uint8_t fighter_script_apply_event(MslBatch* batch, size_t idx,
                                                 const MslScriptEvent* event,
                                                 uint8_t apply_kinetic_transitions) {
  switch ((MslScriptEventKind)event->kind_id) {
    case MSL_SCRIPT_EVENT_CREATE_HITBOX:
      return fighter_script_apply_create(batch, idx, &event->payload.create_hitbox);
    case MSL_SCRIPT_EVENT_SET_HITBOX_DAMAGE:
      if (event->payload.hitbox_damage.idx < (uint8_t)MSL_MAX_HITBOXES) {
        const size_t hi = fighter_script_hitbox_index(idx, event->payload.hitbox_damage.idx);
        batch->state.hitbox_damage[hi] = event->payload.hitbox_damage.damage;
        batch->state.hitbox_stale_damage_valid[hi] = 1u;
        batch->state.hitbox_stale_damage_mul[hi] =
            staling_multiplier_for_move(batch, idx, staling_move_id_from_state(batch, idx));
      }
      break;
    case MSL_SCRIPT_EVENT_SET_HITBOX_SIZE:
      if (event->payload.hitbox_size.idx < (uint8_t)MSL_MAX_HITBOXES) {
        batch->state
            .hitbox_radius[fighter_script_hitbox_index(idx, event->payload.hitbox_size.idx)] =
            event->payload.hitbox_size.size;
      }
      break;
    case MSL_SCRIPT_EVENT_SET_HITBOX_INTERACTION:
      if (event->payload.hitbox_interaction.idx < (uint8_t)MSL_MAX_HITBOXES &&
          event->payload.hitbox_interaction.type <= 1u) {
        const size_t hi = fighter_script_hitbox_index(idx, event->payload.hitbox_interaction.idx);
        const uint16_t bit = (uint16_t)(1u << event->payload.hitbox_interaction.type);
        batch->state.hitbox_flags[hi] |= (uint16_t)MSL_HITBOX_FLAG_X42_INTERACTION_VALID;
        if (event->payload.hitbox_interaction.value != 0u) {
          batch->state.hitbox_flags[hi] |= bit;
        } else {
          batch->state.hitbox_flags[hi] &= (uint16_t)~bit;
        }
        batch->state.hitbox_u16_6[hi] = batch->state.hitbox_flags[hi];
      }
      break;
    case MSL_SCRIPT_EVENT_REMOVE_HITBOX:
      if (event->payload.hitbox_remove.idx < (uint8_t)MSL_MAX_HITBOXES) {
        const size_t hi = fighter_script_hitbox_index(idx, event->payload.hitbox_remove.idx);
        const uint8_t changed =
            batch->state.hitbox_capsule_state[hi] != (uint8_t)MSL_HITCAPSULE_DISABLED;
        fighter_script_disable_hitbox(batch, hi);
        return changed;
      }
      break;
    case MSL_SCRIPT_EVENT_CLEAR_HITBOXES:
      fighter_script_disable_hitcapsules(batch, idx);
      return 0u;
    case MSL_SCRIPT_EVENT_SET_CMD_VAR:
      if (event->payload.cmd_var.idx < 4u) {
        batch->state.script_cmd_vars[fighter_script_cmd_index(idx, event->payload.cmd_var.idx)] =
            event->payload.cmd_var.value;
      }
      break;
    case MSL_SCRIPT_EVENT_SET_THROW_FLAGS:
      if (event->payload.throw_flags.hit_idx == 0u) {
        batch->state.script_throw_flags[idx] |= 1u << 3;
      } else if (event->payload.throw_flags.hit_idx == 1u) {
        batch->state.script_throw_flags[idx] |= 1u << 4;
      }
      break;
    case MSL_SCRIPT_EVENT_ALLOW_INTERRUPT:
      batch->state
          .state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX] |=
          (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
      break;
    case MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE:
      batch->state.script_throw_flags[idx] |= 1u;
      break;
    case MSL_SCRIPT_EVENT_SET_AIRBORNE_STATE:
      if (apply_kinetic_transitions != 0u) {
        fighter_script_set_airborne_state(batch, idx, event->payload.state.state);
      }
      break;
    case MSL_SCRIPT_EVENT_SET_HIT_STATUS:
      batch->state.script_hit_status_x1988[idx] = event->payload.state.state;
      break;
    case MSL_SCRIPT_EVENT_SET_ALL_HURT_STATE:
      fighter_script_set_all_hurtcaps(batch, idx, event->payload.state.state);
      break;
    case MSL_SCRIPT_EVENT_SET_HURT_STATE:
      fighter_script_set_hurtcap_bone(batch, idx, event->payload.hurt_state.bone_idx,
                                      event->payload.hurt_state.state);
      break;
    case MSL_SCRIPT_EVENT_SET_JAB_COMBO:
      if (event->payload.jab_combo.disabled == 0u) {
        batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                 (size_t)MSL_STATE_FLAGS_2218_INDEX] |=
            (uint8_t)MSL_STATE_FLAG_2218_B1;
      }
      break;
    case MSL_SCRIPT_EVENT_SET_JAB_RAPID: {
      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      if (event->payload.state.state != 0u) {
        batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_2218_B2;
      } else {
        batch->state.state_flags[flags_i] &= (uint8_t)~MSL_STATE_FLAG_2218_B2;
      }
    } break;
    case MSL_SCRIPT_EVENT_SET_STATE_FLAGS_221C_U16_Y:
      batch->state.script_state_flags_221c_y[idx] = event->payload.state_flags_221c.flags;
      break;
    case MSL_SCRIPT_EVENT_TOGGLE_BONE_PHYSICS:
      if (event->payload.bone.bone_id < 64u) {
        batch->state.script_bone_physics_mask[idx] ^= UINT64_C(1) << event->payload.bone.bone_id;
      }
      break;
    case MSL_SCRIPT_EVENT_START_SMASH_CHARGE:
      batch->state.smash_charge_state[idx] = 1u;
      batch->state.smash_charge_frames[idx] = 0u;
      batch->state.smash_charge_hold_frames_max[idx] = event->payload.smash_charge.hold_frames;
      batch->state.smash_charge_damage_mul[idx] = event->payload.smash_charge.damage_mul;
      break;
    case MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX:
      if (!batch->debug_rng_disable_pseudo_random_sfx_cmd &&
          event->payload.pseudo_random_sfx.random_range != 0u) {
        // Opcode 38 consumes RNG at command interpretation, before the installed Anim callback.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
        // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
        const int players = (int)batch->config.num_players;
        const int bi = players > 0 ? (int)(idx / (size_t)players) : 0;
        (void)combat_rng_consume_randi_site(
            batch, bi, MSL_RNG_SITE_FTACTION_PSEUDO_RANDOM_SFX_CMD,
            (uint32_t)event->payload.pseudo_random_sfx.random_range);
      }
      break;
    case MSL_SCRIPT_EVENT_SET_THROW_HITBOX:
      fighter_script_set_throw_hitbox(batch, idx, &event->payload.throw_hitbox);
      break;
    default:
      // Audio/visual commands do not publish gameplay state in the supported simulator domain.
      break;
  }
  return 0u;
}

static inline void fighter_script_clear_frame_edges(MslBatch* batch, size_t idx) {
  for (uint8_t hitbox_id = 0; hitbox_id < (uint8_t)MSL_MAX_HITBOXES; hitbox_id++) {
    const size_t hi = fighter_script_hitbox_index(idx, hitbox_id);
    batch->state.hitbox_enable_edge[hi] = 0u;
    batch->state.hitbox_pose_create[hi] = 0u;
  }
}

static float fighter_script_live_rate(MslBatch* batch, size_t idx) {
  const int32_t rate_fp = batch->state.frame_speed_mul_fp_q16_16[idx];
  float rate = batch->state.script_rate_f32[idx];
  if (!isfinite(rate) || msl_q16_16_from_f32(rate) != rate_fp) {
    // Most rate writes go through anim_timebase, which retains the exact source float. A few
    // callback owners still publish the fixed-point lane directly; resynchronize those here so
    // the command interpreter never advances on a stale rate.
    rate = msl_f32_from_q16_16(rate_fp);
    batch->state.script_rate_f32[idx] = rate;
  }
  return rate;
}

static float fighter_script_schedule_timer(const MslScriptEvent* event, float timer,
                                           float frame_count) {
  if (event == NULL) {
    return INFINITY;
  }
  switch (event->timer_kind) {
    case 1u:
      // SynchronousTimer adds to the live residual.
      // refs/melee/src/melee/lb/lbcommand.c::Command_01
      return timer + (float)event->timer_value;
    case 2u:
      // AsynchronousTimer discards the residual and targets the live command frame.
      // refs/melee/src/melee/lb/lbcommand.c::Command_02
      return (float)event->timer_value - frame_count;
    default:
      // No intervening wait: the next command executes in this interpreter pass.
      return timer;
  }
}

static uint8_t fighter_script_consume_due(MslBatch* batch, size_t idx, MslScriptEventRange range,
                                          uint8_t apply_kinetic_transitions) {
  uint32_t cursor = batch->state.script_event_cursor[idx];
  float timer = batch->state.script_timer_f32[idx];
  uint8_t hitbox_count_changed = 0u;

  while (cursor < range.count && timer <= 0.0f) {
    do {
      hitbox_count_changed |=
          fighter_script_apply_event(batch, idx, &range.events[cursor], apply_kinetic_transitions);
      cursor++;
    } while (cursor < range.count && range.events[cursor].timer_kind == 0u);

    timer = cursor < range.count
                ? fighter_script_schedule_timer(&range.events[cursor], timer,
                                                batch->state.script_frame_count_f32[idx])
                : INFINITY;
  }

  batch->state.script_event_cursor[idx] = cursor;
  batch->state.script_timer_f32[idx] = timer;
  return hitbox_count_changed;
}

static uint8_t fighter_script_reset_stream(MslBatch* batch, size_t idx, MslScriptEventRange range,
                                           float interpreter_rate,
                                           uint8_t apply_kinetic_transitions) {
  batch->state.script_event_cursor[idx] = 0u;
  batch->state.script_frame_count_f32[idx] = 0.0f;
  if (range.count == 0u) {
    batch->state.script_timer_f32[idx] = INFINITY;
    return 0u;
  }

  // Fighter_ChangeMotionState invokes ftAction once at the entry AObj frame. Its initial timer is
  // zero and ftAction subtracts the live rate before interpreting the first control command.
  // An asynchronous timer replaces that negative residual; a synchronous timer retains it.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
  const float initial_timer = -interpreter_rate;
  batch->state.script_timer_f32[idx] = fighter_script_schedule_timer(
      &range.events[0], initial_timer, batch->state.script_frame_count_f32[idx]);
  return fighter_script_consume_due(batch, idx, range, apply_kinetic_transitions);
}

static uint8_t fighter_script_rebuild_to_frame(MslBatch* batch, size_t idx,
                                               MslScriptEventRange range, int32_t frame_fp,
                                               uint8_t apply_kinetic_transitions) {
  const float live_rate = fighter_script_live_rate(batch, idx);
  // A frozen replay boundary exposes the post-command AObj frame but not the rate that reached
  // it. Persistent command products can still be reconstructed by seeking the extracted command
  // stream at the unit timebase; normal free-running freezes retain their already-live timer.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
  const float rebuild_rate = live_rate > 0.0f ? live_rate : 1.0f;
  uint8_t hitbox_count_changed =
      fighter_script_reset_stream(batch, idx, range, rebuild_rate, apply_kinetic_transitions);
  if (frame_fp <= 0) {
    return hitbox_count_changed;
  }

  // Fighter_ChangeMotionState and replay reseed can enter an action at a nonzero AObj frame.
  // Re-run the bounded source timer rather than deriving each event independently from the
  // absolute frame; this preserves f32 residual carry across the whole command stream.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
  const float frame = msl_f32_from_q16_16(frame_fp);
  int32_t ticks = (int32_t)lroundf(frame / rebuild_rate);
  if (ticks < 0) {
    ticks = 0;
  } else if (ticks > 512) {
    ticks = 512;
  }
  for (int32_t tick = 0; tick < ticks; tick++) {
    if (batch->state.script_event_cursor[idx] >= range.count) {
      break;
    }
    batch->state.script_frame_count_f32[idx] += rebuild_rate;
    batch->state.script_timer_f32[idx] -= rebuild_rate;
    hitbox_count_changed |=
        fighter_script_consume_due(batch, idx, range, apply_kinetic_transitions);
  }
  return hitbox_count_changed;
}

static void fighter_script_execute_through(MslBatch* batch, size_t idx, int32_t frame_fp,
                                           uint8_t apply_kinetic_transitions) {
  const uint16_t msid = batch->state.script_msid[idx];
  if (frame_fp == batch->state.script_last_frame_fp_q16_16[idx]) {
    return;
  }
  if (msid == UINT16_MAX) {
    batch->state.script_last_frame_fp_q16_16[idx] = frame_fp;
    return;
  }

  MslScriptEventRange range = script_events_range(batch->state.char_id[idx], msid);
  uint8_t hitbox_count_changed = 0u;
  const int32_t last_frame_fp = batch->state.script_last_frame_fp_q16_16[idx];
  if (last_frame_fp == INT32_MIN || frame_fp < last_frame_fp) {
    // Initial nonzero seek and AObj loop restart both rebuild the one source interpreter. The
    // command outputs remain live; replay reseed separately normalizes callback-consumed pulses.
    hitbox_count_changed |=
        fighter_script_rebuild_to_frame(batch, idx, range, frame_fp, apply_kinetic_transitions);
  } else {
    const float rate = fighter_script_live_rate(batch, idx);
    batch->state.script_frame_count_f32[idx] += rate;
    batch->state.script_timer_f32[idx] -= rate;
    hitbox_count_changed |=
        fighter_script_consume_due(batch, idx, range, apply_kinetic_transitions);
  }
  batch->state.script_last_frame_fp_q16_16[idx] = frame_fp;
  if (hitbox_count_changed != 0u) {
    fighter_script_refresh_hitbox_count(batch, idx);
  }
}

static void fighter_script_enter_mode(MslBatch* batch, size_t idx,
                                      uint8_t apply_kinetic_transitions) {
  if (batch == NULL) {
    return;
  }
  const uint32_t anim = batch->state.animation_index[idx];
  batch->state.script_msid[idx] = anim <= (uint32_t)UINT16_MAX ? (uint16_t)anim : UINT16_MAX;
  batch->state.script_event_cursor[idx] = 0u;
  batch->state.script_last_frame_fp_q16_16[idx] = INT32_MIN;
  for (uint8_t i = 0u; i < 4u; i++) {
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, i)] = 0u;
  }
  fighter_script_clear_frame_edges(batch, idx);

  // Fighter_ChangeMotionState immediately runs the destination command stream: frame-zero entry
  // uses ftAction_80073240, while nonzero anim_start uses ftAction_80073354 to seek through it.
  // Contact later in the same frame therefore observes entry commands (notably reflector and
  // frame-zero HitCapsules) even when the transition occurred after the ordinary Anim callback.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80073240,ftAction_80073354}
  fighter_script_execute_through(batch, idx, batch->state.anim_frame_fp_q16_16[idx],
                                 apply_kinetic_transitions);
}

void fighter_script_prepare_motion_entry(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  const uint16_t action = batch->state.action_id[idx];
  const uint8_t common_attack_clears_allow =
      (uint8_t)((action >= (uint16_t)MSL_ACT_ATTACK_11 && action <= (uint16_t)MSL_ACT_ATTACK_13) ||
                (action >= (uint16_t)MSL_ACT_ATTACK_DASH &&
                 action <= (uint16_t)MSL_ACT_ATTACK_AIR_LW) ||
                action == (uint16_t)MSL_ACT_APPEAL_SR || action == (uint16_t)MSL_ACT_APPEAL_SL);
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  if (common_attack_clears_allow != 0u) {
    // These common entry helpers clear fp->allow_interrupt immediately before
    // Fighter_ChangeMotionState. The field is otherwise persistent across motion changes.
    // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Attack1.c,ftCo_AttackDash.c,
    //   ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_AttackS4.c,
    //   ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackAir.c,ftCo_AppealS.c}
    batch->state.state_flags[flags_i] &= (uint8_t)~MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
  }

  if (action == (uint16_t)MSL_ACT_ATTACK_11) {
    // checkAttack11 clears both script-owned jab gates after the immediate entry animation tick.
    // Clearing here is equivalent for all later source phases and avoids a second flag owner.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::checkAttack11
    batch->state.state_flags[flags_i] &=
        (uint8_t) ~(MSL_STATE_FLAG_2218_B1 | MSL_STATE_FLAG_2218_B2);
  } else if (action == (uint16_t)MSL_ACT_ATTACK_12 || action == (uint16_t)MSL_ACT_ATTACK_13) {
    // doAttack12Normal/doAttack13 clear the combo gate but preserve the rapid-jab gate.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{doAttack12Normal,doAttack13}
    batch->state.state_flags[flags_i] &= (uint8_t)~MSL_STATE_FLAG_2218_B1;
  }

  if (msl_motion_state_common_class_has_fast(action, MSL_MS_CLASS_ATTACK_AIR)) {
    // ftCo_AttackAir_EnterFromMsid explicitly clears cmd_vars[0] and throw_flags.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, 0u)] = 0u;
    batch->state.script_throw_flags[idx] = 0u;
  } else if (action == (uint16_t)MSL_ACT_ATTACK_100_START) {
    // ftCo_800D6B00 clears the complete throw-flags byte before changing motion.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D6B00
    batch->state.script_throw_flags[idx] = 0u;
  }

  if (action == (uint16_t)MSL_ACT_RUN_BRAKE) {
    // ftCo_RunBrake_Enter clears both callback command lanes and initializes its freeze latch.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Enter
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, 0u)] = 0u;
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, 1u)] = 0u;
    batch->state.runbrake_freeze_x0[idx] = 0u;
  } else if (action == (uint16_t)MSL_ACT_TURN_RUN) {
    // ftCo_TurnRun_Enter clears cmd_vars[1] and initializes mv.co.turnrun.x14. facing_dir1 is the
    // central motion-entry copy of the pre-turn facing stored by source as turnrun.accel_mul.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Enter
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, 1u)] = 0u;
    batch->state.turnrun_x14[idx] = 0u;
  }
}

void fighter_script_enter(MslBatch* batch, size_t idx) {
  fighter_script_enter_mode(batch, idx, 1u);
}

void fighter_script_reseed(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.script_hit_status_x1988[idx] = 0u;
  fighter_script_set_all_hurtcaps(batch, idx, 0u);
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.script_state_flags_221c_y[idx] =
      (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE) != 0u ? 4u : 0u;
  batch->state.script_bone_physics_mask[idx] = 0u;
  for (uint8_t throw_id = 0u; throw_id < 2u; throw_id++) {
    batch->state.script_throw_hitbox_valid[fighter_script_throw_index(idx, throw_id)] = 0u;
  }
  for (uint8_t i = 0u; i < 4u; i++) {
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, i)] = 0u;
  }
  batch->state.script_throw_flags[idx] = 0u;
  // The replay seed is already the post-command fighter snapshot for this script time. Seek the
  // command stream to reconstruct persistent interpreter outputs (HitCapsules, cmd vars, hurt
  // state, throw data), but do not replay historical kinetic transitions into authoritative
  // position/velocity/jump/ECB seed lanes. A real nonzero-start ChangeMotionState still takes the
  // ordinary entry path above and executes those commands causally through ftAction_80073354.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80073240,ftAction_80073354}
  fighter_script_enter_mode(batch, idx, 0u);
  fighter_script_reseed_restore_carried_throw_hitboxes(batch, idx);
  // Slippi's fp+0x221C byte exposes the T bit of x221C_u16_y. It is authoritative hidden source
  // state at a replay boundary: a frame-preserving ChangeMotionState may have cleared the field
  // without replaying an earlier opcode-52 command in the destination stream. Preserve that
  // exposed bit after seeking the cursor; free-running entry and advancement remain purely causal.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  if ((batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE) != 0u) {
    batch->state.script_state_flags_221c_y[idx] |= 4u;
  } else {
    batch->state.script_state_flags_221c_y[idx] &= (uint16_t)~4u;
  }
  // Throw flags are command pulses consumed by the source Anim callback, not persistent command
  // state. A post-frame replay seed may reconstruct the cursor and throw payloads, but must not
  // replay every historical pulse at the next callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  batch->state.script_throw_flags[idx] = 0u;

  // cmd_vars[2] is a callback-consumed pulse for Fox/Falco Blaster and Illusion/Phantasm. Seeking
  // the command stream reconstructs that historical set command, but a Slippi post-frame seed is
  // after the corresponding Anim/accessory callback has cleared it. Normalize the real callback
  // field here; free-running advancement still sets and consumes the pulse causally.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialN_CreateBlasterShot,ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim}
  const uint8_t fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]);
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_N_LOOP ||
      fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP ||
      fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S || fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S) {
    batch->state.script_cmd_vars[fighter_script_cmd_index(idx, 2u)] = 0u;
  }
}

void fighter_script_advance_fighter(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t msid = anim <= (uint32_t)UINT16_MAX ? (uint16_t)anim : UINT16_MAX;
  if (batch->state.script_msid[idx] != msid) {
    fighter_script_enter(batch, idx);
  }
  fighter_script_clear_frame_edges(batch, idx);
  fighter_script_execute_through(batch, idx, batch->state.anim_frame_fp_q16_16[idx], 1u);
}

void fighter_script_advance(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      fighter_script_advance_fighter(batch, bi, p);
    }
  }
}

void fighter_script_tick_once(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t msid = anim <= (uint32_t)UINT16_MAX ? (uint16_t)anim : UINT16_MAX;
  if (batch->state.script_msid[idx] != msid) {
    fighter_script_enter(batch, idx);
  }
  fighter_script_execute_through(batch, idx, batch->state.anim_frame_fp_q16_16[idx], 1u);
}

uint8_t fighter_script_throw_hitbox_params(const MslBatch* batch, size_t idx, uint8_t throw_id,
                                           MslThrowHitboxParams* out) {
  if (batch == NULL || out == NULL || throw_id >= 2u) {
    return 0u;
  }
  const size_t ti = fighter_script_throw_index(idx, throw_id);
  if (batch->state.script_throw_hitbox_valid[ti] == 0u) {
    return 0u;
  }
  *out = (MslThrowHitboxParams){
      .damage = batch->state.script_throw_hitbox_damage[ti],
      .angle = batch->state.script_throw_hitbox_angle[ti],
      .kbg = batch->state.script_throw_hitbox_kbg[ti],
      .wsk = batch->state.script_throw_hitbox_wsk[ti],
      .bkb = batch->state.script_throw_hitbox_bkb[ti],
      .element = batch->state.script_throw_hitbox_element[ti],
      .sfx_kind = batch->state.script_throw_hitbox_sfx_kind[ti],
      .sfx_severity = batch->state.script_throw_hitbox_sfx_severity[ti],
  };
  return 1u;
}

uint8_t fighter_script_command_due_next_tick(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.script_msid[idx] == UINT16_MAX) {
    return 0u;
  }
  const MslScriptEventRange range =
      script_events_range(batch->state.char_id[idx], batch->state.script_msid[idx]);
  if (batch->state.script_event_cursor[idx] >= range.count) {
    return 0u;
  }
  float rate = batch->state.script_rate_f32[idx];
  if (!isfinite(rate) || msl_q16_16_from_f32(rate) != batch->state.frame_speed_mul_fp_q16_16[idx]) {
    rate = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
  }
  return rate > 0.0f && batch->state.script_timer_f32[idx] - rate <= 0.0f ? 1u : 0u;
}

uint8_t fighter_script_consumed_throw_release(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.script_msid[idx] == UINT16_MAX) {
    return 0u;
  }
  const MslScriptEventRange range =
      script_events_range(batch->state.char_id[idx], batch->state.script_msid[idx]);
  uint32_t consumed = batch->state.script_event_cursor[idx];
  if (consumed > range.count) {
    consumed = range.count;
  }
  for (uint32_t event_i = 0u; event_i < consumed; event_i++) {
    const MslScriptEvent* event = &range.events[event_i];
    if (event->kind_id == (uint16_t)MSL_SCRIPT_EVENT_SET_THROW_FLAGS &&
        event->payload.throw_flags.hit_idx == 0u) {
      return 1u;
    }
  }
  return 0u;
}
