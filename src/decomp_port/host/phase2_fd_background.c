#include "host/phase2_fd_background.h"

#include "gr/types.h"

#include <MetroTRK/intrinsics.h>

#include <baselib/gobj.h>
#include <baselib/gobjproc.h>
#include <baselib/random.h>

static float source_abs(float value)
{
    return value < 0.0F ? -value : value;
}

static void fd_background_rotation_think(HSD_GObj* gobj)
{
    // Gameplay-bearing headless projection of
    // refs/melee/src/melee/gr/grlast.c::grLast_8021ADD0 and retail DOL
    // 0x8021ADD0..0x8021B054. The omitted tail only publishes rotations to
    // renderer-owned JObjs/generators. Both angular state machines remain
    // because their boundary crossings consume the shared HSD RNG stream.
    static const float damping = 0.99F;
    static const float max_speed = 0.0008726646F;
    static const float x_limit = 0.2617994F;
    static const float y_limit = 0.17453292F;
    static const float accel_range = 0.00006981317F;
    static const float accel_base = 0.000017453292F;
    Ground* gp = gobj->user_data;

    gp->u.last.xCC += gp->u.last.xD4;
    gp->u.last.xCC *= damping;
    if (gp->u.last.xCC > max_speed) {
        gp->u.last.xCC = max_speed;
    } else if (gp->u.last.xCC < -max_speed) {
        gp->u.last.xCC = -max_speed;
    }

    gp->u.last.xD0 += gp->u.last.xD8;
    gp->u.last.xD0 *= damping;
    if (gp->u.last.xD0 > max_speed) {
        gp->u.last.xD0 = max_speed;
    } else if (gp->u.last.xD0 < -max_speed) {
        gp->u.last.xD0 = -max_speed;
    }

    gp->u.last.xC4 += gp->u.last.xCC;
    if (gp->u.last.xC4 > x_limit) {
        float randf;
        gp->u.last.xC4 = x_limit;
        gp->u.last.xCC = -source_abs(gp->u.last.xCC);
        randf = HSD_Randf();
        gp->u.last.xD4 =
            __fmsubs(accel_range, -randf, accel_base);
    } else if (gp->u.last.xC4 < -x_limit) {
        gp->u.last.xC4 = -x_limit;
        gp->u.last.xCC = source_abs(gp->u.last.xCC);
        gp->u.last.xD4 =
            __fmadds(accel_range, HSD_Randf(), accel_base);
    }

    gp->u.last.xC8 += gp->u.last.xD0;
    if (gp->u.last.xC8 > y_limit) {
        float randf;
        gp->u.last.xC8 = y_limit;
        gp->u.last.xD0 = -source_abs(gp->u.last.xD0);
        randf = HSD_Randf();
        gp->u.last.xD8 =
            __fmsubs(accel_range, -randf, accel_base);
    } else if (gp->u.last.xC8 < -y_limit) {
        gp->u.last.xC8 = -y_limit;
        gp->u.last.xD0 = source_abs(gp->u.last.xD0);
        gp->u.last.xD8 =
            __fmadds(accel_range, HSD_Randf(), accel_base);
    }
}

void msl_fd_background_init(HSD_GObj* gobj)
{
    // Exact gameplay-visible portion of
    // refs/melee/src/melee/gr/grlast.c::grLast_8021AC30 and retail DOL
    // 0x8021AC30..0x8021ACE4. Material/JObj setup is presentation-only.
    static const float accel_range = 0.00006981317F;
    static const float accel_base = 0.000017453292F;
    Ground* gp = gobj->user_data;

    gp->u.last.xC4 = 0.0F;
    gp->u.last.xC8 = 0.0F;
    gp->u.last.xCC = 0.0F;
    gp->u.last.xD0 = 0.0F;
    gp->u.last.xD4 = __fmadds(accel_range, HSD_Randf(), accel_base);
    gp->u.last.xD4 *= HSD_Randi(2) ? +1.0F : -1.0F;
    gp->u.last.xD8 = __fmadds(accel_range, HSD_Randf(), accel_base);
    gp->u.last.xD8 *= HSD_Randi(2) ? +1.0F : -1.0F;
    gp->u.last.xDC = 0.0F;
    gp->u.last.xE0 = NULL;

    // grlast.c::grLast_8021A7F4 registers callback2 at source priority 4.
    HSD_GObj_SetupProc(gobj, fd_background_rotation_think, 4);
}
