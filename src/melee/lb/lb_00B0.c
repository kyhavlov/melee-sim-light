#include "lb/lb_00B0.h"

#include <placeholder.h>

#include "sc/types.h" // IWYU pragma: keep

#include <MetroTRK/intrinsics.h>
#include <math.h>
#include <dolphin/mtx.h>
#include <baselib/aobj.h>
#include <baselib/dobj.h> // IWYU pragma: keep
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/pobj.h>
#include <baselib/quatlib.h>
#include <baselib/robj.h>
#ifdef MSL_CORE_NATIVE
#include <MSL/trigf.h>
#include "platform/native_dat.h"
#include "runtime/fighter_pose.h"
#endif

/* 00B9D8 */ static void lb_8000B9D8(HSD_JObj*, float**, s32);
/* 00BC04 */ static HSD_JObj* lbFindJObjWithAObj(HSD_JObj*);
/* 00BECC */ static HSD_AnimJoint* lb_8000BECC(HSD_AnimJoint* animjoint);
/* 00CB60 */ static s32 lbGetFreeColorRegImpl(s32 i0, HSD_TevDesc*, HSD_TExp*,
                                              HSD_TExp*);
/* 00CCBC */ static s32 lbGetFreeAlphaRegImpl(s32 i0, HSD_TevDesc* cur,
                                              HSD_TExp*, HSD_TExp*);

static s32 lb_803B9FC0[] = { 14, 14, 14, 14, 2, 4, 6, 0 };
static s32 lb_803B9FE0[] = { 12, 13, 14, 15 };
static s32 lb_803B9FF0[] = { 7, 4, 5, 6 };
static s32 lb_803BA000[] = { 6, 6, 6, 6, 1, 2, 3, 0 };
static s32 lb_803BA020[] = { 28, 29, 30, 31 };
static s32 lb_803BA030[] = { 7, 4, 5, 6 };

bool lb_8000B074(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_is_animating(jobj);
    }
#endif
    HSD_AObj* aobj = jobj->aobj;

    if (aobj != NULL && !(aobj->flags & AOBJ_NO_ANIM)) {
        return true;
    }

    return false;
}

bool lb_8000B09C(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_tree_is_animating(jobj);
    }
#endif
    while (jobj != NULL) {
        if (jobj->aobj != NULL && !(jobj->aobj->flags & AOBJ_NO_ANIM)) {
            return true;
        }
        if ((jobj->child == NULL && jobj->next == NULL) ||
            (jobj->flags & JOBJ_INSTANCE))
        {
            while (true) {
                if (jobj->parent == NULL) {
                    jobj = NULL;
                    break;
                }
                if (jobj->parent->next != NULL) {
                    jobj = jobj->parent->next;
                    break;
                }
                jobj = jobj->parent;
            }
        } else if (jobj->child != NULL) {
            jobj = jobj->child;
        } else {
            jobj = jobj->next;
        }
    }
    return false;
}

bool lb_8000B134(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_tree_rewound(jobj);
    }
#endif
    while (jobj != NULL) {
        if (jobj->aobj != NULL && (jobj->aobj->flags & AOBJ_REWINDED)) {
            return true;
        }
        if ((jobj->child == NULL && jobj->next == NULL) ||
            (jobj->flags & JOBJ_INSTANCE))
        {
            while (true) {
                if (jobj->parent == NULL) {
                    jobj = NULL;
                    break;
                }
                if (jobj->parent->next != NULL) {
                    jobj = jobj->parent->next;
                    break;
                }
                jobj = jobj->parent;
            }
        } else if (jobj->child != NULL) {
            jobj = jobj->child;
        } else {
            jobj = jobj->next;
        }
    }
    return false;
}

inline HSD_JObj* jobj_parent(HSD_JObj* jobj)
{
    if (jobj == NULL) {
        return NULL;
    }
    return jobj->parent;
}

void lb_8000B1CC(HSD_JObj* arg0, Vec3* pos0, Vec3* pos1)
{
    Quaternion r;
    Vec3 s;

    if (arg0 == NULL) {
        *pos1 = *pos0;
        return;
    }
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_transform_point(arg0, pos0, pos1)) {
        return;
    }
#endif
    if (jobj_parent(arg0) != NULL) {
        HSD_JObjSetupMatrix(arg0);
        if (pos0 == NULL || (!pos0->x && !pos0->y && !pos0->z)) {
            pos1->x = arg0->mtx[0][3];
            pos1->y = arg0->mtx[1][3];
            pos1->z = arg0->mtx[2][3];
        } else {
            MTXMultVec(arg0->mtx, pos0, pos1);
        }
        return;
    }
    if (pos0 == NULL || (!pos0->x && !pos0->y && !pos0->z)) {
        HSD_JObjGetTranslation(arg0, pos1);
        return;
    }
    HSD_JObjGetRotation(arg0, &r);
    HSD_JObjGetScale(arg0, &s);
    if (!r.x && !r.y && !r.z && s.x == 1 && s.y == 1 && s.z == 1) {
        HSD_JObjGetTranslation(arg0, pos1);
        pos1->x += pos0->x;
        pos1->y += pos0->y;
        pos1->z += pos0->z;
        return;
    }
    HSD_JObjSetupMatrix(arg0);
    MTXMultVec(arg0->mtx, pos0, pos1);
}

