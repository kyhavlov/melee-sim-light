#include "ft/chara/ftCommon/ftCo_0A01.h"

#include "ft/types.h"
#include "lb/lbcollision.h"
#include "mp/mplib.h"

#include <baselib/random.h>

// Fox/FD projection of refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c.
// The upstream translation unit also owns the complete CPU command/targeting
// tables, which retain every character and stage.  These functions are the
// source gameplay/input initialization slice reached by human Fox fighters.

void ftCo_800A0DA4(Fighter* fp)
{
    int i;
    FighterHurtCapsule* hurt;
    struct Fighter_x1A88_t* data = &fp->x1A88;
    float left = 0.0F;
    float right = 0.0F;
    float top = 0.0F;

    for (i = 0; i < fp->hurt_capsules_len; i++) {
        float dx;
        float dy;
        float scale;

        hurt = &fp->hurt_capsules[i];
        lbColl_800083C4(&hurt->capsule);
        dx = hurt->capsule.a_pos.x - fp->cur_pos.x;
        dy = hurt->capsule.a_pos.y - fp->cur_pos.y;
        scale = hurt->capsule.scale * fp->x34_scale.y;
        if (left > dx - scale) {
            left = dx - scale;
        }
        if (right < dx + scale) {
            right = dx + scale;
        }
        if (top < dy + scale) {
            top = dy + scale;
        }

        dx = hurt->capsule.b_pos.x - fp->cur_pos.x;
        dy = hurt->capsule.b_pos.y - fp->cur_pos.y;
        if (left > dx - scale) {
            left = dx - scale;
        }
        if (right < dx + scale) {
            right = dx + scale;
        }
        if (top < dy + scale) {
            top = dy + scale;
        }
    }

    if (fp->facing_dir > 0.0F) {
        data->x55C = right;
        data->x560 = -left;
    } else {
        data->x55C = -left;
        data->x560 = right;
    }
    data->x564 = 0.5F * (data->x55C + data->x560);
    data->x568 = top;
}

bool ftCo_800A0FB0(Vec3* vec_out, int* line_id_out, u32* flags_out,
                   Vec3* normal_out, int arg4, int arg5, int arg6, float arg7,
                   float arg8, float arg9, float arg10, float arg11)
{
    // The four source exclusions after mpCheckFloor are moving/special stage
    // line owners. Final Destination has none of those line classes.
    *line_id_out = -1;
    return mpCheckFloor(arg7, arg8, arg9, arg10, arg11, vec_out, line_id_out,
                        flags_out, normal_out, arg4, arg5, arg6, NULL, NULL);
}

