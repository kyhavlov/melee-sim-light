#if MSL_VISUAL_TRACK_BUILD
#include "runtime/viewer_track.h"

#include "cm/camera.h"
#include "ft/fighter.h"
#include "ft/chara/ftCommon/ftCo_DamageFall.h"
#include "ft/ft_0DF0.h"
#include "ft/ftlib.h"
#include "ft/types.h"
#include "gr/ground.h"
#include "gr/inlines.h"
#include "gr/types.h"
#include "ft/ftdynamics.h"
#include "it/inlines.h"
#include "it/itCharItems.h"
#include "it/types.h"
#include "lb/types.h"
#include "runtime/context.h"
#include "runtime/scalar.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/gobj.h>
#include <baselib/jobj.h>

// The game camera's render transform and extras (cm/camera.c), read only.
void msl_camera_get_render_transform(Vec3* position, Vec3* interest,
                                     float* fov);
void msl_camera_track_extras(Vec2* translation, u8* background_rgb);

// scalar.c's external stage ids.
enum {
    TRACK_STAGE_BATTLEFIELD = 31,
    TRACK_STAGE_FINAL_DESTINATION = 32,
    TRACK_STAGE_FOUNTAIN = 2,
};

// A state is written only when it changes: the last value of each key (its
// kind and identity fields) is kept.
enum { TRACK_KEYS = 2048, TRACK_KEY_LEN = 48, TRACK_VALUE_LEN = 200 };

enum { TRACK_POSE_OWNERS = 64, TRACK_POSE_JOINTS = 128 };

// One owner's joints as last written (track_poses): an item, a fighter, a
// chain's links.
typedef struct MslTrackPose {
    unsigned char used;
    char kind;
    int id;
    int frame;
    int character;
    unsigned char known[TRACK_POSE_JOINTS];
    unsigned char flag[TRACK_POSE_JOINTS];
    float last[TRACK_POSE_JOINTS][12];
} MslTrackPose;

struct MslViewerTrack {
    // Lines since the last msl_core_match_viewer_track.
    char* text;
    size_t size;
    size_t capacity;
    struct {
        char key[TRACK_KEY_LEN];
        char value[TRACK_VALUE_LEN];
    } state[TRACK_KEYS];
    // The poses last written, per owner (track_poses), and the line being
    // made.
    MslTrackPose pose[TRACK_POSE_OWNERS];
    char pose_line[48 * 1024];
    size_t pose_used;
};

_Thread_local MslViewerTrack* msl_core_context_viewer_track;

MslViewerTrack* msl_viewer_track_create(void)
{
    MslViewerTrack* track = calloc(1, sizeof(*track));
    if (track == NULL) {
        return NULL;
    }
    track->capacity = 64 * 1024;
    track->text = malloc(track->capacity);
    if (track->text == NULL) {
        free(track);
        return NULL;
    }
    track->text[0] = '\0';
    return track;
}

void msl_viewer_track_destroy(MslViewerTrack* track)
{
    if (track != NULL) {
        free(track->text);
        free(track);
    }
}

const char* msl_core_match_viewer_track(MslCoreMatch* match, size_t* size)
{
    MslViewerTrack* track = match != NULL ? match->viewer_track : NULL;
    if (track == NULL) {
        *size = 0;
        return NULL;
    }
    *size = track->size;
    track->size = 0;
    return track->text;
}

static void put(MslViewerTrack* track, const char* format, ...)
{
    for (;;) {
        size_t room = track->capacity - track->size;
        va_list args;
        int n;
        va_start(args, format);
        n = vsnprintf(track->text + track->size, room, format, args);
        va_end(args);
        if (n < 0) {
            track->text[track->size] = '\0';
            return;
        }
        if ((size_t) n < room) {
            track->size += (size_t) n;
            return;
        }
        {
            size_t capacity = track->capacity * 2;
            char* grown;
            while (capacity <= track->size + (size_t) n) {
                capacity *= 2;
            }
            grown = realloc(track->text, capacity);
            if (grown == NULL) {
                fprintf(stderr, "viewer track: out of memory\n");
                abort();
            }
            track->text = grown;
            track->capacity = capacity;
        }
    }
}

// The frame a hook fires in: the match advances frame_id after the gameplay
// procs, so a hook inside a step (or construction) belongs to frame_id + 1.
static int hook_frame(void)
{
    MslCoreMatch* match = msl_core_try_active_match();
    return match != NULL ? match->frame_id + 1 : -123;
}

static unsigned hash_key(const char* key)
{
    unsigned h = 2166136261U;
    while (*key) {
        h = (h ^ (unsigned char) *key++) * 16777619U;
    }
    return h;
}

