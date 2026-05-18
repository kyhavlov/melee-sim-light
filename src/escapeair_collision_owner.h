#ifndef MSL_ESCAPEAIR_COLLISION_OWNER_H
#define MSL_ESCAPEAIR_COLLISION_OWNER_H

#include <stdint.h>

#include "action_ids.h"

typedef enum MslEscapeAirLockedBottomOwner {
  MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE = 0u,
  MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130 = 1u,
  MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM = 2u,
  MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_HARD_FLOOR = 3u,
} MslEscapeAirLockedBottomOwner;

typedef struct MslEscapeAirCollEpisode {
  uint8_t active;
  uint8_t locked;
  uint8_t sustained;
} MslEscapeAirCollEpisode;

typedef struct MslEscapeAirFinalPublicationOwners {
  uint8_t sustained_same_platform_lock;
  uint8_t sustained_same_ledge_lock;
  uint8_t locked_missing_bottom_owner;
  uint8_t locked_desired_platform_without_bottom_sweep;
  uint8_t locked_desired_nonplatform_without_bottom_sweep;
  uint8_t kneebend_slope_entry;
  uint8_t jumpaerial_high_lift_ledge;
  uint8_t jumpaerial_static_platform_overstep;
  uint8_t cliff_horizontal_ledge_locked;
} MslEscapeAirFinalPublicationOwners;

static inline uint8_t msl_escapeair_locked_bottom_owner_any(uint8_t owner) {
  return (uint8_t)(owner != (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE);
}

static inline uint8_t msl_escapeair_locked_bottom_owner_is_seeded(uint8_t owner) {
  return (uint8_t)(owner == (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130);
}

static inline uint8_t msl_escapeair_locked_bottom_owner_is_live_jumpaerial(uint8_t owner) {
  return (owner == (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
          owner == (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_HARD_FLOOR)
             ? 1u
             : 0u;
}

static inline uint8_t msl_escapeair_locked_bottom_owner_is_live_hard_floor(uint8_t owner) {
  return (uint8_t)(owner == (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_HARD_FLOOR);
}

static inline uint8_t msl_escapeair_locked_bottom_owner_for_live_jumpaerial_entry(
    uint8_t stage_has_soft_platform_floor) {
  return stage_has_soft_platform_floor
             ? (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM
             : (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_HARD_FLOOR;
}

static inline uint8_t msl_escapeair_locked_bottom_owner_normalize(uint8_t owner) {
  switch ((MslEscapeAirLockedBottomOwner)owner) {
    case MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM:
    case MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_HARD_FLOOR:
    case MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130:
      return owner;
    case MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE:
    default:
      return (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
  }
}

static inline uint8_t msl_escapeair_locked_bottom_owner_preserve_or_seeded(uint8_t owner) {
  const uint8_t normalized = msl_escapeair_locked_bottom_owner_normalize(owner);
  return normalized != (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE
             ? normalized
             : (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130;
}

static inline MslEscapeAirCollEpisode msl_escapeair_coll_episode_make(uint16_t action_id,
                                                                      uint16_t seed_prev_action_id,
                                                                      uint8_t ecb_lock_active) {
  MslEscapeAirCollEpisode episode;
  episode.active = (uint8_t)(action_id == (uint16_t)MSL_ACT_ESCAPE_AIR);
  episode.locked = (uint8_t)(episode.active && ecb_lock_active);
  episode.sustained = (uint8_t)(episode.active && seed_prev_action_id == action_id);
  return episode;
}

#endif
