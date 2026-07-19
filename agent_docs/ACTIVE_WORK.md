# Active performance packet — compact six-origin fighter ECB evaluator

## Objective

Replace six independent generic JObj point queries in fighter JObj-backed ECB publication with one
compact-pose evaluator. Resolve the union of demanded ancestors once, evaluate dirty ordinary
matrices in canonical topology order, and publish the same six JObj matrix origins directly to the
source ECB owner.

## Final boundary

- **Final owner:** `runtime/fighter_pose.c` owns one six-origin query for joints registered in the
  compact fighter pose. `mpColl_LoadECB_JObj` owns invocation at its existing six-point source site.
- **Canonical state:** evaluated results are written only to each existing `HSD_JObj::mtx`; ECB
  output remains `CollData::desired_ecb`. No origin cache, alternate pose, or per-frame buffer is
  persistent or save-state-visible.
- **Consumers:** fighter ECB publication uses the compact query. Other JObj users and non-fighter
  JObj sources retain their proper generic owner rather than a compatibility mode.
- **Displaced work:** six repeated pose lookups, ancestor recursion, dirty checks, and separately
  dispatched matrix/trig calls. The evaluator admits ordinary Euler SRT nodes and delegates true
  quaternion, IK, RObj, path, user-matrix, and independent-parent nodes to their source routines.
- **Deletion boundary:** the compact query never calls six generic `lb_8000B1CC` paths for a compact
  fighter ECB. There is no synchronized dual result, runtime feature flag, character/stage list,
  approximate math, or replay exception.

## Evidence and acceptance

- Baseline `3d0ecbb0`: 90,017 FPS at 256 and 84,556 FPS at 512; digests
  `8ef126a41244d514` / `6f91f23e3553a090`; debug and release validation are 63 PASS / 90 unchanged
  CLASSIFIED.
- RDTSCP assigns 16.19% to stage collision. Prior helper attribution assigns 14.31% to
  `mpColl_LoadECB_JObj` and records 143,594 calls; the next collision query owner is only 1.59%.
- A 64-environment instruction sample places exact matrix/trig owners (`msl_sincosf3`,
  `HSD_MtxSRTConcat`, `PSMTXConcat`, `HSD_JObjMakeMatrix`, setup recursion) among the largest frame
  costs. A lazy-publication proof moved 4.15 points directly into stage collision, confirming this
  demand rather than deleting it.
- Early proof: scalar compact union evaluation must preserve both digests and reduce setup entries;
  vector/precomputed-trig work proceeds only if the final six-origin boundary is correct and has a
  measurable ceiling.
- Final retention: target at least 5% overall at 512, unchanged digests/classifications, complete
  debug/release/API/copy/save-restore/allocation/PPC/Wasm/viewer gates, no hot allocation, and no
  persistent memory growth.

## Log

- 2026-07-19 — `rejected`
  Scope: the six compact-pose joint origins consumed by `mpColl_LoadECB_JObj`.
  Hypothesis: evaluating their shared ancestor closure once in topology order removes enough
  repeated generic traversal and enables a multi-node exact trig/matrix kernel without a batch
  scheduler seam.
  Evidence: stage collision is 16.19% of the current contract and its six-origin loader dominates
  that owner. Previous per-point deferral proved the work is demanded; previous eager global matrix
  tables failed from cache footprint, which this ephemeral demanded closure avoids.
  Disposition: first establish an exact compact union evaluator using canonical JObj matrices, then
  measure the remaining arithmetic ceiling before vectorizing.
  Next: add the six-origin compact API and route only compact fighter JObj ECBs through it; run both
  production digests and collect exact setup/matrix counts.

- 2026-07-19 — `retained`
  Scope: initialization-bound closure metadata and one topology-ordered query.
  Hypothesis: removing per-query path discovery is the necessary production substrate for a
  multi-matrix evaluator.
  Evidence: supported bindings contain 18, 21, 25, or 28 joints; 318,596 benchmark queries publish
  6,417,376 dirty matrices, with dirty work on 95.7% of queries. Prebinding restores the scalar
  proof from 81,946 to 84,444 FPS with digest `6f91f23e3553a090` unchanged.
  Disposition: retain as the immutable topology owner; scalar traversal alone is neutral and not
  the performance claim.
  Next: evaluate the ordinary dirty subset through one exact trig batch and direct canonical matrix
  publication.

- 2026-07-19 — `rejected`
  Scope: an evaluator that classified every bound node as Euler at initialization.
  Hypothesis: matrix method and flags are immutable after fighter construction.
  Evidence: optimized release validation diverged broadly. Direct source/candidate comparison found
  `JOBJ_USE_QUATERNION` appearing dynamically on bound nodes during interpolation; classical-scale
  inheritance also requires its distinct source path.
  Disposition: reject static Euler classification. Preserve classical-scale semantics and classify
  the dynamic quaternion/path bits at each publication without restoring generic traversal.
  Next: batch only the ordinary subset and dispatch the small special subset in the same topology
  walk.

- 2026-07-19 — `retained`
  Scope: final compact six-origin evaluator with exact wide trig.
  Hypothesis: one AVX-512 evaluation of the ordinary Euler subset plus direct canonical matrix
  publication removes the dominant repeated trig/dispatch work while special nodes remain exact.
  Evidence: 18.6 of 19.9 dirty nodes in a 10,000-query census are direct Euler candidates. Final
  adjacent medians improve 81,453 to 85,633 FPS at 512 (+5.13%) and 86,597 to 91,912 at 256
  (+6.14%), preserving digests `6f91f23e3553a090` / `8ef126a41244d514`. Tightening the pose pool
  against its 976-node supported census offsets topology storage and reduces the lifecycle snapshot
  from 634,232 to 633,432 bytes. Debug and release validation remain 63 PASS / 90 unchanged
  CLASSIFIED; the complete native/PPC/API/save-restore/allocation/Wasm/viewer/pytest/source-sync/
  formatting gate is green.
  Disposition: retain and commit the complete final boundary and evidence atomically.
  Next: refresh the subsystem profile from the committed binary and select the next bounded owner.

- 2026-07-19 — `open`
  Scope: final ablation of the compact ECB boundary from the exact AVX-512 trig owner.
  Hypothesis: the measured gain must come from deleting repeated six-origin work, not merely from
  compiling every existing three-axis matrix through the new exact wide trig kernel.
  Evidence: with exact wide trig still active, three bypass samples are
  83,982/83,178/83,758 FPS at 512 (83,758 median), versus the complete candidate's recent
  88,256/89,094/89,238 (89,094 median). The compact boundary therefore contributes about 6.4% over
  the wide-trig-only shape and is not dead scaffolding.
  Disposition: reject the bypass and retain the complete compact boundary. The temporary compile
  switch is removed.
  Next: rebuild the complete candidate, record final adjacent evidence and full gates, then commit
  the implementation and evidence atomically.
