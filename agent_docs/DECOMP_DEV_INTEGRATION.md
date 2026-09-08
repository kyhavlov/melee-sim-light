# decomp-port-dev into dev — 2026-09-08

## Decision and scope

Integrate `origin/decomp-port-dev` (`22136d7d`) into `dev` (`152f703d`) with a
merge. Their common ancestor is `4a344d56`: 72 incoming-only commits and 11
dev-only commits. A trial merge produced 53 conflicted paths. Replaying 72
commits would repeatedly resolve overlapping character, runtime and validation
changes, so this does not meet the requested easy/clean-rebase criterion.

The integration is prepared on `integration/decomp-port-dev`, based on dev.
Complete it as a local merge and fast-forward local dev to that result. Remote
branches and incoming history are not rewritten or pushed. The original dev
and incoming revisions above remain the reproducible parent checkpoints.

## What the incoming branch adds

| Area | Incoming changes |
|---|---|
| Fighters | Ness, Link and Young Link, including special-move, lifecycle, item/article, extraction, API and viewer wiring. Yoshi was implemented independently on both branches. |
| Ness articles | PK Fire/pillar, PK Flash/explosion, PK Thunder/trail, bat and Yo-Yo. |
| Link family | Bombs, boomerang, bow/arrows, hookshot/tether, sword accessory and Young Link's milk taunt; viewer rendering for their projectiles and caught turnips. |
| UCF | Distinguish classic 0.8 and 0.84 shield-drop rules from replay capabilities; enable extended shield drop in the production profile; default Python cardinal handling to UCF 1.0. |
| Gameplay | Correct counter-shield projectile deflection, FoD platform publication timing, capture-partner pressed-mask ownership, stock-based standings, and several retail fused-arithmetic/rounding boundaries. |
| Memory | Per-fighter article/tether allocator reserves, per-player pose tracks, and larger archive-cache capacity. |
| Validation/tooling | More replays and source-backed classifications; retire an unfrozen transforming Stadium capture; pinned PPC package hashes, compiler/QEMU portability, CI timeout and worker-budget adjustments, host setup documentation. |

The combined suite is dev's 404 recordings plus 30 incoming Yoshi, 30 Ness,
60 Link/Young Link and six Ice Climbers recordings, minus one unsupported
transforming Stadium recording: **529 replays / 5,059,922 transitions**.
Yoshi has 51 recordings after combining both independent corpora; Bowser retains
all 17 dev recordings. The supported roster is now 21 fighters.

## Important merge resolutions

- Keep dev's newer Matching Yoshi callback/article source revision and its typed
  capture-state accesses. Retain incoming common motion-union scalar-layout
  corrections and the source-backed costume-matanim frame publication for
  trackless guard states. The two Yoshi gameplay implementations are not combined
  into alternate dispatch paths.
- Preserve Bowser, normal three-player teams, starting percent, whole-team
  elimination, Python release/warmup support, and dev's pose/collision performance
  representation. Keep the canonical Gekko `acosf` seed restored by the Bowser
  counterexample work.
- Remove silently duplicated Yoshi roster, dispatch, article, data, costume and
  viewer entries. Reconcile the independently implemented Ice Climbers item
  projection masks.
- Retain dev's source-sized Item reserve and full Peach graph reserve. Integrate
  incoming per-fighter allocator terms and source-bounded Shy Guy wave capacity
  through the existing owners. Remove the duplicate capped class-reserve helper.
  Git's automatic merge otherwise passed a numeric item count to a now-boolean
  parameter and retained incompatible duplicate state.
- Link/Young Link OnLoad adds a sword after costume-pose registration. The new
  article-pool smoke caught a landing crash because that accessory had no
  canonical pose node. Construction now inserts it into the existing flat
  preorder and repairs node pointers, parent spans and ECB indices. No generic
  runtime fallback is introduced. Cross-index restore tests cover Ness/Link and
  Young Link/Samus on all six stages.
- The private stream match configuration is **58 bytes**, combining dev's player
  starting-percent fields with the incoming shield-drop capability byte. Rebuild
  all direct stream/Wasm consumers. Regenerate the viewer schema and use its
  offsets in Wasm smoke; the public Python configuration layout remains 52 bytes.
- Preserve one internal-to-external character converter shared by traces and the
  live viewer. Complete its mapping and enable the extended shield-drop flag in
  the live production configuration too.
