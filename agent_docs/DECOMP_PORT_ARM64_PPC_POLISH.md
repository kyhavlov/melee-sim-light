# `decomp-port-arm64-ppc` merge-polish worklog

## Scope

- Local branch: `decomp-port-arm64-ppc-polish`
- Starting head: `479d847d`
- Comparison ref: `origin/experiment/decomp-port` at `2df5f02b`
- Detailed review evidence: `agent_docs/DECOMP_PORT_ARM64_PPC_REVIEW.md`
- Priorities: correctness first, equivalent-computation performance second, then mechanical and
  history cleanliness.

## Baseline

- Full native aggregate: 285 pass / 80 classified / 1 Samus fail / 0 error, 3,501,461 frames.
- Focused PPC: stale Ice Climbers fingerprint with first follower-position split at frame 7665.
- Capacity: four Sheiks on Final Destination attempt to register pose joint 1,001 of capacity 1,000.
- Common release benchmark: +12.85% cycles/frame at batch 256 and +13.54% at batch 512.
- Lifecycle: snapshot 633,156 -> 686,676 bytes; save/restore each about 8.4% slower.

## Work packets

1. Mechanical gates and complete coverage.
2. Fighter-pose capacity boundary.
3. Samus native platform-edge collision correctness.
4. Ice Climbers PPC follower exactness.
5. Steady-state and save/restore performance recovery.
6. Final bounded cross-platform gates and history cleanup.

## Experiments

- 2026-08-01 — `retained`: retain only focused repros, per-suite PPC checks, runtime census cases, and
  adjacent 153-replay performance samples. Do not repeat the eight-minute full PPC aggregate.
- 2026-08-01 — `retained`: replace the global 1,000-joint pose ceiling with a 256-joint/player
  admission boundary and size it from the actual 2/4-player configuration. The complete runtime
  census peaks at 1,016 joints for four Sheiks under the 1,024 ceiling. Two-player snapshots fall
  from 686,676 to 659,348 bytes; save/restore return to the comparison medians.
- 2026-08-01 — `retained`: the Samus failure comes from `ftCo_800DA190` treating the first 32 bits
  of `Fighter` as its kind, which is only true of the retail 32-bit pointer layout. Use the existing
  typed `fp->kind` member. Focused native validation has zero gameplay mismatch over 15,481 frames.
- 2026-08-01 — `retained`: the Ice Climbers PPC split comes from undefined CPU-DI command locals in
  `ftCo_800AC5A0`. The retail asm inherits caller-register residue on its zero-knockback path; the
  focused replay records neutral, while hosted C inherited arbitrary stack data. Neutral
  initialization makes native and PPC gameplay exact; each backend retains only its known
  `item.misc` residue.
- 2026-08-01 — `rejected`: making the later post-solve dynamics publication available to the PPC
  path did not change the Ice Climbers split. An explicit CaptureCut fmadd did not own it either.
  Both experiments were reverted completely.
- 2026-08-01 — `retained`: remove the earlier whole-chain dynamics publication from
  `Fighter_ProcessHit_8006D1EC`; keep the later post-solve publication as the sole end-frame owner.
  Seven exact dynamics-sensitive locks remain unchanged. Against a control build containing the
  duplicate publication, adjacent batch-256 samples improve about 6--7%; batch 512 is neutral
  within noise. The current tree remains roughly 15--17% slower than the comparison branch under
  adjacent common-workload samples, so broader steady-state cost remains open.
- 2026-08-01 — `retained`: make the retained post-solve publication consume the JObj tree's canonical
  `JOBJ_MTX_DIRTY` state instead of forcing every dynamics link dirty. The solver's rotation
  setters already propagate dirtiness through each mutated chain; an unchanged clean link already
  holds its current matrix. All seven dynamics-sensitive locks remain exact and adjacent samples
  improve.
- 2026-08-01 — `retained`: keep the established FObj and class-piece reserves for ordinary matches,
  and select the measured larger reserves only when the configuration actually contains Samus.
  Her grapple is the documented sole owner of both increases. The runtime census and complete
  Samus validation pass; the ordinary two-player snapshot is 611,420 bytes versus 686,676 at the
  review head and 633,156 on the comparison branch.
- 2026-08-01 — `rejected`: verify whether each dynamics descriptor's canonical solved world position
  is bit-identical to the raw JObj translation produced by the retained end-frame publication.
  It is not: all seven focused processes hit the bitwise x-translation assertion immediately.
  The retail-probed JObj publication remains the required owner; the diagnostic assertions and
  include were removed completely without testing a substitute representation.
- 2026-08-01 — `retained`: compile `__fmadds` as the compiler intrinsic it represents in the
  native release profile. The expanded exactness work added 126 explicit sites, but O0
  source-shaped translation units paid an out-of-line call at every site. Seven sensitive output
  locks remain exact and the first adjacent batch-512 pair improved about 5.5% against the
  out-of-line candidate. Debug, Python, Wasm, and PPC retain the existing external implementation.
- 2026-08-01 — `rejected`: extending release inlining to the less frequent `__fmsubs` and
  `__fnmsubs` sites kept the seven locks exact but made the adjacent batch-512 pair 5.8% slower
  while the comparison binary stayed stable. Restore those two compact shared functions.
- 2026-08-01 — `retained`: the final native 366-replay aggregate is 286 exact / 80 classified / 0
  fail / 0 error over 3,501,461 frames. The Samus case is exact and its stale classification is
  removed. Twenty-three Ice Climbers classification snapshots changed with the compact arena;
  guarded refresh accepted only their established `item.misc` identity/residue lanes, plus an
  unchanged pre-existing Dream Land wind-puff episode in its mixed classification. Focused PPC
  Ice Climbers validation is 11,056/11,243 matched with 187 residue-only rows and no follower,
  physics, or render mismatch (`8d5f7f111be1d1a4`).
- 2026-08-01 — `retained`: final alternating common-domain medians are 63,765.7 comparison versus
  62,011.2 polished cycles/frame at batch 256 (-2.75%), and 48,921.1 versus 49,625.9 at batch 512
  (+1.44%). Stable per-ref digests and identical 153-replay workloads bound the remaining spread
  to normal contention. This closes the review head's +12.85%/+13.54% material regression.

## Final state

- Correctness: native aggregate 286 pass / 80 classified / 0 XPASS / 0 fail / 0 error; focused PPC
  Ice Climbers has only the owned item-residue family; seven dynamics-sensitive locks are exact.
- Capacity/lifecycle: complete 16-character x six-stage x 2/4-player census passes at a 1,016-joint
  maximum. Ordinary snapshot/save/restore are 611,420 bytes and 0.504/0.661 ms, better than the
  comparison branch's 633,156 bytes and 0.5265/0.691 ms medians.
- Throughput: old-domain batch 256/512 medians are within -2.75%/+1.44% of the comparison ref,
  closing the material review regression without tolerances or alternate gameplay state.
- Gates: source inventory, formatting, native/PPC smoke, runtime census, native release aggregate,
  focused PPC validation, Wasm/native parity, viewer smoke, and all 45 Python tests pass. The five
  API tests require preloading the checked-in `_native.py` because pre-existing ignored in-tree
  CPython extension artifacts shadow it; all seven API tests pass under the source module.
- Remaining external check: native macOS execution is still intentionally delegated to the
  contributor with actual Apple hardware. Local Makefile/static and cross-platform gates are clean.
