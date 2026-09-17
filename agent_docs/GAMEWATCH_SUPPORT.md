# Game & Watch integration

Implemented and locally admitted, 2026-09-15.

## Implemented

- Ten fighter and ten article source owners imported from the pinned decomp;
  five attack-entry abort stubs displaced by their real implementations.
- Private fighter bootstrap, callback tables, four costume colors, article
  registry, extraction and native DAT translation. The article table contains
  ten articles followed by a visibility lookup; Chef has five trajectories.
- Gameplay skeleton mask derived from extracted data and source anchors:
  37 live joints out of 53. Gameplay-parts smoke covers all four costumes.
- Chef's leading attribute pointer is typed as a pointer so native pointer
  widening preserves its following trajectory index. Retail offsets are
  unchanged. Replay projection retains that numeric index and excludes
  pointer/residue samples for the other nine articles, following their source
  union ownership.

## Validation

- `make gamewatch-smoke`: Judge history and all nine ground hit/miss outcomes,
  aerial miss paths and interrupted neutral/back/up-air accessory cleanup, Chef trajectory history covering all five trajectories,
  laser absorption versus inactive-bucket and missile controls, accumulated
  bucket damage and full release.
- Four Game & Watches: Judge, Chef, Fire Rescue, bucket release, neutral-air,
  partial bucket state, and20 Chef projectiles plus4 Rescue articles. Copy and
  restore into separate arenas reproduce140-frame continuations, RNG, persistent
  histories/bucket state and every live item's position/trajectory, including
  items beyond the15-entry exported item window. Sealed arena checks pass.
- Existing reserves suffice. Largest tested FObj peak is213/313 during four
  simultaneous oil releases; the24-article case peaks at77/313 FObj and38/154
  AObj. All tested scenarios retain at least20% headroom; no reserve increase.
- Public Python cross-batch restore covers a four-player GW/Fox match on
  all six supported stages. Native smoke and browser/WASM special tests pass.
- `replays/suites/gamewatch.json`: 11 full recordings, zero classifications
  on native. Seven corpus games and four scripted Dolphin recordings. Five
  further recordings from the same effort ship separately: two July games
  need the explicit legacy-Dolphin arithmetic profile lane, and three are
  Game & Watch versus Mewtwo. An initial candidate contained unsupported Roy
  and was excluded; it was not a gameplay validation failure.
- Scripted recordings cover all nine Judge outcomes and Chef articles, three
  laser absorptions followed by oil release, inactive-bucket laser hits and
  active-bucket missile hits. Coverage assertions pass in
  `tests/test_gamewatch_interaction_recordings.py`. Source hashes and known
  Dolphin build/settings are in `GAMEWATCH_VALIDATION.json`.
- Public admission updates C/Python enums, both admission gates, CLI/Makefile
  validation defaults, viewer selector/assets, AGENTS/README and aggregate.
  `externalCharId` already contained correct16->10 and24->3 mappings.
- Native supported-domain gate: 514 exact, 26 existing classified, zero
  failures/errors across 540 cases. Existing locks are preserved; 11 new
  native locks were generated after host certification.

The corpus and targeted tests establish the tested behavior; they do not exhaust
all possible projectile pairings or RNG sequences. Additional failures should be
investigated by source owner, without replay-specific exceptions.

The production viewer build and live Chrome smoke passed. To launch on this
NixOS host after building:

```sh
cd /home/blewf/git/melee-sim-light
nix-shell -p gcc gnumake nodejs pkg-config libusb1 --run 'make viewer'
```

Live mode: `http://127.0.0.1:8001/tools/viewer/live/`. Keyboard controls are
listed in the UI; the local server also starts the GameCube USB-adapter bridge.
