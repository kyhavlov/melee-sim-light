#include "input.h"

#include <errno.h>

#include "action_ids.h"
#include "api.h"
#include "buttons.h"
#include "common_params.h"
#include "decomp/lb/lb_00ce.h"
#include "input_axis.h"
#include "move_tables.h"
#include "ucf.h"

// UCF pad buffer: refs/ucf/include/ucf/pad_buffer.h
enum { MSL_UCF_PADBUF_SIZE = 4, MSL_UCF_PADBUF_MASK = MSL_UCF_PADBUF_SIZE - 1 };

static inline uint8_t tilt_timer_update(uint8_t prev_timer, float axis, float prev_axis,
                                        float tilt_thresh) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1908-2008.
  uint16_t t = (uint16_t)prev_timer;
  if (axis >= tilt_thresh) {
    if (prev_axis >= tilt_thresh) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else if (axis <= -tilt_thresh) {
    if (prev_axis <= -tilt_thresh) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

static inline uint8_t press_timer_u8_update_edge(uint8_t prev_timer, uint8_t pressed_now) {
  // Decomp example (x67F): refs/melee/src/melee/ft/fighter.c:2078-2086.
  uint16_t t = (uint16_t)prev_timer;
  if (pressed_now) {
    t = 0;
  } else if (t < 0xFFu) {
    t++;
  }
  return (uint8_t)t;
}

static inline float trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return trigger_u8_to_unit(m);
}

static inline uint16_t input_edge_with_z_macro(uint16_t edge) {
  // Decomp: Fighter_Spaghetti_8006AD10 maps held Z onto the effective input lane as
  // HSD_PAD_A plus the HSD_PAD_LR macro before the button-history timer block consumes x668.
  // MSL has only physical L/R bits and models the Z/LR trigger lane separately; do not translate
  // the synthetic LR macro into physical L/R button edges here.
  // refs/melee/src/melee/ft/fighter.c::{
  //   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
  if ((edge & (uint16_t)MSL_BUTTON_Z) != 0u) {
    edge = (uint16_t)(edge | (uint16_t)MSL_BUTTON_A);
  }
  return edge;
}

static inline uint8_t opening_input_lock_active_for_step(const MslBatch* batch, int bi) {
  if (batch == NULL || batch->state.opening_input_lock_timer[bi] == 0u) {
    return 0u;
  }
  // Replay-derived opening lock countdowns are post-frame remaining-step lanes. The VS overlay
  // clear callback runs before raw frame -39 inputs are processed, so the final replay-seeded
  // countdown tick should not blank the current input for t+1. Teacher-forced rows and validation
  // rollouts expose that boundary as frame -40; replay rollouts with an active opening timer own
  // the replay frame clock until this boundary. Other timer=1 states, including fresh init_match /
  // modelplay episodes or mutated rollout seeds, keep the ordinary Fighter_UnkInitLoad lock
  // semantics.
  // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B7F8
  // refs/melee/src/melee/if/ifstatus.c::ifStatus_802F6EA4
  // refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
  if (batch->state.opening_input_lock_timer[bi] == 1u && batch->state.frame_id[bi] == -40) {
    return 0u;
  }
  return 1u;
}

static inline float msl_ucf_popo_to_nana(float x) {
  // refs/ucf/include/util/melee/pad.h::popo_to_nana
  if (x >= 0.0f) {
    const int8_t q = (int8_t)(x * 127.0f);
    return (float)q * (1.0f / 127.0f);
  }
  const int8_t q = (int8_t)(x * 128.0f);
  return (float)q * (1.0f / 128.0f);
}

static inline uint8_t msl_ucf_is_rim_coord(float stick_x_unit, float stick_y_unit) {
  // refs/ucf/include/util/melee/pad.h::is_rim_coord
  // - abs_coord_to_int(x) = (int)(abs(x)*80 - 0.0001f) + 1
  // - is_rim_coord adds +1 again per axis and compares length^2 > 80^2
  const float bias = 0.0001f;

  const float ax = msl_absf(stick_x_unit) * (float)MSL_STICK_MAX_I8 - bias;
  const float ay = msl_absf(stick_y_unit) * (float)MSL_STICK_MAX_I8 - bias;

  const int ix = (int)ax + 2;
  const int iy = (int)ay + 2;
  const int lsq = ix * ix + iy * iy;
  return lsq > (MSL_STICK_MAX_I8 * MSL_STICK_MAX_I8) ? 1 : 0;
}

static inline int8_t msl_ucf_padbuf_get_raw_x(const MslStateSoA* s, size_t idx, uint8_t offset) {
  const uint8_t base = s->ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)((base + offset) & (uint8_t)MSL_UCF_PADBUF_MASK);
  return s->ucf_padbuf_stick_x[idx * MSL_UCF_PADBUF_SIZE + (size_t)slot];
}

