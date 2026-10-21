#if MSL_VISUAL_TRACK_BUILD
#ifndef MSL_RUNTIME_EFFECT_PIECES_H
#define MSL_RUNTIME_EFFECT_PIECES_H

// Viewer track only: each spawn run through the game's own effect dispatch
// with stand-ins for what it would make (runtime/effect_pieces.c), each
// piece written as an "fxp" event. Reads simulation state, never writes it.

#include <stdarg.h>

#include <platform.h>
#include <baselib/forward.h>
#include <dolphin/mtx.h>

typedef struct MslEffectPiece {
    int index;            // its place among its spawn's pieces
    char kind;            // 'm' an effect model, 'g' a particle generator
    s32 id;               // bank * 1000 + its index in the bank
    const char* how;      // "place", "attach", "child", "create", "carrier",
                          // "joint", "joint-rot", "joint-srt", "srt": the
                          // creation call
    HSD_JObj* joint;      // the joint it was made on (or NULL)
    Vec3 t;               // its translation (a model's, a generator's place
                          // or app SRT translation)
    Vec3 r;               // its rotation, as the dispatch set it
    Vec3 s;               // its scale
    const char* callback; // the update callback set on a model ("" none)
    int lifetime;         // a model's lifetime, when the dispatch set one
    Vec3 params;          // a model's callback parameters
    int mirrored;         // a generator mirrored to face left
    Vec3 at;              // where it was made: its place, or its joint's
                          // world position as it was made
    u32 rng_at;           // the random state as it was made (a generator's
                          // first count, a model's animation's generators)
} MslEffectPiece;

// A spawn efSync_Spawn makes now, with its own arguments.
void msl_effect_pieces(s32 gfx_id, HSD_GObj* gobj, va_list args);
// A queued efAsync_Spawn as efAsync_QueueProcessDeferred makes it: its
// kind, joint, parameters and the two extra floats.
void msl_effect_pieces_deferred(HSD_GObj* gobj, s32 gfx_id, int spawn_kind,
                                HSD_JObj* jobj, const Vec3* params,
                                float extra1, float extra2);

#endif
#endif // MSL_VISUAL_TRACK_BUILD
