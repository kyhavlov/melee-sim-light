# Source-Shaped Melee Core

Status: Phase 6 is complete. PPC32, native x86-64, and wasm32 compile the tracked canonical
gameplay source directly. One immutable `GameData` now serves independent resettable matches; the
parallel batch-first C API, location-independent same-build savestates, persistent validation
workers, Wasm lifetime/differential target, and production live-viewer cutover are implemented.
The 153-replay inventory remains locked at 63 strict passes plus 90 exact source classifications
over 1,415,476 transitions, with no new or widened exception. The existing public `src/api.h` and
Python simulator remain untouched pending the explicit replacement cutover.

Branch base: `core-rewrite` at `6fbcc9bc7719` (`Rewrite core contact and motion state ownership`).

Decomp source pin: doldecomp/melee `91b9789fa6539ea847998a6ed330fa748fa200ec`, the
latest commit on upstream `master` when Phase 2 source import began. The independent ignored
checkout under `refs/melee/` remains clean at this pin for the milestone.

## Goal

Build the replacement simulator core from faithful, headless copies of the original gameplay
source. The PPC32 scalar runtime establishes the correctness oracle. The native x86-64 scalar
runtime compiles the same gameplay implementation and is the future production path, followed by
supported-domain expansion and measured batching/layout optimization.

## Adoption decision

The source-shaped runtime is the chosen direction for the future core rather than an experiment
that will remain only an external oracle. This decision does not authorize an early runtime bridge
or partial public cutover:

- PPC32/QEMU remains the high-fidelity reference while the native runtime is brought up;
- native and PPC targets must compile one gameplay implementation, not fork into parallel semantic
  ports;
- the existing simulator core remains available only until the native source-shaped runtime owns
  the supported domain and public API, then the displaced implementation is deleted;
- Phase 2.5 closed generalized Fox/Fox FD replay parity before any file movement;
- Phase 3 performs renaming, final source organization, and upstream-delta patch standardization
  as one behavior-neutral change;
- native x86-64 work begins only after those two checkpoints are independently validated.

## Agreed architecture

### Parallel source tree

The replacement core lives under `src/melee_core/` and does not link existing simulator gameplay
objects. `gameplay/` is the canonical repository-owned source tree and preserves upstream-relative
paths. PPC and native compile it directly. Core-owned headless behavior lives under `runtime/`,
hosted OS/ABI replacements live under `platform/`, and audited exclusions remain under `stubs/`.
The locked upstream commit and inventory remain provenance inputs, not a second gameplay tree.

Existing build and test conventions may be reused. Existing gameplay implementations, including
`src/decomp/lb/lb_00ce.c`, are not runtime dependencies of the new kernel; the corresponding latest
upstream owner is copied into the new tree when needed.

### Correctness before production architecture

The initial runtime is a standalone 32-bit, big-endian PowerPC Linux scalar C program run under
QEMU user-mode emulation. It may retain original global state, pointer-rich AoS structures, heap
allocation, and HSD allocator behavior. These are not Phase 1 failures. PPC32 preserves the
original pointer widths, byte order, ABI-sized Dolphin typedefs, and substantially reduces changes
to runtime structures and archive relocation logic.

The first implementation is not embedded in CPython. Input and state comparison use the existing
packed validation wire layouts through a standalone file interface. Phase 4 owns explicit native
64-bit runtime layouts and initialization-time DAT translation. A later production phase owns
per-world state, SoA/AoSoA storage, batching, Python integration, and measured structural
optimization.

The build uses a repository-local cross-toolchain/sysroot and QEMU runner so it does not mutate the
host toolchain. A native 32-bit x86 build was proven feasible, but rejected for Phase 1 because it
would still require an endian-aware parallel DAT/object representation and would not improve PPC
floating-point fidelity. PPC Linux is not identical to the GameCube ABI or hardware, so reached
ABI and float differences remain explicit correctness work rather than assumed solved.

### Source import policy

Import complete translation units and source-owner families, not selected functions. Begin with the
dependency closure for two Fox fighters on Final Destination and expand when a real source call
graph requires it. Do not import every supported fighter and stage merely to silence link errors.

Track the imported set in the checked-in lock, exact file list, and manifest. Import new complete
source owners with `tools/melee_core/source_sync.sh add`; then edit the canonical files directly.
Keep true platform policy in `platform/` rather than scattering it through gameplay source, and
keep each gameplay delta source-backed beside the code and in the delta ledger when it matters for
later upstream contribution.

The canonical gameplay tree carries a local `.clang-format` with `DisableFormat: true`; source-sync
treats that file as repository metadata rather than part of the pinned upstream inventory. The root
`make fmt` also prunes `src/melee_core`. This prevents recursive or direct file-based clang-format
runs from rewriting decomp-shaped gameplay source while leaving local runtime/platform formatting
under explicit review.

`src/melee_core/upstream_delta_ledger.tsv` classifies each new Phase 5 decomp-file delta while its
evidence is fresh. `upstream-candidate` means the pinned nonmatching C body itself appears wrong and
the fix can plausibly become a small isolated decomp contribution. Hosted portability, explicit
float-operation spelling, source imports, and headless policy remain recorded beside those
candidates so later upstream preparation does not confuse a correct port adaptation with a retail
source correction. Historical patch identifiers remain in the ledger for provenance; new rows name
the canonical source owner or focused change directly.

Use nonmatching C implementations when they exist and record their status. Inspect or port PPC
assembly only when a reached gameplay owner has no adequate C body or produces relevant
uncertainty.

### Headless bootstrap

The bootstrap is a minimal C wrapper. It loads the required game data, initializes HSD, stage,
player, fighter, and scheduler state through source functions, injects controller input, advances
the original frame schedule, and exports observable state. It does not port menus, scene
transitions, CSS, rendering setup, or the full `gm` frontend unless a source initialization
dependency proves necessary.

### Validation wire interface

Reuse the existing packed wire contracts from `src/api.h` without linking existing gameplay code:

- one `MslCoreMatchConfig` row configures the match;
- one optional `MslCoreInput` row establishes the previous controller sample for the first frame;
- `N` contiguous `MslCoreInput` rows drive `N` free-running frames;
- `N` contiguous `MslCoreCompare` rows record the post-frame result.

The Phase 1 command accepts separate config, initial-previous-input, input-tape, and compare-output
paths. A missing initial previous row means neutral input. `MslCoreInput` retains all four player slots,
the existing `MSL_BUTTON_*` mask, raw signed stick lanes, and raw trigger lanes. These are physical
controller samples; later UCF processing belongs inside the new runtime.

`MslCoreCompare` is the primary output contract. Phase 1 populates every reached source field and uses
source-correct neutral/default values for systems not yet implemented. The on-disk representation
is the current little-endian validation layout even if the selected 32-bit runtime target is
big-endian; the host adapter encodes fields explicitly when native packed writes would differ.
Existing `melee_sim.dtypes.input_dtype()` and `compare_dtype()` must be able to read and write these
rows without field translation.

Do not use the current `MslSeed` to initialize this runtime. It contains hidden state designed for
the existing simulator and would create a reconstruction bridge. Phase 1 validation is
free-running from source match initialization. If later one-step validation needs arbitrary
reseed, define a decomp-native snapshot/restore format around the new runtime's real source state.

An optional diagnostic trace may expose source-only internals such as exact animation frame,
installed callbacks, `CollData`, and JObj state. That trace is forensic output and is not part of
the stable validation contract.

### Game data

Prefer the decomp's own DAT path:

- `HSD_ArchiveParse` and the HSD public/extern relocation machinery;
- `lbArchive_InitializeDAT` and the source archive wrappers;
- source fighter and stage consumers such as `ftdata` and `grdatfiles`.

Host code replaces DVD/file I/O, but the Phase 1 PPC32 process passes raw big-endian DAT bytes to
the original archive relocation machinery. Do not add fighter- or stage-specific schema
translations merely to make one asset boot. If a later native little-endian port requires a
parallel hand-written model of the complete object graph, stop and reassess a source-backed
preprocessing boundary.

`refs/melee-disc` and `SSBM.iso` remain ignored local inputs and are never committed.

### Stubs and exclusions

Every unresolved symbol is classified as an imported gameplay owner, host/platform replacement,
presentation-only semantic stub, supported-domain exclusion, or unresolved source gap.

Presentation stubs may no-op only after checking RNG consumption, callbacks, state writes, object
lifetime, and cleanup effects. Unclassified and supposedly unreachable stubs fail loudly when
called. Maintain a ledger with the source function, classification, call sites, preserved side
effects, and evidence for deletion or exclusion.

### Float policy

Preserve source operation order and explicit `f32` storage. Do not build a general PPC floating
point emulator during bring-up. Add source-backed PPC compatibility helpers when focused
comparisons show a consequential difference in a reached owner.

### Upstream policy

Do not continuously rebase copied source during a vertical. Finish and validate a milestone
against its recorded pin, then review upstream changes deliberately.

## Phase 1 — Fox/FD scalar movement MVP

Deliver a standalone PPC32 Linux executable, run headlessly under QEMU, that boots two Fox fighters
on Final Destination from real game data and advances the original source scheduler through a
bounded locomotion input tape.

Phase 1 included:

- a separate deterministic build target and 32-bit toolchain preflight;
- the checked-in source manifest and copied source-owner closure;
- host replacements for process entry, assertions/reporting, file I/O, and required SDK calls;
- the HSD GObj/GObjProc schedule and the AObj/FObj/JObj animation path required by the fighters;
- source stage, player, fighter, action, script, physics, and map-collision initialization;
- a minimal C bootstrap for two Fox fighters on Final Destination;
- the `MslCoreMatchConfig`/`MslCoreInput`/`MslCoreCompare` standalone file interface described above;
- input injection and compare export for idle, walk, turn, dash, run, jump, fall, and landing paths;
- a stub/exclusion ledger whose unexpected entries abort at runtime;
- repeatable multi-frame `MslCoreCompare` output for a fixed `MslCoreInput` tape;
- a thin Python validation adapter that writes and reads the existing NumPy dtypes and invokes the
  standalone executable once per replay sequence or chunk, never once per frame;
- focused smoke tests for archive loading, bootstrap, scheduling order, locomotion, landing, and
  deterministic repeated output.

Phase 1 did not include fighter-vs-fighter hit resolution, shield/grab/throw, stocks and respawn,
Fox articles and specials, UCF, other fighters or stages, Python integration, batching, allocation
locks, or performance gates.

Phase 1 was complete when the executable built from a clean tree with one documented command,
loads real Fox and Final Destination data, runs the agreed locomotion tape without reaching an
unclassified stub, and produces identical state output across repeated runs. Successful compilation
or a hand-constructed fighter state alone is not the MVP.

## Phase 1 result

Phase 1 meets the vertical's MVP bar at the pinned decomp revision:

- `build/melee_core/ppc/melee-core-ppc` is an ELF32, big-endian PowerPC Linux executable;
- QEMU user mode runs it locally against ordinary files under `refs/melee-disc/files`;
- raw `PlCo.dat`, `PlFx.dat`, `PlFxNr.dat`, and `GrNLa.dat` pass through the copied source archive
  relocation and lookup owners;
- two Fox source `Fighter` objects start at the source-backed FD neutral positions for slots 0/1;
- one copied HSD GObj scheduler tick owns each simulated frame;
- the fixed 200-frame tape reaches Wait, walk, turn, dash, run, knee bend, jump, fall, landing, and
  Ottotto/OttottoWait without reaching a loud unresolved stub;
- two fresh processes emit byte-identical `MslCoreCompare` output for that tape.

The compiled closure is 98 decomp C translation units plus the complete header/type substrate. Of
those C files, 84 are byte-for-byte copies and 14 have host-port edits. The material edits are
bounded to Fox-only registry projection, headless fighter construction, hosted ELF/linkage or
prototype compatibility, DAT reads in place of DVD/ARQ, and renderer omission. The manifest records
every compiled C owner and whether its local copy differs from the pin. No current simulator
gameplay object is linked into the executable.

The most important stub-classification finding was `ftCo_80096CC8`: treating it as an excluded
input check rejected every floor because it is actually the normal floor/platform admission
callback shared by Fall and Jump. The MVP now compiles the exact `ftCo_FallSpecial.c` owner. Moving
to the correct FD slot positions similarly exposed and pulled in exact StopWall and Ottotto source
owners. These are useful evidence that loud stubs plus a vertical tape expand the source boundary
more safely than permissive blanket no-ops.

### Build and test

No host packages need to be installed on the current machine. The first command invokes
`tools/melee_core/setup_ppc32_toolchain.sh`, which uses the existing `apt-get` and `dpkg-deb`
commands to download Debian cross-compiler, PPC sysroot, and QEMU packages and unpack them only
under ignored `build/melee_core/`. It does not run `apt install`, use `sudo`, or alter the host.

```bash
make -f src/melee_core/Makefile -j2 runtime
make -f src/melee_core/Makefile -j2 test
```

The isolated Makefile deliberately does not include the main repository Makefile. PPC compilation
does not start Python; the separate `validator` target asks the selected interpreter for
its C include directory once. `test` runs five bounded native-source smokes followed by the full
2,723-transition starter replay through the native streaming validator. Python-side math-library
thread counts remain pinned to one. The focused commands are:

```bash
make -f src/melee_core/Makefile -j2 smoke
make -f src/melee_core/Makefile -j2 validation-smoke
```

The ordinary repository-wide pytest entry point still requires the existing simulator's generated
`data/manifest.json`. This worktree does not have that generated package, so the isolated
Melee core Makefile remains the authoritative command for this vertical.

### Standalone interface

```text
melee-core-ppc GAME_DATA CONFIG PREV_INPUT_OR_- INPUT_TAPE OUTPUT
```

The fixed wire sizes are 38-byte `MslCoreMatchConfig`, 32-byte `MslCoreInput`, and 1022-byte
`MslCoreCompare`. Multibyte fields are explicitly decoded and encoded little-endian, so the files are
directly compatible with the current NumPy dtypes even though the process is big-endian. The thin
adapter is `melee_sim/ppc_reference.py`; it writes one tape, invokes QEMU once, and reads the compare
array without a per-frame Python loop or process.

The PPC executable also has an internal pipe mode:

```text
melee-core-ppc GAME_DATA --stream
```

It reads one config, one previous input, and then raw input rows from stdin; it writes compare rows
to stdout as they are produced. Diagnostics and loud-stub failures remain on stderr.

Replay comparison is a deliberately small stdout-only diagnostic. Python resolves `.slp`/`.slpz`,
loads each replay once with Peppi, and passes `game.frames` directly to the native validator. From
that point onward C owns all frame work:

- the Arrow C Data Interface exposes Peppi's existing column buffers without NumPy conversion;
- C selects human ports and indexes the last occurrence of each rollback frame;
- C converts physical controller columns directly into the 32-byte wire rows;
- one duplex pipe streams those rows through one QEMU/PPC process while C drains its output;
- C compares each 1022-byte output immediately against replay-visible Arrow columns and retains
  only the first mismatch summary.

