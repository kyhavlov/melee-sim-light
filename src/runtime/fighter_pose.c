#include "runtime/fighter_pose.h"

#include "platform/memory.h"
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#endif
#include "runtime/context.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <MetroTRK/intrinsics.h>
#include <baselib/aobj.h>
#include <baselib/debug.h>
#include <baselib/fobj.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>
#include <baselib/spline.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbanim.h>
#include <MSL/trigf.h>

enum {
    MSL_FIGHTER_POSE_MAGIC = 0x4D534C50,
};

static MslFighterPose* active_pose(void)
{
    return msl_fighter_pose();
}

static bool is_figa(const MslFighterPoseJoint* node)
{
    return node->program_index != MSL_FIGHTER_POSE_PROGRAM_NONE;
}

static bool is_attached(const MslFighterPoseJoint* node)
{
    return is_figa(node) || node->track_count != 0;
}

#ifdef MSL_CORE_NATIVE
static const MslFighterPosePrograms* active_programs(void)
{
    return msl_context_fighter_pose_programs;
}

static uint16_t find_program(const FigaTree* tree)
{
    const MslFighterPosePrograms* programs = active_programs();
    uint32_t begin = 0;
    uint32_t end = programs->program_count;
    uintptr_t target = (uintptr_t) tree;
    while (begin < end) {
        uint32_t middle = begin + (end - begin) / 2;
        uintptr_t candidate =
            (uintptr_t) programs->programs[middle].tree;
        if (candidate < target) {
            begin = middle + 1;
        } else {
            end = middle;
        }
    }
    HSD_ASSERT(51, begin < programs->program_count &&
                       programs->programs[begin].tree == tree);
    HSD_ASSERT(52, begin < MSL_FIGHTER_POSE_PROGRAM_NONE);
    return (uint16_t) begin;
}
#else
static uint16_t find_program(const FigaTree* tree)
{
    (void) tree;
    return MSL_FIGHTER_POSE_PROGRAM_NONE;
}
#endif

static MslFighterPoseJoint* pose_joint(const HSD_JObj* joint)
{
    MslFighterPoseJoint* node;
    if (joint == NULL || joint->aobj == NULL) {
        return NULL;
    }
    node = (MslFighterPoseJoint*) joint->aobj;
    return node->magic == MSL_FIGHTER_POSE_MAGIC ? node : NULL;
}

bool msl_fighter_pose_owns_joint(const HSD_JObj* joint)
{
    return pose_joint(joint) != NULL;
}

bool msl_fighter_pose_path(const HSD_JObj* joint, HSD_JObj** path)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    if (node == NULL) {
        return false;
    }
    *path = node->path;
    return true;
}

int msl_fighter_pose_init(MslFighterPose* pose)
{
#ifdef MSL_CORE_NATIVE
    pose->joints = HSD_MemAllocReloc(
        sizeof(*pose->joints) * MSL_FIGHTER_POSE_JOINT_CAPACITY,
        MSL_RELOC_FIGHTER_POSE_JOINT, MSL_FIGHTER_POSE_JOINT_CAPACITY,
        sizeof(*pose->joints), 0);
    pose->tracks = HSD_MemAllocReloc(
        sizeof(*pose->tracks) * MSL_FIGHTER_POSE_TRACK_CAPACITY,
        MSL_RELOC_FIGHTER_POSE_TRACK, MSL_FIGHTER_POSE_TRACK_CAPACITY,
        sizeof(*pose->tracks), 0);
    if (pose->joints == NULL || pose->tracks == NULL) {
        return -1;
    }
    pose->joint_count = 0;
    pose->track_used = 0;
    pose->ecb_count = 0;
    return 0;
#else
    (void) pose;
    return 0;
#endif
}

static void register_joint(HSD_JObj* joint)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* node;
    MslFighterPoseJoint* parent;
    if (joint == NULL || (joint->flags & JOBJ_MSL_GAMEPLAY_COLD) ||
        pose_joint(joint) != NULL)
    {
        return;
    }
    HSD_ASSERT(70, joint->aobj == NULL);
    HSD_ASSERT(71, pose->joint_count < MSL_FIGHTER_POSE_JOINT_CAPACITY);
    node = &pose->joints[pose->joint_count++];
    memset(node, 0, sizeof(*node));
    node->magic = MSL_FIGHTER_POSE_MAGIC;
    node->flags = AOBJ_NO_ANIM;
    node->joint = joint;
    node->framerate = 1.0F;
    node->program_index = MSL_FIGHTER_POSE_PROGRAM_NONE;
    parent = pose_joint(joint->parent);
    node->parent_index = parent != NULL
                             ? (uint16_t) (parent - pose->joints)
                             : UINT16_MAX;
    joint->aobj = (HSD_AObj*) node;
}

static void register_tree(HSD_JObj* joint)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* node;
    HSD_JObj* child;
    uint16_t start;
    if (joint == NULL || (joint->flags & JOBJ_MSL_GAMEPLAY_COLD)) {
        return;
    }
    start = pose->joint_count;
    register_joint(joint);
    node = &pose->joints[start];
    if (joint->flags & JOBJ_INSTANCE) {
        node->tree_count = 1;
        return;
    }
    for (child = joint->child; child != NULL; child = child->next) {
        register_tree(child);
    }
    node->tree_count = pose->joint_count - start;
}

void msl_fighter_pose_register_tree(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    if (root == NULL || pose_joint(root) != NULL ||
        (root->flags & JOBJ_MSL_GAMEPLAY_COLD))
    {
        return;
    }
    register_tree(root);
}

