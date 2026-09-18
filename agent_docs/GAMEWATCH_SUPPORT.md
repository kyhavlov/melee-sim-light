# Game & Watch integration

PR #24, rebased locally onto the merged Mewtwo admission (`de64f76a`).

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
  all nine aerial miss paths, interrupted neutral/back/up-air accessory cleanup,
  and Chef trajectory history covering all five trajectories;
  laser absorption versus inactive-bucket and missile controls, accumulated
  bucket damage and full release.
- Four Game & Watches: Judge, Chef, Fire Rescue, bucket release, neutral-air,
  partial bucket state, and 20 Chef projectiles plus 4 Rescue articles. Copy and
  restore into separate arenas reproduce 140-frame continuations, RNG, persistent
  histories/bucket state and every live item's position/trajectory, including
  items beyond the 15-entry exported item window. Sealed arena checks pass.
- Existing reserves suffice. Largest tested FObj peak is 213/313 during four
  simultaneous oil releases; the 24-article case peaks at 77/313 FObj and 38/154
  AObj. All tested scenarios retain at least 20% headroom; no reserve increase.
- Public Python cross-batch restore exercises four-player GW/Fox and GW/Mewtwo
  matches on all six supported stages. C API smoke admits Mewtwo and Game & Watch
  together; the Wasm special-move smoke retains both fighters.
- `replays/suites/gamewatch.json`: 11 full recordings, zero classifications
  on the original native branch. Seven corpus games and four scripted Dolphin
  recordings. Two further July games need the separate legacy-Dolphin arithmetic
  profile lane. The author also reported three Game & Watch/Mewtwo recordings;
  their admission dependency is resolved, but those fixtures were not included
  in this PR. An initial candidate contained unsupported Roy and was excluded.
- Scripted recordings cover all nine Judge outcomes and Chef articles, three
  laser absorptions followed by oil release, inactive-bucket laser hits and
  active-bucket missile hits. Coverage assertions pass in
  `tests/test_gamewatch_interaction_recordings.py`. Source hashes and known
  Dolphin build/settings are in `GAMEWATCH_VALIDATION.json`.
- Public admission updates C/Python enums, both admission gates, CLI/Makefile
  validation defaults, viewer selector/assets, AGENTS/README and aggregate.
  `externalCharId` already contained correct 16->10 and 24->3 mappings.
- The original branch reported 514 exact / 26 classified across 540 cases.
  Integration with Mewtwo retains both suites and all existing locks, yielding
  547 cases. Current review gates are recorded below after execution.

The corpus and targeted tests establish the tested behavior; they do not exhaust
all possible projectile pairings or RNG sequences. Additional failures should be
investigated by source owner, without replay-specific exceptions.

The current slippilab Game & Watch bake omits some accessory meshes. Re-baking
those viewer silhouettes is separate presentation work; this PR ports the source
articles used by gameplay. Launch instructions are in the repository README.

## Integration review — 2026-09-17

- All 31 newly imported files match pinned upstream `91b9789f` verbatim. The
  Chef pointer correction is the sole new upstream delta and is ledgered.
- The original source lock was incorrect despite green CI: the upstream checkout
  was absent there, so only inventory was checked. Regenerated the combined
  1227-file lock; full local source verification passes.
- Resolved both fighters' registry, loader, public API, viewer and suite entries.
  The combined extraction has 209 DAT files plus main.dol. Preserve the landed
  stage-lifecycle build dependency and all Mewtwo tests.
- Added mixed GW/Mewtwo cross-batch restore coverage and corrected the supported
  character diagnostic. No gameplay or comparison-policy changes are proposed.
- Gates on Linux x86_64 / GCC 13.3.0:

  | Check | Result |
  | --- | --- |
  | Fresh extraction | 209 DAT files plus main.dol; all hashes verified |
  | `make source-check` | 1227 pinned upstream files verified |
  | `make native validator python-library native-smoke -j8` | Pass, including both fighter smokes and the shared lifecycle/pool checks |
  | Gameplay-parts derivation | Luigi anchor, Mewtwo and Game & Watch rows match |
  | `make validation-suite VALIDATION_ARGS=--no-build` | 521 exact / 26 existing classified / 0 fail / 0 error; 5,205,059 transitions |
  | Game & Watch on native/PPC | All 11 recordings exact; 129,848 transitions each |
  | `pytest -q` | 76 passed, including mixed-fighter restore on all six stages |
  | `make viewer-smoke -j8` | Native/Wasm parity, 23-character / six-stage smoke and live Chrome pass |

- The initial concurrent PPC run passed ten recordings; the 23,010-frame
  `Game_20260707T202855` hit its 30-second execution budget. An isolated rerun
  with a 60-second budget passed all frames in 26.8 seconds with unchanged
  comparison and UCF settings. No validation tolerance or lock was changed.
- All 536 merged-main output locks remain unchanged; Game & Watch adds 11.
  Scratch evidence: `reports/triage/gamewatch_pr24_review/`.
- Review conclusion: no remaining merge blocker found. The user approved
  publishing the rebase and cleanup, followed by merging after fresh CI.
