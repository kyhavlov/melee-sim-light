#include "smoke_context.h"

#include "lb/lbarchive.h"
#include "lb/lb_00B0.h"
#include "platform/files.h"

#include <stdio.h>
#include <string.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/mobj.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>
#include <MetroTRK/intrinsics.h>
#include <MSL/trigf.h>

static int count_joints(HSD_JObj* jobj)
{
    int count = 0;
    for (; jobj != NULL; jobj = jobj->next) {
        count += 1;
        if (!(jobj->flags & JOBJ_INSTANCE)) {
            count += count_joints(jobj->child);
        }
    }
    return count;
}

#ifdef MSL_CORE_NATIVE
static int msl_test_sample_publication(MslFighterPose* pose)
{
    static const u32 special[] = {
        0, 0x80000000, 1, 0x80000001, 0x3A83126E, 0x3A83126F,
        0x3A831270, 0xBA83126F, 0x7F800000, 0xFF800000,
        0x7FC12345, 0xFFA12345, 0x3F800000, 0xBF800000,
    };
    const MslFighterPosePrograms* saved = msl_context_fighter_pose_programs;
    MslFighterPoseProgramNode program_node = { 0 };
    MslFighterPosePrograms programs = { 0 };
    HSD_JObj joint = { 0 };
    MslFighterPoseJoint* node;
    static const unsigned channels[] = { 1, 2, 3, 8, 9, 10, 5, 6, 7 };
    static const unsigned fields[] = { 0, 1, 2, 4, 5, 6, 7, 8, 9 };
    float values[32] = { 0 };
    float source_values[11];
    u32 msl_test_seed = 0x6129AEFB;
    uint16_t base = pose->joint_count;
    unsigned subset, mode, trial;

    for (unsigned i = 0; i < saved->node_count; ++i) {
        const MslFighterPoseProgramNode* stored = &saved->nodes[i];
        if ((stored->value_count == 0) != (stored->sample_count == 0)) {
            fprintf(stderr, "compiled sample count mismatch node %u\n", i);
            return -1;
        }
    }
    programs.nodes = &program_node;
    programs.values = values;
    program_node.frame_value_offset = 2;
    program_node.sample_count = 1;
    msl_context_fighter_pose_programs = &programs;
    msl_fighter_pose_register_tree(&joint);
    node = &pose->joints[base];
    for (subset = 1; subset < 512; ++subset) {
        unsigned type_mask = ((subset & 7) << 1) | ((subset & 504) << 2);
        unsigned i;
        program_node.field_mask = 0;
        for (i = 0; i < 9; ++i) {
            if (type_mask & (1U << channels[i])) {
                program_node.field_mask |= 1U << fields[i];
            }
        }
        program_node.value_count = __builtin_popcount(type_mask);
        for (mode = 0; mode < 4; ++mode) {
            for (trial = 0; trial < 32; ++trial) {
                HSD_JObj expected;
                float* targets[11];
                unsigned type, value = 2, i;
                for (i = 0; i < 11; ++i) {
                    u32 bits;
                    msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
                    bits = trial < 14 ? special[(trial + i) % 14]
                                      : msl_test_seed;
                    memcpy(&source_values[i], &bits, sizeof(bits));
                }
                for (i = 0; i < 9; ++i) {
                    if (type_mask & (1U << channels[i])) {
                        values[value++] = source_values[channels[i]];
                    }
                }
                memset(&joint.rotate, 0xA5, sizeof(joint.rotate));
                memset(&joint.scale, 0x5A, sizeof(joint.scale));
                memset(&joint.translate, 0xC3, sizeof(joint.translate));
                joint.flags = trial & 1 ? JOBJ_MTX_DIRTY : JOBJ_HIDDEN;
                expected = joint;
                targets[1] = &expected.rotate.x;
                targets[2] = &expected.rotate.y;
                targets[3] = &expected.rotate.z;
                targets[5] = &expected.translate.x;
                targets[6] = &expected.translate.y;
                targets[7] = &expected.translate.z;
                targets[8] = &expected.scale.x;
                targets[9] = &expected.scale.y;
                targets[10] = &expected.scale.z;
                // Source channel semantics, independent of packed dispatch:
                // filtering suppresses translation; scale clamps magnitudes.
                for (type = 1; type <= 10; ++type) {
                    if (type_mask & (1U << type)) {
                        float source = source_values[type];
                        if ((mode & 2) ||
                            ((mode & 1) && type >= 5 && type <= 7))
                        {
                            continue;
                        }
                        *targets[type] = type >= 8 &&
                                             __builtin_fabsf(source) < 1e-3F
                                             ? 1e-3F : source;
                        expected.flags |= JOBJ_MTX_DIRTY;
                    }
                }
                node->flags = AOBJ_FIRST_PLAY |
                              (mode & 2 ? AOBJ_NO_UPDATE : 0);
                node->curr_frame = 0;
                node->framerate = 1;
                node->end_frame = 10;
                node->program_is_figa = 1;
                node->program_filtered = mode & 1;
                node->program_node_index = 0;
                node->decoder_synced = 0;
                msl_fighter_pose_animate_joint(&joint);
                if (memcmp(&joint, &expected, sizeof(joint)) != 0 ||
                    node->last_table_frame != 0 || node->decoder_synced)
                {
                    fprintf(stderr, "sample publication mask %x mode %u trial %u\n",
                            type_mask, mode, trial);
                    return -1;
                }
            }
        }
    }
    for (unsigned empty = 0; empty < 2; ++empty) {
        for (unsigned no_update = 0; no_update < 2; ++no_update) {
            HSD_JObj expected;
            program_node.sample_count = empty ? 0 : 1;
            program_node.value_count = empty ? 0 : 1;
            program_node.field_mask = 1;
            memset(&joint.rotate, 0x55, sizeof(joint.rotate));
            joint.flags = JOBJ_MTX_DIRTY;
            expected = joint;
            node->flags = AOBJ_FIRST_PLAY | (no_update ? AOBJ_NO_UPDATE : 0);
            node->program_is_figa = 1;
            node->decoder_synced = 1;
            node->last_table_frame = MSL_FIGHTER_POSE_TABLE_FRAME_NONE;
            node->curr_frame = 1.0F;
            node->end_frame = 10.0F;
            node->framerate = 1.0F;
            msl_fighter_pose_animate_joint(&joint);
            if (memcmp(&joint, &expected, sizeof(joint)) != 0 ||
                !node->decoder_synced ||
                node->last_table_frame != MSL_FIGHTER_POSE_TABLE_FRAME_NONE)
            {
                fprintf(stderr, "empty/out-of-range sample was admitted\n");
                return -1;
            }
        }
    }
    pose->joint_count = base;
    msl_context_fighter_pose_programs = saved;
    return 0;
}

