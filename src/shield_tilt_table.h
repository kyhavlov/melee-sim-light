#pragma once

#include <stdint.h>

// Init-only loader for the ISO-derived ftData.x20 Guard target tree. Runtime combines these local
// SRTs with the ordinary Guard animation and the persistent live JObj tree in anim_pose.c.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091E78
// refs/melee/src/melee/ft/ftanim.c::{ftAnim_80070010,ftAnim_80070108}
int shield_tilt_table_init(void);

uint16_t msl_shield_guard_neutral_frame(uint8_t char_id);
uint16_t msl_shield_guard_frame_count(uint8_t char_id);
uint16_t msl_shield_part_id(uint8_t char_id);
int msl_shield_guard_target_srt(uint8_t char_id, uint16_t part_id, float out_rot[3],
                                float out_pos[3], float out_scl[3]);
