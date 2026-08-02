# `decomp-port-arm64-ppc` review worklog

## Scope

- Review branch: `decomp-port-arm64-ppc`
- Review head: `479d847d` (`Re-record the four icies pool-residue classifications for deterministic identities`)
- Comparison branch: `origin/experiment/decomp-port`
- Merge base: `2df5f02b` (`Remove begin_step from EnvBatch API`)
- Shape at review start: 122 commits ahead, 0 commits behind; 473 changed files,
  50,734 insertions, and 3,352 deletions.
- Primary acceptance criteria: no correctness regression and no material performance regression for
  the existing supported domain. New character packets must also pass their retained exactness and
  output-lock policy.
- This is a review only. Proposed source changes are deferred until after the findings are returned.

## Commit packets

1. Host portability and build infrastructure
   - Native macOS/Clang work, deterministic hosted identities, targeted GCC 14+ diagnostics,
     architecture-matched PPC container/tooling, Wasm/viewer portability, and CI workflow changes.
2. Cross-cutting PPC float and collision fidelity
   - Fused-operation boundaries, MWCC sqrt behavior, trig, matrix/quaternion/vector kernels,
     map/capsule collision, dynamics, spline, and item/fighter publication changes.
3. Luigi packet
   - Character/item source import, suite admission, validation locks, and related fidelity fixes.
4. Mario/Dr. Mario packet
   - Character/item source import, suite admission, collision/dynamics dependencies, and locks.
5. Samus packet
   - Character/item source import, grapple representation, suite admission/extension, and locks.
6. Ice Climbers and CPU-input packet
   - Popo/Nana/items, follower/mimic state, native CPU decision owners, UCF interaction, stage/effect
     RNG work, suite extensions, classifications, PPC locks, and viewer support.
7. Pikachu packet
   - Character/item source import, phantom-revisit gate, suite admission, and locks.
8. Donkey Kong packet
   - Character source import, suite admission, Rollout fused-op work, and classifications.
9. Ganondorf packet
   - Character source import, public/runtime/viewer admission, 30-replay suite, classifications,
     and output locks.
10. Validation corpus, classifications, evidence, and API/viewer surfaces
    - Aggregate manifest expansion, output locks, replay flags, native validator changes, batch API,
      schema/viewer changes, and accumulated investigation records.

## Validation matrix

| Check | Base | Candidate | Result/evidence |
|---|---:|---:|---|
| Static diff/commit audit | reference | complete | 122 commits grouped into 10 semantic packets; no new gameplay heap callsites detected |
| Source/inventory checks | n/a | fail | `source-check` reports stale `src/upstream.lock`; `format-check` reports two trailing-space lines |
| Native debug smoke/tests | n/a | fail | Native smoke passes; 39 tests pass directly, five API tests are masked by a stale ignored `_native*.so`, and one Samus classification test fails |
| Runtime capacity census | pass | fail | Four Sheiks on Final Destination overflow the 1,000-joint fighter-pose capacity during reset and abort |
| PPC smoke/focused validation | reference locks | fail | Smoke passes; focused metadata-preserving validation exposes a stale Ice Climbers PPC classification |
| Wasm/viewer smoke | n/a | pass | Wasm/native parity, live browser viewer, and production viewer smoke pass with the local emsdk on `PATH` |
| Debug aggregate correctness | reference locks | fail | Full 366 selection: 285 pass / 80 classified / 1 Samus fail / 0 error over 3,501,461 frames |
| Release aggregate correctness | reference locks | fail | Same 285 / 80 / 1 / 0 result and frame count; 5.364 s wall, 652,760 aggregate FPS |
| Common-domain release benchmark | reference | fail | Candidate is 12.85% slower at 256 and 13.54% slower at 512 by median cycles/frame |
| Lifecycle/save-restore benchmark | reference | fail | Snapshot is 8.45% larger; save/restore are each about 8.4% slower; debug step probes lose 17-18% FPS |
| New-character coverage audit | n/a | complete | Aggregate and native locks cover all 366; 81 unique classifications; Makefile debug gate alone omits the 25 Ice Climbers replays |

## Findings and experiments

- 2026-08-01: Checked out the branch in the existing workspace as requested. Preserved the
  pre-existing modified `refs/Ishiiruka` submodule and local/ignored artifacts; no clean, stash,
  or secondary worktree was used.
- 2026-08-01: Established the exact remote merge base and captured the 122-commit inventory.
  Initial grouping above is semantic rather than a proposed history rewrite.
- 2026-08-01: Fresh extraction from the checkout's `SSBM.iso` completed successfully and produced
  the expanded `data/raw` manifest. The initial build correctly refused to proceed without it.
