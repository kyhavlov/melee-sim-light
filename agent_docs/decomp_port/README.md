# Decomp Port Exploration

Status: Phase 2 scalar Fox/Fox-on-FD vertical complete. The starter Slippi replay validates
bit-exactly for all 2,723 transitions. No production-core cutover is implied by work on this
branch.

Branch base: `core-rewrite` at `6fbcc9bc7719` (`Rewrite core contact and motion state ownership`).

Decomp source pin: doldecomp/melee `91b9789fa6539ea847998a6ed330fa748fa200ec`, the
latest commit on upstream `master` when Phase 2 source import began. The independent ignored
checkout under `refs/melee/` remains clean at this pin for the milestone.

## Goal

Build a faithful, headless scalar Melee runtime from close copies of the original gameplay source.
The first useful outcome is a correctness MVP and eventual oracle. Only after scalar correctness is
established will the project decide whether to undertake the separate work required for a 64-bit,
reentrant, allocation-free, batched production core.

## Agreed architecture

### Parallel source tree

The port lives under `src/decomp_port/` and does not link existing simulator gameplay objects.
Copied sources preserve the upstream layout beneath that root, including `melee/`,
`sysdolphin/`, and the required Dolphin SDK surface. Host-owned code belongs under
`src/decomp_port/host/`; audited presentation/platform stubs belong under
`src/decomp_port/stubs/`. Phase 2 continues to use that parallel tree and does not link gameplay
objects from the existing simulator.

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
packed validation wire layouts through a standalone file interface. A later production phase owns
64-bit conversion, per-world state, allocation removal, SoA/AoSoA storage, batching, Python
integration, and throughput.

The build uses a repository-local cross-toolchain/sysroot and QEMU runner so it does not mutate the
host toolchain. A native 32-bit x86 build was proven feasible, but rejected for Phase 1 because it
would still require an endian-aware parallel DAT/object representation and would not improve PPC
floating-point fidelity. PPC Linux is not identical to the GameCube ABI or hardware, so reached
ABI and float differences remain explicit correctness work rather than assumed solved.

### Source import policy

Import complete translation units and source-owner families, not selected functions. Begin with the
dependency closure for two Fox fighters on Final Destination and expand when a real source call
graph requires it. Do not import every supported fighter and stage merely to silence link errors.

Track the imported set in a checked-in manifest containing the decomp pin, upstream path, local
path, match status, and disposition. Copied files may receive small necessary host-port edits, but
platform policy should remain concentrated in `host/` rather than scattered through gameplay.

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

- one `MslMatchConfig` row configures the match;
- one optional `MslInput` row establishes the previous controller sample for the first frame;
- `N` contiguous `MslInput` rows drive `N` free-running frames;
- `N` contiguous `MslCompare` rows record the post-frame result.

The Phase 1 command accepts separate config, initial-previous-input, input-tape, and compare-output
paths. A missing initial previous row means neutral input. `MslInput` retains all four player slots,
the existing `MSL_BUTTON_*` mask, raw signed stick lanes, and raw trigger lanes. These are physical
controller samples; later UCF processing belongs inside the new runtime.

`MslCompare` is the primary output contract. Phase 1 populates every reached source field and uses
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
- the `MslMatchConfig`/`MslInput`/`MslCompare` standalone file interface described above;
- input injection and compare export for idle, walk, turn, dash, run, jump, fall, and landing paths;
- a stub/exclusion ledger whose unexpected entries abort at runtime;
- repeatable multi-frame `MslCompare` output for a fixed `MslInput` tape;
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

- `build/decomp_port/phase1/melee-decomp-port` is an ELF32, big-endian PowerPC Linux executable;
- QEMU user mode runs it locally against ordinary files under `refs/melee-disc/files`;
- raw `PlCo.dat`, `PlFx.dat`, `PlFxNr.dat`, and `GrNLa.dat` pass through the copied source archive
  relocation and lookup owners;
- two Fox source `Fighter` objects start at the source-backed FD neutral positions for slots 0/1;
- one copied HSD GObj scheduler tick owns each simulated frame;
- the fixed 200-frame tape reaches Wait, walk, turn, dash, run, knee bend, jump, fall, landing, and
  Ottotto/OttottoWait without reaching a loud unresolved stub;
- two fresh processes emit byte-identical `MslCompare` output for that tape.

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
`tools/decomp_port/setup_ppc32_toolchain.sh`, which uses the existing `apt-get` and `dpkg-deb`
commands to download Debian cross-compiler, PPC sysroot, and QEMU packages and unpack them only
under ignored `build/decomp_port/`. It does not run `apt install`, use `sudo`, or alter the host.

```bash
make -f src/decomp_port/Makefile -j2 mvp
make -f src/decomp_port/Makefile -j2 test
```

The isolated Makefile deliberately does not include the main repository Makefile. PPC compilation
does not start Python; the separate `validation-native` target asks the selected interpreter for
its C include directory once. `test` runs five bounded native-source smokes followed by the full
2,723-transition starter replay through the native streaming validator. Python-side math-library
thread counts remain pinned to one. The focused commands are:

