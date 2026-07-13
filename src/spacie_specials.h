#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"
#include "special_msids.h"

uint16_t spacie_fx_kind_action(uint8_t char_id, uint8_t fx_kind);
uint8_t spacie_char_owns_fx_kind(uint8_t char_id, uint8_t fx_kind);
uint8_t spacie_run_iasa_has_b_special_intent(uint8_t char_id, uint16_t buttons_pressed);

void spacie_dash_iasa_enter_grounded_side_special_start(MslBatch* batch, size_t idx,
                                                        const MslCommonParams* c,
                                                        const MslSpecialMsids* ms,
                                                        const MslCharParams* ch, float stick_x);

uint8_t spacie_specialhi_update(MslBatch* batch, size_t idx, uint8_t char_id,
                                const MslSpecialMsids* ms, uint8_t on_ground);

uint8_t ftfx_specials_anim_update(MslBatch* batch, const MslSpecialMsids* ms,
                                  const MslCharParams* ch, size_t idx, uint8_t on_ground,
                                  uint16_t buttons_pressed, float facing_dir,
                                  uint16_t* action_id_io);

void spacie_side_special_ground_to_air_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t grounded_action);
void spacie_side_special_air_to_ground_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t air_action);

uint8_t spacie_specialhi_hold_air_ground_contact(MslBatch* batch, const MslSpecialMsids* ms,
                                                 const MslCharParams* ch, size_t idx);
void spacie_specialhi_apply_collision_facing_dir(MslBatch* batch, const MslCharParams* ch,
                                                 size_t idx);
uint8_t spacie_specialhi_floor_contact_should_bound(const MslBatch* batch, const MslCharParams* ch,
                                                    size_t idx);
void spacie_enter_specialhi_bound_from_airhi_collision(MslBatch* batch, const MslCharParams* ch,
                                                       size_t idx);
uint8_t spacie_side_special_air_contact_to_ground(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t action_id);
uint8_t spacie_side_special_ground_floor_loss_to_air(MslBatch* batch, const MslSpecialMsids* ms,
                                                     const MslCharParams* ch, size_t idx,
                                                     uint16_t action_id);