There is no generated-data dependency, per-frame Python loop, NumPy frame materialization,
temporary input/output tape, or replay-sized `MslCoreCompare` allocation. The native path uses fixed
64 KiB pipe buffers, one compare row, and an O(number of raw frames) `int64` rollback index. It
compares all exported fighter and item/article fields bit-for-bit. Pointer/allocation identity that
Slippi does not own is deliberately not part of the wire contract. It does not write or refresh
validation reports. The implementation is `bindings/msl_replay_validate.c`; the Python file below
only builds/loads it, asks Peppi for the replay, and formats the small returned summary:

```bash
.venv/bin/python -m tools.melee_core.validate_replay \
  replays/validation/aggregate_recent/Game_20260514T181413.slpz
```

`--frames N` bounds an exploratory run. `--start-frame F` still advances the PPC runtime through
every preceding replay input, but begins comparison at frame `F`; this lets later source-owner gaps
remain visible behind a known match-opening mismatch. Multiple replay paths can be supplied to one
command, which builds and imports the validator once; `--no-build` skips even the no-op Make check
for repeated runs. Both runtimes resolve original archives from `$MSL_DATA_DIR/raw`.

The canonical parallel suite command is:

```bash
make -f src/melee_core/Makefile validation-suite
```

It reads `replays/suites/aggregate_recent.json` rather than maintaining a second replay list. The
default Phase 5 scope selects Fox/Falco across all six legal stages: 32 of the aggregate suite's 97
entries. `VALIDATION_CHARACTERS`, `VALIDATION_STAGES`,
`VALIDATION_BACKEND`, `VALIDATION_WORKERS`, and `VALIDATION_FRAMES` may narrow an exploratory run;
the committed default remains the forward completion gate. For example, the current exact controls
on both backends are:

```bash
make -f src/melee_core/Makefile validation-suite \
  VALIDATION_CHARACTERS=Fox VALIDATION_STAGES=32 \
  VALIDATION_BACKEND=both VALIDATION_FRAMES=1000
```

Auto worker selection uses up to 16 replay runners. A bounded Python thread owns Peppi loading for
one replay at a time, then passes its Arrow buffers directly to the C extension; there is no
per-frame Python work or Arrow/NumPy materialization. The C stream boundary releases the GIL and
uses `posix_spawn` plus atomically close-on-exec pipes to launch isolated scalar runtimes safely
from concurrent workers. Builds and raw-data verification happen once before fan-out. Results are
printed in suite order with per-replay pass/fail/error, first mismatch, runner FPS, and a backend
summary containing compared frames, wall time, aggregate FPS, and summed runner CPU time.

The input wire carries Slippi's physical controller samples, not only the processed Fighter input
floats. This distinction is required for UCF: `Recording/SendGamePreFrame.asm` records processed
input from `Fighter` and separately recovers the current raw stick bytes from Melee's five-frame
hardware ring, while `Playback/Core/RestoreGameFrame.asm` restores raw X specifically to preserve
UCF dashback. UCF therefore belongs inside the runtime at its original phase owners rather than as
a lossy preprocessing transform:

- pad-buffer publication and optional 1.0-cardinal snapping from
  `refs/ucf/src/pad_buffer/pad_buffer.cpp`;
- Turn-frame dashback from `refs/ucf/src/dashback/dashback.cpp`;
- shield-drop, SDI/shield-SDI, tumble, and DBOOC injections from their sibling `refs/ucf/src/`
owners.

The match wire also carries narrowly scoped runtime capabilities rather than treating scene ID as
an execution profile. Scene major 8 selects the online capture's scalar-`fnmsubs` zero-sign
behavior and installs `BrawlOffscreenDamage`. Slippi Dolphin profiles also select that call-site
patch outside the online scene; the tracked offline `metadata.playedOn = "mainline dolphin"`
replay retains retail zero signs, while the Nintendont profile retains the vanilla magnifier
owner. This split is source-visible at
`refs/slippi-ssbm-asm/Online/Core/BrawlOffscreenDamage.asm` and was confirmed at its
`Fighter_8006A360` call site, rather than inferred from replay names.

The current simulator's `src/ucf.c`, `src/input.c`, `src/locomotion.c`, and `src/action.c` were useful
cross-checks for the wire semantics and phase placement, but remain independent gameplay code and
are not linked into this port. Phase 2 implements all eight UCF 0.84 modules, optional 1.0-cardinal
snapping, neutral spawns, Brawl offscreen damage, frozen DeadUpFall physics, and the Slippi L-cancel
patch at their original owners. Ice Climbers- and Dream Land-specific patches are outside the
Fox/FD boundary.

The initial starter replay is
`replays/validation/aggregate_recent/Game_20260514T181413.slpz`, a 2,723-transition Fox/Fox FD
game. Its aggregate suite enables UCF and 1.0 cardinals. The entire input tape now completes in one
PPC process without reaching a loud stub and compares bit-exactly:

```text
frames: 2723/2723 processed=2723/2723 seed_frame=-123 first_ref_frame=-122
PASS matched_frames=2723
```

The output includes source position, velocity, action/motion, animation frame/index, ground state
and line, jumps, stocks, damage/shield, hitlag/hitstun, identity/combo fields, RNG seed, all five
Slippi state-flag bytes, the Slippi L-cancel field, and complete replay-visible Fox article state.

### Upstream match status and risk

At pin `91b9789f`, upstream `configure.py` marks 237 of the copied C units matching and 27
nonmatching. This is a translation-unit classification, not a behavioral correctness percentage.
The checkout has no original DOL or generated objdiff `report.json`, so trustworthy per-function or
per-unit fuzzy percentages were not available without setting up a full matching-decomp build.

One copied owner, `ftCo_BuryWait.c`, is not listed by the upstream configure manifest. Two manifest
rows cover the header substrates rather than C translation units. Of the copied C files, 236 remain
exact, 28 have bounded host adaptations, and `camera.c` is patched at build time. Some copied units
are source substrate retained for owner completeness but are not linked in the Fox/FD binary; the
manifest's disposition column is authoritative.

The nonmatching set includes common action/combat owners, `ftcoll.c`, `ftdynamics.c`, stage and map
collision owners, item collision, camera, and several headless rendering units. “Nonmatching” still
means upstream has not proven identical generated code. For the reached starter path, exact replay
comparison tested those seams directly: the retail DOL supplied the missing camera operation,
hosted matrix code was corrected to PPC operation order, and the required MSL math/trig owners are
compiled instead of using approximate host replacements. Unreached variants in nonmatching owners
remain a risk when the fighter/stage boundary expands.

### Evaluation

The exploratory result is positive: this is demonstrably a viable way to obtain a source-shaped,
locally runnable scalar oracle, and it avoids reconstructing core action, animation, scheduler, DAT,
combat, article, and map-collision semantics by hand. Phase 2 also shows that the remaining decomp
gaps can be bounded and corrected at source seams rather than forcing a piecemeal simulator. It is
not evidence that a complete production simulator is a small final step: supported characters,
dynamic stages, four-player state, native layouts, reentrancy, batching, and allocation-free hot
paths remain substantial separate work.

## Phase roadmap

### Phase 2 — Complete Slippi Fox-vs-Fox gameplay on Final Destination

#### Objective

Produce one source-shaped scalar runtime that owns the complete supported Fox/Fox singles gameplay
boundary on Final Destination under arbitrary controller input, including the UCF and Slippi
behavior present in the replay corpus. The initial acceptance replay is
`replays/validation/aggregate_recent/Game_20260514T181413.slpz`; it must validate from match opening
through completion, but its first mismatch is evidence for choosing a source owner, not permission
to implement replay-row exceptions.

#### Scope

- Replace the minimal fighter bootstrap with the source match-opening, Entry, identity, stock, and
  match-flow state needed to reproduce Slippi-visible state.
- Complete the reached scheduler and callback phases for arbitrary Fox input: action changes,
  animation and script events, IASA, physics, collision, accessories, articles/items, and match
  flow. Preserve source callback ownership and ordering rather than compensating downstream.
- Close the full Fox/FD gameplay systems: normal attacks, hitboxes/hurtboxes, ProcessHit/DmgLog,
  hitlag/hitstun and damage states, shield and reflect, grabs and throws, Fox specials and fighter
  articles, death, stocks, respawn, and game end.
- Port UCF 0.84, 1.0-cardinal behavior, and gameplay-affecting Slippi patches at their original
  input/scheduler owners. Keep physical controller samples as the replay wire substrate.
- Extend native comparison to every replay-visible item/article field once those runtime owners
  exist; item count alone is only a Phase 1 diagnostic.
- Stub presentation, audio, platform I/O, and other genuinely headless-only paths when reached.
  Unreached source may remain for linker garbage collection. Do not delete or false-predicate
  gameplay-adjacent code merely because the starter replay does not exercise it.

Phase 2 does not include other fighters or stages, production SoA/batching, allocation removal,
native 64-bit layout conversion, or final throughput optimization. It must not link or bridge to the
old simulator's gameplay implementation to achieve intermediate validation progress.

#### Completion criteria

1. The 2,723-transition starter replay passes end-to-end against all exported fighter and complete
   item/article state, with UCF and applicable Slippi patches enabled.
2. The declared Fox/Fox FD source boundary is represented broadly enough for arbitrary legal
   controller input, based on direct decomp/source completion rather than replay-specific logic.
   No in-scope arbitrary-input path reaches a loud unresolved stub; the validation replay is the
   Phase 2 behavioral test rather than a separate synthetic test program.
3. Remaining stubs and exclusions are enumerated and source-backed as headless or outside Phase 2;
   there are no replay-, frame-, or dataset-keyed gameplay branches.
4. Repeated runs are bitwise deterministic, the isolated Melee core test suite passes, and the
   streaming validator can process the full replay without Python frame materialization or runtime
   failure.
5. Any reached nonmatching-decomp or PPC-float seam is either corrected/contained with source or
   oracle evidence, or remains an explicit blocker; it is not silently accepted as a successful
   Phase 2 result.

#### Phase 2 result

Phase 2 meets those criteria for the declared Fox/Fox FD boundary:

- the source match-opening and frame schedule reproduce all 2,723 starter transitions bit-for-bit,
  including Entry, arbitrary movement and attacks, combat, grabs/throws, shields/reflect, damage,
  death/stock/respawn flow, Fox specials, and Fox articles;
- complete UCF 0.84 and the applicable Slippi gameplay patches execute at their original source
  phases from physical-controller input;
- source effect dispatch is headlessly projected where it affects camera or RNG, using a
  fixed-capacity source-ordered queue and original particle command data rather than presentation
  no-ops that silently change gameplay state;
- exact camera behavior missing from the nonmatching C body is patched from the retail DOL seam,
  and reached matrix/math paths preserve PPC operation order closely enough for strict replay
  equality;
- all remaining stubs and exclusions are classified in `src/melee_core/stubs/ledger.tsv`;
  presentation/audio/platform paths no-op explicitly, and unsupported fighter, held-item, exotic
  capture, renderer, and other-stage paths remain loud or source-guarded outside this boundary;
- the isolated test target runs five source/substrate smokes plus the complete acceptance replay,
  and repeated fresh-process validation is byte-for-byte deterministic.

The binary remains the intentionally scalar PPC32/QEMU oracle. The wire does not export frontend
results-screen state or timer adjudication, and the declared boundary remains two human Foxes on FD
with ordinary items disabled. Those are later domain/frontend boundaries, not bridges to the old
simulator or replay-keyed exceptions.

### Phase 2.5 — Generalized Fox/Fox FD oracle closure

#### Objective

Close every source-owner gap exposed by all five tracked Fox/Fox Final Destination replays before
renaming the runtime or introducing native-platform differences. The starter remains exact; the
other four are generalization evidence, not row-local implementation boundaries.

#### Initial evidence

- `BlondHardHippopotamus.slpz` first differs at frame 515 in article Y position by two ULPs;
- `FavorableSuperficialPig.slpz` first differs at frame 145 by `+0.0` versus `-0.0` attack Y
  velocity;
- `HungryImportantSnake.slpz`, the P1/P4 case, first differs at frame -113 in Entry timing,
  position, animation, instance identity, and state flags;
- `PutridJoyousOryx.slpz` first differs at frame 2347 by `+0.0` versus `-0.0` attack Y velocity.

These are triage entry points. Fix their shared source owners and continue validation beyond each
first mismatch until no later divergence remains.

#### Scope

- Record signed-zero equivalence only as an opt-in diagnostic comparison that reveals later
  mismatches; gameplay-strict comparison remains the default and completion gate, with only the
  separately reported render-owned bit excluded as defined below.
- Complete source match bootstrap for arbitrary two-human port placement, including non-adjacent
  P1/P4 identity, Entry, spawn, controller, and scheduler ownership.
- Recover the exact source/PPC writers and operation order for attack velocity and article
  position; do not add tolerance, normalization, or replay-keyed behavior to gameplay code.
- Keep the current source paths and build organization stable during correctness work.
- Update the documented result only after the complete five-replay run is known.

#### Completion criteria

1. All five Fox/Fox FD replays pass gameplay-strict fighter and article comparison from match
   opening through their final recorded transition. Gameplay-strict means bit-exact comparison of
   every exported field except only `state_flags[player * 5 + 4] & 0x80`
   (`fp+0x221F_b0`), whose replay/render phase is not reconstructible and which remains separately
   counted rather than silently ignored. Signed zero remains bit-strict.
2. Repeated fresh-process runs are deterministic, the isolated Melee core tests pass, and no
   in-scope path reaches a loud stub.
3. Every gameplay fix belongs to a decomp/source owner rather than a replay, frame, or port-pair
   exception. The sole non-bit-exact classification is source-owner-wide and separately reported,
   not selected by replay identity or expected output.
4. The five-replay oracle result and any newly reached nonmatching/PPC seam are documented and the
   checkpoint is ready to commit before reorganization begins.

#### Progress notifications

During autonomous Phase 2.5 work, send a concise Discord update after material progress using:

```bash
tools/melee_core/notify_discord.sh \
  "Phase 2.5: REPLAY advanced from FRAME_OLD to FRAME_NEW after OWNER summary."
```

Material progress includes a replay advancing beyond its previous first mismatch, a replay becoming
strict-pass, a shared source-owner fix being validated across replays, or a genuine blocker that
changes the work plan. Do not send routine attempts or per-frame noise. Include the replay, old and
new first-divergence frames when applicable, and the source owner changed. The helper reads the
webhook from `MSL_CORE_DISCORD_WEBHOOK_URL` or the ignored local
`build/melee_core/discord_webhook_url`; never put the credential in tracked documentation or
reports.

#### Final gameplay-strict result and Nintendont render boundary

The full clean-build run currently has these results:

