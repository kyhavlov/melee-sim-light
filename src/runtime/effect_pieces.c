#if MSL_VISUAL_TRACK_BUILD
// Viewer track only: what each effect spawn made, piece by piece, as the
// game's own dispatch makes it.
//
// The headless core runs no visual effects: efSync_Spawn (runtime/effects.c)
// only draws the random numbers the game's would. A replay viewer still
// needs to know what the game drew for each spawn - which of an effect
// file's models or particle generators, on which joint or at which place,
// turned and scaled how, run on by which callback - and that is decided by
// the game's dispatch code (efsync.c, efasync.c, efalt.c), case by case. So
// that code is compiled here once more, with every efLib creation call
// renamed to a recorder that makes a stand-in object instead (its JObj or
// app SRT takes whatever the dispatch then sets on it), and each spawn the
// track writes is run through it: an "fxp" event per piece after its "fx".
//
// The run is invisible to the simulation: the random seed is put back after
// it (the recorder draws the numbers a real creation would, from the same
// starting seed, so its random choices are the game's), and it touches no
// object but its stand-ins and reads, never writes, the owner's joints.
//
// refs/melee/src/melee/ef/{efsync.c,efasync.c,efalt.c,eflib.c}

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

// --- the dispatch's calls, renamed to the recorder's -------------------------

#define efSync_Spawn msl_pieces_efSync_Spawn
#define efAsync_Dispatch msl_pieces_efAsync_Dispatch
#define efAlt_Spawn msl_pieces_efAlt_Spawn
#define efAsync_DatEntries msl_pieces_DatEntries
#define efLib_LoadKind msl_pieces_LoadKind
#define efLib_AnimCount msl_pieces_AnimCount
#define efLib_AnimQueue msl_pieces_AnimQueue
#define efLib_Create msl_pieces_Create
#define efLib_Create_Attach msl_pieces_Create_Attach
#define efLib_Create_AttachChild msl_pieces_Create_AttachChild
#define efLib_Create_Attach_Scale msl_pieces_Create_Attach_Scale
#define efLib_Create_AttachChild_Scale msl_pieces_Create_AttachChild_Scale
#define efLib_Create_Attach_Scale_FacingDir msl_pieces_Create_Attach_Scale_FacingDir
#define efLib_Create_Attach_Pos msl_pieces_Create_Attach_Pos
#define efLib_CreateGenerator msl_pieces_CreateGenerator
#define efLib_CreateGenerator_AddAppSRT msl_pieces_CreateGenerator_AddAppSRT
#define efLib_CreateGenerator_Translate_FacingDir msl_pieces_CreateGenerator_Translate_FacingDir
#define efLib_CreateGenerator_Attach msl_pieces_CreateGenerator_Attach
#define efLib_CreateGenerator_Attach_AddAppSRT msl_pieces_CreateGenerator_Attach_AddAppSRT
#define efLib_CreateGenerator_Attach_Scale msl_pieces_CreateGenerator_Attach_Scale
#define efLib_CreateGenerator_AppSRT_SetScale msl_pieces_CreateGenerator_AppSRT_SetScale
#define efLib_CreateGenerator_AppSRT_SetFacingDir msl_pieces_CreateGenerator_AppSRT_SetFacingDir
#define efLib_CreateGenerator_AppSRT_SetFacingDirScale msl_pieces_CreateGenerator_AppSRT_SetFacingDirScale
#define efLib_CreateGenerator_AppSRT_SetPos msl_pieces_CreateGenerator_AppSRT_SetPos
#define efLib_SetTevKonstColor msl_pieces_SetTevKonstColor
#define efLib_SetParamGfxId msl_pieces_SetParamGfxId
#define efLib_SetParamAlpha msl_pieces_SetParamAlpha
#define hsd_8039EFAC msl_pieces_hsd_8039EFAC
#define HSD_JObjAnimAll msl_pieces_JObjAnimAll
// the callbacks a dispatch sets: their own stand-ins, told apart by name
#define CALLBACKS(X)                                                           \
    X(efLib_Cb_SetOffsetY_FromParamY) X(efLib_Cb_SetScale_FromParamX)          \
    X(efLib_Cb_SetRotYAndTransition) X(efLib_Cb_SetJObjOffsetZ)                \
    X(efLib_Cb_SetRotY_FromFighterDir) X(efLib_Cb_SetRotYZ_FromFighter)        \
    X(efLib_Cb_Fall_FromParamY) X(efLib_Cb_SetOffset_FromParams)               \
    X(efLib_Cb_LifetimeEndSpawn) X(efLib_Cb_SetScaleRotY_FromFighter)          \
    X(efLib_Cb_SetRotYZ_FromParamZ_FighterDir) X(efLib_Cb_ftMr_SpecialLw)      \
    X(efLib_Cb_ftLg_SpecialLw) X(efLib_Cb_ftKp_SpecialHi)                      \
    X(efLib_Cb_ftCo_Bury) X(efLib_Cb_ApplyStoredAlpha)                         \
    X(efLib_Cb_AccumOffset_FromParams)
