# Active performance packet — optimized native source closure

## Objective

Replace the native release runtime's decomp-matching `-O0` default with a strict optimized source
closure. Establish the whole-runtime throughput ceiling first, then make every reached supported
owner safe under optimization by fixing explicit source operation boundaries or latent undefined
behavior. Retain the strongest correctness-green final profile; do not accumulate a permanent
allowlist of uninvestigated `-O0` gameplay owners.

## Final boundary

- **Final owner:** the native release build owns one strict optimized compiler contract for runtime,
  platform, and imported gameplay source. PPC retains its matching profile and Wasm its existing
  toolchain profile.
- **Canonical state:** gameplay source state and public outputs are unchanged. Float expressions
  whose source/PPC operation order is observable state that order explicitly in source; native
  optimization must not rely on unsafe math.
- **Consumers:** the production batch API, replay validator, save/restore, Python library, and native
  tools all consume the same optimized native runtime.
- **Displaced work:** O0 stack traffic, redundant loads/stores, uninlined accessor/branch scaffolding,
  and debug-oriented code layout across reached native source owners.
- **Deletion boundary:** `-O0` is no longer the native release default. Any owner retained below the
  final default must have a measured exactness reason and a narrow documented boundary; unresolved
  failures do not justify reverting the complete packet without attribution.

## Evidence and acceptance

- Retained baseline `a8431342`: 85,355 FPS at 256 and 80,041 FPS at 512, digests
  `8ef126a41244d514` / `6f91f23e3553a090`; 63 PASS / 90 unchanged CLASSIFIED.
- The current release compiles only one O1 owner, one O2 owner, and a small O3 allowlist; the large
  imported scheduler closure remains O0. Earlier blanket probes were stopped at changed digests and
  one unresolved unsupported-Ness reference rather than completing correctness attribution.
- Early proof: isolated strict O2/O3 build, benchmark digest/throughput, native smoke, and bounded
  validation triage. A large throughput ceiling requires pursuing correctness inside the optimized
  representation rather than falling back wholesale.
- Final retention: unchanged production digests, no new/widened replay classification, complete
  `test-full`, and adjacent 256/512 measurements. Update retained evidence and commit atomically only
  with a measured correctness-green gain.

## Log

- 2026-07-19 — `open`
  Scope: whole native release compiler boundary, beginning with strict O2 across all native source.
  Hypothesis: removing O0 code generation across the remaining 90%+ scheduler closure is a much
  larger final-form opportunity than another scalar leaf or a scattered-JObj batch seam.
  Evidence: current corrected profile assigns 93.75% to the source scheduler; only a small explicit
  object list is optimized today. Prior blanket probes established correctness problems but did not
  establish or repair their source owners.
  Disposition: build an isolated candidate and measure its throughput ceiling before editing source.
  Next: compile strict whole-runtime O2, run the production digests and native smoke, then attribute
  build/runtime/output failures by translation unit and source owner.
- 2026-07-19 — `open`
  Scope: strict whole-source ceiling and first directory-level attribution.
  Hypothesis: the optimized closure has enough headroom to justify resolving its exactness owners.
  Evidence: whole-source O1 and O2 both reach about 92.2k FPS at 512 (+15% over 80,041), but share
  wrong digest `aa365caa5c1ec1be`; a 300-frame all-replay pass reports ULP-scale velocity/position
  drift in 126/153 cases rather than structural failures. O2 additionally retains unsupported Ness
  yoyo calls through inlining; compiling that unreachable TU at O0 proves the link boundary and
  produces the same throughput/digest as O1. Isolated fighter, non-fighter Melee, and baselib groups
  each change the digest; optimizing only repository runtime/platform source is exact but merely
  80.7k FPS.
  Disposition: retain the strict global ceiling as the packet direction. Attribute the upstream
  drift by smaller translation-unit groups and keep unsupported character code outside the reached
  optimized closure; do not patch replay outputs or accept ULP drift.
  Next: bisect baselib and Melee source groups to identify the small operation-order owners that must
  be repaired or narrowly held below the final default.