```bash
make -f src/decomp_port/Makefile -j2 smoke
make -f src/decomp_port/Makefile -j2 validation-smoke
```

The ordinary repository-wide pytest entry point still requires the existing simulator's generated
`data/manifest.json`. This worktree does not have that generated package, so the isolated
decomp-port Makefile remains the authoritative command for this vertical.

### Standalone interface

```text
melee-decomp-port GAME_DATA CONFIG PREV_INPUT_OR_- INPUT_TAPE OUTPUT
```

The fixed wire sizes are 36-byte `MslMatchConfig`, 32-byte `MslInput`, and 1022-byte
`MslCompare`. Multibyte fields are explicitly decoded and encoded little-endian, so the files are
directly compatible with the current NumPy dtypes even though the process is big-endian. The thin
adapter is `melee_sim/decomp_port.py`; it writes one tape, invokes QEMU once, and reads the compare
array without a per-frame Python loop or process.

The PPC executable also has an internal pipe mode:

```text
melee-decomp-port GAME_DATA --stream
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
temporary input/output tape, or replay-sized `MslCompare` allocation. The native path uses fixed
64 KiB pipe buffers, one compare row, and an O(number of raw frames) `int64` rollback index. It
compares all exported fighter and item/article fields bit-for-bit. Pointer/allocation identity that
Slippi does not own is deliberately not part of the wire contract. It does not write or refresh
validation reports. The implementation is `bindings/msl_decomp_validate.c`; the Python file below
only builds/loads it, asks Peppi for the replay, and formats the small returned summary:

```bash
.venv/bin/python -m tools.decomp_port.validate_replay \
  replays/validation/aggregate_recent/Game_20260514T181413.slpz
```

`--frames N` bounds an exploratory run. `--start-frame F` still advances the PPC runtime through
every preceding replay input, but begins comparison at frame `F`; this lets later source-owner gaps
remain visible behind a known match-opening mismatch. Multiple replay paths can be supplied to one
command, which builds and imports the validator once; `--no-build` skips even the no-op Make check
for repeated runs. The raw game DAT directory remains `refs/melee-disc/files`.

On the development host, the full 2,723-transition starter replay takes roughly 3--4 seconds and
about 69 MiB maximum RSS through the direct path. The discarded validation-buffer implementation
took about 304 MiB for the same replay. At this size QEMU executing the unoptimized PPC source is
the dominant cost; a future native scalar build, not more Python preprocessing, is the next major
throughput lever for million-frame suites.

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
4. Repeated runs are bitwise deterministic, the isolated decomp-port test suite passes, and the
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
- all remaining stubs and exclusions are classified in `src/decomp_port/stubs/ledger.tsv`;
  presentation/audio/platform paths no-op explicitly, and unsupported fighter, held-item, exotic
  capture, renderer, and other-stage paths remain loud or source-guarded outside this boundary;
- the isolated test target runs five source/substrate smokes plus the complete acceptance replay,
  and repeated fresh-process validation is byte-for-byte deterministic.

The binary remains the intentionally scalar PPC32/QEMU oracle. The wire does not export frontend
results-screen state or timer adjudication, and the declared boundary remains two human Foxes on FD
with ordinary items disabled. Those are later domain/frontend boundaries, not bridges to the old
simulator or replay-keyed exceptions.

### Phase 3 — RL 1.0 supported scalar domain

Add Falco, Marth, Sheik, Zelda, Captain Falcon, the remaining five legal stages, relevant
items/articles and stage objects, character/stage-specific Slippi patch behavior, singles match
rules, and four-player/doubles scheduling. Expand by shared source owner rather than by replay row.

### Phase 4 — Scalar correctness hardening

Investigate reached nonmatching decomp owners, PPC float-sensitive paths, RNG streams, endian
boundaries, unsupported stubs, and long-rollout divergence. Establish deterministic supported-domain
validation and document explicit source-backed exclusions.

### Phase 5 — Production architecture decision

Decide whether the scalar kernel remains an oracle or becomes the production core. A production
port then owns 64-bit data/layout conversion, per-world state, allocation-free stepping,
SoA/AoSoA storage, batching, CPython integration, validation-runner integration, and throughput.
None of those optimizations may replace or narrow source behavior established by the scalar model.

## Local setup

- `refs/melee/` is an independent clean clone at the source pin above.
- `refs/melee-disc` and `SSBM.iso` are symlinks to existing local assets.
- `refs/slippi-ssbm-asm` and `refs/ucf` are symlinks to existing reference checkouts.
- The decomp-port replay validator is independent of the existing simulator's generated `data/`
  package. It reads replay-visible columns directly and the runtime reads original game DATs.
- Phase 1's repository-local PPC32 cross-toolchain and sysroot live under ignored
  `build/decomp_port/`; the documented setup target downloads Debian cross packages without
  installing or modifying host packages.

Copied or adapted runtime source belongs in this branch as tracked files. Do not leave substantive
port work only inside the ignored `refs/melee/` checkout.

## Worklog

Record retained and rejected approaches with the source pin, compiler command, correctness
evidence, and stub/data implications. Add allocation and performance evidence only when the work
reaches the production-architecture phase. Do not treat successful linking as gameplay completion.