#define efLib_Cb_SetOffsetY_FromParamY msl_pieces_Cb_SetOffsetY_FromParamY
#define efLib_Cb_SetScale_FromParamX msl_pieces_Cb_SetScale_FromParamX
#define efLib_Cb_SetRotYAndTransition msl_pieces_Cb_SetRotYAndTransition
#define efLib_Cb_SetJObjOffsetZ msl_pieces_Cb_SetJObjOffsetZ
#define efLib_Cb_SetRotY_FromFighterDir msl_pieces_Cb_SetRotY_FromFighterDir
#define efLib_Cb_SetRotYZ_FromFighter msl_pieces_Cb_SetRotYZ_FromFighter
#define efLib_Cb_Fall_FromParamY msl_pieces_Cb_Fall_FromParamY
#define efLib_Cb_SetOffset_FromParams msl_pieces_Cb_SetOffset_FromParams
#define efLib_Cb_LifetimeEndSpawn msl_pieces_Cb_LifetimeEndSpawn
#define efLib_Cb_SetScaleRotY_FromFighter msl_pieces_Cb_SetScaleRotY_FromFighter
#define efLib_Cb_SetRotYZ_FromParamZ_FighterDir msl_pieces_Cb_SetRotYZ_FromParamZ_FighterDir
#define efLib_Cb_ftMr_SpecialLw msl_pieces_Cb_ftMr_SpecialLw
#define efLib_Cb_ftLg_SpecialLw msl_pieces_Cb_ftLg_SpecialLw
#define efLib_Cb_ftKp_SpecialHi msl_pieces_Cb_ftKp_SpecialHi
#define efLib_Cb_ftCo_Bury msl_pieces_Cb_ftCo_Bury
#define efLib_Cb_ApplyStoredAlpha msl_pieces_Cb_ApplyStoredAlpha
#define efLib_Cb_AccumOffset_FromParams msl_pieces_Cb_AccumOffset_FromParams

#include <math.h>
#include <placeholder.h>

#include "ef/efasync.h"
#include "ef/eflib.h"
#include "ef/efsync.h"
#include "ef/types.h"
#include "ft/fighter.h"
#include "ft/inlines.h"
#include "ft/types.h"
#include "lb/lb_00B0.h"
#include "runtime/effect_pieces.h"
#include "runtime/context.h"
#include "runtime/effects.h"
#include "runtime/scalar.h"
#include "runtime/viewer_track.h"

#include <pthread.h>

#include <baselib/gobj.h>
#include <baselib/jobj.h>
#include <baselib/particle.h>
#include <baselib/psstructs.h>
#include <baselib/random.h>

void* efAlt_Spawn(s32 gfx_id, HSD_GObj* gobj, va_list vlist_arg);

volatile u32 efLib_LoadKind;
volatile s32 efLib_AnimCount;
EF_ParamEntry efLib_AnimQueue[0x10];
// the effect files this match loaded, as the dispatch asks (only whether
// Mario's is: efsync.c 0x479)
static char msl_pieces_loaded;
EF_DAT_Entry efAsync_DatEntries[51];

