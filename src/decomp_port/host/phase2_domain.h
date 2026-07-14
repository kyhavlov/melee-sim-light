#ifndef MSL_DECOMP_PORT_PHASE2_DOMAIN_H
#define MSL_DECOMP_PORT_PHASE2_DOMAIN_H

#include "ft/forward.h"
#include "ft/types.h"

#include <platform.h>

void msl_phase2_set_match_rules(int is_teams, float damage_ratio);
void msl_phase2_advance_match_frame(void);
bool msl_phase2_match_is_over(void);

void msl_ucf_seed_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx, s8 raw_cy);
void msl_ucf_set_pending_pad(int slot, s8 raw_x, s8 raw_y, s8 raw_cx,
                             s8 raw_cy);
void msl_ucf_apply_pad_buffer(Fighter* fp);
void msl_ucf_apply_dashback(Fighter* fp);
bool msl_ucf_damagefall_wiggle_check(const Fighter* fp);
bool msl_ucf_sdi_check(const Fighter* fp);
bool msl_ucf_shield_sdi_check(const Fighter* fp);
bool msl_ucf_suppress_spotdodge(const Fighter* fp);
bool msl_ucf_pass_oos_check(const Fighter* fp);
float msl_ucf_squatrv_threshold(const Fighter* fp, float vanilla_threshold);

#endif
