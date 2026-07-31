# Active performance packet — canonical embedded stage-line topology

## Objective

Replace the native per-Match `CollLine -> MapLine` pointer graph and separate copied `MapLine` array
with one compact canonical mutable line record. Preserve the retail layout on PPC and preserve every
source adjacency mutation, line id, query order, flag, and transformed vertex owner.

## Final boundary

- **Final owner:** native `CollLine` owns its source `MapLine` topology inline with its runtime flags.
  `mpLibLoad`, empty-line pruning, stage stitching, island construction, and every `mplib` query read
  and mutate that one record directly.
- **Canonical state:** one embedded `MapLine` per stage line, with runtime enabled/hidden state in
  the otherwise unused high bits of its `hi_flags` field. Extracted
  `MapCollData::lines` is immutable construction input only; mutable Match topology has no second
  array and no pointer back to it.
- **Consumers:** the complete `mplib.c`, `mpisland.c`, `mpcoll.c`, stage callbacks, items, fighter
  collision, copy, and save/restore use the same ids/fields and source ordering.
- **Displaced state/work:** delete `MslMpLibState::map_lines`, its construction allocation/copy, and
  the relocatable pointer slot in each native `CollLine`. Source expressions such as
  `line->x0->v0_idx` decay directly to the inline one-element owner and no longer load a pointer.
- **Deletion boundary:** every native mutable topology access resolves to the embedded record from
  construction onward, including `mpPruneEmptyLines` and `mpLib_800581DC`; no synchronized copy,
  fallback graph, accessor bridge, or partial query cut remains. PPC retains `MapLine* x0` exactly.

## Evidence and acceptance

- Native currently stores a 16-byte `CollLine` plus a separate 16-byte `MapLine` per stage line;
  the former contains an 8-byte pointer to the latter. The final compact record remains 16 bytes
  by aliasing runtime flags into source-unused `hi_flags` bits, removing 16 bytes per line plus the
  separate allocation/relocation graph.
- `mplib.c` contains hundreds of source topology dereferences throughout the 12.99% current fighter
  stage-collision owner. The embedded one-element representation preserves source syntax while
  letting optimized native code address fields directly.
- Both production digests and complete replay/API/copy/save-restore/allocation/PPC/Wasm/viewer gates
  must remain unchanged. Retain only a repeatable throughput or material memory gain at 512/256.

## Log

- 2026-07-19 — `open`
  Scope: `CollLine`, native `MslMpLibState`, `mpLibLoad`, and native empty-line pruning; downstream
  source users retain their current expressions and semantics.
  Hypothesis: deleting the mutable pointer graph improves collision locality and reduces resident
  Match state without a query-time admission branch.
  Evidence: post-load runtime only accesses mutable topology through `groundCollLine[].x0`; the
  separate `MapCollData::lines` copy exists solely to provide those pointees.
  Disposition: make native `x0` an inline one-element `MapLine`, copy construction input once into
  that owner, prune it in place, remove the old allocation, then checkpoint digest/size/512 early.
  Next: complete the singular construction cut and inspect generated relocation layout and symbols.

- 2026-07-19 — `retained`
  Scope: complete native construction, topology mutation, query, relocation, copy, and snapshot
  boundary for supported stage lines.
  Hypothesis: the final 16-byte alias layout can delete the pointer and duplicate record without the
  20-byte candidate's locality regression.
  Evidence: all supported extracted stages use only `hi_flags` values 0, 1, 2, 4, 8, and 17, leaving
  bits 14/15 for native enabled/hidden state. Both production digests are exact. Adjacent 512
  measurements are throughput-neutral; two 256 pairs move from 41,982.1/41,880.0 control to
  41,953.7/41,752.1 candidate cycles per frame. Ordinary arena/allocation state falls from
  633,432 bytes/825 allocations to 631,820 bytes/824 allocations; ordinary savestate falls from
  695,048 to 693,972 bytes, and maximum reached arena falls from 961,900 to 960,000 bytes.
  Disposition: retain the singular embedded owner as a measured state/allocation deletion, making
  no throughput claim. The initial 20-byte layout was exact but slower and has been displaced.
  Next: run the complete replay/API/copy/save-restore/allocation/PPC/Wasm/viewer gate, then record
  and commit atomically if green.

- 2026-07-19 — `retained`
  Scope: complete material gate for the embedded topology owner.
  Hypothesis: aliasing native runtime bits into source-unused topology bits must remain transparent
  to all stage, snapshot, build, and browser consumers.
  Evidence: debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED / zero
  XPASS/fail/error across 1,415,476 frames. Source/API/copy/save-restore and sealed allocation,
  maximum construction, PPC, Wasm parity, viewer/browser, Python, source-sync, and formatting gates
  are green.
  Disposition: packet complete and ready for its atomic implementation/evidence commit.
  Next: commit and notify the retained state deletion, then refresh the profile and select the next
  bounded high-impact final owner.

# Active representation packet — deterministic hosted source identities

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