static inline int8_t msl_ucf_padbuf_get_raw_y(const MslStateSoA* s, size_t idx, uint8_t offset) {
  const uint8_t base = s->ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)((base + offset) & (uint8_t)MSL_UCF_PADBUF_MASK);
  return s->ucf_padbuf_stick_y[idx * MSL_UCF_PADBUF_SIZE + (size_t)slot];
}

uint8_t msl_ucf_check_xsmash(const MslStateSoA* s, size_t idx) {
  // Decomp tie-down:
  // - UCF check uses the pad buffer delta between current input and -2.
  //   refs/ucf/include/ucf/pad_buffer.h::check_ucf_xsmash (delta^2 > 75^2)
  //
  // The pad buffer is a seeded ring buffer that stores raw PADStatus stick bytes (pre-UCF clamp).
  if (s == NULL) {
    return 0;
  }
  const int x_prev = (int)msl_ucf_padbuf_get_raw_x(s, idx, (uint8_t)(-2));
  const int x_cur = (int)msl_ucf_padbuf_get_raw_x(s, idx, 0);
  const int d = x_cur - x_prev;
  return (d * d > 75 * 75) ? 1u : 0u;
}

static inline uint8_t x672_trigger_timer_update(uint8_t prev_timer, float trig, float prev_trig,
                                                float trigger_min) {
  // fp->x672_input_timer_counter (saturating at 0xFE).
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:2020-2050.
  uint16_t t = (uint16_t)prev_timer;
  if (trig >= trigger_min) {
    if (prev_trig >= trigger_min) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

static inline uint8_t clamp_inc_u8_fe(uint8_t prev) {
  uint16_t t = (uint16_t)prev + 1u;
  if (t > 0xFEu) {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

static inline uint8_t clamp_inc_u8_ff(uint8_t prev) {
  uint16_t t = (uint16_t)prev;
  if (t < 0xFFu) {
    t++;
  }
  return (uint8_t)t;
}

static inline float q16_16_to_f32(int32_t x) { return (float)x * (1.0f / 65536.0f); }

static inline uint8_t input_action_is_grounded_smash(uint16_t a) {
  switch (a) {
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
    case MSL_ACT_ATTACK_HI4:
    case MSL_ACT_ATTACK_LW4:
      return 1u;
    default:
      return 0u;
  }
}

static inline void grounded_smash_charge_clear(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.kb_smashcharge_active[idx] = 0u;
  batch->state.smash_charge_state[idx] = 0u;
  batch->state.smash_charge_frames[idx] = 0u;
  batch->state.smash_charge_hold_frames_max[idx] = 0u;
  batch->state.smash_charge_saved_rate_fp_q16_16[idx] = 0;
}

static inline void grounded_smash_charge_release(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const int32_t saved_rate = batch->state.smash_charge_saved_rate_fp_q16_16[idx];
  batch->state.frame_speed_mul_fp_q16_16[idx] = (saved_rate != 0) ? saved_rate : (1 << 16);
  batch->state.kb_smashcharge_active[idx] = 0u;
  batch->state.smash_charge_state[idx] = 3u;  // SmashState_Release
}

static inline void grounded_smash_charge_update_ftCo_800DF0D0_subset(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  const uint16_t action_id = batch->state.action_id[idx];
  if (input_action_is_grounded_smash(action_id) == 0u || batch->state.on_ground[idx] == 0u ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    grounded_smash_charge_clear(batch, idx);
    return;
  }

  const uint8_t held_a =
      ((batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_A) != 0u) ? 1u : 0u;
  // Decomp:
  // - opcode 56 seeds SmashState_PreCharge during the anim/script pass.
  // - the later fighter input proc (ftCo_800DF0D0) promotes PreCharge -> Charging on held A,
  //   stores the current anim rate, and freezes the next anim advances at 0.0f.
  // - while Charging, releasing A restores the saved rate immediately; ftCo_800DEF38 also
  //   auto-releases when the hold-frame counter reaches x211C_holdFrame.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
  // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DEF38,ftCo_800DF0D0}
  if (batch->state.smash_charge_state[idx] == 2u) {
    uint8_t frames = batch->state.smash_charge_frames[idx];
    if (frames < 0xFFu) {
      frames = (uint8_t)(frames + 1u);
    }
    batch->state.smash_charge_frames[idx] = frames;
    const uint8_t hold_max = batch->state.smash_charge_hold_frames_max[idx];
    if (held_a == 0u || (hold_max != 0u && frames >= hold_max)) {
      if (hold_max != 0u && frames > hold_max) {
        batch->state.smash_charge_frames[idx] = hold_max;
      }
      grounded_smash_charge_release(batch, idx);
      return;
    }
    batch->state.kb_smashcharge_active[idx] = 1u;
    batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
    return;
  }

  if (batch->state.smash_charge_state[idx] == 1u) {
    if (held_a == 0u) {
      grounded_smash_charge_clear(batch, idx);
      return;
    }
    batch->state.smash_charge_state[idx] = 2u;
    batch->state.smash_charge_frames[idx] = 0u;
    batch->state.kb_smashcharge_active[idx] = 1u;
    if (batch->state.smash_charge_saved_rate_fp_q16_16[idx] == 0) {
      batch->state.smash_charge_saved_rate_fp_q16_16[idx] =
          batch->state.frame_speed_mul_fp_q16_16[idx];
    }
    batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
    return;
  }

  const float cur_anim_frame = batch->state.anim_frame_f32[idx];
  const float prev_anim_frame =
      cur_anim_frame - q16_16_to_f32(batch->state.frame_speed_mul_fp_q16_16[idx]);
  uint8_t hold_frames = 0u;
  if (!move_tables_grounded_smash_charge_crossed(batch->state.char_id[idx], action_id,
                                                 prev_anim_frame, cur_anim_frame, &hold_frames)) {
    return;
  }

  // Live ownership note:
  // - ftCo_800DF0D0 consults the fighter's current input snapshot when promoting
  //   SmashState_PreCharge -> Charging.
  // - A fresh held-A pulse on the row that reaches af=2 is enough to freeze the next AttackHi4/
  //   AttackLw4 timeline advance; prior-frame A continuity is not required.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ft_0DF0.c::ftCo_800DF0D0
  if (held_a == 0u) {
    return;
  }

  batch->state.smash_charge_state[idx] = 2u;
  batch->state.smash_charge_frames[idx] = 0u;
  batch->state.smash_charge_hold_frames_max[idx] = hold_frames;
  batch->state.smash_charge_saved_rate_fp_q16_16[idx] = batch->state.frame_speed_mul_fp_q16_16[idx];
  batch->state.kb_smashcharge_active[idx] = 1u;
  batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
}

static inline void opening_input_lock_apply_Fighter_UnkInitLoad_80068914_Inner1_subset(
    MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: Fighter_procUpdate snapshots the current raw input lanes, then while fp->x221D_b4 is
  // set it calls Fighter_UnkInitLoad_80068914_Inner1, which blanks current/previous stick lanes,
  // held/edge button lanes, and the fighter input-history timers/counters consumed by IASA.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
  batch->state.input_buttons[idx] = 0u;
  batch->state.prev_input_buttons[idx] = 0u;
  batch->state.input_buttons_pressed[idx] = 0u;
  batch->state.input_buttons_released[idx] = 0u;

  batch->state.input_main_x[idx] = 0;
  batch->state.input_main_y[idx] = 0;
  batch->state.prev_input_main_x[idx] = 0;
  batch->state.prev_input_main_y[idx] = 0;
  batch->state.input_c_x[idx] = 0;
  batch->state.input_c_y[idx] = 0;
  batch->state.prev_input_c_x[idx] = 0;
  batch->state.prev_input_c_y[idx] = 0;
  batch->state.input_l[idx] = 0u;
  batch->state.input_r[idx] = 0u;
  batch->state.prev_input_l[idx] = 0u;
  batch->state.prev_input_r[idx] = 0u;

  batch->state.tilt_timer_x[idx] = 0xFEu;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.tilt_timer_y_frame_start[idx] = 0xFEu;
  batch->state.x672_input_timer[idx] = 0xFEu;
  batch->state.x672_input_timer_frame_start[idx] = 0xFEu;
  batch->state.x673[idx] = 0xFEu;
  batch->state.x674[idx] = 0xFEu;
  batch->state.x675[idx] = 0xFEu;
  batch->state.x676_x[idx] = 0xFEu;
  batch->state.x677_y[idx] = 0xFEu;
  batch->state.x678[idx] = 0xFEu;
  batch->state.x679_x[idx] = 0xFEu;
  batch->state.x67A_y[idx] = 0xFEu;
  batch->state.x679_x_frame_start[idx] = 0xFEu;
  batch->state.x67A_y_frame_start[idx] = 0xFEu;
  batch->state.x67B[idx] = 0xFEu;

  batch->state.x67C[idx] = 0xFFu;
  batch->state.x67D[idx] = 0xFFu;
  batch->state.x67E[idx] = 0xFFu;
  batch->state.x680[idx] = 0xFFu;
  batch->state.x681[idx] = 0xFFu;
  batch->state.x682[idx] = 0xFFu;
  batch->state.x683[idx] = 0xFFu;
  batch->state.x684[idx] = 0xFFu;
  batch->state.lr_press_timer[idx] = 0xFFu;
}

int input_apply_pre_input_snapshot(MslBatch* batch, const uint8_t* prev_input_bytes,
                                   size_t prev_input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (msl_common_params() == NULL) {
    return EINVAL;
  }
  if (prev_input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInput)) {
    return EINVAL;
  }

  const uint8_t ucf_enabled = batch->config.ucf_enabled ? 1 : 0;
  const uint8_t cardinals = batch->config.ucf_cardinals_1_0_enabled ? 1 : 0;
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* prev_ptr = prev_input_bytes + (size_t)bi * prev_input_stride_bytes;
    const MslInput* prev = (const MslInput*)prev_ptr;

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t prev_buttons = prev->p[p].buttons;

      // Decomp ownership: prio1 callbacks run before Fighter_procUpdate input_cb (prio3), so
      // pre-input gameplay should see prior-frame input state.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      //
      // Use the same UCF/legalized stick transform as input_apply():
      // `prev_input_*` / `input_*` fields are documented as processed stick lanes, and raw replay
      // bytes can exceed the legal [-80,80] range before UCF/cardinal handling.
      // refs/ucf/src/pad_buffer/pad_buffer.cpp
      const MslStickI8 prev_main =
          ucf_process_stick_i8(prev->p[p].main_x, prev->p[p].main_y, ucf_enabled, cardinals);
      const MslStickI8 prev_cstick =
          ucf_process_stick_i8(prev->p[p].c_x, prev->p[p].c_y, ucf_enabled, cardinals);
      batch->state.prev_input_buttons[idx] = prev_buttons;
      batch->state.input_buttons[idx] = prev_buttons;
      // Pre-input callbacks run before Fighter_Spaghetti recomputes x668/x66C. Normal frames should
      // not expose current-frame edges here, but active hitlag preserves the previous latched edge
      // until Fighter_Spaghetti_8006AD10_Inner1 ORs in new edges later in the input pass.
      // refs/melee/src/melee/ft/fighter.c::{
      //   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10
      // }
      const uint8_t source_hitlag_latch_active =
          (batch->state.hitlag_started_frame[idx] != 0u || batch->state.hitlag[idx] != 0u) ? 1u
                                                                                           : 0u;
      if (source_hitlag_latch_active == 0u) {
        batch->state.input_buttons_pressed[idx] = 0u;
        batch->state.input_buttons_released[idx] = 0u;
      }

      batch->state.prev_input_main_x[idx] = prev_main.x;
      batch->state.prev_input_main_y[idx] = prev_main.y;
      batch->state.input_main_x[idx] = prev_main.x;
      batch->state.input_main_y[idx] = prev_main.y;
      batch->state.prev_input_c_x[idx] = prev_cstick.x;
      batch->state.prev_input_c_y[idx] = prev_cstick.y;
      batch->state.input_c_x[idx] = prev_cstick.x;
      batch->state.input_c_y[idx] = prev_cstick.y;

      batch->state.prev_input_l[idx] = prev->p[p].l;
      batch->state.prev_input_r[idx] = prev->p[p].r;
      batch->state.input_l[idx] = prev->p[p].l;
      batch->state.input_r[idx] = prev->p[p].r;
    }
  }
  return 0;
}

int input_apply(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }
  const MslCommonParams* com = msl_common_params();
  if (com == NULL) {
    return EINVAL;
  }
  if (prev_input_bytes == NULL || input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInput) || input_stride_bytes < sizeof(MslInput)) {
    return EINVAL;
  }

  const uint8_t ucf_enabled = batch->config.ucf_enabled ? 1 : 0;
  const uint8_t cardinals = batch->config.ucf_cardinals_1_0_enabled ? 1 : 0;
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* prev_ptr = prev_input_bytes + (size_t)bi * prev_input_stride_bytes;
    const uint8_t* cur_ptr = input_bytes + (size_t)bi * input_stride_bytes;
    const MslInput* prev = (const MslInput*)prev_ptr;
    const MslInput* cur = (const MslInput*)cur_ptr;

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const uint16_t prev_buttons = prev->p[p].buttons;
      const uint16_t cur_buttons = cur->p[p].buttons;

      batch->state.prev_input_buttons[idx] = prev_buttons;
      batch->state.input_buttons[idx] = cur_buttons;
      const uint16_t raw_pressed = (uint16_t)(cur_buttons & (uint16_t)~prev_buttons);
      const uint16_t raw_released = (uint16_t)(prev_buttons & (uint16_t)~cur_buttons);
      // Decomp: Fighter_Spaghetti_8006AD10_Inner1 OR-latches input.x668/x66C while
      // fp->x2219_b5 is active, then the button-history timer block consumes that latched x668
      // in the same Fighter_Spaghetti pass. This matters for tech timers: repeated hitlag-latched
      // digital L/R edges reset x680 and overwrite x684, making the debounce gate in
      // ftCo_800986B0 fail instead of falsely admitting Passive/PassiveStand.
      // refs/melee/src/melee/ft/fighter.c::{
      //   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10
      // }
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
      const uint8_t source_hitlag_latch_active =
          (batch->state.hitlag_started_frame[idx] != 0u || batch->state.hitlag[idx] != 0u) ? 1u
                                                                                           : 0u;
      if (source_hitlag_latch_active != 0u) {
        batch->state.input_buttons_pressed[idx] =
            (uint16_t)(batch->state.input_buttons_pressed[idx] | raw_pressed);
        batch->state.input_buttons_released[idx] =
            (uint16_t)(batch->state.input_buttons_released[idx] | raw_released);
      } else {
        batch->state.input_buttons_pressed[idx] = raw_pressed;
        batch->state.input_buttons_released[idx] = raw_released;
      }

      // -------------------------------
      // UCF pad buffer + "effective" stick
      // -------------------------------
      // Match UCF pad buffer write ordering:
      // refs/ucf/src/pad_buffer/pad_buffer.cpp
      // - buffer->index = (buffer->index + 1) & 3
      // - buffer->entries[buffer->index].stick = status.stick (raw PAD bytes)
      //
      // The simulator's gameplay logic should consume the resulting "effective stick",
      // i.e. Melee-legalized axes after any UCF preprocessing (cardinals snap + clamp).
      {
        uint8_t pb = batch->state.ucf_padbuf_index[idx];
        pb = (uint8_t)((pb + 1u) & (uint8_t)MSL_UCF_PADBUF_MASK);
        batch->state.ucf_padbuf_index[idx] = pb;
        batch->state.ucf_padbuf_stick_x[idx * MSL_UCF_PADBUF_SIZE + (size_t)pb] = cur->p[p].main_x;
        batch->state.ucf_padbuf_stick_y[idx * MSL_UCF_PADBUF_SIZE + (size_t)pb] = cur->p[p].main_y;
      }

      const int8_t raw_main_x = msl_ucf_padbuf_get_raw_x(&batch->state, idx, 0);
      const int8_t raw_main_y = msl_ucf_padbuf_get_raw_y(&batch->state, idx, 0);

      const MslStickI8 main = ucf_process_stick_i8(raw_main_x, raw_main_y, ucf_enabled, cardinals);
      const MslStickI8 cstick =
          ucf_process_stick_i8(cur->p[p].c_x, cur->p[p].c_y, ucf_enabled, cardinals);
      const MslStickI8 prev_main =
          ucf_process_stick_i8(prev->p[p].main_x, prev->p[p].main_y, ucf_enabled, cardinals);
      const MslStickI8 prev_cstick =
          ucf_process_stick_i8(prev->p[p].c_x, prev->p[p].c_y, ucf_enabled, cardinals);
      batch->state.input_main_x[idx] = main.x;
      batch->state.input_main_y[idx] = main.y;
      batch->state.prev_input_main_x[idx] = prev_main.x;
      batch->state.prev_input_main_y[idx] = prev_main.y;
      batch->state.input_c_x[idx] = cstick.x;
      batch->state.input_c_y[idx] = cstick.y;
      batch->state.prev_input_c_x[idx] = prev_cstick.x;
      batch->state.prev_input_c_y[idx] = prev_cstick.y;

      // Update input-history tilt timers (x670/x671) using legalized axes.
      const float stick_x = apply_deadzone(stick_i8_to_unit(main.x), com->lstick_deadzone_x);
      const float stick_y = apply_deadzone(stick_i8_to_unit(main.y), com->lstick_deadzone_y);
      const float prev_stick_x =
          apply_deadzone(stick_i8_to_unit(prev_main.x), com->lstick_deadzone_x);
      const float prev_stick_y =
          apply_deadzone(stick_i8_to_unit(prev_main.y), com->lstick_deadzone_y);

      uint8_t prev_tilt_timer_x = batch->state.tilt_timer_x[idx];
      uint8_t prev_tilt_timer_y = batch->state.tilt_timer_y[idx];
      batch->state.tilt_timer_y_frame_start[idx] = prev_tilt_timer_y;
      const uint8_t tilt_timer_x_next =
          tilt_timer_update(prev_tilt_timer_x, stick_x, prev_stick_x, com->lstick_tilt_x_thresh);
      const uint8_t tilt_timer_y_next =
          tilt_timer_update(prev_tilt_timer_y, stick_y, prev_stick_y, com->lstick_tilt_y_thresh);

      batch->state.tilt_timer_x[idx] = tilt_timer_x_next;
      batch->state.tilt_timer_y[idx] = tilt_timer_y_next;

      // Opening-countdown final clear:
      // - While fp->x221D_b4 is set, Fighter_Spaghetti saves physical stick into input.x630/x634,
      //   then Fighter_UnkInitLoad_80068914_Inner1 blanks the live input lanes and resets x670/x671
      //   to 0xFE.
      // - On the frame -40 clear boundary, the saved physical previous-stick sample becomes legal
      //   input history, but a held stick must keep the reset x670/x671 value from the prior locked
      //   frame. A fresh threshold crossing on the clear frame still gets timer 0 and may dash.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10,
      //   Fighter_UnkInitLoad_80068914_Inner1}
      // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B7F8
      if (batch->state.opening_input_lock_timer[bi] == 1u && batch->state.frame_id[bi] == -40) {
        if ((stick_x >= com->lstick_tilt_x_thresh && prev_stick_x >= com->lstick_tilt_x_thresh) ||
            (stick_x <= -com->lstick_tilt_x_thresh && prev_stick_x <= -com->lstick_tilt_x_thresh)) {
          batch->state.tilt_timer_x[idx] = 0xFEu;
        }
        if ((stick_y >= com->lstick_tilt_y_thresh && prev_stick_y >= com->lstick_tilt_y_thresh) ||
            (stick_y <= -com->lstick_tilt_y_thresh && prev_stick_y <= -com->lstick_tilt_y_thresh)) {
          batch->state.tilt_timer_y[idx] = 0xFEu;
        }
      }

      // UCF sdrop-up helper counter (`sdrop_up_frames`) lives in the pad buffer shared state.
      // refs/ucf/src/pad_buffer/pad_buffer.cpp::{check_ucf_sdrop, check_sdrop_up}
      //
      // This is not used by the sim gameplay logic yet; we update it here to keep the
      // pad buffer state self-contained, seedable, and strictly causal.
      {
        // Must be -0.6125 or below along the rim, adjusted to prevent an ICs desync.
        // refs/ucf/src/pad_buffer/pad_buffer.cpp::check_sdrop_up
        const float sdrop_y_thresh = msl_ucf_popo_to_nana(-0.6125f);
        uint8_t sdrop = batch->state.ucf_padbuf_sdrop_up_frames[idx];

        if (stick_y > sdrop_y_thresh || !msl_ucf_is_rim_coord(stick_x, stick_y)) {
          sdrop = 0;
        } else if (sdrop != 0) {
          sdrop = (uint8_t)(sdrop + 1u);
        } else {
          // UCF gate: only check speed on first frame.
          //
          // Source tie-down:
          // - UCF reads `player->input.stick_y_hold_time` (u8) at offset 0x671:
          //   refs/ucf/include/melee/asm/player.h
          // - In decomp, the per-frame update for fp->x671_timer_lstick_tilt_y is:
          //   refs/melee/src/melee/ft/fighter.c:1963-2008
          //
          // Assumption: this sim's `tilt_timer_y` (x671-style timer) corresponds to UCF's
          // `stick_y_hold_time` closely enough for gating the sdrop-up helper.
          //
          // Ordering assumption to revisit if shielddrop behavior is off later:
          // UCF's pad-buffer injection applies 1.0 cardinals before check_sdrop_up
          // (refs/ucf/src/pad_buffer/pad_buffer.cpp), but we haven't proven whether Melee updates
          // `stick_y_hold_time` using pre- or post-injection stick values. We currently treat it
          // as a function of the post-UCF-processed stick (ucf_process_stick_i8 + deadzone),
          // consistent with the x671 input-history timer logic in fighter.c.
          //
          // This matches Melee's x671 input-history timer update (tilt_timer_update),
          // which counts consecutive frames beyond the tilt threshold.
          if (tilt_timer_y_next < 2) {
            const int8_t raw_prev2_y = msl_ucf_padbuf_get_raw_y(&batch->state, idx, (uint8_t)-2);
            const int dy = (int)raw_main_y - (int)raw_prev2_y;
            // refs/ucf/src/pad_buffer/pad_buffer.cpp::check_ucf_sdrop (44*44).
            const int dy_sq = dy * dy;
            if (dy_sq > (44 * 44)) {
              sdrop = 1;
            }
          }
        }

        batch->state.ucf_padbuf_sdrop_up_frames[idx] = sdrop;
      }

      batch->state.input_l[idx] = cur->p[p].l;
      batch->state.input_r[idx] = cur->p[p].r;
      batch->state.prev_input_l[idx] = prev->p[p].l;
      batch->state.prev_input_r[idx] = prev->p[p].r;

      // Update fighter input counters / input-history timers (single-writer invariant: input.c).
      // Decomp reference for the full block:
      // refs/melee/src/melee/ft/fighter.c:1897-2094 (and lb_8000D148 in refs/melee/src/melee/lb/lb_00CE.c:163-225).
      enum {
        A = (uint16_t)MSL_BUTTON_A,
        B = (uint16_t)MSL_BUTTON_B,
        XY = (uint16_t)MSL_BUTTON_XY,
        DPAD_UP = (uint16_t)MSL_BUTTON_D_UP,
        DPAD_DOWN = (uint16_t)MSL_BUTTON_D_DOWN,
        LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R,
      };

      const float trig = trigger_unit_from_input(cur_buttons, cur->p[p].l, cur->p[p].r);
      const float prev_trig = trigger_unit_from_input(prev_buttons, prev->p[p].l, prev->p[p].r);
      // Decomp x67F tie-down:
      // - fp->x67F updates from `fp->input.x668 & HSD_PAD_LR` (edge mask), not held bits directly.
      // - x668 edges are built from held-input lanes that include digital L/R, trigger lane, and
      //   Z-mapped LR lane.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
      // refs/melee/src/melee/ft/fighter.c:1868-1890
      // refs/melee/src/melee/ft/fighter.c:2078-2086
      const uint8_t held_lr_lane_now =
          (((cur_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
           (trig > com->trigger_deadzone))
              ? 1u
              : 0u;
      const uint8_t held_lr_lane_prev =
          (((prev_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
           (prev_trig > com->trigger_deadzone))
              ? 1u
              : 0u;
      uint8_t pressed_lr_lane = (held_lr_lane_now != 0u && held_lr_lane_prev == 0u) ? 1u : 0u;
      if (source_hitlag_latch_active != 0u && batch->state.lr_press_timer[idx] == 0u) {
        // x67F consumes the same hitlag-latched x668 edge as the other fighter input-history
        // timers. If an LR-lane edge was already latched before or during active hitlag, the
        // timer continues to observe that edge until hitlag clears; this is the predicate used by
        // ftCo_LandingAir_EnterWithLag for the L-cancel lag divide branch.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
        pressed_lr_lane = 1u;
      }
      batch->state.lr_press_timer[idx] =
          press_timer_u8_update_edge(batch->state.lr_press_timer[idx], pressed_lr_lane);

      // GuardReflect admission helpers read x672 from the callback-visible frame-start phase while
      // input_apply owns the persistent counter update for the next frame.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
      // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
      batch->state.x672_input_timer_frame_start[idx] = batch->state.x672_input_timer[idx];
      batch->state.x672_input_timer[idx] =
          x672_trigger_timer_update(batch->state.x672_input_timer[idx], trig, prev_trig,
                                    com->powershield_reflect_trigger_min);
      batch->state.x679_x_frame_start[idx] = batch->state.x679_x[idx];
      batch->state.x67A_y_frame_start[idx] = batch->state.x67A_y[idx];

      // x676_x: increment (clamp to 0xFE) then reset on fresh directional entry.
      batch->state.x676_x[idx] = clamp_inc_u8_fe(batch->state.x676_x[idx]);
      if (stick_x >= com->lstick_tilt_x_thresh) {
        if (prev_stick_x >= com->lstick_tilt_x_thresh) {
          batch->state.x673[idx] = clamp_inc_u8_fe(batch->state.x673[idx]);
          batch->state.x679_x[idx] = clamp_inc_u8_fe(batch->state.x679_x[idx]);
        } else {
          batch->state.x676_x[idx] = 0;
          batch->state.x673[idx] = 0;
          // Decomp: on a fresh >= threshold X entry, Fighter updates fp->x2228_b7 = 1.
          // refs/melee/src/melee/ft/fighter.c:1921-1925
          batch->state.x2228_b7[idx] = 1u;
        }
      } else if (stick_x <= -com->lstick_tilt_x_thresh) {
        if (prev_stick_x <= -com->lstick_tilt_x_thresh) {
          batch->state.x673[idx] = clamp_inc_u8_fe(batch->state.x673[idx]);
          batch->state.x679_x[idx] = clamp_inc_u8_fe(batch->state.x679_x[idx]);
        } else {
          batch->state.x676_x[idx] = 0;
          batch->state.x673[idx] = 0;
          // Decomp: on a fresh <= -threshold X entry, Fighter updates fp->x2228_b7 = 0.
          // refs/melee/src/melee/ft/fighter.c:1946-1950
          batch->state.x2228_b7[idx] = 0u;
        }
      } else {
        batch->state.x679_x[idx] = 0xFEu;
        batch->state.x673[idx] = 0xFEu;
      }

      // x677_y: increment (clamp to 0xFE) then reset on fresh directional entry.
      batch->state.x677_y[idx] = clamp_inc_u8_fe(batch->state.x677_y[idx]);
      if (stick_y >= com->lstick_tilt_y_thresh) {
        if (prev_stick_y >= com->lstick_tilt_y_thresh) {
          batch->state.x674[idx] = clamp_inc_u8_fe(batch->state.x674[idx]);
          batch->state.x67A_y[idx] = clamp_inc_u8_fe(batch->state.x67A_y[idx]);
        } else {
          batch->state.x677_y[idx] = 0;
          batch->state.x674[idx] = 0;
        }
      } else if (stick_y <= -com->lstick_tilt_y_thresh) {
        if (prev_stick_y <= -com->lstick_tilt_y_thresh) {
          batch->state.x674[idx] = clamp_inc_u8_fe(batch->state.x674[idx]);
          batch->state.x67A_y[idx] = clamp_inc_u8_fe(batch->state.x67A_y[idx]);
        } else {
          batch->state.x677_y[idx] = 0;
          batch->state.x674[idx] = 0;
        }
      } else {
        batch->state.x67A_y[idx] = 0xFEu;
        batch->state.x674[idx] = 0xFEu;
      }

      // lb_8000D148 zeroing for certain stick transitions.
      if (lb_8000D148(prev_stick_x, prev_stick_y, stick_x, stick_y, 0.0f, 0.0f,
                      com->lstick_tilt_x_thresh)) {
        batch->state.x67A_y[idx] = 0;
        batch->state.x679_x[idx] = 0;
      }

      // x678: increment (clamp to 0xFE) then reset on fresh trigger press.
      batch->state.x678[idx] = clamp_inc_u8_fe(batch->state.x678[idx]);
      if (trig >= com->powershield_reflect_trigger_min) {
        if (prev_trig >= com->powershield_reflect_trigger_min) {
          batch->state.x675[idx] = clamp_inc_u8_fe(batch->state.x675[idx]);
          batch->state.x67B[idx] = clamp_inc_u8_fe(batch->state.x67B[idx]);
        } else {
          batch->state.x67B[idx] = 0;
          batch->state.x678[idx] = 0;
          batch->state.x675[idx] = 0;
        }
      } else {
        batch->state.x67B[idx] = 0xFEu;
        batch->state.x675[idx] = 0xFEu;
      }

      // Button timers (saturating at 0xFF, reset to 0 on press). The MSL-visible
      // `input_buttons_pressed` lane stays in compact replay-button domain; this local view applies
      // the source Z->A macro only where the fighter input-history timers consume x668.
      const uint16_t pressed = input_edge_with_z_macro(batch->state.input_buttons_pressed[idx]);
      if ((pressed & A) != 0) {
        batch->state.x683[idx] = batch->state.x67C[idx];
        batch->state.x67C[idx] = 0;
      } else {
        batch->state.x67C[idx] = clamp_inc_u8_ff(batch->state.x67C[idx]);
      }

      if ((pressed & B) != 0) {
        batch->state.x67D[idx] = 0;
      } else {
        batch->state.x67D[idx] = clamp_inc_u8_ff(batch->state.x67D[idx]);
      }

      if ((pressed & XY) != 0) {
        batch->state.x67E[idx] = 0;
      } else {
        batch->state.x67E[idx] = clamp_inc_u8_ff(batch->state.x67E[idx]);
      }

      if ((pressed & DPAD_UP) != 0) {
        batch->state.x681[idx] = 0;
      } else {
        batch->state.x681[idx] = clamp_inc_u8_ff(batch->state.x681[idx]);
      }

      if ((pressed & DPAD_DOWN) != 0) {
        batch->state.x682[idx] = 0;
      } else {
        batch->state.x682[idx] = clamp_inc_u8_ff(batch->state.x682[idx]);
      }

      if ((pressed & LR) != 0) {
        batch->state.x684[idx] = batch->state.x680[idx];
        batch->state.x680[idx] = 0;
      } else {
        batch->state.x680[idx] = clamp_inc_u8_ff(batch->state.x680[idx]);
      }

      if (opening_input_lock_active_for_step(batch, bi)) {
        opening_input_lock_apply_Fighter_UnkInitLoad_80068914_Inner1_subset(batch, idx);
      }

      grounded_smash_charge_update_ftCo_800DF0D0_subset(batch, idx);
    }
  }

  return 0;
}
