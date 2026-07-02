#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"

uint8_t locomotion_landing_contact_y_owner_matches_source(uint8_t char_id, uint16_t source_act,
                                                          uint16_t source_action_frame,
                                                          uint16_t prev_source_act,
                                                          uint16_t land_act);
uint8_t locomotion_action_uses_ft80082b1c_basic_landing_callback(uint8_t char_id, uint16_t a);
uint16_t locomotion_ft80082b1c_basic_landing_action(const MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx, uint16_t source_act);
uint16_t locomotion_attackair_landing_action_for_contact(const MslBatch* batch, size_t idx,
                                                         uint16_t a);
float locomotion_landing_root_y_from_mpcoll_contact(const MslBatch* batch, size_t idx, size_t bi,
                                                    uint8_t preserve_fall_basic_dd90_order);
void locomotion_enter_landing_action_from_air(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                              size_t bi, uint16_t source_act, uint16_t land_act);