void msl_fighter_pose_set_root_position(HSD_JObj* root, const Vec3* position)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    bool affected[MSL_FIGHTER_POSE_JOINT_CAPACITY];
    uint16_t root_index;
    uint16_t i;

    HSD_ASSERT(150, root_node != NULL);
    root_index = (uint16_t) (root_node - pose->joints);
    HSD_ASSERT(151, root_node->parent_index == UINT16_MAX);
    if (memcmp(&root->translate, position, sizeof(*position)) == 0) {
        return;
    }
    root->translate = *position;
    if (HSD_JObjMtxIsDirty(root)) {
        return;
    }
    root->mtx[0][3] = position->x;
    root->mtx[1][3] = position->y;
    root->mtx[2][3] = position->z;
    affected[root_index] = true;
    for (i = root_index + 1; i < root_index + root_node->tree_count; ++i)
    {
        MslFighterPoseJoint* node = &pose->joints[i];
        HSD_JObj* joint = node->joint;
        int row;
        if (node->parent_index == UINT16_MAX ||
            !affected[node->parent_index] ||
            (joint->flags & (JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT)) ||
            node->path != NULL)
        {
            affected[i] = false;
            continue;
        }
        affected[i] = true;
        if (HSD_JObjMtxIsDirty(joint)) {
            continue;
        }
        if (joint->robj != NULL || (joint->flags & JOBJ_JOINT) != 0 ||
            (joint->flags & JOBJ_MTX_INDEP_SRT))
        {
            HSD_JObjSetMtxDirty(joint);
            continue;
        }
        for (row = 0; row != 3; ++row) {
            float value = joint->parent->mtx[row][0] * joint->translate.x;
            value = fmaf(joint->translate.y, joint->parent->mtx[row][1],
                         value);
            value = fmaf(joint->translate.z, joint->parent->mtx[row][2],
                         value);
            joint->mtx[row][3] =
                fmaf(1.0F, joint->parent->mtx[row][3], value);
        }
    }
}

bool msl_fighter_pose_transform_point(HSD_JObj* joint, const Vec3* local,
                                      Vec3* world)
{
    if (pose_joint(joint) == NULL) {
        return false;
    }
    HSD_JObjSetupMatrix(joint);
    if (local == NULL ||
        (local->x == 0.0F && local->y == 0.0F && local->z == 0.0F))
    {
        world->x = joint->mtx[0][3];
        world->y = joint->mtx[1][3];
        world->z = joint->mtx[2][3];
    } else {
        MTXMultVec(joint->mtx, (Vec3*) local, world);
    }
    return true;
}

bool msl_fighter_pose_transform_pair(HSD_JObj* joint, const Vec3* local_a,
                                     const Vec3* local_b, Vec3* world_a,
                                     Vec3* world_b)
{
    if (pose_joint(joint) == NULL) {
        return false;
    }
    HSD_JObjSetupMatrix(joint);
    MTXMultVec(joint->mtx, (Vec3*) local_a, world_a);
    MTXMultVec(joint->mtx, (Vec3*) local_b, world_b);
    return true;
}

void msl_fighter_pose_bind_origins(HSD_JObj* const joints[6])
{
    MslFighterPose* pose = active_pose();
    uint64_t needed[(MSL_FIGHTER_POSE_JOINT_CAPACITY + 63) / 64] = { 0 };
    uint16_t first = UINT16_MAX;
    uint16_t last = 0;
    uint16_t count = 0;
    uint16_t origin;
    uint16_t i;

    for (i = 0; i < 6; ++i) {
        MslFighterPoseJoint* node = pose_joint(joints[i]);
        if (node == NULL) {
            return;
        }
        for (;;) {
            uint16_t index = (uint16_t) (node - pose->joints);
            HSD_JObj* joint = node->joint;
            needed[index >> 6] |= UINT64_C(1) << (index & 63);
            if (index < first) {
                first = index;
            }
            if (index > last) {
                last = index;
            }
            if (node->parent_index == UINT16_MAX) {
                break;
            }
            node = &pose->joints[node->parent_index];
        }
    }

    origin = (uint16_t) (pose_joint(joints[0]) - pose->joints);
    for (i = 0; i < pose->ecb_count; ++i) {
        if (pose->ecb[i].origin == origin) {
            return;
        }
    }
    HSD_ASSERT(272, pose->ecb_count < MSL_FIGHTER_POSE_ECB_CAPACITY);
    pose->ecb[pose->ecb_count].origin = origin;
    for (i = first; i <= last; ++i) {
        if ((needed[i >> 6] & (UINT64_C(1) << (i & 63))) != 0) {
            MslFighterPoseJoint* node = &pose->joints[i];
            HSD_JObj* joint = node->joint;
            uint16_t encoded = i;
            HSD_ASSERT(273, count < MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY);
            if (joint->parent != NULL &&
                HSD_JOBJ_METHOD(joint)->make_mtx == HSD_JObjMakeMatrix &&
                joint->robj == NULL &&
                (joint->flags &
                 (JOBJ_JOINT | JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT |
                  JOBJ_MTX_INDEP_SRT)) == 0)
            {
                encoded |= 0x8000;
            }
            pose->ecb[pose->ecb_count].joints[count++] = encoded;
        }
    }
    pose->ecb[pose->ecb_count].joint_count = count;
    pose->ecb_count += 1;
}

