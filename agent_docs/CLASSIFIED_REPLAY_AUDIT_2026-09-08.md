# Classified replay audit — 2026-09-08

Implementation follow-up: [the completed closure campaign](CLASSIFIED_REPLAY_CLOSURE_2026-09-08.md) reaches 493 native exact replays. This audit preserves the original 59-exception baseline and hypotheses.

The best next work is source correctness in wall projection, screen-KO camera
publication, Samus grapple integration, and item/input phase ownership. Several
exceptions currently attributed to Dolphin are demonstrably misdescribed. There
is a credible path to retiring a substantial part of the list without rebuilding
presentation, but no evidence yet supports promising 529/529 exact recordings.

This is an audit and recommendation, not a completed fix packet. No gameplay,
comparison policy, classifications, or output locks were changed. The benchmark
documentation amendment is separate from this report.

## Baseline and evidence

- The merged runtime's full native/debug and strict-release checkpoint is **529
  replays, 470 PASS, 59 CLASSIFIED, zero failures**, covering 5,059,922 compared
  transitions. The 59 exceptions account for **16,717 mismatched transitions**
  and 354,198 differing fields: 99.6696% of aggregate transitions are exact under
  the existing comparison contract. Per-replay exactness is 470/529, or 88.85%.
- A fresh native run of all 59 classified replays reproduced every snapshot and
  output lock: 672,239 transitions, zero classification/output drift, zero XPASS,
  zero errors. The full per-field results, first-difference values, recording
  metadata, and action-state context were inspected. No complete retail/PPC
  re-probe was performed for this audit. Earlier retail evidence is explicitly
  identified as retained evidence below.
- Of the manifest's PPC expectations, 45 alias native, ten are separate
  snapshots, and four are absent. These are **hosted PPC** results: they share
  source adaptations, scheduler policy, and omissions with native. Agreement
  between them is useful portability evidence, not independent proof that retail
  agrees or that the recording has a different emulator profile.
