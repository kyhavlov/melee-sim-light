# Performance campaign journal — 2026-08-03, second checkpoint

This journal preserves the complete experiment record between checkpoint `ee32fbc1` and the
second retained >=5% throughput checkpoint. Labels describe historical state; only the final
retained checkpoint remains in the runtime.

## Contract and current control

- Branch: `perf/decomp-throughput`; do not switch branches or create a worktree.
- Current committed checkpoint: `ee32fbc1`; its qualifying controlled evidence and complete
  experiment history are in `agent_docs/performance/JOURNAL_2026-08-03.md`.
- Frozen release binary: `build/melee_core/perf-control-ee32fbc1/replay-bench`,
  SHA-256 `461dd9abaafb6205328fcfa0e0c113bc0c4bec277846559becdd8a57b54d8efd`.
- Resident-256 candidate median: 38,882.2 cycles/frame and 110,383 FPS, digest
  `bdff41cf74a54850`.
- Resident-512 candidate median: 37,318.4 cycles/frame and 115,009 FPS, digest
  `ee9d93c545aa3ef9`.
- Final target: at least 150,000 controlled median FPS at both resident sizes, with exact output
  and no gameplay allocation or unexplained memory growth.
- A later checkpoint requires at least +5% controlled throughput over this checkpoint at both
  sizes, using alternating parent/candidate cycle ratios and supporting wall FPS.

## Next owner selection

- The final 32,768-frame resident-512 profile assigns 14.62% to pose animation, 13.52% to stage
  collision, 7.02% to fighter dynamics, 4.65% to input/action, and 4.17% to camera.
- The narrow pose-blend and exact scalar alternatives exercised in the August 3 journal are
  closed. Select a bounded source owner whose work can actually be deleted; do not add another
  gathering, compatibility, or duplicated representation layer.
- 2026-08-03 — `audit`: system `perf` sampling is unavailable (`perf_event_paranoid=4`), so use
  the existing bounded source-level profiler. Measure the compact main-pose interpreter as
  non-overlapping clock/loop, dense-table publication, decoder, and completion work before changing
  representation. The hypothesis is that ordinary action Figa nodes share one source tree clock
  while the current per-node interpreter repeats timing/control work and reads node-major samples
  from a large immutable table. Do not transpose or add playback state until call counts, shared
  clock invariants, and the actual table/decoder cycle split establish enough removable cost.
- 2026-08-03 — `open`: transpose the existing immutable compiled-Figa values from node-major
  storage to one frame-major row per program. Final owner is `MslFighterPoseProgram`, which owns
  the row base/stride; each `MslFighterPoseProgramNode` owns only its offset within that row.
  Canonical mutable state remains the existing per-node clock/decoder and canonical JObj SRT.
  `publish_program_sample` is the only consumer. The candidate replaces the old value layout
  completely—no duplicate table, Match state, runtime allocation, fallback, or evaluator is added.
  A 32,768-frame resident-512 census finds 1,922,947 table hits among 2,184,225 interpreted nodes.
  The 1,618,766 ordinary main-pose Figa nodes have zero clock, program, or program-order mismatch,
  so their current-frame values can be consumed in the same traversal as a contiguous immutable
  row. Retain only with exact output and controlled gains at both resident sizes.
- 2026-08-03 — `retained, below checkpoint`: the frame-major table preserves digests
  `a00e2d90ed375c84` / `9c1f37ab084f8ef2`. Two 131,072-frame reversals against frozen
  `ee32fbc1` improve paired throughput by 0.52%/0.82% at resident 256 and 1.03%/1.67% at resident
  512. The same values replace the node-major table with no Match state, allocation, duplicate
  representation, or runtime branch. Keep it uncommitted while accumulating the next checkpoint.
- 2026-08-03 — `rejected implementation, hypothesis remains open`: copying the first active Figa
  node's frame/rewind state into later nodes inside the existing per-node interpreter does not
  remove enough work to justify its branch and duplicated animation loop. Short screens suggested
  +0.22%/+1.16%, but 262,144-frame reversals ranged from a 2.25% loss to an outlier-driven 8.74%
  win at resident 256 and from a 0.22% to 5.16% loss at resident 512. The wrapper is removed. This
  does not reject one canonical program clock or contiguous row publication; a useful version must
  delete the repeated per-node interpreter/state work instead of copying it.
- 2026-08-03 — `closed stage leaf`: a 32,768-frame resident-512 owner profile records 69,744
  `mpColl_LoadECB_JObj` calls. Its exact origin transform accounts for 79.0M cycles: 45.8M matrix
  setup, 17.6M topology scan, 13.1M exact wide trig, and 1.4M output copies. The retained compact
  ECB topology already removes sparse queries; unit-scale, inlined concat, eager matrices, and
  batching variants are historically neutral or slower. Do not retry this leaf without a way to
  delete demanded matrix arithmetic or a consumer boundary wider than the rejected batching seam.