void lb_8000B4FC(HSD_JObj* jobj, HSD_Joint* joint)
{
    if (jobj == NULL || joint == NULL) {
        return;
    }
    jobj->rotate.x = joint->rotation.x;
    jobj->rotate.y = joint->rotation.y;
    jobj->rotate.z = joint->rotation.z;
    jobj->scale = joint->scale;
    jobj->translate = joint->position;
    HSD_JObjClearFlags(jobj, JOBJ_USE_QUATERNION);
    if (!(jobj->flags & JOBJ_MTX_INDEP_SRT)) {
        HSD_JObjSetMtxDirty(jobj);
    }
}

void lb_8000B5DC(HSD_JObj* jobj, HSD_Joint* joint)
{
    if (jobj == NULL || joint == NULL) {
        return;
    }
    jobj->rotate.x = joint->rotation.x;
    jobj->rotate.y = joint->rotation.y;
    jobj->rotate.z = joint->rotation.z;
    jobj->translate = joint->position;
    HSD_JObjClearFlags(jobj, JOBJ_USE_QUATERNION);
    if (!(jobj->flags & JOBJ_MTX_INDEP_SRT)) {
        HSD_JObjSetMtxDirty(jobj);
    }
}

void lb_8000B6A4(HSD_JObj* jobj, HSD_Joint* joint)
{
    if (jobj == NULL || joint == NULL) {
        return;
    }
    jobj->scale = joint->scale;
    jobj->translate = joint->position;
    if (!(jobj->flags & JOBJ_MTX_INDEP_SRT)) {
        HSD_JObjSetMtxDirty(jobj);
    }
}

void lb_8000B760(HSD_JObj* jobj, HSD_Joint* joint)
{
    if (jobj == NULL || joint == NULL) {
        return;
    }
    jobj->translate = joint->position;
    if (!(jobj->flags & JOBJ_MTX_INDEP_SRT)) {
        HSD_JObjSetMtxDirty(jobj);
    }
}

void lb_8000B804(HSD_JObj* jobj, HSD_Joint* joint)
{
    if (jobj == NULL || joint == NULL) {
        return;
    }
    jobj->rotate.x = joint->rotation.x;
    jobj->rotate.y = joint->rotation.y;
    jobj->rotate.z = joint->rotation.z;
    jobj->scale = joint->scale;
    jobj->translate = joint->position;
    HSD_JObjClearFlags(jobj, JOBJ_USE_QUATERNION);
    HSD_JObjSetFlags(jobj, JOBJ_MTX_DIRTY);

    lb_8000B804(jobj->next, joint->next);
    lb_8000B804(jobj->child, joint->child);
}

static void lb_8000B9D8(HSD_JObj* jobj, float** arg1, s32 arg2)
{
    float* tmp = *arg1;
    if (arg2 != 0) {
        HSD_AObjSetRate(jobj->aobj, *tmp);
    }
}

void lb_8000BA0C(HSD_JObj* jobj, float arg8)
{
    float* sp10 = &arg8;
    HSD_JObjWalkTree(jobj, lb_8000B9D8, &sp10);
}

void lbDObjSetRateAll(HSD_DObj* dobj, float val)
{
    if (dobj == NULL) {
        return;
    }
    if (dobj->pobj != NULL) {
        HSD_PObj* cur;
        for (cur = dobj->pobj; cur != NULL; cur = cur->next) {
            if (pobj_type(cur) == POBJ_SHAPEANIM &&
                cur->u.shape_set->aobj != NULL)
            {
                HSD_AObjSetRate(cur->u.shape_set->aobj, val);
            }
        }
    }
    if (dobj->mobj != NULL) {
        if (dobj->mobj->aobj != NULL) {
            HSD_AObjSetRate(dobj->mobj->aobj, val);
        }
        if (dobj->mobj->tobj != NULL) {
            HSD_TObj* cur;
            for (cur = dobj->mobj->tobj; cur != NULL; cur = cur->next) {
                if (cur->aobj != NULL) {
                    HSD_AObjSetRate(cur->aobj, val);
                }
            }
        }
    }
}