static void setup_euler_matrix(HSD_JObj* joint, const float sin_xyz[3],
                               const float cos_xyz[3])
{
    Vec3* parent_scale = NULL;

    HSD_ASSERT(322, joint->parent != NULL);
    if (joint->flags & JOBJ_CLASSICAL_SCALE) {
        if (joint->parent->scl != NULL) {
            if (joint->scl == NULL) {
                joint->scl = HSD_VecAlloc();
            }
            *joint->scl = *joint->parent->scl;
        } else if (joint->scl != NULL) {
            HSD_VecFree(joint->scl);
            joint->scl = NULL;
        }
    } else {
        if (joint->scl == NULL) {
            joint->scl = HSD_VecAlloc();
        }
        if (joint->parent->scl != NULL) {
            joint->scl->x = joint->scale.x * joint->parent->scl->x;
            joint->scl->y = joint->scale.y * joint->parent->scl->y;
            joint->scl->z = joint->scale.z * joint->parent->scl->z;
        } else {
            *joint->scl = joint->scale;
        }
    }
    if (joint->parent->scl != NULL) {
        parent_scale = joint->parent->scl;
    }
    HSD_MtxSRTConcatTrig(joint->mtx, joint->parent->mtx, &joint->scale,
                         &joint->translate, parent_scale, sin_xyz, cos_xyz);
    joint->flags &= ~JOBJ_MTX_DIRTY;
}

void msl_fighter_pose_transform_origins(HSD_JObj* const joints[6],
                                        Vec3 world[6])
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* origin = pose_joint(joints[0]);
    HSD_JObj* dirty[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY];
    float angles[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY * 3];
    float sin_values[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY * 3];
    float cos_values[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY * 3];
    uint16_t trig_slot[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY];
    uint16_t origin_index;
    uint16_t binding;
    uint16_t dirty_count = 0;
    uint16_t direct_count = 0;
    uint16_t i;

    HSD_ASSERT(367, origin != NULL);
    origin_index = (uint16_t) (origin - pose->joints);
    for (binding = 0; binding < pose->ecb_count; ++binding) {
        if (pose->ecb[binding].origin == origin_index) {
            break;
        }
    }
    HSD_ASSERT(374, binding < pose->ecb_count);
    for (i = 0; i < pose->ecb[binding].joint_count; ++i) {
        uint16_t encoded = pose->ecb[binding].joints[i];
        MslFighterPoseJoint* node = &pose->joints[encoded & 0x7FFF];
        HSD_JObj* joint = node->joint;
        if (HSD_JObjMtxIsDirty(joint)) {
            dirty[dirty_count] = joint;
            if ((encoded & 0x8000) != 0 && node->path == NULL &&
                (joint->flags & JOBJ_USE_QUATERNION) == 0)
            {
                trig_slot[dirty_count] = direct_count;
                memcpy(&angles[direct_count * 3], &joint->rotate,
                       3 * sizeof(float));
                direct_count += 1;
            } else {
                trig_slot[dirty_count] = UINT16_MAX;
            }
            dirty_count += 1;
        }
    }
    msl_sincosf_many(angles, sin_values, cos_values, direct_count * 3);
    for (i = 0; i < dirty_count; ++i) {
        uint16_t slot = trig_slot[i];
        if (slot == UINT16_MAX) {
            HSD_JObjSetupMatrixSub(dirty[i]);
        } else {
            setup_euler_matrix(dirty[i], &sin_values[slot * 3],
                               &cos_values[slot * 3]);
        }
    }
    for (i = 0; i < 6; ++i) {
        world[i].x = joints[i]->mtx[0][3];
        world[i].y = joints[i]->mtx[1][3];
        world[i].z = joints[i]->mtx[2][3];
    }
}

static void release_joint(MslFighterPoseJoint* node)
{
    node->track_count = 0;
    node->track_start = 0;
    node->flags = AOBJ_NO_ANIM;
    node->curr_frame = 0.0F;
    node->rewind_frame = 0.0F;
    node->end_frame = 0.0F;
    node->framerate = 1.0F;
    node->program_index = MSL_FIGHTER_POSE_PROGRAM_NONE;
    node->program_track_start = 0;
    node->program_track_count = 0;
    node->program_filtered = 0;
    node->decoder_synced = 0;
    if (node->path != NULL) {
        HSD_JObjUnref(node->path);
        node->path = NULL;
    }
}

void msl_fighter_pose_remove_tree(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        release_joint(&pose->joints[i]);
    }
}

static void compact_tracks(MslFighterPose* pose)
{
    uint16_t order[MSL_FIGHTER_POSE_JOINT_CAPACITY];
    uint16_t count = 0;
    uint16_t cursor = 0;
    uint16_t i;

    for (i = 0; i < pose->joint_count; ++i) {
        MslFighterPoseJoint* node = &pose->joints[i];
        if (node->track_count == 0) {
            continue;
        }
        order[count++] = i;
    }
    for (i = 1; i < count; ++i) {
        uint16_t index = order[i];
        uint16_t j = i;
        while (j != 0 &&
               pose->joints[order[j - 1]].track_start >
                   pose->joints[index].track_start)
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = index;
    }
    for (i = 0; i < count; ++i) {
        MslFighterPoseJoint* node = &pose->joints[order[i]];
        if (node->track_start != cursor) {
            memmove(&pose->tracks[cursor], &pose->tracks[node->track_start],
                    (size_t) node->track_count * sizeof(*pose->tracks));
            node->track_start = cursor;
        }
        cursor += node->track_count;
    }
    pose->track_used = cursor;
}

static MslFighterPoseTrack* allocate_tracks(MslFighterPoseJoint* node,
                                            uint16_t count)
{
    MslFighterPose* pose = active_pose();
    if ((uint32_t) pose->track_used + count > MSL_FIGHTER_POSE_TRACK_CAPACITY)
    {
        compact_tracks(pose);
    }
    HSD_ASSERT(163, (uint32_t) pose->track_used + count <=
                        MSL_FIGHTER_POSE_TRACK_CAPACITY);
    node->track_start = pose->track_used;
    node->track_count = count;
    pose->track_used += count;
    memset(&pose->tracks[node->track_start], 0,
           (size_t) count * sizeof(*pose->tracks));
    return &pose->tracks[node->track_start];
}