- `BlondHardHippopotamus.slpz`: strict pass, 10,174 transitions;
- `FavorableSuperficialPig.slpz`: strict pass, 12,185 transitions;
- `PutridJoyousOryx.slpz`: strict pass, 7,543 transitions;
- `Game_20260514T181413.slpz`: strict pass, 2,723 transitions, after classifying its offline
  mainline-Dolphin `BrawlOffscreenDamage` patch independently of online-scene math behavior;
- `HungryImportantSnake.slpz`: gameplay-strict pass, 8,729 transitions; one separately counted
  `fp+0x221F_b0` render-visibility diagnostic at frame 6030 and no later gameplay mismatch.

The starter discrepancy was not an unknowable render artifact. A bounded call-site probe showed
that mainline Dolphin replaces `ifMagnify_802FC998` with Slippi's root/bounds test even in its
offline versus scene. Passing `game.metadata.playedOn` through the native replay boundary and
splitting that patch capability from online `fnmsubs` behavior makes the complete starter strict
without changing Nintendont or online results.

Hungry is different. At frame 6029, the exact retail `Camera_80030BBC` projection and the PPC port
both project P4 to screen X `640.478271484375`, outside the source `[0, 640)` test. A normal retail
render therefore publishes `fp+0x221F = 0x80` for post-frame 6030, and Slippi playback under retail
code does so. The original Nintendont replay alone stores stale `0x00`, then stores `0x80` at 6031.

The retained source identifies the missing boundary. `gm_801A4D34` reads
`pad_queue_count = lb_80019894()`, advances `HSD_GObj_80390CFC` once for every queued sample, and
only then calls `HSD_GObj_80390FC0` once to render. A delayed console outer loop can therefore emit
two Slippi gameplay/post-frame records before the fighter draw republishes `x221F_b0`. Slippi's
frame-start, pre-frame, post-frame, and frame-bookend commands all execute inside the gameplay
process and contain neither the pad-queue depth nor a render-boundary marker. The optional console
polling-drift/visual-buffer codes in `refs/slippi-ssbm-asm/console_lag_pd*.json` further change this
scheduling profile, and their enabled state is also absent from the replay.

The free-running simulator therefore retains the smallest deterministic, source-correct headless
policy: one magnifier/fighter visibility render publication after each public simulator step. It
does not emulate VI/XFB timing, infer a render skip from an expected output byte, or teacher-force
the original replay's presentation schedule. Reproducing Hungry's stale byte strictly would
otherwise require an unrecorded hardware-clock phase/lag-code profile or a replay/frame-specific
exception.

The adopted oracle policy classifies only `state_flags[player * 5 + 4] & 0x80`
(`fp+0x221F_b0`) as a render-visibility diagnostic for every player. The runtime still computes and
exports the bit; the native comparator reports its mismatch count and first frame, masks only that
bit from the gameplay pass/fail result, and compares the other seven bits in the same byte
strictly. This lets validation continue through the remainder of Hungry rather than stopping at
6030. No gameplay behavior, replay/frame exception, tolerance, or signed-zero equivalence is
introduced. Two fresh-process Hungry runs produced the same 8,729-frame pass and the same single
diagnostic. The headless visibility/magnifier model remains because it can affect vanilla
offscreen damage. A future replay format could instead record render boundaries explicitly;
standard `.slp` files cannot reconstruct them.

### Phase 3 — Core reorganization and upstream-delta standardization

Adopt durable production naming and source boundaries in a behavior-neutral checkpoint:

- remove exploratory `decomp_port`, `phase1`, and `phase2` naming from final runtime interfaces and
  build outputs without retaining compatibility aliases for unreleased names;
- keep a pristine, pinned, minimal upstream source snapshot in the repository;
- represent necessary changes to decomp-owned files as a small ordered patch series, while keeping
  true host/platform replacements in separate source files;
- add a deterministic import/update/check command that records the upstream pin, verifies the
  pristine snapshot, applies the patches, and reports conflicts explicitly;
- remove build dependence on the ignored `refs/melee` checkout;
- require all five gameplay-strict Fox/FD results and the exact render-visibility diagnostic
  counts to remain unchanged across the reorganization.

This phase does not introduce native layouts or alter gameplay behavior. It creates the final
source shape in which native work will continue.

#### Phase 3 historical result

At the Phase 3 checkpoint, the core had these ownership boundaries:

- `src/melee_core/vendor/` is a pristine 914-file snapshot of the exact compiled upstream
  dependency closure at the locked commit: 249 C translation units and 665 headers;
- `src/melee_core/patches/` contains four ordered adaptations for hosted-C compatibility,
  headless Fox/FD execution, PPC source exactness, and retail-exact camera behavior;
- `src/melee_core/runtime/` owns the headless match bootstrap, scheduler-facing gameplay
  projections, wire protocol, camera, items, effects, and FD integration;
- `src/melee_core/platform/` owns hosted ABI, libc, filesystem, memory, and SDK replacements;
- `src/melee_core/stubs/` contains the audited headless and unsupported-domain exclusions;
- ignored `build/melee_core/source/` is the only patched materialization, while PPC objects and
  the executable live under `build/melee_core/ppc/` and the native streaming validator lives
  under `build/melee_core/validation/`.

At that checkpoint, `tools/melee_core/source_sync.sh` verified and materialized the pristine-plus-
patch representation. Phase 5.5 supersedes those commands and paths with the canonical-source
workflow documented below.

All five replay gates retain the Phase 2.5 counts: 2,723, 10,174, 12,185, 8,729, and 7,543
transitions pass gameplay-strict comparison. Hungry retains exactly one separately reported
render-visibility diagnostic at frame 6030; signed zero remains strict and has no diagnostics.

The patch series served as provenance during PPC/native scalar parity work. Phase 5.5 promoted its
validated result to canonical source before later whole-core ownership and layout changes.

### Phase 4 — Native x86-64 scalar parity

#### Objective

Produce a scalar x86-64 little-endian runtime that boots the same Fox/Fox FD domain from original
game data and reproduces the complete PPC oracle result. Native and PPC must compile one gameplay
implementation; native bring-up must not become a second semantic port.

#### Scope

- Add an isolated native build target and `build/melee_core/native/melee-core-native` executable
  with the same file and streaming wire interface as the PPC executable. Keep the PPC build and
  validation path working throughout.
- Begin with a bounded compile/layout inventory. Classify each failure as a disk-layout, pointer
  width, endian, ABI typedef, PPC instruction/math, or platform replacement issue before changing
  source. Do not paper over failures with broad casts, packed runtime structures, or permissive
  stubs.
- Separate raw game-file representation from native runtime ownership at initialization. Parse
  the original 32-bit big-endian DAT/archive representation explicitly, translate every reached
  pointer and scalar owner into native-owned data, and perform no endian or pointer bridge work in
  the per-frame gameplay path.
- Reuse source archive metadata, relocation information, declarations, and object ownership where
  sufficient. Add source-backed translation descriptions only where the raw format does not carry
  enough type information; do not add fighter/replay-specific object graphs merely to boot Fox.
- Correct reached 32-bit layout assumptions and PPC floating-point seams at their shared owners.
  Preserve explicit `f32` storage and source operation order. Do not introduce native-only replay
  tolerances, signed-zero equivalence, expected-output branches, or teacher-forced state.
- Extend the native streaming validator to select PPC or native execution without adding Python
  frame loops or materialization. PPC remains the differential oracle during bring-up; the Slippi
  replay result remains the final behavioral gate.
- Record equivalent-work PPC and native scalar throughput, including core frame throughput and
  end-to-end replay time. A native build that is not materially faster than QEMU/PPC requires
  investigation before Phase 4 is called complete, but batching and structural optimization remain
  out of scope.

Phase 4 does not add fighters or stages, promote per-match globals, redesign allocator/pool
layouts, introduce SoA/AoSoA or batching, integrate the production Python API, or cut over/delete
the old simulator. Normal native frame execution nevertheless obeys initialization-only
allocation. Do not build a throwaway 32-bit x86 runtime unless a concrete x86-64 blocker is first
demonstrated and documented.

#### Completion criteria

1. Clean PPC32 and native x86-64 targets build from the same materialized gameplay source, and the
   isolated source/smoke tests pass for both applicable platform paths.
2. Native passes all five Fox/Fox FD replays through their final recorded transitions with the
   exact current gameplay-strict results: signed zero remains bit-exact and Hungry reports exactly
   one separately classified render-visibility mismatch at frame 6030.
3. Repeated fresh native runs are deterministic. There are no native-only gameplay exceptions,
   replay/frame branches, tolerances, hidden PPC subprocesses, or bridges to the old simulator.
4. Every reached DAT/archive object is translated once during initialization into explicitly owned
   native data. The normal native frame step performs no raw big-endian field reads or pointer
   reconstruction.
5. The PPC result remains unchanged, the native/PPC differential is clean for bounded development
   probes, and the full replay gate uses the existing Arrow/native validation path without Python
   per-frame work.
6. Equivalent-work scalar timings are recorded. Native is materially faster than PPC/QEMU, or any
   contrary result remains a Phase 4 blocker with its measured owner identified rather than being
   deferred as generic future optimization.

#### Phase 4 result

Both `runtime` and `native` now compile the same materialized source closure. The native executable
retains the PPC file/stream wire protocol and the Arrow validator selects it directly; Python only
loads each replay with Peppi and hands its Arrow buffers to C. There is no PPC child, old-simulator
link, replay/frame behavior branch, native tolerance, or signed-zero relaxation in the native
path.

Native DAT ownership is explicit and initialization-only:

- the same decomp type-root translation unit is compiled for PPC32 and x86-64 with complete DWARF,
  and `generate_native_dat_layout.py` derives paired source/native field descriptions rather than
  maintaining a second hand-written layout tree;
- the original archive header, relocation table, public table, and packed big-endian body drive
  translation into a fixed-capacity, 32-byte-aligned native graph arena below 4 GiB. The low
  address is required only where HSD uses descriptor pointers as retail `u32` IDs;
- archive parses, public lookups, pointer targets, and action/color command streams are memoized.
  Fox action archives and effect banks are preloaded before the first public frame;
- initialization then protects every raw archive allocation with `PROT_NONE` and seals both DAT
  translation and `HSD_MemAlloc`. Source object/class pools are reserved up front, and normal
  stdio uses caller-owned buffers. The complete replay suite runs with all seals active.

The reached layout work was closed at source owners: pointer-sized HSD/class data, DAT bitfields,
command streams and loop state, fighter/item/stage tables, floor-line ownership, effect command
banks, PPC fused operations, and the typed decomp gaps exposed by native execution. In particular,
removing temporary arena padding exposed and fixed the shared color-command slot representation and
the misdeclared `ftData.x54` effect-part table; exact replay results no longer depend on allocator
spacing.

The clean validation result is:

- starter: 2,723/2,723 gameplay-strict transitions;
- `BlondHardHippopotamus.slpz`: 10,174/10,174;
- `FavorableSuperficialPig.slpz`: 12,185/12,185;
- `HungryImportantSnake.slpz`: 8,729/8,729 plus exactly one render-visibility diagnostic at frame
  6030;
- `PutridJoyousOryx.slpz`: 7,543/7,543.

Two consecutive fresh-process five-replay runs produced identical summaries. Signed zero remains
bit-exact. A clean dual-target build passed PPC and native data-load, model-animation,
map-collision, and scheduler smoke coverage (the raw in-place archive-layout smoke is PPC-only by
design). All five 1,000-transition PPC/native probes were identical, and the complete PPC starter
remained exact.

Equivalent full-starter timings were recorded over three runs with scalar `-O0` gameplay builds;
the table reports medians and includes match initialization and stream IPC in the runner boundary:

| Target | Runner seconds | Frames/second | Replay end-to-end | Relative |
| --- | ---: | ---: | ---: | ---: |
| PPC32/QEMU | 3.342378 | 814.7 | 3.372580 s | 1.0x |
| native x86-64 | 0.170300 | 15,989.4 | 0.177585 s | 19.6x runner / 19.0x end-to-end |

`make -C src/melee_core test -j4`, `make fmt-check`, `source_sync.sh check`, the clean build/smoke
gate, and the final deterministic replay gate pass. Phase 5 may expand source-owner coverage; Phase
6 still owns per-world state, batch integration, and measured layout optimization.

#### Progress notifications

Continue using the ignored Discord helper for material source-core progress through Phase 5:

```bash
tools/melee_core/notify_discord.sh \
  "Phase 5: MILESTONE, with current replay/source-owner progress and next blocker."
```

Send an update when the native source closure first compiles or links, native DAT translation first
boots the match, a replay advances materially or becomes exact, a shared native/PPC owner is
closed, a blocker changes the architecture or schedule, the five-replay gate passes, or the first
equivalent throughput result is known. Do not send routine compile attempts, small frame advances,
or repeated status noise. The helper reads `MSL_CORE_DISCORD_WEBHOOK_URL` or the ignored
`build/melee_core/discord_webhook_url`; never put the credential in tracked files or command output.

### Phase 4.5 — Data-root productionization

Make the existing ISO extraction pipeline the single setup path for both simulator cores. This is
contract cleanup, not a new gameplay-data representation: the source-shaped runtime continues to
consume original retail DAT archives, while the existing runtime continues to consume its derived
JSON/bin tables.

Objectives:

- use `MSL_DATA_DIR` as the canonical root, with original source archives under
  `$MSL_DATA_DIR/raw/` and generated tables elsewhere under the same root;
- extract the complete declared RL 1.0 source archive closure directly from a GALE01 revision 2
  ISO, including archives reached only by the source-shaped runtime;
- emit a deterministic local manifest with the ISO identity and exact source-file hashes, and
  reject missing, stale, wrong-region, or partial roots before simulation;
- keep the ISO, raw archives, and every ISO-derived output ignored and reproducible rather than
  committing game assets;
- make repeated extraction a fast no-op and keep fresh full-domain setup within the normal bounded
  command policy on the development host;
- remove normal build, smoke, and replay-validation dependence on `refs/melee-disc` or `_iso`
  symlinks while retaining explicit low-level extractor overrides for forensic work.

Completion requires a fresh one-command ISO setup, a measured incremental no-op, both native and
PPC source-core smoke/validation through `$MSL_DATA_DIR/raw`, the existing runtime's data-manifest
preflight, focused malformed/stale-root tests, and updated public setup documentation. Do not add a
persistent native DAT cache unless startup profiling later establishes a need.

#### Phase 4.5 result

Phase 4.5 is complete. `python -m melee_sim.extract_data --iso ...` now creates one ignored
`MSL_DATA_DIR` contract for both cores. Its `raw/manifest.json` identifies the full GALE01 revision
2 ISO by SHA-256 and records the exact path, offset, size, and SHA-256 of 72 source files. The
41.4 MiB raw profile covers all seven supported fighters/costumes, six legal stages,
common/player/item archives, and the supported character effect banks; this closes the prior
source-core omissions of `PdPm.dat`, `ItCo.usd`, `EfFxData.dat`, and `EfPrData.dat`.

