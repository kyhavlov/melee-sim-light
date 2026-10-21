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


// Per-frame state, at the end of each step (runtime/scalar.c).
void msl_viewer_track_frame(struct MslCoreMatch* match);

// The lines written since the last call (NUL-terminated, `size` bytes), or
// NULL when the match keeps no track. Valid until the match steps again.
const char* msl_core_match_viewer_track(struct MslCoreMatch* match,
                                        size_t* size);
// A fighter's switchable texture (eyes, mouth): ftAnim_800704F0 sets slot
// `slot` to texture `index`; ftAnim_80070654 puts every slot back, written
// as slot -1, index 0.
void msl_viewer_track_tex(HSD_GObj* fighter, int slot, float index);

#endif
#endif // MSL_VISUAL_TRACK_BUILD