- 2026-08-03 — `rejected`: 482,848 dense publications are dominated by mask `0x00E` (63.31%),
  `0x0EE` (11.86%), and `0x00C` (5.99%). Exact 64-bit copies reduce scalar instructions for
  adjacent fields but shift the traffic from vector to integer pipelines. A reversed 131,072-frame
  screen preserves both digests yet regresses the frame-major candidate by 0.75% at resident 256
  and 1.20% at resident 512. The packed copies and instrumentation are removed completely.
- 2026-08-03 — `rejected implementation, deletion remains open`: in the canonical fighter-part
  pose span, let direct Figa followers whose complete pre-clock state and program identity match a
  preceding selected node consume that
  node's already-computed post-clock/sample result. Each joint's existing clock remains canonical;
  divergent, fractional, decoder-owned, singular, and general-tree animation stays on the one
  interpreter. The change must keep the dense publisher at one compiler callsite and delete the
  repeated frame/loop/table-admission state machine without adding Match state, allocation, a
  fallback representation, or an unchecked group invariant. The first form passed both digest
  screens but routed followers through the same 5 KiB interpreter and enlarged its prologue and
  caller; it regressed short resident-256/512 screens by 2.6%/7.2% and is removed. Complete the
  ordinary interior-frame case before entering that interpreter instead, sharing one factored
  common-mask publisher and leaving boundary/decoder ownership solely in the generic path. That
  refinement also preserves both digests, but after limiting it to the fighter-part span it loses
  1.06%/1.42% in reversed resident-256/512 screens. The common helper and fast path are removed.
  Scalar timing/publisher duplication is now closed; revisit the clock only with one canonical
  program owner that removes per-node clock state and the generic interpreter from the hot span.
- 2026-08-03 — `rejected`: color consecutive native batch arenas by adding one unmapped-use padding
  page to the physical lane stride. Every Match retains the same 3 MiB capacity, byte layout,
  relocation tokens, allocation count, and snapshot; only the distance between independent lanes
  changes from an exact multiple of the private-cache geometry. The mapping owner and batch binder
  are the only consumers. Retain the 4 KiB-per-lane address-space/memory cost only if both resident
  sizes preserve exact output and improve under controlled reversal. Digests remain exact, but a
  reversed 131,072-frame screen improves resident 256 by 1.03% and regresses resident 512 by
  3.43%, including a losing candidate/control adjacency before the final contended sample. The
  stride API and padding are removed; fixed-offset cache coloring is not a stable scale win.
- 2026-08-03 — `closed dynamics leaf`: a 32,768-frame resident-512 owner profile assigns 102.4M
  cycles (6.16% total) to the retained fused dynamics solve and only 9.7M (0.58%) to the hosted
  post-solve matrix refresh required for exact next-frame raw-matrix re-anchoring. Folding that
  publication into the solver cannot remove enough total cost to justify coupling two distinct
  source phases, and the solver's exact constraint/orientation math remains the dominant demanded
  work. The instrumentation is removed; do not revisit the refresh without a wider canonical
  product boundary that also deletes material consumer work.
- 2026-08-03 — `open`: replace the AVX-512 exact trig kernel's two indexed quadrant-table gathers
  with register-generated `+0/+1/-1` factors derived from the already-computed quadrant bits.
  `msl_sincosf_many` remains the sole owner; its inputs, output arrays, reduction, polynomial
  operation order, scalar/PPC paths, and consumers are unchanged. This deletes two gathers per
  16 angles without adding state, lookup data, approximation, or a second evaluator. Retain only
  if both production digests and controlled resident-256/512 comparisons improve.
- 2026-08-03 — `retained candidate, below checkpoint`: register masks reproduce the quadrant table
  exactly and remove both `vgatherdps` instructions. Three 131,072-frame reversals against the
  frame-major-only binary preserve both digests and give median paired gains of 0.28% at resident
  256 and 1.06% at resident 512. A two-`vpermps` refinement is mixed/neutral against that form and
  is removed. Keep the simpler mask form uncommitted while accumulating a qualifying checkpoint.
- 2026-08-03 — `open`: make the existing compiled-Figa interpreter's steady table state enter its
  singular publisher directly. The previous early-path experiment failed because a second
  publisher callsite made GCC outline the 1.6 KiB publisher and enlarged every hot call. This
  refinement uses `last_table_frame`—already canonical decoder-transition state—to prove the next
  exact integer frame, joins the one existing publisher label, and leaves first-play, loop/end,
  fractional, descriptor, decoder, callback, and publication ownership unchanged. It adds no
  state or alternate evaluator. Retain only if disassembly keeps the publisher singular, both
  digests remain exact, and both resident sizes improve.
