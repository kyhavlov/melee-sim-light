# Shared Figa program clock — rejected 2026-08-04

## Parent and workload

- Branch: `perf/decomp-throughput`, dirty aggregate based on `02cfe013`.
- Same-source frozen control SHA-256:
  `5bbb743d4db0a433f4575426c607cd01835a0b11ceea826edfe8d10833b4a2d6`.
- Exact full candidate SHA-256:
  `4f1091eefc9c11a55040afbc6bb0ffb2b2249fd5b5cb587147c9ed721857f6ed`.
- Balanced 366-case manifest, strict native release build, resident 256 and 512,
  131,072 measured match-frames, eight warmup ticks, ABBA ordering. The isolated screen used CPU 7
  on the V-cache CCD because CPU 0 had an active browser renderer; this is rejection evidence, not
  a campaign baseline refresh.

## Intended owner and deletion boundary

Direct compiled Figa nodes in one fighter subtree that shared a program, integer frame, unit rate,
flags, end, and rewind would share one canonical program clock. The existing `track_start` word
encoded membership only while all member nodes owned zero decoder tracks. The ordinary JObj SRT,
dependency, and RObj publication order remained unchanged. Individual requests, excluded parts,
fractional rates, non-direct programs, and mutable decoder tracks stayed on the scalar owner.

The implementation added no state, allocation, approximate math, compiler control, program cache,
or fallback dispatch. Non-loop completion published the immutable per-AObj ended bit once; it did
not mirror moving clocks.

## Correctness result

The first full cut exposed two exactness requirements:

- An unanimated ancestor must run dependency/RObj work before the first animated child advances,
  matching the scalar visitation order.
- An active shared clock may stay singular, but non-loop completion is observably per AObj and must
  publish `AOBJ_NO_ANIM` to each member once.

With those boundaries, the 32,768-frame resident digests were exact at both sizes:
`4124834367a202ec` at 256 and `588be8489df1a62d` at 512. A broader terminal dissolution changed
gameplay and was not retained.

## Controlled performance result

| Resident | Control arms (c/f) | Shared-clock arms (c/f) | Paired result |
|---:|---:|---:|---:|
| 256 | 39,331.2 / 38,324.4 | 40,980.2 / 39,839.8 | 3.9--4.2% slower |
| 512 | 38,717.2 / 37,799.3 | 38,960.4 / 37,995.0 | 0.5--0.6% slower |

All compared arms had identical digests (`a00e2d90ed375c84` at 256 and `9c1f37ab084f8ef2`
at 512). Earlier numbers from `make native-release` were discarded because that target had not
relinked `replay-bench`; only the `native-release-benchmark` binaries above are evidence.

## Decision and revisit criterion

Reject and remove the complete program-clock implementation. The direct frame-table path has made
per-node clock advancement cheap; group formation, eligibility validation, membership routing, and
the added traversal work cost more than the arithmetic removed. No implementation was salvaged.

Revisit only if a representation deletes the node traversal or demanded JObj publication as well
as the clocks. Sharing clock fields alone is not a useful boundary, even when made exact.
