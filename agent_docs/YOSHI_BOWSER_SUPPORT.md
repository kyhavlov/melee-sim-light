# Yoshi and Bowser support — 2026-09-07

Yoshi and Bowser are admitted throughout the C and Python APIs, native and Wasm
runtime, six-stage singles/three-player teams/doubles domain, and live viewer.
The packet imports their complete matching character callback and article owners.
It starts from `3a71888c` and preserves the unrelated stage-spawn JSON deletion.
The concurrent reward packet is coordinated separately in `ACTIVE_WORK.md`.

A viewer follow-up corrected Yoshi's model identity: internal 14 must map to
Slippi external 17; passing 14 through selected the Ice Climbers archive. Live
and saved-trace settings now share the converter. The model checks verify the
renderer character name, and production browser smoke selects Yoshi and checks
that `yoshi.zip` loaded. The earlier package-presence check missed this mistake.

## Source and data ownership

The relevant community decomp revision is
`32420c5f464f211842120c186390a30868b793cd`. Its six Yoshi and five Bowser
translation units, plus Yoshi egg/tongue/star and Bowser flame owners, are
Matching. This includes the newly matched Egg Roll implementation. Imports keep
this repository's source paths and symbol names; each new C file records its
newer upstream path/revision in `src/source_manifest.tsv`. The general baseline
in `src/upstream.lock` remains pinned, with the additive inventory updated.

| Character | Internal ID | Animation entries | Costumes | Gameplay parts |
|---|---:|---:|---:|---:|
| Yoshi | 14 | 314 | 6 | 43 of 70 |
| Bowser | 5 | 316 | 4 | 44 of 76 |

The source owners cover lifecycle, guard where character-specific, special
startup/loop/turn/hit/throw/end transitions, IASA, physics/collision callbacks,
and the common capture/victim flows. The article registries include Egg Throw,
Egg Lay, Star and Bowser Flame; Yoshi Tongue owns captured-item manipulation.
Unsupported character stubs are displaced; only outside-domain Kirby copy
branches retain loud aborts.

Immutable archives, animation programs, effects and article graphs belong to
GameData. Live source Fighter/motion/item state remains match-owned and singular.
Extraction, typed native DAT translation, deterministic layout generation and
required loaders were extended together. Fresh `make extract ISO=SSBM.iso`
passed with 172 profile files plus `main.dol`. Existing extracted data roots need
a fresh extraction before constructing either added fighter. No game archives
or ISO bytes are committed.

## Portability and source exactness corrections

- Preserve every audited retail single/double fused arithmetic boundary in the
  newly imported owners. Egg Roll includes double-precision angle expressions;
  replacing them with float approximations is not equivalent.
- Correct scalar fields previously typed as `UNK_T`, and address Yoshi egg
  timer/duration fields through their owning members. A real live pointer widens
  on native, invalidating the old Captain/walk union aliases. The corrections
  remove native-only guard, captured-mash and egg animation timing differences.
- Omit only Yoshi's shield DObj/MObj material animation on the headless graph;
  shield/collision/hurtbox state and timers stay source-owned. Remove Egg Roll's
  MWCC stack-padding helper whose unused `GET_FIGHTER(NULL)` expressions emit
  no retail instructions but dereference null with the hosted debug compiler.
- Widen the sole animation-program node token beyond the 18-bit limit: the
  expanded bank has 267,980 nodes. Native joint size remains 56 bytes by using
  existing padding; the 32-bit joint grows from 44 to 48 bytes.
- Preserve constraint invalidation when a captured fighter's root position
  repeats but the other fighter's target bone moves. The flat pose owner keeps
  skipping redundant ordinary matrix products.
- Restore the Gekko `acosf` estimate seed on x86. Bowser flame steering disproved
  the previous hardware-seed equivalence claim. See the indexed experiment in
  `performance/HISTORY.md`; no throughput equivalence is claimed.
- Restore the common charged-smash fused multiplier and egg damage-timer fused
  subtract. The smash correction also makes the former classified doubles game
  `Game_20260704T005918` completely exact on both native and PPC (9,407 frames).

Each material adaptation has a source/retail address entry in
`src/upstream_delta_ledger.tsv`. Gameplay is not conditioned on replay identities.

## Retained recordings

`replays/suites/yoshi.json` contains 10 original full games and 11 microreplays;
`bowser.json` contains 8 full games and 9 microreplays. Their 190,427 transitions
are included in the canonical aggregate. Original hashes, Slippi versions,
complete-game bounds, compression hashes and download provenance are generated
in [yoshi_bowser_replay_provenance.json](yoshi_bowser_replay_provenance.json).
Every `.slpz` was decompressed and compared byte-for-byte with its original.

Full games came from SlippiLab's public replay originals. Both characters are
challenged against other supported fighters. Transforming Stadium full games
were excluded: the current supported Stadium model is frozen. The retained
Stadium microreplays have an empty transformation event stream.