static void init_track(MslFighterPoseTrack* destination,
                       const FigaTrack* source)
{
    destination->startframe = source->startframe;
    destination->obj_type = source->obj_type;
    destination->frac_value = source->frac_value;
    destination->frac_slope = source->frac_slope;
    destination->ad_head = source->ad_head;
    destination->length = source->length;
}

void msl_fighter_pose_attach_figa(HSD_JObj* joint, FigaTree* tree,
                                  FigaTrack* tracks, int track_count,
                                  bool filtered)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    MslFighterPoseTrack* destination;
    int retained = 0;
    int i;
    if (node == NULL || track_count == 0) {
        return;
    }
    release_joint(node);
    node->program_index = find_program(tree);
    HSD_ASSERT(355, tracks >= tree->tracks);
    HSD_ASSERT(356, (size_t) (tracks - tree->tracks) <= 0x7FF);
    HSD_ASSERT(357, track_count > 0 && track_count <= 0x7F);
    node->program_track_start = (uint32_t) (tracks - tree->tracks);
    node->program_track_count = (uint32_t) track_count;
    node->program_filtered = filtered;
    for (i = 0; i < track_count; ++i) {
        if (!filtered || (tracks[i].obj_type != 5 &&
                          (uint8_t) (tracks[i].obj_type - 6) > 1))
        {
            ++retained;
        }
    }
    destination = allocate_tracks(node, (uint16_t) retained);
    for (i = 0; i < track_count; ++i) {
        if ((!filtered ||
             (tracks[i].obj_type != 5 &&
              (uint8_t) (tracks[i].obj_type - 6) > 1)) &&
            tracks[i].obj_type == TYPE_JOBJ)
        {
            init_track(destination++, &tracks[i]);
            break;
        }
    }
    for (i = 0; i < track_count; ++i) {
        if (filtered &&
            (tracks[i].obj_type == 5 ||
             (uint8_t) (tracks[i].obj_type - 6) <= 1))
        {
            continue;
        }
        if (tracks[i].obj_type == TYPE_JOBJ) {
            continue;
        }
        init_track(destination++, &tracks[i]);
    }
    node->flags = AOBJ_NO_ANIM | (tree->flags & (AOBJ_LOOP | AOBJ_NO_UPDATE));
    node->end_frame = tree->frames;
    if (tree->type & 1) {
        HSD_JObjSetFlags(joint, JOBJ_CLASSICAL_SCALE);
    } else {
        HSD_JObjClearFlags(joint, JOBJ_CLASSICAL_SCALE);
    }
}

static void init_desc_track(MslFighterPoseTrack* destination,
                            const HSD_FObjDesc* source)
{
    destination->startframe = source->startframe;
    destination->obj_type = source->type;
    destination->frac_value = source->frac_value;
    destination->frac_slope = source->frac_slope;
    destination->ad_head = source->ad;
    destination->length = source->length;
}

void msl_fighter_pose_attach_anim_joint(HSD_JObj* joint,
                                        HSD_AnimJoint* anim_joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    HSD_AObjDesc* desc;
    HSD_FObjDesc* source;
    MslFighterPoseTrack* destination;
    uint16_t count = 0;
    if (node == NULL || anim_joint == NULL) {
        return;
    }
    desc = anim_joint->aobjdesc;
    release_joint(node);
    for (source = desc->fobjdesc; source != NULL; source = source->next) {
        ++count;
    }
    destination = allocate_tracks(node, count);
    for (source = desc->fobjdesc; source != NULL; source = source->next) {
        if (source->type == TYPE_JOBJ) {
            init_desc_track(destination++, source);
            break;
        }
    }
    for (source = desc->fobjdesc; source != NULL; source = source->next) {
        if (source->type == TYPE_JOBJ) {
            continue;
        }
        init_desc_track(destination++, source);
    }
    node->flags = AOBJ_NO_ANIM | (desc->flags & (AOBJ_LOOP | AOBJ_NO_UPDATE));
    node->end_frame = desc->end_frame;
    if (desc->obj_id != 0) {
        HSD_Obj* object = HSD_IDGetDataFromTable(0, desc->obj_id, 0);
        if (object != NULL) {
            ref_INC(object);
        } else {
            object = (HSD_Obj*) HSD_JObjLoadJoint((void*) desc->obj_id);
        }
        node->path = (HSD_JObj*) object;
    }
    HSD_RObjAddAnimAll(joint->robj, anim_joint->robj_anim);
    if (anim_joint->flags & 1) {
        HSD_JObjSetFlags(joint, JOBJ_CLASSICAL_SCALE);
    } else {
        HSD_JObjClearFlags(joint, JOBJ_CLASSICAL_SCALE);
    }
}

static void request_track(MslFighterPoseTrack* track, float frame)
{
    track->ad = track->ad_head;
    track->time = (float) track->startframe + frame;
    track->op = 0;
    track->op_intrp = 0;
    track->flags &= (uint8_t) ~0x40;
    track->nb_pack = 0;
    track->fterm = 0;
    track->p0 = 0.0F;
    track->p1 = 0.0F;
    track->d0 = 0.0F;
    track->d1 = 0.0F;
    track->flags = (track->flags & 0xF0) | FOBJ_LOAD_DATA0;
}

void msl_fighter_pose_request_joint(HSD_JObj* joint, float frame)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* node = pose_joint(joint);
    uint16_t i;
    if (node == NULL || !is_attached(node)) {
        return;
    }
    node->curr_frame = frame;
    node->flags = (node->flags & ~AOBJ_NO_ANIM) | AOBJ_FIRST_PLAY;
    node->decoder_synced = 1;
    for (i = 0; i < node->track_count; ++i) {
        request_track(&pose->tracks[node->track_start + i], frame);
    }
}

