#include "move_tables.h"
#include "char_registry.h"

#include <limits.h>
#include <string.h>

#include "action_ids.h"
#include "hurtcaps_tables.h"
#include "script_events.h"

enum {
  MSL_SPECIAL_CMD0_LATCH_CLEAR_TAIL_FRAMES = 2,
  MSL_MOVE_TABLE_CHAR_COUNT = MSL_CHAR_REGISTRY_COUNT,
  MSL_MOVE_TABLE_MSID_CAP = 512,
  MSL_MOVE_TABLE_FRAME_CAP = 240,
  MSL_MOVE_TABLE_CMD_VAR_COUNT = 4,
  MSL_MOVE_TABLE_HITBOX_CAP = 4,
  MSL_MOVE_TABLE_THROW_HITBOX_CAP = 4,
  MSL_MOVE_TABLE_PULSE_CAP = 16,
  MSL_MOVE_TABLE_SFX_PULSE_CAP = 16,
  MSL_MOVE_TABLE_NO_AIRBORNE_EVENT = 0xFF,
};

typedef struct MslMoveTableCachedHitbox {
  MslThrowHitboxParams params;
  uint8_t loaded;
} MslMoveTableCachedHitbox;

typedef struct MslMoveTableCache {
  MslScriptFrameWindow cmd_var_value1_closed[MSL_MOVE_TABLE_CMD_VAR_COUNT];
  MslScriptFrameWindow cmd_var_value1_open[MSL_MOVE_TABLE_CMD_VAR_COUNT];
  MslScriptFrameWindow allow_interrupt;
  MslScriptFrameWindow hitbox_lifetime;
  MslScriptFrameWindow first_create_hitbox;
  MslScriptFrameWindow second_create_hitbox;
  MslScriptFrameWindow last_create_hitbox;
  MslScriptFrameWindow post_clear_create_hitbox;
  MslScriptFrameWindow throw_flags_any;
  MslScriptFrameWindow throw_flags_hit[MSL_MOVE_TABLE_THROW_HITBOX_CAP];
  MslScriptFrameWindow catchattack_grabbed_hit;
  MslScriptFrameWindow jab_rapid;
  uint16_t cmd_var_value1_pulses[MSL_MOVE_TABLE_CMD_VAR_COUNT][MSL_MOVE_TABLE_PULSE_CAP];
  uint8_t cmd_var_value1_pulse_count[MSL_MOVE_TABLE_CMD_VAR_COUNT];
  uint16_t cmd_var_value0_pulses[MSL_MOVE_TABLE_CMD_VAR_COUNT][MSL_MOVE_TABLE_PULSE_CAP];
  uint8_t cmd_var_value0_pulse_count[MSL_MOVE_TABLE_CMD_VAR_COUNT];
  uint16_t throw_flags_pulses[MSL_MOVE_TABLE_THROW_HITBOX_CAP][MSL_MOVE_TABLE_PULSE_CAP];
  uint8_t throw_flags_pulse_count[MSL_MOVE_TABLE_THROW_HITBOX_CAP];
  uint16_t projectile_pulses[MSL_MOVE_TABLE_PULSE_CAP];
  uint8_t projectile_pulse_count;
  uint16_t sfx_pulse_frames[MSL_MOVE_TABLE_SFX_PULSE_CAP];
  uint8_t sfx_pulse_ranges[MSL_MOVE_TABLE_SFX_PULSE_CAP];
  uint8_t sfx_pulse_count;
  uint32_t hurtbox_can_hit_mask[MSL_MOVE_TABLE_FRAME_CAP];
  uint8_t hit_status[MSL_MOVE_TABLE_FRAME_CAP];
  uint8_t airborne_state_event[MSL_MOVE_TABLE_FRAME_CAP];
  uint8_t state_flags_221c_y[MSL_MOVE_TABLE_FRAME_CAP];
  MslMoveTableCachedHitbox throw_hitboxes[MSL_MOVE_TABLE_THROW_HITBOX_CAP];
  int16_t jab_combo_on_frame;
  int16_t smash_charge_frame;
  float smash_charge_damage_mul;
  uint8_t smash_charge_hold_frames;
} MslMoveTableCache;

static MslMoveTableCache g_move_cache[MSL_MOVE_TABLE_CHAR_COUNT][MSL_MOVE_TABLE_MSID_CAP];
static int g_move_cache_loaded = 0;

static int char_slot(uint8_t char_id) {
  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    if (MSL_CHAR_REGISTRY[ci].char_id == char_id) {
      return ci;
    }
  }
  return -1;
}

static const MslMoveTableCache* move_cache_get(uint8_t char_id, uint16_t msid) {
  const int slot = char_slot(char_id);
  if (slot < 0 || msid >= (uint16_t)MSL_MOVE_TABLE_MSID_CAP) {
    return NULL;
  }
  return &g_move_cache[slot][msid];
}

static uint8_t frame_crossed_u16(uint16_t frame, float prev_frame, float cur_frame) {
  const float on = (float)frame;
  return (prev_frame < on && cur_frame >= on) ? 1u : 0u;
}

static uint8_t frame_window_contains(MslScriptFrameWindow win, float frame) {
  return (win.loaded && frame >= (float)win.start_af && frame < (float)win.end_af) ? 1u : 0u;
}

static uint8_t frame_window_crossed(MslScriptFrameWindow win, float prev_frame, float cur_frame) {
  return (win.loaded && frame_crossed_u16((uint16_t)win.start_af, prev_frame, cur_frame)) ? 1u : 0u;
}

static void add_unique_pulse(uint16_t* frames, uint8_t* count, uint8_t cap, uint16_t frame) {
  if (frames == NULL || count == NULL || *count >= cap) {
    return;
  }
  for (uint8_t i = 0; i < *count; i++) {
    if (frames[i] == frame) {
      return;
    }
  }
  frames[*count] = frame;
  *count = (uint8_t)(*count + 1u);
}

static uint32_t mask_low_bits_u32(uint16_t n) {
  if (n >= 32u) {
    return 0xFFFFFFFFu;
  }
  return (n == 0u) ? 0u : ((1u << n) - 1u);
}

