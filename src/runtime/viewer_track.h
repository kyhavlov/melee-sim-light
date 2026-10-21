#if MSL_VISUAL_TRACK_BUILD
#ifndef MSL_RUNTIME_VIEWER_TRACK_H
#define MSL_RUNTIME_VIEWER_TRACK_H

// The viewer track: state a replay viewer draws and a recording does not
// carry, one JSON array per line (agent_docs/VIEWER_TRACK.md). A match keeps
// one only when its config sets MSL_VISUAL_TRACK in camera_mode. Otherwise
// there is none, and every hook below is one untaken branch. The track reads
// simulation state; it never changes gameplay.

#include <stddef.h>

#include <platform.h>
#include <baselib/forward.h>
#include <dolphin/mtx.h>

struct MslCoreMatch;
typedef struct MslViewerTrack MslViewerTrack;

// The bound match's track, NULL when it has none (runtime/context.c).
extern _Thread_local MslViewerTrack* msl_core_context_viewer_track;
#define msl_viewer_track_on() (msl_core_context_viewer_track != NULL)

MslViewerTrack* msl_viewer_track_create(void);
void msl_viewer_track_destroy(MslViewerTrack* track);

struct Fighter;
struct DynamicsData;

// A fighter's swinging chains as Fighter_Create sets them up, with the
// track on: the prune (ftCo_HeadlessPruneDynamics) as always, and each chain
// it drops put back as a copy the game's solver goes on with.
void msl_viewer_track_keep_dynamics(struct Fighter* fp);
struct DynamicsData* msl_viewer_track_copy_dynamics(
    const struct DynamicsData* head);

// Per-frame state, at the end of each step (runtime/scalar.c).
void msl_viewer_track_frame(struct MslCoreMatch* match);

// The lines written since the last call (NUL-terminated, `size` bytes), or
// NULL when the match keeps no track. Valid until the match steps again.
const char* msl_core_match_viewer_track(struct MslCoreMatch* match,
                                        size_t* size);

// Event hooks, called only when msl_viewer_track_on().
void msl_viewer_track_fx(HSD_GObj* owner, s32 gfx_id, HSD_JObj* jobj,
                         const Vec3* pos, float a0, float a1);
void msl_viewer_track_fxend(HSD_GObj* owner);
// The game's random state now (the seed an "fx" line carries).
u32 msl_viewer_track_state(void);
// One piece of the spawn just written (runtime/effect_pieces.c), an "fxp".
struct MslEffectPiece;
void msl_viewer_track_fxp(HSD_GObj* owner, s32 gfx_id,
                          const struct MslEffectPiece* piece);
// The frame a hook fires in.
int msl_viewer_track_hook_frame(void);
// A queued efAsync spawn's arguments, kept for its pieces until the queue
// processes it (runtime/effects.c): x, y, z, then the two extra floats.
float* msl_viewer_track_queued_args(int slot);
// A fighter's switchable texture (eyes, mouth): ftAnim_800704F0 sets slot
// `slot` to texture `index`; ftAnim_80070654 puts every slot back, written
// as slot -1, index 0.
void msl_viewer_track_tex(HSD_GObj* fighter, int slot, float index);
// A random draw the game makes here and the simulator does not: a
// generator's making when its command counts at random, or `n` at once.
void msl_viewer_track_skipped(int bank, int generator_id);
void msl_viewer_track_skipped_n(int n);

#endif
#endif // MSL_VISUAL_TRACK_BUILD
