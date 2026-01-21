#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api.h"

// Hot SoA state owned by a batch. All arrays are sized for MAX_PLAYERS/ITEMS.
typedef struct MslStateSoA {
  // Meta
  int32_t* frame_id;
  uint32_t* frame_pre_random_seed;
  uint32_t* stage_id;  // [batch]
  uint8_t* is_teams;   // [batch]
  uint8_t* team_id;    // [batch * MSL_MAX_PLAYERS]
  uint8_t* char_id;    // [batch * MSL_MAX_PLAYERS]

  // Kinematics
  float* pos_x;
  float* pos_y;
  float* prev_pos_x;  // Position at start of current frame (pre-integration).
  float* prev_pos_y;  // Position at start of current frame (pre-integration).
  float* speed_air_x_self;
  float* speed_ground_x_self;
  float* speed_y_self;
  float* speed_x_attack;
  float* speed_y_attack;
  uint8_t* facing;
  uint8_t* on_ground;
  uint8_t* prev_on_ground;  // on_ground value before stage_collision_apply().

  // State machine
  uint16_t* action_id;
  int16_t* action_frame;
  uint8_t* jumps_left;
  uint8_t* stocks;
  // Locomotion/input-history internals.
  // - `tilt_timer_*`, `turn_*`, and KneeBend internals are seeded from replay history (MslSeed).
  uint8_t*
      kneebend_jump_input;  // ftCo_JumpInput (refs/melee/src/melee/ft/chara/ftCommon/forward.h)
  uint8_t* kneebend_is_short_hop;  // latched during KneeBend IASA (ftCo_KneeBend_Check_ShortHop)
  uint8_t* tilt_timer_x;  // fp->x670_timer_lstick_tilt_x (refs/melee/src/melee/ft/fighter.c)
  uint8_t* tilt_timer_y;  // fp->x671_timer_lstick_tilt_y (refs/melee/src/melee/ft/fighter.c)
  uint8_t* fall_fast;     // fp->fall_fast (refs/melee/src/melee/ft/ftcommon.c:505-520)
  // FallSpecial internals (seeded/derived).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
  uint8_t* fallspecial_xc;       // fp->mv.co.fallspecial.xC (arg1 to ftCo_80096900)
  uint8_t* turn_has_turned;      // fp->mv.co.turn.has_turned (refs/melee/.../ftCo_Turn.c:39-44)
  uint8_t* turn_frames_to_turn;  // fp->mv.co.turn.frames_to_turn (refs/melee/.../ftCo_Turn.c:39-44)
  uint8_t* lr_press_timer;       // fp->x67F (refs/melee/src/melee/ft/fighter.c:2078-2086)
  uint8_t*
      x672_input_timer;  // fp->x672_input_timer_counter (refs/melee/src/melee/ft/fighter.c:2020-2050)
  // Fighter input counters block: refs/melee/src/melee/ft/fighter.c:1897-2094.
  uint8_t* x673;    // fp->x673
  uint8_t* x674;    // fp->x674
  uint8_t* x675;    // fp->x675
  uint8_t* x676_x;  // fp->x676_x
  uint8_t* x677_y;  // fp->x677_y
  uint8_t* x678;    // fp->x678
  uint8_t* x679_x;  // fp->x679_x
  uint8_t* x67A_y;  // fp->x67A_y
  uint8_t* x67B;    // fp->x67B
  uint8_t* x67C;    // fp->x67C
  uint8_t* x67D;    // fp->x67D
  uint8_t* x67E;    // fp->x67E
  uint8_t* x680;    // fp->x680
  uint8_t* x681;    // fp->x681
  uint8_t* x682;    // fp->x682
  uint8_t* x683;    // fp->x683
  uint8_t* x684;    // fp->x684

  // UCF pad buffer (seeded, multi-frame).
  // refs/ucf/include/ucf/pad_buffer.h
  //
  // `ucf_padbuf_stick_{x,y}` store the raw PAD bytes written by UCF's pad-buffer hook
  // (PADStatus.stick.{x,y}), with a fixed 4-entry ring buffer per player:
  // refs/ucf/src/pad_buffer/pad_buffer.cpp (buffer->index = (index+1)&3; entries[index] = status.stick)
  uint8_t* ucf_padbuf_index;            // [batch * players]
  uint8_t* ucf_padbuf_sdrop_up_frames;  // [batch * players]
  int8_t* ucf_padbuf_stick_x;           // [batch * players * 4]
  int8_t* ucf_padbuf_stick_y;           // [batch * players * 4]

  // Combat/timers
  float* percent;
  float* shield_hp;
  uint16_t* hitlag;
  uint16_t* hitstun;
  uint8_t* l_cancel;
  uint8_t* hurtbox_state;
  // Pose-driven world-space hurt capsule endpoints (computed each frame in hurtboxes_refresh).
  uint8_t* hurtcap_count; // [batch * players]
  float* hurtcap_a_x;     // [batch * players * caps]
  float* hurtcap_a_y;
  float* hurtcap_a_z;
  float* hurtcap_b_x;
  float* hurtcap_b_y;
  float* hurtcap_b_z;
  float* hurtcap_radius;
  uint8_t* hurtcap_is_grabbable;
  uint8_t* hurtcap_height;
  // Pose-driven world-space hitbox centers (computed each frame in hitboxes_refresh).
  // Debug readback only for now; not used for combat resolution yet.
  uint8_t* hitbox_count; // [batch * players]
  uint8_t* hitbox_enabled; // [batch * players * MSL_MAX_HITBOXES]
  float* hitbox_x;
  float* hitbox_y;
  float* hitbox_z;
  float* hitbox_radius;
  float* hitbox_damage;
  uint16_t* hitbox_bone_part_id;
  uint16_t* hitbox_u16_0;
  uint16_t* hitbox_u16_1;
  uint16_t* hitbox_u16_2;
  uint16_t* hitbox_u16_3;
  uint16_t* hitbox_u16_4;
  uint16_t* hitbox_u16_5;
  uint16_t* hitbox_u16_6;
  uint16_t* hitbox_u16_7;
  uint16_t* ground_id;
  uint32_t* animation_index;
  uint16_t* instance_hit_by;
  uint16_t* instance_id;
  uint8_t* last_attack_landed;
  uint8_t* combo_count;
  uint8_t* last_hit_by;
  uint8_t* state_flags;  // [batch * players * 5]

  // Inputs (processed, per-frame) written by input_apply.
  uint16_t* input_buttons;           // [batch * players]
  uint16_t* prev_input_buttons;      // [batch * players]
  uint16_t* input_buttons_pressed;   // [batch * players] (rising edge)
  uint16_t* input_buttons_released;  // [batch * players] (falling edge)
  int8_t* input_main_x;              // [batch * players] (legalized/clamped; -80..80)
  int8_t* input_main_y;              // [batch * players] (legalized/clamped; -80..80)
  int8_t* prev_input_main_x;         // [batch * players] (processed from prev_input_bytes)
  int8_t* prev_input_main_y;         // [batch * players] (processed from prev_input_bytes)
  int8_t* input_c_x;                 // [batch * players] (legalized/clamped; -80..80)
  int8_t* input_c_y;                 // [batch * players] (legalized/clamped; -80..80)
  uint8_t* input_l;                  // [batch * players] (0..255)
  uint8_t* input_r;                  // [batch * players] (0..255)

  // Items (fixed-capacity, per-batch)
  uint8_t* item_exists;        // [batch * MSL_MAX_ITEMS]
  uint8_t* item_state;         // [batch * MSL_MAX_ITEMS]
  uint16_t* item_type;         // [batch * MSL_MAX_ITEMS]
  int8_t* item_owner;          // [batch * MSL_MAX_ITEMS]
  uint16_t* item_instance_id;  // [batch * MSL_MAX_ITEMS]
  float* item_direction;       // [batch * MSL_MAX_ITEMS]
  float* item_vel_x;           // [batch * MSL_MAX_ITEMS]
  float* item_vel_y;           // [batch * MSL_MAX_ITEMS]
  float* item_pos_x;           // [batch * MSL_MAX_ITEMS]
  float* item_pos_y;           // [batch * MSL_MAX_ITEMS]
  uint16_t* item_damage;       // [batch * MSL_MAX_ITEMS]
  float* item_timer;           // [batch * MSL_MAX_ITEMS]
  uint32_t* item_spawn_id;     // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc0;         // [batch * MSL_MAX_ITEMS]
  uint8_t* item_misc1;
  uint8_t* item_misc2;
  uint8_t* item_misc3;
} MslStateSoA;

int state_alloc(MslStateSoA* state, int batch_size);
void state_free(MslStateSoA* state);