static void move_cache_build_for_msid(uint8_t char_id, uint16_t msid, MslMoveTableCache* cache) {
  if (cache == NULL) {
    return;
  }

  cache->jab_combo_on_frame = -1;
  cache->smash_charge_frame = -1;
  cache->smash_charge_damage_mul = 1.0f;
  for (uint16_t frame = 0; frame < (uint16_t)MSL_MOVE_TABLE_FRAME_CAP; frame++) {
    cache->hurtbox_can_hit_mask[frame] = 0xFFFFFFFFu;
    cache->airborne_state_event[frame] = (uint8_t)MSL_MOVE_TABLE_NO_AIRBORNE_EVENT;
  }

  for (uint8_t idx = 0; idx < MSL_MOVE_TABLE_CMD_VAR_COUNT; idx++) {
    (void)script_events_cmd_var_value_window(char_id, msid, idx, 1u, 0u,
                                             &cache->cmd_var_value1_closed[idx]);
    (void)script_events_cmd_var_value_window(char_id, msid, idx, 1u, 1u,
                                             &cache->cmd_var_value1_open[idx]);
  }
  (void)script_events_allow_interrupt_window(char_id, msid, &cache->allow_interrupt);
  (void)script_events_hitbox_lifetime(char_id, msid, &cache->hitbox_lifetime);
  (void)script_events_first_create_hitbox_phase(char_id, msid, &cache->first_create_hitbox);
  (void)script_events_second_create_hitbox_phase(char_id, msid, &cache->second_create_hitbox);
  (void)script_events_last_create_hitbox_phase(char_id, msid, &cache->last_create_hitbox);
  (void)script_events_post_clear_create_hitbox_phase(char_id, msid,
                                                     &cache->post_clear_create_hitbox);
  (void)script_events_throw_flags_window(char_id, msid, 0u, 0u, &cache->throw_flags_any);
  (void)script_events_catchattack_grabbed_hit_window(char_id, msid,
                                                     &cache->catchattack_grabbed_hit);

  for (uint8_t hit_idx = 0; hit_idx < MSL_MOVE_TABLE_THROW_HITBOX_CAP; hit_idx++) {
    (void)script_events_throw_flags_window(char_id, msid, hit_idx, 1u,
                                           &cache->throw_flags_hit[hit_idx]);
  }

  const MslScriptEvent* smash =
      script_events_first(char_id, msid, MSL_SCRIPT_EVENT_START_SMASH_CHARGE);
  if (smash != NULL) {
    cache->smash_charge_frame = (int16_t)smash->frame;
    cache->smash_charge_hold_frames = smash->payload.smash_charge.hold_frames;
    if (smash->payload.smash_charge.damage_mul > 0.0f) {
      cache->smash_charge_damage_mul = smash->payload.smash_charge.damage_mul;
    }
  }

  const MslScriptEventRange range = script_events_range(char_id, msid);
  int jab_rapid_on = -1;
  int jab_rapid_off = -1;
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    switch ((MslScriptEventKind)ev->kind_id) {
      case MSL_SCRIPT_EVENT_SET_CMD_VAR:
        if (ev->payload.cmd_var.idx < MSL_MOVE_TABLE_CMD_VAR_COUNT) {
          const uint8_t idx = ev->payload.cmd_var.idx;
          if (ev->payload.cmd_var.value == 1u) {
            add_unique_pulse(cache->cmd_var_value1_pulses[idx],
                             &cache->cmd_var_value1_pulse_count[idx],
                             (uint8_t)MSL_MOVE_TABLE_PULSE_CAP, ev->frame);
          } else if (ev->payload.cmd_var.value == 0u && ev->frame > 0u) {
            // Window-close pulses (e.g. Counter cmd1 / Dancing Blade cmd0 reset). Frame-0
            // initializers are entry resets, not window closes.
            add_unique_pulse(cache->cmd_var_value0_pulses[idx],
                             &cache->cmd_var_value0_pulse_count[idx],
                             (uint8_t)MSL_MOVE_TABLE_PULSE_CAP, ev->frame);
          }
        }
        break;
      case MSL_SCRIPT_EVENT_SET_JAB_COMBO:
        if (ev->payload.jab_combo.disabled == 0u && cache->jab_combo_on_frame < 0) {
          cache->jab_combo_on_frame = (int16_t)ev->frame;
        }
        break;
      case MSL_SCRIPT_EVENT_SET_JAB_RAPID:
        if (ev->payload.state.state != 0u && jab_rapid_on < 0) {
          jab_rapid_on = (int)ev->frame;
        } else if (ev->payload.state.state == 0u && jab_rapid_on >= 0 && jab_rapid_off < 0) {
          jab_rapid_off = (int)ev->frame;
        }
        break;
      case MSL_SCRIPT_EVENT_SET_THROW_FLAGS:
        if (ev->payload.throw_flags.hit_idx < MSL_MOVE_TABLE_THROW_HITBOX_CAP) {
          const uint8_t hit_idx = ev->payload.throw_flags.hit_idx;
          add_unique_pulse(cache->throw_flags_pulses[hit_idx],
                           &cache->throw_flags_pulse_count[hit_idx],
                           (uint8_t)MSL_MOVE_TABLE_PULSE_CAP, ev->frame);
        }
        break;
      case MSL_SCRIPT_EVENT_SET_THROW_HITBOX:
        if (ev->payload.throw_hitbox.idx < MSL_MOVE_TABLE_THROW_HITBOX_CAP) {
          MslMoveTableCachedHitbox* hb = &cache->throw_hitboxes[ev->payload.throw_hitbox.idx];
          hb->params.damage = ev->payload.throw_hitbox.damage;
          hb->params.angle = ev->payload.throw_hitbox.angle;
          hb->params.kbg = ev->payload.throw_hitbox.kbg;
          hb->params.wsk = ev->payload.throw_hitbox.wsk;
          hb->params.bkb = ev->payload.throw_hitbox.bkb;
          hb->params.element = ev->payload.throw_hitbox.element;
          hb->params.sfx_kind = ev->payload.throw_hitbox.sfx_kind;
          hb->params.sfx_severity = ev->payload.throw_hitbox.sfx_severity;
          hb->loaded = 1u;
        }
        break;
      case MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE:
        add_unique_pulse(cache->projectile_pulses, &cache->projectile_pulse_count,
                         (uint8_t)MSL_MOVE_TABLE_PULSE_CAP, ev->frame);
        break;
      case MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX:
        if (cache->sfx_pulse_count < (uint8_t)MSL_MOVE_TABLE_SFX_PULSE_CAP) {
          const uint8_t n = cache->sfx_pulse_count;
          cache->sfx_pulse_frames[n] = ev->frame;
          cache->sfx_pulse_ranges[n] = ev->payload.pseudo_random_sfx.random_range;
          cache->sfx_pulse_count = (uint8_t)(n + 1u);
        }
        break;
      default:
        break;
    }
  }

  uint8_t bone_to_cap[256];
  for (uint16_t i = 0; i < 256u; i++) {
    bone_to_cap[i] = 0xFFu;
  }
  const MslHurtCap* caps = NULL;
  uint16_t cap_count = 0u;
  if (hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL) {
    const uint16_t n = cap_count < 32u ? cap_count : 32u;
    for (uint16_t i = 0; i < n; i++) {
      if (caps[i].bone_part_id < 256u && bone_to_cap[caps[i].bone_part_id] == 0xFFu) {
        bone_to_cap[caps[i].bone_part_id] = (uint8_t)i;
      }
    }
  }

  uint8_t hit_status = 0u;
  uint8_t state_flags_221c_y = 0u;
  uint8_t hurt_state_by_cap[32];
  for (uint8_t i = 0; i < 32u; i++) {
    hurt_state_by_cap[i] = 0u;
  }
  for (uint16_t frame = 0; frame < (uint16_t)MSL_MOVE_TABLE_FRAME_CAP; frame++) {
    uint32_t mask = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < range.count; i++) {
      const MslScriptEvent* ev = &range.events[i];
      if (ev->frame != frame) {
        continue;
      }
      switch ((MslScriptEventKind)ev->kind_id) {
        case MSL_SCRIPT_EVENT_SET_HIT_STATUS:
          hit_status = ev->payload.state.state;
          break;
        case MSL_SCRIPT_EVENT_SET_ALL_HURT_STATE:
          for (uint8_t cap_i = 0; cap_i < 32u; cap_i++) {
            hurt_state_by_cap[cap_i] = ev->payload.state.state;
          }
          break;
        case MSL_SCRIPT_EVENT_SET_HURT_STATE: {
          const uint8_t cap_i = bone_to_cap[ev->payload.hurt_state.bone_idx];
          if (cap_i < 32u) {
            hurt_state_by_cap[cap_i] = ev->payload.hurt_state.state;
          }
          break;
        }
        case MSL_SCRIPT_EVENT_SET_AIRBORNE_STATE:
          if (ev->payload.state.state <= 2u) {
            cache->airborne_state_event[frame] = ev->payload.state.state;
          }
          break;
        case MSL_SCRIPT_EVENT_SET_STATE_FLAGS_221C_U16_Y:
          state_flags_221c_y = (uint8_t)(ev->payload.state_flags_221c.flags & 0x7u);
          break;
        default:
          break;
      }
    }
    for (uint8_t cap_i = 0; cap_i < 32u; cap_i++) {
      if (hurt_state_by_cap[cap_i] == 0u) {
        mask |= (1u << cap_i);
      } else {
        mask &= ~(1u << cap_i);
      }
    }
    cache->hit_status[frame] = hit_status;
    cache->hurtbox_can_hit_mask[frame] = mask;
    cache->state_flags_221c_y[frame] = state_flags_221c_y;
  }

  if (jab_rapid_on >= 0) {
    if (jab_rapid_off < 0) {
      jab_rapid_off = INT16_MAX;
    }
    if (jab_rapid_off >= jab_rapid_on) {
      cache->jab_rapid = (MslScriptFrameWindow){
          .start_af = (int16_t)jab_rapid_on, .end_af = (int16_t)jab_rapid_off, .loaded = 1u};
    }
  }
}

