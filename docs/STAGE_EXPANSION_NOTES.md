# Stage Expansion Notes

Current staged state:
- `aggregate_recent` includes selected two-player Battlefield, Fountain of Dreams, frozen Pokemon
  Stadium, Yoshi's Story, and Dream Land N64 replays.
- Focused suites exist for Battlefield, Fountain of Dreams, Pokemon Stadium, Yoshi's Story, and
  Dream Land N64.
- `MSLSTG01` v2 extracts FD/Battlefield/Fountain/Pokemon/Yoshi/Dream Land collision segments,
  platform/ledge flags, stage points, spawn/respawn points, and camera/blast bounds.
- Runtime/eval can load all six supported legal-stage artifacts. New-match init uses MSLSTG01
  spawn/respawn/camera/blast roles for all supported stages; Slippi neutral-spawn teams mode
  remains FD-only because that patch table is FD-specific.
- Platform line data is admitted for inspection, and fighter grounding now consumes static
  platform floors through source-shaped Pass/floor-skip gating. FoD platform lines are admitted in
  their source-local/static positions for now; live FoD platform transforms remain stage-object
  residual work.
- Frozen Pokemon Stadium is a current-domain bridge: fighter-solid runtime collision is allowlisted
  to base/frozen line ids `34,35,36,51..54` while all GrPs platform lines remain visible through
  debug/data APIs. Deletion path: expose ground-object activation/transform state in MSLSTG01 or an
  adjacent stage-object artifact and replace the allowlist with active object metadata.
- Yoshi's Story Shy Guys (`It_Kind_Heiho`) are stage-owned item objects, not fighter articles.
  Runtime admits active state 1/4 generic item-position integration from visible `x40_vel`, but
  still does not model item-animation/dynamic-bone velocity, hidden spawn delay, RNG, or collision
  turnaround owners; closing them requires causal Shy Guy spawn/timer plus item animation
  extraction, not replay-next velocity seed lanes.

Useful next stage work before deeper dynamic stage mechanics:
- Decide when to admit four-player local Pokemon Stadium repros into validation; current aggregate
  remains a two-player suite because shared one-step eval requires one `num_players` shape.
- Pokemon Stadium's current top disruptive packet
  (`reports/triage/next_desync_ps/top_packet.md`) is `DeadUpFallHitCamera` position drift, not a
  missing stage role lookup. Decomp `ftCo_DeadUpFall_Phys` advances that action from hidden
  `mv.co.unk_deadup` vectors/timers, so closing it belongs to match-flow DeadUpFall physics/seed
  ownership rather than MSLSTG01 spawn/respawn/camera/blast hookup.
- Model moving-platform transform / stage-object ownership. Pokemon Stadium validation is currently
  restricted to frozen-stadium replays; transformation ownership remains out of scope for the
  foreseeable runtime target.
- Keep procedural mechanics out of `MSLSTG01`: moving platforms, ledge behavior, and mpColl branch
  ordering remain runtime owner work, not table facts.
