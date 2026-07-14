#pragma once

#include <stdint.h>

// Init-time loader for decomp-shaped guard tilt shield-bubble centers.
//
// Source of truth: `data/shields/<character>.bin` (ISO-derived MSLSHLD1 artifact).
// MSLSHLD1 v4 extends the steady Guard tilt table with the ftCo_80091E78
// `ftData.x20->x8` GuardOn target and GuardOn current-pose data.
//
// The simulator consumes this table on the per-frame hot path in shields_refresh() to compute a
// shield bubble center that depends on guard tilt (stick direction) instead of approximating at
// fighter (pos_x,pos_y,z=0).
//
// IMPORTANT: shield_tilt_table_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslShieldTiltTableView {
  const float* xyz;           // length = frame_count * 3 (steady Guard tilt target)
  float guard_on_x20_xyz[3];  // decomp: ftCo_80091E78 ftData.x20->x0->x8 target
  const float* guard_on_xyz;  // length = guard_on_frame_count * 3 (live GuardOn pose trajectory)
  uint16_t frame_count;       // number of frames in the steady Guard tilt table
  uint16_t neutral_frame;     // decomp: mv.co.guard.x8 initial value (e.g. 10)
  uint16_t guard_on_frame_count;  // number of frames in the GuardOn live-pose table
  uint16_t shield_part;  // decomp: ftData.x8->x11 shield bone (fractional tilt anim sampling)
} MslShieldTiltTableView;

int shield_tilt_table_init(void);

// Return a view over the loaded table for this character id.
// Returns 0 on success; nonzero if missing/unloaded.
int msl_shield_tilt_table_view(uint8_t char_id, MslShieldTiltTableView* out);

// Steady Guard tilt shield-bone center at the CONTINUOUS mv.co.guard.x8 frame. The source
// samples the Guard tilt anim at the float x8 (ftAnim_80070710 -> HSD_JObjReqAnimAllByFlags),
// so consumers must not floor the frame before sampling. Evaluates msid 38 (ftCo_SM_Guard)
// FObj tracks at the exact float frame via anim_pose; falls back to the baked integer row
// when track data is unavailable. Returns 0 on success.
int msl_shield_tilt_target_xyz_f32(uint8_t char_id, const MslShieldTiltTableView* tv, float x8_f32,
                                   float out_xyz[3]);
