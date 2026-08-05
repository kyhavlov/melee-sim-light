# Compiled fighter motion to gameplay geometry

Status: rejected as a performance architecture; runtime code not retained. Date: 2026-08-03.

## Hypothesis and boundary

The attempt replaced an admitted class of native fighter JObj/AObj/FObj animation with a compact
per-fighter scalar program and product cache. The proposed owner extended from motion binding and
sampling through hit, hurt, shield/reflect/absorb, ECB, camera, attachment, and dynamics products.
It was intended to delete source animation advancement, dirty matrix traversal, repeated bone/path
discovery, and repeated geometry materialization without dual mutable pose state.

The exact parent was `de8879be`. Its accepted resident benchmark medians were 39,916.6 cycles/frame
at 256 matches and 41,490.4 at 512. The attempt was justified only if the complete vertical made a
substantial whole-runtime improvement; the broader program needs roughly a fourfold gain, so a
large refactor was not supposed to be accepted for a token result.

The final worktree represented roughly eight thousand changed lines including the 4,015-line
compiled owner, integration, validation churn, and documentation.

## What was implemented

- `GameData` compiled topology, exact Figa samples, motion metadata, consumer closures, fractional
  programs, secondary-pose data, and compact authored hit descriptors.
- Each Match-owned fighter lane stored one pointer-free pose/product cache. Admitted segments did
  not advance the source mutable pose in parallel; unsupported topology, source mutation, ground
  IK, relationships, and followers handed ownership back to the source path.
- Hit, hurt, shield, ECB, camera, attachment, and dynamics consumers were integrated with the
  compiled products. The final consumer pass removed a copied six-origin ECB cache, limited hurt
  evaluation to the requested capsule, and rejected repeat contacts before an unnecessary matrix
  lookup using the exact retail AABB predicate.

The implementation became correctness-exact: 310 PASS, 56 unchanged CLASSIFIED, and zero failures
over all 366 replays and 3,501,461 frames. Native, PPC, Wasm, copy/save/restore, and sealed-allocation
gates passed.

## Performance result

The accepted first closing comparison improved only 0.52% at resident 256 and 0.31% at resident
512. The immediate-consumer cleanup improved the pre-cut candidate by 0.22% and 0.37%. A later
closing parent sequence measured 0.91% and 0.85%; all defensible results place the complete
architecture below a 1% gain.

Same-instrumentation profiling showed the central failure. Fighter animation saved about 80.5M
cycles over 65,536 frames, but demanded work reappeared in stage collision, contact publication,
`Fighter_procMap`, and hit processing. After the consumer cleanup the whole-contract profile moved
only 48,904.2 to 48,754.7 cycles/frame. This was a small real reduction, not a path to the required
speedup.

## Bounded variants and evidence

- A 122 MiB dense full-SRT stream improved only about 0.3%; a 40 MiB rotation stream was slower.
- A precomputed world-basis stream raised RSS to roughly 956 MiB and reduced resident-512
  throughput to about 87k FPS.
- O2 and O3 compilation of the exact owner were slower than O1.
- Dependency-demand replay requested only 4.9% fewer nodes than prefix evaluation and would lose
  contiguous trig/product construction.
- Static-trig streams, topology-size admissions, split hot state, cache-line padding, part-map
  relocation, cross-frame static-product reuse, batched samplers, and cross-unit matrix batches
  were neutral or regressive.
- Prechecking first-publication contacts, forced inline/O3 broadphase variants, and splitting the
  exact capsule solver were neutral or slower. Only rejecting repeat contacts before a redundant
  matrix lookup survived, and its whole-runtime value was small.
- A source-owner ablation initially appeared 4–9% favorable but was not a valid parent control; it
  retained the new integration and state. Comparing the real parent corrected the result.

## Why it failed

The representation optimized scalar pose production but retained the source scheduler and
per-consumer demand shape. Products were still requested through scattered map/contact callbacks,
and the compact cache added its own admission, ownership, and lookup costs. The approach removed
decoder/matrix work without replacing the downstream scalar APIs or data layout that consumed it,
so most saved work moved rather than disappeared.

The result is not useful setup for the next large step. A batch-native map/contact phase would need
a different canonical hot-state layout and would replace much of this cache and integration rather
than naturally extending it.

## Related prior architecture attempts

- A disconnected general rewrite from 2026-07-20 accumulated roughly 94k lines around 118
  operation owners and ran 3.7–6.0x slower. Generic operation scheduling and framework overhead
  dominated useful gameplay work.
- `experiments/decomp-port/system-rewrites` proved several finite semantic seams but retained proof
  scaffolding, handoffs, and source machinery. Its whole-runtime results were mostly noise-bound.

Together these failures rule out both extremes already tried: a generic operation framework and a
per-fighter scalar product sidecar. They do not rule out a complete batch-native owner with one
canonical state and a measured deletion boundary.

### 2026-08-04 batch-native occupancy preflight

A bounded census tested whether the current resident workload supplies enough identical direct
pose rows to replace the scalar owner with a contiguous eight-lane kernel. Resident 256 produced
9,709,792 direct rows across 1,036 batch steps; exact program/node/sample groups achieved only
13.8% lane occupancy, only 0.4% of rows belonged to groups of at least eight, and the largest
group was 12. Resident 512 produced 13,147,946 rows across 972 steps; exact occupancy was 14.9%,
only 1.0% of rows belonged to groups of at least eight, and the largest group was 18.

Grouping only by publication shape reaches 97.4% and 98.1% occupancy, respectively, but each lane
then gathers a different immutable sample and scattered mutable JObj destination. That is the
same gather/scatter and handoff shape whose scalar and batched variants were neutral or slower in
this attempt. It also cannot repay the independently measured 7--9% cost of suspending the source
scheduler around a pose-only batch phase. The temporary census was removed completely.

This rules out a contiguous program/sample batch kernel on the current canonical layout. It does
not rule out batch execution after a wider canonical-state cutover makes same-shape inputs and
immediate geometry outputs contiguous; such a proposal must name and delete that scattered state
before implementation rather than treating vector occupancy as sufficient.

## Salvage and revisit rule

The compiled runtime and its integration are not retained. The independent Ice Climbers item
projection correction discovered during validation is retained separately: it publishes only
source-defined gameplay bytes for Ice, Blizzard, and Belay and removes 24 obsolete raw-pointer or
pool-residue classifications net of the one genuine Whispy RNG-phase classification that remains.

Revisit fighter motion/geometry only when the proposal names a materially different canonical
batch representation, the map/contact consumers that use it directly, the old state and callbacks
deleted in the same cut, and a bounded cost model capable of a substantial whole-runtime gain. Do
not rebuild this design by incrementally extending a scalar per-fighter product cache.
