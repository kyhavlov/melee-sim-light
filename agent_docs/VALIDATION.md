# Replay validation

`replays/suites/melee_core_aggregate.json` selects the primary gate. Every selected
recording must pass capture admission and complete strict gameplay comparison.
There is no classified-as-success path. An eligible replay that differs is a bug
investigation, not a reason to exclude it.

Replay bytes are not downloaded on clone. Install Git LFS and opt in before
running replay-dependent commands:

```sh
git lfs pull --include="replays/validation/**" --exclude=""
make source-check native-smoke
make validation-supported-domain
make validation-suite VALIDATION_SUITE=replays/suites/gamewatch.json VALIDATION_BACKEND=ppc
make viewer-smoke
```

Only certified Linux/amd64 GNU builds may record results and output locks; see
[ENVIRONMENT.md](ENVIRONMENT.md). `melee_core_output_locks.json` stores measured
full-output identities. `--write-output-locks` rejects partial, diagnostic,
ineligible and mismatching runs; an aggregate rewrite also removes retired locks.

Exact means the strict gameplay projection in `tools/validation/native.c`,
including source-backed item masks in `src/runtime/item_projection.h`.
Validation supplies recorded RNG observations and available stage events.
It does not establish an unseeded rollout or reproduce omitted render timing.

## Capture admission

[The admission ledger](validation/replay_admission.json) records exclusion
identities, source evidence and coverage replacements. Checks live in
`tools/validation/admission.py` and run before normal validation and benchmark
preparation, including reuse of cached benchmark tapes. Renaming or recompressing
a known rejected capture cannot evade its decoded-SLP SHA-256 identity.

Ban captures with any of these properties, even when they currently compare exact:

1. **Vanilla render-dependent magnifier damage.** Require the reviewed Slippi
   stage-bounds offscreen patch. Its source uses fighter position instead of
   omitted render/VI timing. Absence of a visible damage tick is not an exception.
2. **Missing required inputs.** Human leaders require raw main-stick/C-stick axes,
   physical buttons/triggers and processed inputs. CPU leaders require processed
   inputs and a recorded level in 1..9. Inspect actual fields, not protocol version
   alone. Never reconstruct missing human controller bytes from processed floats.
3. **Unsupported or unestablished arithmetic.** Retail and defensible Dolphin
   legacy instruction semantics are supported. An explicit override requires
   reviewed content-bound provenance. Never pick a profile solely because it
   makes a recording pass or normalize signed zero to hide a mismatch.
4. **Compared source-uninitialized samples.** Reject Chain samples before `x18`
   assignment and a previously sleeping Sheik/Zelda partner's first L-cancel byte.
   The value may happen to match zeroed simulator memory; it remains ineligible.
5. **Unsupported settings or gameplay modifications.** Require supported NTSC
   fighters/stages, UCF, eight-minute, no-random-item rules and Team Attack in
   teams. Shared starting stock counts, per-player handicaps and positive finite
   damage ratios are supported; they need not equal the defaults. Stadium must
   be frozen. The recorded frozen toggle or the source-reviewed older transformation-bypass patch proves
   that property; an older false toggle alone does not prove unfrozen play.
   Unknown modifications require source review. Header and hook checks cannot
   establish the absence of an arbitrary unrecorded modification.
6. **Incomplete or unreconstructible history.** Require the normal -123 seed,
   contiguous finalized rows and a recorded Game End. Reject missing initial
   history, frame gaps and midgame/savestate-injected captures. An absent optional
   metadata `lastFrame` is allowed when the event history is complete.

Admission is independent of comparison results. New captures need recording
provenance, these property checks and a complete strict run. Do not shorten a
failing recording, omit inconvenient rows or add tolerated mismatch snapshots.
The aggregate contains 512 exact recordings across 148 manifest-listed
fighter/stage pairs. This is pair coverage, not a claim
of exhaustive move/branch coverage or every fighter on every stage.

Selected ineligible fixtures remain for diagnostics and rejection tests;
unused captures are removed while their exclusion identities remain in the ledger.
Manual inspection can use `--diagnostic` with positional replay paths. Frame limits,
comparison start frames and signed-zero diagnostics cannot be used with a suite
or to write output locks. Diagnostic successes print `DIAGNOSTIC`, never `PASS`.
The low-level C replay API also remains available for source investigations.

## Recording provenance

- [Yoshi/Bowser](validation/yoshi_bowser_provenance.json): original downloads and
  decoded recording identities.
- [Mewtwo](validation/mewtwo_provenance.json): controller-driven reflection and
  absorption captures, controls, Dolphin build/settings and hashes.