static int msl_test_acos_seed(void)
{
    float (*volatile evaluate)(float) = acosf;
    u32 msl_test_seed = 0x37af219b;
    unsigned trial;

    for (trial = 0; trial < 131072; ++trial) {
        u32 bits;
        float x, radicand, guess, expected, actual;
        unsigned iteration;
        msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
        bits = trial & 1 ? 0x3F000000 | (msl_test_seed & 0x7FFFFF)
                         : msl_test_seed % 0x3F800000;
        bits |= msl_test_seed & 0x80000000;
        memcpy(&x, &bits, sizeof(x));
        // Original acosf owner: general Gekko estimate, then exactly three
        // rounded Newton steps and the unchanged source atan polynomial.
        radicand = __builtin_fmaf(-x, x, 1.0F);
        guess = (float) __frsqrte((double) radicand);
        for (iteration = 0; iteration < 3; ++iteration) {
            guess = (0.5F * guess) *
                    __builtin_fmaf(-(guess * guess), radicand, 3.0F);
        }
        expected = 1.5707963705062866F - atanf(x * guess);
        actual = evaluate(x);
        if (memcmp(&expected, &actual, sizeof(actual)) != 0) {
            fprintf(stderr, "acos seed mismatch for %08x\n", bits);
            return -1;
        }
    }
    if (evaluate(1.0F) != 0.0F || evaluate(-1.0F) != 3.1415927410125732F ||
        evaluate(0.0F) != 1.5707963705062866F ||
        evaluate(-0.0F) != 1.5707963705062866F)
    {
        return -1;
    }
    return 0;
}

