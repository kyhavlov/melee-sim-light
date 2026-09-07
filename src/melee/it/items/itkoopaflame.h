#ifndef GALE01_2AC8A8
#define GALE01_2AC8A8

#include <platform.h>

#include <melee/it/forward.h>

#include <melee/it/items/types.h>

typedef struct itKoopaFlame_Attributes {
    float x0_lifetime;        // 28.0
    float x4_hitbox_lifetime; // 20.0
    float x8_min_speed;       // 1.9
    float xC_max_speed;       // 2.2
    float x10_min_angle;      // 2.1816616
    float x14_max_angle;      // 2.5307274
} itKoopaFlame_Attributes;

/* 2AC8A8 */ void itKoopaFlame_Update_Direction(Item_GObj* gobj, int flags);
/* 2AC9F8 */ void itKoopaFlame_Update_Angle(Item_GObj* gobj, int flags);
/* 2ACBA0 */ Item_GObj* itKoopaFlame_Spawn(Fighter_GObj* parent, Vec3* pos,
                                           f32 facing_dir, bool unk, s32 gfx,
                                           s32 base_speed, s32 scale,
                                           s32 kind);
/* 2ACEBC */ void itKoopaFlame_Setup(Item_GObj* gobj_i, Fighter_GObj* gobj_f,
                                     int unk);
/* 2ACF9C */ bool itKoopaFlame_UnkMotion0_Anim(Item_GObj* gobj);
/* 2AD160 */ void itKoopaFlame_UnkMotion0_Phys(Item_GObj* gobj);
/* 2AD1E4 */ bool itKoopaFlame_UnkMotion0_Coll(Item_GObj* gobj);
/* 2AD2E8 */ bool itKoopaFlame_Logic111_DmgDealt(Item_GObj* gobj);
/* 2AD2F0 */ bool itKoopaFlame_Logic111_Reflected(Item_GObj* gobj);
/* 2AD384 */ bool itKoopaFlame_Logic111_Clanked(Item_GObj* gobj);
/* 2AD38C */ bool itKoopaFlame_Logic111_Absorbed(Item_GObj* gobj);
/* 2AD3B0 */ bool itKoopaFlame_Logic111_ShieldBounced(Item_GObj* gobj);
/* 2AD450 */ bool itKoopaFlame_Logic111_HitShield(Item_GObj* gobj);
/* 2AD458 */ void itKoopaFlame_Logic111_EvtUnk(Item_GObj* item_gobj,
                                               Item_GObj* ref_gobj);
/* 3F6C58 */ extern ItemStateTable ItemStateTable_KoopaFlame[];

#endif