// Writes [frame,key value] when value differs from the last one for key.
static void emit_state(MslViewerTrack* track, int frame, const char* key,
                       const char* value)
{
    unsigned slot = hash_key(key) % TRACK_KEYS;
    unsigned probe;

    for (probe = 0; probe < TRACK_KEYS; ++probe) {
        unsigned i = (slot + probe) % TRACK_KEYS;
        if (track->state[i].key[0] == '\0') {
            snprintf(track->state[i].key, TRACK_KEY_LEN, "%s", key);
            snprintf(track->state[i].value, TRACK_VALUE_LEN, "%s", value);
            put(track, "[%d,%s%s]\n", frame, key, value);
            return;
        }
        if (strcmp(track->state[i].key, key) == 0) {
            if (strcmp(track->state[i].value, value) != 0) {
                snprintf(track->state[i].value, TRACK_VALUE_LEN, "%s", value);
                put(track, "[%d,%s%s]\n", frame, key, value);
            }
            return;
        }
    }
}

// The last value written for key, or NULL.
static const char* last_state(const MslViewerTrack* track, const char* key)
{
    unsigned slot = hash_key(key) % TRACK_KEYS;
    unsigned probe;

    for (probe = 0; probe < TRACK_KEYS; ++probe) {
        unsigned i = (slot + probe) % TRACK_KEYS;
        if (track->state[i].key[0] == '\0') {
            return NULL;
        }
        if (strcmp(track->state[i].key, key) == 0) {
            return track->state[i].value;
        }
    }
    return NULL;
}

// A float as the track writes it: its IEEE single's bits, as "0x%08x" (no
// decimal rounding, so every host's C library writes the same line).
static unsigned vt_f32(float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return (unsigned) bits;
}

static uint32_t rgba(GXColor c)
{
    return ((uint32_t) c.r << 24) | ((uint32_t) c.g << 16) |
           ((uint32_t) c.b << 8) | (uint32_t) c.a;
}

void msl_viewer_track_tex(HSD_GObj* fighter, int slot, float index)
{
    MslViewerTrack* track = msl_core_context_viewer_track;
    Fighter* fp;

    if (track == NULL || fighter == NULL) {
        return;
    }
    fp = GET_FIGHTER(fighter);
    put(track, "[%d,\"tex\",%d,%d,%d,\"0x%08x\"]\n", hook_frame(),
        (int) fp->player_id, fp->x221F_b4 ? 1 : 0, slot, vt_f32(index));
}

// [frame,"tint",port,sub,slot,index,rgba,flags,light rgba,light x,light yz]
// while a color overlay runs; ",0" when it stops.
static void track_overlay(MslViewerTrack* track, int frame, int port, int sub,
                          int slot, const ColorOverlay* co)
{
    char key[TRACK_KEY_LEN];
    char value[TRACK_VALUE_LEN];
    int index = co->x28_colanim.i;
    uint8_t flags;

    snprintf(key, sizeof(key), "\"tint\",%d,%d,%d", port, sub, slot);
    if (index == 0) {
        const char* last = last_state(track, key);
        if (last == NULL) {
            return;
        }
        emit_state(track, frame, key, ",0");
        return;
    }
    // The x7C byte, most significant bit first as declared.
    flags = (uint8_t) ((co->x7C_color_enable << 7) | (co->x7C_flag2 << 6) |
                       (co->x7C_light_enable << 5) | (co->x7C_flag4 << 4) |
                       (co->x7C_flag5 << 3) | (co->x7C_flag6 << 2) |
                       (co->x7C_flag7 << 1) | co->x7C_flag8);
    snprintf(value, sizeof(value), ",%d,%u,%u,%u,\"0x%08x\",\"0x%08x\"", index,
             (unsigned) rgba(co->x2C_hex), (unsigned) flags,
             (unsigned) rgba(co->x50_light_color), vt_f32(co->x74_light_rot_x),
             vt_f32(co->x78_light_rot_yz));
    emit_state(track, frame, key, value);
}

static void track_charge(MslViewerTrack* track, int frame, int port, int sub,
                         const char* field, long value)
{
    char key[TRACK_KEY_LEN];
    char text[32];
    snprintf(key, sizeof(key), "\"charge\",%d,%d,\"%s\"", port, sub, field);
    snprintf(text, sizeof(text), ",%ld", value);
    emit_state(track, frame, key, text);
}

// The draw offset ftCommon_8008021C stores for ftLib_80087074 (hit shake,
// the x2224_b5 wobble, and the smash-charge shake). The hosted build skips
// that render-side function, so the same sum is taken here into locals.
static bool track_shake(Fighter* fp, Vec3* out)
{
    Vec2 shift;
    float x = 0.0F;
    float y = 0.0F;

    if (Fighter_804D6530 != NULL && ftCo_80090690(fp, &shift) != NULL) {
        x += shift.x;
        y += shift.y;
    }
    if (fp->x2224_b5 && Fighter_804D652C != NULL) {
        x += Fighter_804D652C->x0[fp->x1A52].x;
        y += Fighter_804D652C->x0[fp->x1A52].y;
    }
    if (Fighter_804D6528 != NULL && ftCo_800DEEE8(fp, &shift) != NULL) {
        x += shift.x;
        y += shift.y;
    }
    out->x = x;
    out->y = y;
    out->z = 0.0F;
    return x != 0.0F || y != 0.0F;
}