void msl_fighter_pose_request_tree(HSD_JObj* root, float frame)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i])) {
            msl_fighter_pose_request_joint(pose->joints[i].joint, frame);
        }
    }
}

void msl_fighter_pose_set_tree_flags(HSD_JObj* root, uint32_t flags)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return;
    }
    flags &= AOBJ_LOOP | AOBJ_NO_UPDATE;
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i])) {
            pose->joints[i].flags |= flags;
        }
    }
}

void msl_fighter_pose_set_tree_rate(HSD_JObj* root, float rate)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i])) {
            pose->joints[i].framerate = rate;
        }
    }
}

static float parse_float(uint8_t** position, uint8_t fraction)
{
    union {
        float f;
        uint32_t u;
    } value;
    float numerator;
    int denominator;
    if (fraction == HSD_A_FRAC_FLOAT) {
        value.u = (uint32_t) (*position)[0] |
                  (uint32_t) (*position)[1] << 8 |
                  (uint32_t) (*position)[2] << 16 |
                  (uint32_t) (*position)[3] << 24;
        *position += 4;
        return value.f;
    }
    denominator = 1 << (fraction & 0x1F);
    switch (fraction & 0xE0) {
    case HSD_A_FRAC_S8:
        numerator = (int8_t) (*position)[0];
        *position += 1;
        break;
    case HSD_A_FRAC_U8:
        numerator = (*position)[0];
        *position += 1;
        break;
    case HSD_A_FRAC_S16:
        numerator = ((int8_t) (*position)[1] << 8) | (*position)[0];
        *position += 2;
        break;
    case HSD_A_FRAC_U16:
        numerator = ((*position)[1] << 8) | (*position)[0];
        *position += 2;
        break;
    default:
        return 0.0F;
    }
    return numerator / denominator;
}

static uint32_t parse_pack_info(uint8_t** position)
{
    uint8_t value = *(*position)++;
    uint32_t count = ((value >> 4) & 7) + 1;
    int shift = 3;
    if (!(value & 0x80)) {
        return count;
    }
    do {
        value = *(*position)++;
        count += (value & 0x7F) << shift;
        shift += 7;
    } while (value & 0x80);
    return count;
}

static int parse_wait(uint8_t** position)
{
    uint8_t value;
    int wait = 0;
    int shift = 0;
    do {
        value = *(*position)++;
        wait |= (value & 0x7F) << shift;
        shift += 7;
    } while (value & 0x80);
    return wait;
}

static uint32_t track_state(const MslFighterPoseTrack* track)
{
    return track->flags & 0xF;
}

static uint32_t set_track_state(MslFighterPoseTrack* track, uint32_t state)
{
    track->flags = (uint8_t) ((track->flags & 0xF0) | (state & 0xF));
    return state;
}

static void launch_key(MslFighterPoseTrack* track)
{
    if (track->flags & 0x40) {
        track->op_intrp = track->op;
        track->flags &= (uint8_t) ~0x40;
        track->flags |= 0x80;
        track->p0 = track->p1;
    }
}

static uint32_t load_wait(MslFighterPoseTrack* track)
{
    if ((uint32_t) (track->ad - track->ad_head) >= track->length) {
        return 6;
    }
    track->fterm = (uint16_t) parse_wait(&track->ad);
    track->flags |= 0x20;
    return set_track_state(track, FOBJ_LOAD_DATA);
}