/// (exact copy of above, but with HSD_AObjReqAnim)
void lbDObjReqAnimAll(HSD_DObj* dobj, float val)
{
    if (dobj == NULL) {
        return;
    }
    if (dobj->pobj != NULL) {
        HSD_PObj* cur;
        for (cur = dobj->pobj; cur != NULL; cur = cur->next) {
            if (pobj_type(cur) == POBJ_SHAPEANIM &&
                cur->u.shape_set->aobj != NULL)
            {
                HSD_AObjReqAnim(cur->u.shape_set->aobj, val);
            }
        }
    }
    if (dobj->mobj != NULL) {
        if (dobj->mobj->aobj != NULL) {
            HSD_AObjReqAnim(dobj->mobj->aobj, val);
        }
        if (dobj->mobj->tobj != NULL) {
            HSD_TObj* cur;
            for (cur = dobj->mobj->tobj; cur != NULL; cur = cur->next) {
                if (cur->aobj != NULL) {
                    HSD_AObjReqAnim(cur->aobj, val);
                }
            }
        }
    }
}

static HSD_JObj* lbFindJObjWithAObj(HSD_JObj* jobj)
{
    HSD_JObj* tmp;
    if (jobj == NULL) {
        return NULL;
    }
    if (jobj->aobj != NULL) {
        return jobj;
    }
    tmp = lbFindJObjWithAObj(jobj->child);
    if (tmp != NULL) {
        return tmp;
    }
    tmp = lbFindJObjWithAObj(jobj->next);
    if (tmp != NULL) {
        return tmp;
    }
    return NULL;
}

float lbGetJObjFramerate(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_tree_rate(jobj);
    }
#endif
    jobj = lbFindJObjWithAObj(jobj);
    if (jobj != NULL) {
        return jobj->aobj->framerate;
    }
    return 0;
}

float lbGetJObjCurrFrame(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_tree_frame(jobj);
    }
#endif
    jobj = lbFindJObjWithAObj(jobj);
    if (jobj != NULL) {
        return jobj->aobj->curr_frame;
    }
    return 0;
}

float lbGetJObjEndFrame(HSD_JObj* jobj)
{
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_owns_joint(jobj)) {
        return msl_fighter_pose_tree_end(jobj);
    }
#endif
    jobj = lbFindJObjWithAObj(jobj);
    if (jobj != NULL) {
        return jobj->aobj->end_frame;
    }
    return 0;
}

static HSD_AnimJoint* lb_8000BECC(HSD_AnimJoint* animjoint)
{
    HSD_AnimJoint* tmp;
    if (animjoint == NULL) {
        return NULL;
    }
    if (animjoint->aobjdesc != NULL) {
        return animjoint;
    }
    tmp = lb_8000BECC(animjoint->child);
    if (tmp != NULL) {
        return tmp;
    }
    tmp = lb_8000BECC(animjoint->next);
    if (tmp != NULL) {
        return tmp;
    }
    return NULL;
}

float lb_8000BFF0(HSD_AnimJoint* animjoint)
{
    animjoint = lb_8000BECC(animjoint);
    if (animjoint != NULL) {
        return animjoint->aobjdesc->end_frame;
    }
    return 0;
}

void lb_8000C07C(HSD_JObj* jobj, s32 i, HSD_AnimJoint** arg3,
                 HSD_MatAnimJoint** arg4, HSD_ShapeAnimJoint** arg5)
{
    HSD_AnimJoint* phi_r4;
    HSD_MatAnimJoint* phi_r5;
    HSD_ShapeAnimJoint* phi_r6;

    if (arg3 != NULL) {
        phi_r4 = arg3[i];
    } else {
        phi_r4 = NULL;
    }
    if (arg4 != NULL) {
        phi_r5 = arg4[i];
    } else {
        phi_r5 = NULL;
    }
    if (arg5 != NULL) {
        phi_r6 = arg5[i];
    } else {
        phi_r6 = NULL;
    }
    HSD_JObjAddAnimAll(jobj, phi_r4, phi_r5, phi_r6);
}

void lb_8000C0E8(HSD_JObj* jobj, s32 i, DynamicModelDesc* arg2)
{
    lb_8000C07C(jobj, i, arg2->anims, arg2->matanims, arg2->shapeanims);
}

void memzero(void* mem, ssize_t size)
{
    u8* bytes = mem;
    while (size--) {
        *bytes++ = 0;
    }
}

void lb_8000C1C0(HSD_JObj* jobj, HSD_JObj* constraint)
{
    HSD_RObj* robj = HSD_RObjAlloc();
    HSD_RObjSetFlags(robj, 0x90000001);
    HSD_RObjSetConstraintObj(robj, constraint);
    HSD_JObjPrependRObj(jobj, robj);
}

void lb_8000C228(HSD_JObj* jobj, HSD_JObj* constraint)
{
    HSD_RObj* robj = HSD_RObjAlloc();
    HSD_RObjSetFlags(robj, 0x90000002);
    HSD_RObjSetConstraintObj(robj, constraint);
    HSD_JObjPrependRObj(jobj, robj);
}

void lb_8000C290(HSD_JObj* jobj, HSD_JObj* constraint)
{
    HSD_RObj* robj = HSD_RObjAlloc();
    HSD_RObjSetFlags(robj, 0x90000004);
    HSD_RObjSetConstraintObj(robj, constraint);
    HSD_JObjPrependRObj(jobj, robj);
}