static void track_fighter(MslViewerTrack* track, int frame,
                          Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int port = fp->player_id;
    int sub = fp->x221F_b4 ? 1 : 0;
    char key[TRACK_KEY_LEN];
    char value[TRACK_VALUE_LEN];
    Vec3 shake;
    int i;

    track_overlay(track, frame, port, sub, 0, &fp->x408);
    track_overlay(track, frame, port, sub, 1, &fp->x488);
    track_overlay(track, frame, port, sub, 2, &fp->x508);

    switch (fp->kind) {
    case FTKIND_DONKEY:
        track_charge(track, frame, port, sub, "dk", fp->fv.dk.x222C);
        break;
    case FTKIND_SAMUS:
        track_charge(track, frame, port, sub, "ss", fp->fv.ss.x2230);
        break;
    case FTKIND_MEWTWO:
        track_charge(track, frame, port, sub, "mt",
                     fp->fv.mt.x2234_shadowBallCharge);
        break;
    case FTKIND_SEAK:
        track_charge(track, frame, port, sub, "sk", fp->fv.sk.x0);
        break;
    case FTKIND_GAMEWATCH:
        track_charge(track, frame, port, sub, "gw",
                     fp->fv.gw.x2238_panicCharge);
        break;
    case FTKIND_KIRBY:
        track_charge(track, frame, port, sub, "kb.kind",
                     (long) fp->fv.kb.hat.kind);
        track_charge(track, frame, port, sub, "kb.dk", fp->fv.kb.xBC);
        track_charge(track, frame, port, sub, "kb.ss", fp->fv.kb.xA8);
        break;
    default:
        break;
    }

    // Model part variants as shown (ftParts_80074A4C followed by the apply,
    // or the script's ftParts_80074B0C).
    for (i = 0; i < fp->x5AC.model_num && i < 12; ++i) {
        snprintf(key, sizeof(key), "\"part\",%d,%d,%d", port, sub, i);
        snprintf(value, sizeof(value), ",%d", (int) fp->x5F4_arr[i].idx);
        emit_state(track, frame, key, value);
    }

    // The animation as the game plays it: its index in the subaction table,
    // whose table (the fighter's own kind, or a thrower's), the frame, the
    // rate, and the blend into it (blend frames, and how far in:
    // ftAnim_8006EBA4, ftAnim_8006FE9C).
    snprintf(key, sizeof(key), "\"anim\",%d,%d", port, sub);
    snprintf(value, sizeof(value),
             ",%d,%d,\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
             (int) fp->anim_id, (int) Fighter_AnimSourceKind(fp),
             vt_f32(fp->cur_anim_frame), vt_f32(fp->frame_speed_mul),
             vt_f32(fp->x8A4_animBlendFrames), vt_f32(fp->x8A8_anim_frame));
    emit_state(track, frame, key, value);

    // DeadUpStar/DeadUpFall (Slippi action states 4-10) or off the plane:
    // the top joint's place, turn and size.
    if ((fp->motion_id >= 4 && fp->motion_id <= 10) || fp->cur_pos.z != 0.0F) {
        HSD_JObj* top = fp->parts != NULL ? fp->parts[FtPart_TopN].joint
                                          : NULL;
        if (top != NULL) {
            snprintf(key, sizeof(key), "\"pos\",%d,%d", port, sub);
            snprintf(value, sizeof(value),
                     ",\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\","
                     "\"0x%08x\",\"0x%08x\",\"0x%08x\"",
                     vt_f32(top->translate.x), vt_f32(top->translate.y),
                     vt_f32(top->translate.z), vt_f32(top->rotate.x),
                     vt_f32(top->rotate.y), vt_f32(top->rotate.z),
                     vt_f32(top->scale.x));
            emit_state(track, frame, key, value);
        }
    }

    snprintf(key, sizeof(key), "\"shake\",%d,%d", port, sub);
    if (track_shake(fp, &shake)) {
        snprintf(value, sizeof(value),
                 ",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
                 vt_f32(shake.x), vt_f32(shake.y), vt_f32(shake.z));
        emit_state(track, frame, key, value);
    } else if (last_state(track, key) != NULL) {
        emit_state(track, frame, key, ",0,0,0");
    }

    snprintf(key, sizeof(key), "\"hide\",%d,%d", port, sub);
    snprintf(value, sizeof(value), ",%d",
             (fp->invisible ? 1 : 0) | (fp->x221E_b5 ? 2 : 0) |
                 (fp->x2226_b5 ? 4 : 0));
    if (strcmp(value, ",0") != 0 || last_state(track, key) != NULL) {
        emit_state(track, frame, key, value);
    }
}