static void move_cache_build_for_char(uint8_t char_id) {
  const int slot = char_slot(char_id);
  if (slot < 0) {
    return;
  }
  for (uint16_t msid = 0; msid < (uint16_t)MSL_MOVE_TABLE_MSID_CAP; msid++) {
    move_cache_build_for_msid(char_id, msid, &g_move_cache[slot][msid]);
  }
}

int move_tables_init(void) {
  if (g_move_cache_loaded) {
    return 0;
  }
  if (script_events_init() != 0) {
    return -1;
  }
  if (hurtcaps_tables_init() != 0) {
    return -1;
  }
  memset(g_move_cache, 0, sizeof(g_move_cache));
  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    move_cache_build_for_char(MSL_CHAR_REGISTRY[ci].char_id);
  }
  g_move_cache_loaded = 1;
  return 0;
}

static inline int attackair_msid_from_action(uint16_t a, uint16_t* out) {
  if (out == NULL) {
    return 0;
  }
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
      *out = (uint16_t)MSL_SM_ATTACK_AIR_N;
      return 1;
    case MSL_ACT_ATTACK_AIR_F:
      *out = (uint16_t)MSL_SM_ATTACK_AIR_F;
      return 1;
    case MSL_ACT_ATTACK_AIR_B:
      *out = (uint16_t)MSL_SM_ATTACK_AIR_B;
      return 1;
    case MSL_ACT_ATTACK_AIR_HI:
      *out = (uint16_t)MSL_SM_ATTACK_AIR_HI;
      return 1;
    case MSL_ACT_ATTACK_AIR_LW:
      *out = (uint16_t)MSL_SM_ATTACK_AIR_LW;
      return 1;
    default:
      return 0;
  }
}

