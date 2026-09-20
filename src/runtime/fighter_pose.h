#ifndef MSL_RUNTIME_FIGHTER_POSE_H
#define MSL_RUNTIME_FIGHTER_POSE_H

#include <stdbool.h>
#include <stdint.h>

#include <platform.h>
#include <baselib/forward.h>
#include <dolphin/mtx.h>
#include <melee/ft/forward.h>
#include <melee/lb/forward.h>

enum {
    // Construction retains at most 256 main/interpolation nodes per configured
    // player slot, including Sheik/Zelda's transform pair and Popo's follower.
    MSL_FIGHTER_POSE_JOINTS_PER_PLAYER = 256,
    MSL_FIGHTER_POSE_JOINT_CAPACITY =
        MSL_FIGHTER_POSE_JOINTS_PER_PLAYER * 4,
    // Live animation tracks are per-fighter state too: each Ice Climbers
    // port keeps about 265 tracks attached across Popo and Nana, so four
    // Ice Climbers ports hold 1058 live tracks (measured post-compaction)
    // against the former flat 1024 and aborted in allocate_tracks. Zelda,
    // the next-widest port at 446 across a four-port mirror, fits well
    // inside the same per-player figure.
    MSL_FIGHTER_POSE_TRACKS_PER_PLAYER = 384,
    MSL_FIGHTER_POSE_TRACK_CAPACITY =
        MSL_FIGHTER_POSE_TRACKS_PER_PLAYER * 4,
    MSL_FIGHTER_POSE_ECB_CAPACITY = 8,
    MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY = 32,
    MSL_FIGHTER_POSE_PROGRAM_NONE = 0x1FFF,
    MSL_FIGHTER_POSE_PROGRAM_NODE_NONE = UINT32_MAX,
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
    uint8_t track_count;
#ifdef MSL_CORE_WASM
    uint8_t source_part_index;
#elif defined(MSL_CORE_NATIVE)
    uint8_t part_anim_flags;
#endif
    uint16_t parent_index;
    uint16_t tree_count;
    uint32_t program_node_index;
    uint16_t program_is_figa : 1;
    uint16_t program_filtered : 1;
    uint16_t decoder_synced : 1;
    // Last integer frame published from the pose table, or
    // MSL_FIGHTER_POSE_TABLE_FRAME_NONE when the decoder owns the tracks.
    // Dropping out of the table path resyncs the track decoder at this
    // exact integer frame so the wait-subtraction arithmetic reproduces the
    // source engine's incremental accumulation bit-exactly.
    uint16_t last_table_frame : 11;
} MslFighterPoseJoint;

#define MSL_FIGHTER_POSE_TABLE_FRAME_NONE 0x7FFu
#ifdef MSL_CORE_WASM
#define MSL_FIGHTER_POSE_PART_NONE UINT8_MAX
#endif
enum {
    MSL_FIGHTER_POSE_PART_FLAG_B0 = 1 << 0,
    MSL_FIGHTER_POSE_PART_FLAG_B5 = 1 << 5,
};
#ifdef MSL_CORE_NATIVE
_Static_assert(sizeof(MslFighterPoseJoint) ==
                   (sizeof(void*) == 8 ? 56 : 48),
               "fighter pose joint must not grow per-Match state");
#endif

typedef struct MslFighterPose {
    MslFighterPoseJoint* joints;
    MslFighterPoseTrack* tracks;
    struct {
        uint16_t origin;
        uint16_t joint_count;
        uint16_t joints[MSL_FIGHTER_POSE_ECB_JOINT_CAPACITY];
    } ecb[MSL_FIGHTER_POSE_ECB_CAPACITY];
    uint16_t joint_count;
    uint16_t joint_capacity;
    uint16_t track_used;
    uint16_t track_capacity;
    uint16_t ecb_count;
} MslFighterPose;

typedef struct MslFighterPoseProgram {
    FigaTree* tree;
    uint32_t node_start;
    uint32_t track_map_start;
    uint32_t track_count;
    uint32_t value_start;
    uint32_t frame_value_count;
    uint16_t node_count;
    uint16_t sample_count;
} MslFighterPoseProgram;

typedef struct MslFighterPoseProgramNode {
    uint32_t frame_value_offset;
    uint16_t track_start;
    uint16_t type_mask;
    uint16_t sample_count;
    uint16_t program_index;
    uint8_t track_count;
    uint8_t value_count;
    uint16_t sample_stride;
} MslFighterPoseProgramNode;

_Static_assert(sizeof(MslFighterPoseProgramNode) == 16,
               "fighter pose program node must remain compact");

typedef struct MslFighterPosePrograms {
    MslFighterPoseProgram* programs;
    MslFighterPoseProgramNode* nodes;
    float* values;
    uint16_t* track_nodes;
    uint16_t* program_hash;
    uint32_t program_count;
    uint32_t program_hash_mask;
    uint32_t node_count;
    uint32_t value_count;
    uint32_t track_node_count;
} MslFighterPosePrograms;

int msl_fighter_pose_init(MslFighterPose* pose, uint8_t player_count);
int msl_fighter_pose_programs_init(MslFighterPosePrograms* programs);
void msl_fighter_pose_programs_deinit(MslFighterPosePrograms* programs);
#ifdef MSL_CORE_NATIVE
uint16_t msl_fighter_pose_program_token(const FigaTree* tree);
FigaTree* msl_fighter_pose_program_tree(uint16_t token);
#endif
void msl_fighter_pose_register_tree(HSD_JObj* root);
void msl_fighter_pose_insert_joint(HSD_JObj* joint);
#ifdef MSL_CORE_WASM
void msl_fighter_pose_bind_part(HSD_JObj* joint, uint8_t part);
#endif
void msl_fighter_pose_remove_tree(HSD_JObj* root);
// Unregister one inserted accessory joint before HSD_JObjRemove unlinks it.
void msl_fighter_pose_erase_joint(HSD_JObj* joint);
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
#ifdef MSL_CORE_WASM
void msl_fighter_pose_animate_parts(HSD_JObj* root,
                                    const FighterBone* parts);
#else
void msl_fighter_pose_animate_parts(HSD_JObj* root);
#endif
bool msl_fighter_pose_is_animating(HSD_JObj* joint);
bool msl_fighter_pose_has_animation(const HSD_JObj* joint);
bool msl_fighter_pose_tree_is_animating(HSD_JObj* root);
bool msl_fighter_pose_tree_rewound(HSD_JObj* root);
float msl_fighter_pose_tree_rate(HSD_JObj* root);
float msl_fighter_pose_tree_frame(HSD_JObj* root);
int msl_fighter_pose_tree_has_animation(HSD_JObj* root);
float msl_fighter_pose_tree_end(HSD_JObj* root);
bool msl_fighter_pose_owns_joint(const HSD_JObj* joint);
bool msl_fighter_pose_path(const HSD_JObj* joint, HSD_JObj** path);
void msl_fighter_pose_set_root_position(HSD_JObj* root, const Vec3* position);
bool msl_fighter_pose_transform_point(HSD_JObj* joint, const Vec3* local,
                                      Vec3* world);
bool msl_fighter_pose_transform_pair(HSD_JObj* joint, const Vec3* local_a,
                                     const Vec3* local_b, Vec3* world_a,
                                     Vec3* world_b);
void msl_fighter_pose_bind_origins(HSD_JObj* const joints[6]);
void msl_fighter_pose_transform_origins(HSD_JObj* const joints[6],
                                        Vec3 world[6]);

#endif
