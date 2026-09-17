#include "runtime/viewer.h"

#include "cm/camera.h"
#include "ft/chara/ftCommon/forward.h"
#include "ft/fighter.h"
#include "ft/types.h"
#include "gr/types.h"
#include "it/forward.h"
#include "it/inlines.h"
#include "it/itCharItems.h"
#include "it/types.h"
#include "lb/lb_00B0.h"
#include "lb/types.h"
#include "runtime/context.h"
#include "runtime/observation.h"
#include "runtime/scalar.h"

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <baselib/jobj.h>

enum {
    MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS = 2,
    MSL_CORE_STAGE_YOSHIS_STORY = 8,
};

extern void msl_camera_get_render_transform(Vec3* position, Vec3* interest,
                                            float* fov);

static void put_u16(uint8_t* out, size_t field, uint16_t value)
{
    msl_core_put_le16(out + field, value);
}

static void put_u32(uint8_t* out, size_t field, uint32_t value)
{
    msl_core_put_le32(out + field, value);
}

static void put_f32(uint8_t* out, size_t field, float value)
{
    msl_core_put_lef32(out + field, value);
}

static uint16_t fighter_hitbox_bone(const Fighter* fp, const HitCapsule* hit)
{
    uint32_t i;
    FighterPartsTable* table = ftPartsTable[fp->kind];

    if (hit->jobj == NULL || table == NULL) {
        return UINT16_MAX;
    }
    for (i = 0; i < table->parts_num; ++i) {
        if (fp->parts[i].joint == hit->jobj) {
            return (uint16_t) i;
        }
    }
    return UINT16_MAX;
}

static float joint_uniform_scale(HSD_JObj* joint)
{
    MtxPtr matrix;
    float x;
    float y;
    float z;

    if (joint == NULL) {
        return 0.0F;
    }
    matrix = HSD_JObjGetMtxPtr(joint);
    // Column norms are the per-axis scales. Take the largest: Game & Watch's
    // skeleton is flattened along one axis (about 0.03), so the first column
    // alone reported a 0.06-unit shield for him.
    {
        float best = 0.0F;
        int column;
        for (column = 0; column < 3; ++column) {
            float norm;
            x = matrix[0][column];
            y = matrix[1][column];
            z = matrix[2][column];
            norm = sqrtf(x * x + y * y + z * z);
            if (norm > best) {
                best = norm;
            }
        }
        return best;
    }
}

static void write_hitboxes(const Fighter* fp, uint8_t* player_out)
{
    int i;

    for (i = 0; i < MSL_CORE_MAX_HITBOXES; ++i) {
        const HitCapsule* hit = &fp->x914[i];
        uint8_t* out = player_out + offsetof(MslCoreViewerPlayer, hitboxes) +
                       (size_t) i * sizeof(MslCoreViewerHitbox);
        float radius;

        if (hit->state == HitCapsule_Disabled) {
            continue;
        }
        // ftColl_8007AD18 publishes x4C from the live fighter JObj before the
        // collision pass. The collision owner scales ordinary hitboxes by
        // fighter scale and leaves x43_b1 hitboxes unscaled.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,
        //   ftColl_80076ED8}
        radius = hit->scale * (hit->x43_b1 ? 1.0F : fp->x34_scale.y);
        put_f32(out, offsetof(MslCoreViewerHitbox, x), hit->x4C.x);
        put_f32(out, offsetof(MslCoreViewerHitbox, y), hit->x4C.y);
        put_f32(out, offsetof(MslCoreViewerHitbox, z), hit->x4C.z);
        put_f32(out, offsetof(MslCoreViewerHitbox, radius), radius);
        put_f32(out, offsetof(MslCoreViewerHitbox, damage), hit->damage);
        put_u16(out, offsetof(MslCoreViewerHitbox, bone_part_id),
                fighter_hitbox_bone(fp, hit));
        out[offsetof(MslCoreViewerHitbox, enabled)] = 1;
    }
}