// the random numbers a real creation draws (runtime/effects.c)
void msl_effect_pieces_consume_generator(s32 generator_id);
void msl_effect_pieces_consume_model(int model_id);

// the effect files the game would have loaded: the common one and each of
// the match's fighters' (ftData_UnkBytePerCharacter, which their loaders
// pass to efAsync_LoadSync). Every bank is loaded up front here, but the
// dispatch asks whether the game would have Mario's: efsync.c 0x479 is
// Luigi's fireball (0x506) without it.
extern u8 ftData_UnkBytePerCharacter[];
static unsigned char msl_pieces_banks[51];

static void loaded_banks(void)
{
    MslCoreMatch* match = msl_core_active_match();
    int i;

    memset(msl_pieces_banks, 0, sizeof(msl_pieces_banks));
    msl_pieces_banks[0] = 1;
    if (match == NULL) {
        return;
    }
    for (i = 0; i < match->config.num_players; ++i) {
        HSD_GObj* both[2] = { match->fighters[i], match->follower_fighters[i] };
        int k;
        for (k = 0; k < 2; ++k) {
            int bank;
            if (both[k] == NULL) {
                continue;
            }
            bank = ftData_UnkBytePerCharacter[GET_FIGHTER(both[k])->kind];
            if (bank < (int) sizeof(msl_pieces_banks)) {
                msl_pieces_banks[bank] = 1;
            }
        }
    }
}

// --- the stand-ins ------------------------------------------------------------

enum { PIECES = 16, KIDS = 8 };

typedef struct Piece {
    u32 rng_at;
    char kind;           // 'm' a model, 'g' a particle generator
    s32 id;              // bank * 1000 + its index
    const char* how;     // how it was made: the creation call
    HSD_JObj* joint;     // the joint it was made on, if any
    Vec3 at;             // where it was made, when it was given a place
    EF_Effect effect;
    HSD_GObj gobj;
    HSD_JObj jobj;
    HSD_JObj kids[KIDS];
    HSD_Generator gen;
    HSD_psAppSRT srt;
} Piece;

static Piece pieces[PIECES];
static int piece_count;

static Piece* piece(char kind, s32 id, const char* how, HSD_JObj* joint)
{
    Piece* p;
    int i;

    if (piece_count >= PIECES) {
        return NULL;
    }
    p = &pieces[piece_count++];
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    p->id = id;
    p->how = how;
    p->joint = joint;
    p->jobj.scale.x = p->jobj.scale.y = p->jobj.scale.z = 1.0F;
    // (a chain of children: the shield's models set colors down theirs)
    p->jobj.child = &p->kids[0];
    for (i = 0; i < KIDS; ++i) {
        p->kids[i].scale.x = p->kids[i].scale.y = p->kids[i].scale.z = 1.0F;
        p->kids[i].parent = &p->jobj;
        p->kids[i].next = i + 1 < KIDS ? &p->kids[i + 1] : NULL;
    }
    p->gobj.hsd_obj = &p->jobj;
    p->effect.gobj = &p->gobj;
    p->srt.scale.x = p->srt.scale.y = p->srt.scale.z = 1.0F;
    p->gen.appsrt = &p->srt;
    if (joint != NULL) {
        lb_8000B1CC(joint, NULL, &p->at);
    }
    return p;
}

static float root_scale_y(HSD_GObj* gobj)
{
    Vec3 scale = { 1.0F, 1.0F, 1.0F };
    if (gobj != NULL && gobj->hsd_obj != NULL) {
        HSD_JObjGetScale(GET_JOBJ(gobj), &scale);
    }
    return scale.y;
}

// --- models (eflib.c's efLib_Create*) ------------------------------------------

static EF_Effect* model(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj, const char* how)
{
    Piece* p = piece('m', (s32) gfx_id, how, jobj);
    if (p == NULL) {
        return NULL;
    }
    if (gfx_id / 1000 == 0) {
        msl_effect_pieces_consume_model((int) (gfx_id % 1000));
    }
    p->effect.parent_gobj = gobj;
    p->effect.attach_jobj = jobj;
    if (jobj != NULL) {
        p->jobj.translate = p->at;
    }
    return &p->effect;
}

