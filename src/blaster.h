#pragma once

#include "batch_internal.h"

// Fox/Falco neutral special (Blaster) minimal motion-state handler.
// Decomp reference: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
void blaster_update_pre_physics(MslBatch* batch);

// Grounded Wait_IASA B-special subset for Fox/Falco (Side/Up/Neutral only).
//
// Decomp ordering:
// - Wait_IASA checks SpecialS -> SpecialHi -> SpecialN -> SpecialLw before grounded attacks.
// - Grounded Attack* IASA callbacks commonly delegate into Wait_IASA when allow_interrupt is set.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
// refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
uint8_t blaster_try_enter_ground_from_iasa(MslBatch* batch, size_t idx);