- Upstream was fetched and inspected at
  [`64fccd19a0e7c8d54a1da6c56235016b659c354f`](https://github.com/doldecomp/melee/tree/64fccd19a0e7c8d54a1da6c56235016b659c354f),
  September 8, 2026. Its `configure.py` declares all objects Matching; the older
  local reference checkout is `09cfc5b7`. No upstream DOL rebuild was performed
  for this audit, and the production source lock/reference checkout was preserved.
  `chara`/`items` moved to `kinds` upstream; that rename is not a gameplay change.
- Camera, mpColl/mpLib, Fountain, Dream Land, parasol, and Samus grapple were
  already Matching in the old reference. Their inspected changes are principally
  naming, includes, and cleanup. In contrast, **particle and generator completion
  is materially new**: particle was matched/linked in `cca1beea` and generator in
  `7d8b7627`. This improves the basis for completing temporal RNG consumers;
  there is no newly revealed missing fighter function that automatically erases
  these 59 exceptions.

“Exact” here means the repository's existing strict gameplay projection, including
its source-proven item-byte masks and render-visibility diagnostic. Validation
also supplies recorded frame/pre-fighter RNG observations and available stage
events. It is not a claim that every raw Slippi byte or a wholly unseeded RL
rollout matches. See `tools/validation/native.c::{compare_row,refill_write_buffer}`,
`src/runtime/item_projection.h`, and `src/platform/slippi.c`.

## Recommended order

Counts in this table overlap where one replay has multiple independent residuals.
They describe opportunities, not promised closures. Cost assessments are source
estimates; no candidate implementation was benchmarked in this audit.

| Order | Packet | Reach | Feasibility and performance expectation |
|---|---|---|---|
| 1 | **W — wall projection arithmetic** | 14 explicitly attributed standalone cases; three additional candidates and one mixed case | Strong retail-backed lead; medium investigation. A bounded arithmetic correction should be cheap. Do not blanket-enable FMA or replace every division with software emulation. |
| 2 | **C — screen-KO camera transform** | Seven cases; four have no other residual | High-confidence owner, medium investigation. Repair the existing view/inverse/publication path; expected low incremental cost, especially if confined to screen KO. |
| 3 | **A — legacy hitlag-exit input/ASDI** | Two cases, 1,021 mismatch transitions | Medium-confidence new lead; inspect raw/processed stick reconstruction and UCF ordering before platform math. A source/recorder-profile correction should be inexpensive. |
| 4 | **P — parasol teardown; broad old Peach residual** | Two cases; only one is a single isolated sample | High-value phase audit. The isolated case is bounded; the old full-game tail needs several causal boundaries resolved. Small local changes are plausible; moving the whole scheduler is not justified. |
| 5 | **G — Samus grapple** | Three standalone cases | Retained retail evidence favors a real hosted mismatch. Medium investigation; local scalar math/state ordering should have limited cost outside grapple use. |
| 6 | **N/R — needle decisions and Dream Land RNG** | Two confirmed wind/RNG cases and two newly re-owned needle cases | Diagnose bounded missing draw/phase owners first. New upstream particle/generator code helps. Full temporal particle/RNG completion is a larger state/scheduler packet with uncertain cost and must earn a benchmark. |
| 7 | **F/Z — remaining small numeric and signed-zero writers** | Listed individually below | Worth bounded writer probes, especially isolated friction/turnip/boomerang seeds and the three signed-zero-only cases. Avoid a generic float-policy rewrite. |
| Defer | **M — genuinely unrecorded render schedule** | Six magnifier-only cases, plus mixed cases | First exclude camera arithmetic errors. Where the original VI/pad-queue schedule is the missing input, complete decomp alone cannot recover it. |
| Defer | **E/U — capture execution profile or uninitialized bytes** | Two well-arbitrated capture-profile candidates; three residue-only cases | Little RL value in reproducing unknown allocator contents or historical emulator quirks. Keep exact locks and document the limits. |

W's 14 explicitly attributed cases plus C's four standalone cases provide **18
concrete first-pass closure targets**. G adds three more. This is a useful initial
scope, not a forecast that all 21 will fall to one patch. A/P/N deserve early
diagnosis because their errors are more than last-bit drift, even when they do
not immediately retire an entire mixed classification.

## Source findings behind the recommendation

### W: wall-clamp exceptions include proven hosted mismatches

Retained retail engine-dump playback of DK `9560` reproduces the recording while
both hosted backends differ. The earlier worklog explicitly calls this a hosted
residual, yet the manifest still uses `dolphin-ulp-motion-profile`. The implicated
writers are `mpLib_8004E398_LeftWall`, right-wall siblings, intersection/candidate
selection, and the mpColl wall-hug position update. Audit the endpoint/ECB input
bits and each arithmetic store across that complete writer path.

The inspected current source for `mpLib_8004E398_LeftWall` already follows the
retail order at GALE01 `0x8004E618..0x8004E638`: subtract endpoints, multiply,
divide, add endpoint, subtract current X. **That interpolation is not an FMA
site.** Upstream has not supplied a new arithmetic replacement here. This is a
promising small-owner investigation, not an identified one-line fix. Expand from
the retained retail-arbitrated case to both walls, all stage line orientations,
and the additional candidates in the inventory before retiring classifications.

Sources: `src/melee/mp/{mplib.c,mpcoll.c}`, matching
`refs/melee/build/GALE01/asm/melee/mp/mplib.s`, and the July 30 “Session 2” retail
arbitration record in the [historical work log](README.md#historical-work-log).

### C: four “Dolphin/root motion” descriptions are actually screen KO on console

Fresh recording inspection shows **Nintendont**, not Dolphin, for
`GlaringRosyAlpaca`, `WavyRundownAardvark`, `CandidThankfulCockroach`, and
`CapitalPristineTarsier`. Across the entire recorded first-to-last position
residual intervals, their affected fighters are in action **6/7**:
`DeadUpFall`/`DeadUpFallHitCamera`. They belong with `DelayedSuperbGuanaco`,
`CornyDelayedOkapi`, and the screen-KO portion of `ExtraLargeScaryHornet`.

The singular hosted publication owner is
`src/runtime/camera.c::{headless_camera_build_view,msl_camera_publish_match_visibility}`:
it reconstructs the CObj view and applies `PSMTXInverse`/`PSMTXMultVec` to the
screen-KO coordinates. Compare the exact source CObj up-vector/roll, quake,
camera-pass selection, inverse arithmetic, and when `cur_pos` is published.
Current upstream camera cleanup improves names and documentation but does not
by itself establish which of these terms differs. A seven-case retail matrix
probe is a better next step than another fighter-root-motion rewrite.

Four cases are screen-KO-only: Guanaco, Okapi, Alpaca, Aardvark. Hornet,
Cockroach, and Tarsier retain magnifier percent rows even if this is fixed.
Hornet's claimed late DamageFlyRoll interval is gone from the current snapshot.
Tarsier has **no item residual** now: it contains screen-KO positions and two
percent rows. The separate native/PPC position fingerprints on some of these
cases further weaken the old “unspecified Dolphin” explanation.

Sources: current recording action/metadata inspection;
`src/melee/ft/chara/ftCommon/forward.h`; upstream
`ftdrawcommon.c::ftDrawCommon_80080E18_inline2`, `camera.c::Camera_800311EC`,
`sysdolphin/baselib/cobj.c`; and the hosted camera owner above.

### A: the two 0.0375 tails need an ASDI audit before a platform explanation

Puff `diamond-diamond-78251...` first differs by approximately **0.0375 in Y at
4066**, exactly when hitlag reaches zero. Marios `64303_master-platinum...`
first differs by approximately **0.0375 in X at 5182**, also on hitlag exit.
The latter is labeled ULP-scale item/fighter motion, but 0.0375 is roughly
19,661 ULPs at its first X value. Its two later laser positions inherit the
same offset. Both captures are Slippi 3.16 and lack raw C-stick samples.

`ftCo_Damage_OnExitHitlag` applies C-stick or main-stick times `x4BC`; the
3-unit ASDI displacement and one 1/80 stick step give **3/80 = 0.0375**.
At 5182, recorded movement decomposes as previous X + current knockback X +
3.0, while the hosted value corresponds to +2.9625. The recorded processed
C-stick is 0.9875. This is strong reason to audit recorder timing, cardinals,
normalization, and `processed_stick_i8`/pad reconstruction. It is not yet proof
of the exact missing writer. The existing claim that missing Fountain platform
events explain the Puff tail is not sufficient, particularly for the sibling
horizontal tail. Do not inject a 0.0375 correction or infer an input from the
desired post-frame position.

Sources: `tools/validation/native.c::{processed_stick_i8,build_input}`,
`src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag`, UCF
injection in `src/melee/ft/fighter.c`, and recorded hitlag/input/velocity lanes.

### P: a one-sample parasol gap and a separate broad correctness debt

Icies `23549` has exactly one extra closed parasol sample at 2076 when Peach
leaves FallSpecial for Wait. The source has two relevant destruction routes:
fighter teardown (`ftPe_8011D598` -> `it_802BDB94`) and
`itPeachparasol_UnkMotion2_Anim` testing `ftPe_SpecialHi_NotActive`. Slippi's
`SendItemInfo` registers a player-type GObj, plink 7, proc priority 15. Its code
comment about running before player animations is not by itself proof of the
whole frame boundary. Probe the actual GObj order, deferred destruction, and
which item state Slippi samples. Upstream parasol implementation is unchanged
apart from includes/path changes; this needs arbitration, not a source refresh.

`HeartyStiffMallard` is much broader: first X ULP at 785, extra parasol at 970,
then a **real missed hit at 6643** (recorded action 87 and 101.690994%, hosted
action 341 and 88.479996%), followed by action, stock, and death divergence.
It contributes 9,034 of the 16,717 mismatch transitions and 336,847 of 354,198
field differences. It must not be described as only item-slot publication.
Follow the first position seed, then parasol lifecycle, then the first contact
fork; a parasol fix alone is not evidence the entire tail is explained. Preserve
this old capture unless actual unsupported rules/data are demonstrated.

Sources: `src/melee/ft/chara/ftPeach/ftPe_SpecialHi.c`,
`src/melee/it/items/itpeachparasol.c`, `src/melee/it/item.c::Item_8026A8EC`,
`refs/slippi-ssbm-asm/Recording/SendItemInfo.s`, current complete field summaries.

### G: grapple fixes should target the remaining owner, not repeat the old sqrt fix

Three Samus grapple episodes remain. Retained retail playback of DK
`auto-dk-2025-04` agrees with the recording, so generic emulator-profile language
does not settle them. Follow `it_802BA5DC`, rope constraint/wall response, attribute
scratch, and the `cur_pos`/self-velocity feedback during hang and release.

The shared `it_802A3C98` helper already uses Gekko sqrt rounding. Its cross-TU
unfused dot product is intentionally distinct from Link's inlined fused shape;
blindly reusing the Link fused helper would violate the existing asm evidence.
The remaining grapple source has no newly revealed loop since the old matching
reference. A retained scalar correction should have modest cost concentrated in
active grapples. Save/restore must cover any genuinely missing persistent link
state; adding a replay-position correction is not an option.

Sources: `src/melee/it/items/{itsamusgrapple.c,itlinkhookshot.c}` and ledger
entries `canonical:samus-grapple-*`,
`canonical:link-hookshot-chain-solver-gekko-rounding`.

### N/R: new particle completion helps, but the live RNG owner still needs proof

Dream Land Icies `dl-fox-2025-05` and Ness `2026-06` have retained retail draw
traces identifying omitted `grOldPupupu_802113E0` timer draws. The hosted
recorded-wind path returns early after mirroring direction and wind emitters;
it omits the retail timer draws. The resulting stream phase changes blizzard
spawn angle or a needle's survive/bounce decision. Earlier enabling of the
retail wind machine fixed one recording while moving gaps elsewhere, so merely
removing the early return is not a complete solution.

Two more cases deserve targeted RNG/phase investigation:

- Marios `2026-07_Game_20260704T135409`: first needle Y is **1.5 units** different
  at 2244, followed by reflected velocity, state, and lifetime differences. This
  is not a ULP residual. `shootNeedles` uses `HSD_Randi(9)` to select an airborne
  half-unit spawn-height band (`2 * needleYPosScale[rand]`). The first difference
  is consistent with a different band; verify the actual draw and equal
  pre-spawn state before assigning RNG as the cause. There is no fighter-lane
  mismatch in the current result.
- `HumiliatingCreamyReindeer`: one magnifier row at 3939, then needle spawn 49
  survives in the hosted stream at 11333 while the recording has moved on to
  spawn 50. Item count is 3 versus 2, with a 152-frame lifetime/slot tail.
  `it_2725_Logic109_DmgDealt` and related needle callbacks use `HSD_Randi(3)`
  for survival. Probe contact ownership and the draw before choosing between
  RNG omission, contact/phase error, or capture behavior. Calling the complete
  result “float-derived motion” is misleading.

Current `runtime/effects.c` consumes selected effect/model-start RNG but does
not execute the full persistent particle/generator lifecycle. Completed upstream
`particle.c` now exposes lifetime randomization, conditional jumps, child
generators, and later draws explicitly. Some gaps may close by a small existing
owner correction. A larger solution needs one canonical RNG-bearing effect
state, data-defined commands, source ordering, bounded per-match reserves, and
save/restore coverage. Its CPU/memory cost is unknown until implemented and
measured; do not promise exact temporal RNG at zero cost or add replay-local
draw padding. Full graphics execution is not required merely to investigate it.

### M/E/U/Z: distinguish recoverable writers from missing observations

- **Magnifier:** the source can process several pad/gameplay ticks between render
  publications. The hosted schedule publishes once per sim step; standard
  captures do not encode the original VI/render boundary. Correct camera math
  first, but do not claim that upstream completion recovers an absent scheduler
  signal. `TameEmbellishedSparrow` shows why this matters: one unreturned +1%
  tick changes later knockback, landing, and the final death/stock result. It is
  not cosmetic. Keep these exceptions if the remaining difference is truly that
  unrecorded boundary; do not mask percent or seed it from the answer.
- **External execution candidates:** `PositiveRevolvingHyena` and Pikachu
  `master-master` have stronger retained evidence than a native/PPC alias:
  bounded retail interpreter probes reproduced hosted muzzle/transform bits,
  unlike the stored recording. Defer historical emulator-profile reproduction
  until the recording build/settings or a reproducible contrary retail case is
  available. `master-master`'s raw `playedOn` metadata is absent and its suite
  sets `network`; that is not a recorded Dolphin build identity.
- **Uninitialized bytes:** Sheik Chain preassignment `x18` (two cases) and the
  first transformed Zelda L-cancel byte (one case) require unknown historical
  pool contents to match raw bytes. Complete decomp explains the read-before-
  write; it does not provide the missing initial memory. Keep deterministic
  initialization. A separately approved phase-aware comparison policy might
  exclude undefined samples, but that would be a contract change, not a runtime
  replay-exactness fix. Later live Chain scalar values must remain compared.
- **Signed zero:** `peach_demo_1`, `sheik_demo_game`, and Marios `6082` are now
  exclusively attack-velocity signed-zero differences. Their old turnip/needle/
  generic-motion prose is stale. There are additional mixed signed-zero cases
  in doubles and Falcon. The existing diagnostic signed-zero-equal mode makes
  all three cases pass across 23,840 transitions; this was a diagnostic only,
  with strict production scoring unchanged. A bounded sign-producing-writer/retail probe is cheap
  enough to recommend. Do not silently canonicalize zeros in gameplay or scoring
  and count that as a fix; sign bits can matter in downstream numeric operations.

## Complete active inventory

Numbers preserve classification-manifest order. Paths below are relative to
`replays/validation/`; every entry has suffix `.slpz`. Rows count mismatched
transitions, not field occurrences. “Investigate” means the source lead is worth
pursuing, not that a fix has been proved. Packet letters refer to the sections
above; **F** means a bounded scalar/pose/item writer not yet independently
arbitrated. Current manifest IDs are retained here so stale labels are visible.

| # | Replay | Current classification ID | Rows | Audit assessment |
|---:|---|---|---:|---|
| 1 | `aggregate_recent/HilariousVillainousGiraffe` | `vanilla-magnifier-render-schedule` | 1 | **M** — One magnifier percent row; defer missing render schedule after camera check. |
| 2 | `aggregate_recent/PositiveRevolvingHyena` | `unrecorded-dolphin-float-profile` | 1,353 | **E** — 1,353 fighter/laser float rows; retained retail probes favor hosted bits. Defer historical execution profile. |
| 3 | `battlefield_recent/DelayedSuperbGuanaco` | `offline-deadup-render-transform` | 54 | **C** — 54 screen-KO X/Y rows; investigate existing camera transform. Standalone closure target. |
| 4 | `doubles_recent/Game_20260509T152622` | `doubles-dolphin-signed-zero-motion-profile` | 380 | **Z/F** — 331 signed-zero field occurrences plus bounded position residuals; split sign and numeric writers. |
| 5 | `falcon/EmotionalNaturalLemur` | `vanilla-magnifier-render-schedule` | 2 | **M** — Two magnifier percent rows; Nintendont capture. Defer unrecorded schedule. |
| 6 | `falcon/StraightScratchyCheetah` | `mixed-dolphin-ulp-render-profile` | 1 | **M** — Only one magnifier percent row remains; claimed 17 ULP rows are gone. Correct description; schedule limit. |
| 7 | `falcon/falcon_demo` | `dolphin-ulp-and-signed-zero-profile` | 41 | **Z/F** — Four X-position rows plus 37 signed-zero rows; old 13-ULP-row prose is stale. Bounded writer probes. |
| 8 | `fountain_of_dreams_recent/ElatedWearyTermite` | `dolphin-ulp-motion-profile` | 1 | **F** — One frame, two self-velocity fields, 24 ULPs apart during Walk-to-shine on Fountain. Probe friction/entry order. |
| 9 | `ganon/2025-03_Game_20250309T055449` | `transform-swap-stale-lcancel-byte` | 1 | **U** — One L-cancel byte, value 80, on first transformed Zelda frame. Defer unknown pre-reset memory. |
| 10 | `ganon/25827_Game_20250701T195419` | `dolphin-ulp-motion-profile` | 1 | **W** — One Battlefield wall X row. Investigate common clamp. |
| 11 | `ganon/medium-fox-2026-01_Game_20260122T160100` | `dolphin-ulp-motion-profile` | 4 | **W** — Four Yoshi wall X rows. Investigate common clamp. |
| 12 | `ganon/medium-sheik-2026-02_Game_20260218T175200` | `dolphin-ulp-motion-profile` | 1 | **W** — One Yoshi wall X row. Investigate common clamp. |
| 13 | `icies/dl-fox-2025-05_Game_20250504T134832` | `dreamland-wind-rng-draw-gap` | 11 | **R** — Eleven blizzard motion rows; retained trace proves omitted Whispy draws. Complete source RNG owner. |
| 14 | `icies/ys-peach-2025-02_23549_Game_20250221T133850` | `peach-parasol-landing-teardown-window` | 1 | **P** — One extra parasol sample at 2076, FallSpecial-to-Wait. Probe teardown versus item sampling. |
| 15 | `luigi/platinum-platinum-c20d9507b199f01838e901d0` | `dolphin-ulp-motion-profile` | 144 | **F** — 144 mixed fighter/contact and item-position rows; not solely collision-pose X. Separate both seeds before closure. |
| 16 | `marios/2026-07_Game_20260704T135409` | `dolphin-ulp-item-motion-profile` | 135 | **N** — Needle begins 1.5 units low at 2244; later reflection/state/lifetime fork. Probe spawn RNG band and phase; not ULP-only. |
| 17 | `marios/34027_Game_20250422T224627` | `dolphin-ulp-motion-profile` | 66 | **W candidate** — 66 X rows; Dr. Mario FallSpecial at Yoshi left underside (-57.74,-59.28). Include in wall probes; link not proved. |
| 18 | `marios/4308_Game_20250404T231356` | `dolphin-ulp-motion-profile` | 1 | **W candidate** — One Dr. Mario FallSpecial X row near FD right edge (88.71,-13.57). Include in wall/ECB probe. |
| 19 | `marios/6082_Game_20231226T153224` | `dolphin-ulp-motion-profile` | 62 | **Z** — 62 attack-velocity signed-zero rows only. Bounded retail sign-writer probe; no motion/pose tail. |
| 20 | `marios/64303_master-platinum-f57bf677ef721a6e844c0398` | `dolphin-ulp-item-motion-profile` | 651 | **A** — 651-frame tail starts with 0.0375 X on hitlag exit, then inherited laser offsets. Audit ASDI/input reconstruction. |
| 21 | `marios/medium-fox-2026-04_Game_20260413T180551` | `dolphin-ulp-motion-profile` | 1 | **W** — One Dr. Mario wall X row; old capsule-gate family already retired. Investigate common clamp. |
| 22 | `marth/ExtraLargeScaryHornet` | `mixed-unrecorded-render-rng-profile` | 61 | **C/M** — Screen-KO X/Y plus one magnifier row; no late RNG/action residual remains. Camera fix would be partial. |
| 23 | `marth/InternalPowerlessWallaby` | `vanilla-magnifier-render-schedule` | 2 | **M** — Two magnifier percent rows only. Defer unrecorded schedule after camera check. |
| 24 | `peach/CandidThankfulCockroach` | `mixed-dolphin-ulp-render-profile` | 62 | **C/M** — Nintendont screen-KO positions plus two magnifier rows. Current Dolphin/root-motion attribution is wrong. |
| 25 | `peach/CapitalPristineTarsier` | `dolphin-ulp-fighter-and-item-profile` | 60 | **C/M** — Nintendont screen-KO positions plus two magnifier rows; no item residual remains. Camera fix would be partial. |
| 26 | `peach/DisgustingLivelyRaven` | `mixed-dolphin-ulp-render-profile` | 24 | **F/M** — One magnifier row and 23 thrown-item X rows across slots. Probe item integration; render residual remains. |
| 27 | `peach/ExpertWorthlessFinch` | `vanilla-magnifier-render-schedule` | 5 | **M** — Five magnifier percent rows only. Defer unrecorded schedule after camera check. |
| 28 | `peach/HappyGoLuckyScentedButterfly` | `mixed-item-float-and-magnifier-profile` | 20 | **F/M** — 16 item X rows plus four magnifier percent rows; no historical-needle/lifetime residual now. Split owners. |
| 29 | `peach/HeartyStiffMallard` | `legacy-fod-float-and-parasol-publication-profile` | 9,034 | **P/F** — 9,034 rows: X seed 785, parasol 970, missed hit 6643, later stocks/deaths. High-value multi-cause audit; no blanket profile acceptance. |
| 30 | `peach/HumiliatingCreamyReindeer` | `dolphin-float-motion-profile` | 153 | **N/M** — One magnifier row, then 152-frame needle survival/count tail at 11333. Probe contact and RNG; not a float-only profile. |
| 31 | `peach/SmugConfusedTermite` | `dolphin-ulp-motion-profile` | 16 | **F/W** — 15 item Y rows plus one wall X row. Probe turnip/item release/integration and shared wall arithmetic. |
| 32 | `peach/TameEmbellishedSparrow` | `vanilla-magnifier-render-schedule` | 723 | **M** — 723-frame consequence of an unreturned magnifier tick, including last-frame death/stock. Gameplay-relevant schedule limit. |
| 33 | `peach/peach_demo_1` | `ppc-item-float-and-signed-zero-profile` | 10 | **Z** — Ten attack-velocity signed-zero rows only; old turnip gravity residual is gone. Bounded sign-writer probe. |
| 34 | `pokemon_stadium_recent/CornyDelayedOkapi` | `offline-deadup-render-transform` | 58 | **C** — 58 screen-KO X/Y rows. Investigate existing camera transform. Standalone closure target. |
| 35 | `puff/diamond-diamond-78251f79d12df3bcdb1d4abe` | `legacy-fod-hitlag-platform-publication` | 370 | **A** — 370 Y rows from 0.0375 at hitlag exit; ASDI quantization is a new lead. Platform explanation is unproved. |
| 36 | `puff/rollout_ends_ys` | `dolphin-ulp-motion-profile` | 2 | **W** — Two Rollout wall X rows; old speed-decay residual was fixed. Investigate common clamp. |
| 37 | `samus/fox-d18-2026-04_Game_20260419T223341` | `dolphin-ulp-motion-profile` | 1 | **G** — One Samus Y row after grapple wall release. Probe shared rope/release owner. |
| 38 | `samus/master-samus-2026-03_Game_20260302T125058` | `dolphin-ulp-motion-profile` | 135 | **G** — 135-frame final grapple-hang position/self-velocity tail. Probe rope constraints/feedback. |
| 39 | `sheik/GlaringRosyAlpaca` | `dolphin-ulp-motion-profile` | 38 | **C** — 38 screen-KO X/Y rows on Nintendont, not Dolphin motion. Standalone camera closure target. |
| 40 | `sheik/WavyRundownAardvark` | `dolphin-ulp-motion-profile` | 50 | **C** — 50 screen-KO X/Y rows on Nintendont, not Dolphin motion. Standalone camera closure target. |
| 41 | `sheik/sheik_demo_game` | `dolphin-signed-zero-profile` | 15 | **Z** — 15 attack-velocity signed-zero rows only; old needle-byte residual is gone. Bounded sign-writer probe. |
| 42 | `sheik/sheik_demo_game_2` | `fixed-pool-chain-preassignment-residue` | 55 | **U** — 55 Chain preassignment misc3 rows only; old needle-byte residual is gone. Defer raw allocator reconstruction. |
| 43 | `pikachu/master-master-97beba9993588c0e724b06c2` | `dolphin-ulp-fighter-and-item-profile` | 2,499 | **E** — 2,499 laser/fighter float rows; retained retail muzzle probe favors hosted bits. Raw platform metadata absent; suite sets network. Defer. |
| 44 | `pikachu/slippi-2025-11_Game_20251114T162828` | `dolphin-ulp-motion-profile` | 1 | **W** — One Falco Yoshi underside wall X row. Investigate common clamp. |
| 45 | `dk/auto-dk-2025-04_Game_20250408T015844` | `dolphin-ulp-motion-profile` | 150 | **G** — 150-frame Samus rope tail; retained retail agrees with recording. Real hosted investigation, not settled emulator profile. |
| 46 | `dk/9560_Game_20250405T084316` | `dolphin-ulp-motion-profile` | 3 | **W** — Three wall X rows; retained retail agrees with recording. Start common-wall arbitration here. |
| 47 | `dk/22123_Game_20250505T215606` | `dolphin-ulp-motion-profile` | 2 | **W** — Two Dream Land wall X rows. Investigate common clamp. |
| 48 | `dk/basic-dk-2025-04_Game_20250427T125907` | `dolphin-ulp-motion-profile` | 6 | **W** — Six Yoshi wall X rows. Investigate common clamp. |
| 49 | `dk/medium-marth-2025-09_Game_20250912T002037` | `dolphin-ulp-motion-profile` | 2 | **W** — Two Yoshi wall X rows. Investigate common clamp. |
| 50 | `dk/slippi-2025-04_Game_20250422T141746` | `dolphin-ulp-motion-profile` | 1 | **W** — One Dream Land pre-cliff wall X row. Investigate common clamp. |
| 51 | `dk/slippi-2025-04_Game_20250422T152714` | `dolphin-ulp-motion-profile` | 1 | **W** — One Puff Rollout wall X row at FD edge; former speed family gone. Investigate common clamp. |
| 52 | `ness/2026-06_Game_20260616T195545` | `unmodeled-presentation-rng-stream-phase` | 152 | **R** — 152-frame needle lifetime tail; retained retail draw trace proves Whispy RNG gap. Complete source RNG owner. |
| 53 | `ness/2025-02_Game_20250223T112902` | `dolphin-ulp-motion-profile` | 69 | **F** — 69 turnip Y rows from one ULP at carry release, 23119. Probe release transform/store, not every gravity tick. |
| 54 | `links/18932_Game_20250611T185955` | `dolphin-ulp-motion-profile` | 10 | **F** — Ten Link boomerang X rows (item kind 60), 2-ULP seed. Probe boomerang flight/collision; arrow owner is unnecessary. |
| 55 | `links/slippi-2025-01_Game_20250129T125630` | `dolphin-ulp-motion-profile` | 1 | **W candidate** — One DK X row near Stadium left ledge in FallAerial. Probe wall/ECB/cliff writer; not a Link-fighter-specific issue. |
| 56 | `links/slpfiles-2025-10_Game_20251020T000422` | `dolphin-ulp-motion-profile` | 2 | **F** — Two frames of 2-ULP air/ground self-speed at Young Link Run-to-SpecialN on Fountain. Probe friction/action-entry arithmetic. |
| 57 | `yoshi/ClumsyImaginaryOkapi` | `dolphin-ulp-wall-clamp` | 1 | **W** — One wall X row; merged costume action-frame correction already removed the other family. Investigate common clamp. |
| 58 | `bowser/SophisticatedGiganticHeron` | `yoshi-bowser-source-proven-residual` | 1 | **W** — One DK wall X row in Bowser suite. Use common clamp owner, not Yoshi/Bowser dispatch. |
| 59 | `bowser/MildMurkyNewt` | `fixed-pool-chain-preassignment-residue` | 10 | **U** — Ten Chain preassignment misc3 rows, residue 33. Defer raw allocator reconstruction; live scalar remains strict. |

## Classification maintenance and older exception files

The strongest metadata repairs are C's four wrongly labeled Nintendont screen-KO
entries, the two 0.0375 ASDI candidates, the two needle decision/lifetime cases,
and HeartyStiffMallard's broad contact tail. Smaller stale sentences remain in
StraightScratchyCheetah, falcon_demo, ElatedWearyTermite, ExtraLargeScaryHornet,
HappyGoLuckyScentedButterfly, peach_demo_1, sheik_demo_game, and sheik_demo_game_2.
Correct their descriptions as part of the corresponding owner work. A new
fingerprint is not evidence for retaining an old causal explanation. This audit
leaves the manifest untouched so the report's distinctions can be reviewed first.

`replays/validation_exceptions.json` is a separate legacy ledger, not the manifest
used by the current native/PPC suite runner. No repository consumer referencing
that filename was found. It contains 26 Wait-animation RNG entries, representing
13 events duplicated for seeded and rollout probes:

| Dataset | Record/player pairs (player indices are zero-based) | Entries |
|---|---|---:|
| `falcon_demo.slpz` | 226/1, 747/1, 1267/1, 1987/1, 2879/0, 3555/1, 5707/1, 5801/0, 6314/1, 6434/1 | 20 |
| `falcon_demo_2.slpz` | 1093/1, 1597/1, 1837/1 | 6 |

These are not another 26 active classified replays. The current Falcon demo
snapshot has no Wait-animation mismatch. The same legacy file has four
explicit display annotations: Sheik demo position X at record 2452, position Y
at 4078, vertical self velocity at 4077, and magnifier percent in
`Game_20260514T181413.msl` at 2463. They are not validation ignores and are not
counted as active debt here. Archive/remove the unused ledger only in a separately
scoped cleanup; do not resurrect its old RNG list as an implementation queue.

## Evidence and next-packet acceptance

Scratch evidence is under `reports/triage/classification_audit_20260908/`:

- `native-classified.log`: root `make validation-suite` result for the 59-case
  subset, with ordinary strict snapshots/output locks.
- `current-results.json`: all complete field summaries and first mismatch values,
  collected through the same validation runner; all 59 snapshots checked equal.
- `recording-context.json`, `hitlag-and-needle-context.json`: recording metadata,
  state, input, item, and hitlag context supporting the re-ownership above.
- `signed-zero-diagnostic.log`: the existing diagnostic proves the three
  sign-only cases have no additional residual; it does not retire their strict
  classifications.
- `*-upstream.diff`, `upstream/`: inspected upstream owner changes and frozen
  current matching-source checkout.

The subset is flattened from the aggregate with omitted optional keys preserved;
its loaded replay entries and suite defaults are asserted equal to the original.
An initial scratch serialization emitted null optional flags, which the loader
interpreted as false/string overrides. That run is retained as
`invalid-null-profile.log` and is invalid evidence about runtime correctness. The
corrected run is the 59/59 result cited throughout this report. No suite or
production loader behavior was changed.

For a future fix: establish the first differing source writer against retail or
matching asm, retain the canonical owner/state, verify both the target and
neighboring exact cases, then run source/native and full supported-domain gates
on the authoritative Linux/GNU build. Re-record classifications/locks only after
explaining every changed residual. Measure a bounded equivalent-work resident
256/512 comparison when arithmetic, camera scheduling, or RNG state adds material
cost. A particle-state packet additionally needs extraction/layout/load smoke,
per-fighter/stage reserve bounds, and arbitrary-batch-index save/restore coverage.

Do not pursue 100% by removing difficult supported recordings, weakening float
comparison, filling in expected positions/percent, or reproducing arbitrary pool
residue. The useful target is fewer independently demonstrated source gaps, with
remaining capture-information limits stated honestly.
