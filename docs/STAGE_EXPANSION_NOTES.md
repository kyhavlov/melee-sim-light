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
- Platform line data is admitted for inspection and future owner work, but fighter grounding uses a
  non-platform floor graph until drop-through/pass-through/moving-platform mechanics are modeled.

Useful next stage work before deeper platform mechanics:
- Decide when to admit four-player local Pokemon Stadium repros into validation; current aggregate
  remains a two-player suite because shared one-step eval requires one `num_players` shape.
- Pokemon Stadium's current top disruptive packet
  (`reports/triage/next_desync_ps/top_packet.md`) is `DeadUpFallHitCamera` position drift, not a
  missing stage role lookup. Decomp `ftCo_DeadUpFall_Phys` advances that action from hidden
  `mv.co.unk_deadup` vectors/timers, so closing it belongs to match-flow DeadUpFall physics/seed
  ownership rather than MSLSTG01 spawn/respawn/camera/blast hookup.
- Model pass-through/moving platform mechanics. Pokemon Stadium validation is currently restricted
  to frozen-stadium replays; transformation ownership remains out of scope for the foreseeable
  runtime target.
- Keep procedural mechanics out of `MSLSTG01`: platforms/drop-through/pass-through, ledge behavior,
  and mpColl branch ordering remain runtime owner work, not table facts.
