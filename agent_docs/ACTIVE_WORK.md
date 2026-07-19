# Active performance packet — release-only control-flow/layout deletion

## Objective

Delete host toolchain instrumentation and frame-chain state that the single-threaded production
simulator does not consume. Measure this as one exact release-layout packet before returning to
larger source owners.

## Final boundary

- **Final owner:** the native release profile owns its explicit ABI/code-generation contract.
- **Canonical state:** gameplay and API state are unchanged; stack unwinding, debug backtraces, and
  CET metadata are not runtime simulation outputs.
- **Consumers:** the native release core and replay benchmark. Debug/native development, PPC, and
  Wasm builds retain their existing profiles.
- **Displaced work:** per-function ENDBR instructions, frame-pointer setup/teardown, asynchronous
  unwind tables, and any associated release binary metadata.
- **Deletion boundary:** release objects are built with `-fcf-protection=none`,
  `-fomit-frame-pointer`, and no unwind tables. No unsafe floating-point, source optimization,
  gameplay branch, ABI surface, or benchmark-only dispatch is introduced.

## Evidence and acceptance

- Retained commit `f8b3e325` measures 77,319 FPS at 512 and 81,753 FPS at 256 with digests
  `6f91f23e3553a090` and `8ef126a41244d514`.
- The current release binary advertises IBT/SHSTK and every reached function begins with ENDBR64;
  most imported gameplay remains at O0 and therefore also retains frame-pointer traffic.
- Early proof requires unchanged digests, reduced text/unwind footprint, and adjacent resident-512
  samples. Retain even a small stable gain because the final change is a compact global deletion;
  reject if throughput is neutral.
- Final retention requires the complete 153-replay classification and native source/API/copy/
  save-restore/allocation gates; PPC/Wasm are structurally outside the changed profile but must still
  pass the ordinary production gate before commit.

## Log

- 2026-07-19 — `open`
  Scope: native release code-generation metadata and prolog/epilog only.
  Hypothesis: removing CET landing pads, frame chains, and unwind sections lowers instruction and
  i-cache pressure across the large O0 source callback closure without changing any float operation.
  Evidence: the 1.83 MiB text image carries IBT/SHSTK properties and emitted ENDBR64 at every sampled
  hot function; no production API consumes native stack unwinding.
  Disposition: apply the release-only flags together, confirm binary deletion, then run exact digest
  and adjacent 512 proof.
  Next: rebuild the isolated release profile and inspect code/property/size before benchmarking.
- 2026-07-19 — `open`
  Scope: complete release-only frame/CET/unwind deletion candidate.
  Hypothesis: the verified text and hot-prolog deletion should yield a stable whole-frame gain.
  Evidence: text falls from 1,826,759 to 1,581,491 bytes (-13.4%); IBT/SHSTK properties and ENDBR64
  disappear. The 512 digest remains `6f91f23e3553a090`; candidate samples are
  80,375/80,041/79,899 FPS (80,041 median), visibly above the recorded 77,319 baseline.
  Disposition: preserve the candidate and gather same-host adjacent clean controls before full gate.
  Next: named-stash, rebuild clean HEAD, and collect three 512 controls.
- 2026-07-19 — `retained`
  Scope: explicit native release deletion of CET, frame chains, and unwind metadata.
  Hypothesis: removing non-simulation control-flow/layout overhead across the large source closure
  yields a durable exact whole-frame improvement.
  Evidence: adjacent 512 controls are 77,347/76,948/76,606 FPS and candidates are
  80,375/80,041/79,899 FPS; raw medians improve 76,948 to 80,041 FPS (+4.02%). Final 256 samples are
  85,381/84,790/85,355 FPS, an 85,355 median (+4.41% over 81,753). Digests remain
  `6f91f23e3553a090` and `8ef126a41244d514`. Text falls 1,826,759 to 1,581,491 bytes (-13.4%).
  The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero failures across 1,415,476
  frames; native source/API/copy/save-restore/allocation, PPC, Wasm/viewer, pytest, source-sync, and
  formatting gates pass.
  Disposition: retain and atomically commit the release-profile deletion with evidence.
  Next: refresh the retained baseline/profile, commit, then select the next high-impact owner.
