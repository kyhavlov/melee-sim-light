#ifndef MSL_CORE_RUNTIME_MATCH_H
#define MSL_CORE_RUNTIME_MATCH_H

#include "ft/forward.h"
#include "ft/types.h"

#include <platform.h>

typedef struct MslCoreUcfPadBuffer {
    s8 raw_x[3];
    s8 raw_y[3];
    s8 pending_x;
    s8 pending_y;
    s8 pending_cx;
    s8 pending_cy;
    u8 sdrop_up_frames;
} MslCoreUcfPadBuffer;

typedef struct MslCoreMatchRules {
    int is_teams;
    bool friendly_fire;
    float damage_ratio;
    bool online_fnmsubs_zero;
    bool brawl_offscreen_damage;
    bool freeze_dead_up_fall_physics;
    bool ucf_cardinals_1_0_enabled;
    bool ucf_shield_sdi_enabled;
    bool ucf_sdi_enabled;
    bool ucf_shield_drop_extended_enabled;
    bool ucf_shield_drop_084_enabled;
    u32 frame_count;
    bool ended;
    u8 respawn_reservation_timer[6];
    s8 respawn_reservation_character[6];
    MslCoreUcfPadBuffer ucf_pad[4];
    // gm_16AE.c::gm_8016C75C serves lbl_8046B6A0.x24C standings recomputed
    // only when the scene pointer changes; within one match that is a single
    // snapshot taken at the first CPU standings query.
    bool cpu_standings_snapshot_valid;
    s32 cpu_standings_kos[6];
} MslCoreMatchRules;

void msl_core_bind_match_rules(MslCoreMatchRules* rules);
void msl_core_match_rules_init(MslCoreMatchRules* rules, int is_teams,
                               int friendly_fire, float damage_ratio,
                               int online_fnmsubs_zero,
                               int brawl_offscreen_damage,
                               int freeze_dead_up_fall_physics,
                               int ucf_cardinals_1_0_enabled,
                               int ucf_shield_sdi_enabled,
                               int ucf_sdi_enabled,
                               int ucf_shield_drop_extended_enabled,
                               int ucf_shield_drop_084_enabled);
bool msl_core_uses_online_fnmsubs_zero(void);
bool msl_core_has_brawl_offscreen_damage(void);
bool msl_core_freezes_dead_up_fall_physics(void);
void msl_core_advance_match_frame(void);
void msl_core_apply_team_stock_steal(void);
bool msl_core_match_is_over(void);

void msl_ucf_seed_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx, s8 raw_cy);
void msl_ucf_set_pending_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx,
                             s8 raw_cy);
void msl_ucf_apply_pad_buffer(Fighter* fp);
void msl_ucf_apply_dashback(Fighter* fp);
bool msl_ucf_damagefall_wiggle_check(const Fighter* fp);
bool msl_ucf_sdi_check(const Fighter* fp);
bool msl_ucf_shield_sdi_check(const Fighter* fp);
bool msl_ucf_suppress_spotdodge(const Fighter* fp);
bool msl_ucf_pass_oos_stick_check(const Fighter* fp);
float msl_ucf_squatrv_threshold(const Fighter* fp, float vanilla_threshold);

#endif
