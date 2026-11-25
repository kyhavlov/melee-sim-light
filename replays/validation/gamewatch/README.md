# Oil Panic stored-state fixture

`bucket.post320.zst` holds Slippi 3.20 Post-Frame bytes from a Playback Dolphin
regeneration of `bucket.slpz`. The `gamewatch` suite records the original replay
hash and playback build provenance. Keep the original replay for simulation
input. Playback can change recorded physical button bits even when the
post-frame states are identical.

Enable **Regenerate Slippi Replays** and play the complete original with the
Slippi 3.20 Playback codes. Then run

```sh
python -m tools.validation.stored replays/validation/gamewatch/bucket.slpz \
  /path/to/regenerated.slp replays/validation/gamewatch/bucket.post320.zst
```

The generator verifies finalized post-frame row keys and legacy post-frame
bytes, allowing differences in render visibility and inactive action-state data.
Each compressed record contains a big-endian signed frame ID, the zero-based
physical port, the follower byte, and the eight replay bytes at offsets `0x55`
through `0x5C`.