static void write_shield(const Fighter* fp, uint8_t* player_out)
{
    const HitResult* shield = &fp->shield_hit;
    float angle;
    float magnitude;

    if (!fp->x221B_b0 || shield->bone == NULL) {
        return;
    }
    // Publish the collision center the source itself uses: the shield bone's
    // world transform applied to the descriptor offset, exactly as
    // lbColl_80007BCC fills HitResult::pos lazily during collision queries.
    // ftCo_800921DC zeros the bone's local translation at guard start and the
    // guard tilt animation then moves it, so the world matrix at end of frame
    // is the rendered/colliding center.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_80091BC4,ftCo_80091E78,ftCo_800921DC}
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    {
        Vec3 offset = shield->offset;
        Vec3 center;
        lb_8000B1CC(shield->bone, &offset, &center);
        put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_x), center.x);
        put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_y), center.y);
        put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_z), center.z);
    }
    put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_radius),
            shield->size * joint_uniform_scale(shield->bone));

    {
        float strength = fp->lightshield_amount;
        if (strength < 0.0F) {
            strength = 0.0F;
        } else if (strength > 1.0F) {
            strength = 1.0F;
        }
        player_out[offsetof(MslCoreViewerPlayer, shield_strength)] =
            (uint8_t) (strength * 255.0F + 0.5F);
    }

    if (fp->motion_id < ftCo_MS_GuardOn ||
        fp->motion_id > ftCo_MS_GuardReflect)
    {
        return;
    }

    // ftCo_80091BC4 stores a smoothed local angle in x8 (neutral origin 10)
    // and smoothed stick magnitude in x4. Convert that source state back to a
    // facing-aware unit-space direction for the renderer; the renderer owns
    // the purely visual distance scale.
    angle = (fp->mv.co.guard.x8 - 10.0F) * (acosf(-1.0F) / 180.0F);
    magnitude = fp->mv.co.guard.x4;
    put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_tilt_x),
            cosf(angle) * fp->facing_dir * magnitude);
    put_f32(player_out, offsetof(MslCoreViewerPlayer, shield_tilt_y),
            sinf(angle) * magnitude);
}

static void write_player(const MslCoreCompare* compare, int index,
                         const Fighter* fp, uint8_t* out)
{
#define COPY_U8(field)                                                        \
    out[offsetof(MslCoreViewerPlayer, field)] =                               \
        ((const uint8_t*) compare)[offsetof(MslCoreCompare, field) + index]
#define COPY_U16(field)                                                       \
    put_u16(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_le16((const uint8_t*) compare +                      \
                              offsetof(MslCoreCompare, field) +               \
                              (size_t) index * sizeof(uint16_t)))
#define COPY_U32(field)                                                       \
    put_u32(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_le32((const uint8_t*) compare +                      \
                              offsetof(MslCoreCompare, field) +               \
                              (size_t) index * sizeof(uint32_t)))
#define COPY_F32(field)                                                       \
    put_f32(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_lef32((const uint8_t*) compare +                     \
                               offsetof(MslCoreCompare, field) +              \
                               (size_t) index * sizeof(float)))

    COPY_U8(char_id);
    COPY_U8(team_id);
    COPY_U8(facing);
    COPY_U8(on_ground);
    COPY_U8(is_dead);
    COPY_U8(jumps_left);
    COPY_U8(stocks);
    COPY_U8(hurtbox_state);
    COPY_U8(l_cancel);
    COPY_U8(last_attack_landed);
    COPY_U8(combo_count);
    COPY_U8(last_hit_by);
    out[offsetof(MslCoreViewerPlayer, last_hit_element)] =
        (uint8_t) fp->dmg.x1860_element;
    memcpy(out + offsetof(MslCoreViewerPlayer, state_flags),
           (const uint8_t*) compare + offsetof(MslCoreCompare, state_flags) +
               (size_t) index * MSL_CORE_STATE_FLAGS_BYTES,
           MSL_CORE_STATE_FLAGS_BYTES);
    COPY_U16(action_id);
    COPY_U16(action_frame);
    COPY_U16(hitlag);
    COPY_U16(hitstun);
    COPY_U16(ground_id);
    COPY_U32(animation_index);
    COPY_F32(pos_x);
    COPY_F32(pos_y);
    COPY_F32(speed_air_x_self);
    COPY_F32(speed_ground_x_self);
    COPY_F32(speed_y_self);
    COPY_F32(speed_x_attack);
    COPY_F32(speed_y_attack);
    COPY_F32(percent);
    COPY_F32(shield_hp);
    write_shield(fp, out);
    write_hitboxes(fp, out);

#undef COPY_U8
#undef COPY_U16
#undef COPY_U32
#undef COPY_F32
}


