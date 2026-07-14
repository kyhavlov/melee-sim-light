#include "shields.h"

#include "anim_table.h"
#include <float.h>
#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "guard_lifecycle.h"
#include "msl_math.h"
#include "shield_tilt_table.h"

static inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float apply_deadzone_f32(float v, float dz) {
  // Match tools/slippi/seed_history.py::apply_deadzone and common stick handling across the sim:
  // per-axis deadzone in unit space.
  return (fabsf(v) < dz) ? 0.0f : v;
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

static inline uint16_t clamp_u16(uint16_t x, uint16_t lo, uint16_t hi) {
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline float normalize_angle_180(float deg) {
  // Decomp: ftCo_Guard.c::normalizeAngle180 (single wrap into [-180, 180]).
  if (deg > 180.0f) {
    deg -= 360.0f;
  } else if (deg < -180.0f) {
    deg += 360.0f;
  }
  return deg;
}

static inline float normalize_angle_0(float deg) {
  // Decomp: ftCo_Guard.c::normalizeAngle0 (single wrap into [0, 360]).
  if (deg > 360.0f) {
    deg -= 360.0f;
  } else if (deg < 0.0f) {
    deg += 360.0f;
  }
  return deg;
}

static inline float sanitize_lightshield_amount(float light) {
  if (!isfinite(light)) {
    return 0.0f;
  }
  return clamp01(light);
}

void shields_refresh_guard_tilt_body_owner(MslBatch* batch) {
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
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD ||
          batch->state.animation_index[idx] != UINT32_MAX || batch->state.action_frame[idx] >= 0 ||
          batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u ||
          (batch->state.pos_z[idx] <= 1.0e-6f && batch->state.pos_z[idx] >= -1.0e-6f) ||
          batch->state.stocks[idx] == 0u || batch->state.shield_hp[idx] <= 0.0f ||
          c->start_shield_health <= 0.0f) {
        continue;
      }

      const MslCharParams* ca = msl_char_params_fast(batch->state.char_id[idx]);
      MslShieldTiltTableView tv;
      if (ca == NULL || msl_shield_tilt_table_view(batch->state.char_id[idx], &tv) != 0 ||
          tv.xyz == NULL || tv.frame_count == 0u) {
        continue;
      }

      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      float stick_x_unit = (float)batch->state.input_main_x[idx] * (1.0f / 80.0f);
      float stick_y_unit = (float)batch->state.input_main_y[idx] * (1.0f / 80.0f);
      stick_x_unit = apply_deadzone_f32(stick_x_unit, c->lstick_deadzone_x);
      stick_y_unit = apply_deadzone_f32(stick_y_unit, c->lstick_deadzone_y);
      const uint8_t current_tilt_input = (stick_x_unit != 0.0f || stick_y_unit != 0.0f) ? 1u : 0u;
      if (batch->state.guard_tilt_x4[idx] <= 0.0f ||
          (batch->state.guard_tilt_x8[idx] == tv.neutral_frame && current_tilt_input == 0u)) {
        continue;
      }

      const uint16_t neutral = tv.neutral_frame;
      const uint16_t frame_max = (uint16_t)(tv.frame_count - 1u);
      const float x = stick_x_unit * facing_dir;
      const float y = stick_y_unit;
      float rad = msl_melee_lb_angle(y, x);
      if (rad < 0.0f) {
        rad += 2.0f * MSL_PI_F;
      }
      float deg = rad * MSL_RAD_TO_DEG_F;
      if (deg < 0.0f) {
        deg = 0.0f;
      }
      if (deg > 359.0f) {
        deg = 359.0f;
      }

      const float offset = batch->state.guard_tilt_x8_f32[idx] - (float)neutral;
      const float delta = normalize_angle_180(deg - offset);
      const float lerp = c->guard_stick_lerp_x44c;
      const float next_offset = normalize_angle_0(fmaf(lerp, delta, offset));
      const float next_x8_f = (float)neutral + next_offset;
      batch->state.guard_tilt_x8_f32[idx] = next_x8_f;
      batch->state.guard_tilt_x8[idx] = clamp_u16((uint16_t)next_x8_f, 0, frame_max);

      float mag = msl_melee_sqrtf(stick_x_unit * stick_x_unit + stick_y_unit * stick_y_unit);
      if (mag > 1.0f) {
        mag = 1.0f;
      }
      if (mag < 0.0f) {
        mag = 0.0f;
      }
      const float x4 = batch->state.guard_tilt_x4[idx];
      batch->state.guard_tilt_x4[idx] = fmaf(lerp, mag - x4, x4);

      const float tilt_mag = clamp01(batch->state.guard_tilt_x4[idx]);
      const size_t n_i = (size_t)neutral * 3u;
      float target[3];
      if (msl_shield_tilt_target_xyz_f32(batch->state.char_id[idx], &tv,
                                         batch->state.guard_tilt_x8_f32[idx], target) != 0) {
        const uint16_t f = clamp_u16(batch->state.guard_tilt_x8[idx], 0, frame_max);
        const size_t f_i = (size_t)f * 3u;
        target[0] = tv.xyz[f_i + 0];
        target[1] = tv.xyz[f_i + 1];
        target[2] = tv.xyz[f_i + 2];
      }
      const float dx = tv.xyz[n_i + 0] + tilt_mag * (target[0] - tv.xyz[n_i + 0]);
      const float dy = tv.xyz[n_i + 1] + tilt_mag * (target[1] - tv.xyz[n_i + 1]);
      const float dz = tv.xyz[n_i + 2] + tilt_mag * (target[2] - tv.xyz[n_i + 2]);
      const float scale_y = batch->state.fighter_scale_y[idx];
      const float lx = dx * scale_y;
      const float ly = dy * scale_y;
      const float lz = dz * scale_y;
      batch->state.shield_x[idx] = batch->state.pos_x[idx] + facing_dir * lz;
      batch->state.shield_y[idx] = batch->state.pos_y[idx] + ly;
      batch->state.shield_z[idx] = batch->state.pos_z[idx] - facing_dir * lx;

      float light = sanitize_lightshield_amount(batch->state.lightshield_amount[idx]);
      const float denom = 1.0f - c->trigger_deadzone;
      const float trig = trigger_unit_from_input(
          batch->state.input_buttons[idx], batch->state.input_l[idx], batch->state.input_r[idx]);
      if (denom > 0.0f) {
        const float candidate = (trig - c->trigger_deadzone) / denom;
        if (candidate >= 0.0f) {
          light = clamp01(candidate);
        }
      }
      batch->state.lightshield_amount[idx] = light;
      const float hp_ratio = clamp01(batch->state.shield_hp[idx] / c->start_shield_health);
      const float light_scale =
          (light * (c->shield_size_lightshield_max - c->shield_size_lightshield_min)) +
          c->shield_size_lightshield_min;
      const float scale =
          ((1.0f - c->shield_size_min_scale) * hp_ratio * light_scale) + c->shield_size_min_scale;
      batch->state.shield_radius[idx] = scale * ca->initial_shield_size * scale_y;
    }
  }
}