- 2026-08-01: `source-check` fails because the imported source snapshot digest differs from
  `src/upstream.lock`. `format-check` fails on trailing whitespace at `agent_docs/ACTIVE_WORK.md`
  lines 40 and 735. Both are branch-owned merge blockers.
- 2026-08-01: Native smoke passes. Direct pytest reports 39 pass / 6 fail, but five failures are
  caused by pre-existing ignored in-place CPython extensions (`melee_sim/_native*.so`) taking
  precedence over the branch's `_native.py`. Preloading the source module makes all seven
  `tests/test_melee_sim_api.py` tests pass. This is local artifact contamination, not a source
  regression; the ordinary test command remains vulnerable to stale in-place extensions.
- 2026-08-01: Samus `master-peach-2026-06` is a real failure. Its retained
  `dolphin-ulp-contact-gate` classification expects fingerprint `7633e4e139723401`, but five
  independent native processes all diverge at frame 12954 and produce five distinct fingerprints
  (`341db679aa0da961`, `874f216786835c27`, `22002f992ed6ec57`, `60cb35e53799fa4b`,
  `906570ca33f9d48e`). The causal gameplay split is stable: retail stops Samus at the platform edge
  while hosted simulation keeps sliding. The full native output lock is stable across processes;
  only the diagnostic mismatch fingerprint varies as downstream residual lanes accumulate.
  Re-recording one observed diagnostic fingerprint would therefore not be valid.
- 2026-08-01: `make validation-supported-domain` silently selects 341/366 aggregate replays because
  Makefile `VALIDATION_CHARACTERS` omits `Ice Climbers`. Result for that incomplete selection:
  285 pass / 55 classified / 1 fail / 0 error over 3,263,807 frames. The branch's claimed 366-case
  gate and the Ice Climbers packet are therefore not exercised by the normal Makefile target.
- 2026-08-01: Manually supplying all sixteen characters selects all 366 aggregate replays in both
  debug and release. Each produces 285 pass / 80 classified / 1 fail / 0 error over 3,501,461
  frames; the sole failure is the Samus case above. The release run completed in 5.364 seconds at
  652,760 aggregate frames/second.
- 2026-08-01: `ppc-smoke` passes. A full aggregate PPC invocation took 483.7 seconds but cannot be
  used as an acceptance gate because the aggregate output-lock file contains only `native` locks;
  all 366 entries were rejected for a missing PPC output lock after executing. Do not repeat this
  aggregate PPC invocation. Focused runs should use the retained per-classification PPC snapshots.
- 2026-08-01: Focused, metadata-preserving PPC validation confirms the Samus contact-gate case is
  exact on PPC, but exposes a real stale Ice Climbers classification for
  `platinum-platinum-83d0989d367f6d26c0df4863.slpz`. Its retained fingerprint is
  `e1001f540ca27351`; current PPC deterministically produces `d08724a45fbeff6d` with the first
  residue row at frame 1613 and the first gameplay follower-position divergence at frame 7665.
  The replay still fails when run through its owning `icies.json` suite metadata, so the branch's
  recorded claim that per-suite PPC passes is stale.
- 2026-08-01: Wasm/native parity, live viewer browser smoke, and production viewer smoke all pass
  after putting the already-installed local emsdk on `PATH`. State digest is `a8a4eceda2e87198`;
  viewer digest is `5d40dda6e777c112` with 16 characters and 6 stages.
- 2026-08-01: No new `malloc`, `calloc`, `realloc`, or `free` callsites were added under `src/`.
  Added `abort()` sites are initialization/identity invariant checks or unsupported-stage stubs,
  not ordinary gameplay control flow.
- 2026-08-01: Public C/Python/runtime admission covers 16 characters, including Pikachu, Donkey
  Kong, and Ganondorf, while the branch's `AGENTS.md` supported-domain list stops at Ice Climbers.
  The intended production contract and the branch documentation are inconsistent.
- 2026-08-01: Static macOS release-profile audit found that `native-release` overrides
  `NATIVE_CFLAGS` with `NATIVE_RELEASE_CFLAGS`, which omits `HOST_ARCH_FLAGS`. The link still uses
  `-arch $(HOST_TARGET_ARCH)`, but most release objects do not, and selected optimized objects use
  `-march=native`. The advertised Apple-Silicon-to-Rosetta `HOST_TARGET_ARCH=x86_64` release build
  is therefore not coherently cross-targeted and needs a source fix or an explicit scope change.
- 2026-08-01: The branch's original eight-character release gate is clean: 121 exact passes, 32
  retained classifications, 0 failures, and 0 errors over 153 replays. The base retained 90
  classifications for its 153-replay aggregate, while the candidate has 81 across all 366
  replays. The branch therefore narrows, rather than widens, classification policy overall.
