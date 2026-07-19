# Active performance packet — shared compiled fighter animation programs

## Objective

Replace repeated per-environment Figa byte-stream decoding for ordinary fighter pose samples with
one immutable GameData-owned compiled program. The existing exact compact evaluator remains the
general evaluator for genuinely fractional, blended, path, or descriptor-driven samples; it is not
a compatibility runtime and must not remain on the ordinary integer Figa path.

This packet is worth retaining only if it removes a material portion of the current 17.71% pose
owner and improves the complete 512-environment contract by at least 8%. It must not add per-Match
sample caches or increase mutable savestate state.

## Final boundary

- **Final owner:** native DAT/GameData initialization compiles every supported Figa tree into an
  immutable, deterministic track program and exact ordinary-frame sample representation. Fighter
  pose attachment binds directly to that shared program.
- **Canonical mutable state:** the existing Match-owned joint SRT, animation frame/rate/flags,
  blend state, procedural path state, and exact general evaluator state. A sampled Figa joint may
  not own a second pose or per-Match cache.
- **Consumers:** root motion, hitbox/hurtbox/shield geometry, attachments, capture, IK, dynamics,
  ECB publication, viewer geometry, action-end callbacks, copy, and save/restore.
- **Displaced work:** ordinary integer Figa samples no longer parse packed keys, waits, fractions,
  and interpolation state separately in every Match. The direct shared program publishes the same
  SRT values and AObj end/rewind counts in source order.
- **Deletion boundary:** no ordinary Figa node enters `interpret_track`; the exact mutable decoder
  owns only legal non-unit-rate/fractional samples and is resynchronized from source state when
  returning from the immutable path. It is not a pose cache or synchronized side representation.
- **Exclusions:** no legacy extractor, Python hot path, replay-derived table, changed animation
  frame/rate, approximate interpolation, altered matrix arithmetic, scheduler interleaving, or
  observation reduction.

## Sequence

1. Census the current 512 workload by pose source, blend state, rate, fractional frame, track type,
   and byte-stream work. Measure the eligible whole-frame ceiling before structural edits.
2. Enumerate every translated Figa tree during GameData initialization and measure the exact shared
   table/program size. Reject dense full-pose samples if they are needlessly large; prefer compiled
   key segments plus a compact ordinary-frame index.
3. Prove one complete Figa program against the current evaluator over every integer and a bounded
   fractional sample set before changing runtime ownership.
4. Cut ordinary Figa attachment/evaluation to the shared program. Keep the exact mutable evaluator
   only as canonical state for legal nonordinary sampling; do not add a sampled-pose cache. Recover
   exact root motion, loop/end, blend, path, and save/restore inside that boundary.
5. Remove diagnostics. Require unchanged 256/512 digests, 63 PASS / 90 unchanged CLASSIFIED / zero
   failures, native API/copy/save-restore and sealed-allocation gates, plus PPC/Wasm/viewer gates.
6. Retain and commit only a repeatable adjacent 512 improvement of at least 8%, with 256 as the
   cache-pressure secondary result. Otherwise record the completed reason and remove the packet.

## Baseline

- Runtime: `0d2b44f2`
- 256: 58,192 FPS median, digest `8ef126a41244d514`
- 512: 54,202 FPS median, digest `6f91f23e3553a090`
- Current exact profile: pose animation 17.71%; map collision 19.84%; action animation callbacks
  7.72%; input/action callbacks 5.20%; remaining gameplay dynamics 6.14%.

## Log

- 2026-07-19 — `open`
  Scope: production workload eligibility and translated-Figa size census only.
  Hypothesis: ordinary unblended integer Figa sampling dominates the 17.71% pose owner and can use
  one shared compiled program without retaining a per-Match sample cache.
  Evidence: the exact committed profile attributes 1,021,575,725 net cycles to pose animation over
  146,845 calls, while action scripts, secondary pose, and capture pose total only 0.58%.
  Disposition: instrument the named owner; no runtime representation change is approved until the
  eligible cycle share and shared data size are measured.
  Next: collect the workload/source census and select the smallest exact compiled representation.