void lb_8000C2F8(HSD_JObj* jobj, HSD_JObj* constraint)
{
    lb_8000C1C0(jobj, constraint);
    lb_8000C290(jobj, constraint);
}

inline HSD_RObj* robj_next(HSD_RObj* robj)
{
    if (robj != NULL) {
        return robj->next;
    }
    return NULL;
}

void lb_8000C390(HSD_JObj* jobj)
{
    HSD_RObj* next;
    HSD_RObj* cur = HSD_JObjGetRObj(jobj);

    while (true) {
        cur = HSD_RObjGetByType(cur, REFTYPE_JOBJ, 0);

        if (cur == NULL) {
            break;
        }

        next = robj_next(cur);
        HSD_JObjDeleteRObj(jobj, cur);
        HSD_RObjRemove(cur);
        cur = next;
    }
}

void lb_8000C420(HSD_JObj* jobj, u32 flags, float limit)
{
    HSD_RObj* robj = HSD_RObjAlloc();
    HSD_RObjSetFlags(robj, flags | 0xA0000000);
    if (robj != NULL) {
        robj->u.limit = limit;
    }
    HSD_JObjPrependRObj(jobj, robj);
}

void lb_8000C490(HSD_JObj* jobj1, HSD_JObj* jobj2, HSD_JObj* arg2, float arg8,
                 float arg9)
{
    float dx;
    float dy;
    float dz;
    float dw;
    s32 is_quat_1;
    s32 is_quat_2;

    Vec3 sp60;
    Vec3 sp54;
    Quaternion quat1;
    Quaternion quat2;

    float sx;
    float sy;
    float sz;
    float sw;

    float sum_square_diffs;
    float sum_square_sums;

    arg2->translate.x =
        __fmadds(jobj1->translate.x, arg8, jobj2->translate.x * arg9);
    arg2->translate.y =
        __fmadds(jobj1->translate.y, arg8, jobj2->translate.y * arg9);
    arg2->translate.z =
        __fmadds(jobj1->translate.z, arg8, jobj2->translate.z * arg9);
    // GALE01 0x8000C4B4--0x8000C528 evaluates each blend as a rounded
    // second-source multiply followed by one scalar-single fmadds. Guard-pose
    // blending feeds shield collision, so retain those hosted boundaries.
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000C490
    arg2->scale.x =
        __fmadds(jobj1->scale.x, arg8, jobj2->scale.x * arg9);
    arg2->scale.y =
        __fmadds(jobj1->scale.y, arg8, jobj2->scale.y * arg9);
    arg2->scale.z =
        __fmadds(jobj1->scale.z, arg8, jobj2->scale.z * arg9);
    is_quat_1 = HSD_JObjGetFlags(jobj1) & 0x20000;
    is_quat_2 = HSD_JObjGetFlags(jobj2) & 0x20000;
    if (!is_quat_1 && !is_quat_2) {
        dx = jobj1->rotate.x - jobj2->rotate.x;
        dy = jobj1->rotate.y - jobj2->rotate.y;
        dz = jobj1->rotate.z - jobj2->rotate.z;
        if (ABS(dx) <= 0.0001f && ABS(dy) <= 0.0001f && ABS(dz) <= 0.0001f) {
            arg2->rotate = jobj1->rotate;
            HSD_JObjClearFlags(arg2, 0x20000);
            HSD_JObjSetFlags(arg2, 0x40);
            return;
        }
    }
    if (is_quat_1) {
        quat1 = jobj1->rotate;
    } else {
        sp60.x = jobj1->rotate.x;
        sp60.y = jobj1->rotate.y;
        sp60.z = jobj1->rotate.z;
        EulerToQuat(&sp60, &quat1);
    }
    if (is_quat_2) {
        quat2 = jobj2->rotate;
    } else {
        sp54.x = jobj2->rotate.x;
        sp54.y = jobj2->rotate.y;
        sp54.z = jobj2->rotate.z;
        EulerToQuat(&sp54, &quat2);
    }

    sx = SQ(quat1.x + quat2.x);
    sy = SQ(quat1.y + quat2.y);
    sz = SQ(quat1.z + quat2.z);
    sw = SQ(quat1.w + quat2.w);

    dx = SQ(quat1.x - quat2.x);
    dy = SQ(quat1.y - quat2.y);
    dz = SQ(quat1.z - quat2.z);
    dw = SQ(quat1.w - quat2.w);

    sum_square_sums = sx + sy + sz + sw;
    sum_square_diffs = dx + dy + dz + dw;

    if (sum_square_diffs > sum_square_sums) {
        quat2.x = -quat2.x;
        quat2.y = -quat2.y;
        quat2.z = -quat2.z;
        quat2.w = -quat2.w;
    }
    HSD_QuatLib_8037EF28(&quat1, &quat2, &arg2->rotate, arg9);
    HSD_JObjSetFlags(arg2, 0x20000U);
    HSD_JObjSetFlags(arg2, 0x40U);
}