static uint32_t load_data(MslFighterPoseTrack* track)
{
    uint32_t state = track_state(track);
    if ((uint32_t) (track->ad - track->ad_head) >= track->length) {
        return 6;
    }
    track->op_intrp = track->op;
    if (track->nb_pack == 0) {
        track->op = *track->ad & 0xF;
        track->nb_pack = (uint16_t) parse_pack_info(&track->ad);
    }
    --track->nb_pack;
    switch (track->op) {
    case HSD_A_OP_CON:
    case HSD_A_OP_LIN:
        track->p0 = track->p1;
        track->p1 = parse_float(&track->ad, track->frac_value);
        if (track->op_intrp != HSD_A_OP_SLP) {
            track->d0 = track->d1;
            track->d1 = 0.0F;
        }
        break;
    case HSD_A_OP_SPL0:
        track->p0 = track->p1;
        track->d0 = track->d1;
        track->p1 = parse_float(&track->ad, track->frac_value);
        track->d1 = 0.0F;
        break;
    case HSD_A_OP_SPL:
        track->p0 = track->p1;
        track->p1 = parse_float(&track->ad, track->frac_value);
        track->d0 = track->d1;
        track->d1 = parse_float(&track->ad, track->frac_slope);
        break;
    case HSD_A_OP_SLP:
        track->d0 = track->d1;
        track->d1 = parse_float(&track->ad, track->frac_slope);
        return track_state(track);
    case HSD_A_OP_KEY:
        launch_key(track);
        track->p1 = parse_float(&track->ad, track->frac_value);
        track->flags |= 0x40;
        break;
    default:
        return 0;
    }
    return set_track_state(track,
                           state == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static void publish_value_type(MslFighterPoseJoint* node, uint8_t obj_type,
                               float value)
{
    HSD_ObjData data;
    HSD_JObj* joint = node->joint;
    data.fv = value;
    switch (obj_type) {
    case HSD_A_J_ROTX:
        if (joint->flags & JOBJ_JOINT1) {
            HSD_RObj* robj =
                HSD_RObjGetByType(joint->robj, REFTYPE_IKHINT, 0);
            if (robj != NULL) {
                robj->u.ik_hint.rotate_x = value;
            }
        }
        joint->rotate.x = value;
        break;
    case HSD_A_J_ROTY:
        joint->rotate.y = value;
        break;
    case HSD_A_J_ROTZ:
        joint->rotate.z = value;
        break;
    case HSD_A_J_TRAX:
        joint->translate.x = value;
        break;
    case HSD_A_J_TRAY:
        joint->translate.y = value;
        break;
    case HSD_A_J_TRAZ:
        joint->translate.z = value;
        break;
    case HSD_A_J_SCAX:
        if (fabsf_bitwise(value) < 1e-3F) {
            value = 1e-3F;
        }
        joint->scale.x = value;
        break;
    case HSD_A_J_SCAY:
        if (fabsf_bitwise(value) < 1e-3F) {
            value = 1e-3F;
        }
        joint->scale.y = value;
        break;
    case HSD_A_J_SCAZ:
        if (fabsf_bitwise(value) < 1e-3F) {
            value = 1e-3F;
        }
        joint->scale.z = value;
        break;
    default:
        HSD_JObjUpdateAnimValue(joint, obj_type, &data, node->path);
        return;
    }
    if (!(joint->flags & JOBJ_MTX_INDEP_SRT)) {
        joint->flags |= JOBJ_MTX_DIRTY;
    }
}

static void publish_value(MslFighterPoseJoint* node,
                          MslFighterPoseTrack* track, float value)
{
    publish_value_type(node, track->obj_type, value);
}

static bool evaluate_track_value(MslFighterPoseTrack* track, float* value)
{
    switch (track->op_intrp) {
    case HSD_A_OP_KEY:
        if (!(track->flags & 0x80)) {
            return false;
        }
        *value = track->p0;
        track->flags &= (uint8_t) ~0x80;
        break;
    case HSD_A_OP_CON:
        *value = track->time >= track->fterm ? track->p1 : track->p0;
        break;
    case HSD_A_OP_LIN:
        if (track->flags & 0x20) {
            track->flags &= (uint8_t) ~0x20;
            if (track->fterm != 0) {
                track->d0 = (track->p1 - track->p0) / track->fterm;
            } else {
                track->d0 = 0.0F;
                track->p0 = track->p1;
            }
        }
        *value = __fmadds(track->d0, track->time, track->p0);
        break;
    case HSD_A_OP_SPL0:
    case HSD_A_OP_SPL:
    case HSD_A_OP_SLP:
        *value = track->fterm != 0
                     ? splGetHelmite(1.0 / track->fterm, track->time,
                                     track->p0, track->p1, track->d0,
                                     track->d1)
                     : track->p1;
        break;
    default:
        return false;
    }
    return true;
}

static bool update_track(MslFighterPoseJoint* node,
                         MslFighterPoseTrack* track, float* sampled_value)
{
    float value;
    if (!evaluate_track_value(track, &value)) {
        return false;
    }
    if (sampled_value != NULL) {
        *sampled_value = value;
    }
    if (node != NULL) {
        publish_value(node, track, value);
    }
    return true;
}

static bool interpret_track(MslFighterPoseJoint* node,
                            MslFighterPoseTrack* track, float rate,
                            bool publish, float* sampled_value)
{
    float terminal = 0.0F;
    uint32_t state = track_state(track);
    bool sampled = false;
    if (state == 0 || (track->time += rate, track->time < 0.0F)) {
        return false;
    }
    for (;;) {
        switch (state) {
        case 6:
            track->time += terminal;
            launch_key(track);
            if (publish) {
                sampled |= update_track(node, track, sampled_value);
            }
            return sampled;
        case FOBJ_LOAD_DATA0:
        case FOBJ_LOAD_DATA:
            state = load_data(track);
            break;
        case FOBJ_LOAD_WAIT:
            if ((track->flags & 0x80) && publish) {
                sampled |= update_track(node, track, sampled_value);
            }
            state = load_wait(track);
            break;
        case 4:
            if (track->fterm <= track->time) {
                state = FOBJ_LOAD_WAIT;
                terminal = track->fterm;
                track->time -= track->fterm;
                set_track_state(track, state);
                break;
            }
            if (publish) {
                sampled |= update_track(node, track, sampled_value);
            }
            set_track_state(track, 5);
            return sampled;
        case 5:
            state = 4;
            set_track_state(track, state);
            break;
        default:
            return sampled;
        }
    }
}

#ifdef MSL_CORE_NATIVE
typedef struct MslFigaCollect {
    FigaTree** trees;
    uint32_t count;
    uint32_t capacity;
} MslFigaCollect;

static void count_figa(FigaTree* tree, void* context)
{
    MslFigaCollect* collect = context;
    (void) tree;
    ++collect->count;
}

static void collect_figa(FigaTree* tree, void* context)
{
    MslFigaCollect* collect = context;
    HSD_ASSERT(439, collect->count < collect->capacity);
    collect->trees[collect->count++] = tree;
}

static int compare_figa_pointer(const void* lhs, const void* rhs)
{
    uintptr_t a = (uintptr_t) *(FigaTree* const*) lhs;
    uintptr_t b = (uintptr_t) *(FigaTree* const*) rhs;
    return a < b ? -1 : a > b;
}

static uint32_t figa_track_count(const FigaTree* tree)
{
    const s8* node = tree->nodes;
    uint32_t count = 0;
    while (*node != -1) {
        HSD_ASSERT(450, *node >= 0);
        count += (uint8_t) *node++;
    }
    return count;
}

static void validate_figa_node_ranges(const FigaTree* tree)
{
    const s8* node = tree->nodes;
    uint32_t track_start = 0;
    while (*node != -1) {
        uint32_t track_count;
        HSD_ASSERT(462, *node >= 0);
        track_count = (uint8_t) *node++;
        if (track_count != 0) {
            HSD_ASSERT(466, track_start <= 0x7FF);
            HSD_ASSERT(467, track_count <= 0x7F);
        }
        track_start += track_count;
    }
    HSD_ASSERT(471, track_start == figa_track_count(tree));
}

static void set_program_value(MslFighterPosePrograms* programs,
                              uint32_t index, float value)
{
    programs->values[index] = value;
    programs->valid[index >> 3] |= (uint8_t) (1U << (index & 7));
}
#endif

int msl_fighter_pose_programs_init(MslFighterPosePrograms* programs)
{
#ifdef MSL_CORE_NATIVE
    MslFigaCollect collect = { 0 };
    uint64_t value_count = 0;
    uint32_t unique_count;
    uint32_t i;

    memset(programs, 0, sizeof(*programs));
    msl_native_dat_for_each_figa(count_figa, &collect);
    collect.capacity = collect.count;
    collect.count = 0;
    collect.trees = malloc((size_t) collect.capacity * sizeof(*collect.trees));
    if (collect.trees == NULL) {
        return -1;
    }
    msl_native_dat_for_each_figa(collect_figa, &collect);
    qsort(collect.trees, collect.count, sizeof(*collect.trees),
          compare_figa_pointer);
    unique_count = 0;
    for (i = 0; i < collect.count; ++i) {
        if (unique_count == 0 ||
            collect.trees[i] != collect.trees[unique_count - 1])
        {
            collect.trees[unique_count++] = collect.trees[i];
        }
    }
    programs->programs = calloc(unique_count, sizeof(*programs->programs));
    if (programs->programs == NULL) {
        free(collect.trees);
        return -1;
    }
    programs->program_count = unique_count;
    for (i = 0; i < unique_count; ++i) {
        MslFighterPoseProgram* program = &programs->programs[i];
        uint32_t sample_count =
            collect.trees[i]->frames > 0.0F
                ? (uint32_t) ceilf(collect.trees[i]->frames) + 1
                : 1;
        program->tree = collect.trees[i];
        program->track_count = figa_track_count(program->tree);
        validate_figa_node_ranges(program->tree);
        HSD_ASSERT(495, sample_count <= UINT16_MAX);
        program->sample_count = (uint16_t) sample_count;
        program->value_start = (uint32_t) value_count;
        value_count += (uint64_t) program->track_count * sample_count;
        HSD_ASSERT(500, value_count <= UINT32_MAX);
    }
    free(collect.trees);
    programs->value_count = (uint32_t) value_count;
    programs->values = malloc((size_t) programs->value_count *
                              sizeof(*programs->values));
    programs->valid = calloc(((size_t) programs->value_count + 7) / 8, 1);
    if (programs->values == NULL || programs->valid == NULL) {
        msl_fighter_pose_programs_deinit(programs);
        return -1;
    }
    for (i = 0; i < programs->program_count; ++i) {
        MslFighterPoseProgram* program = &programs->programs[i];
        uint32_t track_index;
        for (track_index = 0; track_index < program->track_count;
             ++track_index)
        {
            MslFighterPoseTrack track = { 0 };
            uint32_t sample;
            init_track(&track, &program->tree->tracks[track_index]);
            request_track(&track, 0.0F);
            for (sample = 0; sample < program->sample_count; ++sample) {
                float value;
                if (interpret_track(NULL, &track,
                                    sample == 0 ? 0.0F : 1.0F, true,
                                    &value))
                {
                    uint32_t value_index =
                        program->value_start +
                        sample * program->track_count + track_index;
                    set_program_value(programs, value_index, value);
                }
            }
        }
    }
    return 0;
#else
    memset(programs, 0, sizeof(*programs));
    return 0;
#endif
}

void msl_fighter_pose_programs_deinit(MslFighterPosePrograms* programs)
{
#ifdef MSL_CORE_NATIVE
    free(programs->valid);
    free(programs->values);
    free(programs->programs);
#endif
    memset(programs, 0, sizeof(*programs));
}

static void stop_tracks(MslFighterPoseJoint* node, float rate, bool publish)
{
    MslFighterPose* pose = active_pose();
    uint16_t i;
    for (i = 0; i < node->track_count; ++i) {
        MslFighterPoseTrack* track =
            &pose->tracks[node->track_start + i];
        if (track->op_intrp == HSD_A_OP_KEY) {
            interpret_track(node, track, rate, publish, NULL);
        }
        set_track_state(track, 0);
    }
}

#ifdef MSL_CORE_NATIVE
static bool program_value(const MslFighterPosePrograms* programs,
                          const MslFighterPoseProgram* program,
                          uint32_t sample, uint32_t track, float* value)
{
    uint32_t index;
    if (sample >= program->sample_count || track >= program->track_count) {
        return false;
    }
    index = program->value_start + sample * program->track_count + track;
    if (!(programs->valid[index >> 3] &
          (uint8_t) (1U << (index & 7))))
    {
        return false;
    }
    *value = programs->values[index];
    return true;
}

static void publish_program_sample(MslFighterPoseJoint* node,
                                   uint32_t sample)
{
    const MslFighterPosePrograms* programs = active_programs();
    const MslFighterPoseProgram* program =
        &programs->programs[node->program_index];
    uint32_t i;

    for (i = 0; i < node->program_track_count; ++i) {
        uint32_t track_index = node->program_track_start + i;
        const FigaTrack* track = &program->tree->tracks[track_index];
        float value;
        if (track->obj_type == TYPE_JOBJ &&
            (!node->program_filtered ||
             (track->obj_type != 5 &&
              (uint8_t) (track->obj_type - 6) > 1)) &&
            program_value(programs, program, sample, track_index, &value))
        {
            publish_value_type(node, track->obj_type, value);
            break;
        }
    }
    for (i = 0; i < node->program_track_count; ++i) {
        uint32_t track_index = node->program_track_start + i;
        const FigaTrack* track = &program->tree->tracks[track_index];
        float value;
        if ((node->program_filtered &&
             (track->obj_type == 5 ||
              (uint8_t) (track->obj_type - 6) <= 1)) ||
            track->obj_type == TYPE_JOBJ)
        {
            continue;
        }
        if (program_value(programs, program, sample, track_index, &value)) {
            publish_value_type(node, track->obj_type, value);
        }
    }
}

#endif

static void interpret_joint(MslFighterPoseJoint* node)
{
    MslFighterPose* pose = active_pose();
    float rate;
    uint16_t i;
    bool publish;
    bool used_table = false;
    if (node->flags & AOBJ_NO_ANIM) {
        return;
    }
    if (node->flags & AOBJ_FIRST_PLAY) {
        node->flags &= ~AOBJ_FIRST_PLAY;
        rate = 0.0F;
    } else {
        rate = node->framerate;
        node->curr_frame += node->framerate;
    }
    if ((node->flags & AOBJ_LOOP) && node->end_frame <= node->curr_frame) {
        if (node->rewind_frame < node->end_frame) {
            float period = node->end_frame - node->rewind_frame;
            float value = node->curr_frame - node->rewind_frame;
            if (node->decoder_synced) {
                stop_tracks(node, rate, true);
            }
            node->curr_frame = HSD_FMod(value, period) + node->rewind_frame;
            if (is_figa(node)) {
                node->decoder_synced = 0;
            } else {
                for (i = 0; i < node->track_count; ++i) {
                    request_track(&pose->tracks[node->track_start + i],
                                  node->curr_frame);
                }
            }
        } else {
            node->curr_frame = node->end_frame;
        }
        rate = 0.0F;
        node->flags |= AOBJ_REWINDED;
    } else {
        node->flags &= ~AOBJ_REWINDED;
    }
    publish = !(node->flags & AOBJ_NO_UPDATE);
#ifdef MSL_CORE_NATIVE
    if (is_figa(node) && node->framerate == 1.0F &&
        node->curr_frame >= 0.0F &&
        node->curr_frame == truncf(node->curr_frame) &&
        (uint32_t) node->curr_frame <
            active_programs()->programs[node->program_index].sample_count)
    {
        if (publish) {
            publish_program_sample(node, (uint32_t) node->curr_frame);
        }
        node->decoder_synced = 0;
        used_table = true;
    }
#endif
    if (!used_table) {
        if (is_figa(node) && !node->decoder_synced) {
            for (i = 0; i < node->track_count; ++i) {
                request_track(&pose->tracks[node->track_start + i],
                              node->curr_frame);
            }
            rate = 0.0F;
            node->decoder_synced = 1;
        }
        for (i = 0; i < node->track_count; ++i) {
            interpret_track(node, &pose->tracks[node->track_start + i], rate,
                            publish, NULL);
        }
    }
    if (!(node->flags & AOBJ_LOOP) && node->end_frame <= node->curr_frame) {
        if (node->decoder_synced) {
            stop_tracks(node, node->framerate, publish);
        }
        node->flags |= AOBJ_NO_ANIM;
    }
    if (node->flags & AOBJ_NO_ANIM) {
        ++msl_core_aobj_context()->ended_count;
    } else {
        ++msl_core_aobj_context()->active_count;
    }
}

void msl_fighter_pose_animate_joint(HSD_JObj* joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    if (node == NULL) {
        return;
    }
    HSD_JObjCheckDepend(joint);
    interpret_joint(node);
    if (joint->robj != NULL) {
        HSD_RObjAnimAll(joint->robj);
    }
}

void msl_fighter_pose_animate_tree(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return;
    }
    HSD_AObjInitEndCallBack();
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        msl_fighter_pose_animate_joint(pose->joints[i].joint);
    }
    HSD_AObjInvokeCallBacks();
}