The existing generated table manifest binds 184 outputs to the raw-manifest digest and records
their sizes and hashes. Extraction writes manifests last and atomically, repairs missing or changed
files, removes obsolete generated outputs while preserving the source-authored Slippi spawn table,
rejects non-GALE01 revision 2 input or partial profiles, and skips every derived generator when the
raw profile, schemas, and exact output inventory are current. Audit output is independent of the
chosen root path. Normal source-core builds, smoke tests, runs, and replay validation resolve
`$MSL_DATA_DIR/raw`; no normal path uses `refs/melee-disc` or `_iso`.

Fresh full-domain runs on the development host completed in 6.31--8.05 seconds. Verified
incremental runs completed in 0.81--0.85 seconds at about 39 MiB maximum RSS. PPC and native smoke
passed from the new root, and the complete starter replay remained exact on both targets. The full
repository suite passed in bounded alphabetic shards, followed by the final focused
extraction/data-contract gates. Four previously tracked ISO-derived JSON exceptions were removed;
only the source-authored Slippi neutral-spawn table remains tracked under `data/`.

### Phase 4.75 — Private scalar ownership seam

The standalone executable is now an I/O adapter over the private C interface in
`runtime/scalar.h`: `msl_core_game_data_init`, `msl_core_match_init`,
`msl_core_match_step`, and `msl_core_match_output`. The typed interface consumes host-decoded match
configuration and controller input; packed little-endian wire decoding and stdio remain outside
the gameplay step. Output is retained in match storage and exposed by const pointer, so the seam
does not add a per-frame allocation or intermediate copy.

`MslCoreGameData` owns the immutable data root and translated effect-bank catalog. Native DAT
translation still happens only during initialization, and the used native DAT arena is changed to
read-only protection before the first public frame. `MslCoreMatch` owns the current scalar
runtime's fighter/stage handles, configuration, frame/RNG state, output row, rule/UCF buffers,
camera-publication state, effect queue, and Slippi fighter metadata. Source callbacks without a
context argument bind to those caller-owned state blocks at init/step entry. A direct native/PPC C
smoke initializes and steps this interface without the executable or wire adapter; the native
allocation seal remains active during that step.

This is the production-shaped ownership seam, not the final in-process multi-match conversion.
Imported HSD/decomp globals, object lists, player/item tables, and allocator pools still retain
their source process-global topology inside one scalar runner. They are intentionally not hidden
behind a claim that two complete matches can already coexist in one address space. New Phase 5
headless state must enter `GameData` or `MatchState`, rather than adding parallel local globals.
Before persistent in-process validation workers, batching, or the public API cutover, the validated
materialized source must be promoted to canonical gameplay source and those remaining source
global owners must be context-promoted. SoA/AoSoA layout remains a later measured optimization.

The five Fox/Fox FD controls remain exact through all 41,354 transitions after the seam, at about
0.60 seconds / 69.4k aggregate native FPS with five workers. A 512-transition-per-replay bounded
PPC/native differential passes, and the canonical 23-replay Phase 5 gate remains unchanged.

### Phase 5 — RL 1.0 supported domain

Add Falco, Marth, Sheik, Zelda, Captain Falcon, the remaining five legal stages, relevant
items/articles and stage objects, character/stage-specific Slippi patch behavior, singles match
rules, and four-player/doubles scheduling. Compile each new source-owner vertical for both PPC and
native targets, and expand by shared source owner rather than replay row.

#### First packet — Falco, Battlefield, and frozen Pokemon Stadium

Complete the gameplay-relevant source-owner closure needed for Fox/Falco singles on Final
Destination, Battlefield, and frozen Pokemon Stadium. Extend the existing typed match
configuration and source bootstrap rather than adding a parallel executable or stage/character
path. Character data, animation/script data, articles, stage collision/joints, camera parameters,
neutral spawns, and frozen-Stadium/Slippi behavior must come from `$MSL_DATA_DIR` and the pinned
decomp/Slippi sources. Import complete owner translation units and tables where the reached call
graph requires them; do not implement the packet as a list of replay mismatches.

New immutable catalogs belong in `MslCoreGameData`; new headless mutable owners belong in
`MslCoreMatch`. Do not add another core-owned process global. Existing imported source globals may
retain their scalar-runner topology until canonical-source context promotion, but new source
owners must not create a second host-side state model. Native initialization must preload and seal
every newly reached archive/translation owner and size fixed pools for the expanded domain. A
normal native step may not read game files, translate DAT fields, or allocate. PPC and native must
continue compiling the same gameplay implementation, with PPC used for bounded differential
checks rather than as a hidden runtime dependency.

Keep the existing UCF 0.84/cardinals-1.0 input contract and implement any reached Falco/stage
Slippi modifications at their shared patch/source owners. Generalize character/stage dispatch from
configuration and data; do not branch in shared physics or collision code on Falco, Battlefield,
Stadium, replay identity, or expected output as a proxy for missing state. Preserve the current
strict float/signed-zero policy and the separately classified renderer/VI boundary; do not add a
new tolerance to complete this packet.

Completion requires:

1. `make -f src/melee_core/Makefile validation-suite` selects the existing canonical 23 entries and
   all 23 run through their final transitions without unsupported errors, matching under the
   existing validation policy. The five Fox/Fox FD controls remain exact through all 41,354
   transitions.
2. Native and PPC source builds, source-sync/data checks, direct scalar API smoke, and the core test
   target pass. Use bounded PPC/native replay differentials during development and a full native
   23-replay gate at completion.
3. Native allocation/DAT seals remain active for the complete suite. Any newly required raw or
   generated data updates the full extraction/manifest/runtime-required-key contract and passes a
   fresh-extract smoke; no game asset is checked into Git.
4. Changes retain decomp/Slippi/data provenance, contain no replay/frame-specific gameplay branch,
   and update the source manifest/patch ledger and this worklog. Record equivalent suite wall time,
   aggregate FPS, and peak RSS, but defer SoA/AoSoA, in-process batching, public API cutover, and
   broad performance restructuring.

The first Phase 5 packet is Falco plus Battlefield and frozen Pokemon Stadium. Its canonical
23-replay gate is already active through `validation-suite`. Before gameplay expansion, the current
native baseline reports the five existing Fox/Fox FD controls exact (41,354 transitions) and the 18
future-scope entries as explicit unsupported errors: 11 Falco-containing FD replays, three
Battlefield replays, and four frozen-Stadium replays. With 16 requested workers, that full baseline
completed in 0.66 seconds while validating the runnable 41,354 frames at 62.7k aggregate FPS, with
about 230 MiB peak process RSS. A controlled five-replay native comparison improved from 2.07
seconds / 20.0k FPS with one worker to 0.60 seconds / 68.8k FPS with five workers. Five repeated
concurrent native probes and a five-replay 1,000-transition native/PPC gate pass after replacing
thread-unsafe `fork` launch with `posix_spawn`.

#### First-packet progress

The complete native gate now runs all 23 replays and 218,302 transitions without an unsupported
runtime error. Nineteen replays pass strictly and four retain classified residuals. The latest
16-worker gate completes in 1.22 seconds at 179.1k aggregate FPS (11.23 runner CPU-seconds) with
205,040 KiB peak process-tree RSS.
`MotionlessAggressiveJay` and `GracefulAttachedTurtle` advanced from their first laser
shield-bounce mismatch through the replay end after patch `0046-guard-pose-float-exactness.patch`.
`PriceyPartialAlbatross` then advanced to a strict 7,937/7,937 pass after
`0047-ftcoll-damage-effect-argument-order.patch`, moving the gate from 16 pass / 7 fail to
19 pass / 4 fail.

The shared correction is source-owner exactness, not laser handling: GALE01 blends the live guard
SRT in `lb_8000C490`/`lb_8000C868` with a rounded second term followed by scalar-single `fmadds`,
and computes the live shield joint scale with two further `fmadds` in the
`ftCo_80091D58` owner. Hosted `-ffp-contract=off` had split those expressions and displaced the
shield contact normal by a few ULPs. The patch spells the retail boundaries explicitly and is
classified as `hosted-exactness` in `upstream_delta_ledger.tsv`; the upstream MWCC source already
emits the retail instructions.

`PriceyPartialAlbatross` was not a persistent-particle or broad RNG reconstruction problem. A
one-frame retail PC trace showed one normal-hit effect `HSD_Randi`, followed by the expected
DamageFlyRoll `HSD_Randf`, while the imported nonmatching C made an extra random-effect draw. GALE01
uses separate integer and floating ABI lanes at both DmgLog call sites: integer `r5` is
`HitCapsule::sfx_severity`, and `f1` is the DmgLog damage passed to the effect. The decomp C had
those arguments transposed. Patch `0047` corrects both call sites and is an `upstream-candidate` in
`upstream_delta_ledger.tsv`.

`PositiveRevolvingHyena` frame 5 is now source-classified rather than an open pose implementation
gap. A bounded retail interpreter probe captured the Falco right-thumb world matrix and
`PSMTXMultVec` result at laser creation. The port is bit-identical: the source spawn point is
`(0xc15d8daa, 0x41878af0)`, and the exact `(5, 0)` laser velocity publishes
`(0xc10d8daa, 0x41878af0)`. A direct JIT playback capture produced the same result. The replay,
whose metadata says only `playedOn=dolphin`, records `(0xc10d8db3, 0x41878af2)` and carries that
initial offset forward by exact five-unit X additions. Standard Slippi metadata does not identify
the recording emulator build or its paired-single/JIT mode, so reproducing those bits would mean
replacing source-correct matrix math with an unrecorded emulator-runtime profile. The strict bit
policy remains unchanged and the runtime retains the GALE01 result.

The four classified gate entries are therefore the vanilla magnifier/render-schedule damage tick
in `HilariousVillainousGiraffe`, the unrecorded Dolphin float-runtime profile in
`PositiveRevolvingHyena`, and the DeadUp render-position ULP boundaries in
`DelayedSuperbGuanaco` and `CornyDelayedOkapi`. Keep these classifications separate; do not add a
global float tolerance, replay/frame gameplay branch, or expected-output correction.

#### Complete-stream classification gate

The original native comparator stopped comparing after the first gameplay mismatch even though the
runtime continued to the replay end. Its `matched/frames` display therefore described an exact
prefix, not complete-replay coverage. The comparator now checks every emitted row in C and reports
total exact rows, mismatch rows and fields, exact prefix, last mismatch, strict suffix, field
families, and a stable fingerprint over every mismatching frame/field/expected-bit/actual-bit
record. Python still performs no per-frame comparison or materialization.

`replays/suites/melee_core_classifications.json` locks the complete native result and its source
evidence. The normal suite prints `CLASSIFIED` only when the entire snapshot matches. A changed
field, bit, count, range, or fingerprint is `FAIL`; a now-exact full replay is `XPASS` until the
stale classification is removed. Bounded/partial validation does not apply a full-replay
classification. Raw strict behavior remains available with:

```bash
make -f src/melee_core/Makefile validation-suite \
    VALIDATION_CHARACTERS=Fox,Falco \
    VALIDATION_ARGS=--strict-classifications
```

The complete-stream native result is:

| Replay | Exact rows | Mismatch rows | Classified fields | Strict suffix |
| --- | ---: | ---: | --- | ---: |
| `HilariousVillainousGiraffe` | 9,327 / 9,328 | 1 | `percent[1]` | 2,403 |
| `PositiveRevolvingHyena` | 10,627 / 11,980 | 1,353 | float fighter/article position and velocity; derived laser-angle bytes | 0, with exact rows interleaved through the replay |
| `DelayedSuperbGuanaco` | 12,794 / 12,914 | 120 | DeadUp P2 `pos_x` / `pos_y` | 5,758 |
| `CornyDelayedOkapi` | 12,939 / 13,063 | 124 | DeadUp P2 `pos_x` / `pos_y` | 10,988 |

`HilariousVillainousGiraffe` has exactly one 1% mismatch at frame 6802 and then matches every
remaining transition. This is the gameplay consequence of the already classified vanilla
magnifier boundary: `gm_801A4D34` may run multiple gameplay samples before a render republishes
`x221F_b0`, and the replay records neither that outer-loop schedule nor its render boundary.

`PositiveRevolvingHyena` is broader than the first laser anchor, but complete comparison does not
expose a new discrete source gap. Every residual is a float position/velocity or the recorded byte
of a laser angle derived from those floats. Bounded direct playback probes with the current Dolphin
build reproduce the port rather than the replay at three independent owners: thrown-state position
at replay post-frame 383, Falco special/root position at 1061, and shield-bounced laser position and
velocity at 6147. A 1,000-transition PPC/native run also produces an identical full mismatch
fingerprint. Standard metadata records only `playedOn=dolphin`; it cannot select the original
build's paired-single/JIT behavior. The manifest therefore locks all 2,246 exact residual records
rather than applying a float tolerance or blessing the initially observed laser row alone.

The two DeadUp classifications are bounded presentation intervals. Only the offline render-owned
P2 X/Y publication differs, after which 5,758 and 10,988 transitions respectively match strictly.
The default 23-replay native suite now reports 19 `PASS`, four `CLASSIFIED`, zero failures, and zero
errors across all 218,302 transitions in about 1.22 seconds / 178.7k aggregate FPS. The strict mode
continues to report the same four raw failures.

The packet completion checks pass: source snapshot/patch verification, raw-data validation,
native and PPC scalar smokes, native scalar/API/DAT/allocation-seal smokes, validation-runner
tests, and repository formatting. All five Fox/FD controls pass a 1,000-transition bounded
native/PPC gate, and `PriceyPartialAlbatross` passes both runtimes through transition 4,352, beyond
the corrected frame-4,351 RNG boundary. The four classified residuals intentionally keep
`--strict-classifications` nonzero; they are not hidden by comparator tolerances.

### Phase 5.5 — Canonical source promotion

Promote the validated patched materialization into tracked repository-owned gameplay source under
`src/melee_core/`, preserving upstream-relative paths. PPC and native targets must compile those
files directly rather than applying `vendor/` plus `patches/` into an ignored build tree. Delete the
materialization path as build machinery once direct-build parity is proven; do not retain two
authoritative gameplay trees.

Keep the upstream commit lock, exact source inventory, source pointers, stub ledger, and
`upstream_delta_ledger.tsv`. Preserve deterministic diff/export tooling against the pinned decomp
for upstream updates and focused contributions. Historical patch files remain available through
Git history, while later source expansion and ownership changes become ordinary canonical-source
edits rather than an ever-growing runtime patch stack.

This phase is behavior-neutral. Do not context-promote imported globals, add reusable match reset,
change the existing or replacement public API, or restructure gameplay bodies. Validation continues
to launch one fresh scalar process per replay; source promotion alone does not make the decomp's HSD,
player, item, stage, collision, scheduler, and allocator globals safely resettable.

