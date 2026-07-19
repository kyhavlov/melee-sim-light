#ifndef MSL_RUNTIME_FIGHTER_POSE_H
#define MSL_RUNTIME_FIGHTER_POSE_H

#include <stdbool.h>
#include <stdint.h>

#include <platform.h>
#include <baselib/forward.h>
#include <dolphin/mtx.h>
#include <melee/lb/forward.h>

enum {
    // The supported-domain construction census reaches 976 retained main and
    // interpolation nodes for four Peach instances.
    MSL_FIGHTER_POSE_JOINT_CAPACITY = 1024,
    MSL_FIGHTER_POSE_TRACK_CAPACITY = 1024,
    MSL_FIGHTER_POSE_PROGRAM_NONE = 0x7FF,
};

typedef struct MslFighterPoseTrack {
    uint8_t* ad;
    uint8_t* ad_head;
    uint32_t length;
    uint8_t flags;
    uint8_t op;
    uint8_t op_intrp;
    uint8_t obj_type;
    uint8_t frac_value;
    uint8_t frac_slope;
    uint16_t nb_pack;
    int16_t startframe;
    uint16_t fterm;
    float time;
    float p0;
    float p1;
    float d0;
    float d1;
} MslFighterPoseTrack;

typedef struct MslFighterPoseJoint {
    uint32_t magic;
    uint32_t flags;
    HSD_JObj* joint;
    HSD_JObj* path;
    float curr_frame;
    float rewind_frame;
    float end_frame;
    float framerate;
    uint16_t track_start;
    uint16_t track_count;
    uint16_t parent_index;
    uint16_t tree_count;
    uint32_t program_index : 11;
    uint32_t program_track_start : 11;
    uint32_t program_track_count : 7;
    uint32_t program_filtered : 1;
    uint32_t decoder_synced : 1;
} MslFighterPoseJoint;

#ifdef MSL_CORE_NATIVE
_Static_assert(sizeof(MslFighterPoseJoint) ==
                   (sizeof(void*) == 8 ? 56 : 44),
               "fighter pose joint must not grow per-Match state");
#endif

typedef struct MslFighterPose {
    MslFighterPoseJoint* joints;
    MslFighterPoseTrack* tracks;
    uint16_t joint_count;
    uint16_t track_used;
} MslFighterPose;

typedef struct MslFighterPoseProgram {
    FigaTree* tree;
    uint32_t value_start;
    uint32_t track_count;
    uint16_t sample_count;
} MslFighterPoseProgram;

typedef struct MslFighterPosePrograms {
    MslFighterPoseProgram* programs;
    float* values;
    uint8_t* valid;
    uint32_t program_count;
    uint32_t value_count;
} MslFighterPosePrograms;

int msl_fighter_pose_init(MslFighterPose* pose);
int msl_fighter_pose_programs_init(MslFighterPosePrograms* programs);
void msl_fighter_pose_programs_deinit(MslFighterPosePrograms* programs);
void msl_fighter_pose_register_tree(HSD_JObj* root);
void msl_fighter_pose_remove_tree(HSD_JObj* root);
void msl_fighter_pose_attach_figa(HSD_JObj* joint, FigaTree* tree,
                                  FigaTrack* tracks, int track_count,
                                  bool filtered);
void msl_fighter_pose_attach_anim_joint(HSD_JObj* joint,
                                        HSD_AnimJoint* anim_joint);
void msl_fighter_pose_request_joint(HSD_JObj* joint, float frame);
void msl_fighter_pose_request_tree(HSD_JObj* root, float frame);
void msl_fighter_pose_set_tree_flags(HSD_JObj* root, uint32_t flags);
void msl_fighter_pose_set_tree_rate(HSD_JObj* root, float rate);
void msl_fighter_pose_animate_joint(HSD_JObj* joint);
void msl_fighter_pose_animate_tree(HSD_JObj* root);
bool msl_fighter_pose_is_animating(HSD_JObj* joint);
bool msl_fighter_pose_tree_is_animating(HSD_JObj* root);
bool msl_fighter_pose_tree_rewound(HSD_JObj* root);
float msl_fighter_pose_tree_rate(HSD_JObj* root);
float msl_fighter_pose_tree_frame(HSD_JObj* root);
float msl_fighter_pose_tree_end(HSD_JObj* root);
bool msl_fighter_pose_owns_joint(const HSD_JObj* joint);
bool msl_fighter_pose_path(const HSD_JObj* joint, HSD_JObj** path);
void msl_fighter_pose_set_root_position(HSD_JObj* root, const Vec3* position);
bool msl_fighter_pose_transform_point(HSD_JObj* joint, const Vec3* local,
                                      Vec3* world);
bool msl_fighter_pose_transform_pair(HSD_JObj* joint, const Vec3* local_a,
                                     const Vec3* local_b, Vec3* world_a,
                                     Vec3* world_b);

#endif