static void write_follower(const MslCoreCompare* compare, int index,
                           const Fighter* fp, uint8_t* out)
{
#define COPY_U8(field)                                                        \
    out[offsetof(MslCoreViewerPlayer, field)] =                               \
        ((const uint8_t*)                                                     \
             compare)[offsetof(MslCoreCompare, follower_##field) + index]
#define COPY_U16(field)                                                       \
    put_u16(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_le16((const uint8_t*) compare +                      \
                              offsetof(MslCoreCompare, follower_##field) +    \
                              (size_t) index * sizeof(uint16_t)))
#define COPY_U32(field)                                                       \
    put_u32(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_le32((const uint8_t*) compare +                      \
                              offsetof(MslCoreCompare, follower_##field) +    \
                              (size_t) index * sizeof(uint32_t)))
#define COPY_F32(field)                                                       \
    put_f32(out, offsetof(MslCoreViewerPlayer, field),                        \
            msl_core_get_lef32((const uint8_t*) compare +                     \
                               offsetof(MslCoreCompare, follower_##field) +   \
                               (size_t) index * sizeof(float)))

    COPY_U8(char_id);
    // The follower has no team lane of its own; render with the leader's.
    out[offsetof(MslCoreViewerPlayer, team_id)] =
        ((const uint8_t*) compare)[offsetof(MslCoreCompare, team_id) + index];
    COPY_U8(facing);
    COPY_U8(on_ground);
    out[offsetof(MslCoreViewerPlayer, is_dead)] = 0;
    COPY_U8(jumps_left);
    COPY_U8(stocks);
    COPY_U8(hurtbox_state);
    COPY_U8(l_cancel);
    COPY_U8(last_attack_landed);
    COPY_U8(combo_count);
    COPY_U8(last_hit_by);
    out[offsetof(MslCoreViewerPlayer, last_hit_element)] =
        (uint8_t) fp->dmg.x1860_element;
    memcpy(out + offsetof(MslCoreViewerPlayer, state_flags),
           (const uint8_t*) compare +
               offsetof(MslCoreCompare, follower_state_flags) +
               (size_t) index * MSL_CORE_STATE_FLAGS_BYTES,
           MSL_CORE_STATE_FLAGS_BYTES);
    COPY_U16(action_id);
    COPY_U16(action_frame);
    COPY_U16(hitlag);
    COPY_U16(hitstun);
    COPY_U16(ground_id);
    COPY_U32(animation_index);
    COPY_F32(pos_x);
    COPY_F32(pos_y);
    COPY_F32(speed_air_x_self);
    COPY_F32(speed_ground_x_self);
    COPY_F32(speed_y_self);
    COPY_F32(speed_x_attack);
    COPY_F32(speed_y_attack);
    COPY_F32(percent);
    COPY_F32(shield_hp);
    write_shield(fp, out);
    write_hitboxes(fp, out);

#undef COPY_U8
#undef COPY_U16
#undef COPY_U32
#undef COPY_F32
}

static void write_stage(const MslCoreMatch* match, uint8_t* out)
{
    int i;

    if (match->config.stage_id == MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS) {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            const Ground* ground = &match->stage_ground[i];
            int side;
            if (!match->stage_ground_used[i] || ground->map_id != 4) {
                continue;
            }
            // grIzumi creates the source actors in reverse protocol order:
            // xC8=0 is replay/viewer right (0), xC8=1 is left (1).
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
            side = 1 - ground->gv.izumi3.xC8;
            if ((unsigned) side < 2) {
                put_f32(out,
                        offsetof(MslCoreViewerStage, fod_platform_height) +
                            (size_t) side * sizeof(float),
                        ground->gv.izumi3.xD0);
                out[offsetof(MslCoreViewerStage, fod_platform_valid) + side] =
                    1;
            }
        }
    } else if (match->config.stage_id == MSL_CORE_STAGE_YOSHIS_STORY) {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            const Ground* ground = &match->stage_ground[i];
            Vec3 position;
            if (!match->stage_ground_used[i] || ground->map_id != 2 ||
                ground->u.randall.jobj == NULL)
            {
                continue;
            }
            // Randall is map actor 2; its child JObj drives the moving
            // collision box and therefore is the authoritative render point.
            // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,
            //   grStory_801E33E0}
            // The stage process has already published the matrix consumed by
            // collision for this frame. Output must not force a newer lazy
            // matrix epoch or mutate the state it observes.
            position.x = ground->u.randall.jobj->mtx[0][3];
            position.y = ground->u.randall.jobj->mtx[1][3];
            out[offsetof(MslCoreViewerStage, randall_exists)] = 1;
            put_f32(out, offsetof(MslCoreViewerStage, randall_x), position.x);
            put_f32(out, offsetof(MslCoreViewerStage, randall_y), position.y);
            break;
        }
    }
}

static void write_item_visuals(const MslCoreMatch* match, uint8_t* out)
{
    Item_GObj* gobj = (Item_GObj*) match->gobj.entities->items;
    int slot = 0;

    // Same list order as msl_core_write_items_into_zeroed so slot i here
    // describes items[i].
    while (gobj != NULL && slot < MSL_CORE_MAX_ITEMS) {
        const Item* ip = GET_ITEM(gobj);
        HSD_JObj* joint = GET_JOBJ(gobj);
        uint8_t* visual = out + (size_t) slot * sizeof(MslCoreViewerItemVisual);

        if (ip != NULL &&
            (ip->kind == It_Kind_Mewtwo_ShadowBall ||
             ip->kind == It_Kind_Samus_Charge) &&
            joint != NULL)
        {
            // itMewtwoshadowball_UnkMotion0_Anim and
            // itSamuschargeshot_UnkMotion0_Anim scale the grandchild joint
            // with the charge; Mewtwo's animation also carries the orbit.
            HSD_JObj* child = HSD_JObjGetChild(joint);
            if (child != NULL) {
                HSD_JObj* grandchild = HSD_JObjGetChild(child);
                joint = grandchild != NULL ? grandchild : child;
            }
        }
        if (joint != NULL) {
            MtxPtr matrix = HSD_JObjGetMtxPtr(joint);
            if (matrix != NULL) {
                put_f32(visual, offsetof(MslCoreViewerItemVisual, x),
                        matrix[0][3]);
                put_f32(visual, offsetof(MslCoreViewerItemVisual, y),
                        matrix[1][3]);
                put_f32(visual, offsetof(MslCoreViewerItemVisual, z),
                        matrix[2][3]);
                put_f32(visual, offsetof(MslCoreViewerItemVisual, scale),
                        joint_uniform_scale(joint));
                visual[offsetof(MslCoreViewerItemVisual, valid)] = 1;
            }
        }
        if (ip != NULL) {
            // The chain's x0 link is the one the item physics drives and
            // collides with walls/ledges (it_802A5AE0 / it_802B... take it as
            // link_0), so it is the hook or beam tip and the hang anchor. x4
            // is the hand end. The item position itself is frozen at spawn.
            const ItemLink* tip = NULL;
            if (ip->kind == It_Kind_Samus_GBeam) {
                tip = ip->xDD4_itemVar.samusgrapple.x0;
            } else if (ip->kind == It_Kind_Link_HShot ||
                       ip->kind == It_Kind_CLink_HShot)
            {
                tip = ip->xDD4_itemVar.linkhookshot.x0;
            }
            if (tip != NULL) {
                // Links are zeroed until the chain's first physics pass, so
                // an exact origin means "not placed yet".
                if (tip->pos.x != 0.0F || tip->pos.y != 0.0F ||
                    tip->pos.z != 0.0F)
                {
                    put_f32(visual, offsetof(MslCoreViewerItemVisual, tip_x),
                            tip->pos.x);
                    put_f32(visual, offsetof(MslCoreViewerItemVisual, tip_y),
                            tip->pos.y);
                    visual[offsetof(MslCoreViewerItemVisual, tip_valid)] = 1;
                }
            }
        }
        if (ip != NULL) {
            int h;
            for (h = 0; h < 4; ++h) {
                const HitCapsule* hit = &ip->x5D4_hitboxes[h].hit;
                if (hit->state == HitCapsule_Disabled) {
                    continue;
                }
                put_f32(visual, offsetof(MslCoreViewerItemVisual, hitbox_x),
                        hit->x4C.x);
                put_f32(visual, offsetof(MslCoreViewerItemVisual, hitbox_y),
                        hit->x4C.y);
                put_f32(visual,
                        offsetof(MslCoreViewerItemVisual, hitbox_radius),
                        hit->scale);
                break;
            }
        }
        slot += 1;
        gobj = (Item_GObj*) gobj->next;
    }
}

void msl_core_match_write_viewer(const MslCoreMatch* match,
                                 MslCoreViewerState* output)
{
    const MslCoreCompare* compare = msl_core_match_output(match);
    uint8_t* out = (uint8_t*) output;
    Vec3 eye;
    Vec3 interest;
    float fov;
    int i;

    memset(output, 0, sizeof(*output));
    msl_core_bind_match((MslCoreMatch*) match);
    put_u32(out, offsetof(MslCoreViewerState, frame_id),
            (uint32_t) match->frame_id);
    put_u32(
        out, offsetof(MslCoreViewerState, random_seed),
        msl_core_get_le32((const uint8_t*) compare +
                          offsetof(MslCoreCompare, frame_pre_random_seed)));
    put_u32(out, offsetof(MslCoreViewerState, stage_id),
            match->config.stage_id);
    put_f32(out, offsetof(MslCoreViewerState, damage_ratio),
            match->rules.damage_ratio);
    out[offsetof(MslCoreViewerState, num_players)] = match->config.num_players;
    out[offsetof(MslCoreViewerState, is_teams)] = match->config.is_teams != 0;
    out[offsetof(MslCoreViewerState, friendly_fire)] =
        match->config.friendly_fire != 0;
    out[offsetof(MslCoreViewerState, terminal)] = match->rules.ended != 0;
    out[offsetof(MslCoreViewerState, stock_count)] = match->config.stock_count;

    for (i = 0; i < match->config.num_players; ++i) {
        write_player(compare, i, GET_FIGHTER(match->fighters[i]),
                     out + offsetof(MslCoreViewerState, players) +
                         (size_t) i * sizeof(MslCoreViewerPlayer));
        if (match->follower_fighters[i] != NULL &&
            compare->follower_present[i])
        {
            out[offsetof(MslCoreViewerState, follower_present) + i] = 1;
            write_follower(compare, i,
                           GET_FIGHTER(match->follower_fighters[i]),
                           out + offsetof(MslCoreViewerState, followers) +
                               (size_t) i * sizeof(MslCoreViewerPlayer));
        }
    }
    memcpy(out + offsetof(MslCoreViewerState, items), compare->items,
           sizeof(compare->items));
    msl_core_canonicalize_production_items(output->items);
    write_item_visuals(match, out + offsetof(MslCoreViewerState, item_visuals));
    write_stage(match, out + offsetof(MslCoreViewerState, stage));
    msl_camera_get_render_transform(&eye, &interest, &fov);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, eye_x),
            eye.x);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, eye_y),
            eye.y);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, eye_z),
            eye.z);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, interest_x),
            interest.x);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, interest_y),
            interest.y);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, interest_z),
            interest.z);
    put_f32(out,
            offsetof(MslCoreViewerState, camera) +
                offsetof(MslCoreViewerCamera, fov),
            fov);
}
