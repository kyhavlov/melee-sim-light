#include "laser_params.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

typedef struct LaserTable {
  MslLaserParams by_char[256];
  uint8_t have_char[256];
  uint8_t loaded;
} LaserTable;

static LaserTable g_tbl;

static uint16_t read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint32_t read_u32_le(const uint8_t* p) {
  uint32_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static float read_f32_le(const uint8_t* p) {
  float v = 0.0f;
  memcpy(&v, p, sizeof(v));
  return v;
}

int laser_params_init(void) {
  if (g_tbl.loaded) {
    return 0;
  }

  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/items/lasers.bin", data_dir);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  uint8_t* buf = (uint8_t*)alloc_malloc((size_t)sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    alloc_free(buf);
    return -1;
  }

  const uint8_t* p = buf;
  const uint8_t* end = buf + (size_t)sz;
  if ((size_t)(end - p) < 8 + 4 + 2 + 2) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(p, "MSLLASR1", 8) != 0) {
    alloc_free(buf);
    return -1;
  }
  p += 8;
  const uint32_t version = read_u32_le(p);
  p += 4;
  if (version != 1 && version != 2 && version != 3 && version != 4) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t record_count = read_u16_le(p);
  p += 2;
  (void)read_u16_le(p);  // reserved
  p += 2;

  // Record layout source:
  // - docs/DATA_CONTRACT.md (MSLLASR1)
  // - tools/extraction/extract_lasers.py
  const size_t record_bytes = (version >= 3) ? 254u : (version == 2) ? 166u : 158u;
  if ((size_t)(end - p) < (size_t)record_count * record_bytes) {
    alloc_free(buf);
    return -1;
  }

  for (uint16_t ri = 0; ri < record_count; ri++) {
    if ((size_t)(end - p) < record_bytes) {
      break;
    }
    const uint8_t char_id = p[0];
    // p[1] pad
    MslLaserParams rec = {0};
    rec.loaded = 1;
    rec.shot_itkind = read_u16_le(p + 2);
    rec.gun_itkind = read_u16_le(p + 4);
    rec.spawn_bone_part_id = read_u16_le(p + 6);
    size_t off = 0;
    if (version >= 2) {
      rec.ground_start_msid = read_u16_le(p + 8);
      rec.ground_loop_msid = read_u16_le(p + 10);
      rec.ground_end_msid = read_u16_le(p + 12);
      rec.air_start_msid = read_u16_le(p + 14);
      rec.air_loop_msid = read_u16_le(p + 16);
      rec.air_end_msid = read_u16_le(p + 18);
      rec.blaster_angle = read_f32_le(p + 20);
      rec.blaster_speed = read_f32_le(p + 24);
      rec.spawn_off_xyz[0] = read_f32_le(p + 28);
      rec.spawn_off_xyz[1] = read_f32_le(p + 32);
      rec.spawn_off_xyz[2] = read_f32_le(p + 36);
      rec.lifetime_frames = read_u16_le(p + 40);
      rec.shoot_frame_count_ground = p[42];
      rec.shoot_frame_count_air = p[43];
      // p[44..45] reserved
      off = 46;
    } else {
      // MSLLASR1 v1: only includes loop msids (start/end are not present).
      rec.ground_loop_msid = read_u16_le(p + 8);
      rec.air_loop_msid = read_u16_le(p + 10);
      rec.blaster_angle = read_f32_le(p + 12);
      rec.blaster_speed = read_f32_le(p + 16);
      rec.spawn_off_xyz[0] = read_f32_le(p + 20);
      rec.spawn_off_xyz[1] = read_f32_le(p + 24);
      rec.spawn_off_xyz[2] = read_f32_le(p + 28);
      rec.lifetime_frames = read_u16_le(p + 32);
      rec.shoot_frame_count_ground = p[34];
      rec.shoot_frame_count_air = p[35];
      // p[36..37] reserved
      off = 38;
    }

    for (int i = 0; i < MSL_LASER_MAX_SHOOT_FRAMES; i++) {
      rec.shoot_frames_ground[i] = read_u16_le(p + off + (size_t)i * 2);
    }
    off += (size_t)MSL_LASER_MAX_SHOOT_FRAMES * 2;
    for (int i = 0; i < MSL_LASER_MAX_SHOOT_FRAMES; i++) {
      rec.shoot_frames_air[i] = read_u16_le(p + off + (size_t)i * 2);
    }
    off += (size_t)MSL_LASER_MAX_SHOOT_FRAMES * 2;

    rec.damage = read_f32_le(p + off);
    rec.size = read_f32_le(p + off + 4);
    rec.angle = read_u16_le(p + off + 8);
    rec.kbg = read_u16_le(p + off + 10);
    rec.wsk = read_u16_le(p + off + 12);
    rec.bkb = read_u16_le(p + off + 14);
    // MSLLASR1 v4+: element is stored explicitly (GALE01 HitElement ids).
    // For older artifacts (v1..v3), fall back to the PlFx/PlFc article scripts:
    // - msid/state=0 uses HitElement_Normal (0)
    // - msid/state=1 uses HitElement_Electric (2)
    // refs/melee/src/melee/lb/forward.h::HitElement
    // _iso/PlFx.dat and _iso/PlFc.dat blaster-shot article ItemStateDesc scripts (see tools/extraction/extract_character_attrs.py)
    rec.shield_damage = (int8_t)p[off + 16];
    rec.element = (version >= 4) ? p[off + 17] : 0u;
    // p[off + 18..19] pad
    rec.hitbox_offsets_x_count = p[off + 20];
    // p[off + 21..23] pad
    off += 24;
    for (int i = 0; i < MSL_LASER_MAX_HITBOX_OFFS_X; i++) {
      rec.hitbox_offsets_x[i] = read_f32_le(p + off + (size_t)i * 4);
    }

    // Optional msid/state=1 hitbox params (MSLLASR1 v3+). For older versions, fall back to state0.
    if (version >= 3) {
      off += (size_t)MSL_LASER_MAX_HITBOX_OFFS_X * 4u;

      rec.state1_damage = read_f32_le(p + off);
      rec.state1_size = read_f32_le(p + off + 4);
      rec.state1_angle = read_u16_le(p + off + 8);
      rec.state1_kbg = read_u16_le(p + off + 10);
      rec.state1_wsk = read_u16_le(p + off + 12);
      rec.state1_bkb = read_u16_le(p + off + 14);
      rec.state1_shield_damage = (int8_t)p[off + 16];
      rec.state1_element = (version >= 4) ? p[off + 17] : 2u;
      rec.state1_hitbox_offsets_x_count = p[off + 20];
      off += 24;
      for (int i = 0; i < MSL_LASER_MAX_HITBOX_OFFS_X; i++) {
        rec.state1_hitbox_offsets_x[i] = read_f32_le(p + off + (size_t)i * 4);
      }
    } else {
      rec.state1_damage = rec.damage;
      rec.state1_size = rec.size;
      rec.state1_angle = rec.angle;
      rec.state1_kbg = rec.kbg;
      rec.state1_wsk = rec.wsk;
      rec.state1_bkb = rec.bkb;
      rec.state1_element = rec.element;
      rec.state1_shield_damage = rec.shield_damage;
      rec.state1_hitbox_offsets_x_count = rec.hitbox_offsets_x_count;
      for (int i = 0; i < MSL_LASER_MAX_HITBOX_OFFS_X; i++) {
        rec.state1_hitbox_offsets_x[i] = rec.hitbox_offsets_x[i];
      }
    }

    g_tbl.by_char[char_id] = rec;
    g_tbl.have_char[char_id] = 1;

    p += record_bytes;
  }

  alloc_free(buf);
  g_tbl.loaded = 1;
  return 0;
}

const MslLaserParams* laser_params_get(uint8_t char_id) {
  if (!g_tbl.loaded || !g_tbl.have_char[char_id]) {
    return NULL;
  }
  return &g_tbl.by_char[char_id];
}

const MslLaserParams* laser_params_for_item_type(uint16_t type) {
  // Current target domain: Fox/Falco only. These are the same char ids used throughout the
  // extracted tables (e.g. data/anims/fox.bin uses char_id=1).
  enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

  const MslLaserParams* fox = laser_params_get((uint8_t)MSL_CHAR_FOX);
  if (fox && fox->shot_itkind == type) {
    return fox;
  }
  const MslLaserParams* falco = laser_params_get((uint8_t)MSL_CHAR_FALCO);
  if (falco && falco->shot_itkind == type) {
    return falco;
  }
  return NULL;
}