void lbCopyJObjSRT(HSD_JObj* src, HSD_JObj* dst)
{
    dst->rotate = src->rotate;
    dst->scale = src->scale;
    dst->translate = src->translate;
    if (HSD_JObjGetFlags(src) & JOBJ_USE_QUATERNION) {
        HSD_JObjSetFlags(dst, JOBJ_USE_QUATERNION);
    } else {
        HSD_JObjClearFlags(dst, JOBJ_USE_QUATERNION);
    }
    HSD_JObjSetFlags(dst, JOBJ_MTX_DIRTY);
}

void lb_8000C868(HSD_Joint* arg0, HSD_JObj* arg1, HSD_JObj* arg2, float arg8,
                 float arg9)
{
    Vec3 spC0;
    Vec3 spB4;
    Quaternion spA4;
    Quaternion sp94;
    Quaternion sum;
    Quaternion dif;
    float temp_f0;
    float temp_f10;
    float temp_f1;
    float temp_f2;
    float temp_f2_2;
    float temp_f3;
    float temp_f3_2;
    float temp_f4;
    float temp_f5;
    float temp_f5_2;
    float temp_f6_2;
    float temp_f8;
    float* temp_r3;
    float* temp_r3_2;
    s32 temp_r31;
    float phi_f1;
    float phi_f1_2;
    float phi_f1_3;

    // Same retail blend shape at GALE01 0x8000C898--0x8000C910 for the
    // static-joint/animated-pose path used by directional shield placement.
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000C868
    arg2->translate.x =
        __fmadds(arg0->position.x, arg8, arg1->translate.x * arg9);
    arg2->translate.y =
        __fmadds(arg0->position.y, arg8, arg1->translate.y * arg9);
    arg2->translate.z =
        __fmadds(arg0->position.z, arg8, arg1->translate.z * arg9);
    arg2->scale.x =
        __fmadds(arg0->scale.x, arg8, arg1->scale.x * arg9);
    arg2->scale.y =
        __fmadds(arg0->scale.y, arg8, arg1->scale.y * arg9);
    arg2->scale.z =
        __fmadds(arg0->scale.z, arg8, arg1->scale.z * arg9);
    temp_r31 = HSD_JObjGetFlags(arg1) & 0x20000;
    if (temp_r31 == 0) {
        temp_f5 = arg0->rotation.x - arg1->rotate.x;
        temp_f3 = arg0->rotation.y - arg1->rotate.y;
        temp_f2 = arg0->rotation.z - arg1->rotate.z;
        phi_f1 = temp_f5 < 0 ? -temp_f5 : temp_f5;
        if (phi_f1 <= 1e-4F) {
            phi_f1_2 = temp_f3 < 0 ? -temp_f3 : temp_f3;
            if (phi_f1_2 <= 1e-4F) {
                phi_f1_3 = temp_f2 < 0 ? -temp_f2 : temp_f2;
                if (phi_f1_3 <= 1e-4F) {
                    arg2->rotate.x = arg0->rotation.x;
                    arg2->rotate.y = arg0->rotation.y;
                    arg2->rotate.z = arg0->rotation.z;
                    HSD_JObjClearFlags(arg2, 0x20000U);
                    HSD_JObjSetFlags(arg2, 0x40U);
                    return;
                }
            }
        }
    }
#ifdef MSL_CORE_NATIVE
    spA4 = *msl_native_joint_quaternion(arg0);
#else
    spC0.x = arg0->rotation.x;
    spC0.y = arg0->rotation.y;
    spC0.z = arg0->rotation.z;
    EulerToQuat(&spC0, &spA4);
#endif
    if (temp_r31 != 0) {
        sp94 = arg1->rotate;
    } else {
        spB4.x = arg1->rotate.x;
        spB4.y = arg1->rotate.y;
        spB4.z = arg1->rotate.z;
        EulerToQuat(&spB4, (Quaternion*) &sp94);
    }

    sum.x = SQ(spA4.x + sp94.x);
    sum.y = SQ(spA4.y + sp94.y);
    sum.z = SQ(spA4.z + sp94.z);
    sum.w = SQ(spA4.w + sp94.w);

    dif.x = SQ(spA4.x - sp94.x);
    dif.y = SQ(spA4.y - sp94.y);
    dif.z = SQ(spA4.z - sp94.z);
    dif.w = SQ(spA4.w - sp94.w);

    if (dif.x + dif.y + dif.z + dif.w > sum.x + sum.y + sum.z + sum.w) {
        sp94.x = -sp94.x;
        sp94.y = -sp94.y;
        sp94.z = -sp94.z;
        sp94.w = -sp94.w;
    }
    HSD_QuatLib_8037EF28(&spA4, &sp94, &arg2->rotate, arg9);
    HSD_JObjSetFlags(arg2, 0x20000U);
    HSD_JObjSetFlags(arg2, 0x40U);
}

#ifdef MSL_CORE_NATIVE
enum {
    MSL_LB_BLEND_MAX_JOINTS = 140,
    MSL_LB_BLEND_MAX_EULERS = MSL_LB_BLEND_MAX_JOINTS * 2,
};