static void track_stage(MslViewerTrack* track, int frame,
                        const MslCoreMatch* match)
{
    char key[TRACK_KEY_LEN];
    char value[TRACK_VALUE_LEN];
    Vec3 eye;
    Vec3 interest;
    float fov;
    Vec2 translation;
    u8 bg[3];
    int i;

    // Battlefield / Final Destination map part 3.
    if (match->config.stage_id == TRACK_STAGE_BATTLEFIELD ||
        match->config.stage_id == TRACK_STAGE_FINAL_DESTINATION)
    {
        HSD_GObj* gobj = Ground_801C2BA4(3);
        if (gobj != NULL) {
            Ground* gp = GET_GROUND(gobj);
            if (match->config.stage_id == TRACK_STAGE_BATTLEFIELD) {
                snprintf(value, sizeof(value), ",%d,%d,%d",
                         gp->u.battle.bg_state, gp->u.battle.curr_bg,
                         gp->u.battle.prev_bg);
                emit_state(track, frame, "\"bg\"", value);
            } else {
                float frames = gp->u.map.xC4_b27 ? 0.0F : gp->u.map.x104;
                snprintf(value, sizeof(value),
                         ",%d,\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
                         (int) gp->u.map.xC4_b2_25, vt_f32(gp->u.map.xE4),
                         vt_f32(gp->u.map.xE8), vt_f32(gp->u.map.xEC),
                         vt_f32(frames > 0.0F ? frames : 0.0F));
                emit_state(track, frame, "\"fd\"", value);
                // Part 7's root turns while the background is in phases 9
                // and 10 (grLast_8021C40C and its animation).
                if (gp->u.map.xC4_b2_25 == 9 || gp->u.map.xC4_b2_25 == 10) {
                    HSD_GObj* part = Ground_801C2BA4(7);
                    HSD_JObj* root = part != NULL ? part->hsd_obj : NULL;
                    if (root != NULL) {
                        snprintf(value, sizeof(value),
                                 ",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
                                 vt_f32(root->rotate.x), vt_f32(root->rotate.y),
                                 vt_f32(root->rotate.z));
                        emit_state(track, frame, "\"fdrot\"", value);
                    }
                }
            }
        }
    }

    // Fountain of Dreams: how high each platform stands, left then right -
    // each water column's own height (grIzumi_801CC358's xD0: the file's
    // units, what its joint is stretched by and the platform's joint set to).
    // A replay says it from Slippi 3.18 on; the game's own is here for all.
    // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    if (match->config.stage_id == TRACK_STAGE_FOUNTAIN &&
        match->gobj.entities != NULL)
    {
        float h[2] = { 0.0F, 0.0F };
        int have = 0;
        HSD_GObj* gobj;
        for (gobj = (HSD_GObj*) match->gobj.entities->x14; gobj != NULL;
             gobj = gobj->next)
        {
            Ground* gp;
            if (gobj->classifier != HSD_GOBJ_CLASS_STAGE ||
                gobj->user_data == NULL)
            {
                continue;
            }
            gp = GET_GROUND(gobj);
            if ((int) gp->map_id == 4 &&
                (gp->gv.izumi3.xC8 == 0 || gp->gv.izumi3.xC8 == 1))
            {
                h[gp->gv.izumi3.xC8] = gp->gv.izumi3.xD0;
                have |= 1 << gp->gv.izumi3.xC8;
            }
        }
        if (have == 3) {
            snprintf(value, sizeof(value),
                     ",\"0x%08x\",\"0x%08x\"",
                     vt_f32(h[0]), vt_f32(h[1]));
            emit_state(track, frame, "\"fod\"", value);
        }
    }

    // Stage material color scripts (grMaterial_801C9604) while they run.
    for (i = 0; i < 64; ++i) {
        HSD_GObj* gobj = Ground_801C2BA4(i);
        Ground* gp;
        uint32_t color = 0;
        if (gobj == NULL) {
            continue;
        }
        gp = GET_GROUND(gobj);
        if (gp->x10_flags.b4 && gp->color_overlay.x7C_color_enable) {
            ColorOverlay* co = &gp->color_overlay;
            GXColor c;
            c.r = (u8) co->x30_color_red;
            c.g = (u8) co->x34_color_green;
            c.b = (u8) co->x38_color_blue;
            c.a = (u8) co->x3C_color_alpha;
            color = rgba(c);
        }
        snprintf(key, sizeof(key), "\"gcolor\",%d", (int) gp->map_id);
        snprintf(value, sizeof(value), ",%u", (unsigned) color);
        if (color != 0 || last_state(track, key) != NULL) {
            emit_state(track, frame, key, value);
        }
    }

    msl_camera_get_render_transform(&eye, &interest, &fov);
    msl_camera_track_extras(&translation, bg);
    snprintf(value, sizeof(value), ",%u",
             ((unsigned) bg[0] << 24) | ((unsigned) bg[1] << 16) |
                 ((unsigned) bg[2] << 8) | 0xFFU);
    emit_state(track, frame, "\"bgcolor\"", value);
    // The render transform includes the quake translation; the unshaken
    // place and the translation are written apart.
    snprintf(value, sizeof(value),
             ",\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\","
             "\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
             vt_f32(eye.x - translation.x), vt_f32(eye.y - translation.y),
             vt_f32(eye.z), vt_f32(interest.x - translation.x),
             vt_f32(interest.y - translation.y), vt_f32(interest.z),
             vt_f32(fov), vt_f32(translation.x), vt_f32(translation.y));
    emit_state(track, frame, "\"cam\"", value);
}