- 2026-08-03 — `rejected`: the singular-publisher direct entry preserves both digests but grows
  `interpret_joint` from 0x121b to 0x13b5 bytes and is mixed in short reversals. Factoring the
  decoder without attributes gives the intended 0xb37-byte steady owner plus an 0x892-byte decoder
  leaf, but longer 262,144-frame resident-256 reversals are one 0.91% win, one 0.20% win, and one
  10.7% system outlier loss; shorter resident-512 reversals likewise change sign. The few repeated
  scalar clock branches are not a stable whole-simulator win, and the extra external code boundary
  is removed. This closes only the per-node shortcut: any program-clock revisit must delete the
  per-node interpreter/state across the admitted span and publish the contiguous row as one owned
  operation.
- 2026-08-03 — `open structural packet`: make each construction-proven direct Figa tree span own
  one animation clock. Final owner is a compact `MslFighterPoseSpan` bound by `request_tree` after
  attachment; its leader's existing frame/rate/end/rewind/flags are the canonical mutable clock.
  Member nodes retain only their already-required decoder cursors, program-node identity, filtered
  publication bit, JObj SRT, and RObj work; their per-node clock fields are dormant while bound and
  are materialized only when an individual-node/general-tree operation explicitly dissolves the
  span. Consumers are fighter-part animation and the existing pose tree/joint query and mutation
  APIs. Displaced work is the repeated per-node frame/loop/end admission, program lookup, sample-row
  calculation, and synchronized clock updates. The deletion boundary is direct same-program Figa
  spans only; descriptor animations, mixed/non-direct trees, individually mutated trees, PPC/Wasm,
  and decoder track ownership remain on the existing per-node representation. There is no runtime
  allocation, fallback dispatch, approximate math, second clock, or persistent compatibility
  bridge. Retain only if the complete ownership transition is exact and controlled resident-256
  and resident-512 throughput materially improves.
- 2026-08-03 — `rejected after completed deletion`: the span clock covers 407,553 group frames and
  10,856,771 node publications in the 32,768-frame resident-512 census with exact output. The first
  complete form removes repeated per-node clocks but remains only +0.42% at resident 256 and
  neutral at 512. The final compact form additionally moves steady membership to a 16-byte record,
  owns table/decoder state once per span, and does not touch the 56-byte mutable node descriptor on
  table frames. Two 131,072-frame reversals are controlled-neutral: resident-256 changes +0.08%
  then -0.05%; resident-512 changes -0.03% and -0.18%. This proves the demanded scattered JObj SRT
  publication dominates this boundary; clock, program-row arithmetic, and mutable-descriptor
  traversal are not the missing scale gain. Remove the span representation, membership storage,
  publisher boundary, query/mutation integration, and decoder factoring completely. The rebuilt
  source again matches frozen pre-packet binary SHA-256
  `56be3fd5679d127ca9b1ea08b1ab0376db698798294187306e04b8af0b06d276`.
- 2026-08-03 — `profile`: diagnostic-only callback identity attribution assigns 6.81% of the
  contract to action-animation callbacks. The guard family remains the known demanded pose owner
  (`ftCo_Guard_Anim` 1.93%, `ftCo_GuardOn_Anim` 1.11%, reflect 0.33%); its wider exact fusion and
  blend alternatives are already closed in the history. Simple frame-completion callbacks expose a
  different repeated lookup boundary: Wait is 0.82%, Fall 0.66%, Jump 0.39%, and many smaller
  states call `ftAnim_IsFramesRemaining`, which currently walks sparse `FighterBone` entries and
  resolves each JObj back to its compact pose node.
- 2026-08-03 — `open`: make native `ftAnim_IsFramesRemaining` query the existing canonical compact
  pose preorder directly while applying the same source-part `flags_b0/flags_b5` admission. Final
  owner remains `MslFighterPoseJoint` plus its construction-bound `source_part_index`; the consumer
  is the single fighter frame-completion API. Displace only the duplicate sparse FighterBone/JObj/
  AObj lookup walk. Add no state, cache, action list, approximation, or allocation; PPC retains the
  source traversal. Retain only with exact output and gains at both resident sizes.
- 2026-08-03 — `rejected`: binding the interpolation tree's existing source-part identities makes
  the direct compact query exact, but two 131,072-frame reversals are only +0.20%/+0.33% at
  resident 256 and +0.10%/-0.60% at resident 512. The sparse source walk normally finds an active
  joint early; replacing its few lookups with a complete compact scan is not a scale win. Remove
  the query API, interpolation binding, and call-site branch completely.
