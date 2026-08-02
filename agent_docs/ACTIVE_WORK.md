# Active structural packet — `decomp-port-arm64-ppc` merge polish

## Objective

Bring the reviewed `decomp-port-arm64-ppc` tree to a mergeable state without widening its
correctness policy or accepting regressions to the original eight-character runtime. The retained
review evidence and experiment log live in `agent_docs/DECOMP_PORT_ARM64_PPC_{REVIEW,POLISH}.md`.

## Final boundary

- **Final owners:** `ftCo_800DA190` owns the character-specific grab accessory correction and must
  read `Fighter.kind` rather than a PPC-width raw leading word; `ftCo_800AC5A0` owns the CPU-DI
  command bytes and must not enqueue undefined host locals on its zero-knockback path;
  fighter-pose admission owns live joint capacity; the existing fighter/dynamics publication
  phases own the matrices they consume; Makefile/CI own complete gates and host architecture
  selection.
- **Canonical state:** one source-shaped Match state, one admitted fighter-pose graph, and the
  existing JObj matrices/dynamics descriptors. No replay row, validator classification, benchmark
  mode, or second synchronized cache becomes production state.
- **Consumers:** native/PPC/Wasm simulation, 2- and 4-player reset/copy/save/restore, validation,
  observation/viewer publication, and native/macOS build profiles.
- **Displaced work/state:** remove redundant whole-chain publication and repeated match predicates
  only where equivalent source state already exists; replace unconditional character-heavy pool
  reservation with demand owned by the actual configuration if evidence permits. Do not restore
  cold presentation graphs or displaced renderer machinery.
- **Deletion boundary:** no tolerances, replay-fit clamps, re-recorded stale fingerprints, character
  compatibility flags, fallback dispatch, debug bridges, or retained experimental variants. Every
  performance candidate either passes focused equivalence gates and adjacent benchmarks or is
  removed completely.

## Acceptance

- Original 153-replay domain: zero failures/errors with retained policy; full 366 aggregate: zero
  unclassified failures/errors.
- Focused metadata-preserving PPC checks pass for every touched classification; never use the
  native-lock-only full PPC aggregate as a gate.
- Four-Sheik Final Destination reset passes, and the runtime census covers every publicly admitted
  character across 2/4-player and six-stage configurations without fixed-capacity overflow.
- Common-domain release throughput at 256/512 and lifecycle save/restore return to the comparison
  branch within normal adjacent-run variance, with stable per-ref digests and identical workloads.
- `source-check`, format, native/PPC smoke, Python tests, Wasm/viewer smoke, and complete validation
  selection pass. macOS-only behavior receives static coverage locally and collaborator execution
  on actual hardware.

## Log

- 2026-08-01 — `retained`: review head `479d847d` has one native Samus contact failure, one stale Icies
  PPC follower classification, a four-Sheik fighter-pose capacity abort, +12.85%/+13.54% release
  cycles/frame at 256/512, and +8.45% snapshot/save/restore cost. Mechanical failures are a stale
  source lock, two trailing spaces, an omitted Ice Climbers Makefile filter, inconsistent public
  fighter documentation, and incoherent Rosetta release compile flags. Branch created locally as
  `decomp-port-arm64-ppc-polish`; no remote update is authorized.
- 2026-08-01 — `retained`: pose storage is sized from the admitted player count at 256 joints per
  player, with a 1,024-joint four-player ceiling and a 16-bit capacity. A 16-character, six-stage,
  2/4-player runtime census now exercises that boundary. Four Sheiks consume the observed maximum
  of 1,016 joints; two-player snapshots fall from 686,676 to 659,348 bytes and save/restore return
  to the comparison medians.
- 2026-08-01 — `retained`: the Samus failure was not an mpColl discrepancy. `ftCo_800DA190` read
  `*(s32 *) fp` as the fighter kind, relying on the retail 32-bit leading GObj pointer layout; on
  the 64-bit host that is the pointer's upper half. Reading `fp->kind` restores the source-owned
  Link/Samus/Young Link exclusion. The 15,481-frame focused replay then has no gameplay mismatch.
- 2026-08-01 — `retained`: the Ice Climbers split was not follower collision or fma order. Retail
  `ftCo_800AC5A0` carries caller-register residue for its unassigned CPU-DI bytes when the
  pre-ProcessHit knockback vector is zero; hosted C instead consumed arbitrary uninitialized stack
  bytes. Initializing the local command to neutral removes undefined behavior. Both backends then
  retain only the already-classified item residue, with no follower mismatch.
- 2026-08-01 — `rejected`: a post-solve dynamics refresh on the PPC path and an explicit
  CaptureCut fmadd did not own the Ice Climbers mismatch and were removed completely.
- 2026-08-01 — `retained`: remove the first of two whole-chain dynamics matrix publications in
  `Fighter_ProcessHit_8006D1EC`. The later post-solve publication remains the canonical end-frame
  owner added for retail-probed correctness. Seven exact replays whose locks originally depended
  on dynamics publication remain byte-exact, while adjacent 256-batch sampling improves about
  6--7% against the redundant-publication control; 512-batch sampling is neutral within noise.
  A material common-domain throughput gap to `experiment/decomp-port` remains open.
- 2026-08-01 — `retained`: the sole post-solve dynamics publication now consumes the JObj tree's
  canonical dirty bits rather than forcing every link dirty. The solver's setters already
  propagate dirtiness down each mutated chain. All seven retail-probed locks remain exact.
- 2026-08-01 — `retained`: the larger FObj and class-piece reserves are selected only for a Match
  containing Samus, the documented sole consumer through her grapple. The all-character census
  still reaches the Samus high-water safely; the ordinary two-player snapshot is now 611,420
  bytes, below the comparison branch's 633,156, with faster save/restore medians.
- 2026-08-01 — `retained`: native release compiles the now-widespread `__fmadds` operation as the
  compiler intrinsic it represents rather than an out-of-line call from O0 source-shaped TUs.
  Debug, Python, Wasm, and PPC keep the existing external owner. Seven sensitive locks stay exact;
  extending the treatment to fmsubs/fnmsubs regressed adjacent throughput and was removed.
- 2026-08-01 — `retained`: final alternating common-domain samples close the original
  +12.85%/+13.54% regression. Batch 256 median cycles/frame are 63,765.7 comparison versus
  62,011.2 polished (-2.75%); batch 512 is 48,921.1 versus 49,625.9 (+1.44%). The per-ref digests
  are stable and workloads identical; the remaining spread is ordinary machine contention, not a
  material regression.
- 2026-08-01 — `retained`: final native aggregate after the representation changes has 286 exact
  passes / 80 classified / 0 failures / 0 errors over all 366 replays. The obsolete Samus
  classification is deleted. Twenty-three Ice Climbers snapshots were refreshed only after a
  guarded audit proved their drift was confined to the already-owned item identity/residue lanes;
  the one mixed Dream Land entry retained its byte-identical pre-existing wind-puff fields.

# Previous structural packet — Ganondorf

## Objective

Add Ganondorf (internal kind FTKIND_GANON 25, public/CSS char id 25) to the supported domain.
Ganondorf is the Captain Falcon semi-clone: every special is owned by the already-ported
`ftCa_Special*` TUs (their `FTKIND_GANON` branches select the Ganon gfx/motion variants), and
the only character TU is the Matching `ftGn_Init.c` (motion-state table delegating to ftCa
handlers, strings, item/knockback callbacks). Registry rows, PlGn extraction (five costumes
Nr/Re/Bu/Gr/La + PlGnAJ + PlGnDViWaitAJ), native DAT translation reusing the Captain ext-attr
layout (`ftCa_Init_OnLoadForGanon`/`ftCa_Init_LoadSpecialAttrs` are the shared owners; anim
count 318; `MSL_FIGHTER_ARTICLES_NONE`), derived part-admission row (54 live / 30 cold of 84),
public API/viewer/validation admission, and a 30-replay suite wired into the aggregate.

## Final boundary

- **Owner (character):** the imported `ftGn_Init.c`, byte-identical to the pinned decomp. No
  OnLoad ext-attr mutation (ftCa_Init_OnLoadForGanon only pushes attrs — no DK-style store
  guard needed); no motion/fighter-var pointers (fv/mv `.gn` alias the ftCaptain views).
- **Effects:** EfGnData.dat owns efAsync bank 19 with a real particle bank, but the hosted
  loaders do NOT load it — the Falcon precedent: every Ganon-reachable efSync id
  (0x50B..0x50F -> efLib model creates on bank-19 model ids 0x4A38..0x4A3D) is model-backed
  only, so no generator RNG projection consumes bank-19 ids (EfCaData bank 4 is likewise
  unloaded for Falcon). `MSL_CORE_EFFECT_BANK_CAPACITY` stays 19.
- **Data:** `PlGn*` + `EfGnData.dat`; manifest 146 -> 155 files; no capacity bumps needed for
  the fifteenth character (arena and archive-cache headroom from the DK packet hold).
- **Exactness fix shipped:** the ThrownLw follow `ftCo_800DE508` now ports its MWCC fusions
  (GALE01 0x800DE558/0x800DE56C fmadds both lanes — the same shape as the fused
  ftCo_800DB464 capture-follow twin); identity at current inputs (unit scale), aggregate
  byte-stable.