static void scaled(EF_Effect* e, HSD_GObj* gobj)
{
    float s = root_scale_y(gobj);
    if (e != NULL) {
        GET_JOBJ(e->gobj)->scale.x = GET_JOBJ(e->gobj)->scale.y = GET_JOBJ(e->gobj)->scale.z = s;
    }
}

EF_Effect* efLib_Create(int gfx_id, HSD_GObj* parent_gobj)
{
    return model((u32) gfx_id, parent_gobj, NULL, "create");
}

EF_Effect* efLib_Create_Attach(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj)
{
    return model(gfx_id, gobj, jobj, "attach");
}

EF_Effect* efLib_Create_AttachChild(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj)
{
    return model(gfx_id, gobj, jobj, "child");
}

EF_Effect* efLib_Create_Attach_Scale(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj)
{
    EF_Effect* e = model(gfx_id, gobj, jobj, "attach");
    scaled(e, gobj);
    return e;
}

EF_Effect* efLib_Create_AttachChild_Scale(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj)
{
    EF_Effect* e = model(gfx_id, gobj, jobj, "child");
    scaled(e, gobj);
    return e;
}

EF_Effect* efLib_Create_Attach_Scale_FacingDir(u32 gfx_id, HSD_GObj* gobj, HSD_JObj* jobj)
{
    EF_Effect* e = model(gfx_id, gobj, jobj, "attach");
    scaled(e, gobj);
    if (e != NULL) {
        e->update = efLib_Cb_SetRotY_FromFighterDir;
    }
    return e;
}

EF_Effect* efLib_Create_Attach_Pos(u32 gfx_id, HSD_GObj* gobj, Vec3* position)
{
    EF_Effect* e = model(gfx_id, gobj, NULL, "place");
    if (e != NULL && position != NULL) {
        Piece* p = &pieces[piece_count - 1];
        p->at = *position;
        p->jobj.translate = *position;
    }
    return e;
}

// --- generators (eflib.c's efLib_CreateGenerator*, particle.c's hsd_8039EFAC) --

static HSD_Generator* generator(s32 gfx_id, HSD_JObj* jobj, const char* how)
{
    Piece* p = piece('g', gfx_id, how, jobj);
    if (p == NULL) {
        return NULL;
    }
    msl_effect_pieces_consume_generator(gfx_id);
    p->gen.bank = (u8) (gfx_id / 1000);
    p->gen.jobj = jobj;
    return &p->gen;
}

HSD_Generator* hsd_8039EFAC(s32 linkNo, s32 bank, s32 gfx_id, HSD_JObj* jobj)
{
    (void) linkNo;
    (void) bank;
    // (as the dispatch calls it itself: the generator keeps the joint's turn
    // for its emission, which efLib_CreateGenerator_Attach takes away)
    return generator(gfx_id, jobj, "joint-rot");
}

HSD_Generator* efLib_CreateGenerator(s32 gfx_id, Vec3* pos)
{
    HSD_Generator* g = generator(gfx_id, NULL, "place");
    if (g != NULL && pos != NULL) {
        g->pos = *pos;
        pieces[piece_count - 1].at = *pos;
    }
    return g;
}

HSD_Generator* efLib_CreateGenerator_AddAppSRT(s32 gfx_id)
{
    return generator(gfx_id, NULL, "srt");
}

HSD_Generator* efLib_CreateGenerator_Translate_FacingDir(s32 gfx_id, Vec3* translation, f32 direction)
{
    HSD_Generator* g = generator(gfx_id, NULL, "srt");
    if (g != NULL) {
        g->appsrt->translate = *translation;
        if (direction < 0.0F) {
            g->kind |= 0x40000;
            g->appsrt->rot.y = -M_PI_2_F;
        } else {
            g->appsrt->rot.y = M_PI_2_F;
        }
    }
    return g;
}

