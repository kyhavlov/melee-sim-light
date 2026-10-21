#if MSL_VISUAL_TRACK_BUILD
#include "runtime/viewer_track.h"

#include "ft/fighter.h"
#include "ft/chara/ftCommon/ftCo_DamageFall.h"
#include "ft/ft_0DF0.h"
#include "ft/ftlib.h"
#include "ft/types.h"
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

// A state is written only when it changes: the last value of each key (its
// kind and identity fields) is kept.
enum { TRACK_KEYS = 2048, TRACK_KEY_LEN = 48, TRACK_VALUE_LEN = 200 };


struct MslViewerTrack {
    // Lines since the last msl_core_match_viewer_track.
    char* text;
    size_t size;
    size_t capacity;
    struct {
        char key[TRACK_KEY_LEN];
        char value[TRACK_VALUE_LEN];
    } state[TRACK_KEYS];
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
}
#endif // MSL_VISUAL_TRACK_BUILD