- 2026-07-19 — `open`
  Scope: O1 default with fighter/item/quatlib source-profile exceptions and exact release validation.
  Hypothesis: the non-fighter closure supplies most of the blanket-O1 ceiling while the identified
  float-sensitive subtrees can remain at their measured source profiles without masking new drift.
  Evidence: the candidate preserves both production benchmark digests and reaches 84,556 FPS at 512
  and 90,017 FPS at 256 by three-sample median. Release validation exposed one previously hidden
  two-ULP Peach turnip ECB mismatch in the already-retained O1 `mpcoll.c`; the ordinary full gate had
  validated the O0 development binary rather than the release binary. Attribution proves GCC fused
  the source's separate `sinf` and `cosf` calls into `sincosf` in `mpColl_LoadECB_Fixed`.
  Disposition: preserve the candidate and repair the operation boundary in source; do not classify
  the mismatch or hold the full collision function at O0.
  Next: force separate hosted trig calls at the narrow source site, run all 153 replays against the
  release binary, then gather an adjacent clean-HEAD control.
- 2026-07-19 — `open`
  Scope: source-ordered fixed-ECB trig boundary and complete optimized-release validation.
  Hypothesis: non-inlinable scalar wrappers preserve the retail call graph without sacrificing the
  optimized collision owner around it.
  Evidence: the release object now imports separate `sinf` and `cosf` symbols and no `sincosf`;
  `ExpertWorthlessFinch` returns to fingerprint `cd6428688741de53`. The complete 153-replay release
  run is 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames. Final
  candidate medians remain 84,556 FPS at 512 and 90,017 FPS at 256 with unchanged digests.
  Disposition: retain the narrow operation-order repair pending adjacent control and complete gates.
  Next: preserve this tree in a named stash, rebuild exact clean HEAD, collect adjacent control
  samples, restore the candidate, and retain only if the measured gain remains material.
- 2026-07-19 — `open`
  Scope: adjacent clean-HEAD throughput control and production release-validation contract.
  Hypothesis: the optimized source closure is a stable gain relative to the exact committed binary,
  and making release validation a first-class full-gate dependency prevents compiler admissions from
  being judged only through the O0 development runtime.
  Evidence: clean-HEAD samples are 80,118/79,807/80,213 FPS at 512 and
  85,001/84,597/84,513 FPS at 256. Candidate medians of 84,556 and 90,017 improve the adjacent
  medians by +5.54% and +6.41%, respectively, with both digests unchanged. The Makefile now exposes
  and includes a dedicated all-153 optimized-release validation gate.
  Disposition: retain the complete packet pending the ordinary full gate.
  Next: run source/API/copy/save-restore/allocation, PPC, debug and release replay validation, Wasm,
  viewer, pytest, source-sync, and formatting gates; then update retained evidence atomically.
- 2026-07-19 — `retained`
  Scope: strict optimized native source closure, fixed-ECB trig operation boundary, and release gate.
  Hypothesis: optimizing the exact-safe closure and explicitly preserving the few measured lower
  source profiles yields a durable whole-scheduler gain without weakening correctness.
  Evidence: adjacent medians improve 80,118 to 84,556 FPS at 512 (+5.54%) and 84,597 to 90,017 FPS
  at 256 (+6.41%), preserving digests `6f91f23e3553a090` / `8ef126a41244d514`. Both debug and release
  validation are 63 PASS / 90 unchanged CLASSIFIED / zero failures across 1,415,476 frames. Native
  source/API/copy/save-restore/allocation, PPC, Wasm/viewer, pytest, source-sync, and formatting gates
  pass. The refreshed profile assigns 94.59% to the scheduler, led by fighter maintenance (29.74%),
  map collision (16.97%), dynamics (8.12%), and Spaghetti input/IASA (7.33%).
  Disposition: retain and atomically commit implementation, release gate, and evidence.
  Next: begin a bounded packet against the largest remaining final-form source owner, using the new
  optimized-release gate before retention.