static inline int grounded_attack_msid_from_action(uint16_t a, uint16_t* out) {
  if (out == NULL) {
    return 0;
  }
  switch (a) {
    case MSL_ACT_ATTACK_11:
      *out = (uint16_t)MSL_SM_ATTACK_11;
      return 1;
    case MSL_ACT_ATTACK_12:
      *out = (uint16_t)MSL_SM_ATTACK_12;
      return 1;
    case MSL_ACT_ATTACK_13:
      *out = (uint16_t)MSL_SM_ATTACK_13;
      return 1;
    case MSL_ACT_ATTACK_DASH:
      *out = (uint16_t)MSL_SM_ATTACK_DASH;
      return 1;
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
      *out = (uint16_t)MSL_SM_ATTACK_S3;
      return 1;
    case MSL_ACT_ATTACK_HI3:
      *out = (uint16_t)MSL_SM_ATTACK_HI3;
      return 1;
    case MSL_ACT_ATTACK_LW3:
      *out = (uint16_t)MSL_SM_ATTACK_LW3;
      return 1;
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
      *out = (uint16_t)MSL_SM_ATTACK_S4;
      return 1;
    case MSL_ACT_ATTACK_HI4:
      *out = (uint16_t)MSL_SM_ATTACK_HI4;
      return 1;
    case MSL_ACT_ATTACK_LW4:
      *out = (uint16_t)MSL_SM_ATTACK_LW4;
      return 1;
    default:
      return 0;
  }
}

static inline int throw_msid_from_action(uint16_t a, uint16_t* out) {
  if (out == NULL) {
    return 0;
  }
  switch (a) {
    case MSL_ACT_THROW_F:
      *out = (uint16_t)MSL_SM_THROW_F;
      return 1;
    case MSL_ACT_THROW_B:
      *out = (uint16_t)MSL_SM_THROW_B;
      return 1;
    case MSL_ACT_THROW_HI:
      *out = (uint16_t)MSL_SM_THROW_HI;
      return 1;
    case MSL_ACT_THROW_LW:
      *out = (uint16_t)MSL_SM_THROW_LW;
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t cmd_var_active(uint8_t char_id, uint16_t msid, uint8_t idx, uint8_t open_end,
                                     float frame) {
  if (idx >= (uint8_t)MSL_MOVE_TABLE_CMD_VAR_COUNT) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  const MslScriptFrameWindow win =
      open_end ? cache->cmd_var_value1_open[idx] : cache->cmd_var_value1_closed[idx];
  return frame_window_contains(win, frame);
}

static inline uint8_t allow_interrupt_active(uint8_t char_id, uint16_t msid, float frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->allow_interrupt, frame) : 0u;
}

uint8_t move_tables_attackair_cmd0_active(uint8_t char_id, uint16_t attackair_action_id,
                                          float cur_anim_frame_f32) {
  uint16_t msid = 0;
  return attackair_msid_from_action(attackair_action_id, &msid)
             ? cmd_var_active(char_id, msid, 0u, 0u, cur_anim_frame_f32)
             : 0u;
}

uint8_t move_tables_attackair_allow_interrupt(uint8_t char_id, uint16_t attackair_action_id,
                                              float cur_anim_frame_f32) {
  uint16_t msid = 0;
  return attackair_msid_from_action(attackair_action_id, &msid)
             ? allow_interrupt_active(char_id, msid, cur_anim_frame_f32)
             : 0u;
}

uint8_t move_tables_attackair_throw_flags_b3_crossed(uint8_t char_id, uint16_t attackair_action_id,
                                                     float prev_anim_frame_f32,
                                                     float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || cache->throw_flags_pulse_count[0] == 0u) {
    return 0u;
  }
  for (uint8_t i = 0; i < cache->throw_flags_pulse_count[0]; i++) {
    if (frame_crossed_u16(cache->throw_flags_pulses[0][i], prev_anim_frame_f32,
                          cur_anim_frame_f32)) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_attackair_throw_flags_b3_crossed_fp(uint8_t char_id,
                                                        uint16_t attackair_action_id,
                                                        int32_t cur_anim_frame_fp_q16_16,
                                                        int32_t frame_speed_mul_fp_q16_16) {
  const int32_t prev_anim_frame_fp_q16_16 = cur_anim_frame_fp_q16_16 - frame_speed_mul_fp_q16_16;
  const float prev_anim_frame_f32 = (float)prev_anim_frame_fp_q16_16 / 65536.0f;
  const float cur_anim_frame_f32 = (float)cur_anim_frame_fp_q16_16 / 65536.0f;
  return move_tables_attackair_throw_flags_b3_crossed(char_id, attackair_action_id,
                                                      prev_anim_frame_f32, cur_anim_frame_f32);
}

uint8_t move_tables_attackair_hitbox_script_lifetime(uint8_t char_id, uint16_t attackair_action_id,
                                                     float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->hitbox_lifetime, cur_anim_frame_f32) : 0u;
}

uint8_t move_tables_attackair_first_hitbox_phase(uint8_t char_id, uint16_t attackair_action_id,
                                                 float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->first_create_hitbox, cur_anim_frame_f32) : 0u;
}

int16_t move_tables_attackair_first_create_hitbox_frame(uint8_t char_id,
                                                        uint16_t attackair_action_id) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return -1;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return (cache != NULL && cache->first_create_hitbox.loaded) ? cache->first_create_hitbox.start_af
                                                              : -1;
}

uint8_t move_tables_attackair_second_create_hitbox_phase(uint8_t char_id,
                                                         uint16_t attackair_action_id,
                                                         float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->second_create_hitbox, cur_anim_frame_f32)
                       : 0u;
}

int16_t move_tables_attackair_second_create_hitbox_frame(uint8_t char_id,
                                                         uint16_t attackair_action_id) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return -1;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return (cache != NULL && cache->second_create_hitbox.loaded)
             ? cache->second_create_hitbox.start_af
             : -1;
}

uint8_t move_tables_attackair_last_create_hitbox_phase(uint8_t char_id,
                                                       uint16_t attackair_action_id,
                                                       float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->last_create_hitbox, cur_anim_frame_f32) : 0u;
}

uint8_t move_tables_attackair_post_clear_create_hitbox_phase(uint8_t char_id,
                                                             uint16_t attackair_action_id,
                                                             float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->post_clear_create_hitbox, cur_anim_frame_f32)
                       : 0u;
}

