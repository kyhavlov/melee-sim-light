# Stage Expansion Notes

Current committed state:
- `aggregate_recent` includes three Battlefield replays.
- `battlefield_recent` exists as the focused stage/platform suite.
- `MSLSTG01` v2 extracts FD/Battlefield collision segments, platform/ledge flags, stage points,
  spawn/respawn points, and camera/blast bounds.
- Runtime/eval can load FD and Battlefield stage data, but new-match init is still FD-only until
  non-FD Slippi neutral-start ownership is modeled.

Useful next stage-prep work before platform mechanics:
- Add a small stage-registry path for remaining legal stages so adding stages is mostly data/config:
  DAT key, stage id, suite name, and known expected role rows.
- Promote one additional stage at a time through `MSLSTG01` known-row tests before adding replays.
- Verify spawn/respawn/camera/blast role extraction against each new stage's `map_head` layout.
- Add focused suites per stage, then include selected replays in `aggregate_recent` only after the
  stage artifact and clean-checkout generation path are solid.
- Keep procedural mechanics out of `MSLSTG01`: platforms/drop-through/pass-through, ledge behavior,
  and mpColl branch ordering remain runtime owner work, not table facts.