- [Game & Watch](validation/gamewatch_provenance.json): corpus and scripted
  Judge/Chef/bucket captures, Dolphin build/settings and hashes.
- [CPU inputs](validation/cpu_provenance.json): synced/separated Ice Climbers
  captures and the diagnostic Peach recording.
- [Arithmetic profiles](validation/arithmetic_provenance.json): July Mewtwo and
  Game & Watch captures, first-writer operands, profile comparisons and limits.
- [Roy](validation/roy_provenance.json): corpus sources, controller-driven CPU
  capture and console network metadata sources.
- [Pichu](validation/pichu_provenance.json) and [Kirby](validation/kirby_provenance.json):
  original corpus sources, scripted copy/special captures and unavailable recordings.
  Roy/Pichu copy recordings were unavailable at integration; source-callback smoke
  covers those two copy owners.

Provenance describes the original captures, including excluded recordings.
Active suite manifests select the admitted subset; the admission ledger
owns eligibility. Historical entries may describe files removed from the tree.
Keeping a capture for a diagnostic test does not admit it.

The interaction coverage tests inspect what a recording actually contains.
Callback smokes exercise source paths with extracted data; they are not
independent Dolphin recordings or exhaustive branch coverage.

The scripts in `tools/validation/record_*_interactions.exs` run from an ExPhil
checkout with its compiled bridge and libmelee_ex dependencies. For example:

```sh
devenv shell -- elixir -pa '_build/dev/lib/*/ebin' \
  /path/to/melee-sim-light/tools/validation/record_mewtwo_interactions.exs \
  missile /absolute/fresh/output /path/to/dolphin-emu-headless /path/to/melee.iso
```

Keep each recording's executable hash, arithmetic settings, controller schedule,
source and decoded-SLP hash. Git LFS owns compressed-file identity; the replay
owns its frame/player metadata. Use ordinary controller inputs and finalized
games; do not inject positions, actions, damage or savestates into capture fixtures.
Store new validation replays as `.slpz`; direct tooling also accepts `.slp`.
Forensic outputs and incomplete recordings belong under ignored `reports/triage/`.

## Recording arithmetic

Suite entries accept `fnmsubs_profile: "retail" | "dolphin-legacy"`; the CLI
option is `--fnmsubs-profile`. Both select the existing match capability before
initialization. Omission retains the metadata/scene default. Those defaults are
capture conventions, not proof of a historical executable's identity. Explicit
changes require [reviewed provenance](validation/arithmetic_provenance.json)
bound to the decoded recording hash. Profiles are reported in results and
preserved in benchmark tapes/cache identities without changing comparison rules
or public RL defaults.

Retail `fnmsubs` negates after fused subtraction, `-(a*b-c)`. Legacy Dolphin can
fuse `c-a*b` instead, changing an exact zero's sign; `atan2f` may turn that sign
into a different DI angle. The provenance record links both JIT implementations,
July first-writer operands and prior historical writer investigations. These
establish instruction behavior, not unavailable build identities. Do not retry
profiles automatically or label every float residual an arithmetic limitation.

`make fnmsubs-smoke` checks cancellation, signed zeros and rounded underflow.
The admitted `legacy_arithmetic.json` fixtures and the explicitly profiled Falcon,
Peach and doubles games compare strictly. Tests alternate profiles on one
persistent runner through the diagnostic API to check isolation and retain
negative controls; these diagnostic results cannot write acceptance locks.

## Recorded CPU inputs

CPU replay ports require a level in 1..9. Source player slots and AI callbacks
remain active. Exact processed sticks, trigger and buttons enter at Slippi's
`Playback/Core/RestoreGameFrame.asm` boundary (GALE01 `0x8006B0DC`), after AI
sampling and before input edges/timers. Physical controller bytes remain separate.
Validation also supplies `Recording/SendGamePreFrame.asm`'s RNG observation;
Slippi playback itself restores RNG there only when resync is enabled.
This lane never restores position, action, damage or velocity and does not expose
autonomous CPU control through the public RL API.

Nana keeps source follower AI inputs for human and CPU Popo alike; the recorded
input hook skips followers. The Ice Climbers suite covers synced and separated
followers, but neither recording contains a solo Nana death/respawn wait.
The diagnostic Peach fixture tests input restoration over 5,000 transitions;
its full recording has 18 strict knockback-Y sign differences at 5109–5126 and
is outside every acceptance suite. That prefix is not validation coverage.

Controller-only benchmark tapes cannot represent CPU processed inputs. Direct
export rejects them; `make benchmark-prepare` reports CPU exclusions and retains
selected admitted human recordings. A CPU-only suite is an error. Tape format v3
includes the private CPU-level config byte; rebuild native/Wasm stream consumers.