static int msl_test_slerp_weights(void)
{
    static const float weights[] = { 0.0F, 1.0F, 0.5F, 0.25F, -0.5F, 1.5F };
    float (*volatile scalar)(float) = acosf;
    u32 msl_test_seed = 0x92731f8b;
    unsigned trial;

    for (trial = 0; trial < 2048; ++trial) {
        float input[65], first[65], second[65], inplace[65], inplace_second[65];
        float angles[195], sine[195], cosine[195];
        float weight = weights[trial % 6];
        unsigned count = trial % 65;
        unsigned i;
        for (i = 0; i < 65; ++i) {
            u32 bits;
            msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
            bits = trial & 1 ? 0x3F000000 | (msl_test_seed & 0x7FFFFF)
                             : msl_test_seed % 0x3F800000;
            bits |= msl_test_seed & 0x80000000;
            memcpy(&input[i], &bits, sizeof(bits));
            inplace[i] = input[i];
            first[i] = second[i] = inplace_second[i] = -1234.0F;
            angles[3 * i] = scalar(input[i]);
            angles[3 * i + 1] = (1.0F - weight) * angles[3 * i];
            angles[3 * i + 2] = weight * angles[3 * i];
        }
        msl_sincosf_many(angles, sine, cosine, (int) count * 3);
        msl_slerp_weights_many(input, weight, first, second, (int) count);
        msl_slerp_weights_many(inplace, weight, inplace, inplace_second, (int) count);
        for (i = 0; i < 65; ++i) {
            float sp = i < count ? sine[3 * i + 1] / sine[3 * i] : -1234.0F;
            float sq = i < count ? sine[3 * i + 2] / sine[3 * i] : -1234.0F;
            float inplace_expected = i < count ? sp : input[i];
            if (memcmp(&sp, &first[i], sizeof(sp)) != 0 ||
                memcmp(&sq, &second[i], sizeof(sq)) != 0 ||
                memcmp(&sq, &inplace_second[i], sizeof(sq)) != 0 ||
                memcmp(&inplace_expected, &inplace[i], sizeof(sp)) != 0)
            {
                fprintf(stderr, "slerp weights mismatch count %u lane %u\n",
                        count, i);
                return -1;
            }
        }
    }
    return 0;
}

static int msl_test_blend_publication(void)
{
    HSD_JObj first[65], actual[65], expected[65];
    HSD_JObj* first_ptr[65];
    HSD_JObj* actual_ptr[65];
    u32 msl_test_seed = 0x98645acb;
    unsigned trial;
    for (trial = 0; trial < 256; ++trial) {
        unsigned count = trial % 65;
        float weight = (float) (trial % 9) / 8.0F;
        unsigned i, j;
        memset(first, 0, sizeof(first));
        memset(actual, 0, sizeof(actual));
        for (i = 0; i < 65; ++i) {
            float* a = &first[i].rotate.x;
            float* b = &actual[i].rotate.x;
            first[i].flags = actual[i].flags = JOBJ_USE_QUATERNION | JOBJ_MTX_DIRTY;
            for (j = 0; j < 10; ++j) {
                msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
                a[j] = (float) (s16) msl_test_seed / 65536.0F;
                msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
                b[j] = (float) (s16) msl_test_seed / 65536.0F;
            }
            if (trial % 4 == 0) {
                actual[i].rotate = first[i].rotate;
            }
            first_ptr[i] = &first[i];
            actual_ptr[i] = &actual[i];
        }
        memcpy(expected, actual, sizeof(expected));
        for (i = 0; i < count; ++i) {
            lb_8000C490(&first[i], &expected[i], &expected[i], 1.0F - weight, weight);
        }
        msl_lb_blend_jobj_batch(first_ptr, actual_ptr, count, 1.0F - weight, weight);
        if (memcmp(actual, expected, sizeof(actual)) != 0) {
            fprintf(stderr, "blend publication mismatch trial %u count %u\n", trial, count);
            return -1;
        }
    }
    return 0;
}