As a bounded validation-speed experiment, build an optimized native validation profile at `-O2`
without fast-math, broad float reassociation, or implicit FMA contraction. Retain the existing
aliasing and source-exact math controls, and compare it bit-for-bit with PPC and the current native
debug build across the 23-entry gate. Keep and make it the normal full-suite backend only if outputs
and all four complete classification snapshots/fingerprints remain identical; otherwise remove the
experiment cleanly. Do not delay source promotion for speculative IPC or persistent-worker work.
The current one-frame 23-process measurement is about 0.27 seconds wall / 1.27 CPU-seconds, so
fresh-process startup is not the main full-replay cost.

Completion requires direct canonical PPC/native builds, source inventory/delta checks, current
smokes, a bounded PPC/native differential, and the full native 23-replay result unchanged. Commit
this phase independently before importing another fighter or stage owner.

#### Phase 5.5 result

`src/melee_core/gameplay/` is now the sole tracked gameplay-source authority. Its 988-file inventory
matches `upstream-files.txt`; 88 files differ from the pinned decomp after promotion of the exact
validated materialization. `tools/melee_core/source_sync.sh check` verifies the canonical inventory
and, when the locked checkout is available, its commit, pristine digest, and delta count. `diff`
exports a deterministic binary-capable patch against the pin, while `add` imports new source-owner
paths and refreshes the inventory lock. Builds do not require the checkout.

The old tracked pristine `vendor/`, 47-file patch series, and ignored materialization rule were
deleted. Their focused change history remains in Git and in `source_manifest.tsv` /
`upstream_delta_ledger.tsv`; future source expansion edits canonical owners normally.

A clean direct-source PPC/native build and both smoke paths pass. The five Fox/FD controls pass a
1,000-transition PPC/native gate, and the full native 23-replay gate remains 19 strict passes plus
the same four exact classification snapshots across 218,302 transitions. A bounded native `-O2`
build changed broad gameplay/float results, so it was rejected and removed; the proven source-exact
native compile profile remains the default.

### Phase 5 continuation — Current supported source domain

After source promotion, complete the declared Fox, Falco, Marth, Captain Falcon, Sheik, Zelda,
Jigglypuff, and Peach domain across Final Destination, Battlefield, Fountain of Dreams, frozen
Pokemon Stadium, Yoshi's Story, and Dream Land N64. Work by source-owner packet, not mismatch row. Import
complete reached fighter/stage/article/scheduler owners, preserve direct source structure, update
DAT translation and extraction contracts when new archive fields are required, and keep PPC/native
compiling the same canonical gameplay implementation.

The active validation inventory contains 153 distinct replays behind one composed
`melee_core_aggregate.json` manifest:

- `aggregate_recent.json`: 97 two-player entries across all six legal stages;
- `puff.json`: 15 two-player Jigglypuff entries imported from the dedicated new-character capture
  branch;
- `peach.json`: 20 two-player Peach entries spanning every source-reachable character-specific
  action-state row, including the two local special/item coverage demos;
- `doubles_recent.json`: 21 four-player entries across FD, Battlefield, frozen Stadium, Yoshi's,
  and Dream Land;
- the current Fox/Falco FD/Battlefield/frozen-Stadium gate selects 23 singles, leaving 74 additional
  singles plus all 21 doubles, or 95 new entries for this goal.

Use the character/stage suite manifests as focused development selections, but treat
`melee_core_aggregate.json` as the canonical union rather than maintaining another replay list.
The default supported-character filter executes all 153 entries.
Existing heldout manifests remain evidence against owner-local overfitting; do not implement
replay-specific branches or add a second state model to satisfy them.

Advance the default character/stage selection as each packet becomes supported. The canonical
supported-domain validation target schedules the composed aggregate through the existing parallel
native/Arrow path with deterministic stdout. Keep focused per-owner selections for development,
but do not require manual replay lists or a Python per-frame path to run the whole gate.

During source expansion, maintain a lightweight future-ownership ledger without restructuring the
runtime. Classify each newly reached mutable global, persistent pointer-bearing type, callback
family, allocator/pool, and immutable catalog as future `GameData`, `MatchState`, or explicit
presentation exclusion. Do not add new host-owned process-global gameplay state merely because the
imported source is still process-global. Record four-player high-water marks and state owners for
the later context/savestate phase.

#### Subphase A — Remaining legal stages for Fox/Falco

Add the Fountain of Dreams, Yoshi's Story, and Dream Land N64 source-owner closures before another
fighter. Cover collision/joint data, camera bounds, animated/dynamic platforms, Randall, Whispy,
stage items/actors, Slippi frozen or legal-stage patches, and headless presentation exclusions as
reached. All data must come from ignored `$MSL_DATA_DIR/raw` archives or existing source-backed
Slippi configuration.

The first gate adds the nine aggregate entries containing only Fox/Falco on those stages (three FoD,
four Yoshi's, and two Dream Land) to the existing 23-entry control. Run every selected replay through
its final transition without an unsupported exit, retaining strict results or explicit source-backed
oracle classifications. Commit this stage packet independently.

Subphase A result: complete. The canonical build now includes the complete reached `grIzumi`,
`grStory`, `grOldPupupu`, stage-actor generator, Shy Guy/Heiho, food, freeze, and ambient-item source
owners. Native DAT translation supplies stage params, spline/JObj/AObj graphs, and stage `itemdata`
from ignored extracted archives. Slippi's recorded FoD platform and Dream Land Whispy events enter
the corresponding source-owned fields; replay files with no event stream use the ordinary source
state machine. A 64-bit `GroundVars` union alias was made explicit at the actual FoD moving-platform
owner, eliminating an 8,934-row native-only divergence and restoring PPC/native parity.

The nine-replay focused native gate is three strict passes plus six exact classifications over
73,387 transitions. Five classifications are bounded float-only residuals with exact PPC/native
fingerprints and exact suffixes; the Dream Land `FlippantEnchantedHorse` classification records an
unresolved causal Falco-laser capsule gate where both source targets hit one frame before an
unspecified Dolphin replay. It preserves the entire full-stream fingerprint and is not a tolerance
or gameplay exception. The growing 32-replay Fox/Falco gate is 22 strict passes plus ten exact
classifications over 291,689 transitions at roughly 188k aggregate native frames/second.

Future ownership recorded by this packet:

- `GameData`: immutable GrIz/GrSt/GrOp archive bytes and translated JObj/AObj/spline, params,
  collision, stage-item article, and attribute graphs;
- `MatchState`: `stage_info`, Ground/GObj pools, FoD actor state, Randall/Whispy state, wind-device
  registrations, stage items, retained Slippi stage-event publications, camera state, and the
  post-frame gameplay matrix publication;
- explicit presentation exclusions: FoD EFB reflection, stage particles/lighting/audio, and
  Shy-Guy result/bonus bookkeeping.

#### Subphase B — Marth

Add the complete reached Marth fighter, motion-state, animation/script, collision geometry, effect,
item/article, and common-callback closure. Generalize shared dispatch from character data and source
tables rather than branching in common gameplay code on Marth as a missing-owner proxy.

The focused singles gate is the 19-entry Marth manifest: Marth dittos and Marth versus Fox/Falco
across the legal-stage aggregate. Retain all prior stage/space-animal results, compile PPC/native,
run bounded cross-target differentials while bringing owners up, then run every newly supported
Marth replay to completion. Commit the Marth packet independently.

Subphase B result: complete. The canonical build now compiles all five matching Marth source units:
fighter lifecycle/data registration plus neutral, side, up, and down special owners. The hosted
source registry publishes the complete admitted Marth callback rows, native DAT translation loads
Marth attributes, animation tables, costumes, and reached article graphs directly from `PlMs.dat`,
and the scalar/bootstrap character mapping admits Marth without a shared-system character proxy.

The common closure gained source-exact handicap and live match-standing publication for grab escape
timers, retail fmadds in both grab-timer expressions, and the retail fmsubs/fmadds boundary for
rotated animation velocity used by Marth SpecialHi. Old Slippi streams can disable the later
Cardinals 1.0 normalization per manifest entry while retaining UCF 0.84; the source dashback gate
uses signed facing-relative input. Headless effect initialization now catalogs every reached common
effect model's frame-zero generator events, and pure SSM metadata remains live even though audio
playback is excluded, preserving gameplay-visible HSD RNG ownership without a sound device.

The 19-replay focused gate is ten strict passes plus nine exact classifications across 179,882
transitions, with zero open failures. Native and PPC produce identical complete fingerprints. The
classifications comprise two magnifier damage ticks, two bounded Dolphin ULP profiles, four
DamageFlyRoll decisions whose deterministic pre-gate state is exact but whose excluded post-input
particle scheduler leaves replay stream phase unavailable, one mixed DeadUp/magnifier/RNG replay,
and one 2008 Nintendont stream whose P3 post-frame-only fields become absence sentinels for its final
710 rows. The RNG cases remain visible exact classifications rather than a bridge that would change
free-running rollout semantics.

The growing legal-stage singles gate now selects 51 aggregate entries and reports 32 strict passes,
19 exact classifications, zero failures, and zero errors over 471,571 transitions at roughly 175k
aggregate native frames/second. `Fox,Falco,Marth` is now the default validation character scope.

Future ownership recorded by this packet:

- `GameData`: immutable Marth fighter/archive attributes, motion and animation tables, costume
  graphs, reached article graphs, common effect model/generator catalog, and SSM metadata tables;
- `MatchState`: Marth fighter/special state, live match standings, player handicap/configuration,
  capture timers, effect queues, controller/UCF history, and per-frame RNG state;
- explicit presentation exclusions: audio playback/device handles, ongoing particle simulation,
  magnifier render publication, and legacy replay rows that omit post-frame observations.

#### Subphase C — Captain Falcon

Add the complete reached Captain Falcon source closure with the same data, callback, scheduler, and
headless ownership discipline. The immediately runnable aggregate subset includes Falcon dittos and
Falcon versus Fox, Falco, and Marth; Falcon versus Sheik/Zelda entries become runnable when the next
packet closes those owners.

Retain every prior result, run the newly supported Falcon subset through completion, and revisit the
full 22-entry Falcon manifest after Sheik/Zelda support lands. Commit the Falcon packet independently.

Subphase C result: complete for every Falcon replay whose opponent is in the admitted source
domain. The canonical build now compiles all five matching Captain Falcon source units: lifecycle
and data registration plus neutral, side, up, and down special owners. Falcon Dive's source capture,
release, and victim callbacks remain direct common/Captain call-graph owners rather than a validation
bridge. The hosted registry and native DAT translator load Captain attributes, motion tables,
costumes, scripts, and reached graphs directly from `PlCa.dat`.

Two shared source owners were completed while exercising the packet. Native DAT translation now
mirrors an archive's complete four-byte command-word address space into widened `CmdUnion` slots;
this preserves sequential script cursors and interior relocation targets when several actions share
a script suffix. Match state now owns the six retail respawn reservations, their 0x90-frame timers,
and the source `lbl_803B7A44` platform offsets used by stages with a shared spawn point. The latter
replaces the earlier FD-only spawn shortcut and fixes simultaneous Fountain/Yoshi's respawns without
character or replay conditions.

The immediately supported 19-replay Falcon gate is eleven strict passes plus eight exact
classifications over 156,548 transitions, with zero open failures. Complete native and PPC runs
produce identical fingerprints for all eight residual streams. Six are bounded Dolphin ULP motion
profiles with exact trailing intervals, one Nintendont stream contains only two accepted magnifier
damage ticks, and the manual `falcon_demo` records an unresolved Dolphin one-ULP root-motion result
that later changes a Yoshi's collision branch. That last classification locks the entire resulting
stream and stays explicitly unresolved; no tolerance or replay-keyed gameplay behavior was added.

The growing legal-stage singles gate now selects 70 aggregate entries and reports 43 strict passes,
27 exact classifications, zero failures, and zero errors over 628,119 transitions.
`Fox,Falco,Marth,Captain Falcon` is now the default validation character scope. The three Falcon
replays versus Sheik/Zelda remain intentionally deferred to the combined transformation packet.

Future ownership recorded by this packet:

- `GameData`: immutable Captain fighter/archive attributes, motion/animation command address space,
  costume graphs, and reached effect/data catalogs;
- `MatchState`: Captain fighter/special/capture state, the six respawn reservation timers and
  character owners, live spawn offsets, player state, effects, controller/UCF history, and RNG;
- explicit presentation exclusions: audio/rumble, particle rendering, magnifier publication, and
  emulator-build-specific paired-single/JIT results not present in replay metadata.

#### Subphase D — Sheik and Zelda

Treat Sheik/Zelda as one source-owner packet even though validation reporting remains per starting
character. Transformation makes their fighter data, callbacks, articles, object ownership, and
persistent state one gameplay boundary; splitting them solely for commit symmetry would create an
artificial partially supported runtime.

Add the complete reached transformation, needle, chain, special-move, animation, effect, item, and
common-callback closure. Run the Sheik and Zelda manifests, the deferred Falcon matchups, and then the
entire 97-entry singles aggregate. Preserve strict matching where the replay records the source
runtime and classify only bounded source/emulator boundaries with concrete evidence. Commit the
combined Sheik/Zelda packet independently.

Subphase D result: complete. The canonical build now compiles the complete reached Sheik and Zelda
lifecycle and special-state units together with thrown/held needles, Chain, Vanish, Din's Fire, and
Din's Fire explosion article owners. Transformation uses the retail two-entity player mapping: the
inactive half enters Sleep, the active entity pointer follows the source swap, and native
initialization translates and preloads both fighters' immutable DAT graphs before runtime. The
hosted registry, source item tables, article attributes, generic Slippi byte serializer, and
little-endian item-command decoder now cover the complete admitted transformation boundary.

Two shared source corrections were proven while closing the packet. Every `ftMapping_list`
consumer now reads the real character mapping rather than a nonmatching linker-layout alias. More
importantly, GALE01 `mpColl_80049EAC_LeftWall` loads the left-wall candidate count, while the pinned
nonmatching C used the adjacent right-wall count; correcting that owner removes the former
Yoshi's collision divergences in both `StraightScratchyCheetah` and `falcon_demo`. The latter falls
from 5,869 divergent rows with no suffix to 50 isolated ULP/signed-zero rows followed by 2,189 exact
transitions. The correction is recorded as an upstream candidate.

Replay playback now carries the source RNG observation at the fighter-pre scheduler boundary in
addition to frame-start RNG. This recreates the stream phase observed by Slippi after earlier
stage/effect callbacks without changing ordinary free-running semantics. Per-replay UCF metadata
can independently select Cardinals 1.0, Shield SDI, and SDI patches; the old pre-UCF Zelda/Falcon
stream therefore runs through the corresponding vanilla source gates rather than through an input
workaround.

The 20-entry Sheik gate is two strict passes plus eighteen exact classifications, and all four
Zelda fixtures pass strictly. The three deferred Falcon-versus-transformation streams add one
strict pass and two exact classifications. PPC/native fingerprints are identical for every replay
except `MixedAllQuetzal`: PPC has one isolated historical-needle byte, while native's FObj/JObj
evaluation moves a terminal Fox hurt capsule by about 0.01 units at one needle contact, producing a
bounded 662-row consequence and the same 3,382-transition exact suffix. Both backend snapshots are
locked explicitly. The complete legal-stage singles gate now reports 50 strict passes, 47 exact
classifications, zero failures, and zero errors over 848,202 transitions at roughly 177k aggregate
native frames/second. These original six fighters became the default validation scope at this
subphase boundary; the later Puff extension expands that default to seven.

Future ownership recorded by this packet:

- `GameData`: immutable Sheik/Zelda fighter archives, paired motion/animation tables, costume
  graphs, article catalogs and attributes, item command streams, and transformation mapping;
- `MatchState`: both transformation entities and active selector, needle/Chain/Din's Fire article
  state, fixed pools, player/UCF histories, frame-start and fighter-pre replay RNG observations,
  scheduler state, collision candidates, and live pose matrices;
- explicit presentation exclusions: Chain's unassigned fixed-pool metadata residue, render/VI
  magnifier publication, audio/particles, and emulator-build-specific float execution profiles.

#### Subphase E — Four-player and doubles

Close four-player source scheduling and storage last, after every fighter/stage owner used by the
doubles corpus is present. The 21-entry doubles manifest uses Fox, Falco, Marth, and Sheik across FD,
Battlefield, frozen Stadium, Yoshi's, and Dream Land. Implement the complete reached player/team,
friendly-fire, target iteration, collision/combat ordering, camera, stocks/death, item/article,
allocator/pool, and scheduler paths; do not special-case doubles replay identities or assume two
players in shared systems.

Run all 21 doubles replays through their final transitions and rerun the complete 97-entry singles
aggregate. Record four-player memory/pool high-water marks and every new persistent owner for the
later context/savestate design. Commit the doubles packet independently.

Subphase E result: complete. The replay loader now reconstructs nullable, split Arrow player rows
with an independent finalized-row cursor for each physical port, admits two- and four-player games,
and masks only source-valid absent post-frame rows for already eliminated players. Match startup
publishes all four Slippi neutral-spawn entries, applies the retail team-ordered spawn permutation,
loads the recorded Team Attack bit, and carries friendly fire through the source rules. The runtime
also implements the reached START-button team stock-share path directly from `gm_8016B918` rather
than treating eliminated teammates as absent inputs. A single canonical target runs the current
terminal inventories through the existing parallel Arrow/native runner:

```bash
make -f src/melee_core/Makefile validation-supported-domain
```

Two shared source corrections were proven while closing the packet. The simultaneous grab-pair
damage path now uses the zero-applied-knockback predicate present in GALE01 at `0x8008EE60` and
`0x8008F270`, correcting the pinned nonmatching `ftCo_Damage` transcription. Grounded fighter pose
publication now compiles the reached `lbbgflash` ground-IK chain plus source-backed `MTXRotRad`
instead of the former headless no-op. Both are recorded as upstream candidates; neither is a
doubles-specific replay adjustment.

The terminal native doubles gate is eight strict passes plus thirteen exact classifications, zero
failures, and zero errors over 183,980 transitions at roughly 80k aggregate frames/second. The
classifications preserve full-stream fingerprints. Their bounded owners are signed-zero/ULP
execution profiles, historical needle-pose bytes, presentation-owned DamageFlyRoll RNG phase, and
two backend-specific source profiles: PPC dynamic-bone evaluation can advance one Fox BODY contact
by one frame in `Game_20260509T012635`, while native/PPC FObj/JObj evaluation produces different
bounded pose residuals in `Game_20260509T012353`. Neither classification changes gameplay or stops
validation after its first mismatch. The complete singles inventory remains 50 strict passes plus
47 exact classifications.

A representative four-player run completed with the native sealed preallocation envelope intact
and no post-initialization allocation. Source allocator high-water marks by element size were 948
for 64-byte records, 780 for 24-byte records, 737 for the largest 48-byte graph pool, 373 for
40-byte records, 158 and 4 for the other 48-byte pools, 26 for 12-byte records, 24 for 96-byte
records, 10 for the 32-KiB fighter archive-work buffers, 7 for 5,416-byte records, and 6 for
800-byte records. Native initialization retains at least 256 spare records in every registered
pool, with larger explicit reserves for AObj, FObj, and ID pools.

Future ownership recorded by this packet:

- `GameData`: the existing immutable fighter/stage archives, translated graphs, callback tables,
  article catalogs, and stage collision data; four-player support adds no mutable catalog state;
- `MatchState`: four physical player slots and input histories, fighter/article entities, team and
  friendly-fire rules, stocks and stock-share state, spawn permutation, per-port replay row
  presence, scheduler/camera/render matrices, dynamic-bone/IK state, and all fixed pools;
- explicit presentation exclusions: renderer/VI visibility publication, effect/audio RNG work,
  and backend-specific float execution profiles already locked by exact classifications.

#### Subphase F — Jigglypuff extension

The post-goal Jigglypuff packet imports the five matching `ftPurin` lifecycle and special-state
source owners, publishes the exact fighter callback/data registry, and translates the reached
fighter, costume, attribute, and `EfPrData.dat` graphs into the fixed native initialization arena.
The separately allocated costume-hat JObj remains an explicit presentation-only headless exclusion;
the ordinary skeleton, live pose, collision, and gameplay callbacks are retained. Both PPC32 and
native x86-64 compile the same canonical packet.

Slippi replay playback now carries normalized gameplay axes separately from the physical raw pad
bytes consumed by UCF. This follows `refs/slippi-ssbm-asm/Playback/Core/RestoreGameFrame.asm`:
fighter input receives the recorded normalized axes while UCF's pending-input ring receives the
recorded raw bytes. Legacy captures may also declare `metadata.playedOn` and independent Cardinals
1.0 enablement once in the suite manifest. These are source-profile inputs, not per-frame Python
derivation or replay-row gameplay branches.

The 15-entry `puff.json` gate reaches every final transition as three strict passes plus twelve
exact classifications, zero failures, and zero errors over 114,171 transitions at roughly 135k
aggregate native frames/second. The classifications retain full-stream fingerprints and exact
suffix evidence where the stream returns to parity. Most residuals are signed-zero, bounded
hosted-float, or already-modeled DamageFlyRoll presentation RNG profiles. Three explicit headless
debts remain visible rather than being replay-fitted:

- the scene-owned standings cache behind `gm_8016C5C0` changes Sing's DamageSong timer rank;
- missing retail JObj/contact-matrix fidelity can choose Puff back-air BODY contact one frame before
  the replay's shield contact in one stream;
- an old Slippi 3.16 Fountain capture lacks the later platform-event publication and carries one
  0.0375-unit platform-Y offset after hitlag before returning to an exact suffix.

Future ownership recorded by this packet adds immutable Puff fighter/costume/effect graphs to
`GameData`, and Puff special state plus the distinct normalized/raw replay pad observations to
`MatchState`. The packet introduces no post-initialization allocation and no new process-global
mutable gameplay owner.

#### Subphase G — Peach extension

The Peach packet imports the complete reached `ftPeach` lifecycle, float, smash, and special-state
owners plus the Peach turnip, parasol, Toad, spore, and explosion articles. Peach's rare down-B
outcomes also promote the reached Bob-omb, Mr. Saturn, Beam Sword, and Parasol swing common-item
owners rather than substituting simplified item behavior. Native DAT initialization translates
`PlPe.dat`, costume graphs, fighter attributes, all five Peach article graphs, and the required
common-item articles/attributes into fixed arenas. Both PPC32 and native x86-64 compile the same
canonical source packet.

Two shared native source-layout defects were fixed before accepting classifications. The
`ftCommon_MotionVars::itemthrow` opaque lanes now retain their retail 32-bit widths, which removes a
wrong shield item-throw action and 12,584 downstream mismatch rows in the motivating replay.
Peach's live turnip-owner source pointer likewise remains one 32-bit word (native gameplay objects
already live below `UINT32_MAX`), keeping the following Bob-omb scale lane at retail offset `+0x14`.
Mr. Saturn validation excludes only generic Slippi bytes sampled from its uninitialized xDE4 lane
in states that do not own it; states that publish/consume the historical position remain strict.

