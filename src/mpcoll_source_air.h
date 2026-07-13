#pragma once

#include "batch_internal.h"

// Dispatch the installed airborne mpColl wrapper for every fighter not handled by the direct
// grounded callback path. The callback-local CollData loop owns floor contact and ECB lifetime;
// wall/ceiling resolution is folded into this owner in the following cutover slice.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80046904}
void mpcoll_source_air_apply(MslBatch* batch);

// Run one explicitly installed airborne Coll callback outside the ordinary Fighter_procMap slot.
// Capture connect uses this because fn_800DAADC changes the victim MotionState at grab-processing
// priority and immediately invokes the new victim coll_cb in the same source callback.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAADC
uint8_t mpcoll_source_air_run_installed_callback(MslBatch* batch, int bi, int p);

// Run DDDE4's release-local mpColl_800471F8 geometry packet. This consumes the canonical air
// kernel but deliberately omits the released fighter's installed MotionState continuation; throw
// damage is entered only after DDDE4 returns.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
uint8_t mpcoll_source_air_run_release_471f8(MslBatch* batch, int bi, int p, float last_x,
                                            float last_y);

// Run DC920's Clear-ECB fallback wrapper: grounded uses 48654 (flags-5 ECB), airborne uses 477E0
// (stay-airborne). Return value is mpColl's touched-floor result, not its public FloorMask.
uint8_t mpcoll_source_air_run_dc920_fallback(MslBatch* batch, int bi, int p, float last_x,
                                             float last_y, uint8_t grounded);
