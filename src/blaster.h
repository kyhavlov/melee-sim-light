#pragma once

#include "batch_internal.h"

struct MslCommonParams;

// Fox/Falco neutral special (Blaster) minimal motion-state handler.
// Decomp reference: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
void blaster_update_anim_callbacks_pre_input(MslBatch* batch);
void blaster_update_pre_physics(MslBatch* batch);

// Grounded Wait_IASA B-special subset (Side/Up/Neutral), intentionally excluding reflector.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
uint8_t blaster_try_enter_ground_from_wait_iasa(MslBatch* batch, const struct MslCommonParams* c,
                                                size_t idx);

// Grounded Wait_IASA B-special subset without the outer Wait/Squat action gate.
// Used by grounded-attack IASA delegates that route through ftCo_Wait_IASA after their own
// allow_interrupt gate.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
uint8_t blaster_try_enter_ground_from_iasa_subset(MslBatch* batch, const struct MslCommonParams* c,
                                                  size_t idx);

// Aerial B-special subset without the ordinary Fall/Jump/Damage action gate.
// Used by RebirthWait_IASA, which calls ftCo_SpecialAir_CheckInput directly before its Fall-enter
// fallback.
// refs/melee/src/melee/ft/ft_0D4D.c::ftCo_RebirthWait_IASA
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
uint8_t blaster_try_enter_air_from_iasa_subset(MslBatch* batch, const struct MslCommonParams* c,
                                               size_t idx);
