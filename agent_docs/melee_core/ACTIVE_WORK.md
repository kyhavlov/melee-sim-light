# Active melee-core work

This is the required resume point for the current structural packet. Retained evidence belongs in
[`PERFORMANCE.md`](PERFORMANCE.md); this file records the live owner map and chronological decisions.

## Completed packet — queue item 7 strict-O3 owner audit

- **Final owner:** the `NATIVE_RELEASE_OPT_OBJS` allowlist in `src/melee_core/Makefile`, with each
  entry covering one complete measured source owner rather than an arbitrary hot leaf.
- **Canonical state and consumers:** unchanged source gameplay state, scheduler order, and public
  output. This packet is compiler-only and may not add state, dispatch, caches, or source changes.
- **Displaced code:** strict `-O0` host code generation for a candidate owner is displaced only when
  strict `-O3 -march=native -mtune=native`, with unsafe math/LTO still forbidden, retains exact
  output and produces a repeatable adjacent 512 improvement. No runtime compatibility code exists.
- **Deletion boundary:** a rejected candidate is removed fully from the allowlist and build output
  before the next owner. No float workaround, special-case branch, or partial TU is added to make
  optimization validate.
- **Confidence:** potential. Existing retained owners show large gains, while `gobj.c`,
  `ftdynamics.c`, and context/native-DAT access have already proved neutral or negative.
- **Proof:** refresh a bounded current production profile, choose only remaining material O0 owners,
  require exact benchmark digests, then run the complete replay/API gates for any retained set.

The audit is complete with no new allowlist entry. The current packet is the combined item 6/8–10
boundary below.

## Packet 6 — final compact fighter pose/geometry boundary

Queue item 6 cannot be implemented as another scalar pose cache. Its only qualifying production
form includes the pose-specific slices of queue items 8–10: compact cross-environment state, a fixed
pose/publication phase, and direct homogeneous consumers. Queue items 8–10 remain responsible for
applying those shapes to the other engine owners. This grouping avoids creating a scalar
representation that the later batched kernel would immediately delete.

**Confidence:** potential as a complete scalar replacement, definite only when the completed direct
batched consumer boundary deletes the live tree/matrix owner. Pose animation is 13–16% and the broad
pose/geometry group about 25% of the complete frame, so this packet cannot supply the whole 500k
target by itself.

### Final owner and canonical state

- Immutable `MslCoreGameData` owns extracted, source-exact animation programs and ordinary-frame
  semantic geometry/matrices keyed by fighter kind, motion, frame, and semantic part. Parent/rest
  metadata and bounded exceptional-program metadata are shared once across all environments.
- Each active fighter owns only action/motion clock and blend state plus sparse explicit procedural
  state for reached dynamics, IK, guard shaping, capture/throw, partial animation, and character
  mechanics. Ordinary frames do not materialize local SRT or world matrices per tree joint.
- The batch owns aligned AoSoA hot pose inputs and compact published semantic contact/anchor
  geometry. Publication occurs at explicit source scheduler epochs and consumers use direct indices.
- Target mutable budget is at most 1–2 KiB per active fighter, with no 128-part local/world arrays
  and no per-environment copy of immutable frame data.

### Complete consumer boundary

The cut owns every supported-fighter gameplay read of pose:

- ECB and ground-IK source points;
- hit, hurt, shield, reflect, absorb, and grab geometry;
- held-item and article spawn/attachment anchors;
- throw/capture/damage positions and constraints;
- fighter dynamics and partial/secondary animation;
- Fox, Falco, Marth, Sheik/Zelda, Falcon, Puff, and Peach mechanics that read or write fighter
  joints; and
- any gameplay RNG or state transition whose ordering is coupled to those consumers.

Presentation-only model state is deleted or explicitly excluded and may not feed gameplay state.

### Displaced state/code and deletion boundary

For supported hosted fighter gameplay, the completed packet deletes ordinary FObj/AObj/JObj
interpretation as a runtime pose owner, generic fighter-tree matrix recursion, per-joint dirty matrix
publication, and eager hurt-capsule publication through JObj matrices. It also deletes any temporary
source handoff/materialization, FObj reseek, generic JObj setter interception, owner scans, lazy
matrix fallback, dual pose flags, or compensating fighter-tree allocation. Source trees may be used
by an ignored oracle/probe during development but cannot remain on the production step path.

Items/articles keep their own source-backed pose owner unless a fighter semantic consumer named
above crosses the boundary; this packet does not silently absorb the independent item-pose domain.