static inline void msl_blend_set_flags(HSD_JObj* joint, bool quaternion)
{
    if (HSD_JObjMtxIsDirty(joint)) {
        if (quaternion) {
            joint->flags |= JOBJ_USE_QUATERNION;
        } else {
            joint->flags &= ~JOBJ_USE_QUATERNION;
        }
    } else {
        if (!quaternion) {
            HSD_JObjClearFlags(joint, JOBJ_USE_QUATERNION);
        }
        HSD_JObjSetFlags(joint, quaternion ? JOBJ_USE_QUATERNION |
                                                JOBJ_MTX_DIRTY
                                          : JOBJ_MTX_DIRTY);
    }
}

static void msl_euler_to_quat_batch(const Vec3* const* rotations,
                                    Quaternion* const* outputs, size_t count)
{
    float angles[MSL_LB_BLEND_MAX_EULERS * 3];
    float sin_values[MSL_LB_BLEND_MAX_EULERS * 3];
    float cos_values[MSL_LB_BLEND_MAX_EULERS * 3];
    size_t i;

    HSD_ASSERT(733, count <= MSL_LB_BLEND_MAX_EULERS);
    for (i = 0; i < count; ++i) {
        size_t angle = i * 3;
        angles[angle + 0] = 0.5F * rotations[i]->x;
        angles[angle + 1] = 0.5F * rotations[i]->y;
        angles[angle + 2] = 0.5F * rotations[i]->z;
    }
    msl_sincosf_many(angles, sin_values, cos_values, (int) count * 3);
    for (i = 0; i < count; ++i) {
        size_t angle = i * 3;
        Quaternion* quat = outputs[i];
        float sx = sin_values[angle + 0];
        float sy = sin_values[angle + 1];
        float sz = sin_values[angle + 2];
        float cx = cos_values[angle + 0];
        float cy = cos_values[angle + 1];
        float cz = cos_values[angle + 2];
        float ss = sy * sz;
        float cc = cy * cz;
        quat->w = __fmadds(cx, cc, sx * ss);
        quat->x = __fmsubs(sx, cc, cx * ss);
        quat->y = __fmadds(cz, cx * sy, sz * (sx * cy));
        quat->z = __fmsubs(sz, cx * cy, cz * (sx * sy));
    }
}

static void msl_blend_quaternion_batch(Quaternion* first, Quaternion* second,
                                       HSD_JObj* const* output,
                                       const uint8_t* active, size_t count,
                                       float weight)
{
    float angles[MSL_LB_BLEND_MAX_JOINTS * 3];
    float sin_values[MSL_LB_BLEND_MAX_JOINTS * 3];
    float cos_values[MSL_LB_BLEND_MAX_JOINTS * 3];
    uint8_t general_joint[MSL_LB_BLEND_MAX_JOINTS];
    size_t general_count = 0;
    size_t i;

    for (i = 0; i < count; ++i) {
        Quaternion sum;
        Quaternion dif;
        float cosom;
        float theta;
        size_t angle;
        if (!active[i]) {
            continue;
        }
        sum.x = SQ(first[i].x + second[i].x);
        sum.y = SQ(first[i].y + second[i].y);
        sum.z = SQ(first[i].z + second[i].z);
        sum.w = SQ(first[i].w + second[i].w);
        dif.x = SQ(first[i].x - second[i].x);
        dif.y = SQ(first[i].y - second[i].y);
        dif.z = SQ(first[i].z - second[i].z);
        dif.w = SQ(first[i].w - second[i].w);
        if (dif.x + dif.y + dif.z + dif.w >
            sum.x + sum.y + sum.z + sum.w)
        {
            second[i].x = -second[i].x;
            second[i].y = -second[i].y;
            second[i].z = -second[i].z;
            second[i].w = -second[i].w;
        }
        cosom = __fmadds(
            first[i].w, second[i].w,
            __fmadds(first[i].z, second[i].z,
                     __fmadds(first[i].x, second[i].x,
                              first[i].y * second[i].y)));
        if ((1.0F + cosom) > 1e-10F &&
            (1.0F - cosom) > 1e-10F)
        {
            theta = acosf(cosom);
            angle = general_count * 3;
            general_joint[general_count] = (uint8_t) i;
            angles[angle + 0] = theta;
            angles[angle + 1] = (1.0F - weight) * theta;
            angles[angle + 2] = weight * theta;
            ++general_count;
        } else {
            HSD_QuatLib_8037EF28(&first[i], &second[i], &output[i]->rotate,
                                 weight);
            msl_blend_set_flags(output[i], true);
        }
    }
    msl_sincosf_many(angles, sin_values, cos_values,
                     (int) general_count * 3);
    for (i = 0; i < general_count; ++i) {
        size_t joint = general_joint[i];
        size_t angle = i * 3;
        float sinom;
        float sp;
        float sq;
        sinom = sin_values[angle + 0];
        sp = sin_values[angle + 1] / sinom;
        sq = sin_values[angle + 2] / sinom;
        output[joint]->rotate.x =
            __fmadds(sp, first[joint].x, sq * second[joint].x);
        output[joint]->rotate.y =
            __fmadds(sp, first[joint].y, sq * second[joint].y);
        output[joint]->rotate.z =
            __fmadds(sp, first[joint].z, sq * second[joint].z);
        output[joint]->rotate.w =
            __fmadds(sp, first[joint].w, sq * second[joint].w);
        msl_blend_set_flags(output[joint], true);
    }
}

