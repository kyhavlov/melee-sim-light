# Source-Shaped Melee Core

Status: Phase 4 native x86-64 scalar parity is complete in the working tree. All five tracked
Fox/Fox FD replays retain the Phase 2.5 gameplay-exact result; the one unrecorded Nintendont
render-publication difference remains an explicit diagnostic. Native is about 19.6x faster than
PPC/QEMU on the equivalent starter runner workload. No production-core cutover has occurred.

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
objects. `vendor/` is a pristine minimal snapshot preserving upstream paths; `patches/` contains
the ordered adaptations applied only to the ignored materialized build tree. Core-owned headless
behavior lives under `runtime/`, hosted OS/ABI replacements live under `platform/`, and audited
exclusions remain under `stubs/`. Neither the pristine snapshot nor host/platform policy is mixed
into the other boundary.

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

Track the imported set in the checked-in lock, exact file list, and manifest. Files under
`vendor/` never receive local edits. Small necessary hosted/source-exact changes belong in the
ordered patch series; true platform policy remains in `platform/` rather than being scattered
through gameplay source.

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

#### Phase 3 result

The durable core now has explicit ownership boundaries:

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

`tools/melee_core/source_sync.sh check` verifies the exact file list, count, content digest, and
complete patch series. `import CHECKOUT` reconstructs the current pin, `update CHECKOUT COMMIT`
tests the patches against a deliberate new pin before replacing the snapshot, and `materialize`
creates the ignored build input. Normal build and test targets no longer read `refs/melee`; a clean
test run also succeeds with that checkout temporarily absent. The checkout is needed only when a
developer explicitly imports or updates the pin.

All five replay gates retain the Phase 2.5 counts: 2,723, 10,174, 12,185, 8,729, and 7,543
transitions pass gameplay-strict comparison. Hungry retains exactly one separately reported
render-visibility diagnostic at frame 6030; signed zero remains strict and has no diagnostics.

The patch series is the durable provenance and upstream-update mechanism during PPC/native scalar
parity work. It is not intended to encode later whole-core architecture changes as an ever-growing
diff against pristine decomp files. Before Phase 6 introduces cross-cutting per-match state,
initialization-only allocation, SoA/AoSoA layout, or batching, the then-validated patched result
will be promoted to repository-owned canonical gameplay source. PPC and native oracle targets must
continue to compile that same canonical implementation so layout optimization cannot create a
second semantic port. Small future upstream changes can still be reviewed and applied deliberately
through the pinned snapshot and patch provenance.

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

Continue using the ignored Discord helper for material Phase 4 progress:

```bash
tools/melee_core/notify_discord.sh \
  "Phase 4: MILESTONE, with current native/PPC parity and next blocker."
```

Send an update when the native source closure first compiles or links, native DAT translation first
boots the match, a replay advances materially or becomes exact, a shared native/PPC owner is
closed, a blocker changes the architecture or schedule, the five-replay gate passes, or the first
equivalent throughput result is known. Do not send routine compile attempts, small frame advances,
or repeated status noise. The helper reads `MSL_CORE_DISCORD_WEBHOOK_URL` or the ignored
`build/melee_core/discord_webhook_url`; never put the credential in tracked files or command output.

### Phase 5 — RL 1.0 supported domain

Add Falco, Marth, Sheik, Zelda, Captain Falcon, the remaining five legal stages, relevant
items/articles and stage objects, character/stage-specific Slippi patch behavior, singles match
rules, and four-player/doubles scheduling. Compile each new source-owner vertical for both PPC and
native targets, and expand by shared source owner rather than replay row.

### Phase 6 — Production performance and API cutover

Establish explicit per-world state, enforce initialization-only allocation, connect the native
runtime to the batch-first C/Python API, and profile equivalent workloads before changing layout.
Apply SoA/AoSoA, batching, and other throughput work only where measured, preserving PPC/native
correctness evidence after each change. Delete the displaced old core at complete supported-domain
and API ownership rather than retaining a fallback runtime.

Correctness hardening remains continuous through Phases 4--6: reached nonmatching decomp owners,
PPC/native float seams, RNG streams, endian boundaries, unsupported stubs, and long-rollout
divergence must be corrected or explicitly source-classified when their owner enters scope.

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
- `refs/melee-disc` and `SSBM.iso` are symlinks to existing local assets.
- `refs/slippi-ssbm-asm` and `refs/ucf` are symlinks to existing reference checkouts.
- The Melee core replay validator is independent of the existing simulator's generated `data/`
  package. It reads replay-visible columns directly and the runtime reads original game DATs.
- Phase 1's repository-local PPC32 cross-toolchain and sysroot live under ignored
  `build/melee_core/`; the documented setup target downloads Debian cross packages without
  installing or modifying host packages.

Copied or adapted runtime source belongs in this branch as tracked files. Do not leave substantive
port work only inside the ignored `refs/melee/` checkout.

## Worklog

Record retained and rejected approaches with the source pin, compiler command, correctness
evidence, and stub/data implications. Add allocation and performance evidence only when the work
reaches the production-architecture phase. Do not treat successful linking as gameplay completion.