// Owner port and sub (0 leader, 1 Nana) of an item whose owner is a fighter.
static bool item_owner_port(Item* ip, int* port, int* sub)
{
    if (ip->owner == NULL || ip->owner->classifier != HSD_GOBJ_CLASS_FIGHTER) {
        return false;
    }
    *port = GET_FIGHTER(ip->owner)->player_id;
    *sub = GET_FIGHTER(ip->owner)->x221F_b4 ? 1 : 0;
    return true;
}

// A chain as [x, y] pairs: `head` first when given, then each link from
// `start` along next (`along_next`) or prev, then `tail` when given. Points
// still at the origin (not out yet) are left out, and a chain with none
// left writes no event.
static void track_chain(MslViewerTrack* track, int frame, Item* ip,
                        const Vec3* head, ItemLink* start, bool along_next,
                        const Vec3* tail)
{
    enum { MAX_LINKS = 64 };
    char key[TRACK_KEY_LEN];
    char value[64 * 2 * 14 + 64];
    size_t used = 0;
    int port;
    int sub;
    int count = 0;
    ItemLink* link;

    if (!item_owner_port(ip, &port, &sub)) {
        port = -1;
        sub = 0;
    }
    if (head != NULL && (head->x != 0.0F || head->y != 0.0F)) {
        used += (size_t) snprintf(value + used, sizeof(value) - used,
                                  ",\"0x%08x\",\"0x%08x\"",
                                  vt_f32(head->x), vt_f32(head->y));
    }
    for (link = start; link != NULL && count < MAX_LINKS;
         link = along_next ? link->next : link->prev, ++count)
    {
        if (&link->pos == head || &link->pos == tail ||
            (link->pos.x == 0.0F && link->pos.y == 0.0F))
        {
            continue;
        }
        used += (size_t) snprintf(value + used, sizeof(value) - used,
                                  ",\"0x%08x\",\"0x%08x\"",
                                  vt_f32(link->pos.x), vt_f32(link->pos.y));
        if (used >= sizeof(value) - 64) {
            break;
        }
    }
    if (tail != NULL && (tail->x != 0.0F || tail->y != 0.0F)) {
        used += (size_t) snprintf(value + used, sizeof(value) - used,
                                  ",\"0x%08x\",\"0x%08x\"",
                                  vt_f32(tail->x), vt_f32(tail->y));
    }
    if (used == 0) {
        return;
    }
    snprintf(key, sizeof(key), "\"hook\",%d,%d", port, sub);
    put(track, "[%d,%s%s]\n", frame, key, value);
}

static void track_items(MslViewerTrack* track, int frame,
                        const MslCoreMatch* match)
{
    Item_GObj* gobj = (Item_GObj*) match->gobj.entities->items;
    char key[TRACK_KEY_LEN];
    char value[TRACK_VALUE_LEN];

    while (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        HSD_JObj* jobj = GET_JOBJ(gobj);

        switch (ip->kind) {
        case It_Kind_Link_HShot:
        case It_Kind_CLink_HShot: {
            // The hook is the last link (xDD4+0); the item itself stays at
            // its spawn point. Head first, then back toward the hand.
            ItemLink* hook = ip->xDD4_itemVar.linkhookshot.x0;
            if (hook != NULL) {
                track_chain(track, frame, ip, &hook->pos, hook, true, NULL);
            }
            break;
        }
        case It_Kind_Samus_GBeam: {
            // The claw is the last link (xDD4+0), which the grapple moves and
            // collides; the other links follow joint animation the headless
            // core does not run, so only the ones that have moved appear.
            ItemLink* claw = ip->xDD4_itemVar.samusgrapple.x0;
            if (claw != NULL) {
                track_chain(track, frame, ip, &claw->pos, claw, true, NULL);
            }
            break;
        }
        case It_Kind_Ness_Yoyo: {
            // Ness's yo-yo: the string's links from the hand (xDD4+C) toward
            // the ball, then the ball (xDD4+8, the link the yo-yo's hitbox
            // position is written to) last. The item itself stays at its
            // spawn point.
            ItemLink* ball = ip->xDD4_itemVar.nessyoyo.x8;
            track_chain(track, frame, ip, NULL, ip->xDD4_itemVar.nessyoyo.xC,
                        false, ball != NULL ? &ball->pos : NULL);
            break;
        }
        default:
            break;
        }
        // Which of its owner's articles (ftData x48_items, the list each
        // fighter's OnLoad registers its items' kinds from: it_8026B3F8)
        // this kind of item is drawn with, once per kind: [frame,"art",kind,
        // owner's character, index], -1 when it is none of them (a Kirby
        // copy's, an item of the stage's).
        snprintf(key, sizeof(key), "\"art\",%d", (int) ip->kind);
        if (last_state(track, key) == NULL) {
            int found = -1;
            int character = -1;
            if (ip->owner != NULL &&
                ip->owner->classifier == HSD_GOBJ_CLASS_FIGHTER)
            {
                Fighter* fp = GET_FIGHTER(ip->owner);
                Article** items = fp->ft_data != NULL
                                      ? (Article**) fp->ft_data->x48_items
                                      : NULL;
                int k;
                character = (int) fp->kind;
                for (k = 0; items != NULL && k < 16; ++k) {
                    if (items[k] == ip->xC4_article_data) {
                        found = k;
                        break;
                    }
                }
            }
            snprintf(value, sizeof(value), ",%d,%d", character, found);
            emit_state(track, frame, key, value);
        }
        // The root joint's Euler rotation, on change (spinning throws,
        // turnips, bombs).
        if (jobj != NULL && !(jobj->flags & JOBJ_USE_QUATERNION)) {
            snprintf(key, sizeof(key), "\"irot\",%d", (int) ip->x1C);
            snprintf(value, sizeof(value),
                     ",\"0x%08x\",\"0x%08x\",\"0x%08x\"",
                     vt_f32(jobj->rotate.x), vt_f32(jobj->rotate.y),
                     vt_f32(jobj->rotate.z));
            if (last_state(track, key) != NULL || jobj->rotate.x != 0.0F ||
                jobj->rotate.y != 0.0F || jobj->rotate.z != 0.0F)
            {
                emit_state(track, frame, key, value);
            }
        }
        gobj = (Item_GObj*) gobj->next;
    }
}