static int msl_test_srt_concat(void)
{
    u32 msl_test_seed = 0x734219AD;
    unsigned trial;

    for (trial = 0; trial < 4096; ++trial) {
        Mtx parent, local, expected, actual;
        Vec3 scale, rotate, translate, parent_scale;
        Vec3* compensation;
        float values[24], sine[3], cosine[3];
        unsigned i, row, column, mode;

        for (i = 0; i < 24; ++i) {
            msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
            values[i] = (float) (s16) (msl_test_seed >> 16) / 2048.0F;
        }
        memcpy(parent, values, sizeof(parent));
        memcpy(&scale, values + 12, sizeof(scale));
        memcpy(&rotate, values + 15, sizeof(rotate));
        memcpy(&translate, values + 18, sizeof(translate));
        for (i = 21; i < 24; ++i) {
            if (values[i] == 0.0F) {
                values[i] = 1.0F;
            }
        }
        memcpy(&parent_scale, values + 21, sizeof(parent_scale));
        if (trial % 3 == 1) {
            parent_scale = (Vec3) { 1.0F, 1.0F, 1.0F };
        }
        compensation = trial % 3 == 0 ? NULL : &parent_scale;
        if ((trial & 7) == 0) {
            u32 tiny = 1;
            scale.x = -0.0F;
            rotate.y = -0.0F;
            memcpy(&translate.z, &tiny, sizeof(tiny));
        }
        // Independent source composition: HSD_MtxSRT followed by the
        // ordered scalar PSMTXConcat formula, including translation's FMA.
        HSD_MtxSRT(local, &scale, &rotate, &translate, compensation);
        for (row = 0; row < 3; ++row) {
            for (column = 0; column < 4; ++column) {
                float value = parent[row][0] * local[0][column];
                value = __builtin_fmaf(parent[row][1], local[1][column], value);
                value = __builtin_fmaf(parent[row][2], local[2][column], value);
                expected[row][column] = column == 3
                    ? __builtin_fmaf(1.0F, parent[row][3], value) : value;
            }
        }
        msl_sincosf3(&rotate.x, sine, cosine);
        for (mode = 0; mode < 2; ++mode) {
            if (mode & 1) {
                HSD_MtxSRTConcatTrig(actual, parent,
                                    &scale, &translate, compensation,
                                    sine, cosine);
            } else {
                HSD_MtxSRTConcat(actual, parent,
                                &scale, &rotate, &translate, compensation);
            }
            if (memcmp(actual, expected, sizeof(actual)) != 0) {
                fprintf(stderr, "SRT concat mismatch trial %u mode %u\n",
                        trial, mode);
                return -1;
            }
        }
    }
    return 0;
}

static void msl_test_source_dirty(HSD_JObj* joint)
{
    joint->flags |= JOBJ_MTX_DIRTY;
    if (!(joint->flags & JOBJ_INSTANCE)) {
        HSD_JObj* child;
        for (child = joint->child; child != NULL; child = child->next) {
            if (!(child->flags & JOBJ_MTX_INDEP_PARENT) &&
                !(!(child->flags & JOBJ_USER_DEF_MTX) &&
                  (child->flags & JOBJ_MTX_DIRTY)))
            {
                msl_test_source_dirty(child);
            }
        }
    }
}

