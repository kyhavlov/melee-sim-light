# Active performance packet — optimized PPC-exact square root owner

## Objective

Move the process-wide PPC-exact `sqrtf` implementation out of the quaternion translation unit that
must remain O0. Compile the singular native owner with the optimized release profile while preserving
the authored `frsqrte` seed, three double-precision Newton steps, final float store, and every caller.

## Final boundary

- **Final owner:** `src/runtime/ppc_sqrt.c` owns the hosted definition backed by
  `refs/melee/src/sysdolphin/baselib/quatlib.c::sqrtf` and the matching MSL sequence.
- **Canonical state/semantics:** there is no state or approximation table. The exact source operation
  order and `ppc_frsqrte` seed remain canonical.
- **Consumers:** all native/Wasm gameplay `sqrtf` calls resolve to this one definition. PPC retains
  its upstream-shaped inline definition in `quatlib.c`.
- **Displaced code:** native/Wasm no longer emit `sqrtf` from the O0 quaternion object.
- **Deletion boundary:** one definition per platform; no dispatch, fallback, duplicate helper, or
  fast-math flag.

## Acceptance

- Both production digests and the complete replay/API/copy/save-restore/allocation/PPC/Wasm/viewer
  gates remain unchanged.
- Native disassembly contains the same arithmetic sequence without O0 stack round-trips.
- Resident 512 and 256 show a repeatable whole-frame gain; otherwise restore the original owner.

## Log

- 2026-07-19 — `open`
  Scope: exact square-root source-owner split and native compiler admission.
  Hypothesis: the O0 global definition currently spills every intermediate and costs about 300 bytes
  of code per call; the existing representative gprof run attributes 2.88% self time to `sqrtf`.
  Evidence: `quatlib.o` is deliberately O0 for quaternion exactness, while its global `sqrtf` symbol
  serves the entire executable. The release function repeatedly stores/reloads `x`, `guess`, and
  final `y` around an out-of-line exact `__frsqrte` call.
  Disposition: establish the singular optimized owner directly, then measure exact production output.
  Next: split the definition, inspect codegen, and run adjacent resident-512 controls.

- 2026-07-19 — `retained`
  Scope: singular optimized hosted definition and adjacent production measurements.
  Hypothesis: preserving the exact source expression inside the optimized owner will delete O0
  spill/reload overhead without changing the PPC estimate or rounding sequence.
  Evidence: resident-512 control/candidate medians are 45,724.9/45,222.2 cycles per frame (-1.10%);
  resident-256 medians are 43,122.9/42,736.7 (-0.90%). Digests remain
  `6f91f23e3553a090` / `8ef126a41244d514`. Disassembly preserves the three Newton steps and final
  float store while eliminating the O0 frame traffic.
  Disposition: retain the singular exact owner; it adds no state, approximation, or platform fork in
  gameplay semantics.
  Next: complete the material gate and refresh the owner profile before the atomic commit.

- 2026-07-19 — `retained`
  Scope: complete correctness/build gate and corrected profile refresh.
  Hypothesis: the source-owner split must remain exact across every platform and state contract.
  Evidence: debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED across
  1,415,476 frames. Native API/copy/save-restore/allocation, PPC, Wasm parity, viewer/browser, 38
  Python tests, source sync, and formatting are green. The corrected profile remains dominated by
  hosted fighter maintenance (28.88%), map collision (13.78%), dynamics (8.78%), and Spaghetti
  input/IASA (7.59%).
  Disposition: packet complete and ready for its atomic implementation/evidence commit.
  Next: commit and select a larger profile-backed final-form owner.
