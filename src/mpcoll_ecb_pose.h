#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "mpcoll_ecb_points.h"

uint8_t mpcoll_non_air_common_damage_submotion(uint32_t smid);
uint8_t mpcoll_ground_specialhi_uses_jobj_ecb(uint8_t char_id, uint16_t action_id);
uint8_t mpcoll_ground_damageflyroll_uses_jobj_ecb(uint16_t action_id);

uint8_t mpcoll_ground_try_sample_damageflyroll_jobj_ecb(MslEcbWorldPoints* out,
                                                        const MslBatch* batch, size_t idx,
                                                        uint8_t char_id, uint32_t anim,
                                                        uint16_t action_id, uint16_t frame_u16,
                                                        float facing_dir, float pos_x, float pos_y);

uint8_t mpcoll_ground_try_sample_specialhi_jobj_ecb(MslEcbWorldPoints* out, const MslBatch* batch,
                                                    size_t idx, uint8_t char_id, uint32_t anim,
                                                    uint16_t action_id, uint16_t frame_u16,
                                                    float facing_dir, float pos_x, float pos_y);

float mpcoll_pose_ecb_bottom_rel_y(uint8_t char_id, uint32_t anim, uint16_t frame_u16,
                                   uint8_t lock_bottom_to_zero);
float mpcoll_action_pose_ecb_bottom_rel_y(uint8_t char_id, uint16_t action_id, int16_t action_frame,
                                          uint8_t lock_bottom_to_zero);

uint8_t mpcoll_common_fall_blended_ecb_live_owner(const MslBatch* batch, size_t idx,
                                                  uint16_t action_id);
uint8_t mpcoll_common_fall_blended_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch,
                                              size_t idx, uint8_t char_id, uint16_t msid,
                                              uint16_t frame_u16, float facing_dir, float pos_x,
                                              float pos_y);
uint8_t mpcoll_vanish_jobj_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch, size_t idx,
                                      uint8_t char_id, uint16_t msid, uint16_t frame_u16,
                                      float facing_dir, float pos_x, float pos_y);

void mpcoll_bottom_world_point_from_rel(MslEcbBottomWorldPoint* out, float pos_x, float pos_y,
                                        float bottom_rel_y, uint16_t frame_u16);
void mpcoll_ecb_world_points_from_rel(MslEcbWorldPoints* out, float pos_x, float pos_y,
                                      float bottom_rel_y, float top_rel_y, float left_rel_x,
                                      float right_rel_x, float side_rel_y, uint16_t frame_u16);
void mpcoll_ecb_points_apply_jobj_horizontal_normalization(const MslBatch* batch, size_t idx,
                                                           MslEcbWorldPoints* ecb);

uint8_t mpcoll_state_current_ecb_points(const MslBatch* batch, size_t idx, MslEcbWorldPoints* out,
                                        float pos_x, float pos_y, uint16_t frame_u16);
uint8_t mpcoll_state_squeeze_restore_ecb_points(const MslBatch* batch, size_t idx,
                                                MslEcbWorldPoints* out, float pos_x, float pos_y,
                                                uint16_t frame_u16);
uint8_t mpcoll_state_desired_ecb_points(const MslBatch* batch, size_t idx, MslEcbWorldPoints* out,
                                        float pos_x, float pos_y, uint16_t frame_u16);

void mpcoll_store_current_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb);
void mpcoll_store_prev_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb);
void mpcoll_store_desired_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb);
