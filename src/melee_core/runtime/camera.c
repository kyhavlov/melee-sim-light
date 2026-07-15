#include "cm/camera.h"
#include "cm/types.h"
#include "ft/fighter.h"
#include "ft/ftcamera.h"
#include "ft/ftlib.h"
#include "ft/types.h"
#include "ft/chara/ftCommon/forward.h"
#include "gr/ground.h"
#include "gr/stage.h"
#include "lb/lbvector.h"
#include "lb/types.h"
#include "pl/player.h"
#include "sc/types.h"

#include "match.h"

#include <baselib/controller.h>
#include <baselib/aobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjplink.h>
#include <baselib/gobjproc.h>
#include <baselib/jobj.h>

#include <math.h>

extern void msl_camera_get_render_transform(Vec3* position, Vec3* interest,
                                            float* fov);
static bool msl_vanilla_magnify_offscreen[6];
// camera.static.h documents this DOL data block but intentionally leaves its
// definition commented out. Retain the exact source values for the standard
// gameplay-camera owner.
// refs/melee/src/melee/cm/camera.static.h::cm_803BCCA0
CameraUnkGlobals cm_803BCCA0 = {
    83.0F,  1000.0F, -30.0F, 5.0F,   -7.0F,  17.5F,  -17.5F, 0.0F,
    0.0682F, 60.0F,   120.0F, 0.05F,  0.1F,   120.0F, 900.0F, 0.15F,
    38.0F,   0.1F,    0.1F,   0.001F, 0.1F,   1.0F,   1.0F,   0.6F,
    0.6F,    0.05F,   0.1F,   29.0F,  0.1F,   0.1F,   0.1F,   0.1F,
    0.5F,    0.5F,    0.4F,   -11.0F, 400.0F, 0.2F,   4.0F,   0.05F,
    1.0F,    -7.0F,   7.0F,   0.5F,   0.5F,   0.004F, 0.2F,   0.025F,
    0.2F,    0.003F,  0.2F,   0.025F, 0.2F,   0.02F,  1.0F,   0.14F,
    1200.0F, -0.2F,   1.2F,   0.0F,
};

float Stage_GetCamBoundsLeftOffset(void)
{
    return stage_info.cam_info.cam_bounds.left +
           stage_info.cam_info.cam_x_offset;
}

float Stage_GetCamBoundsRightOffset(void)
{
    return stage_info.cam_info.cam_bounds.right +
           stage_info.cam_info.cam_x_offset;
}

float Stage_GetCamFixedZoom(void)
{
    return stage_info.cam_info.cam_fixed_zoom;
}

float Stage_GetCamZoomRate(void)
{
    return stage_info.cam_info.cam_zoom_rate;
}

float Stage_GetCamInfoX20(void) { return stage_info.cam_info.x20; }
float Stage_GetCamInfoX24(void) { return stage_info.cam_info.x24; }
float Stage_GetCamTrackRatio(void)
{
    return stage_info.cam_info.cam_track_ratio;
}
float Stage_GetCamTrackSmooth(void)
{
    return stage_info.cam_info.cam_track_smooth;
}
float Stage_GetCamMaxDepth(void)
{
    return stage_info.cam_info.cam_max_depth;
}

float Stage_GetCamPanAngleRadians(void)
{
    return 0.0174532923847F * stage_info.cam_info.cam_pan_degrees;
}

float Stage_GetPauseCamZPosInit(void)
{
    return stage_info.cam_info.pausecam_zpos_init;
}

float Stage_GetCamAngleRadiansUp(void)
{
    return 0.0174532923847F * stage_info.cam_info.cam_angle_up;
}

float Stage_GetCamAngleRadiansDown(void)
{
    return 0.0174532923847F * stage_info.cam_info.cam_angle_down;
}

float Stage_GetCamAngleRadiansLeft(void)
{
    return 0.0174532923847F * stage_info.cam_info.cam_angle_left;
}

