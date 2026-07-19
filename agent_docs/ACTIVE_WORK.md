# Active performance packet — exact paired trig evaluation

## Objective

Delete duplicated exact range-reduction work where a source owner immediately requests both
`sinf(x)` and `cosf(x)`. Retain only a single canonical paired evaluator that returns the same two
MSL polynomial results and produces a material end-to-end gain on the resident 512 workload.

## Final boundary

- **Final owner:** `MSL/trigf.c` owns one exact paired sine/cosine evaluator alongside the existing
  source-shaped scalar entry points.
- **Canonical state:** the existing quadrant table, polynomial table, source range reduction, and
  source result operation order remain the only math definition. The paired call has no cache,
  lookup table, mutable state, approximation, or alternate result representation.
- **Consumers:** initially `HSD_MtxSRT` and `HSD_MkRotationMtx`, whose source bodies request three
  adjacent sine/cosine pairs for the same Euler components. Additional consumers are admitted only
  when the same-value pair is explicit and the packet already clears its retention threshold.
- **Displaced work:** the second identical sign/range/quadrant reduction and one of each pair's two
  call boundaries. Both source polynomials still execute exactly once per requested result.
- **Deletion boundary:** each converted owner contains only the paired call; no simultaneous scalar
  calls, validation fallback, runtime flag, per-angle cache, or unsafe system-libm path remains.
  PPC keeps the matching scalar source calls unless the paired implementation is separately proven
  appropriate for that reference target.

## Evidence and acceptance

- Profile census at retained commit `3305b234` records 13,259,757 `sinf`, 11,999,886 `cosf`, and
  3,318,067 `HSD_MtxSRT` calls in the benchmark process. The absolute gprof samples include setup,
  but these call counts establish the repeated source shape; throughput comes only from the
  production benchmark.
- First prove bit-exact production digests and measure adjacent 512 control/candidate pairs. Reject
  without leaf tuning if shared range reduction does not produce a credible material gain.
- A retained packet must keep both production digests, the complete 153-replay classification,
  source/API/copy/save-restore/allocation, PPC, Wasm/viewer, source-sync, and formatting gates green.
- Commit implementation and evidence atomically only after those gates; otherwise preserve a named
  rejected stash and move to the next profile-backed owner.

## Baseline

- Runtime: `3305b234`
- 256: 74,466 FPS, digest `8ef126a41244d514`
- 512: 70,468 FPS, digest `6f91f23e3553a090`
- Ordinary arena/savestate: 633,432 / 695,048 bytes.

## Log

- 2026-07-19 — `open`
  Scope: exact paired range reduction in MSL trig, first consumed by the two three-axis HSD matrix
  constructors.
  Hypothesis: the dominant matrix owner needlessly performs each angle's identical source range
  reduction twice; one exact pair call deletes that work without changing either polynomial.
  Evidence: `HSD_MtxSRT` alone accounts for about 19.9 million of the observed sin/cos calls, and
  both matrix bodies express adjacent same-input pairs directly.
  Disposition: implement the singular paired evaluator, verify the production digest, and run the
  adjacent 512 ceiling before expanding scope or invoking full gates.
  Next: add the paired MSL owner and convert only the proven matrix consumers.
- 2026-07-19 — `retained`
  Scope: one looped three-axis exact sine/cosine evaluator consumed by `HSD_MtxSRT` and
  `HSD_MkRotationMtx`; PPC retains the matching scalar source calls.
  Hypothesis: sharing sign/range/quadrant reduction and the call boundary deletes dominant matrix
  work while retaining both exact source polynomials.
  Evidence: three adjacent 512 control samples are 69,882/70,242/69,633 FPS and candidate samples
  are 74,405/74,669/74,322 FPS, preserving digest `6f91f23e3553a090`; medians improve 69,882 to
  74,405 FPS (+6.47%). A final independent candidate set has a 74,340 median. At 256, three final
  candidate samples have a 78,599 median (+5.55% over the retained 74,466 baseline) and preserve
  digest `8ef126a41244d514`. The complete replay gate remains 63 PASS / 90 unchanged CLASSIFIED /
  zero failures across 1,415,476 frames. Source, native allocation/API/copy/save-restore, PPC,
  Wasm/viewer, pytest, and formatting gates pass; persistent storage is unchanged.
  Disposition: retain as the singular exact hosted three-axis trig owner and commit implementation
  plus evidence atomically.
  Next: refresh the profile-backed queue from the new breakdown and start the next bounded packet.