HSD_Generator* efLib_CreateGenerator_Attach(s32 gfx_id, HSD_JObj* jobj)
{
    return generator(gfx_id, jobj, "joint");
}

HSD_Generator* efLib_CreateGenerator_Attach_AddAppSRT(s32 gfx_id, HSD_JObj* jobj)
{
    return generator(gfx_id, jobj, "joint-srt");
}

HSD_Generator* efLib_CreateGenerator_Attach_Scale(s32 gfx_id, va_list vlist, HSD_GObj* gobj)
{
    HSD_Generator* g = generator(gfx_id, va_arg(vlist, HSD_JObj*), "joint-srt");
    if (g != NULL) {
        g->appsrt->scale.x = g->appsrt->scale.y = g->appsrt->scale.z = root_scale_y(gobj);
    }
    return g;
}

HSD_Generator* efLib_CreateGenerator_AppSRT_SetScale(s32 gfx_id, va_list vlist)
{
    HSD_Generator* g = generator(gfx_id, va_arg(vlist, HSD_JObj*), "joint-srt");
    if (g != NULL) {
        g->appsrt->scale.x = g->appsrt->scale.y = g->appsrt->scale.z = *va_arg(vlist, f32*);
    }
    return g;
}

HSD_Generator* efLib_CreateGenerator_AppSRT_SetFacingDir(s32 gfx_id, va_list vlist)
{
    HSD_Generator* g = generator(gfx_id, va_arg(vlist, HSD_JObj*), "joint-srt");
    if (g != NULL) {
        g->appsrt->rot.y = *va_arg(vlist, f32*) < 0.0F ? -M_PI_2 : M_PI_2;
    }
    return g;
}

HSD_Generator* efLib_CreateGenerator_AppSRT_SetFacingDirScale(s32 gfx_id, va_list vlist)
{
    HSD_Generator* g = generator(gfx_id, va_arg(vlist, HSD_JObj*), "joint-srt");
    if (g != NULL) {
        g->appsrt->rot.y = *va_arg(vlist, f32*) < 0.0F ? -M_PI_2 : M_PI_2;
        g->appsrt->scale.x = g->appsrt->scale.y = g->appsrt->scale.z = *va_arg(vlist, f32*);
    }
    return g;
}

EF_Effect* efLib_CreateGenerator_AppSRT_SetPos(int gfx_id, HSD_GObj* gobj, HSD_JObj* jobj, Vec3* vec)
{
    // (an empty effect that carries the generator along the joint, at an offset)
    EF_Effect* e = model(0, gobj, NULL, "carrier");
    if (e != NULL) {
        e->update = efLib_Cb_AccumOffset_FromParams;
        e->attach_jobj = jobj;
        e->params = *vec;
        generator((s32) gfx_id, jobj, "joint-srt");
    }
    return e;
}

// --- what the dispatch sets that the viewer has no use for --------------------

void efLib_SetTevKonstColor(HSD_JObj* jobj, s32 count, u32 konst, u32 tev0)
{
    (void) jobj;
    (void) count;
    (void) konst;
    (void) tev0;
}

void efLib_SetParamGfxId(HSD_GObj* gobj, s32 gfx_id)
{
    (void) gobj;
    (void) gfx_id;
}

void efLib_SetParamAlpha(HSD_GObj* gobj, u8 alpha)
{
    (void) gobj;
    (void) alpha;
}

void HSD_JObjAnimAll(HSD_JObj* jobj)
{
    (void) jobj;
}

#define CALLBACK_STAND_IN(name)                                                \
    void name(EF_Effect* effect) { (void) effect; }
CALLBACKS(CALLBACK_STAND_IN)

static const char* callback_name(EF_UpdateFn fn)
{
#define CALLBACK_NAME(name)                                                    \
    if (fn == (EF_UpdateFn) name) {                                            \
        return #name + sizeof("efLib_Cb_") - 1;                                \
    }
    if (fn == NULL) {
        return "";
    }
    CALLBACKS(CALLBACK_NAME)
    return "?";
#undef CALLBACK_NAME
}