float Stage_GetCamAngleRadiansRight(void)
{
    return 0.0174532923847F * stage_info.cam_info.cam_angle_right;
}

void Stage_80224CAC(Vec3* out)
{
    Vec3 forward = { 0.0F, 0.0F, -100.0F };
    Vec3 rotation;
    Vec3 result;
    float scale;

    *out = stage_info.cam_info.fixed_cam_pos;
    rotation.x =
        0.0174532923847F * stage_info.cam_info.fixed_cam_vert_angle;
    rotation.y =
        0.0174532923847F * stage_info.cam_info.fixed_cam_horz_angle;
    rotation.z = 0.0F;
    lbVector_ApplyEulerRotation(&forward, &rotation);
    scale = out->z / -forward.z;
    result.x = forward.x * scale + out->x;
    result.y = forward.y * scale + out->y;
    result.z = 0.0F;
    *out = result;
}

void Stage_SetVecToFixedCamPos(Vec3* out)
{
    *out = stage_info.cam_info.fixed_cam_pos;
}

float Stage_GetCamFixedFov(void)
{
    return stage_info.cam_info.fixed_cam_fov;
}

// Standard camera mode does not consume these input helpers, but all mode
// callbacks share one source table and therefore retain their link closure.
u64 gm_GetButtonsPressed(u8 slot)
{
    return slot < 4 ? HSD_PadCopyStatus[slot].button : 0;
}

u64 gm_GetButtonsTriggered(u8 slot)
{
    return slot < 4 ? HSD_PadCopyStatus[slot].trigger : 0;
}

static void msl_grlib_quake_loop(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    HSD_JObjAnimAll(jobj);
    Camera_8002A278(HSD_JObjGetTranslationX(jobj),
                    HSD_JObjGetTranslationY(jobj));
}

static void msl_grlib_quake_once(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    HSD_AObj* aobj = jobj->aobj;

    HSD_JObjAnimAll(jobj);
    Camera_8002A278(HSD_JObjGetTranslationX(jobj),
                    HSD_JObjGetTranslationY(jobj));
    if (aobj == NULL || aobj->flags & 0x40000000) {
        HSD_GObjPLink_80390228(gobj);
    }
}

HSD_GObj* grLib_801C9CEC(s32 kind)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    s32 anim_index;

    // Gameplay-bearing source projection of the quake-model owner. Although
    // the model is invisible headlessly, its animated X/Y translation feeds
    // Camera_8002A278 and therefore the CObj used by Camera_80030BBC.
    // refs/melee/src/melee/gr/grlib.c::grLib_801C9BC8,
    // grLib_801C9C40,grLib_801C9CEC
    if (kind < 1 || kind > 4 || stage_info.quake_model_set == NULL) {
        return NULL;
    }
    anim_index = kind - 1;
    gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 18, (u8) kind);
    jobj = HSD_JObjLoadJoint(stage_info.quake_model_set->joint);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, jobj);
    HSD_GObj_SetupProc(gobj,
                       kind == 1 ? msl_grlib_quake_loop
                                 : msl_grlib_quake_once,
                       1);
    HSD_JObjAddAnimAll(jobj, stage_info.quake_model_set->anims[anim_index],
                       NULL, NULL);
    HSD_JObjReqAnimAll(jobj, 0.0F);
    if (kind == 1) {
        HSD_ForeachAnim(jobj, 6, 0x20, HSD_AObjSetFlags, AOBJ_ARG_AU,
                        AOBJ_LOOP);
    }
    return gobj;
}

// Exact source camera-subject maintenance. Drawing is absent, but these
// callbacks are motion-state owners and feed fp->x221F_b0 and magnify logic.
// refs/melee/src/melee/ft/ftcamera.c
void ftCamera_80076018(UnkFloat6_Camera* in, UnkFloat6_Camera* out, float mul)
{
    out->x0.x = in->x0.x * mul;
    out->x0.y = in->x0.y * mul;
    out->x0.z = in->x0.z * mul;
    out->xC.x = in->xC.x * mul;
    out->xC.y = in->xC.y * mul;
    out->xC.z = in->xC.z * mul;
}