uint8_t move_tables_attackair_same_group_payload_preserves_hitcapsule(uint8_t char_id,
                                                                      uint16_t attackair_action_id,
                                                                      float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!attackair_msid_from_action(attackair_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !frame_window_contains(cache->second_create_hitbox, cur_anim_frame_f32) ||
      frame_window_contains(cache->post_clear_create_hitbox, cur_anim_frame_f32)) {
    return 0u;
  }
  const int16_t second_frame = cache->second_create_hitbox.start_af;
  if (second_frame < 0) {
    return 0u;
  }
  uint8_t prior_group[MSL_MOVE_TABLE_HITBOX_CAP] = {0u};
  uint8_t prior_loaded[MSL_MOVE_TABLE_HITBOX_CAP] = {0u};
  uint8_t saw_second_create = 0u;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->frame > (uint16_t)second_frame) {
      break;
    }
    if (ev->kind_id == (uint8_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES) {
      for (uint8_t hb = 0; hb < (uint8_t)MSL_MOVE_TABLE_HITBOX_CAP; hb++) {
        prior_loaded[hb] = 0u;
      }
      continue;
    }
    if (ev->kind_id != (uint8_t)MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      continue;
    }
    const MslScriptCreateHitboxPayload* hb = &ev->payload.create_hitbox;
    if (hb->hitbox_id >= (uint8_t)MSL_MOVE_TABLE_HITBOX_CAP) {
      return 0u;
    }
    if (ev->frame < (uint16_t)second_frame) {
      prior_group[hb->hitbox_id] = hb->hit_group;
      prior_loaded[hb->hitbox_id] = 1u;
      continue;
    }
    saw_second_create = 1u;
    if (prior_loaded[hb->hitbox_id] == 0u || prior_group[hb->hitbox_id] != hb->hit_group) {
      return 0u;
    }
  }
  return saw_second_create;
}

uint8_t move_tables_grounded_attack_allow_interrupt(uint8_t char_id, uint16_t grounded_action_id,
                                                    float cur_anim_frame_f32) {
  uint16_t msid = 0;
  return grounded_attack_msid_from_action(grounded_action_id, &msid)
             ? allow_interrupt_active(char_id, msid, cur_anim_frame_f32)
             : 0u;
}

int16_t move_tables_grounded_attack_first_create_hitbox_frame(uint8_t char_id,
                                                              uint16_t grounded_action_id) {
  uint16_t msid = 0;
  if (!grounded_attack_msid_from_action(grounded_action_id, &msid)) {
    return -1;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return (cache != NULL && cache->first_create_hitbox.loaded) ? cache->first_create_hitbox.start_af
                                                              : -1;
}

uint8_t move_tables_grounded_attack_create_hitbox_payload_matches(
    uint8_t char_id, uint16_t grounded_action_id, uint8_t hitbox_id, int hitcapsule_int_dmg,
    uint16_t angle, uint16_t kbg, uint16_t bkb) {
  uint16_t msid = 0;
  if (!grounded_attack_msid_from_action(grounded_action_id, &msid)) {
    return 0u;
  }
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id != (uint8_t)MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      continue;
    }
    const MslScriptCreateHitboxPayload* hb = &ev->payload.create_hitbox;
    if (hb->hitbox_id != hitbox_id) {
      continue;
    }
    const int dmg = (int)(hb->damage + (hb->damage >= 0.0f ? 0.5f : -0.5f));
    if (dmg == hitcapsule_int_dmg && hb->angle == angle && hb->kbg == kbg && hb->bkb == bkb) {
      return 1u;
    }
  }
  return 0u;
}

static const MslMoveTableCache* grounded_smash_charge_cache(uint8_t char_id,
                                                            uint16_t grounded_action_id) {
  uint16_t msid = 0;
  if (!grounded_attack_msid_from_action(grounded_action_id, &msid)) {
    return NULL;
  }
  return move_cache_get(char_id, msid);
}

uint8_t move_tables_grounded_smash_charge_crossed(uint8_t char_id, uint16_t grounded_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32,
                                                  uint8_t* out_hold_frames) {
  if (out_hold_frames == NULL) {
    return 0u;
  }
  const MslMoveTableCache* cache = grounded_smash_charge_cache(char_id, grounded_action_id);
  if (cache == NULL || cache->smash_charge_frame < 0 ||
      !frame_crossed_u16((uint16_t)cache->smash_charge_frame, prev_anim_frame_f32,
                         cur_anim_frame_f32)) {
    return 0u;
  }
  *out_hold_frames = cache->smash_charge_hold_frames;
  return 1u;
}

uint8_t move_tables_grounded_smash_charge_info(uint8_t char_id, uint16_t grounded_action_id,
                                               uint16_t* out_frame, uint8_t* out_hold_frames) {
  const MslMoveTableCache* cache = grounded_smash_charge_cache(char_id, grounded_action_id);
  if (cache == NULL || cache->smash_charge_frame < 0) {
    return 0u;
  }
  if (out_frame != NULL) {
    *out_frame = (uint16_t)cache->smash_charge_frame;
  }
  if (out_hold_frames != NULL) {
    *out_hold_frames = cache->smash_charge_hold_frames;
  }
  return 1u;
}

float move_tables_grounded_smash_charge_damage_mul(uint8_t char_id, uint16_t grounded_action_id) {
  const MslMoveTableCache* cache = grounded_smash_charge_cache(char_id, grounded_action_id);
  if (cache == NULL || cache->smash_charge_frame < 0 || !(cache->smash_charge_damage_mul > 0.0f)) {
    return 1.0f;
  }
  return cache->smash_charge_damage_mul;
}

uint8_t move_tables_escape_allow_interrupt(uint8_t char_id, uint16_t action_id,
                                           float cur_anim_frame_f32) {
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_N) {
    return allow_interrupt_active(char_id, (uint16_t)MSL_SM_ESCAPE_N, cur_anim_frame_f32);
  }
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return allow_interrupt_active(char_id, (uint16_t)MSL_SM_ESCAPE_AIR, cur_anim_frame_f32);
  }
  return 0u;
}

uint8_t move_tables_escapeair_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  return cmd_var_active(char_id, (uint16_t)MSL_SM_ESCAPE_AIR, 0u, 1u, cur_anim_frame_f32);
}