// Poses: the joints whose pose the game's own code sets apart from an
// animation a viewer plays itself, as that code left them at the frame's end,
// so a viewer can draw them as the game does. Each is written only when it
// changes ("mask" bit k says which values follow, in order).
//
//   [frame,"jp",item,(joint,mask,values)...]
//       an item's joints below its root (the root is the item's place and
//       "irot"), by preorder index in its model as the file lays it out (an
//       instance's children counted): rot x, y, z, scale x, y, z, translate
//       x, y, z (mask bits 0-8). A joint is written once it differs from the
//       file's rest pose, and then whenever it changes.
//   [frame,"fp",port,sub,character,(part,mask,values)...]
//       a fighter's swinging joints (ftdynamics.c): the chains a hurtbox sits
//       on, which gameplay solves (Fox's tail, Mewtwo's, Jigglypuff's), and
//       with the track the capes, caps and hair the gameplay build drops
//       (msl_viewer_track_keep_dynamics), by part, the same nine values. A
//       fighter's first line, and its first as another character, has every
//       value.
//   [frame,"lm",item,links,(link,out,mask,values)...]
//       a chain's links (Link's and Young Link's hookshots, Samus's grapple,
//       Sheik's chain, Ness's yo-yo, the Ice Climbers' belay): each link's
//       matrix as the item's code set it (it_802A6DC8, it_802A6F80: its turn
//       along the chain, its place and size), rows 0-2 of the 3x4 (mask bits
//       0-11).
//       Link 0 is the end with no prev, `links` the chain's length; out is
//       the link's x2C_b0 (it_802A24A0 draws a link only then). A link
//       coming out or going back is written whole.
// refs/melee/src/melee/{ft/ftdynamics.c,it/items/itlinkhookshot.c,
//   it/items/itseakchain.c,it/items/itnessyoyo.c,
//   it/items/itclimbersstring.c}

// The owner of a pose, kept while it is there.
static MslTrackPose* pose_owner(MslViewerTrack* track, char kind, int id,
                                int frame)
{
    int i;
    int free_at = -1;
    for (i = 0; i < TRACK_POSE_OWNERS; ++i) {
        MslTrackPose* o = &track->pose[i];
        if (o->used && o->kind == kind && o->id == id) {
            o->frame = frame;
            return o;
        }
        if (!o->used && free_at < 0) {
            free_at = i;
        }
    }
    if (free_at < 0) {
        return NULL;
    }
    {
        MslTrackPose* o = &track->pose[free_at];
        memset(o, 0, sizeof(*o));
        o->used = 1;
        o->kind = kind;
        o->id = id;
        o->frame = frame;
        return o;
    }
}

static void pose_append(MslViewerTrack* track, const char* format, ...)
{
    size_t room = sizeof(track->pose_line) - track->pose_used;
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(track->pose_line + track->pose_used, room, format, args);
    va_end(args);
    if (n < 0 || (size_t) n >= room) {
        fprintf(stderr, "viewer track: pose line too long\n");
        abort();
    }
    track->pose_used += (size_t) n;
}

// One joint against its last written values: ",joint[extra],mask,v..." when
// any differs bit for bit (or none was written), and they are kept.
static void pose_joint(MslViewerTrack* track, MslTrackPose* o, int joint,
                       int extra, const float* v, int n)
{
    unsigned mask = 0;
    int k;
    if (joint < 0 || joint >= TRACK_POSE_JOINTS) {
        return;
    }
    for (k = 0; k < n; ++k) {
        if (!o->known[joint] || vt_f32(v[k]) != vt_f32(o->last[joint][k])) {
            mask |= 1U << k;
        }
    }
    if (mask == 0) {
        return;
    }
    if (extra >= 0) {
        pose_append(track, ",%d,%d,%u", joint, extra, mask);
    } else {
        pose_append(track, ",%d,%u", joint, mask);
    }
    for (k = 0; k < n; ++k) {
        if (mask & (1U << k)) {
            pose_append(track, ",\"0x%08x\"", vt_f32(v[k]));
        }
        o->last[joint][k] = v[k];
    }
    o->known[joint] = 1;
}