The 20-entry `peach.json` gate reaches every final transition as two strict passes plus eighteen
exact classifications, zero failures, and zero errors over 269,123 transitions in about 3.6 seconds
on the 16-worker native runner. A bounded PPC/native probe agrees exactly at the first direct
DamageFlyRoll RNG-phase gate, and another agrees on the demo's first turnip-gravity ULP. Remaining
full-stream fingerprints use the same narrow source/profile classes as the established suite:
Dolphin/PPC float execution, presentation-owned DamageFlyRoll stream phase, legacy magnifier or
item-publication scheduling, signed zero, and source-owned historical article bytes. The composed
153-replay aggregate completes as 63 strict passes plus 90 exact classifications, zero failures,
and zero errors over 1,415,476 transitions in about 8.3 seconds.

Future ownership recorded by this packet adds immutable Peach fighter/costume/article and common
item catalogs to `GameData`; Peach float/special state, live article links, rare-pull item state, and
their fixed pools belong to `MatchState`. No post-initialization allocation or replay-keyed gameplay
branch was introduced.

#### Autonomous goal and commit policy

The source-promotion phase and each source-domain subphase above is an authorized autonomous commit
boundary. Finish its source-owner closure and completion gate, update this README plus the source,
stub, data, and upstream-delta ledgers, send a material-progress Discord notification, commit with a
focused message, and continue to the next subphase without waiting for confirmation. Do not commit a
partially linked owner, a replay-row workaround, or bare diagnostic artifacts.

Use focused native windows and bounded PPC/native differentials during bring-up. Run the growing
full native selection at each packet boundary and all current terminal inventories after doubles. Keep
routine commands under the existing five/ten-second policy; announce genuine fresh full builds.
If the optimized native validation profile is exact, use it for broad gates while retaining PPC and
native-debug as bounded correctness oracles.

The original autonomous goal completed when canonical source became the direct build input and all
97 singles plus 21 doubles reached their final transitions with every residual either strict or
source-classified. Jigglypuff and Peach were then added as post-goal source-domain extensions with
15- and 20-entry terminal manifests. Other new fighter scope, real context promotion, arbitrary
savestates, the replacement API, Wasm, the browser viewer, public cutover, and SoA/AoSoA
optimization remain outside the reached goal.

After the Peach extension, reconvene to choose between expanding the canonical source domain again
or locking the reached state closure and starting the context/API/savestate/Wasm phase.

### Phase 6 — Match ownership, arbitrary savestates, API, Wasm, and live viewer

The supported source boundary is now locked at the 153-replay aggregate. This phase changes
ownership and build structure while that complete reached corpus provides the correctness lock. It
must not become a gameplay rewrite: source file boundaries, function bodies, source structs,
callback signatures, the scalar scheduler, pointer-rich AoS representation, object ordering, and
floating-point operation order remain recognizable against the pinned decomp.

The phase ends with the same canonical gameplay implementation compiling for PPC32, native x86-64,
and wasm32; a resettable batch-first C API; location-independent on-demand match savestates; and a
Node-hosted Wasm smoke. It then cuts the existing browser live-play viewer over to the new core as a
batch of one. It does **not** replace `src/api.h`, connect the existing Python package, introduce
SoA/AoSoA, optimize snapshot size, or attempt the later maximum-throughput rewrite.

Functional multi-environment execution is in scope: several independent matches must coexist,
reset, step, copy, save, and restore inside one process and one Wasm module. Performance-oriented
batch representation is not: the first implementation may dispatch the existing scalar,
pointer-rich match representation across selected matches. Keep the API and ownership opaque so a
later dense SoA/AoSoA implementation can replace those internals without another public cutover.

#### Packet 1 — Lock behavior, state closure, and performance baselines

Freeze the chosen supported source boundary and inventory its complete mutable symbol, allocation,
pool, callback, persistent pointer, immutable data, and presentation-exclusion closure. Confirm PPC
and native compile the same canonical files directly before changing ownership. This is an internal
checkpoint, not a standalone phase result.

Context promotion is behavior-neutral. Record a direct per-replay digest of the simulator's complete
actual output stream in addition to the validation result, and require that digest to remain exact
for strict and classified replays alike. Every currently strict replay must remain strict and every
classified replay must also retain its classification id, mismatch rows, field digest, first/last
mismatch, and fingerprint. Timing is excluded. Do not add or widen a classification, tolerance, or
signed-zero exception to land this refactor. A real source-backed change that removes a
classification or strictly narrows its residual belongs in a separate correctness commit, followed
by a newly locked digest; it must not be mixed into ownership movement.

Record separate native baselines for one-time `GameData` initialization, fresh match construction,
reset, steady-state scalar step with production output, validation step with forensic output, the
full aggregate wall/CPU/RSS result, and Wasm only when that target exists. The current runtime
already aborts on HSD heap growth after match initialization; the principal lifecycle waste is
per-replay process creation, archive translation, and match reconstruction. Context promotion is
expected to remove that waste but is not expected to close the scalar 16k FPS versus optimized
300–400k+ FPS gap by itself. Investigate and resolve any repeatable steady-state scalar regression
greater than five percent before completing the phase.

#### Packet 2 — Context-promote mutable engine ownership

Inventory the imported mutable global symbols and classify each as immutable game data, mutable
match state, presentation-only excluded state, or a true immutable compile-time table. Promote only
the currently imported/reached closure; new character/stage owners will be classified when domain
expansion resumes.

One immutable `GameData` owns the data root, original archive bytes, translated DAT graphs,
character/stage/article catalogs, effect catalogs, and other initialization-only reusable data.
Each independent `MatchState` owns every mutable gameplay/runtime owner, including:

- HSD GObj lists, scheduler state, object allocators, fixed pools, and free lists;
- player slots, fighters, items/articles, stage objects, collision/mpColl state, and match rules;
- controller history, UCF state, RNG state, Slippi state, camera publication, and effect queues;
- mutable source caches and headless callback state that currently rely on process lifetime.

Keep existing source names and types where practical. Source callbacks whose signatures cannot
carry a context may resolve a scoped non-owning active match binding installed at API init/reset/step
entry. That binding owns no gameplay state; all referenced mutable bytes live in the selected match.
Use a thread-local binding where native parallel execution requires it. Do not rewrite gameplay
algorithms around a new abstraction merely to remove a global symbol. Keep this compatibility
binding internal, measurable, and removable from later hot owners; it must not enter the public API
or force per-frame pointer relocation.

Batch creation preallocates fixed-capacity storage for four players and the complete current-domain
object high-water marks. Match reset, stepping, output, copying, saving, and restoring perform no
heap allocation. `GameData` initialization performs all archive reads and DAT translation; normal
reset/step paths perform neither. Preserve deterministic object addresses/identities within a match,
allocation order, zero-initialization, and callback-list order.