uint8_t move_tables_special_cmd0_active_at_frame(uint8_t char_id, uint16_t msid, int action_frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->cmd_var_value1_open[0].loaded) {
    return 0u;
  }
  const MslScriptFrameWindow win = cache->cmd_var_value1_open[0];
  const int end_tail = (int)win.end_af + MSL_SPECIAL_CMD0_LATCH_CLEAR_TAIL_FRAMES;
  return (action_frame >= (int)win.start_af && action_frame < end_tail) ? 1u : 0u;
}

uint8_t move_tables_special_cmd0_raw_active_at_frame(uint8_t char_id, uint16_t msid,
                                                     int action_frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->cmd_var_value1_open[0].loaded) {
    return 0u;
  }
  const MslScriptFrameWindow win = cache->cmd_var_value1_open[0];
  return (action_frame >= (int)win.start_af && action_frame < (int)win.end_af) ? 1u : 0u;
}

uint8_t move_tables_hit_status_at_frame(uint8_t char_id, uint16_t msid, uint16_t frame,
                                        uint8_t* out_status) {
  if (out_status == NULL) {
    return 0u;
  }
  *out_status = 0u;
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  uint16_t f = frame;
  if (f >= (uint16_t)MSL_MOVE_TABLE_FRAME_CAP) {
    f = (uint16_t)(MSL_MOVE_TABLE_FRAME_CAP - 1);
  }
  *out_status = cache->hit_status[f];
  return 1u;
}

uint8_t move_tables_hurtbox_can_hit_mask_at_frame(uint8_t char_id, uint16_t msid, uint16_t frame,
                                                  uint16_t cap_count, uint32_t* out_mask) {
  if (out_mask == NULL) {
    return 0u;
  }
  *out_mask = mask_low_bits_u32(cap_count);
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  uint16_t f = frame;
  if (f >= (uint16_t)MSL_MOVE_TABLE_FRAME_CAP) {
    f = (uint16_t)(MSL_MOVE_TABLE_FRAME_CAP - 1);
  }
  *out_mask = cache->hurtbox_can_hit_mask[f] & mask_low_bits_u32(cap_count);
  return 1u;
}

uint8_t move_tables_state_flags_221c_y_at_frame(uint8_t char_id, uint16_t msid, uint16_t frame,
                                                uint8_t* out_flags) {
  if (out_flags == NULL) {
    return 0u;
  }
  *out_flags = 0u;
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  uint16_t f = frame;
  if (f >= (uint16_t)MSL_MOVE_TABLE_FRAME_CAP) {
    f = (uint16_t)(MSL_MOVE_TABLE_FRAME_CAP - 1);
  }
  *out_flags = cache->state_flags_221c_y[f];
  return 1u;
}

uint8_t move_tables_state_flags_221c_y_with_event_frame(uint8_t char_id, uint16_t msid,
                                                        uint16_t frame, uint8_t* out_flags,
                                                        uint16_t* out_last_event_frame) {
  // Level + provenance: also report the frame of the opcode-52 event that produced the level
  // (the last frame at or before `frame` where the scripted lane changed). Consumers model the
  // Fighter_ChangeMotionState x221C_u16_y clear on frame-preserving transitions by suppressing
  // levels whose source event predates the transition entry frame.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState (clear when !Ft_MF_Unk24)
  if (out_flags == NULL || out_last_event_frame == NULL) {
    return 0u;
  }
  *out_flags = 0u;
  *out_last_event_frame = 0u;
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  uint16_t f = frame;
  if (f >= (uint16_t)MSL_MOVE_TABLE_FRAME_CAP) {
    f = (uint16_t)(MSL_MOVE_TABLE_FRAME_CAP - 1);
  }
  *out_flags = cache->state_flags_221c_y[f];
  uint16_t e = f;
  while (e > 0u && cache->state_flags_221c_y[e - 1u] == cache->state_flags_221c_y[e]) {
    e--;
  }
  *out_last_event_frame = e;
  return 1u;
}

uint8_t move_tables_airborne_state_event_at_frame(uint8_t char_id, uint16_t msid, uint16_t frame,
                                                  uint8_t* out_state) {
  if (out_state == NULL) {
    return 0u;
  }
  *out_state = (uint8_t)MSL_MOVE_TABLE_NO_AIRBORNE_EVENT;
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || frame >= (uint16_t)MSL_MOVE_TABLE_FRAME_CAP) {
    return 0u;
  }
  const uint8_t state = cache->airborne_state_event[frame];
  if (state == (uint8_t)MSL_MOVE_TABLE_NO_AIRBORNE_EVENT) {
    return 0u;
  }
  *out_state = state;
  return 1u;
}

uint8_t move_tables_special_cmd2_pulse_crossed(uint8_t char_id, uint16_t msid,
                                               float prev_anim_frame_f32, float cur_anim_frame_f32,
                                               int16_t* out_pulse_frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || cur_anim_frame_f32 < prev_anim_frame_f32) {
    return 0u;
  }
  const uint8_t idx = 2u;
  for (uint8_t i = 0; i < cache->cmd_var_value1_pulse_count[idx]; i++) {
    const uint16_t frame = cache->cmd_var_value1_pulses[idx][i];
    if (frame_crossed_u16(frame, prev_anim_frame_f32, cur_anim_frame_f32)) {
      if (out_pulse_frame != NULL) {
        *out_pulse_frame = (int16_t)frame;
      }
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_special_cmd2_first_pulse_frame(uint8_t char_id, uint16_t msid,
                                                   int16_t* out_first_pulse_frame) {
  if (out_first_pulse_frame == NULL) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || cache->cmd_var_value1_pulse_count[2] == 0u) {
    return 0u;
  }
  uint16_t first = cache->cmd_var_value1_pulses[2][0];
  for (uint8_t i = 1; i < cache->cmd_var_value1_pulse_count[2]; i++) {
    if (cache->cmd_var_value1_pulses[2][i] < first) {
      first = cache->cmd_var_value1_pulses[2][i];
    }
  }
  *out_first_pulse_frame = (int16_t)first;
  return 1u;
}

uint8_t move_tables_escapef_should_flip_facing(uint8_t char_id, int16_t prev_action_frame,
                                               int16_t cur_action_frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, (uint16_t)MSL_SM_ESCAPE_F);
  if (cache == NULL || !cache->throw_flags_hit[0].loaded) {
    return 0u;
  }
  const MslScriptFrameWindow win = cache->throw_flags_hit[0];
  return (prev_action_frame < win.start_af && cur_action_frame >= win.start_af) ? 1u : 0u;
}