// The owner's line, when it has any joint.
static void pose_flush(MslViewerTrack* track, const char* head)
{
    if (track->pose_used > 0) {
        put(track, "%s%s]\n", head, track->pose_line);
    }
    track->pose_used = 0;
    track->pose_line[0] = '\0';
}

static void joint_values(const HSD_JObj* j, float v[9])
{
    v[0] = j->rotate.x;
    v[1] = j->rotate.y;
    v[2] = j->rotate.z;
    v[3] = j->scale.x;
    v[4] = j->scale.y;
    v[5] = j->scale.z;
    v[6] = j->translate.x;
    v[7] = j->translate.y;
    v[8] = j->translate.z;
}

// An item's model and its file's joints together, in the file's preorder.
// A joint the file has and the model does not (an instance's children) is
// counted and left out.
static void pose_item_walk(MslViewerTrack* track, MslTrackPose* o,
                           HSD_JObj* j, HSD_Joint* d, int* index)
{
    for (; d != NULL; d = d->next, j = j != NULL ? j->next : NULL) {
        int at = (*index)++;
        if (at > 0 && at < TRACK_POSE_JOINTS && j != NULL &&
            !(j->flags & JOBJ_USE_QUATERNION))
        {
            float v[9];
            if (!o->known[at]) {
                // (the file's rest pose: what a viewer has without a line)
                o->last[at][0] = d->rotation.x;
                o->last[at][1] = d->rotation.y;
                o->last[at][2] = d->rotation.z;
                o->last[at][3] = d->scale.x;
                o->last[at][4] = d->scale.y;
                o->last[at][5] = d->scale.z;
                o->last[at][6] = d->position.x;
                o->last[at][7] = d->position.y;
                o->last[at][8] = d->position.z;
                o->known[at] = 1;
            }
            joint_values(j, v);
            pose_joint(track, o, at, -1, v, 9);
        }
        if (d->child != NULL) {
            pose_item_walk(track, o,
                           j != NULL && !(j->flags & JOBJ_INSTANCE) ? j->child
                                                                    : NULL,
                           d->child, index);
        }
    }
}

// A chain's links, from the end with no prev along next.
static void pose_links(MslViewerTrack* track, int frame, Item* ip,
                       ItemLink* any)
{
    MslTrackPose* o;
    ItemLink* link = any;
    char head[64];
    int n;
    if (link == NULL) {
        return;
    }
    for (n = 0; link->prev != NULL && n < TRACK_POSE_JOINTS; ++n) {
        link = link->prev;
    }
    o = pose_owner(track, 'l', (int) ip->x1C, frame);
    if (o == NULL) {
        return;
    }
    for (n = 0; link != NULL && n < TRACK_POSE_JOINTS; link = link->next, ++n)
    {
        HSD_JObj* j = link->gobj != NULL ? link->gobj->hsd_obj : NULL;
        float v[12];
        int r;
        if (j == NULL) {
            continue;
        }
        for (r = 0; r < 3; ++r) {
            v[r * 4 + 0] = j->mtx[r][0];
            v[r * 4 + 1] = j->mtx[r][1];
            v[r * 4 + 2] = j->mtx[r][2];
            v[r * 4 + 3] = j->mtx[r][3];
        }
        if (o->known[n] && o->flag[n] != link->x2C_b0) {
            o->known[n] = 0;
        }
        o->flag[n] = link->x2C_b0;
        pose_joint(track, o, n, link->x2C_b0 ? 1 : 0, v, 12);
    }
    snprintf(head, sizeof(head), "[%d,\"lm\",%d,%d", frame, (int) ip->x1C,
             n);
    pose_flush(track, head);
}

static void track_item_poses(MslViewerTrack* track, int frame,
                             const MslCoreMatch* match)
{
    Item_GObj* gobj = (Item_GObj*) match->gobj.entities->items;
    char head[64];
    for (; gobj != NULL; gobj = (Item_GObj*) gobj->next) {
        Item* ip = GET_ITEM(gobj);
        HSD_JObj* root = GET_JOBJ(gobj);
        if (root != NULL && ip->xC8_joint != NULL) {
            MslTrackPose* o = pose_owner(track, 'i', (int) ip->x1C, frame);
            int index = 0;
            if (o != NULL) {
                pose_item_walk(track, o, root, ip->xC8_joint, &index);
                snprintf(head, sizeof(head), "[%d,\"jp\",%d", frame,
                         (int) ip->x1C);
                pose_flush(track, head);
            }
        }
        switch (ip->kind) {
        case It_Kind_Link_HShot:
        case It_Kind_CLink_HShot:
            pose_links(track, frame, ip, ip->xDD4_itemVar.linkhookshot.x0);
            break;
        case It_Kind_Samus_GBeam:
            pose_links(track, frame, ip, ip->xDD4_itemVar.samusgrapple.x0);
            break;
        case It_Kind_Seak_Chain:
            pose_links(track, frame, ip, ip->xDD4_itemVar.seakchain.x4);
            break;
        case It_Kind_Ness_Yoyo:
            pose_links(track, frame, ip, ip->xDD4_itemVar.nessyoyo.x8);
            break;
        case It_Kind_IceClimber_GumStrings:
            pose_links(track, frame, ip, ip->xDD4_itemVar.climbersstring.x8);
            break;
        default:
            break;
        }
    }
}