// --- the game's dispatch, compiled against the stand-ins -----------------------

// The decompilation's own files (src/upstream-files.txt, at src/upstream.lock's
// commit), which compile to nothing without MSL_EF_DISPATCH. efdata.h's
// externs are the definitions above (volatile, as efalt.c declares them), so
// its body is skipped; efasync.c's allocator data gets a name of its own.
#define MSL_EF_DISPATCH 1
#define MELEE_EF_DATA_H 1
#define efAsync_AllocData msl_pieces_AllocData
#include "melee/ef/efasync.c"
#include "melee/ef/efalt.c"
#include "melee/ef/efsync.c"

void* efSync_Spawn(s32 gfx_id, HSD_GObj* gobj, ...)
{
    void* out;
    va_list args;
    va_start(args, gobj);
    out = efSync_Spawn_v(gfx_id, gobj, args);
    va_end(args);
    return out;
}

// --- running it -----------------------------------------------------------------

// The random state after what a piece's making draws, from `state`: a
// generator's first count, or the generators a model's animation starts on
// its first step (the draws runtime/effects.c makes for them).
static u32 drawn(const Piece* p, u32 state)
{
    u32 keep = *seed_ptr;
    u32 out;
    *seed_ptr = state;
    if (p->kind == 'g') {
        msl_effect_pieces_consume_generator(p->id);
        // (a generator of a file gameplay does not load: the simulator draws
        // nothing for it; the game does when its command counts at random)
        if (msl_effect_view_only(p->id / 1000) &&
            msl_effect_generator_draws(p->id / 1000, p->id))
        {
            (void) HSD_Randf();
        }
    } else if (p->id >= 0 && p->id < 0x28) {
        msl_effect_pieces_consume_model((int) (p->id % 1000));
    }
    else {
        // (a fighter's file's model: the simulator draws nothing for its
        // first step; the game does, once per generator that counts at
        // random)
        int n = msl_effect_model_first_draws(p->id);
        while (n-- > 0) {
            (void) HSD_Randf();
        }
    }
    out = *seed_ptr;
    *seed_ptr = keep;
    return out;
}

static void emit(HSD_GObj* owner, s32 gfx_id, u32 state)
{
    int i;

    // (each piece's seed in the track is its state in this order)
    // The game's order: each generator as the dispatch makes it, then - the
    // dispatch done - the models' animations' first steps, the last made
    // first (efLib_AnimQueue, flushed after the dispatch).
    // refs/melee/src/melee/ef/{eflib.c::efLib_Create,efasync.c,efalt.c}
    for (i = 0; i < piece_count; ++i) {
        Piece* p = &pieces[i];
        if (p->kind == 'g') {
            p->rng_at = state;
            state = drawn(p, state);
            if (msl_effect_view_only(p->id / 1000) &&
                msl_effect_generator_draws(p->id / 1000, p->id))
            {
                msl_viewer_track_skipped_n(1);
            }
        }
    }
    for (i = piece_count - 1; i >= 0; --i) {
        Piece* p = &pieces[i];
        if (p->kind == 'm') {
            p->rng_at = state;
            state = drawn(p, state);
            if (p->id >= 0x28) {
                msl_viewer_track_skipped_n(msl_effect_model_first_draws(p->id));
            }
        }
    }

    for (i = 0; i < piece_count; ++i) {
        Piece* p = &pieces[i];
        MslEffectPiece out;
        memset(&out, 0, sizeof(out));
        out.index = i;
        out.kind = p->kind;
        out.id = p->id;
        out.how = p->how;
        out.joint = p->joint;
        out.at = p->at;
        if (p->kind == 'm') {
            out.t = p->jobj.translate;
            out.r.x = p->jobj.rotate.x;
            out.r.y = p->jobj.rotate.y;
            out.r.z = p->jobj.rotate.z;
            out.s = p->jobj.scale;
            out.callback = callback_name(p->effect.update);
            out.lifetime = p->effect.lifetime;
            out.params = p->effect.params;
            out.rng_at = p->rng_at;
            // (one made on no joint that the dispatch then hung on one: it
            // follows that joint, at its callback's offset)
            if (out.joint == NULL) {
                out.joint = p->effect.attach_jobj;
            }
        } else {
            out.t = strcmp(p->how, "place") == 0 ? p->gen.pos : p->srt.translate;
            out.r.x = p->srt.rot.x;
            out.r.y = p->srt.rot.y;
            out.r.z = p->srt.rot.z;
            out.s = p->srt.scale;
            out.callback = "";
            out.mirrored = (p->gen.kind & 0x40000) != 0;
            out.rng_at = p->rng_at;
        }
        msl_viewer_track_fxp(owner, gfx_id, &out);
    }
}

