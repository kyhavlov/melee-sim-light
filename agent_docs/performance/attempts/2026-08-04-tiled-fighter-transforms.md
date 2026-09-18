# Tiled fighter transforms and SIMD publication

## Hypothesis

Move native fighter JObj transforms into fixed eight-Match tiles, schedule the
main-pose owner across each tile, and publish the dominant exact `0x00E`
rotation shape with a true SIMD kernel. More than 97% of direct pose rows share
their publication shape, so a canonical tiled owner was the remaining
materially different form after older scalar tile schedulers and gathered pose
sidecars failed.

## Completed boundary tested

The experiment included external fixed fighter JObj storage, tiled lifecycle
binding, arbitrary-lane save/restore relocation, split prepare/finish scheduler
ownership, direct compact-pose traversal, and an eight-lane AVX2 gather/AVX-512
scatter publisher. The vector kernel retained lane-owned clocks, dependencies,
dirty flags, and exact float values. A frozen build with only the vector
threshold disabled provided the internal scalar-owner ablation; it was not used
as a parent control.

## Result

All measured screens preserved exact production digests. At resident 256, the
clean reversed comparison centered at 46,177.7 cycles/frame for the scalar
ablation and 46,204.8 for SIMD, effectively neutral. At resident 512, reversed
pairs centered at 44,434.6 scalar and 43,883.2 SIMD, a 1.25% local recovery.

The complete SIMD candidate was nevertheless much slower than committed
`02cfe013`: roughly 45-47k versus 36,908.0 cycles/frame at resident 256, and
43,883.2 versus 35,267.5 at resident 512. The dominant-shape kernel cannot
repay the scheduler split, lost per-Match residency, and indirect transform
ownership. Width sweeps, a one-pointer runtime-context consolidation, direct
scalar publication, and the actual SIMD publisher all failed to change that
conclusion.

## Decision

Reject the tiled-transform packet. Do not revisit it by changing tile width,
adding scheduler phases, consolidating TLS/context pointers, or vectorizing
gather/scatter publication. A future batch-native transform representation
would have to eliminate gathers and scatters entirely and keep immediate
geometry consumers in the same canonical layout; it cannot use this packet as
retainable setup.

The packet was removed hunk-selectively after the salvage inventory below.
Earlier independent dense canonical JObj matrices and lossless compact pose
samples were preserved.

## Source salvage inventory

Removal must be hunk-selective because the dirty tree also contains independent
measured candidates. The rejected packet owns these complete changes:

- `runtime/context.{c,h}`: the single TLS context object;
- `runtime/relocation.{c,h}` and the savestate-v5 additions in
  `runtime/savestate.c`: external fighter-JObj relocation/serialization;
- `baselib/class.{c,h}`: placement construction and fixed-JObj release;
- `baselib/gobj.{c,h}`: split callback invoke begin/end;
- `runtime/batch.c`: context/JObj backing stores, configuration permutation,
  tiled priority scheduling, seed scratch, and logical/physical indirection;
- `runtime/scalar.{c,h}`: context/JObj lifecycle, split-scheduler and fighter-pose
  entry points, and the scheduler-owner substitution in step finish;
- `fighter.c`, `fighter.h`, `ftanim.{c,h}`: main-pose prepare/finish and batch
  pose seams;
- `fighter_pose.{c,h}`: fixed JObj allocation, live bitmap, batch lane types,
  direct batch traversal, gather/scatter SIMD publisher, and tree-frame query;
- `baselib/jobj.{c,h}`: only the default-class accessor and fixed fighter-JObj
  construction path. The independent dense matrix pointer/pool changes in the
  same files are not part of this packet.

The following dirty changes predate the tiled packet and must survive its
removal:

- exact wide-trig sign publication, hardware native square root, dynamics angle
  cutoffs, direct fixed-point pose scaling, the compact pose sample-stride/value
  representation, and the native unreachable IK/dirty-condition deletions;
- dense canonical JObj matrices, including the native `mtx` pointer,
  construction/release allocation, and native-DAT layout exclusion;
- duplicate output-clear/item work deletion, emitted-item-count
  canonicalization, terminal team-mask popcount, inline wire primitives, and the
  one-keyword batch-mask predicate inline;
- the source-backed native fighter-part animation byte in the compact pose node.
  Frozen pre-tile binaries confirm that this singular owner predates the tiled
  packet, so cleanup preserves it rather than restoring separate `FighterBone`
  reads;
- all other independently measured arithmetic and source-owner cuts recorded in
  the [historical work log](https://github.com/kyhavlov/melee-sim-light/blob/5018738c8b2da68330823bb20fee37a5044841cd/agent_docs/ACTIVE_WORK.md).

Frozen pre-tile binaries also confirm that the direct output publication and
general native `PSMTXConcat` vector owner predate this packet. They remain part
of the independent dirty aggregate. The format-only whitespace changes in
`it_2725.c` and `replay_bench.c` had no performance ownership and were dropped.