uint8_t move_tables_jab_combo_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!grounded_attack_msid_from_action(grounded_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return (cache != NULL && cache->jab_combo_on_frame >= 0 &&
          cur_anim_frame_f32 >= (float)cache->jab_combo_on_frame)
             ? 1u
             : 0u;
}

uint8_t move_tables_jab_rapid_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!grounded_attack_msid_from_action(grounded_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_contains(cache->jab_rapid, cur_anim_frame_f32) : 0u;
}

uint8_t move_tables_attack100_loop_end_check_crossed(uint8_t char_id, int16_t prev_action_frame,
                                                     int16_t cur_action_frame) {
  const MslMoveTableCache* cache = move_cache_get(char_id, (uint16_t)MSL_SM_ATTACK_100_LOOP);
  if (cache == NULL || cache->throw_flags_pulse_count[0] == 0u) {
    return 0u;
  }
  for (uint8_t i = 0; i < cache->throw_flags_pulse_count[0]; i++) {
    const int16_t on = (int16_t)cache->throw_flags_pulses[0][i];
    if (cur_action_frame >= prev_action_frame) {
      if (prev_action_frame < on && cur_action_frame >= on) {
        return 1u;
      }
    } else if (prev_action_frame < on || cur_action_frame >= on) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_special_throw_flags_window(uint8_t char_id, uint16_t msid,
                                               float anim_frame_f32) {
  // True once the move's first set_throw_flags pulse has been reached (the ftCheckThrowB3
  // consumable window opener used by e.g. Dolphin Slash's B-reverse).
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || cache->throw_flags_pulse_count[0] == 0u) {
    return 0u;
  }
  return ((float)cache->throw_flags_pulses[0][0] <= anim_frame_f32) ? 1u : 0u;
}

uint8_t move_tables_special_cmd_var_value_at_frame(uint8_t char_id, uint16_t msid, uint8_t var_idx,
                                                   float anim_frame_f32) {
  // Script-owned cmd var value at a frame: 1 iff the latest set_cmd_var pulse at or before the
  // frame is a value-1 pulse (frame-0 value-0 initializers are excluded at cache build).
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || var_idx >= (uint8_t)MSL_MOVE_TABLE_CMD_VAR_COUNT) {
    return 0u;
  }
  int last_on = -1;
  int last_off = -1;
  for (uint8_t i = 0; i < cache->cmd_var_value1_pulse_count[var_idx]; i++) {
    const int f = (int)cache->cmd_var_value1_pulses[var_idx][i];
    if ((float)f <= anim_frame_f32 && f > last_on) {
      last_on = f;
    }
  }
  for (uint8_t i = 0; i < cache->cmd_var_value0_pulse_count[var_idx]; i++) {
    const int f = (int)cache->cmd_var_value0_pulses[var_idx][i];
    if ((float)f <= anim_frame_f32 && f > last_off) {
      last_off = f;
    }
  }
  return (last_on >= 0 && last_on >= last_off) ? 1u : 0u;
}

uint8_t move_tables_dash_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  return cmd_var_active(char_id, (uint16_t)MSL_SM_DASH, 0u, 1u, cur_anim_frame_f32);
}

uint8_t move_tables_runbrake_cmd0_active(uint8_t char_id, float cur_anim_frame_f32) {
  return cmd_var_active(char_id, (uint16_t)MSL_SM_RUN_BRAKE, 0u, 1u, cur_anim_frame_f32);
}

uint8_t move_tables_turnrun_cmd1_active(uint8_t char_id, float cur_anim_frame_f32) {
  return cmd_var_active(char_id, (uint16_t)MSL_SM_TURN_RUN, 1u, 1u, cur_anim_frame_f32);
}

uint8_t move_tables_catchpull_should_enter_wait(uint8_t char_id, uint16_t catch_action_id,
                                                float cur_anim_frame_f32) {
  const uint16_t msid = (catch_action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL)
                            ? (uint16_t)MSL_SM_CATCH_DASH
                            : (uint16_t)MSL_SM_CATCH;
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->throw_flags_any.loaded) {
    return 0u;
  }
  return cur_anim_frame_f32 >= (float)cache->throw_flags_any.start_af ? 1u : 0u;
}

uint8_t move_tables_catchattack_grabbed_hit_active(uint8_t char_id, float cur_anim_frame_f32) {
  const MslMoveTableCache* cache = move_cache_get(char_id, (uint16_t)MSL_SM_CATCH_ATTACK);
  return cache != NULL ? frame_window_contains(cache->catchattack_grabbed_hit, cur_anim_frame_f32)
                       : 0u;
}

uint8_t move_tables_throw_has_release(uint8_t char_id, uint16_t throw_action_id) {
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return (cache != NULL && cache->throw_flags_hit[0].loaded) ? 1u : 0u;
}

uint8_t move_tables_throw_release_frame(uint8_t char_id, uint16_t throw_action_id,
                                        float* out_release_af) {
  if (out_release_af == NULL) {
    return 0u;
  }
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->throw_flags_hit[0].loaded) {
    return 0u;
  }
  *out_release_af = (float)cache->throw_flags_hit[0].start_af;
  return 1u;
}

uint8_t move_tables_throw_release_hit_idx(uint8_t char_id, uint16_t throw_action_id,
                                          float cur_anim_frame_f32, uint8_t* out_hit_idx) {
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->throw_flags_hit[0].loaded ||
      cur_anim_frame_f32 < (float)cache->throw_flags_hit[0].start_af) {
    return 0u;
  }
  if (out_hit_idx != NULL) {
    *out_hit_idx = 0u;
  }
  return 1u;
}

uint8_t move_tables_throw_hitbox_params(uint8_t char_id, uint16_t throw_action_id, uint8_t hit_idx,
                                        MslThrowHitboxParams* out) {
  if (out == NULL) {
    return 0u;
  }
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || hit_idx >= (uint8_t)MSL_MOVE_TABLE_THROW_HITBOX_CAP ||
      !cache->throw_hitboxes[hit_idx].loaded) {
    return 0u;
  }
  *out = cache->throw_hitboxes[hit_idx].params;
  return 1u;
}

