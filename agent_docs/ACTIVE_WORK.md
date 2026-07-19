# Active performance packet — exact fighter map-collision compiler admission

## Objective

Test the complete `mpcoll.c` source owner at the strongest exact native optimization level. Fighter
map collision owns roughly 23% of the measured frame and its source-profile build remains O0 after a
stale O3 admission was removed for a real replay mismatch. Retain only a correctness-green complete-
owner admission with at least 3% overall improvement at 512 environments.

## Final boundary

- **Final owner:** the native release Makefile admits the complete `mpcoll.c` translation unit at one
  explicit exact compiler level; source collision order and state remain canonical.
- **Canonical state:** unchanged `CollData`, stage geometry, callback tables, and scalar collision
  results.
- **Consumers:** every fighter action collision callback continues through the same source functions.
- **Displaced work:** only O0 compiler artifacts inside this complete owner.
- **Deletion boundary:** no function clone, action specialization, runtime dispatch, replay exception,
  alternate collision state, or source edit. An inexact compiler level is removed, not wrapped.

## Sequence

1. Test isolated O1, O2, and only if useful O3 admissions, requiring both production digests.
2. Measure adjacent 512 control/candidate binaries for the strongest exact level; reject below 3%.
3. If retained, require the complete replay, native API/copy/save-restore/allocation, PPC,
   Wasm/viewer, source, and formatting gates. Record evidence, send Discord progress, and commit
   implementation and evidence atomically.

## Baseline and evidence

- Runtime: `4e275d65`
- 256: 70,847 FPS median, digest `8ef126a41244d514`
- 512: 66,953 FPS median, digest `6f91f23e3553a090`
- Profile: fighter map collision owns about 22.6% of the production workload. Historical O3 was
  rejected because `ExpertWorthlessFinch` changed; the retained compiler audit did not establish an
  exact measured O1/O2 result for this owner.

## Log

- 2026-07-19 — `open`
  Scope: complete `mpcoll.c` compiler admission only.
  Hypothesis: O1 or O2 removes substantial O0 control/stack overhead while preserving the exact
  collision arithmetic that O3 changed.
  Evidence: the current profile identifies this as the dominant callback source owner and the
  Makefile intentionally leaves it at the source profile after the stale O3 correction.
  Disposition: test compiler levels as an atomic source owner before considering any collision
  representation change.
  Next: build isolated O1 and require the 512 digest, then escalate only while exact.
- 2026-07-19 — `retained`
  Scope: complete `mpcoll.c` native release admission at O1.
  Hypothesis: O1 removes O0 control and stack overhead without changing exact source arithmetic.
  Evidence: O1 preserves both production digests and improves adjacent medians from 67,572 to
  70,468 FPS at 512 (+4.29%) and 71,383 to 74,466 FPS at 256 (+4.32%). O2 is exact but reaches only
  68,414 FPS at 512; historical O3 is inexact. The complete suite remains 63 PASS / 90 unchanged
  CLASSIFIED / zero failures over 1,415,476 frames; native, PPC, Wasm/viewer, source, and formatting
  gates pass.
  Disposition: retain O1 as the strongest measured exact complete-owner level. No source or runtime
  behavior changed.
  Next: commit implementation and evidence atomically, refresh the profile, and select the next
  bounded work-deletion or batch/data-layout owner.