### Early proof and execution order

1. Add a temporary exact-digest production census for ordinary integer frames, blends/fractional
   rates, procedural owners, semantic part demand, and current tree/matrix work. Remove it after the
   result is recorded.
2. Build an ignored isolated kernel using the actual extracted data and representative batch inputs.
   It must exercise the intended direct ordinary-frame lookup/publication shape and bounded
   exceptional evaluator, report mutable/global memory, and compare against the measured current
   owner. It is proof code, never a production bridge.
3. Proceed only if the final shape has a credible material whole-runtime gain and bounded memory.
   Establish the final shared data and AoSoA state, then directly cut all named consumers and delete
   the displaced owner before broad correctness recovery.
4. Recover replay/API/save-restore/Wasm correctness inside the final representation. Never restore a
   displaced path to make an intermediate gate green.
5. Run the complete gates and adjacent alternating 512/256 production benchmarks. Retain and commit
   only an exact-output improvement with no new or widened replay classification.

The early proof supports the final representation but also confirms that a scalar-only cut would
have a limited whole-runtime ceiling. Production implementation of this boundary is therefore
grouped with the AoSoA/fixed-phase/SIMD packet in queue items 8–10. Queue item 7 is an independent
bounded compiler audit and runs before that structural group; it may not introduce a pose bridge or
change the approved boundary above.

If the proof refutes this representation or two structural experiments fail on the same unknown,
stop this packet cleanly and document the evidence. Do not recreate the archived compatibility
implementation in `stash@{6}` or apply that stash wholesale; its owner inventory and extracted-track
evaluator are reference material only.

## Chronological log

### 2026-07-17 — retained starting point

- **Scope:** clean `1d09aa6d` after queue items 4–5 were rejected and removed.
- **Hypothesis:** a direct shared ordinary-frame/AoSoA geometry boundary can delete the measured
  13–25% live pose/geometry owner without the previous dual-state machinery.
- **Evidence:** retained CPU-0 baseline is 85,156 FPS at 512 and 63,315 FPS at 256 with digests
  `3fb5823d90657775` and `bdc54107c51fa3d7`. Current extracted SSANIM artifacts already contain
  semantic matrices, closure-local SRT, and exact FObj tracks. The archived failed attempt exceeded
  100 KiB/fighter and was about 8% slower while compatibility paths remained.
- **Disposition:** **open**. The old implementation is not a production candidate; only its owner
  inventory and source semantics may be salvaged.
- **Next:** run the bounded production census before choosing the immutable table and exceptional
  state layout.

### 2026-07-17 — retained production pose census

- **Scope:** unchanged complete-output 512-resident benchmark, 32,768 match-frames, all 153 suite
  cases; temporary counters at fighter animation, matrix-build, blend, and hurt-publication owners.
- **Hypothesis:** ordinary integral frames dominate and current work repeatedly evaluates and
  publishes far more tree joints than the compact semantic consumer set needs.
- **Evidence:** digest remained exactly `3fb5823d90657775`. Of 452,014 animation-owner entries,
  419,519 (92.8%) had no active motion blend; among the 353,163 entries that completed an active
  motion evaluation, 330,803 (93.7%) ended on an integral frame and 22,360 (6.3%) were fractional.
  Non-unit rate occurred in 24,386 entries (5.4%), partial animation in 25,749 (5.7%), and root
  motion in 42,352 (9.4%). The current path interpreted 10,754,397 fighter joints, blended
  1,803,552 joints, built 16,911,128 JObj matrices (516 per match-frame), and republished 5,101,253
  hurt capsules. More than half of animation entries had a fighter-dynamics owner, so dynamics is
  common rather than a globally cold fallback; it still affects a sparse joint subset.
- **Disposition:** **retained evidence**. The dominant path supports shared ordinary-frame semantic
  data, but the final evaluator must make fractional/blend and sparse dynamics first-class bounded
  paths rather than rare error handling. All diagnostic runtime hooks are removed next.
- **Next:** prove direct semantic lookup/publication and memory behavior in an ignored isolated
  batch kernel using the current extracted SSANIM data.

### 2026-07-17 — retained final-shape kernel proof

- **Scope:** ignored C probes over all eight extracted `SSANIM01` v5 tables, 512 environments,
  direct publication of two endpoints for 16 semantic parts, representative root transforms, 6.3%
  fractional traffic, and the existing exact SSANIMT1 fractional evaluator called from a C hot
  loop. The probe is not linked into production.