- Preserve incoming historical performance evidence under
  `performance/IMPORTED_DECOMP_RESERVES_2026-08-26.md` instead of restoring the
  obsolete `agent_docs/CURRENT_BASELINE.md` owner.

## Validation-record changes and evidence

The first combined native run had nine output-lock failures, with no crashes.
They were investigated before recording new identities:

- Seven dev Yoshi recordings classified solely for the guard animation cursor
  become exact. On the no-AObj path, the incoming source-backed costume material
  end-frame owner publishes 100 instead of 0.
- `ClumsyImaginaryOkapi` improves from 43 mismatching rows to one: the same prior
  wall-clamp `pos_x` ULP at frame 7770. Its classification is narrowed accordingly.
- Ness `2025-03_Game_20250304T191933` improves from 184 mismatching rows / 515
  differing fields to exact. Applying only dev's charged-smash fused multiplier
  to an isolated incoming-parent binary reproduces the correction. This retires
  its prior knockback classification.

The repository validator regenerated exactly nine native output locks. Eight
classifications were retired, one narrowed, and none widened. Fresh native and
PPC snapshots agree for all nine affected recordings. Raw stream comparisons
against frozen parents distinguish these source corrections from masked item
pool residue. Investigation outputs are under ignored
`reports/triage/decomp_dev_integration/`.

## Verification completed

All native identities below come from Linux x86-64, GCC 13.3.0. All 24 applicable
PPC toolchain packages were checked against the incoming SHA-256 manifest.

| Check | Result |
|---|---|
| Fresh `make extract ISO=SSBM.iso` | 197 profile files plus `main.dol`; raw manifest verified |
| `make source-check` | Pass; merged upstream inventory and snapshot digest verified |
| `make native-smoke` | Pass, including source/API/context/copy/restore, every costume and article-pool scenarios |
| `make validation-suite` | **470 PASS / 59 CLASSIFIED / 0 XPASS / 0 FAIL / 0 ERROR**, all 529 recordings |
| `make validation-release-supported-domain` | Same complete result and output locks |
| Full `pytest -q -n0` | **58 passed**, including the long Peach reserve regression and added cross-index article restores |
| PPC Yoshi/Bowser/Ness/Links suites | 158 distinct recordings; all residuals accounted for. The nine stale classification cases were rerun on both backends before refreshing their records. |
| `make wasm-smoke` | Native/Wasm digest parity, copy/save/restore and no-growth checks pass |
| Viewer Wasm smoke | All 21 selectors and special callbacks for Yoshi, Bowser, Ness, Link and Young Link pass |
| `make viewer-production-smoke` | Live and production Chrome browser checks pass |
| `make large-batch-smoke` | 256-lane allocation/history/copy lifecycle passes |
| `make lifecycle-benchmark` | Pass; timing is not a controlled throughput comparison |
| `make runtime-census` | 21 fighters × six stages × singles/three-player teams/doubles; no runtime allocation in the retained step probe |
| `git diff --check` | Pass |

Native binary SHA-256:
`f4877653a0c3e38115292864481dfd4186af6131515eb17f0add5881e4507977`.
Release binary SHA-256:
`7f7a1968d58a127c861236bc8c5860ba4434efc1d2c638e7fa56bc79367c3f76`.
PPC binary SHA-256:
`22a3553ee976b4af19ae32e6a65181d15de728320637bdb64370ef5700b85a29`.

## After integration

Re-extract the expanded data profile and rebuild native/Python/Wasm consumers in
other checkouts. Existing extracted profiles and native saved states are not
portable across this representation change.

The gates above cover the merge's correctness checkpoint. A controlled short
resident-256/512 comparison against pre-merge dev now measures median throughput
changes of -1.53%/-0.52%, with matching pre/post digests on identical shared
inputs. This corrects the temporary-manifest profile used in the initial
-1.59%/-0.96% check; the corrected changes remain within that user-accepted
range. This measures the 403 shared recordings, not the expanded 529-replay
workload. Samples, method, limits and the construction census are recorded in
`performance/HISTORY.md`.

A sustained mixed-roster RL/chaos soak, especially four Ness, four Samus, tether
fighters and Peach, remains useful beyond these bounded tests. The complete PPC
aggregate and macOS/arm64 host builds were not rerun; PPC checks focused on the
four overlapping/added character suites. No new classification or gameplay
exception should be admitted merely to make those future checks pass.
