# Stage Expansion Notes

Current staged state:
- `aggregate_recent` includes selected two-player Battlefield, Fountain of Dreams, frozen Pokemon
  Stadium, Yoshi's Story, and Dream Land N64 replays.
- Focused suites exist for Battlefield, Fountain of Dreams, Pokemon Stadium, Yoshi's Story, and
  Dream Land N64.
- `MSLSTG01` v6 extracts FD/Battlefield/Fountain/Pokemon/Yoshi/Dream Land collision segments, raw
  `MapLine` graph links, platform/ledge/fighter-solid flags, stage points, spawn/respawn points,
  camera/blast bounds, and source-backed platform transform records.
- Runtime/eval can load all six supported legal-stage artifacts. New-match init uses MSLSTG01
  spawn/respawn/camera/blast roles for all supported stages; Slippi neutral-spawn teams mode
  remains FD-only because that patch table is FD-specific.
- Platform line data is admitted for inspection, and fighter grounding consumes admitted platform
  floors through source-shaped Pass/floor-skip gating. FoD side-platform floor endpoints are
  transformed at runtime from either current Slippi FoD platform-height event state or the
  free-running `grIzumi_801CC358` phase/timer/RNG scheduler initialized from generated
  `GrIz.dat::yakumono_param` metadata. Webplay/modelplay consume the same C-owned debug
  stage-state height path used by collision.
- Yoshi's Story now carries generated current-domain stage-object terrain metadata: the raw center
  raised segment remains debug-visible but not fighter-solid, and Randall is admitted as a
  generated pass-through floor line with a runtime transform from `grStory_801E3370`/`Ground_801C2FE0`
  path timing.
- Frozen Pokemon Stadium fighter-solid policy is generated into MSLSTG01 as line metadata for the
  base/frozen legal-stage domain. Transformation geometry remains visible through debug/data APIs,
  but fighter collision and floor traversal query the generated active/fighter-solid mask instead
  of a runtime line-id allowlist.
- Yoshi's Story Shy Guys (`It_Kind_Heiho`) are stage-owned item objects, not fighter articles.
  Runtime admits active state 1/4 generic item-position integration from visible `x40_vel`, but
  still does not model item-animation/dynamic-bone velocity, hidden spawn delay, RNG, or collision
  turnaround owners; closing them requires causal Shy Guy spawn/timer plus item animation
  extraction, not replay-next velocity seed lanes.

Useful next stage work before deeper dynamic stage mechanics:
- Decide when to admit four-player local Pokemon Stadium repros into validation; current aggregate
  remains a two-player suite because shared one-step eval requires one `num_players` shape.
- Pokemon Stadium DeadUpFallHitCamera packets are match-flow/hidden-offset work, not missing stage
  role lookup. The source timers and visible `speed_y_self` phase-3 update are now runtime-owned from
  `p_ftCommonData->x520`, but full `cur_pos` parity still requires causal `mv.co.unk_deadup.x50/x5C`,
  `xD4_unk_vel`, and `ftAnim_80070FD0` release ownership rather than MSLSTG01 spawn/respawn/camera
  hookup.
- Pokemon Stadium validation is currently restricted to frozen-stadium replays; transformation
  ownership remains out of scope for the foreseeable runtime target.
- Keep procedural mechanics out of `MSLSTG01`: random hazards, ledge behavior, and mpColl branch
  ordering remain runtime owner work, not table facts. Stage geometry policy and static transform
  metadata that come directly from source/data belong in generated artifacts.