Microreplays were recorded by the supplied Slippi Online doublesbot AppImage,
using ordinary VS controller inputs through libmelee. Each uses a private
Dolphin user directory, display and controller port. They start normally at
frame -123 and end with a recorded no-contest LRAS. No emulator memory writes,
savestate injection, replay editing, or simulator-generated gameplay were used.

| Owner | Focused recorded situations |
|---|---|
| Yoshi Egg Throw | Ground/air use, short/long holds, both angle directions |
| Yoshi Egg Lay | Ground/air whiffs and transitions, fighter capture, mash-out, attacks on the egg |
| Yoshi Egg Roll | Air/ground use and landing, reversals, manual exit, off-edge death, opponent hit and shield contact |
| Yoshi Bomb | Ground/air use, Battlefield platform/ground landing and stars |
| Yoshi guard | Regular/light shield and grab inputs; combat full games challenge shield contact |
| Bowser Flame | Sustained/depleted/recovered and airborne use, shield/reflect/contact inputs |
| Bowser Koopa Klaw | Ground/air startup transitions, whiffs, fighter capture, repeated grounded bites and throws |
| Bowser Fortress | Grounded use, airborne use and landing, moving off FD into air and ledge capture |
| Bowser Bomb | Ground/air use and Battlefield landing |

[Replay coverage](yoshi_bowser_replay_coverage.json) reports actual observed
states, not the recording driver's intended scenario. The port contains every
callback in the character state tables; the retained set is not an exhaustive
branch proof. It does not observe Yoshi's four item-swallow variants or the
first Egg Roll startup landing state, nor Bowser's repeated airborne bite/hold
variants. Their source implementations are present; these remain useful future
recording extensions. Existing full games exercise the other airborne capture
and throw owners. Failed recording attempts are not admitted as evidence for
those missing states.

## Validation and residual limits

All results below come from Linux/amd64 GNU builds.
Before commit, the character-only staged tree was built in an isolated checkout
without the concurrent reward code. Source/native lifecycle, focused Python
checks and both full debug/release gates passed there as well.

| Check | Result |
|---|---|
| Prior-domain baseline | 310 PASS, 56 CLASSIFIED; 3,501,461 transitions |
| Expanded debug aggregate | 339 PASS, 65 CLASSIFIED, 0 FAIL/ERROR; 404 replays, 3,691,888 transitions |
| Expanded release aggregate | Same exact classifications and output locks across all 3,691,888 transitions |
| Added Yoshi suite | 13 PASS, 8 CLASSIFIED; 98,372 transitions |
| Added Bowser suite | 15 PASS, 2 CLASSIFIED; 92,055 transitions |
| All 20 retained microreplays | Exact on native and PPC |
| New complete-game PPC check | Every residual snapshot agrees with native |
| Native source/lifecycle/census | Pass, including all costumes and parts |
| Wasm and browser viewer | Live and production package pass; all 18 selectors, six stages, new specials/articles and no heap growth |
| Python/API/replay tests | 15 pass, including roster/team construction and mixed Yoshi/Bowser live-article restore at another batch index across all stages |
| Inventory/data/schema tests | 33 pass |

The 38 added replays have 229 mismatched transitions, all bounded and individually
locked: 217 Yoshi held-shield cursor rows, two single-ULP wall-clamp rows, and ten
Sheik Chain preassignment-byte rows. Their ownership is:

1. Yoshi held shield (342) has animation ID -1 and no joint AObj. Retail
   `ftAnim_8006F3DC` falls through without assigning f1 at `8006F468`; these full
   games record 100 from caller register residue, while both hosted backends
   return zero. Its gameplay guard timers and shield state remain exact. The
   new controller-driven shield recording itself reports zero and is exact.
2. Dream Land Yoshi frame 7770 and Yoshi's Story DK frame 4606 have the existing
   one-ULP offstage wall-clamp execution-profile residual; PPC/native agree.
3. Sheik Chain's original constructor leaves x18 unassigned before its first
   dynamic-chain publication. The existing hosted policy initializes it to zero.
   `MildMurkyNewt` records residue 33 for ten early rows. Later live x18 is still
   compared; no new mask or gameplay initialization policy was introduced.

The strict comparator is unchanged. Unowned Yoshi article bytes use the existing
production projection policy; raw replay discrepancies remain visible. New
classification snapshots and full native output locks were generated through
the validator. The other 365 existing output locks remain unchanged; only the
now-exact charged-smash doubles lock is refreshed and its obsolete classification
removed. Renderer visibility diagnostics retain their existing separate policy.

Forensic logs, source/asm experiments, replay downloads and recording attempts
remain under ignored `reports/triage/yoshi_bowser_port/` and
`reports/triage/yoshi_bowser_recording_probe/`.