- 2026-08-03 — `open`: revisit the historical exact-zero trig observation only at the existing
  AVX-512 block owner: if all active input bit patterns in a block are positive zero, publish exact
  positive-zero sine and one cosine before range reduction. Unlike the rejected scalar per-axis
  branch, this adds one test per existing wide block and removes its complete reduction/factor path.
  Inputs, output order, nonzero lanes, tails, scalar/PPC/Wasm paths, and math remain unchanged.
- 2026-08-03 — `rejected`: the block-zero exit preserves both digests but is effectively neutral
  in the first reversed screen (+0.03% at resident 256 and +0.29% at 512). Existing batches mix
  zero and nonzero Euler lanes, so the block test rarely deletes work and is removed rather than
  repeating the historical per-axis branch with a smaller ceiling.
- 2026-08-03 — `rejected`: sharing the side-effect-free current-animation-frame query inside
  `ftAnim_8006E9B4` is exact, but two 131,072-frame reversals are +0.77%/-0.13% at resident 256
  and +0.11%/-0.10% at 512. The second query exists only for a flagged subset and is too small to
  survive code-layout noise, so the source order is restored.
- 2026-08-03 — `closed map leaf`: attribute the two canonical root-position publications surrounding
  every fighter map callback. Final owner remains the fighter root JObj and its already-canonical
  matrix state; consumers are the immediate ECB/map callback and later contact phases. The
  existing hosted setter incrementally shifts every clean ordinary descendant after a translation
  change. Measure the pre-map and post-map calls separately before considering a smaller exact
  publication boundary; do not add cached position, a second matrix representation, or deferred
  semantics merely to avoid the traversal. The 65,536-frame resident-512 profile records 143,360
  calls at each boundary: the pre-map setter is 7.16M cycles (0.23% of the contract) and the
  post-map setter is 5.46M (0.18%). The complete repeated publication is only 0.41%; remove the
  timers and do not pursue a new matrix owner at this leaf.
- 2026-08-03 — `rejected`: share exact camera frustum-rotation coefficients. Final owner is
  `Camera_8002A768`; canonical state remains its four transient corner vectors and the unchanged
  camera transform. The only consumer is the existing corner construction. Each frame currently
  evaluates the private `lbVector` sine/cosine polynomials twelve times even though it uses only
  four distinct input angles (`half_fov`, `-half_fov`, pitch, and yaw). Compute each exact pair
  once and feed the same scalar rotation operation in the same corner/axis order. Displace only the
  eight duplicate coefficient evaluations; keep the generic `lbVector_Rotate`, PPC path, vector
  normalization, overlap classification, state, and allocation unchanged. Both the direct cached
  form and a single optimized frustum helper preserve the two production digests. The direct form
  grows `Camera_8002A768` from 0xA83 to 0xE4E bytes and loses at both sizes. The compact caller plus
  one helper reduces the camera function to 0x8D8 bytes, but its helper is 0xBCB bytes; a
  131,072-frame reversal changes resident-256 cost from 38,207.4 to 38,249.5 cycles/frame
  (-0.11%) and resident-512 from 36,655.2 to 36,515.3 (+0.38%). The deletion is not stable at both
  sizes and is far below its code-size cost, so remove the helper, API, and all call-site changes.
- 2026-08-03 — `superseded and removed`: make the sealed native-DAT archive cache
  searchable instead of scanning
  every translated archive on each fighter motion change. Final owner remains
  `MslNativeDatContext::archive_cache`; canonical entries and their `HSD_Archive` values do not
  change. Sort the complete cache once when native initialization seals it, then use an exact
  `(source, file_size)` binary lookup during gameplay. This displaces only the current linear
  cache scan (about 5.0M instructions and 827k simulated D1 read misses in the 4,096-frame
  resident-512 callgrind window). Initialization keeps its existing linear lookup while the cache
  is still growing. Consumers remain `HSD_ArchiveParse` motion-data loads; there is no new state,
  allocation, hash table, fallback representation, or output change. Both 32,768-frame digest
  screens are exact. Three 131,072-frame reversals against the frozen frame-major/trig-mask
  candidate reduce median cost from 38,130.0 to 37,892.7 cycles/frame at resident 256 (+0.69%
  median paired throughput) and from 36,635.7 to 36,419.4 at resident 512 (+0.85% median paired
  throughput). The later direct motion-program token removes every reached gameplay parse instead
  of accelerating the archive lookup, so the sort, binary search, and helper are removed rather
  than retained as dead layered machinery.