bool msl_fighter_pose_is_animating(HSD_JObj* joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    return node != NULL && is_attached(node) &&
           !(node->flags & AOBJ_NO_ANIM);
}

bool msl_fighter_pose_tree_is_animating(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return false;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i]) &&
            !(pose->joints[i].flags & AOBJ_NO_ANIM))
        {
            return true;
        }
    }
    return false;
}

bool msl_fighter_pose_tree_rewound(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return false;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i]) &&
            (pose->joints[i].flags & AOBJ_REWINDED))
        {
            return true;
        }
    }
    return false;
}

static MslFighterPoseJoint* first_animated(HSD_JObj* root)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;
    if (root_node == NULL) {
        return NULL;
    }
    i = (uint16_t) (root_node - pose->joints);
    end = i + root_node->tree_count;
    for (; i < end; ++i) {
        if (is_attached(&pose->joints[i])) {
            return &pose->joints[i];
        }
    }
    return NULL;
}

float msl_fighter_pose_tree_rate(HSD_JObj* root)
{
    MslFighterPoseJoint* node = first_animated(root);
    return node != NULL ? node->framerate : 0.0F;
}

float msl_fighter_pose_tree_frame(HSD_JObj* root)
{
    MslFighterPoseJoint* node = first_animated(root);
    return node != NULL ? node->curr_frame : 0.0F;
}

float msl_fighter_pose_tree_end(HSD_JObj* root)
{
    MslFighterPoseJoint* node = first_animated(root);
    return node != NULL ? node->end_frame : 0.0F;
}