- **Heap-overflow fix shipped (fighter_pose.c pose-program map):** a FigaTree node with ZERO
  tracks wrote its `track_nodes` map entry at the NEXT node's first-track position; a trailing
  zero-track node in the final program wrote one uint16 past the calloc'd map. Ganondorf's
  PlGnAJ bank is the first with such a tree, so every process that ran the 15-character
  game-data preload corrupted its heap (symptom: `malloc(): invalid next size` aborts in
  later in-process work — pytest EnvBatch tests after an in-process validate_one). The map is
  only ever read at tracked nodes' first-track indices (request_figa early-returns for
  trackless nodes), so skipping the write for zero-track nodes is read-identical: native
  aggregate byte-stable after the fix, ASan-clean end-to-end (debug recipe: ASan-build
  libmelee_core via `make python-library PYTHON_BUILD=/tmp/asan_build NATIVE_OBJ_DIR=...`
  CFLAGS="-O1 -g -fsanitize=address ..."`, stub the ~34 gc-section-dead undefined symbols,
  LD_PRELOAD libasan into python).

## Suite state (container authoritative): aggregate 366 = 285 pass / 81 classified / 0 fail

30 replays (all six stages: 5 FD / 5 BF / 5 FoD / 5 YS / 5 DL / 5 event-verified frozen PS;
opponents Captain Falcon, Falco, Fox, Jigglypuff, Marth, Peach, Samus, Sheik; Slippi netplay +
anonymized ranked + mainline; zero re-wraps, raw pad lanes populated in all 30 zip files):
**container native 25 pass / 5 classified, PPC identical fingerprints on all five.**

**FROZEN-PS POLICY CORRECTED (user-directed):** the Stadium Transformations event (0x41) was
added at Slippi 3.18.0 (refs/slippi-wiki/SPEC.md) and fires on every transformation change, so
zero `stadium_transformation` events over a full game proves frozen for ANY version >= 3.18.0.
The prior "3.18 recorders are silent" note was a wrong inference from v3.18.0 controls whose
non-frozen status came from the start-block `is_frozen_ps` flag — which is only trustworthy at
3.19.0+. Empirical confirmation: the three v3.18.0 zero-event Stadium games in this zip
validate fully bit-exact over 11-13k frames each against the frozen-only hosted Stadium model
(a transforming game would diverge massively within the first ~75-second cycle). The five
classified families:

- `2025-03` @5974: single l_cancel row on Zelda's first post-transform frame (new mechanism id
  `transform-swap-stale-lcancel-byte`): Slippi keeps L-cancel status in the repurposed
  PlayerData+0x25FF byte and Zelda's freshly-swapped block carries stale retail memory (0x50)
  for one frame until the 8006c324 reset hook runs; hosted per-fighter lcancel state starts 0.
  Export-only lane, unmodelable without emulating uninitialized retail memory.
- `24891` @10479: 35-row P1 pos_y episode — Ganondorf down-throws Marth over FoD's descending
  right platform; the `ftCo_800DDDE4` mid-frame release resolve picks a floor line 0.075 below
  retail's (our resolved y 2.0251 vs retail 2.1001 back-computed; the recorded platform height
  1.19994 gives our line at +0.825, and the descending platform line vs the curved stage flank
  sit within 0.075 at that x). pos_x bit-exact throughout; heals at the landing snap. Same mp
  floor-line-pick seed as the dk `auto-dk-2025-04` entry (retail arbitration there showed
  retail == recording). Debug recipe that isolated it: temp trace in ftCo_800DE508 (follow
  inputs) + ftCo_800DDDE4 (release vec / resolve output / live platform heights via a temp
  slippi.c accessor).
- `25827` @3816 (BF x=72.84), `medium-fox` @3819..3822 (YS x=57.27), `medium-sheik` @5451
  (YS x=57.33): the documented open wall-hug clamp 1-ULP pos_x re-derivation family, all
  single-episode self-healing, identical on both backends.

## Follow-ups

- The dk suite rejected three v3.18.0 Stadium candidates under the old (wrong) "3.18 recorders
  are silent" policy; under the corrected policy they are admissible if their
  stadium_transformation streams are empty (verify with the frozen-model validation
  arbitration). Same check applies to any earlier suite's v3.18.0 PS rejections.

## Log

- 2026-08-01 — `merge` (macos-native-build update integrated): the collaborator's rebased
  macos-native-build (deterministic hosted source identities + targeted compatibility flags +
  -fpermissive removal) was linearized onto the original branch tips as
  `macos-native-build-linear` (tree-identical to their tip) and merged. Conflicts: effects.c
  (kept the DK model-only effect-bank path, adopted MSL_LBARCHIVE_END) and this doc. The
  deterministic-token representation shifted item-var pool residues on the four
  `unrecorded-item-pool-residue` icies classifications (item.misc* lanes only — every other
  snapshot stat identical; verified by full pre/post field diffs in msl-luigi); native
  snapshots + 4 output locks re-recorded. Container aggregate 366 = 285 pass / 81 classified /
  0 fail. PPC snapshots for all four verified bit-identical pre/post merge (platinum's
  aggregate-PPC snapshot was already stale beforehand — per-suite PPC remains the gate).
  macOS-native aggregate unchanged (same 122/18/201 with identical drift fingerprints
  pre/post; known non-gate signed-zero/ULP condition).

- 2026-07-30 — `open` (this packet: Ganondorf source validation packet; all 30 zip files
  admitted after the frozen-PS policy correction and wired straight into the aggregate with
  locks + five classifications; aggregate 366 = 285 pass / 81 classified / 0 fail native;
  PPC ganon suite identical fingerprints; fighter_pose track_nodes heap overflow found by the
  15th character's preload and fixed).

# Previous packet — deterministic hosted source identities

## Objective

Remove host-address identity from the native `u32` fields retained by the source-shaped runtime.
Allow GameData and native DAT arenas to map anywhere without making match construction, HSD lookup,
copy, or save/restore depend on ASLR or allocator reuse.

## Final boundary

- **Final owner:** `MslMemoryContext` owns GameData raw-file tokens; `MslNativeDatContext` owns native
  DAT descriptor tokens; the HSD ID API owns conversion from hosted pointer identity to its `u32`
  key. Callers do not manufacture hosted IDs by truncating pointers.
- **Canonical state:** true native pointer fields store full host pointers. A GameData source-width
  file address is a one-based byte offset in its GameData arena. An HSD pointer ID is a tagged byte
  offset in the native DAT arena, Match arena, or core image. Zero remains null and equivalent
  immutable GameData produces identical tokens regardless of mapping address.
- **Consumers:** `lbFile_800168A0` and fighter animation subarchive loading consume GameData tokens;
  native DAT translation, AObj/fighter-pose loading, JObj/RObj/PObj reference resolution, fighter
  metal, ground material, and `HSD_IDTable` consume HSD pointer IDs. Match copy and save/restore carry
  these stable scalar tokens unchanged while relocating only real pointer slots.
- **Displaced state/work:** delete low-32 mapping retries, low-32 pointer reconstruction, fixed/low
  address assumptions, and hosted pointer-to-`u32` casts at HSD ID call sites. No numeric-ID
  savestate relocation table is introduced.
- **Deletion boundary:** no hosted GameData file handle or HSD ID key contains bits from a native
  address. Arena mappings use the ordinary host allocator once, all token decode is range-checked
  at its owning boundary, and there is no low-address compatibility path or fallback
  representation.

## Evidence and acceptance

- Keep two equivalent GameData instances resident simultaneously so their GameData and native DAT
  mappings must differ; destroy the snapshot's source instance, restore into the other, cross an
  animation/HSD lookup continuation, and require exact state equality.
- Preserve the exact native/PPC/Wasm correctness gates and retained replay classifications/locks.
- Compare lifecycle and release replay throughput against the current `experiment/decomp-port`
  baseline under identical build, data, CPU, and benchmark inputs; any retained change must be
  throughput-neutral or better.

## Log

- 2026-07-31 — `open`
  Scope: GameData file handles, native DAT descriptor identities, HSD pointer IDs, and the existing
  cross-GameData savestate continuation.
  Hypothesis: arena-relative tokens eliminate the address-reuse hole and the 64-attempt mmap scan
  without adding gameplay work; the conversions occur during initialization or object creation.
  Evidence: PR #11 currently derives these `u32` values from arena pointer low bits. Typed
  savestate relocation correctly moves full pointers but cannot and should not infer provenance for
  numeric fields, so a restore into simultaneously resident equivalent GameData retains stale HSD
  keys under the current representation.
  Disposition: implement the singular token representation, strengthen the existing recreation
  smoke to force distinct simultaneous mappings, then checkpoint exactness and performance.
  Next: replace low-32 encode/decode and audit every hosted HSD pointer-ID cast before running the
  focused native smoke.

- 2026-07-31 — `checkpoint`
  Scope: complete hosted file/HSD token cut plus simultaneous-GameData restore smoke.
  Hypothesis: every persistent HSD pointer identity belongs to native DAT, Match, or the core image;
  temporary retail descriptor copies must resolve to an equivalent persistent source owner rather
  than acquiring a new runtime registry.
  Evidence: the first native smoke rejected `ground.c::get_jobj_inline`'s stack copy. Its source
  descriptor is the immutable `Ground_803B7E0C`; hosted loading now uses that image-relative owner
  and publishes the copied scale before consumption. No other unowned identity was reached. The
  complete native smoke, strengthened cross-GameData save/restore continuation, and PPC smoke pass.
  `source-check` verifies the canonical inventory; the locked `refs/melee` checkout is not yet
  populated in this isolated worktree.
  Disposition: retain the singular tokens and source-image identity for broader gates.
  Next: populate the local source reference, run source/Wasm/viewer/replay gates, then benchmark the
  exact candidate against `experiment/decomp-port`.

- 2026-07-31 — `experiment`
  Scope: save/restore cost after sharing ELF/Mach-O image-range discovery between HSD identity and
  savestate relocation.
  Hypothesis: the lifecycle target's four restore samples are too small to distinguish a real
  roughly 0.035 ms candidate increase from timer/layout noise. Temporarily raise only its snapshot
  sample count to 256 in the rebased PR control and candidate, rebuild the same debug profile, and
  compare adjacent pinned-core runs; remove the probe afterward.
  Evidence: moving savestate image discovery behind the shared external helper measured 0.6705 ms
  median restore versus 0.6545 ms for the rebased PR control at 256 samples (+2.4%), even after
  deleting its duplicate loader walk. That refactor was rejected. With PR #11's original local
  savestate discovery restored, adjacent candidate/current-branch medians are 0.6595/0.6580 ms
  restore and 0.4775/0.4780 ms save; snapshot size remains exactly 633,156 bytes.
  Disposition: retain deterministic tokens but leave the measured savestate path and layout intact;
  keep source-image lookup isolated to cold HSD identity construction.
  Next: remove the temporary 256-sample probe and rerun the ordinary lifecycle/correctness gates.

- 2026-07-31 — `checkpoint`
  Scope: final production throughput and lifecycle comparison against `experiment/decomp-port`.
  Hypothesis: deterministic token conversion remains outside gameplay and does not regress the
  resident 256/512 production profiles or copy/save/restore.
  Evidence: six alternating release runs preserve both benchmark digests. Median cycles/frame move
  47,900.1 to 47,326.3 at 256 (-1.20%) and 43,673.2 to 43,922.8 at 512 (+0.57%, within run/layout
  variance). The high-resolution lifecycle probe is neutral against current for save, restore,
  artifact size, initialization, and stepping. Exact commands and medians are retained in
  `agent_docs/PERFORMANCE.md`.
  Disposition: retain the identity representation as performance-neutral; make no speedup claim.
  Next: run final native/PPC/Wasm/viewer/replay gates on the exact commit candidate.

- 2026-07-31 — `checkpoint`
  Scope: hosted compiler portability at the archive variadic boundary.
  Hypothesis: PR #11's Apple-only low-word terminator check masks a caller/callee type mismatch and
  should be replaced by the pointer-width sentinel that the hosted loader actually consumes.
  Evidence: a fresh Linux Clang 18 build reproduced the upper-word failure during Fox costume
  loading (`symbol=0x7fff00000000`). Converting every archive-list terminator to
  `MSL_LBARCHIVE_END` on hosted builds removes the mismatch; the same fresh Clang native smoke now
  passes. The 32-bit source build retains its literal-zero ABI.
  Disposition: retain the typed hosted boundary and delete the Mach-O-only low-word heuristic.
  Next: rerun the exact final native/PPC/Wasm/viewer/replay gates and commit the local stack.

- 2026-07-31 — `retained`
  Scope: final deterministic hosted identity and compiler-portability boundary.
  Hypothesis: the cleanup must preserve every supported replay lock and production digest while
  allowing equivalent GameData owners to coexist at unrelated host addresses.
  Evidence: simultaneous-GameData save/restore, GCC and Clang 18 native smokes, PPC smoke, source
  lock, Wasm parity, live Chrome viewer, and formatting all pass. Debug and optimized-release gates
  both remain 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames.
  Final 256/512 release samples preserve digests `8ef126a41244d514` / `6f91f23e3553a090` at
  47,259.0 / 43,710.2 cycles per frame, inside the retained A/B envelope.
  Disposition: packet complete and ready for its local implementation/evidence commit; no remote
  branch has been updated.
  Next: verify the native macOS profiles on Apple hardware before final rebase merge.

- 2026-07-31 — `retained`
  Scope: merge-review reduction of source-image IDs and hosted compiler workarounds.
  Hypothesis: image-base equality is sufficient for the encode-only static-image owner; native DAT
  is the only inverse-ID consumer. Explicit hosted return values and volatile rounding slots can
  remove blanket diagnostics and undefined writes without changing source/PPC behavior.
  Evidence: the callsite audit found one static-image encoder and two inverse consumers, both native
  DAT descriptors; Clang exposed two legacy return diagnostics hidden by the global suppression.
  GCC, fresh Clang, PPC, Wasm/viewer, and both 153-replay gates pass. Adjacent release medians are
  neutral-to-better at 256 and 512 environments with both production digests unchanged.
  Disposition: retain the narrower decoder, defined hosted spill, and explicit hosted returns; the
  source/PPC paths remain unchanged and the general image-range module is deleted.

# Previous packet — Donkey Kong

## Objective (closed)

Add Donkey Kong (internal kind FTKIND_DONKEY 3) to the supported domain: the thirteen Matching
chara TUs (`ftDk_Init/SpecialN/SpecialS/SpecialHi/SpecialLw`, the seven `ftDk_Heavy*` cargo-hold
states, `ftDk_MS_345_0`), registry rows, PlDk extraction, native DAT translation
(`MSL_FIGHTER_ARTICLES_NONE` — DK spawns no articles), part-admission row, effect bank 8,
public API/viewer/validation admission, and a 27-replay suite (staged standalone, not yet in
the aggregate). The cargo carrier/victim common owners (`ftCo_Cargo*`, `ftCo_Shouldered`) were
already ported; DK is their first live consumer.

## Final boundary

- **Owner (character):** the imported ftDonkey TUs, byte-identical to the pinned decomp except
  the OnLoad idempotent-store guard (ledger `canonical:donkey-onload-attr-store-guard`): retail
  writes the three cargo-walk anim durations into the shared PlDk ftData ext-attr on every
  fighter load; GameData preload performs the first mutation before the native DAT arena seals
  read-only, later Match constructions recompute and skip the identical store (ftPe_Init_OnLoad
  precedent). DK motion/fighter vars carry no pointers — neither Pikachu bug class applies.
- **Effects:** EfDkData.dat is MODEL-ONLY — both leading effDonkeyDataTable words are
  unrelocated NULLs and retail efAsync_LoadSync skips psInitDataBank entirely; both hosted
  loaders now publish an empty command bank for that shape (ledger
  `canonical:model-only-effect-bank`). All DK efSync ids (0x4C6..0x4CC) are pure
  efLib model creates — no generator RNG cases needed.
- **Data:** `PlDk*` (Nr/Bk/Re/Bu/Gr — five costumes) + `EfDkData.dat`; manifest 146 files.
- **Capacity:** archive cache 3072 -> 4096 entries; shared game-data arena 64 -> 96 MiB
  (fourteenth character).

## Exactness fixes shipped with the packet

- **Pose-table dropout resync** (ledger `canonical:fighter-pose-table-dropout-resync`): the
  native pose engine re-derived track time from a fractional absolute frame when dropping from
  the integer-frame table fast path to fractional-rate decoding (cargo walk anims switch rate
  every frame). The decoder now resyncs at the recorded last table frame (integer wait
  subtraction is exact) with a dry walk, so the following rate step reproduces the source
  engine's incremental rounding bit-exactly. Root-caused from a 1-ULP carry-bone fork (victim
  XRotN is ROBJ-constrained to DK's TransN2, publishing the animated spine chain directly into
  pos). Three DK replays became fully bit-exact; two more dropped their native-only rows.
- **HSD_FMod wrap fnmsubs** (`canonical:anim-loop-wrap-fnmsubs`), **capture-follow /
  shoulder-timer / Hand Slap fusions** (`canonical:capture-follow-fmadds`) — retail-faithful
  boundaries, identity at current inputs.

## DK tie triage (2026-07-30, user viewer report — RESOLVED, no sim change)

The horizontal tie in the live viewer is not produced by the sim. Verified from extracted
data: DK's only dynamics chain is the tie (parts 66-71, rooted under chest part 26); NO
hurtbox rides it (hurtbox bone 73 attaches at part 3, near the hip) and NO subaction Create
Hitbox anchors on it (hitbox bones: 0,4,7,17,25,29,30,31,43,44,51,52,53,62). The headless
policy in ftdynamics.c therefore correctly skips its dangle solver (same class as Luigi's
hair and the capes), and the bit-exact corpus confirms zero gameplay impact. The on-screen
body outline comes from slippilab's donkeyKong.zip per-animation 2D frames (the sim supplies
only action/frame/pos/facing + hitbox capsules), and those outlines were generated without
dangle physics — the stiff tie is baked into the asset and appears in slippilab's own viewer.
If presentation parity ever matters, the lever is regenerating the zip frames, not the sim.

## Session 4 (2026-07-30): pre-fusion-era audit — Tier A ports

Corpus-wide inventory (grep fused ops per asm TU vs __f* markers per C TU) found the
pre-fusion-era gap set. Ported with per-site asm verification (126 sites): Sheik chain (35),
Ice Climbers belay string (28), Pikachu QA/Skull Bash/jolt spawns (18), Sheik
needles/chain-charge/Vanish (12), Zelda Din spawn/Farore (7), Fox reflector turn (3), Icies
squall/belay/spawns (11), ftcommon hitlag/nudge/grab-timer (12). Native aggregate byte-stable
throughout (identity at current inputs); PPC pool-residue-fed lanes shift where
backend-specific residuals are inexact (re-recorded).

REMAINING INVENTORY (asm-vs-marker gaps, supported-domain candidates, unported):
ftCo_Guard (8), itcoll (8), fighter.c (5), lbcollision (7), mplib (10 — likely inline
double-count), ftCo_0A01 (75 — needs per-fn reachability triage: DeadUpStar etc.),
Peach/Mario/Luigi SpecialS (2/2/1), Samus SpecialN/Lw_1 (2), items: din fire (3+2), toadspore
(2), missile/chargeshot (2), sword/dosei/foods/freeze (4), heiho (4), iteffect (3), it_26B1
(4), itmaplib (6), item.c (1); ft small TUs: ftCo_09F7 (6), JumpAerial (4), DamageIce (4),
Damage/DamageFall/Throw/Thrown/TurnRun/CaptureCut/Bury/FlyReflect/DamageBind/DamageSong
(2 each), ftpickupitem (3), ftlib (2), ft_07C1 (8), ft_0D4D (6), ft_0899/0C31/0D27/0DF0,
ftCo_800C7590/7CA0; infra to triage per-function: lbvector (46), spline (58 — verify the
session-6 port shape), mtx (98 — display vs gameplay split), grlib (8), grizumi (2),
grpstadium (1), ground (1), groldpupupu (4), jobj (1). Skipped as unreachable: itlinkhookshot
(113, Link), ftcpuattack (46, in-game AI), pobj/lbbgflash/ftafterimage/camera render, itzako.

## Session 3 close (2026-07-30): THE ROLLOUT FUSION SET — aggregate 336 = 260 pass / 76 classified / 0 fail

The "cargo-walk" family was misattributed: the diverging player is P1 JIGGLYPUFF in
ftPr_MS_SpecialNTurn (351) — Rollout pinned at FD's edge, turning, speed decaying. The rollout
TU carried FOURTEEN unported MWCC fusions (ftPr_SpecialN.c predates the fusion discipline):
four Turn_Phys gr_vel accumulates (0x80140934-family — THE seed), four charge-spin roll-angle
steps, two turn-spin steps, and the bounce-dust half-height (inline x4). Porting them retired
the 22-row DK family to one wall-hug pos row, promoted classified puff
Game_20260318T101341 to fully bit-exact BOTH backends (entry retired), and shrank
rollout_ends_ys 64->2 and specials 849->817. All re-records native≡ppc; locks refreshed;
the mislabeled DK classification rationale corrected.

## Session 2 close (2026-07-30): suite IN THE AGGREGATE — 336 = 259 pass / 77 classified / 0 fail

All seven residual families classified with native+PPC snapshots (identical fingerprints both
backends). Retail engine-dump playback arbitrated two of them (auto-dk-2025-04 grapple-swing
fork and the 9560 wall-hug episode): retail reproduces the RECORDING, so these are hosted
ULP residuals, not recorder profile — the wall-hug clamp's pos_x re-derivation
(mpLib_8004E398/511A4 X-at-Y + mpcoll hug candidates; inputs bit-equal, output 1 ULP) is the
documented open lever, alongside the known Samus grapple-chain integration. Trace toolkit
used: bracketing pos probes around coll_cb + an auto-instrumented MPX dump of every
mpcoll cur_pos.x writer (site L2419 = the LeftWall hug clamp) — rebuild from this description.
Fused retail-faithfully along the way (identity at current inputs): throw-release velocity
blend (0x8006BC7C/90), walk-cycle wrap (0x800E0010), plus session 1's HSD_FMod/capture-follow
set. dk.json joined include_suites with 27 output locks; tests updated to 336.

## Suite state (session 1 close, container authoritative)

27 replays (all six stages; Fox/Falco/Marth/Sheik/Jigglypuff/Samus/Captain Falcon; Slippi
netplay + anonymized ranked + mainline; both Pokémon Stadium entries event-verified frozen,
three v3.18.0 PS candidates rejected — 3.18 recorders are silent on transformation events):
**container native 20 pass / 7 fail; PPC 20 pass / 7 fail — identical failures and
fingerprints on both backends.** The seven shared families:

- `auto-dk-2025-04` @8654: 150 rows, Samus offstage low on FoD, a position resolve (wall-line
  pick on the curved underside?) shifts ~0.017 then heals — the one non-ULP family; needs a
  retail probe before classification.
- `slippi-2025-04_..152714` @2663: DK walk-speed ULP family (speed_air/ground_x_self 16 rows +
  pos_x tail).
- Five 1–6-row self-healing pos ULP families at stage-edge positions (22123 @7897, 9560 @2120,
  basic-dk-2025-04 @7265, medium-marth-0912 @7060, slippi-..141746 @9611).

(Superseded by session 2 above.)

## Log

- 2026-07-30 — `open` (this packet: DK source validation packet, suite staged standalone;
  aggregate re-verified 239 pass / 70 classified / 0 fail native after re-recording the two
  icies pool-residue analogs shifted by the arena growth — same ripple class as the Pikachu
  packet).

- 2026-07-30 — `closed` (samus suite extended 12 -> 25 from ~/SSBM/Replays/Samples/samus_replays.zip;
  aggregate 309 = 239 pass / 70 classified / 0 fail)
  **Event-based frozen-PS policy shipped**: PS admission now requires recorder version strictly
  newer than 3.18.0 and zero `stadium_transformation` events over the full game, ignoring the
  game-start `is_frozen_ps` flag. Validated against transforming controls (ics.zip v3.19 PS
  games emit 13/27 events; two v3.18.0 non-frozen PS games emit ZERO over 3-5 minutes — at
  3.18.0 exactly the recorder is silent, so absence proves nothing there). Both admitted PS
  captures (medium-fox-2025-10, medium-sheik-2026-03, v3.19.0) show zero events over ~14k
  frames.
  **Re-wrap rejection class established (5 of 18 zip files rejected)**: one PS capture at
  v3.18.0 exactly (Nicki, flag also false); one 2020 game and three anonymized ranked FoD
  games re-wrapped to the 3.18 layout — payload sizes match 3.18 but raw_analog_y and/or raw
  c-stick lanes are all-zero (genuine captures show thousands of nonzero samples) and gecko
  lists are 2.8-4.9KB vs the genuine 57KB. The three ranked re-wraps fork discrete lanes
  (action_id/facing) during the countdown under EVERY era-flag combination — the destroyed
  raw pad lanes feed the UCF ring machinery, unmodelable by flags (first fork signature was
  the cardinals one: retail speed 1.28375 = 1.3 x raw 0.9875 stick our cardinals patch snaps
  to 1.0; flags moved matched 90 -> 101 only). The existing bit-exact diamond-platinum ranked
  captures carry their TRUE older versions (peppi doesn't expose the newer lanes at all) —
  version-vs-populated-lane disagreement is the re-wrap tell.
  **FObj reserve fix (arena abort)**: medium-sheik-2026-06 (DL Sheik/Samus) aborted post-seal
  (`HSD_ObjAllocAddFree` size-64 grow at used=686912). Backtrace (new glibc-gated
  backtrace_symbols_fd dump on the msl_memory_alloc failure path, kept): Samus AIR
  grapple-catch (ftCo_AirCatch_Anim -> it_802B7C18 -> it_802B743C) runs HSD_JObjAddAnimAll
  across the doubled beam link chain, one FObj per track per link jobj, crossing the former
  256-slot reserve -> now 512 (scalar.c pre-seed block). Replay then bit-exact both backends;
  no classified fingerprint moved (pool growth is layout-neutral for the corpus).
  **PPC snapshots recorded for the two samus classified entries** (icies-precedent
  completion): fox-d18-2026-04 PPC residual is ONLY the 1-ULP pos_y @7389 row (the @9075
  contact-gate flip is native-only, consistent with dolphin-ulp-contact-gate); master-samus
  PPC keeps the taut-rope fork @15146 (390 rows vs native's 135). master-peach is PPC-exact
  (its edge-stop flip is native-only). Digest key trap: compact snapshots store
  `mismatch_fields_digest`, not a digest under `mismatch_fields` — the wrong key silently
  reads as raw fields and reports drift/FAIL.
  **Stale-PPC-binary trap**: build/melee_core/ppc/melee-core-ppc predated the Pikachu port and
  every suite replay errored with the supported-character banner; `make ppc` first.
  Gates (msl-luigi container): samus suite native 22/3/0, PPC 23/2/0; aggregate native
  239/70/0 with output locks appended for exactly the 13 new replays; pytest 44 passed
  (suite-inventory assertions bumped 296 -> 309, filtered cases stay 133 since every new
  replay contains Samus).
- 2026-07-29 — `retained` (session 3: both residual fails dispositioned; pikachu.json JOINED THE
  AGGREGATE — 296 = 226 pass / 70 classified / 0 fail)
  **master-master root-caused as a recorder-profile residual**: the drifting FoD items are Fox
  LASERS (kind 54; the static kind-74 item is his blaster gun) whose spawn X rides the
  RThumbNb muzzle transform (ftFx_SpecialN_FtGetHoldJoint -> lb_8000B1CC with offset
  (0, 1.2325, 4.2636)); every flight all game is offset ~2^-16 with the fraction preserved by
  the laser's integral +7.0/frame velocity. A new MSL_MUZZLE_PROBE in the engine-dump Dolphin
  (lb_8000B1CC entry filtered on that offset vector, dumping the out vector and the full
  ancestor jobj chain) showed retail interpreter playback of this exact recording reproducing
  OUR bits exactly at frames -24 and 78 — every rotate/scale/translate/matrix word of all 12
  chain links identical. The recording disagrees with its own canonical re-simulation: the
  anonymized ranked capture records Dolphin without its build/execution profile. Classified
  dolphin-ulp-fighter-and-item-profile (bounded laser pos/vel intervals + derived angle bytes +
  reconverging fighter-pos ULP intervals; all discrete lanes exact; native/ppc fingerprints
  identical: 040cf4c553e20933, 9,734/12,233).
  **slippi-2025-11** classified dolphin-ulp-motion-profile (one 1-ULP wall-ride pos_x row,
  3,711 strict suffix, native/ppc identical: 72651ec5c1e33764).
  Wiring: pikachu.json added to melee_core_aggregate include_suites (271->296); 25 output locks
  recorded (271 existing locks byte-stable; the two icies locks re-recorded in session 1 stay
  put); tests updated (296 pins, manifest set, classified-loader suite list, coverage
  parametrize). Container full pytest 44 green (incl. ppc-snapshot verification); Mac pytest +
  source-check green. Trap note: the container clone's git HEAD is the stale clone base —
  diff lock files against the MAC HEAD, not the container's.
  The Pikachu port is at the endgame state: 23/25 bit-exact on both backends, 2 classified,
  suite in the aggregate.

- 2026-07-29 — `retained` (session 2: extended suite to 25 + the phantom-revisit decomp fix)
  Scope: the 12 remaining usable batch replays joined pikachu.json (25 total; three anonymized
  ranked now, the v3.15/v3.16 pair carrying cardinals=false — both bit-exact, which isolates
  master-master's fork from the pre-cardinals flags). Container native 23/25, PPC 23/25,
  identical fails.
  **Fix (ledger canonical:phantom-revisit-hit-gate)**: 40830 (DL Pikachu/Falco) forked at 3702 —
  our sim landed a full usmash hit one frame before retail. Retail collision probe
  (MSL_COLLISION_PROBE on the engine-dump Dolphin) showed the frame-3702 capsule sweep
  BIT-IDENTICAL to ours (two same-group grazes, coll_distance 0.0032/0.0092, both phantom-range)
  with ftColl_80076ED8 returning (1, 0) where ours returned (1, 1). GALE01 asm
  0x8007702C..0x80077444: a phantom-range contact whose victim is already in the hit group's
  victims_2 registry (populated by the first phantom's inlineB0 across ALL same-group hitboxes)
  returns FALSE; the NonMatching decomp fell through to the full-hit path instead. One-branch
  fix; the sibling item path (ftColl_80077C60) was already faithful. 40830 → bit-exact 8,892;
  aggregate 203/68/0 byte-stable (the bad branch never fires in the 271-replay corpus);
  container pytest 43 green.
  Remaining fails: (a) master-master (FoD ambience-item ULP from -24, both backends,
  next session's target — the icies FoD construction-RNG/grIzumi toolkit applies);
  (b) slippi-2025-11 = ONE 1-ULP pos_x row (fast-falling Falco hugging the YS right underside
  wall, pos re-derived from the wall line each frame; 3,711-frame exact suffix; native and PPC
  bit-identical) — meets the dolphin-ulp-motion-profile bar; classify when pikachu.json joins
  the aggregate (a classification entry for a non-aggregate suite breaks
  test_native_validation_compares_complete_classified_replays' case lookup).

- 2026-07-29 — `retained` (session 1: the full packet, three hosted-portability fixes)
  Scope: full character packet + 13-replay suite; container gates green.
  1. **Thunder motion-vars pointer widening** (ledger pikachu-thunder-vars-pointer-widening):
     four replays segfaulted (ditto + all three mainline) — `specialhi.x4 = 1` writes landed
     in the widened `speciallw.x0` high half (0x1_00000000 dereferenced in it_802B1FE8), and
     enter-time pointer zeroing flowed through the `specialhi.x0` alias. Fixed with a hosted
     speciallw layout (flag kept at its mv+4 alias position, full-width pointer past the
     aliased pair) + two named-owner zeroing sites in ftPk_SpecialLw.c.
  2. **Jolt item-var native sampling** (scalar.c item_var_source_byte): the ground/air jolt
     structs lead with pointers, so raw retail-offset sampling read padding on 64-bit hosts —
     the whole item.misc family across 7 replays (expected float bytes vs constant 0/1).
     Pikachu cases added per the Chain/DinFire/icies precedent; this promoted the suite from
     4 to 12 native passes.
  3. **Aggregate fingerprint re-record**: the wider archive arena + grown Fighter struct
     shifted deterministic pool-residue analogs on two icies classified replays (medium-fox
     2025-08, ys-fox 2025-07) — native mismatch stats identical, fingerprints moved;
     re-recorded native snapshots + 2 output locks (PPC snapshots verified unchanged).
     Aggregate 271 = 203/68/0; container pytest 42 green; Mac pytest green; source-check
     159 deltas.

# Prior structural packet — Ice Climbers (Popo + Nana follower entity)

## Objective

Add Ice Climbers (external char, internal kinds FTKIND_POPO 10 / FTKIND_NANA 11) to the supported
domain. Unlike every prior port, one player slot owns TWO fighter entities: Nana is spawned
alongside Popo by the already-imported `Player_80031AD0` follower branch and is driven by the
retail CPU input block (AI type 6: record Popo's pads into a 30-slot ring, play back 5 frames
delayed, with follow/approach fallback). The packet has four owners: the character/article
sources, the fighter-axis widening of match/observation/compare state, the CPU input subsystem
slice, and follower-frame validation.

## Final boundary

- **Final owner (entities):** the imported `src/melee/pl/player.c` dual-entity machinery
  (`player_entity[2]`, `ftMapping_list[CKIND_POPONANA]`, `Player_8003248C` returning
  `Gm_PKind_Cpu` for the non-transforming second entity) becomes live. The runtime stops assuming
  fighters ≡ players: `MslCoreMatch` tracks per-player leader and follower fighters; every
  consumer that iterates fighters walks both entities of a slot.
- **Final owner (Nana's brain):** the retail CPU input path. `ftCo_800A2040` (headless stub
  `false` today) becomes the real predicate; the reachable AI slice — `ftCo_800B3900` tick chain,
  `ftCo_800B101C` type-6 think, `ftCo_800B0918`/`ftCo_800B0AF4` ring record/playback,
  `ftCo_800A589C` partner getter, command-execution helpers, and the `ftcpuattack.c` slice the
  type-6 graph reaches — is ported into the existing `src/runtime/ftco_0A01.c` projection or a
  full upstream import (decide at import time by measuring the reachable closure; upstream TU is
  NonMatching, so every ported function is verified against `refs/melee` asm individually).
- **Canonical state:** Nana's input state is `Fighter.x1A88` (`Fighter_x1A88_t`, 0x57C, already in
  `types.h`) exactly as retail — no hosted mirror, no synthesized controller lane. Inputs from the
  wire stay 4-wide per player; Nana never reads `HSD_PadGameStatus` and never touches the UCF pad
  ring (retail-natural: the AI branch skips the physical-pad block in
  `Fighter_Spaghetti_8006AD10`).
- **Consumers:** fighter creation/respawn (`scalar.c` bootstrap, `match.c` respawn), post-frame
  refresh, native DAT preload (already iterates entity 0..1), observation/output lanes, savestate
  copy, viewer live schema, and validation compare all address (player, entity∈{leader,follower}).
- **Displaced code/state:** the entity-0-only assumptions in `scalar.c:1074`/`:1804` (fighters[] =
  index 0, kept only for the Sheik/Zelda swap); the `ftCo_800A2040` false stub and the
  `ftCo_800B3900` abort row in `src/stubs/headless_exclusions.c`/`ledger.tsv`; validation's
  leader-only descent in `tools/validation/native.c:load_player`.
- **Deletion boundary:** no compatibility flag for "ICs without Nana", no synthesized Nana input
  lane in the wire format, no duplicate Nana state outside `Fighter`/`StaticPlayer`. The public
  input API stays `players[4]`; the observation/compare structs gain follower lanes in one cut
  (Python dtypes, native.c, wire.h move together).

## Sequencing (each step gates green before the next)

1. **Data plumbing:** `PlPp`/`PlNn` prefixes + `EfIcData.dat` in `tools/data/extract.py` and
   `raw.py`; re-extract from `./SSBM.iso`; manifest/test counts.
2. **Character packet (compile + spawn):** import 8 Matching chara TUs (`ftPopo/*.c` ×5,
   `ftNana/*.c` ×3) + 3 Matching article TUs (`itclimbersice/blizzard/string.c`), ftdata.c hosted
   registry rows, native DAT translation rows, `derive_gameplay_parts_mask.py` rows for both
   kinds, effect bank, char id 10 through core enums (public API admission deferred to step 5).
   `source_character_kind` maps id 10 → `CKIND_POPONANA` (first 1→2 expansion).
3. **Fighter-axis widening:** leader+follower fighter tracking in `MslCoreMatch`, respawn/death
   (`gm_80167320(slot, subchar)` already wired), UCF gate, savestate, observation lanes.
4. **CPU slice port:** un-stub `ftCo_800A2040`, port the type-6 reachable closure, asm-verify per
   function (NonMatching upstream).
5. **Validation + suite:** follower descent in `native.c` (peppi `ports.P{n}.follower`), follower
   compare lanes and life-cycle row carry (rows vanish while Nana is dead), public API/python/
   viewer admission, stage the icies suite (11 usable candidates in
   `~/SSBM/Replays/Samples/top14.zip`: FD/BF/YS/FoD/DL ×2 each; both PS games non-frozen,
   excluded), container aggregate gate.

## Evidence and acceptance

- Retail behaviors that must fall out of the port, not be re-implemented: Popo death kills Nana
  (`ftCo_800BFD9C`); belay partner tether (`checkNanaInRange`, rope item 113); squall sync
  (`ftNn_Init_80123954` kinematic slaving); Nana's percent-derived CPU level
  (`min(popo_percent/20, 9)`); Nana motion-table fallback to Popo (`ftData_80085FD4`).
- Acceptance: container aggregate stays 0 fail with all previously-passing replays byte-stable;
  icies suite validates with follower rows compared; full pytest green; no wire-format second cut.

## Log

- 2026-07-27 — `retained` (step 2)
  Scope: character packet committed as 5d0d3858 (11 TUs, registry rows, DAT translation, parts
  masks, extraction, effect bank 14, preload with Nana costume coverage).
  Evidence: native-smoke green including the Popo parts row; triage harness steps ICs-vs-Fox 600
  frames, Nana spawns as player_entity[1] and idles. One native delta (ledger
  nana-anim-leader-share-native): the ftData_80085CD8/E50 leader-share branch is excluded under
  MSL_CORE_NATIVE (x59C never materializes natively; x14 keys the same immutable subarchive).
  UCF finding: msl_ucf_apply_pad_buffer sits in the COMMON input path mirroring retail hook
  0x8006B460, so Nana running it is retail-faithful — no gate needed once the CPU predicate is
  real.
  Next: CPU TU import (ftCo_0A01.c + ftcpuattack.c wholesale, projection reconciliation,
  stub triage) — delegated; then fighter-axis lanes and follower validation.

- 2026-07-27 — `retained` (steps 3-5 infrastructure)
  Scope: CPU TU import committed (14de9dc2); follower compare lanes cut through
  wire.h/scalar/native.c in one step (MslCoreCompare 1022→1302, 28 follower lanes + presence);
  peppi follower descent with independent life-cycle row indexing (rows null during Nana's
  Sleep, resume at Popo's respawn — measured); ICs article registry rows fixed in items.c (NULL
  state-table segfault); public API/python/viewer admission; 12-replay icies suite staged.
  Evidence: container aggregate 183/63/0 under the new wire with all 247 output locks
  re-recorded; icies suite runs end-to-end (12/12, zero errors, four-climber ditto included),
  every entry diverging at frames 83-204 in follower lanes — the untuned Nana start-of-match AI
  decision stream.
  Next: Nana AI fidelity (retail probe of the x1A88 decision stream at the first divergence,
  fd-falco @-39 class), then classify/lock the suite and wire it into the aggregate.

- 2026-07-27 — `retained` (first Nana fidelity seed)
  Scope: session 2 traced the universal frames-83..204 follower divergence to the hosted UCF
  pad-buffer injection: it replaced Nana's AI stick with the port's cardinal-snapped raw pad and
  double-shifted the ring. The real gecko wraps its whole body in !Player_IsCPU, and Player_IsCPU
  IS ftCo_800A2040 (GALE01r2 map) — the predicate the CPU import made real.
  Evidence: with the one-line gate, container icies prefixes moved 83-204 → 95-1,416
  (medium-sheik: 21s of bit-exact two-climber play); aggregate unchanged (gate is vacuous for
  humans). Next layer, all classic per-mechanism families: itclimbersice SendItemInfo misc lanes
  (192/0 vs 64/255 @-27 bf-falco), 1-ULP Nana pos_x @98 fd-falco (fusion site), a crouch-mimic
  action pick @26 bf-falco.

- 2026-07-27 — `retained` (session 3: three source fixes, rollback boundary identified)
  Scope: (1) ftCo_800ADE48 upstream decomp had the blast-zone bounds branch INVERTED against
  GALE01 asm (800ADF50: beq skips the reset for found in-bounds targets; the C reset them) —
  every valid CPU navigation target was clobbered to self-floor, which kept Nana inert during
  the entry fall (retail general-CPU steers her toward Popo's floor position with
  LstickXTowardDestination 0x7F from frame -123 until mimic engages at landing). The sibling
  bounds-check sites (A3908 ×3, A4038 ×3, A648C, B33B0, A21FC, A8210, AE7AC, inlineD0s) were
  swept against asm and are faithful. (2) ftCo_800B0AF4's x2225_b3 mimic position lerp is a PPC
  fmadd (0.05*ring rounded once, then fused 0.95*cur + t before frsp); the C's two rounded
  products drifted 1 ulp — the whole follower_pos_x 1-ULP family. Fixed with __builtin_fma per
  the lbcollision.c precedent. (3) item_var_source_byte gained native pointer-widening cases for
  It_Kind_IceClimber_Ice (offset 3 = owner GObj low byte, 7 = live scale f32) and GumStrings
  (7/0x17 = link/joint pointer low bytes); 0x1B and ice 0x17 are pool residue.
  Evidence: container icies first-mismatch fronts moved from -39/-27-class to per-replay
  engage-adjacent fronts; e.g. ys-fox prefix 261→4,258, gm-peach first -39→378 (item.vel ULP),
  medium-sheik 1,416→316 (engage-front reshuffle). Aggregate re-verified vacuous.
  Boundary finding: the residual follower fronts are one-frame stick skews at mimic
  engage/disengage boundaries where the recorded Nana pre-joystick is a ring slot one NEWER or
  one OLDER than the (asm-verified) cursor arithmetic can produce from the finalized Popo rows
  (bf-marth @12 = popo@7 under steady d6; ics-ditto @187 = a -125 flick-transit sample where the
  straight-line ring holds -128; both directions occur). Every candidate mechanism (ring cursor
  code, B101C/B2AFC/B3900 call orders, UCF gecko rewrite timing vs the 0x8006B0E0 record hook,
  A17E4 quantizer) was verified against asm/refs and eliminated: the recordings are netplay
  rollback captures, and Nana's mimic ring is the first supported state whose content depends on
  input ARRIVAL timing, which straight-line re-simulation of finalized rows cannot reproduce.
  Disposition: treat engage-boundary skews as per-replay classifications (rollback-recording
  owner) unless a Dolphin playback probe shows Slippi playback itself reproduces the recorded
  follower rows; remaining fixable fronts are the ice-block item-motion ULP family (gm-peach
  @378) and the fod-sheik @297 item stream.

- 2026-07-27 — `retained` (session 3 continued: UCF follower retro-write, probe loop established)
  Scope: the bf-marth @12 engage-front was NOT a rollback artifact. A new Dolphin interpreter
  probe (slippi-dolphin engine-dump branch, MSL_NANA_RING_PROBE_*: logs the B0918 capture call
  and the B0AF4 playback exit with ring cursors and slot content) showed the slot recorded at
  slippi 6 with the -86 flick-transit sample being read back at slippi 12 as -128: UCF's
  dashback gecko (refs/ucf/src/dashback/dashback.cpp, Interrupt_AS_Turn+0x4C) retroactively
  rewrites the paired sub-character's freshest mimic-ring slot (full-deflection stick + new
  facing) when the leader's dashback converts. Ported into msl_ucf_apply_dashback (8a191e85);
  bf-marth prefix 134→179. Probe frame anchor: probe/dump frame N = slippi N; sim tick trace
  t = slippi + 132; ring capture is one frame stale (prio-2 think before prio-3 input
  processing), steady playback delay is 6 frames.
  Boundary of the next front (ys-fox @14, medium-sheik @194, bf-falco @71 class): retail Nana
  sits DISENGAGED in general-CPU state x18=10 emitting stick 0 while Popo airdodges (probe
  shows xFA mim bit clear, x18=10 constant); our sim demotes x18 10→1 at the frame Popo's
  stick returns to neutral and then walks toward the surviving x54 target (ftCo_800ACD5C's
  early branches differ). Islands are live natively (probe: non-NULL island, A2718=0) and x54
  correctly tracks Popo's airdodge floor in both. Suspect surface: the scene-keyed hosted
  mirrors consulted by ACD5C/ADE48 — gm_8016C75C standings-cache value at match start (x88/x8C
  timer arming) and gm_801A4310 major-scene id vs 0x1C — plus ftCo_800A21FC/A3554 gates.
  Next: probe gm_8016C75C/gm_801A4310 returns retail-side during the ys-fox window, align the
  hosted mirrors, then reassess which residual fronts remain for classification (item misc0
  pointer bytes, misc2/3 pool residue, item-motion ULP family per the dolphin-profile
  precedent).

- 2026-07-27 — `retained` (session 3 continued: CPU size-field publication)
  Scope: the state-10 front's root cause was NOT the scene mirrors: the retail probe showed
  ftCo_800A3554 passing with x38 ~= 8.9 (breathing per frame) against our 2.0. x38 comes from
  the partner's x1A88.x564 hurt-capsule half-span, published every frame by ftCo_800A0DA4 in
  retail; the hosted build had substituted the dynamics-only capsule publication and never
  wrote x55C/x560/x564/x568 (dead state before Nana). Fixed in 59b0af15: run the full retail
  publication whenever a CPU-input fighter exists (msl_fighter_cpu_input_live), keep the cheap
  path for human-only matches. Container icies prefixes moved dramatically: ys-fox 269 -> 5,694,
  bf-marth 179 -> 6,489, auto-falco 256 -> 5,731, medium-sheik 316 -> 2,643, ics-ditto
  309 -> 2,269; master-fox unchanged (@147 damage-exit family).
  Tooling note (user-reported): the extracted probe driver's `[DSP] Backend = NullSound` is an
  invalid backend name in the engine-dump Dolphin and silently falls back to the audible
  default; the correct silent config is `Backend = No Audio Output` + `Muted = True` +
  `Volume = 0` (fixed in the working copy; apply when tools/dolphin merges to a branch).

- 2026-07-27 — `retained` (session 3 finale: PlCo CPU table translation, 76c44b50)
  Scope: the B4AB0 instrument revealed the true owner of every remaining behavioral front:
  Fighter_804D64FC_t declares its table members void**, so the DWARF-driven native layout
  emitted POINTER->POINTER->VOID and the CPU attack tables/thresholds/scripts were copied as
  raw big-endian bytes (probed entry cmd=0x02000000 = swapped 2). No CPU-driven fighter could
  ever select an attack. Hand translator (native_dat.c translate_fighter_cpu_tables): per-kind
  [33] pointer arrays of cmd-terminated 0x24-byte attack entries, float[33] thresholds,
  float[6] weapon reach, and byte command scripts indexed by attack command id (count 64 — 39
  segfaulted via xA4 ids 0x28/0x29; past-array slots read non-relocated -> NULL).
  Evidence: prefixes gm-peach 747->9,966 (96% of the replay bit-exact), dl-fox 360->5,086,
  bf-falco->4,798, master-fox 269->2,434, fod-sheik->3,333, ics-ditto->3,021; aggregate
  183/63/0 unchanged. Remaining first-mismatch fronts are now dominated by the unrecordable
  item.misc0/2/3 pointer/pool-residue bytes at each ice-block spawn (classification class),
  plus fd-falco @346 (follower CliffWait pick) and the gm-peach @378 item-motion ULP family.

- 2026-07-27 — `retained` (session 3 coda: live standings, d2ab8923)
  Scope: the fd-falco @346 front (follower CliffWait pick) probe showed retail's
  gm_8016C75C returning the UPDATED KO count (1) at Nana's first post-KO state-10 tick with
  a dpad-up press (held=8) from ftCo_800ACD5C's x88 branch: retail's x24C scratch is dirtied
  between reads, so the observable behavior is a live standings recompute, not the frozen
  first-read snapshot the hosted mirror served. Made gm_8016C75C recompute per call.
  Evidence: bf-falco prefix 4,798->9,176 (95%), fd-falco 468->7,979 (95%), master-fox
  2,434->5,948, fod-sheik->4,086, ics-ditto->3,825, medium-sheik->3,962; aggregate 183/63/0.
  Note: MslCoreMatchRules.cpu_standings_snapshot_valid is now dead state (cleanup with the
  next wire/struct touch).
  Current fronts after d2ab8923 (prefixes 302-9,966): (a) item.misc0/2/3 pointer + pool
  residue lanes lead 9 of 12 replays (classification class); (b) micro AI-stick episodes:
  ics-ditto @1068 = state-4 recovery DI steering quantization phase (retail alternates
  -88/-127 with +88 up per the AA320 Nana stick=0x40 increments; ours slightly offset —
  suspect an upstream 1-ULP or an x570 draw), medium-sheik @210 = one-frame jump-decision
  skew (Landing vs KneeBend), ys-fox @5572 = ice-block scale low-byte drift (misc1 32 vs 72,
  ULP-class); (c) gm-peach @378 item-motion ULP family. The behavioral tail is now a handful
  of one-frame/1-ULP episodes per replay; classification authoring for (a)+(c) plus episode
  attribution for (b) is the remaining path to flipping the suite to CLASSIFIED.

- 2026-07-27 — `open` (gm-peach @3986 attributed to a disengaged general-CPU dash pick)
  Probe: retail Nana disengages at 3983 (xFA mim bit clear; ring slot +127 IGNORED) and the
  general CPU emits a full -127 stick at 3986 -> dash-left at 3987 (ground -1.4); ours emits
  a partial steering stick (~-45, -0.3545 momentum) -> walk. Same class as ics-ditto @1068
  and medium-sheik @210: the disengaged-CPU state/script pick differs for one frame. These
  three episodes (plus ys-fox @5572 scale-byte ULP and the item misc/motion classification
  classes) are the entire remaining icies distance. Next iteration: our-side x18/script trace
  at gm-peach 3975-3990 vs the retail x18 evolution (extend the ring probe with the ADE48
  cascade returns already wired for ACD5C) to find which state handler picks the full-stick
  dash command in retail.

- 2026-07-27 — `open` (gm-peach @3986 refined: identical stick, divergent landing physics)
  Follow-up traces (our tick + AB224 branch vs the retail ring probe) show BOTH sims emit the
  full -127 at slippi 3986 in state 1 (same-island AA42C walker) and dash at 3987; published
  lanes match through 3985. The @3986 delta (air self -0.3895 vs -0.3545 = one accel
  quantum; pos 0.035) therefore arises INSIDE frame 3986's Landing-state update with
  identical inputs — an unpublished state bit or IASA-order detail. Blocked on visibility:
  the engine dump carries only leader rows (port_count=2). Next tooling step: add follower
  fighter rows to the engine-dump writer (slippi-dolphin engine-dump branch) or an
  interpreter probe over ftCo_Landing IASA/physics for Nana at 3985-3987, then diff
  self-vel/decel inputs. gm-peach anchors: our tick t = slippi + 128 for the FIRST Nana
  lifetime; x7C resets on her respawn (filter traces by position, not t, across lifetimes).

- 2026-07-27 — `retained` (cascade-input bit-verification; episode isolated to script cadence)
  A paired probe of the ADE48 cascade's ftCo_800A3554(fp, 0) call (new kCascadeSafeCall PC
  0x800AE768 retail-side; msl_casc_trace sim-side) shows at gm-peach tick 3987 the AI inputs
  are BIT-IDENTICAL across sims: x54x 0x41321AB4, and x38 0x40C9B274 — i.e. the hosted
  hurt-capsule extents publication (59b0af15) is bit-exact against retail, and targets/x60
  match. The only difference is the post-3986 position itself: retail's walk script fired at
  tick 3986 (ring probe: ls -127 set during 3986 -> dash during 3986), ours at 3987. With
  identical state at every sampled tick, the残 divergence is the EXPIRY TICK of the recovery
  script chain built during her damage/air phase (csP/command_duration cadence) — one more
  probe layer: log ftCo_800B49F4 script-finalize ticks + durations on both sides across the
  recovery (~3950-3987) and find the first build tick that differs, then asm-diff that
  handler. All three residual behavioral episodes are this same cadence class.

- 2026-07-27 — `retained` (script-cadence layer probed; episodes converge on A2C80 recovery entry)
  The kScriptFinalize probe (0x800B49F4, logs per-build frame/x18/buffer bytes) shows retail
  building a state-4 recovery script EVERY air frame 3945-3982, demoting to x18=1 at the 3983
  landing, and building the state-1 AA42C walk script at 3984: SetLstickY 0;
  LstickXTowardDestination 0; WaitFor 2; LstickXTowardDestination 0x7F; Done - the WaitFor 2
  lands the full -127 exactly at 3986. Ours builds the same script one frame later (3985)
  because our x18 was 10 (not 4) through the recovery: our sim never entered the A2C80
  recovery state for THIS knockback while retail did (the dl-fox episode was the same gate
  agreeing; here it disagrees). All three residual behavioral episodes therefore converge on
  ftCo_800A2C80's per-knockback evaluation during the air phase - its inputs are the
  xFA_b5 in-bounds flag (B33B0's own bounds chain over the floor probe), the fall-angle
  lb_8000D008 gate, and the long recovery ray. Next: probe A2C80's ENTRY inputs (pos_delta,
  xFA_b5, the ray result) retail-side across the air phase (kA2c80Entry already logs dx/dy/
  xfa; add the mpCheckFloor ray result PC) and mirror sim-side; the first differing input
  identifies the owner (suspect: xFA_b5 phase, since B33B0's bounds chain runs every tick
  and its in_bounds -> xFA_b5 store was asm-verified but its mpCheckFloor probe inputs
  depend on cur_pos while airborne).

- 2026-07-27 — `retained` (A2C80 exonerated at the gm-peach onset; divergence is a state-4 exit)
  Paired A2C80 gate traces at the knockback onset (retail kA2c80Entry/kA2c80Ret vs sim-side
  msl_a2c80_trace): BOTH sims enter recovery state 4 at the same tick (slippi 3925, our
  t=979; angle bf8be887 passes, xFA_b5 clear, floor ray finds nothing, ret 1). The earlier
  'ours never entered 4' reading was wrong. Retail stays x18=4 through 3982 (script probe);
  ours is x18=10 by t=1038 (3984): our sim EXITS state 4 somewhere in 3925-3983 mid-air.
  Next (first action of the continuation): sample our x18 per tick across t=979-1042 (the
  tick trace prints it; one grep), find the exit tick, then diff the state-4 handlers'
  gates at that tick — case-4 of ftCo_800B2790 (grounded demote / ftCo_800A9904 air) and
  ftCo_800B24B8 (case 4 of B2AFC) against the retail probes. The one-frame walk-script
  cadence at landing (and hence the entire episode class) follows from whichever gate
  flips ours out of 4 early.

- 2026-07-27 — `retained` (the episode split is the landing-tick state demote: 4->1 vs 4->10)
  Definitive lifetime-2 timeline (gm-peach, NANA-tick t = slippi - 2945; beware TWO Nana
  lifetimes sharing t ranges — filter by position): both sims enter state 4 at slippi 3925
  with bit-matching gate inputs (angle/dx sequences identical), both steer -84 through 3982,
  both land during 3983. At tick 3983 retail demotes 4->1 (ftCo_800B2790 case-4 grounded
  path: x18 = x1C, Done) and builds the AA42C walk script at 3984; OURS lands in x18=10 at
  the same tick, spends 3984 in ACD5C (stick 0), demotes to 1 at 3985, and builds the walk
  script one frame late — the entire one-frame cadence divergence. The ONLY x18 = x20(=10)
  writer is the ADE48 cascade hop guarded by ftCo_800A3554(fp, 0), which cannot pass while
  airborne at the think; so either the hop fired against expectation (trace it: one
  msl-trace at the cascade hop and at the case-4 grounded demote, logging tick + which path)
  or an unaudited writer exists. FIRST ACTION NEXT SESSION: instrument those two sites, run
  gm-peach --frames 4115, read the single line that says which path wrote 10 at t=1038, then
  asm-diff that path. Everything else (episodes, classifications, locks, wiring) hangs off
  this one answer.

- 2026-07-27 — `retained` (the write is the cascade hop; retail never reaches it at 3983)
  The x18-write trace answered the pinned question: OUR 4->10 at the landing tick is the
  ADE48 cascade final transition (A3554(fp,0) passing because ga publishes Ground at the
  think while motion still lags in Fall, and the ADE48 head reset x54 to her own position,
  making dist 0). The retail cascade probe shows NO A3554(0) call at 3983 — its first CALL
  is 3984 — so retail's ADE48 exits BEFORE the transition cascade at exactly that tick,
  while ours falls through. Remaining search space is tiny: ADE48's pre-transition early
  exits at one tick — the x18==0x12 return, the x221A_b3 hitlag switch_cmd path
  (B4A78 + x18=0x12), and the motion-keyed 0x125/0x154 -> x11/x13 branches. NEXT: either
  sim-side log which ADE48 exit path runs at the landing tick for both x18 histories, or a
  retail PC probe over ADE48's early-return addresses at 3983. Note the ga-vs-motion phase
  at landing think (ga=Ground from the prio-6 map pass of the PREVIOUS frame while
  motion_id still Fall) is what arms A3554 grounded gates one tick before the Landing
  state exists — REFINED by direct predicate probes (kB4ab0Entry + cascade
  predicate returns): at the landing ticks retail calls NONE of A5ACC/B9CBC/B8A9C/B732C/
  A3710/B4AB0 (their first calls are 3984, all returning 0), so retail's ADE48 exits BEFORE
  the whole predicate block while ours reaches the final transition and hops. Also note the
  NANA tick-trace anchor correction: t-row(F) prints post@(F-1) motion, so our landing motion
  (42) appears at post@3982 — SAME frame as retail; the earlier one-frame-landing readings
  were anchor artifacts. The remaining search space is the ADE48 region between its head and
  the bl ftCo_800A5ACC (asm 0x800AE460-0x800AE568: the x18==0x12 return, the x221A_b3
  switch_cmd block, and the motion-keyed 0x125/0x154 -> 0x11/0x13 and 0xBF/0xB7 -> 5,
  0xFC/0xFD -> 6 sets): add a PC-trace over that range (MSL_PROBE_PC_TRACE with PC_START/
  PC_END already exists!) for one retail tick and mirror the taken branch sim-side. It is
  the last unexplained divergence gate. NOT ftCo_800B8A9C returning TRUE (x18 -> 2, returning before the final transition — matching
  the missing CALL row) with B2790's case-2 handler demoting 2 -> 1 the SAME tick (matching
  retail's end-of-tick x18=1 and the 3984 walk script). Ours' B8A9C returns FALSE at that
  tick. So the last mile is ftCo_800B4AB0's per-entry evaluation (or B8A9C's target-state
  gates A3134/A3200 on Peach) at ONE tick with known inputs — instrument the entry loop
  (which entries pass the level/cmd gates, the predicted relx/relPredY per entry, and the
  final selection) sim-side, and the same via an interpreter probe on B4AB0's entry/return
  retail-side. Suspects: the f64-mixed prediction arithmetic (lines ~161-213, fmadd-class),
  or a subtle table-translation field. This single comparison closes the episode class.

- 2026-07-27 — `retained` (BREAKTHROUGH: cascade repair landed; first replay CLASSIFIED)
  The compensator was a FOURTH upstream decomp drop: ftCo_800B2790's case 4 (and case 19)
  read special_floor UNINITIALIZED where the asm carries a zeroed register — the garbage
  routed retail-faithful landings into ftCo_800A0148's blast-zone escape script, masking the
  missing cascade exit. Landing all three repairs together (9bc1b259: the ADE48 x18==4
  cascade exit + both zero-inits) restored the retail landing sequence and moved the suite
  to matched rows 4,215-14,150: ys-fox 8,140/8,152, master-fox 8,543/8,610, gm-peach
  10,299/10,401, medium-marth 14,150/14,709. ys-fox's entire residue = 12 rows of belay
  string item.misc1/misc2 = Slippi's raw bytes over retail heap ItemLink/JObj pointer
  identities — unrecordable in principle — and is now CLASSIFIED (0a20c832,
  id unrecorded-item-pointer-identity, full native snapshot; note the manifest was
  re-serialized with indent=1, formatting churn only). SUITE: 0 pass / 1 CLASSIFIED /
  11 fail; aggregate 183/63/0 verified after both commits.
  Remaining: the other 11 replays now follow the same endgame — small residues per replay
  (master-fox 67 rows, gm-peach 102) to attribute as fix-or-classify, then locks and
  aggregate wiring.

- 2026-07-27 — `open` (ADE48 cascade truth table complete; the x18==4 exit contradiction)
  Full asm branch-skeleton extraction (0x800AE280-0x800AE7A8, saved recipe in-session): EVERY
  state guard in the transition cascade exits the function when x18 already equals it
  (fifteen exits; guard 7's exit also calls ftCo_800BB9B4 first). The C's PYRAMID NESTING
  actually implements fourteen of them (each `if (x18 != N) {` nests the whole remainder and
  the closing braces fall to returns) — the ONLY non-nesting guard is x18 != 4 at the
  ftCo_800A2C80 site: when x18==4 the C uniquely falls through into the 0xF+ guards. That is
  the single genuinely dropped exit. CONTRADICTION TO RESOLVE: adding the asm-faithful
  `else return` REGRESSED the suite (gm-peach matched rows 9,966 -> 4,077 — note the FAIL
  column is total matched rows, not prefix), which should be impossible against a retail
  recording unless (a) a compensating infidelity elsewhere currently cancels the fall-through
  bug (find it by tracing the with-fix landing sequence: ours should now build the walk
  script at 3984 and dash at 3986 exactly like the retail probe — verify, then chase where
  the tail diverges instead), WITH-FIX TRACE RESULT (the compensator, partially unmasked): with the
  else-return applied, the landing tick builds a TEN-frame WaitFor script (csp=1 dur=10) and
  x18 HOLDS 4 through 1038-1047 — the B2790 case-4 grounded demote never fires even though
  the A3554-based analysis proved ga=Ground at that think. Suspects: the script was built by
  something reached before case-4 (ftCo_800A8DE4_noinline preamble? an ACD5C-shaped WaitFor
  0xA script implies a case-10 dispatch — check whether x18 was still 10 from the previous
  frame's structures), or the csP gate sequencing. DISPATCH-TRACE RESULT (fix + case-id/ga logged): the landing tick DOES
  dispatch case 4 with ga=Ground (t=1037, the demote path executes), but NO dispatch occurs
  for at least the next 8 ticks — the post-demote script gate (csP/command_duration) stays
  blocked even though ftCo_800B3E04's Done handler (csP=NULL, dur=0) is verified correct in
  isolation and ftCo_800B49F4 runs at B2790's tail. Retail's script probe shows builds
  CONTINUING (3984+). NEXT: one run with csP/dur logged at B3E04 entry/exit and B49F4 for
  the landing tick — the blocked gate is the last link; beware the two-Nana-lifetime t-space
  collision (filter by position) and that DISP prints only when the gate passes.
  Hypothesis (b) ELIMINATED: no UCF or slippi-ssbm-asm injection touches 0x800ADE48-0x800AE7AC, so the recording's ADE48 is vanilla and (a) — a compensating infidelity that currently cancels the missing exit — is the operative theory; the with-fix landing-sequence trace comparison is the way in. The reverted change is
  one `else { return; }` at the A2C80 guard — trivial to re-apply once (a)/(b) is resolved.

- 2026-07-27 — `open` (the x18==4 cascade exit: asm-real, but the naive port regresses)
  PC-coverage trace (generic MSL_PROBE_PC_TRACE over 0x800AE0F0-0x800AE568 at the landing
  tick) shows retail's ADE48 executing: motion 0xFC/0xFD checks, x18==9 guard, IsGrabbing,
  then `lwz x18; cmpwi 4; beq .L_800AE484 -> li r3,1; b .L_800AE790` = FUNCTION EPILOGUE:
  when already in recovery state 4 the transition cascade EXITS (the asm function returns a
  transitioned flag the void C drops). The upstream C falls through — a second control-flow
  drop in this TU. HOWEVER the one-line `else return` port empirically REGRESSED the suite
  (gm-peach's landing episode grew into leader-lane divergence from 4100; master-fox/
  ics-ditto/bf-falco prefixes dropped) and was reverted (uncommitted). Hypotheses for the
  regression: the exit's r3=1 gates caller behavior not modeled by the void C; or the other
  state guards (9/5/6/...) have similar unported exits so restoring only the 4-exit skews
  the state machine asymmetrically; or downstream within-frame RNG draw-order compensations.
  NEXT SESSION: audit the ENTIRE ADE48 transition block against asm as a unit (the same
  PC-coverage probe run at a few ticks in different states gives the ground truth per
  guard), port ALL its exits together, and re-verify. The committed tree (117a2053 state)
  remains the best-verified point: prefixes 302-9,966.

- 2026-07-27 — `open` (dl-fox @238 root-cause narrowed to ftCo_800A2C80's long recovery ray)
  Scope: with the tick trace aligned (our t = slippi + 133 on dl-fox), our Nana's x18 path
  through her damage (~f210-236) is 1 -> 4 -> 10 -> 1 while retail lands in x18=2 (attack).
  Both sims run the ADE48 transition cascade (x221A_b3 hitlag arming verified); the split is
  ftCo_800A2C80 ("falling toward doom" recovery check, NOT in the earlier bounds sweep):
  ours returns 1 (-> state 4 recover) where retail returns 0 and falls through to
  ftCo_800B8A9C (attack tables ARE populated natively — probed non-NULL). A2C80 casts a
  1000-unit ray from the ECB bottom along the normalized fall direction through mpCheckFloor
  and returns 0 when it hits an in-bounds floor. REFUTED by a follow-up probe (kA2c80Entry/kA2c80Ret added
  to the Nana ring probe): retail A2C80 ALSO returns 1 at f210 — both sims enter recovery
  state 4 identically, and hosted mpCheckFloor is the verbatim retail loop (mpBoundingCheck2
  broadphase, no O1 shortcut). The real split is AFTER landing (~f223): both demote out of 4
  and re-run the ADE48 cascade; retail's ftCo_800B8A9C ground path returns TRUE (x18=2,
  attack) while ours always returns false. Gates verified equal (xF9_b2 set, target present);
  the per-kind attack table pointer ((void**)Fighter_804D64FC->x4)[FTKIND_NANA] is translated
  and non-NULL natively (probed host pointers). NEXT INSTRUMENT: ftCo_800B4AB0 (the CPU
  attack-script evaluator) — log its parse walk and result natively vs a retail interpreter
  probe at its call sites; suspects are the attack-script PAYLOAD translation (per-entry
  structures behind the per-kind table read via retail byte offsets would break under native
  widening) and its internal ftcpuattack range math reading target x55C spans. This family
  plausibly owns the remaining behavioral fronts (dl-fox @238 walk-vs-dash after the missed
  attack, ys-fox @5572+, medium-sheik @194 Landing-vs-Turn = a missed script jump,
  master-fox @147 damage-exit).
  Scope: retail ring probe on dl-fox frames 225-242 (dl_ring2.jsonl recipe): retail Nana is
  DISENGAGED (xFA mim bit clear) in general-CPU x18=2 from at least 225 through 242, in Wait
  with friction slide (-0.3498) while Popo dashes; the UCF retro-write slot (-128) is present
  in her ring but ignored. Ours re-engages/dashes at ~237 (ground -1.4). x7C phase MATCHES
  retail (x7C = slippi + 132 in both, so the %-gate phases are aligned). gm_8016C75C mirror
  re-verified faithful (one zero-KO snapshot at first read arms nothing). The divergence is in
  internal x1A88 state evolution through Nana's damage reaction (~frames 210-235): retail
  lands in x18=2 (escape) and stays; our x18 path unknown — needs the our-side tick trace
  (rebuild container with the ftCo_800B3900 tail trace from msl-icies-port-state) compared
  frame-by-frame, then an asm diff of the diverging handler. Candidate handlers: the damage
  reaction chain (ftCo_800B4A78 -> x18=0x12 -> ftCo_800AC5A0 case 18 -> demotes), case 2
  ftCo_800B04DC. Note B0E98 re-engage requires pos_delta diff below co_attrs.mid_walk_point,
  which should REJECT engage while Popo dashes (delta gap ~1.05) — whichever sim engaged there
  is the wrong one, and ours engaged.
  Evidence: dual-entity player machinery already compiled in and inert; CPU predicate stubbed
  false; validation reads only `ports.P{n}.leader`; UCF pad ring would double-shift if Nana took
  the pad path. Nana's AI is type 6 (mimic ring + follow), NOT the general CPU tree; reachable
  slice bounded inside NonMatching `ftCo_0A01.c` + `ftcpuattack.c`.
  Disposition: proceed with sequencing above; step-2 import is mechanical (all 11 chara/article
  TUs are Matching upstream).

- 2026-07-28 — `retained` (FOD CONSTRUCTION RNG FIX, commit 4fa63d66; probes 3325a6f218)
  The fod-sheik/auto-falco follower fronts traced to Nana's x1A88.x7C AI phase counter:
  its init value is (int)(10*HSD_Randf()) drawn at fighter creation, and our FoD
  construction stream sat two draws EARLY because the hosted grIzumi platform proc
  early-returns to the Slippi height stream and never consumed retail's two creation-tick
  HSD_Randi draws (grIzumi_801CC358 case 0 via rand_range; case 3 for below-ground
  starts). Nana drew x7C=7 where retail drew 0; every x7C%N gate (x56C %600 follow-radius
  redraw, x570 %30, DI %120) fired 7 ticks early, sampling different stream positions.
  Fix: msl_grizumi_consume_replay_creation_draw at match construction post-OnInit, gated
  on msl_slippi_fod_platform_height owning the creation publication; do NOT touch
  xC4/xC6 (arming them at creation broke all 27 human Fountain aggregate replays through
  mistimed platform self-motion — the machine still runs event-quiescent frames).
  VERIFIED per-stage construction draw counts vs retail (Randf probe + first-A101C
  chain distance): BF 1=1, DL 2=2, YS 1=1, FD 4=4 (grLast port), PS 1=1, FoD 2=2 after
  fix. Retail x7C inits per replay are now reproduced exactly (fod: Popo 7, Nana 0,
  Sheik 4, Zelda 0). fod-sheik 4,215 -> 5,077 matched; aggregate 183/63/0 green.
  KEY RECIPES: (a) HSD_Randi INLINES the LCG (0x80380588) — a Randf-entry probe never
  sees Randi draws; infer via chain distance. (b) CPUCore=0 in the probe user-dir
  Dolphin.ini interprets the whole boot so construction draws (frame counter -124) hit
  the interpreter probes without the EXI window. (c) Slippi ONLINE per-frame seeds are
  synthesized (base + frame<<16) — construction draw-count divergence is erased per
  frame, EXCEPT values that persist (A101C x7C, B9704 x34).
  NEXT FRONT (fod-sheik @1248): follower_speed -0.725372 vs -0.726160 while mimicking
  (stick 125 walking), NOT a /127 byte value on our side — ring/clamp interplay again;
  ring probe run at frames 1240-1256 pending analysis.

- 2026-07-28 — `retained` (UCF DASHBACK OVER-FIRE FIX, commit after 4fa63d66)
  The fod-sheik @1248 mimic front: retail ring probe showed the slot playing at x7C=1371
  held 125 = trunc(127 x 0.986) natural capture while ours held a retro-written 127. The
  gecko replaces the facing store at Interrupt_AS_Turn+0x4C, which lives INSIDE the
  if (!has_turned) branch of ftCo_Turn_IASA; our hook ran unconditionally after the if,
  so already-turned smash turns (has_turned set at frame 1) still converted at anim
  frame 2 (87/88 fires in fod-sheik were spurious). Moving the call inside the branch:
  fod-sheik 5,077 -> 9,724, medium-sheik -> 14,279, ics-ditto -> 11,693, fd-falco ->
  8,117, bf-marth -> 6,993, auto-falco -> 6,654; aggregate 183/63/0. DIAGNOSIS RECIPE:
  the DASHBACK-FIRE env trace (fire-time anim/has_turned/x670/raws) + the ring probe's
  natural-vs-retro slot values pinpointed the condition delta in minutes.

- 2026-07-28 — `retained` (BLIZZARD SPAWN FMADDS, commit 5023a036) + `open` (dl-fox @5082
  = Fox TAIL dynamics). The dl-fox item vel/pos ULP family (4476+) was the blizzard puff
  spawn angle: GALE01 0x802C2290 fuses (x10-xC)*rand+xC; __fmadds port retired the family
  on four replays. The remaining dl-fox fork @5082: Popo usmash frame 9 hits in retail,
  whiffs in ours. Collision probe (dl_coll_probe3.jsonl in job tmp 164f8743) + sim-side
  HB/HU dump (scalar.c MSL_HB_TRACE, container-only): our usmash HITBOX x4C bits are
  RETAIL-IDENTICAL (incl the z=±8.35 swing arc — verified byte-exact at the 981 usmash
  too), and 12 of Fox's 13 hurt capsules match retail x/y bits exactly. The 13th — bone_idx
  18, offsets (0,0,±0.8), scale 1.62 = FOX'S TAIL, a dynamics-driven chain — sits at
  (49.26, 5.97) in ours vs retail (50.62, 7.97, -0.65): 2.4 units of accumulated dynamics
  divergence, and retail's hit lands exactly on it (coll_distance +0.319). NEXT: the tail
  dynamics chain (lb dynamics springs + Whispy wind coupling on DL + lb_800115F4 emitter
  phase); the marios sessions' dynamics/quat toolkit applies. NOTE the retail probe
  numbering: this window's collision-probe frames aligned 1:1 with validator frames.
  TOOLING (container-only, uncommitted): MSL_ICE_TRACE_* (per-frame item kind/vel/pos
  bits at export) and MSL_HB_TRACE_* (fighter hit capsules x4C/x58 + hurt capsule a/b
  positions at export) in /work/src/runtime/scalar.c; validate_replay.py detail rows
  widened to 14 in the container copy.

- 2026-07-28 — `open` (WHISPY WIND MACHINE — dl-fox 5,086 -> 10,818, UNCOMMITTED: aggregate
  gate blocked). Root cause of the dl-fox tail divergence: hosted grOldPupupu_802113E0
  SHORT-CIRCUITED Whispy's whole machine to the replay xDC direction (the FoD-platform
  lesson one level up), so the blow loop's wind force emitters (lb_80011A50 every 10
  ticks while blowing, first at f~754 on dl-fox) never spawned and NO fighter tail/body
  dynamics chain ever felt wind. Sim-side proof: msl_emitter_trace (container-only, in
  lbspdisplay.c lb_80011A50/lb_800119DC + scalar.c helper) logged ZERO emitters pre-fix,
  243 post-fix. Fix in working tree (Mac+container, uncommitted): delete the hosted
  early-return; the retail machine runs (its rand_range draws are prio-4 post-think =
  reseed-erased; DL construction counts already matched), and apply_replay_stage_events
  still republishes recorded xDC pre-frame for wind-push consumers.
  RESULTS: dl-fox 10,818/11,083 (the 5082 tail usmash now connects); icies otherwise
  unchanged. AGGREGATE: 164/54/28 — 22/28 fails are LOCK-DRIFT ONLY (mismatch_rows=0,
  tails are in the locked output; locks need re-recording once green) + 6 REAL row fails
  (puff Game_20260602 8,613 rows; luigi medium-fox 2,022; marth Goshawk 1,766 +
  QuestionableHarmfulPanther 74; marios plat-marth 72; marios medium-fox-2025-12 ONE
  row) — knife-edge marginal outcomes flipped because wind-blown dynamics are close but
  not bit-exact yet.
  THE REMAINING ULP CLASS (pre-wind, reproducible): dl-fox tail bit-exact through frame
  46, then Fox's hit/hitlag (47-50, both sims' DD94 correctly skip — cadence verified
  identical via MSL_DD94_TRACE vs the retail MaybeCaptureTailProbe rows) — capsules
  bit-equal again at 51 — then ONE dynamics tick at 52 forks 0.05 (~100 ULPs). Suspect:
  the hitlag/damage SHAKE displacement feeding the dynamics origins/colliders at the
  resume tick (hosted may skip the model vibrate as visual-only), or the colliders
  (&fp->x1670, ftColl_8007AF60) / x2228_b1 arg at 52. NEXT: dump solver inputs at
  validator 51-52 both sides (parent_mtx origin, colliders, arg1, ret_B0) — the
  MaybeCaptureTailProbe (slippi-dolphin, PC 0x8009DD94 entry, port==1 1-BASED) plus a
  matching sim trace. Transients DAMP when quiet (exact at 200-210 & 38-46; 0.61@400
  after shine, 0.23@600) — fix the per-event seed, chaos disappears.
  TOOLING NOTES: retail probe rows appear only when DD94 runs (hitlag gaps are DATA);
  probe frame == validator frame here; our export HB/HU rows are end-of-frame; sim
  frame_id in traces = validator frame - 1 (DD94 trace f=35..45 == validator 36..46).
  Retail per-frame tail baselines saved: job tmp 164f8743/tail_*.jsonl (windows -20..-10,
  38..62, 60..70, 200..210, 400..410, 600..610) + our_tail.txt (post-whispy-fix).
  A giant interpreter window (-123..990) STALLS playback at -123 — sample 10-frame
  windows instead.

- 2026-07-28 — `open` (tail fork: SOLVER INPUTS EXONERATED). The dl-fox validator-52
  one-tick fork: retail MaybeCaptureTailProbe (extended: arg1/ncol/collider dump,
  slippi-dolphin committed) vs sim DD94IN trace shows arg1 (x2228_b1, constant 0) and
  the x1670 collider list (ncol=1, world pos + radius) BIT-IDENTICAL at every tick 44-56
  including the fork. The divergence is inside the tail chain state itself. CAVEAT: the
  capsule comparisons carry a sampling skew at the hitlag boundary (retail probe reads
  capsule 12 at DD94 ENTRY = pre-tick; our HB trace exports post-frame), so the "equal
  at 51, 0.05 apart at 52" reading may be one tick off. NEXT: dump the CHAIN STATE
  per-link (DynamicsData jobj rotate quats + desc.lb_unk0.unk_58/unk_2C) at DD94 entry
  BOTH sides for validator 46-53 and find the first diverging link+field; then audit
  that op (msl_dynamics_build_basis / transform helpers vs Gekko paired-single PSMTX).
  PROBE QUIRK (reproduced twice): a tail-probe window 44..56 yields ZERO rows while
  38..62 works — start windows earlier/wider. Sim traces: MSL_DD94_TRACE=<path> now
  dumps DD94IN inputs (ftdynamics.c container-only; needs the msl_emitter_trace helper
  in scalar.c container copy + stdio includes).

- 2026-07-28 — `retained` (WHISPY WIND + CHAIN MATRIX REFRESH LANDED, commit 637b6706;
  AGGREGATE 190/56/0 — up from 183/63/0). Final design after the live-machine attempt
  regressed Fountain knife-edges: (1) wind emitters mirrored deterministically from the
  RECORDED xDC stream (nonzero == retail xD0 in (0x2D,0x140) entering 0x2E; spawn on
  %10==0; onset tracked via this proc's own xD0-as-counter because the pre-frame replay
  pass already wrote xDC); the machine itself stays short-circuited (its rand timers are
  not replayable headlessly — retail's effect draws precede its prio-4 stream position —
  and fn_802112F4's push reads xDC post-prio-4, so a live machine forks positions by the
  0.10 wind speed). (2) msl_fighter_refresh_dynamics_matrices at the publication phase:
  retained chain-link jobj matrices rebuilt end-of-frame so ftCo_8009CB40's re-anchor
  (mtx[i][3] reads on anim changes) sees last-frame world poses like retail's render
  guarantees; previously links past the capsule bone were NEVER built (identity → tail
  tip re-anchored to x=0 exactly at dl-fox validator 51). Chain-state probe now
  BIT-IDENTICAL across the hit window. SEVEN long-classified replays (5 DL, 1 YS, 1 FD,
  all with Fox) became full passes — classifications removed, output locks re-recorded.
  dl-fox 10,818/11,083. Icies otherwise unchanged; ys-fox classification intact.
  OWED: container pytest sweep; PPC-backend lock parity check (locks re-recorded from
  native; hosted-only fixes leave PPC behavior unchanged but verify before the wire cut).

- 2026-07-28 — `retained` (SEVEN MISC-LANE CLASSIFICATIONS) + `open` (the 88/91 roll
  misalignment = A MISSING EFFECT DRAW). Icies now 0 pass / 8 classified / 4 fail.
  The four open episodes and what is known:
  * bf-marth @5605 + medium-marth @10783 (leader DamageFly 88 vs 91): ROOT-CAUSED TO THE
    RNG STREAM. The variant roll (ftCo_Damage.c:419, HSD_Randf < x240 in ftCo_8008DCE0;
    91=DamageFlyRoll picked when kb_level==3 && percent>=x23C) samples one position
    early in ours. Retail frame 5605 draw sequence (bfm_randf_probe.jsonl):
    [09F7 CPU x3 BIT-MATCH, efSync lib draw BIT-MATCH, **extra dispatcher draw at lr
    0x80063B74 inside efAsync_Dispatch = the gfx 0x3EC case: li r3,8; bl efLib create;
    HSD_Randf**, then the roll]. Ours lacks the dispatcher draw because OUR queue holds
    gfx_id 0x3F8 (1016, spawn_kind 6) at that tick — no case in effects.c's dispatcher
    switch — while retail dispatches 0x3EC (1004, model 8 + one Randf, a case we already
    model). SO: either our enqueued gfx id is wrong (enqueue caller attribution via
    __builtin_return_address lands in ftCo_0A01.c ftCo_800A05F4 region — inlining-
    degraded, re-attribute with a debugger or wider addr capture), or 0x3F8 needs its own
    dispatcher-draw case (check refs efasync.c case 0x3F8: efLib_Create_Attach_Pos(0x13)
    — MODEL-BACKED, no draw, so more likely the ID is wrong our side or retail spawned a
    DIFFERENT SECOND effect). Fixing this likely clears BOTH marth replays.
  * dl-fox @5205-5215: small item vel/pos episode (12 fields, blizzard-adjacent).
  * fod-sheik @10759+: item existence episode (item_count/type/owner from 10761).
  TOOLING ADDED (container): MSL_GFX_TRACE_PATH prints GFXQ (enqueue, caller low24 in b
  as float — decode carefully, it is exact for <2^24) and GFX (process) rows via
  msl_emitter_trace; our RNG trace pairs frames via synthesized online seeds
  (base + (frame+123)<<16 — bf-marth base 33407).

- 2026-07-29 — `closed` (session 12: master-diamond @682 root-caused — the UCF pad-ring
  patch profile; ALL 13 extended icies replays in the aggregate: 271 = 203/68/0)
  The @682 "grab-attach" was misread: Sheik was hit by an ice block at 681 and our
  runtime SDI'd her 6.0*stick = -5.85 during hitlag at 682. Trace chain: the
  displacement matched sdi_pos_scale * lstick exactly; the spaghetti trace showed her
  x670 tilt timer correctly stale (flick began on the hit frame, so the count-up
  branch ran), meaning vanilla SDI could not fire - the trigger was
  msl_ucf_sdi_check (the pad-ring f2 SDI patch), which retail provably did not run
  for this session.
  A first attempt keying all ring patches on ucf_cardinals_1_0_enabled BROKE EIGHT
  aggregate replays (2022 luigi/marios, puff master-diamond, marth x2, falcon,
  peach x2 - all cardinals-false yet bit-exact under ring-patch emulation): the
  ring-patch rollout and the cardinals rollout are INDEPENDENT eras. Reverted;
  the correct model is the existing per-replay rollout-flag scheme (wire.h already
  documents ucf_sdi_enabled/ucf_shield_sdi_enabled as "separate
  rollout/capability" for exactly this reason).
  CHANGES: new ucf_shield_drop_extended_enabled flag plumbed end to end
  (wire.h/wire.c config byte - MslCoreMatchConfig 52->53, header 108->109;
  match rules; scalar init; native.c kwargs x2; suite_io + validate_replay
  schema); msl_ucf_pass_oos_stick_check re-keyed from the session-11 cardinals
  gate to the new flag; the two ranked-anonymized icies captures
  (master-diamond, platinum-platinum) carry ucf_sdi/ucf_shield_sdi/
  ucf_shield_drop_extended = false (platinum's flags also reconcile its ppc
  snapshot history: the 1326-row follower fork seen pre-gate was sdrop-on
  behavior).
  RESULT: master-diamond 782 -> 6,186/6,460 matched, item.misc-only; classified
  (native 914 / ppc 194) + locked; icies_parked.json dissolved into
  icies_extended.json (13 replays); counts 270->271; aggregate 203/68/0; container
  pytest green; WingedGorgeousPanther ppc snapshot verified stable under the
  re-keyed gates; PPC rebuilt for the wire change.
  OPEN QUESTION (for future imports): what distinguishes the ranked-anonymized
  profile (no ring patches, 2023-24 vintage) from ordinary 2022+ netplay (ring
  patches present)? Possibly a ranked-mode gecko set. New anonymized imports
  should A/B the three flags against retail probes before classification.

- 2026-07-29 — `closed` (session 11: icies_extended wired into the aggregate — 270
  replays, 203 pass / 67 classified / 0 fail; the pre-cardinals UCF shield-drop gate)
  master-diamond's frame -19 fork root-caused with retail probes: retail Nana
  (mimic-driven off Popo's ring) dashes to the platform edge, GuardOns off the
  ring-played trigger at -19, and vanilla shield-drops (Pass) a frame later when
  the played stick crosses -0.66. Ours Passed at -19 through the UCF
  shield-drop-extended emulation: msl_ucf_pass_oos_stick_check read the PORT's
  sdrop counter (hot from Popo's own deep-down stick) with no version gate. The
  shipped gecko's counter lives in the shared UCF pad ring, which ships inside
  the combined "UCF Pad Buffer + 1.0 Cardinals" patch — recordings that predate
  1.0 cardinals (ucf_cardinals_1_0_enabled=false) never ran that buffer, so the
  counter branch cannot fire there. FIX (runtime/match.c):
  msl_ucf_pass_oos_stick_check returns false when cardinals are disabled.
  Verified: master-diamond -19 fork gone (103 -> 782 matched rows); aggregate
  unchanged (several cardinals-false replays re-validated identically).
  master-diamond's NEXT fork (frame 682) is a different class — Sheik
  grab-attached ~5.85 units from retail while BOTH climbers match bit-exact
  (pummel/throw cascade from 687); unaffected by the session-10 matrix refresh
  (A/B verified). PARKED in the new replays/suites/icies_parked.json.
  WIRING: icies_extended.json rescoped to the 12 green replays and added to
  melee_core_aggregate.json (258 -> 270); 12 unrecorded-item-pool-residue
  classifications with native + ppc snapshots; 12 output locks; coverage/runner
  tests updated (270 + manifest set + classified loader). Trace infra: negative
  frame windows now work for MSL_NANA_TRACE (match.c gate accepted only
  start >= 0); container gained AA42C-IN/RECORD/MIMIC-held/PASSGATE/GUARDCHK/
  OOSCHK probes (ftCo_0A01.c, ftCo_Pass.c, ftCo_Guard.c, container-only);
  slippi-dolphin ring probe now dumps nana x618/x671/x668.
  KEY RECIPE: Nana entry behavior = mimic ring playback of Popo's inputs
  (capture quantizes input.lstick*127 at think time, one frame stale; the ring
  x4/x5 trigger bytes truncate the merged x650 float to 0/1 — shields propagate
  through the held word's HSD_PAD_LR bit, not the analog).

- 2026-07-29 — `closed` (session 10: THE TAIL-SOLVER FORK ROOT-CAUSED AND FIXED —
  two production fixes, 4 classified replays promoted to bit-exact, aggregate
  203 pass / 55 classified / 0 fail)
  The session-9 "solver fork with bit-identical inputs" was never in the solver.
  Per-stage link_dir instrumentation (MSL_ST rows inside lb_8001044C, container)
  showed our frame-3468 YS solve matching retail's implied final dirs to 0.2-0.8
  deg — but our OWN 3469-entry anchors differed from what our solve wrote.
  ftCo_8009CB40 (the anim-ownership toggle) had rewritten the chain anchors from
  raw jobj->mtx at the taunt-exit action change. Retail probe on 0x8009CB40
  (slippi-dolphin: cb40 event in MaybeCaptureTailProbe + per-link unk_38 "av"
  dump) proved retail fires the identical release/acquire pair but reads
  END-OF-FRAME RENDER matrices (solved pose, stale through the next frame's anim)
  — our port had left the last MID-FRAME pre-solve HSD_JObjSetupMatrix in mtx.
  FIX 1 (melee/ft/fighter.c): Fighter_8006D9AC re-runs
  msl_fighter_refresh_dynamics_matrices after ftCo_8009E0A8 (post-solve), so the
  toggle samples the solved pose exactly as retail's renderer leaves it.
  Verified bit-identical CB40 rewrites vs retail at YS 3469 and 33692 frames
  36/313/677.
  That fix flipped marios/33692 (was pass) — bisecting its 380-solve fork against
  retail (windows 100..380 bit-identical in w/rot/s58/jr/jt/par/av) landed on the
  collider-avoidance stage: retail rotated link 2 by 0.1896 rad off collider
  (-43.64,10.65,0.38); ours read collider position (x14, pos.x, pos.y) because
  struct lb_Collider mirrored Fighter_x1670_t with byte-offset padding — the
  embedded HSD_JObj* widens to 8 bytes on 64-bit hosts, shifting position from
  0x18 to 0x1C and the stride from 0x28 to 0x30. Hosted collider avoidance had
  read garbage since the port began (PPC, with 4-byte pointers, was always
  correct — hence "ppc matches more rows than native").
  FIX 2 (melee/lb/lbspdisplay.c): lb_Collider declared with real field types
  (Vec3, f32, void*, f32, Vec3, s32) — layout-correct at any pointer width.
  RESULTS: 33692 passes; auto-fox-b @3503 SDI cascade gone (item.misc-only now);
  icies_extended = 12 misc-only + master-diamond @-19 (parked); icies suite 12/12
  classified; aggregate 203/55/0 with FOUR replays promoted to fully bit-exact
  (sheik MixedAllQuetzal, doubles Game_20260704T012353, marios medium-fox-2025-12
  + silver-fox-2026-06 — the last dropped a 52,031-field residual).
  Suite updates: 2 classification entries removed, 2 kept ppc-only (native dict
  dropped; test_melee_core_validation now selects native-snapshot entries only),
  4 output locks re-recorded, 2 ledger rows appended. PPC re-verified per-entry
  after the rebuild (the PPC build also defines MSL_CORE_HOSTED so FIX 1 applies
  there; snapshots re-recorded where drifted).
  KNOWN GAP: the Mac-side data/ pipeline only covers 7 characters (no ICs) — Mac
  icies runs fail on stale data, container remains the validation authority.
  Remaining icies-extended non-misc work: master-diamond @-19 (match-start
  entry-approach clamp via published extents, likely CB40-at-spawn sampling
  pre-first-refresh matrices).

- 2026-07-28 — `closed by session 10` (session 9: icies_extended staged; the tail-solver fork isolated
  to a single frame with bit-identical inputs)
  Scope: ~/SSBM/Replays/Samples/ics.zip imported (30 files = the 12 existing suite
  replays + 18 new). Excluded: 4 non-frozen-PS games (established policy) and the 2022
  FoD capture (Slippi 3.9.1 predates the animation_index stream — validator ERROR;
  samus-suite precedent). The remaining 13 staged as replays/suites/icies_extended.json
  (commit 770c8c27), NOT included by melee_core_aggregate.json — icies.json stays at 12
  so the aggregate/coverage gates stay green (258, container-verified 12 classified/0
  fail post-split). ucf refinements: master-diamond (v3.16) + platinum-platinum (v3.15)
  ranked-anonymized carry ucf_cardinals_1_0_enabled=false + played_on=network (the
  cardinals flip fixed master-diamond's frame -39 Popo fork: 0.185 vs 0.185*cardinal
  ratio signature).
  Extended-suite state: 13 FAIL. 11 are item.misc-lane-only (unrecorded-item-pool
  class, classify after fixes). Two real forks, both root-caused deep:
  * auto-fox-2025-02_Game_20250224T030855 (YS) @3503: spurious CPU-Nana SDI (+6.0
    exactly; retail frozen in hitlag). Full causal chain established with retail
    probes: Fox's PUBLISHED capsule extent x1A88.x560 (ftCo_800A0DA4 max over hurt
    capsules) diverges at the frame-3470 publication (retail 3.446 vs ours 4.260;
    3465-3469 bit-exact) because cap[12] = THE TAIL (scale 1.62) sits 0.8 too far
    out -> Nana's B8A9C/B4AB0 range check admits an attack candidate ONE THINK EARLY
    (our x18 0->2 at think 3471, retail at 3472; x7C/x80/x570/positions/colliders all
    verified bit-identical) -> different B4AB0 Randf pick (frame-seeded): ours built
    a WaitFor(26+5)+PressR+LstickXForward(0x7F) roll macro, retail a WaitFor(10)+
    Y-tap macro -> our stale macro replays +127 during Nana's 3502-3504 hitlag ->
    x670 tilt-timer resets -> ftCo_Damage_OnEveryHitlag SDI fires.
  * THE TAIL SOLVER FORK (the dl-fox @5082 class, now minimally reproduced, wind-free):
    Fox's 4-link tail chain state at ftCo_8009DD94 entry is bit-identical at frames
    3466-3468 (per-link rot/s58/unk_2C anchors/params unk_44-8C, jobj jr/jt, full
    ancestor mtx walk incl. dirty flags, colliders x1670, arg1, and retail
    lb_804D63B0 emitters = NULL on YS both sides) yet the FRAME-3468 SOLVE (Fox taunt
    -exit + movement-start jerk) forks the free links' unk_2C anchors by 14-23
    degrees of link direction (root link 0.0 stays bit-exact -> parent pose agrees;
    ours under-rotates toward the new pose = tail trails wider). lb_8001044C C source
    == refs C (only documented fmadds deltas). NOTE the jr-equality at next entry is
    NOT evidence the solved rotations matched — anim overwrites jobj->rotate before
    the next DD94; the surviving solver state is unk_2C (+un-dumped unk_38/unk_44
    angular velocity). NEXT: stage-by-stage link_dir dump inside our lb_8001044C at
    frame 3468 (after stiffness/gravity/force/angvel/max-angle(unk_88=0.0524!)/
    convergence/deviation/collider/ground stages) vs retail's implied final dirs
    (derivable from the 3469-entry anchors); suspect a branch-boundary flip or an
    op-level slip in a jerk-only stage (max-angle clamp math, RotateAboutUnitAxis,
    or the ground-collision mpCheckFloor block near the YS slope).
  * master-diamond residual @-19 (post-cardinals): Nana entry-approach clamp 125 vs
    127 (1.4*125/127 velocity signature) = the AA42C clamp fed by published capsule
    extents — plausibly the same tail/extent class (Sheik hair dynamics), park until
    the solver fix lands.
  TOOLING: slippi-dolphin 1872b778b9 (tail probe: MSL_TAIL_PROBE_PORT, per-link
  params, ancestor walk, emitter global; ring probe: x80/x84/xA4/x570 + target
  extents). Container /work (env-gated, uncommitted): SDI-CHECK in ftCo_Damage.c,
  NANAIN x18/x670 fields + THINK buffer dump in ftCo_0A01.c, B8A9C-IN in
  ftcpuattack.c, EXTENT capsule dump in ftCo_800A0DA4, DD94 trace parameterized
  (MSL_DD94_PLAYER/START/END) + PARAMS/ANC rows in ftdynamics.c, frame-gated
  msl_nana_trace_file (gm_8016AEDC-based, prints F<frame>) in match.c, rand/randf
  trace in sysdolphin/baselib/random.c. Mainline bot captures are per-frame-seeded
  like online games (verified: base+frame<<16 stride in our stream).