// The dispatch's globals above are the recorder's one set: one spawn is run
// at a time across threads.
static pthread_mutex_t msl_pieces_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int busy;

static void run(s32 gfx_id, HSD_GObj* gobj, va_list args)
{
    u32 saved;

    if (busy || !msl_viewer_track_on()) {
        return;
    }
    busy = 1;
    pthread_mutex_lock(&msl_pieces_lock);
    saved = *seed_ptr;
    piece_count = 0;
    msl_pieces_loaded = 1;
    loaded_banks();
    {
        int i;
        for (i = 0; i < 51; ++i) {
            efAsync_DatEntries[i].data =
                msl_pieces_banks[i] ? &msl_pieces_loaded : NULL;
        }
    }
    efSync_Spawn_v(gfx_id, gobj, args);
    efLib_AnimCount = 0;
    *seed_ptr = saved;
    // (the game's state as the spawn was made: the simulator's, stepped for
    // the draws it has left out this frame)
    *seed_ptr = saved;
    emit(gobj, gfx_id, msl_viewer_track_state());
    pthread_mutex_unlock(&msl_pieces_lock);
    busy = 0;
}

void msl_effect_pieces(s32 gfx_id, HSD_GObj* gobj, va_list args)
{
    va_list mine;
    va_copy(mine, args);
    run(gfx_id, gobj, mine);
    va_end(mine);
}

static void call(s32 gfx_id, HSD_GObj* gobj, ...)
{
    va_list args;
    va_start(args, gobj);
    run(gfx_id, gobj, args);
    va_end(args);
}

void msl_effect_pieces_deferred(HSD_GObj* gobj, s32 gfx_id, int spawn_kind, HSD_JObj* jobj, const Vec3* params,
                                float extra1, float extra2)
{
    // efasync.c::efAsync_QueueProcessDeferred: the arguments each kind of
    // queued spawn is made with
    Vec3 pos;
    Vec3 ofs = *params;
    f32 p0 = params->x;
    f32 e1 = extra1;
    f32 e2 = extra2;

    switch (spawn_kind) {
    case EF_SPAWN_ATTACH:
        call(gfx_id, gobj, jobj);
        break;
    case EF_SPAWN_POS:
        lb_8000B1CC(jobj, NULL, &pos);
        call(gfx_id, gobj, &pos);
        break;
    case EF_SPAWN_POS_OFFSET:
        lb_8000B1CC(jobj, &ofs, &pos);
        call(gfx_id, gobj, &pos);
        break;
    case EF_SPAWN_ATTACH_PARAM:
        call(gfx_id, gobj, jobj, &p0);
        break;
    case EF_SPAWN_POS_PARAM:
        lb_8000B1CC(jobj, NULL, &pos);
        call(gfx_id, gobj, &pos, &p0);
        break;
    case EF_SPAWN_POS_OFFSET_PARAM:
        lb_8000B1CC(jobj, &ofs, &pos);
        call(gfx_id, gobj, &pos, &e1);
        break;
    case EF_SPAWN_POS_OFFSET_PARAM2:
        lb_8000B1CC(jobj, &ofs, &pos);
        call(gfx_id, gobj, &pos, &e1, &e2);
        break;
    case EF_SPAWN_ATTACH_OFFSET:
        call(gfx_id, gobj, jobj, &ofs);
        break;
    default:
        break;
    }
}
#endif // MSL_VISUAL_TRACK_BUILD
