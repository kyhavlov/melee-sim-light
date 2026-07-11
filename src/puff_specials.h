#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api.h"
#include "char_params.h"
#include "state.h"

// Jigglypuff char-special module. Current scope: the multi-jump ladder
// (ftPr_MS_JumpAerialF1..F5, actions 341..345 driven by the common ftCo_JumpAerialF1_*
// callbacks and the fp->x2D0 stats block). Entry lives in locomotion.c (the ladder is jump
// locomotion and reuses the static air-jump entry bookkeeping); this module owns the
// per-frame Anim/IASA work, the reseed reconstruction, and (via physics.c branches) the
// scaled ladder drift. Specials (Rollout/Pound/Sing/Rest) land here in later phases.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D730C,ftCo_800D74A4,
//   ftCo_JumpAerialF1_Anim,ftCo_JumpAerialF1_Phys}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ft_800CB6EC,ftCo_800CBAC4}

// Submotion mapping for the puff char-range block: uniformly action - 46 (295..326).
static inline uint16_t puff_special_submotion(uint16_t action_id) {
  return (uint16_t)(action_id - 46u);
}

static inline uint8_t puff_action_is_multijump(uint8_t char_id, uint16_t action_id) {
  return (uint8_t)(char_id == 15u /* MSL_CHAR_ID_PUFF */ && action_id >= 341u &&
                   action_id <= 345u);
}

// One ft_800CB6EC turnaround-window tick: decrement the armed counter; the scalar facing flips
// when the countdown reaches turn_frames/2 (the model rotation itself is cosmetic).
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
void puff_mjump_turn_tick(MslBatch* batch, const MslCharParams* ch, size_t idx);

// Per-frame Anim-callback work for the ladder states (turnaround window tick + anim-end
// exits) plus the ladder's aerial IASA chain. Runs with the other char-special modules in
// the action phase.
void puff_specials_update_pre_physics(MslBatch* batch);

// Reseed reconstruction for hidden ladder state (the turnaround window counter) and the
// Pound impulse consume-latch. `seed` (may be NULL for legacy/synthetic paths) supplies the
// builder-derived Rollout `mv.pr.specialn` lanes; invalid/missing lanes fall back to the
// documented single-row approximations.
void puff_specials_reseed_init(MslBatch* batch, int batch_index, const MslSeed* seed);

// Char-special Phys ownership (Pound ground friction/root motion + the aerial cmd ladder's
// pre-phase-2 phases). Returns 1 when this module owns the fighter's velocity update.
uint8_t puff_specials_phys(MslBatch* batch, size_t idx);

// Pound ground <-> air phase flips (frame-preserving; 363 <-> 364).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::{ftPr_SpecialS_8013D590,
//   ftPr_SpecialS_8013D5F0}
uint8_t puff_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);
uint8_t puff_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);

// ---------------------------------------------------------------------------
// Rollout (ftPr_SpecialN; actions 346..362)
// ---------------------------------------------------------------------------

static inline uint8_t puff_action_is_rollout(uint8_t char_id, uint16_t action_id) {
  return (uint8_t)(char_id == 15u /* MSL_CHAR_ID_PUFF */ && action_id >= 346u &&
                   action_id <= 362u);
}

// Grounded Start/Loop/Full own an unprojected self-velocity: ftPr_SpecialNStart_Phys writes
// fp->self_vel.x = facing * 0.0001f directly (no ftCommon_ApplyGroundMovement), so the generic
// grounded gr_vel -> self_vel floor-normal projection must not run for these rows.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNStart_Phys
static inline uint8_t puff_rollout_ground_charge_state(uint8_t char_id, uint16_t action_id) {
  return (uint8_t)(char_id == 15u && action_id >= 346u && action_id <= 349u);
}

// deal_dmg_cb (ftPr_SpecialS_8013D764): rolling states -> SpecialNHit at the preserved frame
// with the backward hop (self_vel.x scaled by specialn_vel.x, self_vel.y = specialn_vel.y).
// Fired from the combat x1914 dealt-damage sites next to falcon's; the action gate makes the
// call idempotent within a frame (the first call leaves NHit).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialS_8013D764
void puff_rollout_on_deal_dmg(MslBatch* batch, size_t a_idx);

// Per-frame velocity-scaled hitbox refresh (ftPr_SpecialS_8013D8E4, run pre-combat after the
// script hitbox refresh): below da->xCC the roll hitboxes disable; otherwise damage =
// (s32)(x84 * (x80 + |vel|)), min 1.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialS_8013D8E4
void puff_rollout_hitbox_speed_damage_refresh(MslBatch* batch);

// Wall bounce (Release_Coll / AirChargeRelease_Coll): rolling toward a wall reverses direction
// and decays charge/velocity by da->xD4. Returns 1 when a bounce fired this frame.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::{ftPr_SpecialNRelease_Coll,
//   ftPr_SpecialAirNChargeRelease_Coll}
uint8_t puff_rollout_try_wall_bounce(MslBatch* batch, size_t idx);

// Grounded rollout floor loss -> air variant at the preserved frame. Unlike the falcon-style
// swaps the source does NOT transfer gr_vel into self_vel (ftCommon_8007D5D4 zeroes gr_vel and
// leaves self_vel.x; the air Phys recomputes it from charge next frame), so this owns its lane
// effects and the caller only `continue`s.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c (grounded *_Coll floor-loss paths)
uint8_t puff_rollout_try_floor_loss_swap(MslBatch* batch, size_t idx);

// AirChargeRelease ground contact: vy' = |vy * x78|; below x7C lands into grounded Release at
// the preserved frame (returns 1; caller applies the grounding bundle), else bounces upward
// with an optional stick re-aim and stays airborne (returns 2; caller keeps the fighter in the
// air with the collision-corrected root). Returns 0 when not applicable.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
uint8_t puff_rollout_air_release_land_or_bounce(MslBatch* batch, size_t idx);

// AirChargeRelease cliff-catch tail: the catch inline consumes the mv facing latch before
// re-running ftCliffCommon_80081370, which sets facing toward the stage; the net observable on
// the generic catch is only the consumed latch (model scale/rot resets are cosmetic).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
void puff_rollout_on_cliff_catch(MslBatch* batch, size_t idx);

// ftPr_SpecialNTurn_Coll ground-check selector: at |gr_vel| <= attr x74 the turn uses
// ft_80082978, whose mpColl_8004A45C_Floor endpoint snap keeps the fighter grounded at the
// floor edge (the EDGE_SNAP phase); above the threshold ft_80082888 lets the floor loss fire.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNTurn_Coll
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B21C,mpColl_8004B3F0,mpColl_8004A45C_Floor}
uint8_t puff_rollout_turn_coll_edge_snap(const MslBatch* batch, size_t idx);