void shields_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

  // Shield bubble size follows ftCo_Guard.c's inlineB0:
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:172-190.
  //
  // Notes:
  // - `initial_shield_size` is per-character (co attrs; ISO-extracted to data/characters/*.json).
  // - `shield_size_*` and `start_shield_health` are ftCommonData fields (ISO-extracted to
  //   data/common/ft_common_data.json).
  // - Shield bubble center is pose/guard-derived in Melee (via a dedicated "shield bone" whose
  //   translation is driven by guard tilt). We approximate this by sampling an ISO-derived guard
  //   tilt table (data/shields/*.bin) and applying stick direction + facing.

  const float denom = 1.0f - c->trigger_deadzone;
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Default center approximation: fighter world position; z=0 in our current 2D stage model.
      // If guard tilt data is available and the shield is active, we override this below.
      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];
      float sx = pos_x;
      float sy = pos_y;
      float sz = pos_z;
      float sr = 0.0f;

      const uint8_t stocks = batch->state.stocks[idx];
      if (stocks != 0 &&
          msl_guard_lifecycle_action_has_shield_callback(batch->state.action_id[idx]) &&
          batch->state.shield_hp[idx] > 0.0f && c->start_shield_health > 0.0f) {
        const MslCharParams* ca = msl_char_params_fast(batch->state.char_id[idx]);
        if (ca != NULL) {
          const float scale_y = batch->state.fighter_scale_y[idx];

          // Guard-tilt shield bubble center (decomp-shaped):
          // - Sample the ISO-derived msid=38 ("Guard") tilt timeline in data/shields/<char>.bin.
          // - Use stick direction (main stick) and facing to choose an angle frame.
          // - Blend towards that angled center based on inertial stick magnitude state (x4; 0..1).
          //
          // NOTE (model_scaling): `lb_8000B1CC(shield_hit->bone, ...)` reads the live shield-bone
          // world matrix after the fighter root model scale has been applied. Even though
          // ftAnim_8006FA58/ftAnim_8006FB88 install the inverse-scale collision subtree for body
          // capsules, Dolphin probes of `lbColl_80007BCC` show the ShieldDesc bone position follows
          // the model-scaled guard pose. Apply both `fighter_scale_y` and `co_attrs.model_scaling`.
          //
          // Decomp refs:
          // - refs/melee/src/melee/ft/ftcommon.c::ftCommon_GetModelScale
          // - refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FA58 and ::ftAnim_8006FB88 (inv-scale part x10)
          // - refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F6A4 (inverse model_scaling application)
          // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c (shield bone `fp->ft_data->x8->x11`)
          MslShieldTiltTableView tv = {0};
          const uint8_t has_tv = (msl_shield_tilt_table_view(batch->state.char_id[idx], &tv) == 0 &&
                                  tv.xyz != NULL && tv.frame_count > 0)
                                     ? 1
                                     : 0;

          // Guard tilt state update (independent of whether a shield table exists for this character).
          const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          // Decomp uses fp->input.lstick.{x,y} which are post-UCF and deadzoned floats.
          // Match preprocessing derivation (tools/slippi/seed_history.py) by applying the ftCommonData
          // per-axis deadzones here before ftCo_80091BC4 math.
          float stick_x_unit = (float)batch->state.input_main_x[idx] * (1.0f / 80.0f);
          float stick_y_unit = (float)batch->state.input_main_y[idx] * (1.0f / 80.0f);
          stick_x_unit = apply_deadzone_f32(stick_x_unit, c->lstick_deadzone_x);
          stick_y_unit = apply_deadzone_f32(stick_y_unit, c->lstick_deadzone_y);

          // Decomp: ftCo_800921DC initializes mv.co.guard.x8 = 10 and x4 = 0 on GuardOn entry.
          // Our shield tables expose the neutral frame explicitly; fall back to GALE01's 10 if missing.
          uint16_t neutral = 10;
          uint16_t frame_max = (uint16_t)(neutral + 360u);  // decomp: x8 is neutral + [0..360]
          if (has_tv) {
            neutral = tv.neutral_frame;
            frame_max = (uint16_t)(tv.frame_count - 1);
          }

          // Decomp init on GuardOn entry.
          if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON &&
              batch->state.action_frame[idx] == 0) {
            batch->state.guard_tilt_x8[idx] = neutral;
            batch->state.guard_tilt_x8_f32[idx] = (float)neutral;
            batch->state.guard_tilt_x4[idx] = 0.0f;
          }

          if (msl_guard_lifecycle_action_updates_tilt(batch->state.action_id[idx])) {
            const float x = stick_x_unit * facing_dir;
            const float y = stick_y_unit;

            float rad = msl_melee_lb_angle(y, x);
            if (rad < 0.0f) {
              rad += 2.0f * MSL_PI_F;
            }
            float deg = rad * MSL_RAD_TO_DEG_F;
            // Decomp: ftCo_80091BC4 clamps lstick_deg to [0, 359].
            if (deg < 0.0f) {
              deg = 0.0f;
            }
            if (deg > 359.0f) {
              deg = 359.0f;
            }

            const float offset = batch->state.guard_tilt_x8_f32[idx] - (float)neutral;
            const float delta = normalize_angle_180(deg - offset);
            const float lerp = c->guard_stick_lerp_x44c;
            const float next_offset = normalize_angle_0(fmaf(lerp, delta, offset));
            const float next_x8_f = (float)neutral + next_offset;
            batch->state.guard_tilt_x8_f32[idx] = next_x8_f;
            batch->state.guard_tilt_x8[idx] = clamp_u16((uint16_t)next_x8_f, 0, frame_max);

            float mag = msl_melee_sqrtf(stick_x_unit * stick_x_unit + stick_y_unit * stick_y_unit);
            if (mag > 1.0f) {
              mag = 1.0f;
            }
            if (mag < 0.0f) {
              mag = 0.0f;
            }
            const float x4 = batch->state.guard_tilt_x4[idx];
            batch->state.guard_tilt_x4[idx] = fmaf(lerp, mag - x4, x4);
          }

          if (has_tv) {
            const uint16_t f = clamp_u16(batch->state.guard_tilt_x8[idx], 0, frame_max);
            const float mag = clamp01(batch->state.guard_tilt_x4[idx]);
            const uint8_t guard_on_no_submotion_entry =
                (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON &&
                 batch->state.animation_index[idx] == UINT32_MAX &&
                 batch->state.action_frame[idx] < 0 &&
                 !msl_guard_lifecycle_action_has_shield_callback(
                     batch->state.seed_prev_action_id[idx]) &&
                 tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u)
                    ? 1u
                    : 0u;
            const uint8_t guard_on_no_submotion_sustained =
                (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON &&
                 batch->state.animation_index[idx] == UINT32_MAX &&
                 batch->state.action_frame[idx] < 0 &&
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON &&
                 tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u)
                    ? 1u
                    : 0u;
            const uint16_t guard_reflect_prev_action = batch->state.seed_prev_action_id[idx];
            const uint8_t guard_reflect_locomotion_no_submotion_entry =
                (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
                 batch->state.animation_index[idx] == UINT32_MAX &&
                 batch->state.action_frame[idx] < 0 &&
                 msl_guard_reflect_entry_uses_guardon_pose_source(guard_reflect_prev_action) &&
                 guard_reflect_prev_action != (uint16_t)MSL_ACT_GUARD_ON &&
                 guard_reflect_prev_action != (uint16_t)MSL_ACT_GUARD &&
                 guard_reflect_prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
                 guard_reflect_prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF &&
                 tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u)
                    ? 1u
                    : 0u;
            const uint8_t steady_guard_no_tilt =
                (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD &&
                 (mag == 0.0f || mag < FLT_MIN))
                    ? 1u
                    : 0u;

            // Decomp owner:
            // - `mv.co.guard.x8` is initialized to 10, but `ftCo_80091E78` only samples the
            //   angled Guard timeline through `ftAnim_80070710(..., x8)` when `mv.co.guard.x4`
            //   is nonzero.
            // - With `x4 == 0`, ShieldDesc collision uses the current no-tilt Guard pose, i.e. the
            //   table's frame-0 live pose, not the x8 neutral target frame.
            // - Keep this lane to steady Guard. GuardOn/GuardReflect have separate entry/reflect
            //   pose owners, and nonzero x4 follows the angled `x8` branch.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091BC4,ftCo_80091E78}
            float dx = 0.0f;
            float dy = 0.0f;
            float dz = 0.0f;
            if (guard_on_no_submotion_entry || guard_reflect_locomotion_no_submotion_entry) {
              // GuardOn entry creates ShieldDesc after ftAnim_8006EBA4 but then ftCo_800921DC
              // zeroes the guard joint translate and calls ftCo_80091E78(..., 0). Locomotion-origin
              // GuardReflect (`ftCo_80091A4C -> ftCo_80093A50`) calls the same ftCo_800921DC after
              // installing ShieldDesc and ReflectDesc. While Slippi exposes these first visible
              // snapshots with animation_index=-1/action_frame=-1, lbColl_80007BCC still consumes
              // that live current-pose shield bone rather than the settled steady-Guard neutral
              // target. Scope this to real non-shield entry snapshots using the previous post-frame
              // owner lane; later no-submotion GuardOn/GuardReflect rows keep their normal
              // tilt/neutral placement.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
              //   ftCo_800924C0,ftCo_80093A50,ftCo_800921DC,ftCo_80091E78}
              // data/shields/{fox,falco}.bin::guard_on_xyz[0]
              dx = tv.guard_on_xyz[0];
              dy = tv.guard_on_xyz[1];
              dz = tv.guard_on_xyz[2];
            } else if (guard_on_no_submotion_sustained) {
              // Sustained no-submotion GuardOn snapshots expose action_frame=-1/anim=-1 while the
              // live ShieldDesc bone has already been republished by the prior GuardOn_Anim
              // callback. Collision samples that previously published JObj before the current
              // frame's `ftCo_800925A4`/x10 decrement, so reconstruct the publication index from
              // the post-frame countdown as x10 + 1 and sample the extracted GuardOn current-pose
              // trajectory. This keeps free rollout on live ShieldDesc geometry instead of relying
              // on the one-step-only `combat_shield_contact_hb_kind` seed bridge.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
              //   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
              // data/shields/<char>.bin::guard_on_xyz (MSLSHLD1 v4)
              uint16_t go_frame = (uint16_t)batch->state.guard_x10[idx] + 1u;
              if (go_frame >= tv.guard_on_frame_count) {
                go_frame = (uint16_t)(tv.guard_on_frame_count - 1u);
              }
              const size_t go_i = (size_t)go_frame * 3u;
              dx = tv.guard_on_xyz[go_i + 0u];
              dy = tv.guard_on_xyz[go_i + 1u];
              dz = tv.guard_on_xyz[go_i + 2u];
              if (mag > 0.0f) {
                const size_t f_i = (size_t)f * 3u;
                const float fx = tv.xyz[f_i + 0u];
                const float fy = tv.xyz[f_i + 1u];
                const float fz = tv.xyz[f_i + 2u];
                dx += mag * (fx - dx);
                dy += mag * (fy - dy);
                dz += mag * (fz - dz);
              }
            } else {
              const size_t n_i = steady_guard_no_tilt ? 0u : (size_t)neutral * 3u;
              const size_t f_i = (size_t)f * 3u;
              const float nx = tv.xyz[n_i + 0];
              const float ny = tv.xyz[n_i + 1];
              const float nz = tv.xyz[n_i + 2];
              const float fx = tv.xyz[f_i + 0];
              const float fy = tv.xyz[f_i + 1];
              const float fz = tv.xyz[f_i + 2];

              dx = nx + mag * (fx - nx);
              dy = ny + mag * (fy - ny);
              dz = nz + mag * (fz - nz);
            }

            const float model_scaling = (isfinite(ca->model_scaling) && ca->model_scaling > 0.0f)
                                            ? ca->model_scaling
                                            : 1.0f;
            const float pose_scale = (steady_guard_no_tilt || guard_on_no_submotion_entry ||
                                      guard_on_no_submotion_sustained)
                                         ? (scale_y * model_scaling)
                                         : scale_y;

            // Match the ShieldDesc bone policy above: the no-tilt steady-Guard lane uses the live
            // model-scaled ShieldDesc bone. The no-submotion GuardOn entry snapshot uses the same
            // live model-scaled ShieldDesc bone from ftCo_800921DC/ftCo_80091E78(0), and sustained
            // no-submotion GuardOn samples the same ShieldDesc bone through the extracted current
            // GuardOn pose trajectory; other guard actions retain the existing collision-subtree
            // scaling policy until their separate pose owners are proved.
            //
            // Facing parity: same as hurtcaps_refresh() / in-engine root part rotY = (M_PI_2 * fp->facing_dir),
            // which mixes X/Z in world space.
            // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
            const float lx = dx * pose_scale;
            const float ly = dy * pose_scale;
            const float lz = dz * pose_scale;
            const float off_x = facing_dir * lz;
            const float off_z = -facing_dir * lx;
            sx = pos_x + off_x;
            sy = pos_y + ly;
            sz = pos_z + off_z;
          }

          float light = sanitize_lightshield_amount(batch->state.lightshield_amount[idx]);
          const float trig =
              trigger_unit_from_input(batch->state.input_buttons[idx], batch->state.input_l[idx],
                                      batch->state.input_r[idx]);
          if (msl_guard_lifecycle_action_updates_tilt(batch->state.action_id[idx])) {
            // Decomp owner: `ftCo_800925A4` snapshots `fp->lightshield_amount` into
            // `mv.co.guard.x2C`, then only overwrites it when the current trigger is above the
            // shield deadzone. Released-trigger Guard/GuardOn/GuardReflect frames therefore keep
            // the previous lightshield bubble scale. GuardSetOff does not run this update and
            // consumes the preserved entry amount.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4}
            if (denom > 0.0f) {
              const float candidate = (trig - c->trigger_deadzone) / denom;
              if (candidate >= 0.0f) {
                light = clamp01(candidate);
              }
            }
            batch->state.lightshield_amount[idx] = light;
          }
          const uint8_t guard_second_frame =
              ((batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON ||
                batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) &&
               batch->state.guard_on_entered_this_frame[idx] == 0u &&
               batch->state.guard_reflect_entered_this_frame[idx] == 0u &&
               c->guard_x10_init_frames > 1.0f &&
               batch->state.guard_x10[idx] == (uint8_t)((uint16_t)c->guard_x10_init_frames - 1u))
                  ? 1u
                  : 0u;
          if (guard_second_frame && denom > 0.0f) {
            // Second guard frame: ftCo_800921DC seeds fp->lightshield_amount as
            // input.x650 / (1 - x10) with no deadzone subtraction and no upper clamp, so a full
            // digital press yields ~1.43. The collision matrix phase makes this visible on the
            // SECOND guard frame only: the entry frame's fighter pass still consumes the
            // pre-entry matrix (same-frame entry contact locks hit the full bubble), the entry's
            // ftCo_80091D58 scale is what the next frame's collision reads, and ftCo_800925A4
            // then overwrites the lane so frame 3+ returns to steady. Suite-wide witness fit
            // (reports/triage/maj_burn/shield_boundary_sweep.py, first-of-window kind-1/kind-2
            // rows): the deepest static-radius "misses inside" (-2.5) match steady(8.0) -
            // entry(5.5) on second-frame rows.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4}
            const float entry_candidate = trig / denom;
            if (entry_candidate >= 0.0f) {
              light = entry_candidate;
            }
          }
          const float hp_ratio = clamp01(batch->state.shield_hp[idx] / c->start_shield_health);
          float light_scale =
              (light * (c->shield_size_lightshield_max - c->shield_size_lightshield_min)) +
              c->shield_size_lightshield_min;
          if (light_scale < 0.0f) {
            light_scale = 0.0f;
          }
          const float n1 = hp_ratio * light_scale;
          const float n2 = 1.0f - c->shield_size_min_scale;
          const float scale = (n2 * n1) + c->shield_size_min_scale;

          // NOTE (shield radius): In-engine shield collision uses joint transforms + a separate
          // radius term; our debug/approx shield bubble keeps the prior scaling policy (only the
          // per-fighter fp->x34_scale.y) to avoid global mismatch shifts.
          sr = scale * ca->initial_shield_size * scale_y;
        }
      }

      batch->state.shield_x[idx] = sx;
      batch->state.shield_y[idx] = sy;
      batch->state.shield_z[idx] = sz;
      batch->state.shield_radius[idx] = sr;
      // Keep `state_flags` "isShieldActive" in sync with the derived shield bubble.
      //
      // Decomp-first references (GALE01):
      // - Shield activation is represented by `fp->x221B_b0`:
      //   - set to true in ftColl_8007B1B8 (shield desc init),
      //     refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8.
      //   - consulted by the main fighter-vs-fighter collision loop to gate shield collision checks,
      //     refs/melee/src/melee/ft/ftcoll.c (see `if (this_fp->x221B_b0) { ... lbColl_80007BCC(...shield...) ... }`).
      // - Bitfield layout at fp+0x221B is documented in refs/melee/src/melee/ft/types.h.
      //
      // Slippi post-frame: `lbz r3,0x221B(REG_PlayerData)  #0x80 = isShieldActive`.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      uint8_t f = batch->state.state_flags[flags_i];
      // Decomp: fp->x221B_b0 is toggled by collision "shield desc" creation/destruction:
      // - set by ftColl_8007B1B8 (shield desc init),
      // - cleared on shield break (ftCo_800925A4) and on the default GuardReflect entry path
      //   (ftCo_8009388C).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_8009388C}
      //
      // GuardReflect nuance:
      // - GuardOn IASA path (ftCo_80093694 -> ftCo_80093850 -> ftCo_8009388C) clears fp->x221B_b0.
      // - Locomotion guard-check path (ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50) calls
      //   ftCo_80092450 on entry, which recreates shield desc and sets fp->x221B_b0.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_IASA,ftCo_80093694,ftCo_8009388C,ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_80092450}
      //
      // Sim mapping without new hidden state:
      // - Use prev_action_id (cached at frame start) to detect GuardReflect entry.
      // - GuardOn -> GuardReflect uses the clear path.
      // - All other entry sources use the set path.
      // - While GuardReflect x14 is active, preserve the selected entry-path value.
      const uint8_t in_guard_reflect =
          (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) ? 1u : 0u;
      const uint8_t guard_reflect_timer_active =
          (in_guard_reflect && batch->state.guard_reflect_timer_x14[idx] != 0) ? 1u : 0u;
      const uint8_t entered_guard_reflect =
          (in_guard_reflect && batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT)
              ? 1u
              : 0u;
      const uint8_t guard_family_shielddesc =
          (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD ||
           batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_OFF ||
           batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF)
              ? 1u
              : 0u;

      // Always clear when the shield is broken / absent (decomp clears x221B_b0 on break).
      if (!(batch->state.stocks[idx] != 0 && batch->state.shield_hp[idx] > 0.0f)) {
        f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
      } else if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON) {
        // GuardOn entry path creates shield desc via ftCo_80092450 before GuardOn motion state setup.
        // Keep fp+0x221B_b0 ownership aligned on GuardOn entry even when this frame has no resolved
        // shield bubble radius sample yet. ftCo_80092450 calls ftColl_8007B1B8, which sets
        // x221B_b0 and clears x221B_b1..b4; do not carry stale Counter x221B_b1 into ordinary
        // GuardOn entry snapshots.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092450
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
        f |= (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
        f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B1;
      } else if (entered_guard_reflect) {
        const uint16_t prev_a = batch->state.prev_action_id[idx];
        if (prev_a == (uint16_t)MSL_ACT_GUARD_ON) {
          // GuardOn_IASA powershield path (ftCo_8009388C): clear on entry.
          f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
        } else {
          // Locomotion guard-check powershield path (ftCo_80093A50 -> ftCo_80092450): set on entry.
          f |= (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
        }
      } else if (guard_reflect_timer_active) {
        // Preserve.
      } else if (sr > 0.0f) {
        f |= (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
        if (guard_family_shielddesc != 0u) {
          // Sustained common guard-family ShieldDesc publication is owned by the same
          // ftColl_8007B1B8-created descriptor, so stale non-guard x221B_b1 must not survive.
          // Do not apply this to every positive-radius descriptor: Marth Counter and other
          // character specials set x221B_b1/b2/b3/b4 after creating their own descriptors.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
          // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c
          f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B1;
        }
      } else {
        f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
      }
      batch->state.state_flags[flags_i] = f;
    }
  }
}