static void track_fighter_poses(MslViewerTrack* track, int frame,
                                Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int port = fp->player_id;
    int sub = fp->x221F_b4 ? 1 : 0;
    MslTrackPose* o;
    char head[64];
    int i;
    if (fp->parts == NULL || fp->dynamics_num <= 0) {
        return;
    }
    o = pose_owner(track, 'f', port * 2 + sub, frame);
    if (o == NULL) {
        return;
    }
    if (o->character != (int) fp->kind + 1) {
        memset(o->known, 0, sizeof(o->known));
        o->character = (int) fp->kind + 1;
    }
    for (i = 0; i < (int) fp->dynamics_num; ++i) {
        struct DynamicsData* link = fp->dynamic_bone_sets[i].dyn_desc.data;
        for (; link != NULL; link = link->next) {
            HSD_JObj* j = link->desc.lb_unk0.jobj;
            u32 part;
            if (j == NULL || (j->flags & JOBJ_USE_QUATERNION)) {
                continue;
            }
            for (part = 0; part < ftPartsTable[fp->kind]->parts_num; ++part)
            {
                if (fp->parts[part].joint == j) {
                    float v[9];
                    joint_values(j, v);
                    pose_joint(track, o, (int) part, -1, v, 9);
                    break;
                }
            }
        }
    }
    snprintf(head, sizeof(head), "[%d,\"fp\",%d,%d,%d", frame, port, sub,
             (int) fp->kind);
    pose_flush(track, head);
}

// The chains are copied before the prune (it puts the dropped ones' links
// back on the pool's free list); a copy goes in where the prune left a set
// empty. The pool ends as the prune leaves it, as without the track.
void msl_viewer_track_keep_dynamics(Fighter* fp)
{
    struct DynamicsData* copies[Ft_Dynamics_NumMax];
    int count = fp->dynamics_num < Ft_Dynamics_NumMax ? (int) fp->dynamics_num
                                                    : Ft_Dynamics_NumMax;
    int i;
    // (not yet for Peach's and Zelda's dresses and hair and Donkey Kong's
    // tie: solved this way they came out further from the game's own draws
    // than a viewer's solve, the cause not yet found; their chains are pruned
    // as without the track)
    if (fp->kind == FTKIND_PEACH || fp->kind == FTKIND_ZELDA ||
        fp->kind == FTKIND_DONKEY)
    {
        ftCo_HeadlessPruneDynamics(fp);
        return;
    }
    for (i = 0; i < count; ++i) {
        copies[i] = msl_viewer_track_copy_dynamics(
            fp->dynamic_bone_sets[i].dyn_desc.data);
    }
    ftCo_HeadlessPruneDynamics(fp);
    for (i = 0; i < count; ++i) {
        DynamicsDesc* desc = &fp->dynamic_bone_sets[i].dyn_desc;
        if (desc->data == NULL && copies[i] != NULL) {
            desc->data = copies[i];
        }
    }
}

static void track_poses(MslViewerTrack* track, int frame,
                        const MslCoreMatch* match)
{
    int i;
    for (i = 0; i < match->config.num_players; ++i) {
        if (match->fighters[i] != NULL) {
            track_fighter_poses(track, frame, match->fighters[i]);
        }
        if (match->follower_fighters[i] != NULL &&
            !GET_FIGHTER(match->follower_fighters[i])->x221F_b3)
        {
            track_fighter_poses(track, frame, match->follower_fighters[i]);
        }
    }
    track_item_poses(track, frame, match);
    // (an owner not seen this frame is gone: its place is free)
    for (i = 0; i < TRACK_POSE_OWNERS; ++i) {
        if (track->pose[i].used && track->pose[i].frame != frame) {
            track->pose[i].used = 0;
        }
    }
}

void msl_viewer_track_frame(MslCoreMatch* match)
{
    MslViewerTrack* track = match->viewer_track;
    int frame;
    int i;

    if (track == NULL) {
        return;
    }
    frame = match->frame_id;
    for (i = 0; i < match->config.num_players; ++i) {
        if (match->fighters[i] != NULL) {
            track_fighter(track, frame, match->fighters[i]);
        }
        if (match->follower_fighters[i] != NULL &&
            !GET_FIGHTER(match->follower_fighters[i])->x221F_b3)
        {
            track_fighter(track, frame, match->follower_fighters[i]);
        }
    }
    track_stage(track, frame, match);
    track_items(track, frame, match);
    track_poses(track, frame, match);
}
#endif // MSL_VISUAL_TRACK_BUILD