void msl_lb_blend_joint_batch(HSD_Joint* const* authored,
                              HSD_JObj* const* live, size_t count,
                              float authored_weight, float live_weight)
{
    Quaternion source_quats[MSL_LB_BLEND_MAX_JOINTS];
    Quaternion live_quats[MSL_LB_BLEND_MAX_JOINTS];
    const Vec3* euler_rotations[MSL_LB_BLEND_MAX_EULERS];
    Quaternion* euler_outputs[MSL_LB_BLEND_MAX_EULERS];
    uint8_t active[MSL_LB_BLEND_MAX_JOINTS];
    size_t euler_count = 0;
    size_t i;

    HSD_ASSERT(733, count <= MSL_LB_BLEND_MAX_JOINTS);
    for (i = 0; i < count; ++i) {
        HSD_Joint* source = authored[i];
        HSD_JObj* joint = live[i];
        bool is_quaternion = (joint->flags & JOBJ_USE_QUATERNION) != 0;
        float dx;
        float dy;
        float dz;

        joint->translate.x = __fmadds(source->position.x, authored_weight,
                                     joint->translate.x * live_weight);
        joint->translate.y = __fmadds(source->position.y, authored_weight,
                                     joint->translate.y * live_weight);
        joint->translate.z = __fmadds(source->position.z, authored_weight,
                                     joint->translate.z * live_weight);
        joint->scale.x = __fmadds(source->scale.x, authored_weight,
                                 joint->scale.x * live_weight);
        joint->scale.y = __fmadds(source->scale.y, authored_weight,
                                 joint->scale.y * live_weight);
        joint->scale.z = __fmadds(source->scale.z, authored_weight,
                                 joint->scale.z * live_weight);
        if (!is_quaternion) {
            dx = source->rotation.x - joint->rotate.x;
            dy = source->rotation.y - joint->rotate.y;
            dz = source->rotation.z - joint->rotate.z;
            if (fabsf(dx) <= 1e-4F && fabsf(dy) <= 1e-4F &&
                fabsf(dz) <= 1e-4F)
            {
                joint->rotate.x = source->rotation.x;
                joint->rotate.y = source->rotation.y;
                joint->rotate.z = source->rotation.z;
                msl_blend_set_flags(joint, false);
                active[i] = 0;
                continue;
            }
        }
        active[i] = 1;
        source_quats[i] = *msl_native_joint_quaternion(source);
        if (is_quaternion) {
            live_quats[i] = joint->rotate;
        } else {
            euler_rotations[euler_count] = (const Vec3*) &joint->rotate;
            euler_outputs[euler_count] = &live_quats[i];
            ++euler_count;
        }
    }
    msl_euler_to_quat_batch(euler_rotations, euler_outputs, euler_count);
    msl_blend_quaternion_batch(source_quats, live_quats, live, active, count,
                               live_weight);
}