static int msl_test_dirty_traversal(MslFighterPose* pose)
{
    static const int children[] = { 1, 2, -1, -1, 5, -1, 7, -1, 3, -1, -1 };
    static const int siblings[] = { 9, 4, 3, -1, 8, 6, -1, -1, 10, -1, -1 };
    static const int parents[] = { -1, 0, 1, 1, 0, 4, 4, 6, 0, -1, 0 };
    static const u32 flags[] = {
        JOBJ_MTX_DIRTY, JOBJ_USER_DEF_MTX, JOBJ_MTX_INDEP_PARENT,
        JOBJ_INSTANCE, JOBJ_HIDDEN,
    };
    HSD_JObj actual[11];
    HSD_JObj expected[11];
    u32 msl_test_seed = 0x6d2b79f5;
    uint16_t base = pose->joint_count;
    unsigned mode;
    unsigned trial;
    unsigned root;
    unsigned i;

    for (mode = 0; mode < 2; ++mode) {
        memset(actual, 0, sizeof(actual));
        memset(expected, 0, sizeof(expected));
        for (i = 0; i < 11; ++i) {
            if (children[i] >= 0) {
                actual[i].child = &actual[children[i]];
                expected[i].child = &expected[children[i]];
            }
            if (siblings[i] >= 0) {
                actual[i].next = &actual[siblings[i]];
                expected[i].next = &expected[siblings[i]];
            }
            if (parents[i] >= 0) {
                actual[i].parent = &actual[parents[i]];
                expected[i].parent = &expected[parents[i]];
            }
        }
        // Generic trees may share instance children. Registered pose trees
        // own a preorder; node 10 models an inserted presentation-only leaf.
        actual[10].flags = expected[10].flags = JOBJ_MSL_GAMEPLAY_COLD;
        if (mode != 0) {
            actual[8].child = expected[8].child = NULL;
            msl_fighter_pose_register_tree(&actual[0]);
        }
        for (trial = 0; trial < 4096; ++trial) {
            for (root = 0; root < 11; ++root) {
                for (i = 0; i < 11; ++i) {
                    unsigned bit;
                    u32 value = i == 10 ? JOBJ_MSL_GAMEPLAY_COLD : 0;
                    msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
                    for (bit = 0; bit < 5; ++bit) {
                        if (msl_test_seed & (1U << (bit + 16))) {
                            value |= flags[bit];
                        }
                    }
                    actual[i].flags = expected[i].flags = value;
                }
                msl_test_source_dirty(&expected[root]);
                HSD_JObjSetMtxDirtySub(&actual[root]);
                for (i = 0; i < 11; ++i) {
                    // ftParts excludes cold leaves from pose publication.
                    // Explicit cold-root calls still use the generic owner.
                    if (mode != 0 && root < 9 && i == 10) {
                        continue;
                    }
                    if (actual[i].flags != expected[i].flags) {
                        fprintf(stderr,
                                "dirty traversal mode %u trial %u "
                                "root %u joint %u\n", mode, trial, root, i);
                        return -1;
                    }
                }
            }
        }
        pose->joint_count = base;
    }
    return 0;
}

