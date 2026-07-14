#pragma once

#include "batch_internal.h"

// Priority-12 Catch selection and priority-13 fighter contact traversal.
// refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_8006CB94}
void fighter_contact_resolve_catch(MslBatch* batch);
void fighter_contact_resolve_damage(MslBatch* batch);
int fighter_contact_debug_select_body(MslBatch* batch, int batch_index,
                                      MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                      uint16_t* out_count);
