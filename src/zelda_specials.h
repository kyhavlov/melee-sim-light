#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"

uint8_t zelda_action_is_special(uint16_t action_id);
uint8_t zelda_special_try_transform_iasa(MslBatch* batch, size_t idx, uint8_t ground);
void zelda_special_enter_transform(MslBatch* batch, size_t idx, uint8_t ground);
uint8_t zelda_special_try_ground_iasa(MslBatch* batch, size_t idx);
uint8_t zelda_special_try_air_iasa(MslBatch* batch, size_t idx);
void zelda_specials_update_pre_physics_for_fighter(MslBatch* batch, const MslCommonParams* c,
                                                   const MslCharParams* ch, size_t idx,
                                                   uint8_t frame_start_owner);
uint8_t zelda_specials_phys(MslBatch* batch, size_t idx);
uint8_t zelda_special_farore_air_travel_wallceil_end(MslBatch* batch, size_t idx, float nx,
                                                     float ny);
uint8_t zelda_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);
uint8_t zelda_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);
