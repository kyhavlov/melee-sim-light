# Active performance packet — direct hosted GObj scheduler

## Objective

Restore the source scheduler's single nested process walk for the canonical scalar Match step. The
hosted port currently implements `HSD_GObj_80390CFC` by repeatedly calling four resumable-scheduler
API functions for every priority and process; the production scalar path never suspends there, so
that decomposition adds out-of-line calls, repeated TLS context recovery, and duplicated state
publication to every scheduled gameplay callback.

Retain only if exact output is green and the complete 512-environment contract improves at least 3%.

## Final boundary

- **Final owner:** hosted `HSD_GObj_80390CFC` directly mirrors the decomp's priority/process loop and
  pending-mutation order using the already-bound Match GObj context.
- **Canonical state:** the existing per-Match `HSD_GObjContext`, process lists, epoch, masks, and
  pending mutation record. No static schedule, copied proc array, or alternate dispatch state.
- **Consumers:** every fighter, item, stage, camera, and collision process remains called in exact
  list/priority order with identical enable/epoch/pending-mutation checks.
- **Displaced work:** canonical complete steps no longer call `run_procs_begin`, `priority_begin`,
  `next_owner`, and `invoke` as external resumable seams. Those functions remain only for focused
  diagnostics/tools that explicitly suspend the scheduler; production does not dispatch through
  them.
- **Deletion boundary:** there is one direct production loop. No callback allowlist, fixed-object
  assumption, process cache, or batch bridge is introduced.

## Sequence

1. Copy the non-hosted decomp-shaped loop into the hosted production owner, retaining profile timing
   only around the callback itself. Compare generated code to confirm the wrapper calls disappear.
2. Run native smoke, exact 512 digest, and adjacent throughput. Reject below 3%; do not add a static
   schedule or callback specialization to rescue it.
3. If retained, require unchanged 256/512 digests, 63 PASS / 90 unchanged CLASSIFIED / zero failure
   validation, native API/copy/save-restore/allocation, PPC, Wasm/viewer, source, and formatting
   gates. Update `PERFORMANCE.md`, send Discord evidence, and commit implementation plus evidence.

## Baseline and evidence

- Runtime: `edf724d4`
- 256: 66,767 FPS, digest `8ef126a41244d514`
- 512: 63,110 FPS, digest `6f91f23e3553a090`
- Current release disassembly shows `HSD_GObj_80390CFC` calling all four hosted seams out of line in
  its inner loop. This is not an inferred compiler issue; the calls are present in the measured
  binary.
- The exact profile cannot quantify production dispatch cost because owner hash accounting occurs
  outside each callback timer. It does establish 166,272 invocations for each of the eleven fixed
  fighter process owners over the 65,536-frame contract, plus item/stage/camera processes. Release
  A/B is therefore the only acceptance evidence.

## Log

- 2026-07-19 — `open`
  Scope: direct decomp-shaped hosted scheduler loop only.
  Hypothesis: deleting millions of out-of-line seam calls and repeated TLS lookups clears three
  percent overall without touching a gameplay owner.
  Evidence: release disassembly at `edf724d4` contains calls from `HSD_GObj_80390CFC` to
  `msl_hsd_gobj_run_procs_{begin,priority_begin,next_owner,invoke}`; the non-hosted function already
  contains the required exact loop and mutation order.
  Disposition: implement that same loop for hosted production; keep resumable APIs separate and
  untouched.
  Next: cut over the function, inspect assembly, then run digest and adjacent 512 throughput.
- 2026-07-19 — `retained`
  Scope: direct decomp-shaped hosted scheduler with one bound-context local and function-local O3.
  Hypothesis: the exact loop plus direct bound owner removes the production dispatch decomposition
  without relying on a fixed callback/object set.
  Evidence: release assembly contains only the indirect gameplay callback and the three exact
  pending-mutation calls; all resumable seam calls are gone. Digest `6f91f23e3553a090` is unchanged.
  Three adjacent CPU-0 control/candidate pairs are 63,609/65,445, 63,464/65,652, and
  63,544/65,668 FPS: median paired +3.34%, raw medians 63,544 to 65,652 (+3.32%).
  Disposition: candidate clears the three-percent packet gate. Keep the singular direct loop and
  proceed to complete correctness/platform gates; do not add static scheduling or callback caches.
  Next: run full validation plus native API/save-restore/allocation, PPC, Wasm/viewer, source, and
  formatting gates, then record retained evidence and commit atomically if all remain green.
- 2026-07-19 — `retained`
  Scope: complete direct hosted scheduler packet, release A/B, and all production gates.
  Hypothesis: the direct loop is a durable deletion of production decomposition rather than an
  intermediate scheduler representation.
  Evidence: three 512 control/candidate pairs preserve digest `6f91f23e3553a090` and improve raw
  medians 63,544 to 65,652 FPS (+3.32%; paired +3.34%). Candidate 256 median is 68,850 FPS with
  digest `8ef126a41244d514`, +3.12% over the retained 66,767 baseline. The complete gate is 63 PASS /
  90 unchanged CLASSIFIED / zero failures across 1,415,476 frames; native API/copy/save-restore,
  sealed allocation, source sync, PPC, Wasm parity, viewer, and formatting all pass.
  Disposition: retain and atomically commit the singular direct production loop plus evidence.
  Next: refresh the committed subsystem profile and select the next bounded source owner; do not
  retry scattered-JObj cross-environment pose publication or leaf timer tuning.