void ftCamera_80076064(Fighter* fp)
{
    CmSubject* camera_box = fp->x890_cameraBox;
    UnkFloat6_Camera scaled;

    ftCamera_80076018(fp->ft_data->x3C, &scaled, fp->x34_scale.y);
    camera_box->x8 = 0;
    if (fp->facing_dir == 1.0F) {
        camera_box->x40.x = scaled.x0.z;
        camera_box->x40.y = scaled.x0.y * Stage_GetCamFixedZoom();
        camera_box->x28 = 1.0F;
    } else {
        camera_box->x40.x = -scaled.x0.y * Stage_GetCamFixedZoom();
        camera_box->x40.y = -scaled.x0.z;
        camera_box->x28 = -1.0F;
    }
    camera_box->x48.x = scaled.xC.x;
    camera_box->x48.y = scaled.xC.y;
    camera_box->x48.z = scaled.xC.z;
    camera_box->x2C = camera_box->x40;
    camera_box->x34 = camera_box->x48;
    camera_box->x10.x = fp->cur_pos.x;
    camera_box->x10.y = fp->cur_pos.y + scaled.x0.x;
    camera_box->x10.z = fp->cur_pos.z;
    camera_box->x1C = camera_box->x10;
}

void ftCamera_UpdateCameraBox(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CmSubject* camera_box = fp->x890_cameraBox;
    UnkFloat6_Camera scaled;

    ftCamera_80076018(fp->ft_data->x3C, &scaled, fp->x34_scale.y);
    if (fp->facing_dir == 1.0F) {
        camera_box->x40.x = scaled.x0.z;
        camera_box->x40.y = scaled.x0.y * Stage_GetCamFixedZoom();
        camera_box->x28 = 1.0F;
    } else {
        camera_box->x40.x = -scaled.x0.y * Stage_GetCamFixedZoom();
        camera_box->x40.y = -scaled.x0.z;
        camera_box->x28 = -1.0F;
    }
    camera_box->x10.x = fp->cur_pos.x;
    camera_box->x10.y = fp->cur_pos.y + scaled.x0.x;
    camera_box->x10.z = fp->cur_pos.z;
    camera_box->xC_b0 = false;
    ftLib_800866DC(gobj, &camera_box->x1C);
}

void ftCamera_800762F4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLib_800866DC(gobj, &fp->x890_cameraBox->x1C);
}

void ftCamera_80076320(HSD_GObj* gobj)
{
    Vec3 center_pos;
    Fighter* fp = GET_FIGHTER(gobj);
    CmSubject* camera_box = fp->x890_cameraBox;
    float blast_height;
    float camera_height;

    ftCamera_UpdateCameraBox(gobj);
    Stage_UnkSetVec3TCam_Offset(&center_pos);
    blast_height = Stage_GetBlastZoneTopOffset() - center_pos.y;
    camera_height = Stage_GetCamBoundsTopOffset() - center_pos.y;
    camera_box->x10.x = (camera_box->x10.x * camera_height) / blast_height;
    camera_box->x10.y = Stage_GetBlastZoneTopOffset();
}