- 2026-07-19 — `retained`
  Scope: exact workload eligibility and complete translated-Figa size census.
  Hypothesis: integer Figa samples dominate pose cost and a shared dense ordinary-value index is
  small enough to be a practical GameData owner.
  Evidence: integer-rate/integer-frame Figa joints account for 835,993,960 of 1,006,309,005 timed
  joint cycles (83.1%); other Figa samples account for 14.7% and descriptor animation 2.2%. The
  supported native graph contains 1,683 Figa trees, 261,195 tracks, 8.32 MiB of packed streams, and
  10,721,046 dense track/frame values (40.90 MiB as floats before a validity bitset). Diagnostic
  output preserves digest `6f91f23e3553a090`.
  Disposition: the final shared program is viable. Use dense exact ordinary values plus stateless
  general evaluation from the same immutable source program; do not retain mutable Figa decoder
  state or a per-Match cache.
  Next: implement GameData compilation and prove its samples against the current evaluator before
  cutting Figa attachment.
- 2026-07-19 — `open`
  Scope: shared dense program proof and first production-path table cut.
  Hypothesis: publishing ordinary Figa values directly from the immutable table removes enough
  decoding to clear the 8% whole-frame target before deleting displaced mutable state.
  Evidence: exhaustive initialization compiled all 10,721,046 values; reached integer samples were
  checked bit-for-bit against current joint SRT under diagnostic execution. The first production
  checkpoint preserves digest `6f91f23e3553a090` and reaches 60,880 FPS at 512 versus the retained
  54,202 median (~+12.3%).
  Disposition: retain the shared table and direct publisher as the final ordinary path. The current
  Figa decoder arrays are temporary proof scaffolding and may not survive the packet.
  Next: replace nonordinary Figa evaluation with immutable-program sampling, remove per-Match Figa
  track allocation/state, then repeat exact A/B and gates.
- 2026-07-19 — `rejected`
  Scope: remove Figa mutable tracks and reconstruct nonordinary samples from absolute joint frame.
  Hypothesis: packed-stream state is a pure function of absolute frame, so a stateless seek can own
  the 14.7% nonordinary path without per-Match track state.
  Evidence: 512 throughput remained 59,177 FPS, but the digest changed. A 33-replay Fox/Falco gate
  produced 31 failures, beginning with ULP-scale geometry differences. The exact decoder's residual
  track time accumulates rates and segment subtractions in a different f32 order from recomputing
  `startframe + curr_frame`; absolute frame alone is not canonical exact state.
  Disposition: reject stateless raw seeking. Preserve only compact mutable segment cursor/time for
  nonordinary Figa tracks; p0/p1/d0/d1, byte cursor, fractions, and decoded keys belong to immutable
  GameData programs. Restore the exact mutable oracle until that segment representation is proven.
  Next: reinstate the exact candidate, compile immutable key segments, and replace each 40-byte Figa
  track with the minimum exact cursor/time/event state.
- 2026-07-19 — `open`
  Scope: cleaned production candidate, complete correctness gates, and exact-HEAD adjacent A/B.
  Hypothesis: the direct dense publisher clears the packet's 8% whole-engine acceptance threshold
  once diagnostic verification is removed.
  Evidence: 63 PASS / 90 unchanged CLASSIFIED / zero failures, native API/copy/save-restore and
  sealed-allocation checks, source sync, PPC, Wasm parity, and live viewer all pass. Three adjacent
  512 samples preserve digest `6f91f23e3553a090`: controls 57,462/57,276/57,225 FPS and candidates
  60,898/60,392/60,511 FPS. Raw medians improve 57,276 to 60,511 FPS (+5.65%), below the required
  8% despite eliminating most packed-stream decoding.
  Disposition: keep the packet open. Do not commit the marginal publisher or relax the threshold.
  Next: profile the cleaned candidate, then remove common-path per-track validity/type dispatch with
  an immutable publication program; retain the exact mutable decoder only for legal nonordinary
  samples and transition resynchronization.
- 2026-07-19 — `open`
  Scope: candidate pose-owner drill-down only; instrumentation is temporary and profile-build-only.
  Hypothesis: the remaining 12.35% pose owner is dominated by dense-table publication dispatch, so
  compiling node-local publication order and component destinations can recover the missing 2.35
  whole-frame points without changing pose state or matrix arithmetic.
  Evidence: the shared table reduced pose animation from the retained 17.71% to 12.35%, while map
  collision is now 21.33%. The exact publisher still performs two raw-track scans, repeated bounds
  and validity checks, source-DAT type loads, and a general type switch for each published value.
  Disposition: attribute table publication, general decoding, dependency checks, and RObj work
  separately before selecting the final publication representation.
  Next: collect the profile-build-only split, remove its instrumentation, and implement only the
  dominant final-form deletion.
