#include "runtime/fighter_pose.h"

#include "platform/memory.h"
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#endif
#include "runtime/context.h"

#include <math.h>
#include <stdio.h>
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
#include <melee/ft/types.h>
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
    return node->program_is_figa;
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
    uintptr_t target = (uintptr_t) tree;
    uint32_t slot =
        (uint32_t) ((target >> 3) * UINT32_C(2654435761)) &
        programs->program_hash_mask;
    for (;;) {
        uint16_t index = programs->program_hash[slot];
        HSD_ASSERT(51, index != UINT16_MAX);
        if (programs->programs[index].tree == tree) {
            return index;
        }
        slot = (slot + 1) & programs->program_hash_mask;
    }
}
#else
static uint16_t find_program(const FigaTree* tree)
{
    (void) tree;
    return MSL_FIGHTER_POSE_PROGRAM_NONE;
}
#endif

#ifdef MSL_CORE_NATIVE
uint16_t msl_fighter_pose_program_token(const FigaTree* tree)
{
    return (uint16_t) (find_program(tree) + 1);
}

FigaTree* msl_fighter_pose_program_tree(uint16_t token)
{
    const MslFighterPosePrograms* programs = active_programs();
    HSD_ASSERT(72, token != 0 && token <= programs->program_count);
    return programs->programs[token - 1].tree;
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

int msl_fighter_pose_init(MslFighterPose* pose, uint8_t player_count)
{
#ifdef MSL_CORE_NATIVE
    pose->joint_capacity =
        MSL_FIGHTER_POSE_JOINTS_PER_PLAYER * player_count;
    pose->joints = HSD_MemAllocReloc(
        sizeof(*pose->joints) * pose->joint_capacity,
        MSL_RELOC_FIGHTER_POSE_JOINT, pose->joint_capacity,
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
    (void) player_count;
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
    HSD_ASSERT(71, pose->joint_count < pose->joint_capacity);
    node = &pose->joints[pose->joint_count++];
    memset(node, 0, sizeof(*node));
    node->magic = MSL_FIGHTER_POSE_MAGIC;
    node->flags = AOBJ_NO_ANIM;
    node->joint = joint;
    node->framerate = 1.0F;
    node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
    node->source_part_index = MSL_FIGHTER_POSE_PART_NONE;
    node->program_node_index = MSL_FIGHTER_POSE_PROGRAM_NODE_NONE;
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

void msl_fighter_pose_bind_part(HSD_JObj* joint, uint8_t part)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    HSD_ASSERT(145, node != NULL);
    HSD_ASSERT(146, part < MSL_FIGHTER_POSE_PART_NONE);
    HSD_ASSERT(147, node->source_part_index == MSL_FIGHTER_POSE_PART_NONE ||
                        node->source_part_index == part);
    node->source_part_index = part;
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
    node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
    node->program_node_index = MSL_FIGHTER_POSE_PROGRAM_NODE_NONE;
    node->program_is_figa = 0;
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
    HSD_ASSERT(162, count <= UINT8_MAX);
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

#ifdef MSL_CORE_NATIVE
static void materialize_figa_tracks(MslFighterPoseJoint* node)
{
    const MslFighterPosePrograms* programs = active_programs();
    const MslFighterPoseProgramNode* program_node =
        &programs->nodes[node->program_node_index];
    const MslFighterPoseProgram* program =
        &programs->programs[program_node->program_index];
    const FigaTrack* tracks =
        &program->tree->tracks[program_node->track_start];
    MslFighterPoseTrack* destination;
    uint16_t retained = 0;
    uint16_t i;

    HSD_ASSERT(350, is_figa(node));
    HSD_ASSERT(351, node->track_count == 0);
    for (i = 0; i < program_node->track_count; ++i) {
        if (!node->program_filtered ||
            (tracks[i].obj_type != 5 &&
             (uint8_t) (tracks[i].obj_type - 6) > 1))
        {
            ++retained;
        }
    }
    destination = allocate_tracks(node, retained);
    for (i = 0; i < program_node->track_count; ++i) {
        if ((!node->program_filtered ||
             (tracks[i].obj_type != 5 &&
              (uint8_t) (tracks[i].obj_type - 6) > 1)) &&
            tracks[i].obj_type == TYPE_JOBJ)
        {
            init_track(destination++, &tracks[i]);
            break;
        }
    }
    for (i = 0; i < program_node->track_count; ++i) {
        if ((node->program_filtered &&
             (tracks[i].obj_type == 5 ||
              (uint8_t) (tracks[i].obj_type - 6) <= 1)) ||
            tracks[i].obj_type == TYPE_JOBJ)
        {
            continue;
        }
        init_track(destination++, &tracks[i]);
    }
}
#endif

void msl_fighter_pose_attach_figa(HSD_JObj* joint, FigaTree* tree,
                                  FigaTrack* tracks, int track_count,
                                  bool filtered)
{
#ifdef MSL_CORE_NATIVE
    const MslFighterPosePrograms* programs = active_programs();
    const MslFighterPoseProgram* program;
    const MslFighterPoseProgramNode* program_node;
    uint16_t program_index;
#endif
    MslFighterPoseJoint* node = pose_joint(joint);
#ifndef MSL_CORE_NATIVE
    MslFighterPoseTrack* destination;
    int retained = 0;
    int i;
#endif
    if (node == NULL || track_count == 0) {
        return;
    }
    release_joint(node);
    node->program_is_figa = 1;
#ifdef MSL_CORE_NATIVE
    program_index = find_program(tree);
    program = &programs->programs[program_index];
    HSD_ASSERT(355, tracks >= tree->tracks);
    HSD_ASSERT(356, track_count > 0 && track_count <= UINT8_MAX);
    {
        uint32_t track_start = (uint32_t) (tracks - tree->tracks);
        uint16_t node_index;
        HSD_ASSERT(357, track_start < program->track_count);
        node_index = programs->track_nodes[program->track_map_start +
                                           track_start];
        program_node = &programs->nodes[program->node_start + node_index];
        HSD_ASSERT(357, node_index < program->node_count &&
                            program->node_start + node_index <
                                MSL_FIGHTER_POSE_PROGRAM_NODE_NONE &&
                            program_node->track_start == track_start &&
                            program_node->track_count == track_count);
        node->program_node_index = program->node_start + node_index;
    }
#endif
    node->program_filtered = filtered;
#ifndef MSL_CORE_NATIVE
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
#endif
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
            // Hosted obj_id carries the source-width HSD identity token for
            // this immutable native DAT joint.
            object = (HSD_Obj*) HSD_JObjLoadJoint(
                msl_hsd_native_dat_pointer_from_id(desc->obj_id));
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
    node->decoder_synced = !is_figa(node) || node->track_count != 0;
    node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
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

static uint32_t figa_node_count(const FigaTree* tree)
{
    const s8* node = tree->nodes;
    uint32_t count = 0;
    while (*node != -1) {
        HSD_ASSERT(450, *node >= 0);
        ++count;
        ++node;
    }
    return count;
}

static uint32_t figa_track_count(const FigaTree* tree)
{
    const s8* node = tree->nodes;
    uint32_t count = 0;
    while (*node != -1) {
        HSD_ASSERT(451, *node >= 0);
        count += (uint8_t) *node++;
    }
    return count;
}

static bool direct_srt_type(uint8_t type)
{
    return type >= HSD_A_J_ROTX && type <= HSD_A_J_SCAZ &&
           type != HSD_A_J_PATH;
}

static uint32_t mask_count(uint16_t mask)
{
    return (uint32_t) __builtin_popcount((unsigned int) mask);
}
#endif

int msl_fighter_pose_programs_init(MslFighterPosePrograms* programs)
{
#ifdef MSL_CORE_NATIVE
    MslFigaCollect collect = { 0 };
    uint64_t value_count = 0;
    uint64_t track_node_count = 0;
    uint32_t hash_capacity = 1;
    uint32_t node_count = 0;
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
    HSD_ASSERT(491, unique_count < MSL_FIGHTER_POSE_PROGRAM_NONE);
    programs->programs = calloc(unique_count, sizeof(*programs->programs));
    while (hash_capacity < unique_count * 2) {
        hash_capacity <<= 1;
    }
    programs->program_hash = malloc(
        (size_t) hash_capacity * sizeof(*programs->program_hash));
    for (i = 0; i < unique_count; ++i) {
        node_count += figa_node_count(collect.trees[i]);
        track_node_count += figa_track_count(collect.trees[i]);
    }
    programs->nodes = calloc(node_count, sizeof(*programs->nodes));
    HSD_ASSERT(492, track_node_count <= UINT32_MAX);
    programs->track_nodes = calloc((size_t) track_node_count,
                                   sizeof(*programs->track_nodes));
    if (programs->programs == NULL || programs->program_hash == NULL ||
        programs->nodes == NULL || programs->track_nodes == NULL)
    {
        free(collect.trees);
        msl_fighter_pose_programs_deinit(programs);
        return -1;
    }
    programs->program_count = unique_count;
    programs->program_hash_mask = hash_capacity - 1;
    memset(programs->program_hash, 0xFF,
           (size_t) hash_capacity * sizeof(*programs->program_hash));
    programs->node_count = node_count;
    programs->track_node_count = (uint32_t) track_node_count;
    node_count = 0;
    track_node_count = 0;
    for (i = 0; i < unique_count; ++i) {
        MslFighterPoseProgram* program = &programs->programs[i];
        const s8* source_node = collect.trees[i]->nodes;
        uint32_t track_start = 0;
        uint16_t node_index;
        uint32_t sample_count =
            collect.trees[i]->frames > 0.0F
                ? (uint32_t) ceilf(collect.trees[i]->frames) + 1
                : 1;
        program->tree = collect.trees[i];
        program->node_start = node_count;
        program->track_map_start = (uint32_t) track_node_count;
        program->track_count = figa_track_count(program->tree);
        program->value_start = (uint32_t) value_count;
        program->frame_value_count = 0;
        track_node_count += program->track_count;
        HSD_ASSERT(495, figa_node_count(program->tree) <= UINT16_MAX);
        program->node_count = (uint16_t) figa_node_count(program->tree);
        HSD_ASSERT(495, sample_count <= UINT16_MAX);
        program->sample_count = (uint16_t) sample_count;
        for (node_index = 0; node_index < program->node_count; ++node_index) {
            MslFighterPoseProgramNode* node = &programs->nodes[node_count++];
            uint32_t track_count;
            uint32_t track;
            HSD_ASSERT(500, *source_node >= 0);
            track_count = (uint8_t) *source_node++;
            HSD_ASSERT(501, track_start <= UINT16_MAX);
            HSD_ASSERT(502, track_count <= UINT8_MAX);
            node->track_start = (uint16_t) track_start;
            node->track_count = (uint8_t) track_count;
            node->program_index = (uint16_t) i;
            node->sample_count = program->sample_count;
            // Only a tracked node owns a track_nodes map slot: request_figa
            // looks nodes up by their first track index and returns early for
            // trackless nodes. A trailing zero-track node (first hit by
            // Ganondorf's animation bank) would otherwise write one entry
            // past the final program's map region.
            if (track_count != 0) {
                programs->track_nodes[program->track_map_start + track_start] =
                    node_index;
            }
            node->direct = track_count != 0;
            for (track = 0; track < track_count; ++track) {
                uint8_t type =
                    program->tree->tracks[track_start + track].obj_type;
                if (!direct_srt_type(type)) {
                    node->direct = 0;
                } else if (node->type_mask & (uint16_t) (1U << type)) {
                    node->direct = 0;
                }
                if (type < 16) {
                    node->type_mask |= (uint16_t) (1U << type);
                }
            }
            if (node->direct) {
                node->value_count = (uint8_t) mask_count(node->type_mask);
                node->frame_value_offset = program->frame_value_count;
                program->frame_value_count += node->value_count;
            }
            track_start += track_count;
        }
        value_count +=
            (uint64_t) program->frame_value_count * program->sample_count;
        HSD_ASSERT(503, value_count <= UINT32_MAX);
        HSD_ASSERT(505, *source_node == -1);
        HSD_ASSERT(506, track_start == program->track_count);
    }
    free(collect.trees);
    programs->value_count = (uint32_t) value_count;
    programs->values = malloc((size_t) programs->value_count *
                              sizeof(*programs->values));
    if (programs->values == NULL) {
        msl_fighter_pose_programs_deinit(programs);
        return -1;
    }
    for (i = 0; i < programs->program_count; ++i) {
        MslFighterPoseProgram* program = &programs->programs[i];
        uint16_t node_index;
        for (node_index = 0; node_index < program->node_count; ++node_index) {
            MslFighterPoseProgramNode* node =
                &programs->nodes[program->node_start + node_index];
            uint32_t local_track;
            if (!node->direct) {
                continue;
            }
            for (local_track = 0; local_track < node->track_count;
                 ++local_track)
            {
                const FigaTrack* source =
                    &program->tree->tracks[node->track_start + local_track];
                MslFighterPoseTrack track = { 0 };
                uint16_t lower_mask =
                    (uint16_t) (node->type_mask &
                                ((1U << source->obj_type) - 1));
                uint32_t slot = mask_count(lower_mask);
                uint32_t sample;
                init_track(&track, source);
                request_track(&track, 0.0F);
                for (sample = 0; sample < program->sample_count; ++sample) {
                    float value;
                    if (interpret_track(NULL, &track,
                                        sample == 0 ? 0.0F : 1.0F, true,
                                        &value))
                    {
                        uint32_t value_index =
                            program->value_start +
                            sample * program->frame_value_count +
                            node->frame_value_offset + slot;
                        programs->values[value_index] = value;
                    } else {
                        node->direct = 0;
                    }
                }
            }
        }
    }
    for (i = 0; i < programs->program_count; ++i) {
        uintptr_t tree = (uintptr_t) programs->programs[i].tree;
        uint32_t slot =
            (uint32_t) ((tree >> 3) * UINT32_C(2654435761)) &
            programs->program_hash_mask;
        while (programs->program_hash[slot] != UINT16_MAX)
        {
            slot = (slot + 1) & programs->program_hash_mask;
        }
        programs->program_hash[slot] = (uint16_t) i;
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
    free(programs->program_hash);
    free(programs->track_nodes);
    free(programs->values);
    free(programs->nodes);
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
static bool publish_program_sample(MslFighterPoseJoint* node,
                                   uint32_t sample, bool publish)
{
    const MslFighterPosePrograms* programs = active_programs();
    const MslFighterPoseProgramNode* program_node =
        &programs->nodes[node->program_node_index];
    HSD_JObj* joint = node->joint;
    const float* values;
    uint16_t mask;
    uint16_t type_mask;
    bool wrote = false;

    if (!program_node->direct || sample >= program_node->sample_count) {
        return false;
    }
    if (!publish) {
        return true;
    }
    mask = program_node->type_mask;
    type_mask = program_node->type_mask;
    if (node->program_filtered) {
        mask &= (uint16_t) ~((1U << HSD_A_J_TRAX) |
                            (1U << HSD_A_J_TRAY) |
                            (1U << HSD_A_J_TRAZ));
    }
    {
        const MslFighterPoseProgram* program =
            &programs->programs[program_node->program_index];
        values = &programs->values[program->value_start +
                                   sample * program->frame_value_count +
                                   program_node->frame_value_offset];
    }
#define PUBLISH_ROTX(value)                                                   \
    do {                                                                     \
        if (joint->flags & JOBJ_JOINT1) {                                    \
            HSD_RObj* robj =                                                 \
                HSD_RObjGetByType(joint->robj, REFTYPE_IKHINT, 0);           \
            if (robj != NULL) {                                              \
                robj->u.ik_hint.rotate_x = (value);                           \
            }                                                                \
        }                                                                    \
        joint->rotate.x = (value);                                           \
    } while (0)
    switch (type_mask) {
    case 0x00E:
        PUBLISH_ROTX(values[0]);
        joint->rotate.y = values[1];
        joint->rotate.z = values[2];
        wrote = true;
        goto published;
    case 0x004:
        joint->rotate.y = values[0];
        wrote = true;
        goto published;
    case 0x00C:
        joint->rotate.y = values[0];
        joint->rotate.z = values[1];
        wrote = true;
        goto published;
    case 0x002:
        PUBLISH_ROTX(values[0]);
        wrote = true;
        goto published;
    case 0x0EE:
        PUBLISH_ROTX(values[0]);
        joint->rotate.y = values[1];
        joint->rotate.z = values[2];
        if (!node->program_filtered) {
            joint->translate.x = values[3];
            joint->translate.y = values[4];
            joint->translate.z = values[5];
        }
        wrote = true;
        goto published;
    case 0x40E:
        PUBLISH_ROTX(values[0]);
        joint->rotate.y = values[1];
        joint->rotate.z = values[2];
        joint->scale.z =
            fabsf_bitwise(values[3]) < 1e-3F ? 1e-3F : values[3];
        wrote = true;
        goto published;
    case 0x006:
        PUBLISH_ROTX(values[0]);
        joint->rotate.y = values[1];
        wrote = true;
        goto published;
    case 0x008:
        joint->rotate.z = values[0];
        wrote = true;
        goto published;
    case 0x0E0:
        if (!node->program_filtered) {
            joint->translate.x = values[0];
            joint->translate.y = values[1];
            joint->translate.z = values[2];
            wrote = true;
        }
        goto published;
    case 0x00A:
        PUBLISH_ROTX(values[0]);
        joint->rotate.z = values[1];
        wrote = true;
        goto published;
    }
#define PUBLISH_SRT(type, target)                                             \
    if (type_mask & (1U << (type))) {                                        \
        float value = *values++;                                             \
        if (mask & (1U << (type))) {                                         \
            target = value;                                                  \
            wrote = true;                                                    \
        }                                                                    \
    }
    if (type_mask & (1U << HSD_A_J_ROTX)) {
        float value = *values++;
        if (mask & (1U << HSD_A_J_ROTX)) {
            PUBLISH_ROTX(value);
            wrote = true;
        }
    }
    PUBLISH_SRT(HSD_A_J_ROTY, joint->rotate.y);
    PUBLISH_SRT(HSD_A_J_ROTZ, joint->rotate.z);
    PUBLISH_SRT(HSD_A_J_TRAX, joint->translate.x);
    PUBLISH_SRT(HSD_A_J_TRAY, joint->translate.y);
    PUBLISH_SRT(HSD_A_J_TRAZ, joint->translate.z);
    if (type_mask & (1U << HSD_A_J_SCAX)) {
        float value = *values++;
        if (mask & (1U << HSD_A_J_SCAX)) {
            joint->scale.x = fabsf_bitwise(value) < 1e-3F ? 1e-3F : value;
            wrote = true;
        }
    }
    if (type_mask & (1U << HSD_A_J_SCAY)) {
        float value = *values++;
        if (mask & (1U << HSD_A_J_SCAY)) {
            joint->scale.y = fabsf_bitwise(value) < 1e-3F ? 1e-3F : value;
            wrote = true;
        }
    }
    if (type_mask & (1U << HSD_A_J_SCAZ)) {
        float value = *values++;
        if (mask & (1U << HSD_A_J_SCAZ)) {
            joint->scale.z = fabsf_bitwise(value) < 1e-3F ? 1e-3F : value;
            wrote = true;
        }
    }
#undef PUBLISH_SRT
published:
#undef PUBLISH_ROTX
    if (wrote && !(joint->flags & JOBJ_MTX_INDEP_SRT)) {
        joint->flags |= JOBJ_MTX_DIRTY;
    }
    return true;
}

#endif

static void interpret_joint(MslFighterPoseJoint* node)
{
    MslFighterPose* pose = active_pose();
    float rate;
    uint16_t i;
    bool publish;
    bool used_table = false;
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
                node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
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
        node->curr_frame == truncf(node->curr_frame))
    {
        used_table = publish_program_sample(
            node, (uint32_t) node->curr_frame, publish);
        if (used_table) {
            node->decoder_synced = 0;
            node->last_table_frame =
                (uint32_t) node->curr_frame <
                        MSL_FIGHTER_POSE_TABLE_FRAME_NONE
                    ? (uint32_t) node->curr_frame
                    : MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
        }
    }
#endif
    if (!used_table) {
        if (is_figa(node) && !node->decoder_synced) {
            if (node->track_count == 0) {
                materialize_figa_tracks(node);
            }
            if (node->last_table_frame !=
                MSL_FIGHTER_POSE_TABLE_FRAME_NONE)
            {
                // The table sampled this node at an exact integer frame.
                // Rebuild the decoder there (integer wait subtraction is
                // exact) and dry-walk without publishing; the ordinary
                // rate step below then reproduces the source engine's
                // incremental `time += rate` rounding bit-exactly instead
                // of re-deriving the time from a fractional absolute
                // frame.
                float base = (float) node->last_table_frame;
                for (i = 0; i < node->track_count; ++i) {
                    request_track(&pose->tracks[node->track_start + i],
                                  base);
                }
                for (i = 0; i < node->track_count; ++i) {
                    interpret_track(node,
                                    &pose->tracks[node->track_start + i],
                                    0.0F, false, NULL);
                }
            } else {
                for (i = 0; i < node->track_count; ++i) {
                    request_track(&pose->tracks[node->track_start + i],
                                  node->curr_frame);
                }
                rate = 0.0F;
            }
            node->decoder_synced = 1;
            node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
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

static void animate_node(MslFighterPoseJoint* node)
{
    HSD_JObj* joint = node->joint;
    HSD_JObjCheckDepend(joint);
    if (!(node->flags & AOBJ_NO_ANIM)) {
        interpret_joint(node);
    }
    if (joint->robj != NULL) {
        HSD_RObjAnimAll(joint->robj);
    }
}

void msl_fighter_pose_animate_joint(HSD_JObj* joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    if (node != NULL) {
        animate_node(node);
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
        animate_node(&pose->joints[i]);
    }
    HSD_AObjInvokeCallBacks();
}

void msl_fighter_pose_animate_parts(HSD_JObj* root,
                                    const FighterBone* parts)
{
    MslFighterPose* pose = active_pose();
    MslFighterPoseJoint* root_node = pose_joint(root);
    uint16_t end;
    uint16_t i;

    HSD_AObjInitEndCallBack();
    if (root_node != NULL) {
        i = (uint16_t) (root_node - pose->joints);
        end = i + root_node->tree_count;
        for (; i < end; ++i) {
            MslFighterPoseJoint* node = &pose->joints[i];
            uint8_t part = node->source_part_index;
            HSD_ASSERT(1588, part != MSL_FIGHTER_POSE_PART_NONE);
            if (!parts[part].flags_b0 && !parts[part].flags_b5) {
                animate_node(node);
            }
        }
    }
    HSD_AObjInvokeCallBacks();
}

bool msl_fighter_pose_is_animating(HSD_JObj* joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    return node != NULL && is_attached(node) &&
           !(node->flags & AOBJ_NO_ANIM);
}

bool msl_fighter_pose_has_animation(const HSD_JObj* joint)
{
    MslFighterPoseJoint* node = pose_joint(joint);
    return node != NULL && is_attached(node);
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