- 2026-08-03 — `rejected`: shrink the singular compact fighter-pose node from 56 to 48 bytes without
  splitting its state. Final owner remains `MslFighterPoseJoint`; canonical clocks, decoder state,
  topology, JObj/path pointers, and consumers are unchanged. Use the otherwise-unused high AObj
  flag bit as the hosted-pose ownership marker instead of a separate 32-bit magic field, and order
  the existing members without tail padding. This displaces only the magic word and padding;
  unlike the rejected split-hot-state layouts, it adds no array, lookup, synchronization, or
  consumer branch. The full node remains one contiguous allocation and saves eight bytes for every
  admitted joint. Output remains exact, but two 131,072-frame reversals against the archive-cache
  candidate change sign at resident 256 (+0.34% then -0.35%) and give only +0.13%/+0.61% at
  resident 512. That does not justify repurposing an AObj flag bit; restore the explicit magic and
  original 56-byte node. This agrees with the earlier split-hot-state result: descriptor layout is
  not the remaining scale boundary.
- 2026-08-03 — `rejected`: simplify the exact AVX-512 trig quadrant publication after range reduction.
  Final owner remains `msl_sincosf_many`; inputs, reduction, polynomial operation order, output
  arrays, scalar/PPC/Wasm paths, and consumers remain unchanged. The current kernel materializes
  sparse `0/+1/-1` factor vectors, then multiplies polynomial outputs by them. Select sine/cosine
  magnitudes by odd/even quadrant and flip their sign bits from the already-computed quadrant masks
  instead. The same exact construction covers the small-angle path and deletes factor generation
  plus redundant multiplies. Digests remain exact, but two 131,072-frame reversals change sign at
  resident 256 (+0.42% then -0.30%) and lose 0.13%/0.41% at resident 512. The compiler's existing
  masked factor form is at least as good in the complete runtime, so restore it and keep only the
  earlier proven gather deletion.
- 2026-08-03 — `rejected`: place native `HSD_GObj::user_data` in the first cache line with the object
  classifier and process-link metadata. `HSD_GObj` remains the singular canonical object; every
  field, consumer, pointer, list, allocation, and mutation remains unchanged. On 64-bit hosts the
  source-order expansion puts `user_data` at byte 72, so nearly every fighter/item/stage callback
  reaches a second line immediately after the scheduler reads `p_link`. Reorder existing fields in
  the hosted-only representation so `user_data`, `next`, and `hsd_obj` share the first line; PPC
  keeps the retail layout. This displaces no behavior and adds no state, lookup, or allocation.
  Generated relocation metadata follows the actual type layout. Digests remain exact, but two
  131,072-frame reversals lose 0.08%/0.16% at resident 256 and 0.27%/0.01% at resident 512. The
  second line is already warm enough through repeated per-object callbacks that reordering does not
  delete a meaningful miss boundary. Restore the source-shaped layout completely.
- 2026-08-03 — `rejected`: compact the singular native `HSD_JObj` and put its canonical SRT/dirty
  fields together before its matrix and cold graph pointers. Every field, pointer, consumer, and
  operation remains unchanged; generated relocation metadata follows the actual layout, and PPC
  retains the retail structure. The ordinary 64-bit source expansion is 184 bytes and makes every
  dense pose publication span two cache lines before any matrix demand. The hosted-only order is
  176 bytes: object header plus quaternion/scale/translation/flags/id occupy the first 64 bytes,
  the matrix follows, and graph/animation pointers remain singular afterward. This differs from
  the rejected 192-byte, 64-byte-aligned JObj experiment: it adds no alignment padding or arena
  growth and instead removes eight bytes per JObj. Retain only with exact outputs, save/restore
  integrity, and controlled gains at both resident sizes. Digests remain exact, but two
  131,072-frame reversals lose 0.31%/0.02% at resident 256 and 0.70%/0.75% at resident 512. The
  source order evidently serves the mixed matrix/topology consumers better than an SRT-first
  order. Restore the JObj layout and remove the temporary code-generator exception completely.