- 2026-08-01: Adjacent release benchmarks used separately serialized but replay-identical 153-case
  manifests (the benchmark wire changed on the branch), the same extracted data, fixed CPUs, and
  five alternating samples per ref. At batch 256 on CPU 8, median cycles/frame regress from
  50,989.9 to 57,543.9 (+12.85%; 84,172 to 74,585 FPS). At batch 512 on CPU 0, they regress from
  44,345.9 to 50,350.6 (+13.54%; 96,783 to 85,241 FPS). Per-ref digests are stable in every sample;
  the refs have different digests because the branch deliberately changed hosted identities and
  the compare wire. This is a real whole-branch performance regression, not run variance.
- 2026-08-01: Four alternating lifecycle samples independently confirm the regression. The fixed
  snapshot grows from 633,156 to 686,676 bytes (+8.45%); median save goes 0.5265 to 0.5710 ms and
  restore 0.691 to 0.749 ms. Debug lifecycle step probes lose 17.3-17.8% FPS. Game-data init also
  grows from 407 to 717 ms as expected from loading eight additional public characters, but the
  per-match and steady-state costs affect the old domain and are not explained by cold asset load.
- 2026-08-01: Subsystem profiling attributes the steady-state loss broadly to the scheduler rather
  than observation/terminal serialization. The largest changed hot owners are fighter animation,
  action-animation callbacks, `Fighter_8006D9AC`, `Fighter_ProcessHit_8006D1EC`, camera, and the
  finish visibility pass. Static inspection finds two full retained-dynamics matrix refreshes per
  live fighter/frame, an O(fighters-squared) repeated scan in `msl_fighter_cpu_input_live`, and
  unconditional larger Samus FObj/memory-piece reserves even in old-character matches. These
  source-backed correctness changes cannot simply be reverted, but they are the first owners to
  make conditional/incremental inside the final representation.
- 2026-08-01: `make runtime-census` aborts during an ordinary supported configuration before it can
  report storage: four Sheiks on Final Destination register a 1,001st live fighter-pose joint into
  `MSL_FIGHTER_POSE_JOINT_CAPACITY == 1000`. The census still enumerates only the original eight
  characters, so the new public domain is not capacity-covered either. This is a production reset
  failure hidden by the replay corpus and ordinary smoke tests, not a census-only tooling problem.
- 2026-08-01: The aggregate contains 366 unique replay locks and 81 unique classifications. Suite
  counts for the new packets are Luigi 31, Marios 50, Samus 25, Ice Climbers 25, Pikachu 25,
  Donkey Kong 27, and Ganondorf 30. Ganondorf is fully admitted in C, Python, the viewer, and the
  aggregate; it is not a source-only packet.

## Proposed pre-merge changes

1. Repair the Samus platform-edge contact split from its source owner and make diagnostic
   fingerprint construction agree with full-output canonicalization; do not re-record one of the
   varying diagnostic tails.
2. Root-cause the stale Ice Climbers PPC follower-position split and refresh its retained PPC
   classification only after the source behavior is settled. Re-run the focused per-suite PPC
   classifications, not the structurally unusable full PPC aggregate.
3. Recover the old-domain performance budget. Start with incremental/conditional dynamics matrix
   publication, compute the CPU-input-live predicate once per match phase, and make character-heavy
   pool reserves follow the actual configured source demand. Acceptance is adjacent common-domain
   throughput plus lifecycle/save/restore medians at the base level within normal variance.
4. Fix the four-Sheik pose-capacity abort without blindly growing fixed state. Expand the runtime
   census to all 16 admitted characters, both player counts, and all six stages, then derive the
   final admission/capacity boundary from that result and add the failing configuration to smoke.
5. Restore mechanical gates: refresh `src/upstream.lock`, remove the two trailing-space failures,
   add Ice Climbers to `VALIDATION_CHARACTERS`, and make CI run `source-check` plus the same complete
   supported-domain target it claims to gate.
6. Make macOS release architecture flags coherent for native arm64 and the advertised Rosetta
   override, then have the contributor run the native/release/PPC/Wasm checks on actual macOS.
7. Decide and document the public fighter contract consistently across root `AGENTS.md`, C/Python
   APIs, validation defaults, and viewer schema.
8. Once the branch is green, fold the 122 commits into the semantic packets above before a rebase
   merge. There are two merge commits and roughly 30 investigation-log/probe commits; preserving
   that forensic chronology as individual mainline commits would obscure the reviewable changes.

## Open questions

- Whether the character additions are intended to expand the production supported-domain contract
  immediately or land as source-completion/validation packets ahead of public support.
- Whether the final history should keep one commit per character packet or combine the closely
  coupled cross-character float/collision prerequisites with their first consumer.
