# Compact collision vertices — rejected 2026-09-21

The parent is the fully gated `final-matrix` candidate after `83356f34`, benchmark
SHA-256 `bb0dfc11403a8946e621eebd48fae2c81747dfc82865b34b77982d2a0806caab`.
Both arms use the current 508-tape production contract on CPU 0: 262,144 measured
match-frames, eight warmup ticks and 128-frame observation/terminal history.
Compiler, host settings, tape contents and gameplay behavior are unchanged.

## Owner and completed deletion

`MapCollData::verts` already owns the original local coordinates. The candidate
removed their duplicate `CollVtx::x0/x4` copies, leaving one 16-byte mutable
current/previous position record instead of 24 bytes. Source audit finds the
copies written only in `mpLibLoad`; uniform/general joint transforms and the
relative endpoint setter are their only readers. All native readers moved to
the shared original array; both initialization stores and native fields were
removed. Every collision/island query then used the smaller stride. PPC retained
its source layout. No dual state, cache, allocation or artifact schema was added.
The existing snapshot layout hash included the raw vertex size to reject old
layouts without a compatibility path.

## Correctness and memory

Source/native/API/sealed allocation/copy/save checks pass. A native and actual
release fixture runs 512 seeded uniform/general transforms and relative endpoint
updates against source arithmetic, while checking previous-position publication.
Both benchmark digests and reset counts match. The complete native ownership cut
was screened before spending the full supported-domain gate.

Ordinary arena and snapshot size each fall by 128 bytes, to 1,757,004 and
1,820,980 bytes. The construction maximum falls by 288 bytes to 2,119,020.
Allocation counts, Match size and shared GameData/DAT storage do not change.
The supported stages have small vertex sets; this is a small working-set saving.

## Whole-runtime result

| Resident batch | Parent FPS | Candidate FPS |
| ---: | --- | --- |
| 256 | 124762, 123832 | 123504, 123259 |
| 512 | 121044, 119123 | 120038, 120141 |

Three of four adjacent comparisons lose. Batch 256 is about 0.7% slower at the
symmetric center, while 512 is neutral. The smaller addressing stride does not
pay for the ownership/read changes in the complete workload.

Restore the entire candidate, including its snapshot hash extension and fixture;
no compact vertex representation remains. All source/objects, raw samples and
census logs remain under `reports/triage/perf_120k_20260921/compact-vertices/`.
A revisit needs a consumer change that removes more collision work, or evidence
that a materially larger stage working set makes this payload reduction useful.
Merely shrinking these same records is not sufficient.