- 2026-08-03 — `rejected`: make each source Figa attachment traversal resolve its compiled program
  once and consume that program's already-canonical node order directly. Final owners remain
  `ftAnim_8006F4C8`/partial/remapped variants for source part admission and
  `MslFighterPoseProgramNode` for compiled track identity. Canonical mutable state remains the
  existing pose node/JObj; filtered admission, part order, tree flags, decoder transition, and
  attachment lifetime do not change. Displace the repeated per-attached-joint program hash,
  track-pointer subtraction, track-node map lookup, and associated assertions. The caller advances
  one local compiled-node cursor exactly when it advances the source Figa node cursor. Add no state,
  table, cache, allocation, or fallback representation. The completed cut also deletes the now-dead
  261,195-entry per-track node map (522,390 bytes) and all of its compiler/allocation bookkeeping.
  Both digest screens remain exact, but three 131,072-frame resident-256 reversals are mixed
  (-0.74%, +0.59%, and -0.04% paired throughput), while resident-512 likewise changes sign
  (+0.35%, -0.16%) before a third candidate run loses 1.85% to its adjacent control. The complete
  lookup deletion is not a stable runtime gain and the source traversals become more complex, so
  restore the original attachment API and map. The rebuilt retained source is byte-identical to the
  frozen pre-experiment binary, SHA-256
  `cbcbf7ae2c19cb51a5968d3d191bec0fc27960188e5de1816485aea312b0219f`.
- 2026-08-03 — `retained candidate, below checkpoint`: bind each native fighter motion record
  directly to its existing compiled
  Figa program after exhaustive GameData preload. Final owner is the record's existing 32-bit
  `x14` token: during source initialization it remains the raw subarchive token; before GameData is
  sealed it is replaced in place by a tagged compiled-program index. Canonical trees and samples
  remain `MslFighterPoseProgram`; consumers are the two source motion loaders. Displace their
  gameplay-time raw-token translation, archive-cache search, `HSD_Archive` copy, and public-symbol
  lookup. The initialization parse remains the only construction path and PPC remains unchanged.
  Add no table, Match state, allocation, fallback representation, or approximate behavior. This is
  a complete native motion-binding cut, not another archive-cache layer. Both production digest
  screens remain exact. Three 131,072-frame reversals against the superseded sorted-cache candidate
  give median paired gains of 0.95% at resident 256 and 0.52% at resident 512. Removing the now-dead
  cache sorting retains the gain: two 65,536-frame resident-256 pairs improve 0.86%/1.01%, and two
  131,072-frame resident-512 pairs improve 0.66%/0.32% (one shorter screen is -0.38%). Against
  committed `ee32fbc1`, the complete current candidate is now about 4.6--4.9% faster at resident
  256 and 4.3--4.6% at resident 512, still short of the required checkpoint at both sizes.
- 2026-08-03 — `rejected refinement`: replacing each motion record's initialization-only symbol
  pointer with the resolved Figa tree avoids the compact program-index lookup, but two
  131,072-frame resident-256 reversals lose 0.39% and 1.01%; resident 512 is neutral. Restore the
  program-owned tree lookup and leave the record's source-shaped `x0` field unchanged.
- 2026-08-03 — `retained candidate, below checkpoint`: resolve `ftCo_800A2040`'s side-effect-free
  CPU-input ownership predicate
  once inside `Fighter_Spaghetti_8006AD10` instead of repeating the same player-slot and fighter
  state lookup three times before any owning field can change. Final state and consumer remain the
  source input owner; displace only the two duplicate queries, with no cache, new state, changed
  ordering, or hosted-only policy. Digests remain exact. Two 131,072-frame reversals improve
  paired throughput by 0.88%/0.35% at resident 256 and 0.45%/0.15% at resident 512. The two-query
  deletion is small but consistently positive and leaves one local Boolean, so retain it while
  accumulating the checkpoint.
- 2026-08-03 — `rejected`: make the hosted full-hurt-capsule publication gate consume the existing
  match-owned follower axis instead of rescanning every live fighter and resolving its player
  input owner for every fighter publication. `MslCoreMatch::follower_fighters` is already the
  canonical list of independently simulated Nana entities, and the supported runtime creates all
  leaders as human-controlled; therefore a non-NULL follower is exactly the condition under which
  the CPU input tree can consume the full capsule spans. Final state remains the existing follower
  pointers, and the sole consumer remains `Fighter_8006D9AC`. Displace only the repeated GObj-list,
  fighter-state, and player-owner walk; add no derived bit, character proxy, allocation, fallback,
  or changed publication order. Digests remain exact, but two 131,072-frame reversals change sign
  at resident 256 (-1.08%, +0.87%) and both lose at resident 512 (-0.26%, -0.18%). The live list
  is already hot and the smaller scan does not survive code-layout/system noise; restore the
  source-owned predicate rather than encode the supported-domain follower invariant here.
- 2026-08-03 — `profile`: temporary collision-callback identity attribution confirms that the
  10.92% instrumented stage-collision bucket is fragmented across ordinary action callbacks whose
  work converges in the shared map owner: Landing is 1.08% of the complete contract, AttackAir
  0.88%, DamageFly 0.74%, Wait 0.63%, Dash 0.60%, Jump 0.58%, and every other collision callback
  is below 0.5%. This agrees with the historical function-level result that
  `mpColl_LoadECB_JObj`, not an action wrapper or remaining line-query leaf, is the dominant
  collision cost. The attribution is removed. Do not specialize action collision callbacks; a
  useful stage packet must change the demanded ECB matrix/product boundary itself.
