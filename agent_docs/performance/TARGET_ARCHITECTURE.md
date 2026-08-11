# Final batch-native architecture

This document is the architectural contract for the native production simulator. It describes the
required end state, not an intermediate optimization and not a claim that the throughput target has
already been demonstrated.

## Required end state

`MslBatch` owns the only mutable production gameplay representation. State is component-major
AoSoA, conceptually `[component][fighter slot][tile][lane]`, with the tile shape chosen for native
SIMD across independent matches. Shared extracted game data is immutable and addressed with compact
IDs or offsets. Only gameplay-live mutable fields are replicated per match.

The production representation contains no hosted `Fighter`, GObj, JObj, CollData, heap arena,
function-pointer callback graph, pointer relocation, mirrored state, sidecar, or compatibility
publication. Reset, copy, arbitrary-index save/restore, observation, and terminal publication
operate directly on the canonical arrays and allocate nothing during gameplay.

A frame is a fixed source-ordered phase spine:

1. input/UCF and action-state decisions;
2. animation and script advancement;
3. demanded pose and dynamics products;
4. physics and stage collision;
5. hurtbox, hitbox, shield, and contact generation;
6. hit, throw, item, and stage resolution; and
7. camera, observation, and terminal publication.

Each phase processes resident match lanes while its component state and immutable tables are hot.
Action-specific behavior is selected by compact program IDs and stable lane masks without moving
state into temporary source objects. Exact retail order is retained inside each match for fighters,
contacts, hits, items, callbacks, RNG, and floating-point operations. Vectorization occurs across
independent matches, so it does not reassociate operations within a match.

Pose does not construct or publish a general JObj hierarchy. It evaluates exactly the canonical
joint products demanded by ECB, hurtbox, hitbox, dynamics, item, and camera consumers, and those
consumers use the products in the same phase. Each required product has one owner and is evaluated
no more often than its source semantics require.

The hosted decomp runtime remains available only as a separately executed differential oracle. It
is not constructed or entered by the native production simulator, and the two implementations do
not synchronize mutable state. Once the batch-native engine is complete, the public API switches
to it and the displaced hosted runtime is removed from the production build and call graph.

## Throughput case

The current baseline costs roughly 38--40k cycles per simulated frame. At approximately 4.3 GHz,
400k FPS permits about 10.7k cycles/frame and 500k FPS permits about 8.6k cycles/frame. Reaching the
target therefore requires about a 3.6--4.7x reduction; callback dispatch or isolated leaf tuning
cannot supply it.

The required reduction is conceivable because the final representation changes all of the costs
that the hosted runtime preserves:

- The current ordinary match owns roughly 609 KiB of arena-backed state. Component-major hot state
  replaces the large pointer-rich working set with the fields required by the current phase.
- Phase-major execution replaces complete actor/match traversal, repeatedly cold code and tables,
  and opaque callback dispatch with visible loops over contiguous match lanes.
- Cross-match SIMD amortizes exact animation sampling, transforms, integration, bounds generation,
  broad-phase tests, and suitable stage queries across native vector lanes.
- Direct producer-to-consumer ownership removes general pose publication, repeated API traversal,
  ownership recovery, and equivalent product reconstruction.
- Compact IDs and immutable tables replace source graph traversal without changing gameplay
  semantics or intra-match ordering.

The historical compact simulator's roughly 400k throughput is architectural evidence that this
problem and machine admit the general batch-owned, phase-major execution shape. Its gameplay
inaccuracies mean it is not an implementation template or correctness proof. The final engine must
combine that execution shape with the completed source semantics now present in the hosted runtime.

The following is a design budget, not retained evidence:

| Owner group | Target cycles/frame |
|---|---:|
| Input, action, and scripts | 1.0--1.5k |
| Pose and dynamics | 2.0--2.5k |
| Physics and stage collision | 2.0--2.5k |
| Contact, combat, and items | 2.0--2.5k |
| Camera, observation, and bookkeeping | 1.0--1.5k |
| **Total** | **8.0--10.5k** |

The target is at least 500k FPS at both resident 256 and resident 512 on the canonical single-core
benchmark with exact outputs and full validation. The architectural cutover is still required if
an early full-engine measurement lands below that target; the completed canonical representation
is where remaining costs can be profiled and removed without another representation rewrite.

## Cutover discipline

This is deliberately a major replacement, but it must remain bounded by ownership rather than by
elapsed time or line count.

Before implementation, `agent_docs/ACTIVE_WORK.md` must contain one compact cutover ledger with a
row for every gameplay owner. Each row records:

- canonical mutable fields and final owner;
- source references and required operation order;
- producer and every consumer;
- batch-native implementation and exactness evidence;
- displaced hosted state, entry points, and callbacks; and
- deletion/call-graph proof and measured cycle attribution.

That ledger is the progress authority across context compactions. A phase is not complete because
new code exists; it is complete only when its canonical state, full producer/consumer closure,
exact behavior, and deletion boundary are all accounted for.

The new engine is implemented directly in its final state and phase shape alongside the frozen
oracle. Production must never become a chain of old and new phases joined by mirrors, adapters, or
publication bridges. Work proceeds in dependency order through complete phase owners, using
focused source-backed exactness tests while the full engine is incomplete. After the final phase
closes, full differential validation, allocation/save-restore gates, and controlled resident
benchmarks precede the atomic API cutover and hosted-runtime deletion.

The fixed architectural invariants are singular batch ownership, component-major state, the
source-ordered phase spine, direct consumers, exact intra-match semantics, and the complete hosted
deletion boundary. Kernel details such as tile width, mask formation, and vector grouping remain
empirical choices. A failed kernel is reassessed inside these invariants rather than causing a
return to hosted state or an unrelated optimization campaign.

Material experiments and decisions remain searchable under `agent_docs/performance/`. After two
hours without a retained result or completed deletion boundary, after two failures on one
architectural unknown, or when provenance becomes uncertain, implementation pauses for a ledger
update and a bounded reassessment before continuing. That reassessment changes the kernel approach
when evidence requires it; it does not silently abandon the final architecture or delete dirty
work. Dirty implementation is never discarded without a salvage inventory, and no temporary
throughput number is represented as an architectural win.

## Completion criteria

The cutover is complete only when all of the following hold:

- every supported gameplay owner and consumer is closed in the ledger;
- the production API constructs and steps only canonical `MslBatch` state;
- hosted objects, callbacks, arenas, and compatibility publication are absent from the production
  call graph;
- reset, copy, arbitrary-index save/restore, and observation use canonical state with no gameplay
  allocation;
- exact-output checks and the full supported-domain validation suite pass;
- native, PPC, Wasm, and required viewer gates pass; and
- controlled alternating resident-256 and resident-512 measurements demonstrate at least 500k FPS
  at both sizes, with retained cycle/profile evidence.
