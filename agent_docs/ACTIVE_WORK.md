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