- 2026-08-03 — `checkpoint screen, below threshold`: three alternating 262,144-frame CPU-0 pairs
  against frozen `ee32fbc1` preserve digests `bdff41cf74a54850` / `ee9d93c545aa3ef9` but do not
  meet the required +5% at both sizes. Resident-256 control/candidate cycles per frame are
  `38,885.3/37,586.2`, `39,119.6/37,700.2`, and `39,013.2/37,444.5` (+3.46%, +3.77%, +4.19%
  paired throughput). Resident-512 pairs are `37,609.4/35,620.8`, `37,417.5/35,703.2`, and
  `37,367.3/35,697.6` (+5.58%, +4.80%, +4.68%). Candidate median observed FPS is 114,189 at 256
  and 120,231 at 512. Keep the exact candidate uncommitted and accumulate a real additional gain.
- 2026-08-03 — `rejected`: delay compiled-sample translation filtering until a node actually owns
  translation tracks. Final owners remain the immutable program-node type mask and the existing
  per-attachment filtered bit; canonical JObj SRT, sample values, and publication order are
  unchanged. The dominant rotation-only masks (`0x00E` alone is 63.31% of dense publications)
  cannot observe translation filtering, yet currently pay its branch and mask operation before
  the specialized publisher. Displace only that irrelevant admission work; add no state, table,
  approximation, fallback, or new publisher. Digests remain exact, but two 131,072-frame
  reversals lose 0.78%/1.36% at resident 256 and 1.73%/0.95% at resident 512. Moving this branch
  below the specialized switch makes the large publisher's hot code layout worse than the trivial
  admission it deletes; restore the original ordering completely.
- 2026-08-03 — `rejected before timing`: complete the native motion-token cut by separating initialization-time raw
  archive resolution from gameplay-time compiled-program resolution. Final state remains the
  tagged `Fighter_WaitAnimData::x14` token and final owner remains `MslFighterPosePrograms`.
  Exhaustive GameData binding already proves every nonzero gameplay token is compiled, so the hot
  resolver should not retain or branch into the displaced archive-parse fallback. Construction
  keeps one private raw resolver used only before token replacement. Add no state, allocation,
  compatibility mode, or changed lookup. The attempted deletion fails during the first preload
  `Fighter_Create`: pose programs cannot be compiled and tagged until exhaustive fighter preload
  has exposed every translated Figa tree, while that preload itself resolves initial motion trees
  through this function. The raw branch is construction-live, not a gameplay fallback. Reordering
  would require a second discovery/loading mechanism solely to remove one correctly predicted hot
  branch, so restore the singular resolver and do not broaden this leaf.
- 2026-08-03 — `rejected after completed deletion`: consolidate native static-ground collision-epoch
  publication. Final owner is `MslGroundState::headless_epoch_increment`, a construction-only count
  occupying existing alignment padding. Canonical gameplay state remains the one source
  `mpColl_804D64AC` epoch; consumers remain the later fighter/item collision queries. Supported
  static ground objects have no `x8_callback`, so their separate priority-1 processes do nothing
  except increment that same epoch before collision priority. Retain the first process as the sole
  publisher, add the exact number of displaced increments there, and delete the remaining static
  animation/epoch processes. Animated/moving ground processes, stage-specific processes, epoch
  value, wrap behavior within supported match durations, and collision ordering remain unchanged.
  The deletion boundary is native supported-stage construction only; PPC/Wasm keep the source
  schedule. Add no gameplay allocation, alternate epoch, fallback, or per-frame scan. Both digest
  screens remain exact, but two 131,072-frame reversals lose 0.22%/0.24% at resident 256 and
  change sign at resident 512 (-0.63%, +1.25%). The 1.14% diagnostic owner share substantially
  included per-callback timing overhead; the actual trivial scheduler entries are below a stable
  whole-runtime point, and deleting their process records perturbs layout enough to erase the
  theoretical saving. Restore the source-shaped static epoch schedule and remove the count/API.