uint8_t move_tables_throw_release_after_create_hitbox(uint8_t char_id, uint16_t throw_action_id) {
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL || !cache->first_create_hitbox.loaded || !cache->throw_flags_hit[0].loaded) {
    return 0u;
  }
  return (cache->first_create_hitbox.start_af < cache->throw_flags_hit[0].start_af) ? 1u : 0u;
}

uint8_t move_tables_throw_should_flip_facing(uint8_t char_id, uint16_t throw_action_id,
                                             float prev_anim_frame_f32, float cur_anim_frame_f32) {
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  return cache != NULL ? frame_window_crossed(cache->throw_flags_hit[1], prev_anim_frame_f32,
                                              cur_anim_frame_f32)
                       : 0u;
}

uint8_t move_tables_throw_cmd1_active(uint8_t char_id, uint16_t throw_action_id,
                                      float cur_anim_frame_f32) {
  uint16_t msid = 0;
  return throw_msid_from_action(throw_action_id, &msid)
             ? cmd_var_active(char_id, msid, 1u, 1u, cur_anim_frame_f32)
             : 0u;
}

static uint8_t throw_projectile_pulses(uint8_t char_id, uint16_t throw_action_id,
                                       uint16_t* out_frames, uint8_t max_out, uint8_t* out_count) {
  if (out_frames == NULL || out_count == NULL) {
    return 0u;
  }
  *out_count = 0u;
  uint16_t msid = 0;
  if (!throw_msid_from_action(throw_action_id, &msid)) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  const uint8_t n =
      cache->projectile_pulse_count < max_out ? cache->projectile_pulse_count : max_out;
  for (uint8_t i = 0; i < n; i++) {
    out_frames[i] = cache->projectile_pulses[i];
  }
  *out_count = n;
  return *out_count != 0u ? 1u : 0u;
}

uint8_t move_tables_throw_should_spawn_projectile(uint8_t char_id, uint16_t throw_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32) {
  return move_tables_throw_crossed_projectile_pulse_frame(
             char_id, throw_action_id, prev_anim_frame_f32, cur_anim_frame_f32, NULL)
             ? 1u
             : 0u;
}

uint8_t move_tables_throw_crossed_projectile_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                         float prev_anim_frame_f32,
                                                         float cur_anim_frame_f32,
                                                         int16_t* out_pulse_frame) {
  uint16_t frames[16] = {0};
  uint8_t count = 0u;
  if (!throw_projectile_pulses(char_id, throw_action_id, frames,
                               (uint8_t)(sizeof(frames) / sizeof(frames[0])), &count)) {
    return 0u;
  }
  for (uint8_t i = 0; i < count; i++) {
    if (frame_crossed_u16(frames[i], prev_anim_frame_f32, cur_anim_frame_f32)) {
      if (out_pulse_frame != NULL) {
        *out_pulse_frame = (int16_t)frames[i];
      }
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_throw_projectile_first_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                       int16_t* out_first_pulse_frame) {
  if (out_first_pulse_frame == NULL) {
    return 0u;
  }
  uint16_t frames[16] = {0};
  uint8_t count = 0u;
  if (!throw_projectile_pulses(char_id, throw_action_id, frames,
                               (uint8_t)(sizeof(frames) / sizeof(frames[0])), &count)) {
    return 0u;
  }
  uint16_t first = frames[0];
  for (uint8_t i = 1; i < count; i++) {
    if (frames[i] < first) {
      first = frames[i];
    }
  }
  *out_first_pulse_frame = (int16_t)first;
  return 1u;
}

uint8_t move_tables_throw_projectile_last_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                      int16_t* out_last_pulse_frame) {
  if (out_last_pulse_frame == NULL) {
    return 0u;
  }
  uint16_t frames[16] = {0};
  uint8_t count = 0u;
  if (!throw_projectile_pulses(char_id, throw_action_id, frames,
                               (uint8_t)(sizeof(frames) / sizeof(frames[0])), &count)) {
    return 0u;
  }
  uint16_t last = frames[0];
  for (uint8_t i = 1; i < count; i++) {
    if (frames[i] > last) {
      last = frames[i];
    }
  }
  *out_last_pulse_frame = (int16_t)last;
  return 1u;
}

uint8_t move_tables_throw_projectile_pulse_ordinal(uint8_t char_id, uint16_t throw_action_id,
                                                   int16_t pulse_frame, uint8_t* out_ordinal) {
  if (out_ordinal == NULL) {
    return 0u;
  }
  uint16_t frames[16] = {0};
  uint8_t count = 0u;
  if (!throw_projectile_pulses(char_id, throw_action_id, frames,
                               (uint8_t)(sizeof(frames) / sizeof(frames[0])), &count)) {
    return 0u;
  }
  uint8_t ordinal = 0u;
  for (uint8_t i = 0; i < count; i++) {
    if ((int16_t)frames[i] <= pulse_frame) {
      ordinal++;
    }
    if ((int16_t)frames[i] == pulse_frame) {
      *out_ordinal = ordinal;
      return 1u;
    }
  }
  return 0u;
}

uint8_t move_tables_special_pseudo_random_sfx_ranges_crossed(uint8_t char_id, uint16_t msid,
                                                             float prev_anim_frame_f32,
                                                             float cur_anim_frame_f32,
                                                             uint8_t* out_random_ranges,
                                                             uint8_t max_out) {
  if (out_random_ranges == NULL || max_out == 0u) {
    return 0u;
  }
  const MslMoveTableCache* cache = move_cache_get(char_id, msid);
  if (cache == NULL) {
    return 0u;
  }
  uint8_t out_n = 0u;
  for (uint8_t i = 0; i < cache->sfx_pulse_count && out_n < max_out; i++) {
    const float on = (float)cache->sfx_pulse_frames[i];
    if ((prev_anim_frame_f32 < on && cur_anim_frame_f32 >= on) ||
        (on == 0.0f && prev_anim_frame_f32 == 0.0f && cur_anim_frame_f32 > 0.0f)) {
      out_random_ranges[out_n++] = cache->sfx_pulse_ranges[i];
    }
  }
  return out_n;
}