static void msl_test_source_root_position(MslFighterPose* pose,
                                          HSD_JObj* root, const Vec3* position)
{
    MslFighterPoseJoint* root_node = (MslFighterPoseJoint*) root->aobj;
    bool affected[MSL_FIGHTER_POSE_JOINT_CAPACITY];
    uint16_t root_index;
    uint16_t i;
    bool unchanged = memcmp(&root->translate, position, sizeof(*position)) == 0;

    HSD_ASSERT(150, root_node != NULL);
    root_index = (uint16_t) (root_node - pose->joints);
    HSD_ASSERT(151, root_node->parent_index == UINT16_MAX);
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
        // A stationary root still invalidates source constraints: a captured
        // fighter can stand still while the other fighter's target bone moves.
        // Only ordinary local matrix products are redundant in that case.
        if (unchanged) {
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

static int msl_test_root_translation(MslFighterPose* pose)
{
    static const int parents[] = { -1, 0, 1, 1, 0, 4, 4, 6 };
    static const int children[] = { 1, 2, -1, -1, 5, -1, 7, -1 };
    static const int siblings[] = { -1, 4, 3, -1, -1, 6, -1, -1 };
    static const u32 flag_bits[] = {
        JOBJ_MTX_DIRTY, JOBJ_USER_DEF_MTX, JOBJ_MTX_INDEP_PARENT,
        JOBJ_INSTANCE, JOBJ_JOINT1, JOBJ_MTX_INDEP_SRT,
    };
    HSD_JObj trees[2][8] = { 0 };
    HSD_RObj constraint = { 0 };
    uint16_t base = pose->joint_count;
    u32 msl_test_seed = 0x71bda543;
    unsigned side, i, trial;

    for (side = 0; side < 2; ++side) {
        for (i = 0; i < 8; ++i) {
            HSD_JObj* joint = &trees[side][i];
            if (parents[i] >= 0) {
                joint->parent = &trees[side][parents[i]];
            }
            if (children[i] >= 0) {
                joint->child = &trees[side][children[i]];
            }
            if (siblings[i] >= 0) {
                joint->next = &trees[side][siblings[i]];
            }
        }
        msl_fighter_pose_register_tree(&trees[side][0]);
    }
    for (trial = 0; trial < 4096; ++trial) {
        Vec3 position;
        for (i = 0; i < 8; ++i) {
            HSD_JObj* a = &trees[0][i];
            HSD_JObj* b = &trees[1][i];
            u32 flags = 0;
            unsigned bit, row, col;
            msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
            for (bit = 0; bit < 6; ++bit) {
                if (msl_test_seed & (1U << (bit + 16))) {
                    flags |= flag_bits[bit];
                }
            }
            a->flags = b->flags = flags;
            a->robj = b->robj = msl_test_seed & 0x1000 ? &constraint : NULL;
            ((MslFighterPoseJoint*) a->aobj)->path =
                msl_test_seed & 0x2000 ? a : NULL;
            ((MslFighterPoseJoint*) b->aobj)->path =
                msl_test_seed & 0x2000 ? b : NULL;
            a->translate.x = (float) (s16) msl_test_seed / 32.0F;
            a->translate.y = (float) (s16) (msl_test_seed >> 8) / 64.0F;
            a->translate.z = (float) (s16) (msl_test_seed >> 16) / 128.0F;
            b->translate = a->translate;
            for (row = 0; row < 3; ++row) {
                for (col = 0; col < 4; ++col) {
                    msl_test_seed = msl_test_seed * 1664525U + 1013904223U;
                    a->mtx[row][col] = b->mtx[row][col] =
                        (float) (s16) msl_test_seed / 256.0F;
                }
            }
        }
        position = trees[0][0].translate;
        if (trial & 1) {
            position.x += 0.25F;
        }
        msl_test_source_root_position(pose, &trees[0][0], &position);
        msl_fighter_pose_set_root_position(&trees[1][0], &position);
        for (i = 0; i < 8; ++i) {
            if (trees[0][i].flags != trees[1][i].flags ||
                memcmp(trees[0][i].mtx, trees[1][i].mtx, sizeof(Mtx)) ||
                memcmp(&trees[0][i].translate, &trees[1][i].translate,
                       sizeof(Vec3)))
            {
                fprintf(stderr, "root translation trial %u joint %u\n", trial, i);
                return -1;
            }
        }
    }
    pose->joint_count = base;
    return 0;
}

#endif

int main(int argc, char** argv)
{
    static MslSmokeContext context;
    HSD_Joint* joint_desc = NULL;
    HSD_MatAnimJoint* matanim_desc = NULL;
    HSD_Archive* archive;
    HSD_JObj* jobj;
    int joint_count;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (msl_smoke_context_init(&context, argv[1]) != 0) {
        return 1;
    }
#ifdef MSL_CORE_NATIVE
    if (msl_fighter_pose_init(&context.match.fighter_pose, 1) != 0 ||
        msl_test_dirty_traversal(&context.match.fighter_pose) != 0 ||
        msl_test_root_translation(&context.match.fighter_pose) != 0 ||
        msl_test_acos_seed() != 0 || msl_test_slerp_weights() != 0 ||
        msl_test_blend_publication() != 0 || msl_test_srt_concat() != 0 ||
        msl_test_sample_publication(&context.match.fighter_pose) != 0)
    {
        return 1;
    }
#endif
    HSD_AObjInitAllocData();
    HSD_FObjInitAllocData();
    HSD_IDInitAllocData();
    HSD_ListInitAllocData();
    HSD_MtxInitAllocData();
    HSD_VecInitAllocData();
    HSD_RObjInitAllocData();
    archive = lbArchive_LoadSymbols(
        "PlFxNr.dat", (void**) &joint_desc, "PlyFox5K_Share_joint",
        (void**) &matanim_desc, "PlyFox5K_Share_matanim_joint", NULL);
    if (archive == NULL || joint_desc == NULL || matanim_desc == NULL) {
        fprintf(stderr, "failed to load Fox model descriptors\n");
        return 1;
    }

    jobj = HSD_JObjLoadJoint(joint_desc);
    if (jobj == NULL || (joint_count = count_joints(jobj)) < 2) {
        fprintf(stderr, "failed to instantiate Fox JObj tree\n");
        return 1;
    }
    HSD_JObjAddAnimAll(jobj, NULL, matanim_desc, NULL);
    HSD_JObjReqAnimAll(jobj, 0.0F);
    HSD_JObjAnimAll(jobj);

    printf("source Fox model/animation: %d JObjs\n", joint_count);
    msl_smoke_context_destroy(&context);
    return 0;
}