Prove one `GameData` can serve at least two simultaneously alive matches, that interleaved stepping
does not leak state, and that repeated reset produces the same bytes as a fresh match. Do not treat a
process-global state snapshot/restore bridge as context promotion. The completed runtime must no
longer need one isolated process per replay merely to obtain clean source globals.

#### Packet 3 — Parallel replacement API

Add the future-facing C API inside `src/melee_core/` while leaving the existing `src/api.h` and old
runtime untouched. Model it on the useful existing API properties: opaque batch ownership,
batch-first reset/step/output, match masks, zero-copy caller buffers with explicit strides, and
initialization-only allocation. A batch of one is the scalar/browser use case; do not add a separate
public scalar API.

Use **match** for one independent simulation inside a batch. Do not use lane. Public terminology is
`match_count`, `match_index`, `match_mask`, `copy_matches`, `save_match`, and `restore_match`.
Retain `Batch`, `MatchConfig`, and `Input` as the core concepts, using the final semantic names where
coexistence permits rather than adding a compatibility layer for the unreleased parallel API.

Improve the old contract where the new ownership makes it possible:

- batch creation chooses only the match count; each match configuration declares its active players,
  and storage always supports four;
- normal step accepts current physical controller input only, while previous input and free-running
  RNG live in match state;
- UCF and runtime/capture capability choices are explicit match configuration with the supported
  default, not mutable batch-wide setters;
- normal output, terminal state, and the compact allocation-free viewer projection are production
  API owners;
- replay reseeding, authoritative per-frame replay RNG, strict compare output, and forensic hooks
  remain in a private validation API;
- typed fixed-width packed rows replace untyped byte pointers where the C type is known, while byte
  strides retain zero-copy Python/Wasm interoperability;
- invalid input/configuration returns a small result enum without hidden logging or allocation.

The initial API surface must cover create/destroy `GameData`, create/destroy batch, query match
count, reset all or masked matches, step matches, write state/terminal/viewer output, copy matches,
and save/restore a match. Public calls must not expose POSIX handles, host-sized wire fields,
internal engine pointers, or Python types.

This packet delivers real multi-environment behavior, not only future scaffolding. A batch owns the
requested number of complete `MatchState` regions and steps every selected match deterministically.
The initial loop may remain scalar and caller-controlled native threads may provide parallelism;
cross-match SIMD, scheduler fusion, and SoA/AoSoA storage remain later measured optimizations.

#### Packet 4 — Exact match save/restore

Treat savestates as a first-class ownership test, not a later serializer. The saved state contains
every mutable gameplay/engine byte needed to resume exactly at a frame boundary: match context,
source globals, scheduler and GObj topology, allocator/free-list state, object pools, fighters,
items, stage/collision, callbacks, controller history, RNG, camera/effects, and output-visible state.
Immutable `GameData` is referenced by identity rather than copied into each snapshot.

Expose an opaque caller-owned buffer with a queryable required size. Save and restore allocate
nothing, perform no gameplay reconstruction, and rebind only non-owning host/context pointers after
copying state. The snapshot header records a magic/version, exact state-layout identity, and
`GameData` fingerprint. The initial format is an in-memory same-build checkpoint, not stable
cross-version disk serialization; compression and copy-on-write are out of scope.

Savestate artifacts are location-independent and have no originating match-index affinity. A saved
state must restore into any compatible match in any batch/process using the same state schema/target
ABI and matching `GameData`, completely replacing the destination environment. Encode persistent
runtime pointers as exact match-region offsets, immutable-data references, or stable callback IDs;
use generated relocation metadata plus an explicit ledger for ambiguous union/`void*` owners. Never
conservatively scan raw words or convert gameplay structures wholesale to handles merely to make
serialization easier. Cross-match `copy_matches` uses the same ownership/relocation model without
requiring an intermediate external artifact.

Test save, advance, restore, and replay of the same input suffix; nested/repeated checkpoints;
restore after match reset; allocator/free-list reuse; and independent matches with indistinguishable
object values. Restored output and subsequent evolution must be bit-identical to the uninterrupted
reference.

#### Packet 5 — wasm32 build and Node API smoke

After canonical-source and native/PPC context parity are complete, add wasm32 as a real build target
for the same gameplay implementation. Keep platform differences behind compile-time platform files:
replace Linux `mmap`/`mprotect` ownership with a bounded Wasm linear-memory allocator and logical
initialization seal, generate DAT translation metadata for the wasm32 destination layout, and remove
GCC/x86-specific endian or fused-math assumptions with source-exact portable implementations.
Do not fork gameplay source or reload the Wasm module to simulate match reset.

Package only the current supported-domain raw archives from ignored `$MSL_DATA_DIR/raw`; no game
asset enters Git. Record uncompressed/download size and initialization time, but defer broad bundle
and runtime optimization unless the result prevents normal browser use.

Run a Node-hosted smoke against the exported batch API: initialize one `GameData`, create multiple
matches, reset and step them, save/advance/restore one match, copy between matches, and reset again
without recreating the module. Compare a bounded deterministic input stream bit-for-bit with native
output, including the source-exact float and signed-zero policy. A successful link alone is not
completion.

#### Packet 6 — Browser live-viewer cutover

Replace the old simulator behind `tools/viewer/live/sim.js` and
`tools/viewer/live/build_wasm.sh` with the new wasm32 batch API. Reuse the existing viewer UI,
keyboard/GameCube-adapter input, Slippi-viewer rendering, controls, and trace export. A single
long-lived Wasm module owns one `GameData` and a batch of one; reset must reuse that module and match
storage rather than reload Wasm or repeat DAT translation.

Expose one compact allocation-free viewer projection for the gameplay-bearing fighter, item,
stage-platform, shield, hitbox, camera, and terminal fields already consumed by
`viewer_adapter.js`. Keep forensic replay compare output private. Use one fixed-width wire schema as
the authority for native and Wasm, and generate or mechanically verify the JavaScript sizes and
offsets rather than maintaining an unrelated hand-written layout.

Package only the raw archives required by the current supported fighters/stages from ignored
`$MSL_DATA_DIR/raw`; reuse the normal extraction contract and do not copy game assets into Git. The
viewer character/stage controls must expose the complete supported aggregate scope, including Puff
and Peach. Keep simulator I/O buffers allocated once at viewer creation; normal input, step,
projection, render handoff, and reset must not call the Wasm allocator.

Complete a browser smoke covering module initialization, two-character reset, keyboard/controller
input stepping, stage and character selection, visible fighter/item/hitbox/shield output, death/reset,
and trace export. Retain the Node native/Wasm differential as the deterministic engine gate; the UI
smoke verifies integration rather than substituting rendered appearance for state equality.

#### Execution and commit boundaries

Work in the packet order above and preserve a green direct-output differential throughout. Packet 1
is baseline infrastructure for Packet 2, not a standalone checkpoint. Under an autonomous Phase 6
goal, commit coherent closures after context ownership/multi-match isolation (Packets 1–2), the
batch API plus arbitrary savestates (Packets 3–4), the wasm32/Node differential (Packet 5), and the
live-viewer cutover (Packet 6). Do not commit a half-promoted owner, a second runtime bridge, or a
classification workaround merely to create a checkpoint. Send a concise update through the ignored
Discord helper after each material closure or meaningful replay/Wasm/viewer advance.

#### Phase 6 completion criteria

1. PPC32, native x86-64, and wasm32 compile the same canonical gameplay files directly; no ignored
   materialized gameplay tree is a build input.
2. PPC/native smokes and bounded differentials pass, and every replay in the complete reached
   153-replay aggregate retains its exact actual-output digest and strict/classified result. The only
   permitted change is a separately committed source-backed reduction of an existing residual,
   followed by a newly locked digest; no new or widened classification is introduced.
3. One immutable `GameData` serves multiple resettable matches with interleaved, state-isolated
   stepping and four-player-capable storage.
4. Normal reset/step/output/copy/save/restore paths perform no heap allocation, file access, DAT
   translation, formatting, or logging.
5. The parallel batch-first C API uses match terminology, keeps replay authority private, and is
   covered for buffer shape/stride, masks, invalid inputs, reset, copy, and output semantics.
6. Save/restore captures the complete mutable engine state and reproduces bit-identical output and
   continuation; cross-match copy is exact and source-pointer-safe.
7. The Wasm module passes the Node lifetime/savestate smoke and the bounded native/Wasm output
   differential without module recreation or game assets in Git.
8. The live browser viewer runs the new core as a resettable batch of one across the complete
   supported fighter/stage scope, preserves input/render/trace behavior, and allocates no Wasm
   gameplay memory after initialization.
9. Native lifecycle and production-step benchmarks are recorded; repeated initialization is
   removed and no repeatable steady-state scalar regression greater than five percent remains.
10. `src/api.h` and the existing Python runtime remain untouched; the plan and upstream/source delta
    ledgers describe retained structure and any new target-specific deltas.

#### Phase 6 result — 2026-07-16

The canonical source now has explicit lifetime ownership without changing its gameplay algorithms
or pointer-rich scalar representation. `MslCoreGameData` owns immutable extracted archives,
translated DAT graphs, and initialization catalogs. Each `MslCoreMatch` owns the complete reached
mutable source closure, HSD scheduler/allocator topology, fixed pools, controllers/UCF, RNG,
fighters, articles, stage/collision, camera, and headless effect state. A scoped thread-local binding
keeps source callbacks recognizable while selecting one match; it owns no gameplay bytes. Native
validation workers use the same boundary to initialize `GameData` once and reset match storage for
successive replay jobs rather than restarting and retranslating DAT data per game. PPC validation
remains the isolated QEMU oracle path.

The new `src/melee_core/api.h` is batch-first and uses match terminology throughout. It supports
masked reset/step/output, terminal and compact viewer projections, cross-match copy, and save or
restore into any compatible match index. Save artifacts contain the `MslCoreMatch` image plus only
the used match arena; immutable `GameData` is fingerprinted but not copied. Generated typed
relocation metadata rebases match-arena, `GameData`, translated-DAT, scheduler, pool, and callback
pointers without raw-word scanning. Native callbacks, HSD descriptors, and compile-time table
pointers are additionally rebased by their ELF image load bias, so a same-build artifact does not
depend on its originating process's ASLR address. `runtime/relocation_ambiguities.tsv` records every
reached union/`void*` family and its typed relocation policy. The artifact header hashes its header
and payload, rejects truncation/corruption or incompatible GameData before destination mutation,
and has no originating-batch or originating-index affinity. Snapshot size optimization and stable
cross-version serialization remain intentionally deferred.

The ownership smoke byte-locks the complete `GameData` object, its used source-data arena, and its
used translated-DAT arena across isolated construction, interleaved stepping, and reset. The
safety lock exposed the remaining mutable source `gFtDataList` registry: archive/catalog roots stay
in immutable `GameData`, while each match now owns its loaded-fighter registry and reference state.
The generated native, PPC, and wasm32 relocation layouts include that per-match pointer array; the
Wasm layout generator's dependency file is part of the normal build graph, so a context layout
change cannot leave a stale snapshot pointer map. The savestate smoke checkpoints a live Peach item
graph, destroys the article, reuses the source fixed pools for another pull, restores into an
unrelated match, and requires identical state and viewer
continuation. Reached retail `OSReport` calls are diagnostic-only and are silent headlessly; fatal
panic paths remain loud, while normal reset/step performs no formatting or logging.

The wasm32 target uses the same canonical gameplay files, generated command-field and relocation
layouts, bounded linear-memory platform owners, and initialization-only DAT translation. The Node
lifetime/differential smoke creates multiple matches, resets, copies, saves/restores, and compares
native and Wasm state exactly without recreating the module. Its locked state digest is
`cef96ff32edfe898`; current initialization is about 740 ms and the tested snapshot is 15,506,200
bytes. The packaged supported raw profile is 49,862,928 bytes, with a 1,240,243-byte Wasm module
and 69,556-byte loader. These are uncompressed development artifacts, not a bundle-size target.
PPC-DWARF-generated accessors cover both fighter/item `CmdUnion` streams and color-overlay command
streams because Clang/wasm32 does not implement GCC's big-endian scalar-storage-order pragma. A
deterministic Peach/Falco mixed-input soak locks the color-animation path that exposed this seam.

The production live viewer now owns one long-lived Wasm module, one `GameData`, and a batch of one.
Its fixed 1,560-byte generated projection covers fighters, hitboxes, shields, items, moving stage
actors, camera, and terminal state without exposing forensic replay rows. Reset reuses every Wasm
buffer. Node coverage spans all eight fighters and six legal stages plus visible hitbox, shield,
Peach item, death/reset, trace, and no-memory-growth paths. A real headless-Chrome smoke loads the
assembled Slippi renderer, switches to Puff on Yoshi's Story, steps projected frames, observes the
rendered SVG, drives a real keyboard sample through the input recorder, and verifies a trace
artifact through the UI control. Puff and Peach renderer archives join the other
fighters in the checksum-pinned asset manifest; archives resolve from the ignored SlippiLab
checkout/cache and no game or model asset is tracked by Git.

Lifecycle medians on the native scalar target are: 248.514 ms `GameData` initialization, 0.040 ms
batch allocation, 3.627 ms first match construction, 0.718 ms reset, 28,574 FPS step with compare
state, 28,801 FPS step plus production state, and 28,597 FPS step plus viewer projection. Save and
restore of a 17,303,848-byte exercised match image take about 14.0 ms each. The
full 153-replay native gate completes in 8.652 seconds at 163,594 aggregate FPS and 123.352 runner
CPU-seconds. The pre-refactor locked run was 8.315 seconds / 170,236 aggregate FPS and 119.839 CPU
seconds, so current wall time is 4.1% higher and remains inside the five-percent phase limit;
the large temporary regression from paying initialization once per replay was removed by persistent
workers.

All 153 full-output fingerprints remain locked. The one validation-classification snapshot change
is a strict reduction from 7,324 to 7,022 compared fields for
`HumiliatingCreamyReindeer.slpz`: when its already-classified item-count divergence compares an
extra thrown needle against an empty replay slot, the needle source does not own the stale
`xDD4/xDD8` fixed-pool bytes. The replay's rows, first/last mismatch, and gameplay output lock are
unchanged; no classification was added or widened. This source-backed validation-only reduction is
kept as an isolated review hunk rather than being used to justify any runtime behavior change.

Final gates:

```bash
make -f src/melee_core/Makefile source-check smoke native-smoke -j16
make -f src/melee_core/Makefile viewer-production-smoke -j16
make -f src/melee_core/Makefile validation-supported-domain \
  VALIDATION_BACKEND=native VALIDATION_ARGS=--no-build
make -f src/melee_core/Makefile lifecycle-benchmark
```

After this phase, connect the replacement to the public C/Python API, delete the displaced old core,
and begin the dedicated maximum-throughput program. Profile equivalent production workloads first,
then remove source-clear presentation work, densify fixed pools, promote hot state to SoA/AoSoA,
step scheduler phases across matches, vectorize, and add native parallel execution. The Phase 6
opaque batch API and correctness digests are the stable boundary for that work; the scalar
source-shaped representation is not presumed to be the final performance ceiling.