- 2026-08-03 — `rejected`: make the compact ECB origin producer publish the six demanded X/Y lanes
  directly and reduce their bounds in one native vector operation. Final owners remain the
  canonical JObj matrices and `mpColl_LoadECB_JObj`; the product is still ephemeral inside that
  single call. The existing producer copies unused Z for every origin, then the consumer performs
  ten scalar min/max branch chains after identical float subtractions. Replace that interface with
  padded eight-lane X/Y arrays, perform the same per-lane subtractions, and reduce with ordered
  compare/select trees that preserve the earliest operand on equality. Any NaN takes the original
  sequential scalar reduction, preserving its non-associative behavior. Matrix construction,
  origin order, ECB formulas, PPC/Wasm path, state, and allocation do not change. This is a
  complete producer/consumer cut, not a cached ECB. Both 32,768-frame digests remain exact. Two
  131,072-frame resident-256 reversals improve only 0.64% and 0.33%. Resident-512 changes sign:
  candidate/control pairs lose 0.97% and 0.52%, while the reverse-order middle pair improves
  0.49%. The vector reduction adds shuffle/blend pressure beside the demanded matrix work and is
  not stable across resident sizes. Restore the original `Vec3[6]` producer and scalar ordered
  reduction completely; the wider matrix/product boundary remains the only plausible stage-collision
  target.
- 2026-08-03 — `diagnostic unavailable`: native-ISA Callgrind cannot decode an emitted instruction
  on this host's AVX-512 build. A profiler-only x86-64-v3 build ran but emitted no measured stats
  through the benchmark's client requests, so no ownership inference is drawn and no production
  flag or source change is retained from that attempt.
- 2026-08-03 — `profile`: native `gprofng` clock sampling is also unavailable under the host's
  profiling restrictions: the bounded runs complete normally but expose no PC-time metric. A
  disposable source-timed 32,768-frame resident-512 profile instead assigns 80.3M cycles to
  JObj ECB construction and 102.3M to the collision driver, including 86.5M in its query callback;
  the driver averages only 1.015 iterations. Ground collision (`mpColl_8004ACE4`, 57.1M) and air
  collision (`mpColl_80046904`, 32.5M) dominate the query.
- 2026-08-03 — `retained candidate, below checkpoint`: in both dominant query callbacks, the source deliberately repeats the
  left/right wall checks after an initial wall response so correcting one side can reveal the
  other. When both first checks report no possible wall, neither changes position or wall scratch
  state, and the second pair repeats identical broad- and narrow-phase work. Preserve the source
  second pass whenever either first check succeeds; skip it only after two negative checks. Final
  state remains `CollData` plus the existing mpColl scratch owner, with no cache, new state,
  approximation, allocation, or action specialization. Both production-prefix digests remain
  exact. Two 131,072-frame reversals improve paired throughput by 0.65%/0.94% at resident 256 and
  0.92%/0.62% at resident 512 against the frozen pre-change candidate. Keep the single shared
  negative-wall gate uncommitted while accumulating the checkpoint.
- 2026-08-03 — `rejected refinement`: sharing one joint traversal for the initial left/right wall
  broad phases preserves both digests but does not improve the retained negative-wall gate.
  Resident-256 reversals change sign (-0.21%, +0.46%), and resident-512 loses both (-0.36%,
  -0.06%). Per-kind line-range checks remain demanded, while the extra result mask and inner/public
  split enlarge the already-large collision owner. Remove the paired helper, alternate entry
  points, and disposable build ablation completely.


- 2026-08-03 — `retained checkpoint`: the final candidate combines the frame-major compiled-Figa
  value layout, exact register-generated AVX-512 trig quadrant factors, construction-time binding
  of native fighter motion records to compiled pose programs, one evaluation of the repeated CPU
  input-owner predicate, and an exact negative fast path around the duplicated second left/right
  wall pass in the air and ground collision drivers. Three 262,144-frame alternating pairs against
  frozen committed control `ee32fbc1` preserve digests `bdff41cf74a54850` /
  `ee9d93c545aa3ef9`. Resident-256 control/candidate cycles per frame are
  `38,873.5/37,104.3`, `38,922.1/36,891.6`, and `38,874.7/36,908.0`; the median paired
  throughput gain is +5.33%, and candidate median wall throughput is 116,288 FPS. Resident-512
  pairs are `37,212.4/35,267.5`, `37,598.0/35,327.1`, and `37,467.9/35,249.0`; the median
  paired throughput gain is +6.30% (+6.24% by ratio of raw medians), and candidate median wall
  throughput is 121,697 FPS. The final release SHA-256 is
  `d5de6ab2f3b66415f1cf15b129ea8d773098e2b5693015044feced5e3c6665e0`.
  `source-check`, native smoke, PPC smoke, the complete 3,501,461-frame optimized-release
  supported-domain suite, allocation/save-restore census, Wasm parity, and viewer smoke pass. The
  final bounded resident-512 profile is 45,307.3 diagnostic cycles/frame; pose animation remains
  14.23%, stage collision 12.75%, dynamics 7.25%, input/action 6.71%, and camera 4.32%. Match storage,
  savestate size, gameplay allocation, and output classifications are unchanged.