static bool headless_camera_point_on_screen(const Vec3* point)
{
    Mtx view;
    Vec3 eye;
    Vec3 interest;
    Vec3 camera_forward;
    Vec3 camera_up;
    Vec3 look;
    Vec3 right;
    Vec3 up;
    Vec3 projected_point;
    Vec3 eye_point;
    float angle;
    float fov;
    float cotangent;
    float clip_x;
    float clip_y;
    float reciprocal_w;
    float near_test;
    float screen_x;
    float screen_y;
    int pixel_x;
    int pixel_y;

    // Camera_8002AF68 adds the current quake translation when publishing the
    // HSD CObj used by Camera_80030BBC. The raw transform accessors omit it.
    // refs/melee/src/melee/cm/camera.c::Camera_8002AF68
    msl_camera_get_render_transform(&eye, &interest, &fov);

    // The standard CObj descriptor stores roll=0 rather than an explicit up
    // vector. HSD_CObjGetUpVector therefore derives and normalizes the up
    // vector from the current eye vector before C_MTXLookAt consumes it.
    // refs/melee/src/sysdolphin/baselib/cobj.c::{HSD_CObjGetEyeVector,
    // roll2upvec,HSD_CObjGetUpVector}
    camera_forward.x = interest.x - eye.x;
    camera_forward.y = interest.y - eye.y;
    camera_forward.z = interest.z - eye.z;
    PSVECNormalize(&camera_forward, &camera_forward);
    if (1.0F - fabsf(camera_forward.y) < 0.0001F) {
        camera_up.x = sqrtf(camera_forward.y * camera_forward.y +
                            camera_forward.z * camera_forward.z);
        camera_up.y = camera_forward.y *
                      (-camera_forward.x / camera_up.x);
        camera_up.z = camera_forward.z *
                      (-camera_forward.x / camera_up.x);
    } else {
        camera_up.y = sqrtf(camera_forward.x * camera_forward.x +
                            camera_forward.z * camera_forward.z);
        camera_up.x = camera_forward.x *
                      (-camera_forward.y / camera_up.y);
        camera_up.z = camera_forward.z *
                      (-camera_forward.y / camera_up.y);
    }
    PSVECNormalize(&camera_up, &camera_up);

    // Exact release-SDK C_MTXLookAt operation order. VECNormalize and
    // VECCrossProduct resolve to the paired-single implementations in retail.
    // refs/melee/extern/dolphin/src/dolphin/mtx/{mtx.c,vec.c}
    look.x = eye.x - interest.x;
    look.y = eye.y - interest.y;
    look.z = eye.z - interest.z;
    PSVECNormalize(&look, &look);
    PSVECCrossProduct(&camera_up, &look, &right);
    PSVECNormalize(&right, &right);
    PSVECCrossProduct(&look, &right, &up);
    view[0][0] = right.x;
    view[0][1] = right.y;
    view[0][2] = right.z;
    view[0][3] = -((eye.z * right.z) +
                   ((eye.x * right.x) + (eye.y * right.y)));
    view[1][0] = up.x;
    view[1][1] = up.y;
    view[1][2] = up.z;
    view[1][3] =
        -((eye.z * up.z) + ((eye.x * up.x) + (eye.y * up.y)));
    view[2][0] = look.x;
    view[2][1] = look.y;
    view[2][2] = look.z;
    view[2][3] =
        -((eye.z * look.z) + ((eye.x * look.x) + (eye.y * look.y)));

    // Exact lbVector_WorldToScreen near-plane projection followed by the
    // perspective subset of SDK GXProject. cm_803BCB64 uses a 640x480
    // viewport/scissor and aspect 1.2173333.
    // refs/melee/src/melee/lb/lbvector.c::lbVector_WorldToScreen
    // refs/melee/extern/dolphin/src/dolphin/gx/GXTransform.c::GXProject
    projected_point = *point;
    near_test = view[2][0] * point->x + view[2][1] * point->y +
                view[2][2] * point->z + view[2][3];
    if (near_test > -0.01F) {
        near_test = -near_test - 0.01F;
        projected_point.x += view[2][0] * near_test;
        projected_point.y += view[2][1] * near_test;
        projected_point.z += view[2][2] * near_test;
    }
    eye_point.x =
        view[0][3] +
        ((view[0][2] * projected_point.z) +
         ((view[0][0] * projected_point.x) +
          (view[0][1] * projected_point.y)));
    eye_point.y =
        view[1][3] +
        ((view[1][2] * projected_point.z) +
         ((view[1][0] * projected_point.x) +
          (view[1][1] * projected_point.y)));
    eye_point.z =
        view[2][3] +
        ((view[2][2] * projected_point.z) +
         ((view[2][0] * projected_point.x) +
          (view[2][1] * projected_point.y)));
    angle = 0.5F * fov;
    angle = angle * 0.017453293F;
    cotangent = 1.0F / tanf(angle);
    clip_x = eye_point.x * (cotangent / 1.2173333F);
    clip_y = eye_point.y * cotangent;
    reciprocal_w = 1.0F / -eye_point.z;
    screen_x = 320.0F + (reciprocal_w * (clip_x * 320.0F));
    screen_y = 240.0F + (reciprocal_w * (-clip_y * 240.0F));
    pixel_x = (int) screen_x;
    pixel_y = (int) screen_y;
    return pixel_x >= 0 && pixel_x < 640 && pixel_y >= 0 && pixel_y < 480;
}