void msl_lb_blend_jobj_batch(HSD_JObj* const* first,
                             HSD_JObj* const* second, size_t count,
                             float first_weight, float second_weight)
{
    Quaternion first_quats[MSL_LB_BLEND_MAX_JOINTS];
    Quaternion second_quats[MSL_LB_BLEND_MAX_JOINTS];
    const Vec3* euler_rotations[MSL_LB_BLEND_MAX_EULERS];
    Quaternion* euler_outputs[MSL_LB_BLEND_MAX_EULERS];
    uint8_t active[MSL_LB_BLEND_MAX_JOINTS];
    size_t euler_count = 0;
    size_t i;

    HSD_ASSERT(836, count <= MSL_LB_BLEND_MAX_JOINTS);
    for (i = 0; i < count; ++i) {
        HSD_JObj* lhs = first[i];
        HSD_JObj* rhs = second[i];
        bool lhs_quat = (lhs->flags & JOBJ_USE_QUATERNION) != 0;
        bool rhs_quat = (rhs->flags & JOBJ_USE_QUATERNION) != 0;
        float dx;
        float dy;
        float dz;

        rhs->translate.x = __fmadds(lhs->translate.x, first_weight,
                                    rhs->translate.x * second_weight);
        rhs->translate.y = __fmadds(lhs->translate.y, first_weight,
                                    rhs->translate.y * second_weight);
        rhs->translate.z = __fmadds(lhs->translate.z, first_weight,
                                    rhs->translate.z * second_weight);
        rhs->scale.x = __fmadds(lhs->scale.x, first_weight,
                                rhs->scale.x * second_weight);
        rhs->scale.y = __fmadds(lhs->scale.y, first_weight,
                                rhs->scale.y * second_weight);
        rhs->scale.z = __fmadds(lhs->scale.z, first_weight,
                                rhs->scale.z * second_weight);
        if (!lhs_quat && !rhs_quat) {
            dx = lhs->rotate.x - rhs->rotate.x;
            dy = lhs->rotate.y - rhs->rotate.y;
            dz = lhs->rotate.z - rhs->rotate.z;
            if (fabsf(dx) <= 1e-4F && fabsf(dy) <= 1e-4F &&
                fabsf(dz) <= 1e-4F)
            {
                rhs->rotate = lhs->rotate;
                msl_blend_set_flags(rhs, false);
                active[i] = 0;
                continue;
            }
        }
        active[i] = 1;
        if (lhs_quat) {
            first_quats[i] = lhs->rotate;
        } else {
            euler_rotations[euler_count] = (const Vec3*) &lhs->rotate;
            euler_outputs[euler_count++] = &first_quats[i];
        }
        if (rhs_quat) {
            second_quats[i] = rhs->rotate;
        } else {
            euler_rotations[euler_count] = (const Vec3*) &rhs->rotate;
            euler_outputs[euler_count++] = &second_quats[i];
        }
    }
    msl_euler_to_quat_batch(euler_rotations, euler_outputs, euler_count);
    msl_blend_quaternion_batch(first_quats, second_quats, second, active,
                               count, second_weight);
}
#endif

static s32 lbGetFreeColorRegImpl(s32 i0, HSD_TevDesc* tevdesc, HSD_TExp* texp1,
                                 HSD_TExp* texp2)
{
    bool register_used[8];
    int i;

    for (i = 0; i < 8; i++) {
        register_used[i] = false;
    }
    while (texp1 != NULL) {
        if (texp1->cnst.reg < 8) {
            register_used[texp1->cnst.reg] = true;
        }
        texp1 = texp1->cnst.next;
    }
    while (tevdesc != NULL) {
        s32 idx = lb_803B9FF0[tevdesc->u.tevconf.clr_out_reg];
        register_used[idx] = true;
        tevdesc = tevdesc->next;
    }
    while (texp2 != NULL) {
        if (texp2->cnst.reg < 8) {
            register_used[texp2->cnst.reg] = true;
        }
        texp2 = texp2->cnst.next;
    }
    for (i = i0; i < 7; i++) {
        if (!register_used[i]) {
            return i;
        }
    }
    return -1;
}

s32 lbGetFreeColorRegister(s32 i0, HSD_MObj* mobj, HSD_TExp* texp)
{
    return lbGetFreeColorRegImpl(i0, &mobj->tevdesc->desc, mobj->texp, texp);
}

s32 lb_8000CC8C(s32 i)
{
    return lb_803B9FC0[i];
}

s32 lb_8000CCA4(s32 i)
{
    return lb_803B9FE0[i];
}

static s32 lbGetFreeAlphaRegImpl(s32 i0, HSD_TevDesc* cur, HSD_TExp* arg2,
                                 HSD_TExp* arg3)
{
    bool register_used[8];
    int i;

    for (i = 0; i < 8; i++) {
        register_used[i] = false;
    }
    while (cur != NULL) {
        s32 idx = lb_803BA030[cur->u.tevconf.alpha_out_reg];
        register_used[idx] = true;
        cur = cur->next;
    }
    for (i = i0; i < 7; i++) {
        if (!register_used[i]) {
            return i;
        }
    }
    return -1;
}

s32 lbGetFreeAlphaRegister(s32 i0, HSD_MObj* mobj, HSD_TExp* texp)
{
    return lbGetFreeAlphaRegImpl(i0, &mobj->tevdesc->desc, mobj->texp, texp);
}

s32 lb_8000CD90(s32 i)
{
    return lb_803BA000[i];
}

s32 lb_8000CDA8(s32 i)
{
    return lb_803BA020[i];
}

inline HSD_LObj* lobj_next(HSD_LObj* lobj)
{
    if (lobj == NULL) {
        return NULL;
    }
    return lobj->next;
}

HSD_LObj* lb_8000CDC0(HSD_LObj* cur)
{
    while (cur != NULL) {
        if (!(cur->flags & ((1 << 0) | (1 << 1))) &&
            !(HSD_LObjGetFlags(cur) & (1 << 5)))
        {
            return cur;
        }
        cur = lobj_next(cur);
    }
#ifdef BUGFIX
    return NULL;
#endif
}

void lb_8000CE30(HSD_DObj* dobj, HSD_DObj* next)
{
    if (dobj != NULL) {
        dobj->next = next;
    }
}

void lb_8000CE40(HSD_JObj* jobj, HSD_DObj* dobj)
{
    if (jobj != NULL) {
        jobj->u.dobj = dobj;
    }
}
