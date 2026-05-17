#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MSL_LASER_MAX_HITBOX_OFFS_X = 16 };

typedef struct MslLaserParams {
  uint8_t loaded;

  // Item kinds (Slippi `item.type` / GALE01 ItemKind).
  uint16_t shot_itkind;
  uint16_t gun_itkind;

  // Spawn attachment (Fighter_Part id domain, compatible with anim_pose_get_matrix part_id).
  uint16_t spawn_bone_part_id;
  float spawn_off_xyz[3];

  // Fighter blaster attrs (ftFox_DatAttrs): used to compute initial projectile velocity.
  float blaster_angle;
  float blaster_speed;

  // SpecialN loop submotion ids (Slippi `animation_index` lower 16 bits).
  //
  // Decomp source-of-truth (GALE01):
  // - refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion (Start/Loop/End)
  // Note: these ids are game-code enums (not DAT attrs); they are stored in lasers.bin for
  // table-driven gameplay and are documented as decomp-backed constants.
  uint16_t ground_start_msid;
  uint16_t ground_loop_msid;
  uint16_t ground_end_msid;
  uint16_t air_start_msid;
  uint16_t air_loop_msid;
  uint16_t air_end_msid;

  // Laser lifetime (frames) and hitbox params (article state script).
  uint16_t lifetime_frames;
  float damage;
  float size;  // radius
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  int8_t shield_damage;
  // Extracted proxy "no flinch" signal for this laser state.
  // Source of truth: `data/items/lasers.bin` (MSLLASR1 reserved-byte lane), currently derived in
  // extraction from article hitbox kbg/wsk/bkb terms by tools/extraction/extract_lasers.py.
  // TODO(decomp/non-flinch-authoritative-signal): replace with a truly authoritative no-flinch lane
  // once identified in decomp/game data.
  uint8_t non_flinch;
  // Per-scripted-hitbox item->x5D4_hitboxes[id].x138 bit. ftColl_8007925C skips item hitboxes
  // with this bit clear while gm_8016B1C4() is active.
  uint16_t hitbox_x138_mask;

  uint8_t hitbox_offsets_x_count;
  float hitbox_offsets_x[MSL_LASER_MAX_HITBOX_OFFS_X];

  // Alternate hitbox params for item msid/state=1 (ItemStateDesc[1].xC_script).
  //
  // Decomp: blaster shots can be spawned with msid=0 or msid=1 via itfoxlaser.c:
  // - it_8029C6A4 -> it_8029C504(..., msid=0, ...)
  // - it_8029C6CC -> it_8029C504(..., msid=1, ...)
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6A4 and ::it_8029C6CC
  //
  // The hitbox scripts for each state live in the fighter's Pl*.dat article:
  // Article.xC_itemStates[msid].xC_script.
  // refs/melee/src/melee/it/types.h::ItemStateDesc
  float state1_damage;
  float state1_size;  // radius
  uint16_t state1_angle;
  uint16_t state1_kbg;
  uint16_t state1_wsk;
  uint16_t state1_bkb;
  uint8_t state1_element;
  int8_t state1_shield_damage;
  uint8_t state1_non_flinch;
  uint16_t state1_hitbox_x138_mask;

  uint8_t state1_hitbox_offsets_x_count;
  float state1_hitbox_offsets_x[MSL_LASER_MAX_HITBOX_OFFS_X];
} MslLaserParams;

// IMPORTANT: laser_params_init() may do IO/allocations; call only during batch init.
int laser_params_init(void);

const MslLaserParams* laser_params_get(uint8_t char_id);

// Map an item kind (Slippi `item.type` / GALE01 ItemKind) to the corresponding laser params, or
// NULL if this item kind is not a supported blaster shot.
//
// IMPORTANT: laser_params_init() must have run during batch init.
const MslLaserParams* laser_params_for_item_type(uint16_t type);

#ifdef __cplusplus
}  // extern "C"
#endif