Correctness hardening remains continuous: reached nonmatching decomp owners, PPC/native/Wasm float
seams, RNG streams, endian boundaries, unsupported stubs, and long-rollout divergence must be
corrected or explicitly source-classified when their owner enters scope.

### Maximum-throughput program

The next three phases turn the validated source-shaped core into the production RL runtime. The
first major throughput target is at least 500,000 complete environment-frames per second on one
pinned physical core of the local AMD Ryzen 9 9950X3D at a batch size of 256 or 512. A complete
frame includes controller ingestion, all gameplay systems, terminal calculation, and the normal RL
observation write; validation compare rows, viewer projection, skipped systems, or inflated frame
accounting do not count toward the target.

The execution model remains deliberately simple: one single-threaded simulator process owns one
logical batch, and users run independently pinned processes when they want multiple cores. Large
batches may be tiled internally for cache locality, but the core will not create a worker pool,
manage processes, or schedule work across CPU cores in these phases. One process must eventually be
able to own 4,096 to 16,384 environments without pathological virtual, resident, or observation
memory use.

The local target has two L3 domains: physical CPUs 0–7 share 96 MiB and physical CPUs 8–15 share
32 MiB; CPUs 16–31 are their SMT siblings. Phase 7 measures a pinned physical core from each domain
before selecting and locking the official 500k benchmark core. The secondary AMD EPYC 9655P host
(`gigaserver`) has 96 physical cores, 384 MiB L3 across twelve domains, and the same useful broad
AVX-512 families as the local target. It is a compatibility and many-process scaling target, not a
500k single-core completion target. Produce host-tuned native builds rather than weakening the
local target for a lowest-common-denominator binary.

Optimization must preserve the character-admission path. Shared runtime systems may not encode the
currently supported character set as a performance shortcut. Batch dispatch and specialization use
source/data-backed callback or owner identities so a future character primarily adds extracted
data and genuinely new character callbacks rather than requiring a new state layout, scheduler, or
observation design. All retained speedups perform equivalent gameplay computation and preserve the
153-replay output locks without new or widened classifications.

#### Phase 7 — Production contract, benchmark, and optimized baseline

Phase 7 establishes the only workload and evidence on which later architectural optimizations are
judged. It intentionally stops before broad mutable-state or scheduler restructuring so Phase 8 is
chosen from trustworthy measurements.

The accepted baseline, compiler experiments, runtime census, and the current 32-bit resident-batch
blocker are recorded in [`PERFORMANCE.md`](PERFORMANCE.md). Keep future retained and rejected
optimization results in that worklog rather than overwriting the baseline.

1. Add a typed production observation projection semantically equivalent to the old simulator's
   980-byte `MeleeGamestate`, which was selected for Slippi-AI compatibility. It must include
   viewpoint-relative self/ally/opponent ordering, presence/source/team metadata, the existing
   fighter kinematics/action/combat fields, items, Randall, Fountain platform heights, and derived
   invulnerability. Do not expose the 1,560-byte viewer schema or the replay-forensic compare row as
   the RL contract. Restore the detailed terminal result rather than the current one-byte `done`
   projection. Lock field-by-field parity with the old production projection for the supported
   domain while allowing a cleaner new API/layout.
2. Implement one native replay-driven production benchmark. Decode and stage the aggregate-suite
   match configs and controller streams before timing, assign replay streams cyclically across
   every match, and repeat them as needed to fill the batch. Each environment resets and restarts
   when its input stream ends. The timed loop uses ordinary free-running gameplay semantics: it
   must not invoke validation comparison, per-frame replay RNG authority, teacher forcing, Python
   row loops, or Wasm/viewer output.
3. Make the benchmark write the production observation and terminal output into a caller-owned
   128-frame ring. The core owns only the current match state and writes one ring position per
   frame; it does not retain or copy observation history internally. Include reset cost in the main
   throughput number and report a step-only diagnostic separately. Lock deterministic state and
   observation digests so a candidate cannot claim speed by omitting equivalent computation.
4. Run the primary benchmark at batches 256 and 512 on one pinned physical core with SMT excluded.
   Measure both the 96 MiB and 32 MiB local L3 domains once, choose the faster repeatable target,
   and record affinity, compiler, CPU, clocks/governor context, wall time, aggregate FPS,
   cycles/environment-frame, reset frequency, and observation bytes written. The benchmark must be
   fast enough for iterative use and have bounded warmup/sample durations.
5. Add an optimized strict-float native configuration. Evaluate `-O3`, host architecture tuning,
   LTO, section garbage collection, and similarly behavior-neutral compiler/linker choices without
   enabling fast-math, unsafe contraction, or a different gameplay workload. Keep debug/forensic
   builds available but make the production benchmark use the release configuration explicitly.
6. Produce a source-backed reachability and cost census: per-phase CPU samples/cycles, reached
   scheduled callbacks, headless/presentation work still running, compiled/link-retained source,
   allocation and fixed-pool high-water marks, `MslCoreMatch`/arena reserved and touched bytes,
   savestate component sizes, and immutable data duplicated per match. Distinguish code that merely
   compiles from work that executes every frame.
7. Keep a performance worklog with the baseline and every retained/rejected candidate. A speedup is
   accepted only with the same benchmark workload/digest and full correctness gates. Do not retain
   speculative flags, alternate implementations, or rejected experiments.

Phase 7 is complete when the production observation/terminal contract is covered and Slippi-AI
compatible, the replay benchmark is reproducible on both local cache domains, the optimized build
has an equivalent-computation baseline, and the profiling/memory census is sufficient to propose
concrete Phase 8 storage and scheduler boundaries. The full 153-replay native gate, output locks,
native/PPC smokes, native/Wasm differential, no-allocation runtime contract, and browser viewer must
remain green with no new classification. Phase 7 does not need to reach 500k FPS or make 16k
environments practical.

#### Phase 8 — Memory compaction and batched execution substrate

Phase 8 makes the runtime structurally capable of large RL batches before introducing widespread
SIMD-specific layout. Use the Phase 7 census rather than replay names or guessed hot fields to set
the exact memory budgets and owner boundaries.

1. Remove source-backed headless-irrelevant files, callbacks, scheduler entries, presentation work,
   diagnostic projection, and initialization machinery that remains reached in production without
   contributing gameplay state. Let linker garbage collection handle merely unused functions;
   prioritize work that consumes runtime cycles, memory, or build ownership.
2. Move every proven immutable archive, translated DAT graph, table, callback descriptor, and stage
   or character catalog into shared `GameData`. Replace mutable per-match copies only after proving
   the source owner and save/restore behavior.
3. Replace the 32 MiB per-match arena and oversized fixed pools with bounded, measured storage.
   Separate compact hot per-frame state from cold mutable scheduler/object/pool state, retain no
   heap allocation after initialization, and update typed relocation/savestate metadata rather than
   weakening arbitrary-index restore.
4. Remove the 4,096-match API ceiling once the representation supports it. Add bounded memory and
   lifecycle coverage for 4,096 and 16,384 initialized environments with a 128-frame caller-owned
   observation ring; report virtual, resident, touched, hot-state, cold-state, and observation
   bytes separately.
5. Tile one logical batch into cache-sized groups while remaining single-threaded. Step scheduler
   phases across matches and group divergent lanes by generated source callback/owner identity;
   preserve deterministic lane order and tie-breaking. Do not add an internal worker pool.
6. Keep shared physics, collision, combat, stage, item, and observation paths character-generic.
   Any specialization must be keyed by real extracted/source owner data rather than a current
   character-id list, and future characters must retain a correct canonical execution path.

Phase 8 is complete when a single process can create, reset, step, observe, save/restore, and destroy
4k and 16k batches within the measured memory budget; production paths still allocate nothing;
cache-tiled owner scheduling is the canonical runtime path; and all Phase 7 benchmark/correctness
locks remain equivalent. Record the new 256/512 single-core throughput and memory scaling, but do
not call Phase 8 complete merely for an FPS gain if large-batch memory remains impractical.

Phase 8 evidence is accumulated in [`PERFORMANCE.md`](PERFORMANCE.md). The retained implementation
now has a 3 MiB bounded Match arena, compact generated relocation state, one contiguous arena map
per batch, same-config reset cloning, true resident 256/512 measurements, a complete 4,096-Match
lifecycle/memory gate, and canonical two-Match source-owner scheduling. The supervised 16,384-Match
lifecycle also passes at 33.57 GiB peak RSS including a physically touched 128-frame output ring;
retain it as an explicit high-memory release check and do not run it unattended or mistake its
process for a routine benchmark.

#### Phase 9 — SoA/AoSoA, SIMD, and 500k completion

Phase 9 optimizes the measured hot path after the production contract and state ownership are
stable. The public batch, observation, terminal, and arbitrary-match savestate semantics remain
unchanged while internal physical storage may be reorganized aggressively.

1. Promote measured hot fields to aligned SoA/AoSoA storage in cache-sized tiles. Keep cold or
   rarely active source structures out of unconditional tick streams and avoid broad gathers from
   pointer-rich graphs.
2. Make hot loops compiler-vectorizable first, using 64-byte alignment, clear aliasing/restrict
   contracts, compact masks, deterministic lane order, and contiguous owner groups. Inspect emitted
   code and counters rather than treating a vectorization report as proof of useful SIMD.
3. Add explicit AVX-512 kernels where they win on the 9950X3D: likely input normalization, timers,
   unconditional integration, collision candidate math, masks/bitsets, observation packing, and
   other shared systems with coherent lanes. Compare AVX2 and scalar forms when divergence,
   gather/scatter cost, or frequency behavior makes wider vectors questionable. Keep target-specific
   instructions behind internal kernels, not character or public-API branches.
4. Evaluate measured late-stage techniques such as software prefetch, active-lane compaction,
   branchless bitsets, huge pages, first-touch placement, PGO, and post-link optimization. Retain
   only equivalent-computation wins and keep host-specific tuning reproducible.
5. Verify that a large logical batch still tiles correctly and efficiently in one process, then run
   independent pinned processes for an out-of-band many-core smoke on the local CPU and EPYC. Do
   not make internal multi-core orchestration a completion dependency.

Phase 9 completes at a repeatable minimum of 500,000 complete environment-frames per second on the
selected pinned physical 9950X3D core at batch 256 or 512, including resets and the 128-frame
production observation/terminal ring writes. The 4k/16k per-process memory contract, future-character
admission boundary, full validation/output locks, native/Wasm parity, save/restore exactness, and
no-allocation runtime contract must remain intact. Report the EPYC result as portability/scaling
evidence, not as a substitute for the local single-core target.

##### Phase 9 execution and commit policy

Begin with one bounded pre-SoA packet: profile the true resident 256/512 production workload by
source phase/owner, close the highest-cost optimizer-sensitive type/ownership boundaries, and
expand the strict `-O3` allowlist only where the complete output locks prove equivalence. Establish
the best safe compiler/dispatch baseline before promoting broad mutable state. The following packet
uses that evidence for the hot/cold and SoA/AoSoA cut; explicit AVX2/AVX-512 kernels and late-stage
tuning follow only after the scalar tiled layout is demonstrably vectorizable.

Start each candidate from a clean retained commit and keep experiments uncommitted while measuring
them. Rejected candidates are removed completely and recorded in `PERFORMANCE.md`; do not retain
alternate paths, speculative flags, or dead scaffolding. Commit a candidate only when it is a
coherent measurable improvement with the same workload/digest and its proportionate correctness
gates pass. Keep implementation, focused tests, worklog numbers, and relevant source/delta ledger
updates in that commit. Combine behavior-neutral substrate with its first real consumer rather than
committing unused machinery. Completed packet boundaries must leave a clean worktree; if work stops
inside a packet, leave the scoped work uncommitted and report it explicitly.

Use the ignored `tools/melee_core/notify_discord.sh` helper after each concrete retained performance
win, reporting the before/after complete FPS, batch size/core, digest-equivalence status, and gates
completed. Do not send progress notifications for merely attempted or rejected candidates. Send a
separate update at a material architectural closure such as the hot/cold cut, canonical SoA/AoSoA
execution, first retained SIMD kernel, or final 500k lock.

The bounded pre-SoA packet is complete. The strict native release allowlist now includes the full
hosted matrix, FObj, JObj, and lbVector owners; native-DAT, GObj scheduler, context-TU, and direct
context-inline experiments were measured and removed. All 153 replay locks and Phase 8 runtime/API
contracts remain intact. The final ordinary resident result is 27,831 complete FPS at 256 and
38,086 at 512 on CPU 0, whose 96 MiB V-cache is now materially faster than the previously selected
frequency-domain core for true resident state. [`PERFORMANCE.md`](PERFORMANCE.md) contains every
retained/rejected result and the preservation audit. The next packet begins the measured hot/cold
and SoA/AoSoA representation cut; it must not reopen the compiler sweep or introduce explicit SIMD
before the scalar tiled layout is proven.

## Command runtime policy

Routine development commands should normally complete in under five seconds and must be scoped to
finish within ten seconds. Prefer one translation unit, one subsystem, a short frame window, or one
replay over broad rebuilds and suites. Set timeout ceilings close to the expected runtime; a large
timeout is not a substitute for narrowing the command.

A fresh simulator compile is the expected exception and should be announced before it starts. Full
replay gates run only at material parity checkpoints; during bring-up, use bounded native/PPC
differentials and retain C/Arrow frame processing rather than Python loops. By Phase 4 completion,
the native five-replay gate itself should be fast enough for routine local use. Keep CPU and memory
bounded, and improve a repeatedly needed slow tool instead of normalizing long-running commands.

## Local setup

- `refs/melee/` is an optional independent clean clone used only by explicit source import/update
  commands; normal build and test targets do not depend on it.
- `SSBM.iso` is an ignored local input. `python -m melee_sim.extract_data --iso ...` creates the
  canonical ignored `$MSL_DATA_DIR/raw` archive profile; `refs/melee-disc` is optional forensic
  access to a full disc filesystem and is not used by normal builds or validation.
- `refs/slippi-ssbm-asm` and `refs/ucf` are symlinks to existing reference checkouts.
- The Melee core replay validator reads replay-visible columns directly and resolves original game
  DATs from the same `MSL_DATA_DIR` root used by the existing simulator.
- Phase 1's repository-local PPC32 cross-toolchain and sysroot live under ignored
  `build/melee_core/`; the documented setup target downloads Debian cross packages without
  installing or modifying host packages.

Copied or adapted runtime source belongs in this branch as tracked files. Do not leave substantive
port work only inside the ignored `refs/melee/` checkout.

## Worklog

Record retained and rejected approaches with the source pin, compiler command, correctness
evidence, and stub/data implications. Add allocation and performance evidence only when the work
reaches the production-architecture phase. Do not treat successful linking as gameplay completion.