void msl_camera_publish_fighter_visibility(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CmSubject* subject = fp->x890_cameraBox;
    Vec3* point;

    // The magnifier camera is GX link 0 while fighter drawing is GX link 5.
    // Its vanilla render callback therefore consumes the visibility bit left
    // by the preceding fighter draw before this pass publishes a new one.
    // refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC618
    // refs/melee/src/melee/ft/fighter.c::Fighter_80068E64
    if (fp->player_id >= 0 && fp->player_id < 6) {
        msl_vanilla_magnify_offscreen[fp->player_id] =
            fp->x221F_b0 && ftLib_80086ED0(gobj);
    }

    // This is the gameplay-visible side effect at the head of
    // ftDrawCommon_80080E18, with Camera_80030CD8's viewport test projected
    // onto the source stage camera bounds. The HSD camera, scissor, and all
    // draw calls remain presentation-only and are intentionally absent.
    // refs/melee/src/melee/ft/ftdrawcommon.c::ftDrawCommon_80080E18
    // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    // refs/melee/src/melee/cm/camera.c::{Camera_80030BBC,Camera_80030CD8}
    if (subject == NULL || fp->x2229_b3 || fp->x2220_b7) {
        fp->x221F_b0 = false;
    } else {
        point = &subject->x1C;
        if (headless_camera_point_on_screen(point)) {
            fp->x221F_b0 = false;
        } else {
            fp->x221F_b0 = true;
        }
    }

}

bool ifMagnify_802FC998(s32 slot)
{
    HSD_GObj* gobj = Player_GetEntity(slot);
    Fighter* fp;

    // Slippi netplay replaces Fighter_8006A360's call at 0x8006A880 with
    // BrawlOffscreenDamage. It deliberately removes the render-owned vanilla
    // magnifier dependency and tests the fighter root against the stage camera
    // bounds, while excluding dead/star-KO states. Offline versus retains the
    // prior-render magnifier publication modeled above.
    // refs/slippi-ssbm-asm/Online/Core/BrawlOffscreenDamage.asm
    // refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,
    //     ifMagnify_802FC998}
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    if (!msl_core_has_brawl_offscreen_damage()) {
        return slot >= 0 && slot < 6 &&
               msl_vanilla_magnify_offscreen[slot];
    }
    if (gobj == NULL) {
        return false;
    }
    fp = GET_FIGHTER(gobj);
    if (fp->x221F_b1 || fp->motion_id == ftCo_MS_DeadUpStar ||
        fp->motion_id == ftCo_MS_DeadUpFall)
    {
        return false;
    }
    return fp->cur_pos.x < Stage_GetCamBoundsLeftOffset() ||
           fp->cur_pos.x > Stage_GetCamBoundsRightOffset() ||
           fp->cur_pos.y > Stage_GetCamBoundsTopOffset() ||
           fp->cur_pos.y < Stage_GetCamBoundsBottomOffset();
}