void ftCo_800A101C(Fighter* fp, int cpu_type, int cpu_level, int arg3)
{
    Vec3 floor_pos;
    Vec3 floor_normal;
    int floor_line;
    u32 floor_flags;
    struct Fighter_x1A88_t* data = &fp->x1A88;
    int i;

    data->xC = cpu_type;
    data->x3C = 40.0F;
    data->level = cpu_level;
    data->x14 = arg3;
    switch (data->xC) {
    case 1:
    case 25:
        data->x18 = 0xC;
        data->x1C = 0xC;
        data->x20 = 0;
        break;
    case 3:
        data->x18 = 1;
        data->x1C = 1;
        data->x20 = 0xB;
        break;
    case 15:
        data->x18 = 0;
        data->x1C = 0;
        data->x20 = 0;
        break;
    default:
        data->x18 = 1;
        data->x1C = 1;
        data->x20 = 0xA;
        break;
    }
    data->x24 = 0x12C;
    data->x28 = 0;
    data->x2C = 5;
    data->x30 = 0;
    data->xF9_b0 = false;
    data->x38 = 5.0F;
    data->x40 = 50.0F;
    data->x44 = NULL;
    data->x48 = 0;
    data->x50 = 0;
    data->x98 = fp->cur_pos;
    if (ftCo_800A0FB0(&floor_pos, &floor_line, &floor_flags, &floor_normal,
                      -1, -1, -1, fp->cur_pos.x, 10.0F + fp->cur_pos.y,
                      fp->cur_pos.x, fp->cur_pos.y - 1000.0F, 0.0F))
    {
        data->x64.x = data->x54.x = floor_pos.x;
        data->x64.y = data->x54.y = floor_pos.y;
    } else {
        data->x64.x = data->x54.x = fp->cur_pos.x;
        data->x64.y = data->x54.y = fp->cur_pos.y;
    }
    data->x60 = 0;
    data->x74.y = 0.0F;
    data->x74.x = 0.0F;
    data->x6C.y = 0.0F;
    data->x6C.x = 0.0F;
    data->xF8_b0 = false;
    data->xF8_b12 = 0;
    data->x7C = data->x80 = 10.0F * HSD_Randf();
    data->x84 = 0;
    data->x90 = 0;
    data->x88 = 0;
    data->x8C = 0;
    data->csP = NULL;
    data->command_duration = 0;
    data->write_pos = data->buffer;
    data->xA4 = 0;
    data->xC8 = 0;
    data->xEC = 0;
    data->xF8_b5 = false;
    data->xF8_b6 = false;
    data->xF8_b7 = false;
    data->xF9_b1 = false;
    data->x0 = 0;
    data->lstickX = 0;
    data->lstickY = 0;
    data->cstickX = 0;
    data->cstickY = 0;
    data->rtrigger = 0;
    data->ltrigger = 0;
    data->xF9_b2 = false;
    data->xF9_b3 = false;
    data->xF9_b4 = false;
    data->xF9_b6 = false;
    data->xF9_b5 = false;
    data->xF9_b7 = false;
    data->xFA_b7 = false;
    data->xFB_b0 = false;
    data->x94 = 0;
    data->xFA_b1 = false;
    data->xFA_b2 = false;
    data->xFA_b34 = 1;
    data->xFA_b5 = false;
    data->xFA_b6 = false;
    data->x55C = 1.0F;
    data->x560 = 1.0F;
    data->x564 = 0.5F * (data->x55C + data->x560);
    data->x568 = 2.0F;
    data->x444 = &data->xFC[5];
    data->x448 = data->xFC;
    for (i = 0; i < 30; i++) {
        data->xFC[i].x0 = 0;
        data->xFC[i].lstickX = 0;
        data->xFC[i].lstickY = 0;
        data->xFC[i].x4 = 0;
        data->xFC[i].x5 = 0;
        data->xFC[i].cur_pos.x = 0.0F;
        data->xFC[i].cur_pos.y = 0.0F;
        data->xFC[i].cur_pos.z = 0.0F;
        data->xFC[i].facing_dir = 1.0F;
    }
    if (fp->co_attrs.grav < 0.00001F && fp->co_attrs.grav > -0.00001F) {
        data->x558 = 10.0F;
    } else {
        float jump_v = fp->co_attrs.jump_v_initial_velocity *
                       fp->co_attrs.air_jump_v_multiplier;
        data->x558 = jump_v * jump_v / (2.0F * fp->co_attrs.grav);
    }
    data->half_width = 10.0F;
    data->half_height = 10.0F;
    data->x570 = 1.0F;
    data->x34 = (10 - data->level) * (HSD_Randf() * 15.0F + 15.0F) + 10.0F;
    if (data->xC == 7) {
        data->x34 /= 2.0F;
    }
    data->x56C = 3.5F;
}

float ftCo_800A17E4(Fighter* fp)
{
    float result = fp->x1A88.lstickX > 0
                       ? fp->x1A88.lstickX / 127.0F
                       : fp->x1A88.lstickX / 128.0F;
    return result > 1.0F ? 1.0F : result < -1.0F ? -1.0F : result;
}

static float normalized_axis(s8 value)
{
    float result = value > 0 ? value / 127.0F : value / 128.0F;
    return result > 1.0F ? 1.0F : result < -1.0F ? -1.0F : result;
}

float ftCo_800A1874(Fighter* fp) { return normalized_axis(fp->x1A88.lstickY); }
float ftCo_800A1904(Fighter* fp)
{
    float result = fp->x1A88.ltrigger / 255.0F;
    return result > 1.0F ? 1.0F : result;
}
float ftCo_800A1948(Fighter* fp)
{
    float result = fp->x1A88.rtrigger / 255.0F;
    return result > 1.0F ? 1.0F : result;
}
HSD_Pad ftCo_800A198C(Fighter* fp) { return fp->x1A88.x0; }
float ftCo_800A1994(Fighter* fp) { return normalized_axis(fp->x1A88.cstickX); }
float ftCo_800A1A24(Fighter* fp) { return normalized_axis(fp->x1A88.cstickY); }
