#pragma once

#include <stddef.h>
#include <stdint.h>

// Init-time loader for typed fighter action-script timelines.
//
// Runtime consumers must use these typed tables instead of reparsing script payloads or adding
// local action-frame constants. The source owner is ftAction command dispatch on the fighter
// animation/script timebase.
// refs/melee/src/melee/ft/ftaction.c::{ftAction_80071820,ftAction_80071950,ftAction_80071974,ftAction_80073008,ftAction_80073240}
int script_events_init(void);

typedef enum MslScriptEventKind {
  MSL_SCRIPT_EVENT_CREATE_HITBOX = 1,
  MSL_SCRIPT_EVENT_SET_HITBOX_DAMAGE = 2,
  MSL_SCRIPT_EVENT_SET_HITBOX_SIZE = 3,
  MSL_SCRIPT_EVENT_SET_HITBOX_INTERACTION = 4,
  MSL_SCRIPT_EVENT_REMOVE_HITBOX = 5,
  MSL_SCRIPT_EVENT_CLEAR_HITBOXES = 6,
  MSL_SCRIPT_EVENT_SET_CMD_VAR = 7,
  MSL_SCRIPT_EVENT_SET_THROW_FLAGS = 8,
  MSL_SCRIPT_EVENT_ALLOW_INTERRUPT = 9,
  MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE = 10,
  MSL_SCRIPT_EVENT_SET_AIRBORNE_STATE = 11,
  MSL_SCRIPT_EVENT_SET_HIT_STATUS = 12,
  MSL_SCRIPT_EVENT_SET_ALL_HURT_STATE = 13,
  MSL_SCRIPT_EVENT_SET_HURT_STATE = 14,
  MSL_SCRIPT_EVENT_SET_JAB_COMBO = 15,
  MSL_SCRIPT_EVENT_SET_JAB_RAPID = 16,
  MSL_SCRIPT_EVENT_TOGGLE_BONE_PHYSICS = 17,
  MSL_SCRIPT_EVENT_SET_STATE_FLAGS_221C_U16_Y = 18,
  MSL_SCRIPT_EVENT_START_SMASH_CHARGE = 19,
  MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX = 20,
  MSL_SCRIPT_EVENT_SET_THROW_HITBOX = 21,
} MslScriptEventKind;

enum {
  MSL_SCRIPT_CREATE_HITBOX_FLAG_CLANK = 1u << 0,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_REBOUND = 1u << 1,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_HIT_GROUNDED = 1u << 2,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_HIT_AERIAL = 1u << 3,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_USE_COMMON_BONE_IDS = 1u << 4,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_IGNORE_FIGHTER_SCALE = 1u << 5,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_ITEM_HIT_INTERACTION = 1u << 6,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS = 1u << 7,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_ONLY_HIT_GRABBED = 1u << 8,
  MSL_SCRIPT_CREATE_HITBOX_FLAG_ITEM_MATCH_START_X138 = 1u << 9,
};

typedef struct MslScriptCreateHitboxPayload {
  float damage;
  float size;
  float x_offset;
  float y_offset;
  float z_offset;
  uint32_t flags;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t hitbox_id;
  uint8_t bone;
  uint8_t hit_group;
  uint8_t element;
  uint8_t sfx_kind;
  uint8_t sfx_severity;
  int8_t shield_damage;
  uint8_t rehit_frames;
} MslScriptCreateHitboxPayload;

typedef struct MslScriptThrowHitboxPayload {
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t idx;
  uint8_t element;
  uint8_t sfx_kind;
  uint8_t sfx_severity;
} MslScriptThrowHitboxPayload;

typedef struct MslScriptEvent {
  uint16_t frame;
  uint16_t kind_id;
  union {
    struct {
      uint8_t idx;
      uint16_t value;
    } cmd_var;
    struct {
      uint8_t hit_idx;
    } throw_flags;
    struct {
      float damage;
      uint8_t idx;
    } hitbox_damage;
    struct {
      uint8_t state;
    } state;
    struct {
      uint8_t bone_idx;
      uint8_t state;
    } hurt_state;
    struct {
      uint8_t disabled;
    } jab_combo;
    struct {
      uint16_t flags;
    } state_flags_221c;
    struct {
      float damage_mul;
      uint8_t hold_frames;
      uint8_t color_anim;
    } smash_charge;
    struct {
      uint8_t random_range;
      uint8_t volume;
      uint8_t panning;
      uint8_t behavior;
    } pseudo_random_sfx;
    MslScriptThrowHitboxPayload throw_hitbox;
    MslScriptCreateHitboxPayload create_hitbox;
  } payload;
} MslScriptEvent;

typedef struct MslScriptEventRange {
  const MslScriptEvent* events;
  uint32_t count;
} MslScriptEventRange;

typedef struct MslScriptFrameWindow {
  int16_t start_af;
  int16_t end_af;
  uint8_t loaded;
} MslScriptFrameWindow;

MslScriptEventRange script_events_range(uint8_t char_id, uint16_t msid);
const MslScriptEvent* script_events_first(uint8_t char_id, uint16_t msid, MslScriptEventKind kind);

uint8_t script_events_window_contains(MslScriptFrameWindow win, float frame);
uint8_t script_events_window_crossed(MslScriptFrameWindow win, float prev_frame, float cur_frame);
uint8_t script_events_frame_crossed(uint16_t frame, float prev_frame, float cur_frame);

uint8_t script_events_cmd_var_window(uint8_t char_id, uint16_t msid, uint8_t idx, uint8_t open_end,
                                     MslScriptFrameWindow* out);
uint8_t script_events_cmd_var_value_window(uint8_t char_id, uint16_t msid, uint8_t idx,
                                           uint8_t value, uint8_t open_end,
                                           MslScriptFrameWindow* out);
uint8_t script_events_allow_interrupt_window(uint8_t char_id, uint16_t msid,
                                             MslScriptFrameWindow* out);
uint8_t script_events_throw_flags_window(uint8_t char_id, uint16_t msid, uint8_t hit_idx,
                                         uint8_t use_hit_idx, MslScriptFrameWindow* out);
uint8_t script_events_throw_flags_pulses(uint8_t char_id, uint16_t msid, uint8_t hit_idx,
                                         uint16_t* out_frames, uint8_t max_out, uint8_t* out_count);
uint8_t script_events_first_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                MslScriptFrameWindow* out);
uint8_t script_events_second_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                 MslScriptFrameWindow* out);
uint8_t script_events_last_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                               MslScriptFrameWindow* out);
uint8_t script_events_post_clear_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                     MslScriptFrameWindow* out);
uint8_t script_events_hitbox_lifetime(uint8_t char_id, uint16_t msid, MslScriptFrameWindow* out);
uint8_t script_events_catchattack_grabbed_hit_window(uint8_t char_id, uint16_t msid,
                                                     MslScriptFrameWindow* out);