- **Hypothesis:** immutable semantic-frame data plus compact AoSoA inputs can publish the complete
  ordinary contact shape for far less than the measured current pose/geometry budget, without
  per-environment tree state.
- **Evidence:** the eight tables map 146,171,451 shared bytes. Warm direct publication measured
  4.99M env-FPS / 200.4 ns per environment with two fighters and 2.35M env-FPS / 425.2 ns with four.
  Hot inputs were 22 bytes/fighter and the deliberately broad published endpoint buffer 384
  bytes/fighter. The current exact fractional source evaluator measured 4.11M matrix queries/sec
  (243.4 ns/query); this is a conservative upper-bound primitive because the final exceptional path
  evaluates a joint closure once in bulk rather than once per consumer query. For comparison, the
  profiled current broad pose/geometry owner consumes roughly 2.9 microseconds/environment at the
  85,156-FPS baseline. Cold first-touch measurements were excluded, as immutable tables are loaded
  and faulted during initialization rather than timed gameplay.
- **Disposition:** **retained design proof**, not a production checkpoint. The final shape has a
  credible material gain and bounded per-environment state. A standalone scalar pose cache remains
  disallowed; production item 6 is inseparable from its item 8–10 AoSoA/fixed-phase consumer cut.
- **Next:** execute queue item 7's independent strict-O3 owner audit, then perform the combined
  item 6/8–10 production cutover.

### 2026-07-18 — rejected `lb_00B0.c` strict-O3 owner

- **Scope:** complete `melee/lb/lb_00B0.c` translation unit under the existing strict release O3
  flags; no source changes or unsafe math.
- **Hypothesis:** optimizing the high-frequency SRT copy/blend and point-transform owner would
  remove material call/math overhead without changing exact float output.
- **Evidence:** adjacent 512 measurements were 85,566 baseline / 90,348 candidate, then 84,362
  baseline / 92,144 candidate (+5.6% and +9.2%); 256 candidate was 67,849. Digests stayed exact in
  the benchmark. The full release replay gate, however, produced 63 pass / 89 classified / 1 fail:
  `ExpertWorthlessFinch.slpz` gained an unclassified item-position float drift and a changed output
  lock (`b2c640a35d692ea4` to `1e712ef9bfb8f54e`).
- **Disposition:** **rejected**. The whole TU is removed from the allowlist. No partial-TU flag,
  arithmetic workaround, or classification change is retained.
- **Next:** test the independently measured `ftaction.c` action-animation callback owner.

### 2026-07-18 — rejected `ftaction.c` strict-O3 owner

- **Scope:** complete action-state animation callback translation unit under strict O3.
- **Hypothesis:** the measured callback owner would benefit materially without changing state.
- **Evidence:** exact 512 digest, but 84,547 FPS versus the adjacent 84,362 baseline and below the
  85,156 retained result. The difference is noise-level and does not justify another allowlist
  owner or a full-gate run.
- **Disposition:** **rejected** and removed completely.
- **Next:** test `ft_081B.c`, the remaining measured fighter ECB/collision callback owner.

### 2026-07-18 — rejected `ft_081B.c` strict-O3 owner

- **Scope:** complete fighter ECB/collision callback translation unit under strict O3.
- **Hypothesis:** the measured callback and repeated ECB helpers would benefit materially.
- **Evidence:** exact 512 digest, but 83,189 FPS versus the 84,362 adjacent baseline (-1.4%).
- **Disposition:** **rejected** and removed completely.
- **Next:** test `fighter.c`, the only remaining large O0 TU that owns the measured ProcessHit and
  fighter-publication boundary; reject it immediately on exactness or throughput failure.

### 2026-07-18 — rejected `fighter.c`; compiler audit complete

- **Scope:** complete fighter construction/process/publication TU under strict O3.
- **Hypothesis:** the remaining large O0 owner might optimize the measured ProcessHit boundary.
- **Evidence:** exact 512 digest, but 84,104 FPS versus the 84,362 adjacent baseline and below the
  85,156 retained result. Together with the prior historical rejections (`gobj.c`, `ftdynamics.c`,
  context, native DAT), no remaining O0 owner has both material profile evidence and a qualifying
  compiler-only result.
- **Disposition:** **rejected** and removed. Queue item 7 closes with no runtime or allowlist change.
- **Next:** execute the approved combined item 6/8–10 compact-state, fixed-phase, and homogeneous
  kernel cutover.
