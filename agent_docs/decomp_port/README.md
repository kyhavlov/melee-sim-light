# Decomp Port Exploration

Status: Phase 1 exploratory MVP working. No production-core cutover is implied by work on this
branch.

Branch base: `core-rewrite` at `6fbcc9bc7719` (`Rewrite core contact and motion state ownership`).

Decomp source pin: doldecomp/melee `507715a982c95992c271483c7e5be05a06689dff`, the
latest commit on upstream `master` when this exploration began. The independent ignored checkout
under `refs/melee/` must remain at this pin throughout the first vertical.

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
`src/decomp_port/stubs/`.

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

Phase 1 includes:

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

Phase 1 does not include fighter-vs-fighter hit resolution, shield/grab/throw, stocks and respawn,
Fox articles and specials, UCF, other fighters or stages, Python integration, batching, allocation
locks, or performance gates.

Phase 1 is complete only when the executable builds from a clean tree with one documented command,
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

The isolated Makefile deliberately does not include the main repository Makefile and therefore
does not run Python while parsing or compiling C. `test` runs six bounded native-source smokes and
one bounded Python adapter smoke; the latter starts two QEMU processes for deterministic comparison
and pins NumPy math-library thread counts to one. The focused commands are:

```bash
make -f src/decomp_port/Makefile -j2 smoke
make -f src/decomp_port/Makefile -j2 locomotion-smoke
```

The ordinary repository-wide pytest entry point still requires the existing simulator's generated
`data/manifest.json`. This worktree does not have that generated package, so the isolated tests are
the authoritative Phase 1 command; `tests/test_decomp_port_phase1.py` remains available when the
rest of the checkout has been bootstrapped.

### Standalone interface

```text
melee-decomp-port GAME_DATA CONFIG PREV_INPUT_OR_- INPUT_TAPE OUTPUT
```

The fixed wire sizes are 36-byte `MslMatchConfig`, 32-byte `MslInput`, and 1022-byte
`MslCompare`. Multibyte fields are explicitly decoded and encoded little-endian, so the files are
directly compatible with the current NumPy dtypes even though the process is big-endian. The thin
adapter is `melee_sim/decomp_port.py`; it writes one tape, invokes QEMU once, and reads the compare
array without a per-frame Python loop or process.

Phase 1 exports source position, velocity, action/motion, animation frame/index, ground state and
line, jumps, stocks, damage/shield, hitlag/hitstun, identity/combo fields, RNG seed, and the five
Slippi state-flag bytes. Items and excluded combat state remain zero. `l_cancel` is zero because its
Slippi post-frame field comes from a custom game patch offset rather than vanilla `Fighter` state.

### Upstream match status and risk

At pin `507715a9`, upstream `configure.py` marks 87 of the imported C units matching and 11
nonmatching. This is a translation-unit classification, not a behavioral correctness percentage.
The checkout has no original DOL or generated objdiff `report.json`, so trustworthy per-function or
per-unit fuzzy percentages were not available without setting up a full matching-decomp build.

The imported nonmatching units are:

- gameplay-risk owners: `ft_0852.c`, `ftcoll.c`, `ftdynamics.c`, `ground.c`, `lbcollision.c`,
  `mpcoll.c`, `mpisland.c`, and `mplib.c`;
- primarily headless-renderer owners in this vertical: `lbspdisplay.c`, `texp.c`, and `texpdag.c`.

For Phase 1 locomotion, the largest correctness uncertainty is concentrated in `mpcoll.c` and
`mplib.c`; their C bodies are present and the FD floor/edge vertical works, but “nonmatching” means
the project has not proven bit-identical generated code. `ground.c` and `mpisland.c` become more
important for dynamic legal stages. `ftcoll.c`, `ftdynamics.c`, and `lbcollision.c` are major Phase 2
combat/pose risks rather than reasons to distrust the demonstrated scalar scheduler and basic
locomotion path.

There is also an explicit float seam: hosted libc `sqrtf` is used where some GameCube headers use a
PPC `frsqrte` seed plus Newton iterations, and the host matrix replacements have not been compared
instruction-by-instruction against Dolphin. The runtime preserves `f32` state and source operation
order elsewhere, but replay/oracle validation must measure this seam before claiming bit-exactness.

### Evaluation

The exploratory result is positive: this is demonstrably a viable way to obtain a source-shaped,
locally runnable scalar oracle, and it avoids reconstructing core action, animation, scheduler, DAT,
and map-collision semantics by hand. It is not evidence that a complete production simulator is a
small final step. The remaining loud-stub and false-predicate surface is large once arbitrary inputs,
combat, articles, death/respawn, all supported characters, and dynamic stages are admitted. Phase 2
should continue by importing reached source-owner families, then compare free-running output against
Dolphin before deciding whether this kernel is only an oracle or the basis of the optimized core.

## Later phases

### Phase 2 — Complete Fox-vs-Fox gameplay on Final Destination

Close normal attacks, hitboxes/hurtboxes, ProcessHit/DmgLog, damage states, shield, reflect, grab,
throw, death, stocks, respawn, Fox specials, and fighter articles. Accept arbitrary controller input
for a complete vanilla Fox mirror and compare free-running state with source/replay evidence.

### Phase 3 — RL 1.0 supported scalar domain

Add Falco, Marth, Sheik, Zelda, Captain Falcon, the remaining five legal stages, relevant
items/articles and stage objects, UCF, singles match rules, and four-player/doubles scheduling.
Expand by shared source owner rather than by replay row.

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
- The current simulator can use the parent worktree's generated data with
  `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data` for later comparisons.
- Phase 1's repository-local PPC32 cross-toolchain and sysroot live under ignored
  `build/decomp_port/`; the documented setup target downloads Debian cross packages without
  installing or modifying host packages.

Copied or adapted runtime source belongs in this branch as tracked files. Do not leave substantive
port work only inside the ignored `refs/melee/` checkout.

## Worklog

Record retained and rejected approaches with the source pin, compiler command, correctness
evidence, and stub/data implications. Add allocation and performance evidence only when the work
reaches the production-architecture phase. Do not treat successful linking as gameplay completion.
