#include "ft/chara/ftCommon/ftCo_ItemThrow.h"
#include "ft/fighter.h"

#include <baselib/controller.h>

// The decomp currently gives this source-global helper external linkage from
// ftCo_Attack100.c while its definition lives in ftCo_ItemThrow.c. Keep the
// exact helper here until that decomp translation-unit seam is repaired.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_ItemThrow.c::ftCo_800952DC
bool ftCo_800952DC(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    if (fp->item_gobj != NULL && fp->input.held_inputs & HSD_PAD_LR) {
        ftCo_800957F4(gobj, ftCo_MS_LightThrowDash);
        return true;
    }
    return false;
}