- 2026-07-19 — `open`
  Scope: eliminate transition-entry general decoding for exact ordinary Figa requests.
  Hypothesis: `request_track` already establishes exact decoder state at the requested frame, so an
  integer, rate-one Figa request can publish its first pose from the immutable table immediately;
  forcing one full decode for every attached joint is redundant work, not required state.
  Evidence: the profile-build-only split records 3,128,158 table publications (174,481,100 net
  cycles) but 825,260 general decodes (318,865,468 net cycles). Dependency checks are 91,381,966
  cycles and RObj work is negligible. The timers perturb absolute throughput, but the dominant
  avoidable source is transition-entry/general decoding rather than table value dispatch.
  Disposition: mark exact ordinary requests table-ready at the canonical request owner; preserve
  `decoder_synced` until the first table publication marks it stale, so legal later nonordinary
  evaluation can still resynchronize exactly.
  Next: require unchanged digest/replay locks, then measure release A/B before attempting a larger
  compiled publication representation.
- 2026-07-19 — `open`
  Scope: restore the packet's zero mutable-state-growth contract before final gates.
  Hypothesis: the five one-byte joint mode fields are independent booleans/two-bit source state and
  can share one byte, keeping the native pose joint at its retained 56-byte size without changing
  access semantics or save/restore ownership.
  Evidence: clean adjacent 512 A/B is now 57,486/63,209, 56,827/63,258, and 56,627/63,204 FPS;
  raw medians improve 56,827 to 63,209 (+11.23%) and median paired improvement is +11.32%.
  At 256 the pairs are 60,559/66,531, 60,563/66,327, and 60,494/66,217 FPS; raw medians improve
  60,559 to 66,327 (+9.52%). Runtime census exposes an unintended 8,192-byte Match/savestate
  increase solely from padding the 1,024 joint entries from 56 to 64 bytes.
  Disposition: pack the existing mode fields into one byte and enforce the 56-byte native layout;
  do not retain a per-environment memory regression for immutable-program metadata.
  Next: repeat census, throughput digest, full correctness, and cross-platform gates.
- 2026-07-19 — `open`
  Scope: make immutable-program identity the canonical Figa/source/attachment state on both native
  and 32-bit Wasm/PPC layouts.
  Hypothesis: `program_index != NONE` already proves Figa attachment, while a nonzero general track
  count proves descriptor attachment. `table_ready` is redundant because exact eligibility is a
  pure check of source, rate, integer frame, and table bounds. Packing only program identity/range,
  filter mode, and decoder synchronization into one 32-bit word fits the retained native and 32-bit
  joint layouts with no synchronized representation.
  Evidence: native packing restored the 56-byte joint and exact 676,440-byte arena, but the Wasm
  compile correctly exposed that a pointer-sized assertion was wrong and the candidate still grew
  the 32-bit joint from 44 to 48 bytes. Program count is 1,683; Figa node counts are source `s8`.
  Disposition: delete `attached`, `source`, and `table_ready`; reserve the all-ones 11-bit program
  id as NONE, validate the complete source track range fits its 11/7-bit fields, and derive the
  displaced state directly.
  Next: rebuild native/PPC/Wasm and require exact replay/benchmark digests before final evidence.
- 2026-07-19 — `retained`
  Scope: final shared compiled Figa owner, derived attachment state, complete gates, and adjacent
  release A/B.
  Hypothesis: complete-source ordinary samples can be paid once in immutable GameData while exact
  mutable decoding remains only for legal nonordinary rates/frames, eliminating repeated packed
  stream interpretation without a per-Match cache or state growth.
  Evidence: GameData compiles 1,683 unique Figa trees into 10,721,046 exact values (40.90 MiB plus
  validity bits). At 512, exact control/candidate pairs are 57,292/63,409, 57,460/63,110, and
  57,082/62,818 FPS; raw medians improve 57,292 to 63,110 (+10.15%) and median paired improvement
  is +10.05%, digest `6f91f23e3553a090`. At 256, pairs are 60,571/66,869, 59,961/66,767, and
  59,373/66,194 FPS; raw medians improve 59,961 to 66,767 (+11.35%), digest
  `8ef126a41244d514`. The full gate is 63 PASS / 90 unchanged CLASSIFIED / zero failures across
  1,415,476 frames. Native API/copy/save-restore, sealed allocation, source sync, PPC, Wasm parity,
  live viewer, and formatting pass. Native arena/savestate remain exactly 676,440/738,056 bytes;
  Wasm snapshot size is 547,712 bytes.
  Disposition: retain and atomically commit. The ordinary byte-stream work and redundant
  transition-entry decode are deleted; no dual pose, fallback, runtime allocation, or legacy
  extractor remains.
  Next: refresh the retained baseline/profile and select the next highest-impact owner from the
  committed runtime.
