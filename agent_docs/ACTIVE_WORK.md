# CI and collaborator PR cleanup — 2026-09-08

- Scope: investigate the prior main CI failure locally, verify/integrate open
  collaborator PRs #17–21, close fully addressed PRs, then update dev/main.
- Status: CI recovery and PR review complete; publish the regression test and
  retained audit together on dev/main.
- Previous run 34246202940 at 152f703d failed with an empty server job during
  the aggregate gate, which used an eight-second timeout. Integrated commit 5d596be7
  already retires interrupted pooled runners and gives hosted CI 30 seconds.
- Local experiment: interrupt a real native server after one header byte, then
  validate a short replay through the same pool. Require one error, a successful
  next replay on a replacement process, and both processes reaped. Challenge the
  test against the previous runner lifecycle before running the CI commands.
- Review and reproduction artifacts: reports/triage/ci_pr_cleanup_20260908/.
- Result: the injected interrupted header fails under 152f703d's runner
  lifecycle with a downstream protocol error during pool teardown; the current
  lifecycle reports only the interrupted case and completes the next 16 frames
  exactly on a new process. Both processes are reaped. No production fix beyond
  the already integrated 5d596be7 is needed for the reported failure.
- Local CI commands: the four-worker, 30-second full aggregate passes 503 exact /
  26 classified / zero fail/error over 5,059,922 transitions in 26.783 seconds.
  The workflow's pytest selection passes 57 tests, with its two calibrated
  throughput tests deselected. Fresh source-check and native-smoke pass, including
  the eight article-pool scenarios. GitHub run 34286490957 on published main
  260d9746 completes successfully, including its aggregate gate and pytest.
- PR audit: every commit in each of #17–21 is an ancestor of integration merge
  24643394, already published on dev/main at 260d9746. Checked later changes and
  retained consumers/tests; no omitted implementation remains. All five PRs are
  closed as integrated with individual verification notes:
  - #17 / 14de69d3: hosted CI deselections retained; aggregate coverage remains.
  - #18 / 3d075ac1: Ness source/admission/articles and 30 recordings retained;
    current suite is 30/30 exact.
  - #19 / d59b65ba: six added Ice Climbers captures and projection work retained;
    current suite is 31/31 exact after subsequent corrections.
  - #20 / 23553b91: Link/Young Link source/admission/articles/viewer and 60
    recordings retained; 59 exact / one justified historical boomerang capture.
    The included toolchain and pooled-runner fixes are retained as well.
  - #21 / 3cd7ead3: additive fighter reserves, per-port tracks and JObj floors,
    article stress scenarios and toolchain fallback retained. The final source
    item-pool owner supersedes the temporary per-fighter item estimate and
    separately reserves stage-owned Shy Guys; observation width is not a limit.

# Correctness completion campaign — 2026-09-08

- Status: complete for the current supported roster and corpus. Native debug and
  strict release pass 503 exact / 26 justified classifications / zero failures or
  errors across 5,059,922 transitions. All independently fixable mixed fields are
  closed; the remaining cases require missing capture/execution information.
- Final gates: source/native, full replay locks, 38-case PPC arbitration, 47 API/
  tooling tests, Wasm/live/production viewer, sealed allocation and arbitrary-index
  copy/save/restore pass. The checkpoint-profile worktree and all runtime probes
  are removed. The user authorized committing the completed campaign after
  reviewing the correctness result and retained performance cost.
- Final performance: exact stationary-query optimizations recover roughly half
  the source-lifecycle cost. Eight paired samples per size retain a real
  3.74%/4.29% throughput cost versus 5f097b2a at resident 256/512. Report this
  explicitly; do not call the new packet performance-neutral. See the completion
  report and performance/HISTORY.md for all evidence and the earlier checkpoint's
  separate neutral comparison.

- Authorized scope: after performance verification and checkpoint commit
  `5f097b2a`, eliminate every remaining implementable exception and polish the
  current supported roster/replay domain. Only missing capture information or
  contradictory retail execution evidence can justify a durable exception;
  effort or an incomplete source investigation is not a stopping criterion.
  Commit the completed campaign with its generated expectations and retained
  evidence together, then verify a clean working tree.
- Completion evidence: source-owner coverage, every remaining mixed exception
  inspected for independently fixable fields, full native/release output locks,
  PPC arbitration where relevant, Wasm/viewer parity, arbitrary-index reset/copy/
  save-restore and sealed allocation checks, and retained performance evidence.
  Preserve strict comparison, all 529 replays, recorded inputs and source data.
- First structural packet: FD and shared stage material lifecycle.
  Final owners are imported grLast callbacks, existing grBattle callbacks,
  Ground/ColorOverlay and HSD animation. Canonical mutable state is
  Ground.u.map/last, Ground.color_overlay, and their existing GObj/JObj/AObj
  lifetimes. Immutable command/animation data comes from the stage DAT archives.
  Consumers are stage scheduling, RNG before item callbacks, collision publication,
  observations and Match save/restore. No parallel controller/timer mirror.
- Deletion boundary: remove runtime/final_destination.c/.h and scalar.c's manual
  FD construction/metadata fallback. All supported stages construct through
  StageData. Replace Ground's incorrectly typed +0x40 bytes with the completed
  upstream ColorOverlay member; delete offset-cast access and suppressed timer
  lifecycle. Correct command-pointer typing/loading for FD and Battlefield.
  Retain source actor creation/destruction and animation completion conditions.
- Source evidence: grLast_8021AAB0/B2E8/B5C4/B920 creates map 7 in cases 1/17 and
  destroys it in case 14. The perpetual hosted angular proc therefore consumes
  RNG while retail has no actor (confirmed absence at 12288..12290). Finished
  upstream 64fccd19 identifies Ground.color_overlay; the pinned import remains
  91b9789f and selective completed-source type recovery is recorded explicitly.
- Experiment: implement this final representation, smoke stage construction and
  the traced Samus case, compare retained source state to retail through the
  actor lifecycle, then challenge with remaining cases and the full gate.
  A red intermediate cut is not justification to restore the bypass.
- Scratch: reports/triage/correctness_done_20260908/. Frozen checkpoint native
  debug/release and benchmark executables are saved there before changes.

- First stage gate: 488 pass / 34 classified / 7 fail. Six failures are output
  lock drift (including unchanged PositiveRevolvingHyena classification), and
  one new strict failure is plat-samus FD at 12806..12957. Master-samus FD is
  newly exact. Probe the complete FD controller state/color timer at the new
  RNG boundary against retail before refreshing any locks. Early Falco laser
  residual was rechecked against the frozen checkpoint and is unchanged.

- FD regression trace: controller state/timer match retail (state 15, timer
  1776 at 12806), but hosted fog color is absent. StageCallbacks flags are
  initialized as PPC u32 constants and incorrectly read through native byte
  bitfields. Final representation is the existing numeric flags word with
  explicit source masks at all three consumers; delete its bitfield overlay.
  This restores authored fog loading and color rejection-loop RNG ownership.

- Flags correction result: complete plat-samus FD is exact again; native
  controller/color/timer values match retail at all 21 sampled frames
  12790..12810. Remove temporary diagnostics and validate the stage packet,
  including newly reachable fog relocation and intro callback lifecycle.

- Dream Land structural packet: final owner is grOldPupupu_802113E0's
  existing idle/blink/turn/blow FSM and HSD animation cursor. Canonical xD0
  remains its source timer; xDC remains the published wind direction. Consumers
  are source RNG, force emitters and fighter/device physics. Delete the replay
  early-return FSM bypass, synthetic emitter counter and scalar pre-frame xDC
  publication. Run the source machine, then publish the recorded xDC at the
  recording hook's function epilogue (SendDreamlandInfo.asm, 0x80211BF8), before
  fighter priority-4 physics. Preserve all recorded inputs and exact scoring.
  Compare every Dream Land replay and inspect any remaining temporal divergence
  against retail before changing other owners.

- Dream Land initial source cut: blizzard dl-fox-2025-05 (11 rows) and Ness
  20260616 (152 rows) become exact; all other prior Dream Land exceptions are
  unchanged. Puff Game_20260602T221603 newly forks at 5509. At 5495 retail is
  blowing (xC8=2, xD0=137, xD8=0), while native is idle (xC8=0, xD0=710,
  xD8=1). Recorded xDC agrees, but source force-emitter phase does not.
  Trace the FSM from construction to its first divergence; do not reintroduce
  the synthetic counter or accept the new hit divergence.

- First Dream Land causal fork is Slippi's WhispyBlowDirFix.asm at 8008653C:
  retail ignores the screen-KO fighter (motion_id <= 0xB); native counts its
  camera-dependent bone, creates a tie and draws RNG at 3327. Earlier FSM
  state is exact. Add an explicit immutable whispy_dead_fighter_fix capability
  to the private match config, consumed directly by the ftLib direction-vote
  rule (no second mutable rule copy). Native replay setup derives it from the
  captured Slippi execution profile, verified against embedded hook lists;
  public RL/viewer construction enables the current Slippi patch. Update wire
  sizes, native config producer, viewer schema and construction fixtures.
  Remove the temporary ftLib/Whispy probes after verification.

- Dream Land final result: all 85 replays preserve strict behavior except the
  two intended improvements to exact (11 blizzard rows and 152 needle rows).
  Puff Game_20260602T221603 is exact again. Embedded gecko scan proves the
  Whispy hook in all 78 Dolphin/mainline captures, absent in all 7 Nintendont
  captures, agreeing with the explicit native profile mapping. Private wire
  config grows 58->59 bytes; benchmark wire/cache format advances to v2 and
  viewer offsets are regenerated. No published API struct changes.
- FD fresh extraction and native smoke pass. The full 17-phase lifecycle
  smoke covers source actor loss/recreation, fog and command pointers,
  intro callbacks, copy/save/restore and sealed allocation counts. Expanded
  context smoke includes FD and Battlefield shared-data/global immutability.

- Camera continuation: retain the existing source transform/target and CObj
  representation. Probe Camera_80029C88 inputs against retail around the
  remaining Aardvark frame-1054 eye-X residual to locate the first target or
  tracking arithmetic difference. No camera schedule or scoring change is
  justified by the downstream screen-KO residual alone.

- Camera trace result: all 14 transform fields entering Camera_80029C88
  match retail on both camera copies for all 20 frames 1035..1054. The eye-X
  difference therefore enters through camera translation, not target tracking.
  GALE01 8002A230/8002A234 fuse Camera_8002A0C0's two depth-factor lerps;
  hosted source currently rounds multiplication separately. Restore those two
  fmas and challenge all remaining exceptions before retaining the change.

- Camera-pan result: Guanaco 3->0, Aardvark 1->0 and Hornet 2->1 (only
  its magnifier row remains). Every other remaining replay output is unchanged.
  This closes all native screen-KO position residuals in the inventory. Retain
  the two asm-backed fmas; cross-check hosted PPC and the full gate later.

- Puff yaw owner correction: rollout_ends_ys frame 3630 is motion 342,
  JumpAerialF2, not a Rollout callback. Its root yaw comes from the shared
  ft_800CB6EC aerial-turn helper. GALE01 800CB76C fuses the negative yaw
  increment with the previous rotation (fnmsubs); hosted HSD_JObjAddRotationY
  rounds the increment separately. Restore that exact inline operation and
  dirty-matrix semantics, then compare all remaining replays and nearby
  aerial-turn owners. No new state or collision correction is needed.

- Aerial-yaw result: rollout_ends_ys 2->0, SmugConfusedTermite 1->0,
  and dk/slippi-2025-04_Game_20250422T152714 1->0. All other remaining output
  locks are unchanged. Combined campaign count is 501 native exact / 28
  remaining exceptions before the next full gate.

- Remaining friction packet: ElatedWearyTermite 2705 (Fox Walk->SpecialS)
  and links/000422 7362 (Young Link Walk->SpecialS) both enter
  ft_80084F3C on Fountain's slope. Probe the shared friction inputs and
  subsequent velocity projection against retail; preserve canonical gr_vel,
  acceleration and floor-normal state. Separately, the remaining Luigi pose
  and Link boomerang floats still need their first writer identified.

- Friction trace: Fox's friction and self velocity inputs agree, but gr_vel
  is already different entering ftCommon_ApplyFrictionGround (-0x1.7e2130p-4
  retail vs -0x1.7e212ap-4 native). The shared side-special entry owns the
  seed: ftCo_SpecialS.c::doEnter applies the xB8 momentum adjustment with
  fmadds at 80096624 after a separately rounded negative product. Restore
  that fused addition and compare the two slope cases and all remaining rows.

- Side-special result: ElatedWearyTermite 1->0 and links/000422 2->0;
  all other remaining outputs unchanged. Combined native count 503 exact / 26
  remaining. Run the complete supported-domain gate as a checkpoint before
  further leaf investigation. Hosted PPC still has its separate camera rows;
  native camera position rows are all exact, so compare backend view inputs
  and operations directly rather than classifying the reference discrepancy.

- Full checkpoint: all 529 native replays retain or improve strict results,
  with 503 exact and 26 remaining exceptions; no new strict mismatch. The
  runner reports 83 failures because old direct-output locks are deliberately
  retained: 79 rows are raw exact, and the other four are unchanged/improved
  classifications. Preserve these until their complete output deltas are
  inspected and final native/release identities can be regenerated.
- PPC camera investigation: compare eye/interest, basis vectors and view
  matrix for the same Aardvark frame with native and retail. The reference
  still has ~100 screen-KO rows despite the native camera being exact; do not
  treat agreement between hosted backends as necessary evidence of retail.

- PPC camera cause found: at Aardvark 1054 eye coordinates match native,
  but interest X is 0x1.51c460p+4 vs native 0x1.51c464p+4. PPC trigf.o
  disassembly proves tanf contains R_PPC_REL24 sincosf: GCC O2 combined
  sin__Ff/cos__Ff and bypassed the game polynomials through libc. Native
  already forbids these builtins. Apply the same sinf/cosf restrictions to
  optimized PPC and Wasm math objects, remove temporary camera diagnostics,
  and re-run the PPC exception inventory. Preserve the source math owner.

# Classified replay closure campaign — 2026-09-08

- Pre-commit follow-up authorized: verify that this retained packet has no material
  performance regression, then commit only this completed correctness work.
  Repeat the frozen-merge comparison with ten adjacent pre/post pairs per size,
  alternating pair order, the identical 403-case tapes and 262144 match-frames.
  Retain every sample and quantify the paired ratio uncertainty before committing.

- Authorized: pursue every proposed exception owner from the September 8 audit
  until remaining work is not worthwhile; report the achieved exact count.
  Baseline runtime 24643394: 529 replays, 470 exact, 59 classified. Preserve
  the existing uncommitted audit report/worklog. No commit/push requested.
- Final owners/state: existing mpColl line/ECB state; camera view/KO publication;
  source pad/ASDI, parasol Item/GObj lifetime, grapple ItemLink state; and source
  RNG-bearing effect/stage lifecycle. Consumers are gameplay, strict validation,
  observations and arbitrary-index save/restore. Record any representation cut
  before implementing it; no dual state, replay-row patches, or scoring relaxation.
- Deletion boundary: replace only proven incorrect arithmetic/order/omissions
  within their source owners; remove diagnostic instrumentation after each
  finding. Retire classifications only on exact output with explained changes.
- First packet W/C: preserve existing collision and camera representations.
  Compare emitted PPC arithmetic and bounded retail/source inputs, then test
  source-backed leaf fixes against the classified subset and nearby exact cases.
  Log material attempts/results here and retained performance evidence under
  performance/. Scratch: reports/triage/classified_closure_20260908/.
- C experiment 1: GALE01 cobj roll2upvec 0x80368D00..D40 / DA4..DE4
  uses frsqrte and three fused double Newton refinements. Hosted camera uses
  hardware sqrtf instead. Replace these two radicand calls with the existing
  msl_gekko_sqrtf owner and restore the source double near-vertical predicate;
  test all classified cases before pursuing matrix or scheduling changes.
- C experiment 1 result: all 59 snapshots/output locks unchanged. The sqrt
  correction is source-backed but does not seed this corpus residual.
- C experiment 2: C_MTXLookAt assembly 0x803427E8..2804, 2834..2850,
  2880..289C uses three fmuls followed by two fadds per translation lane.
  Hosted camera incorrectly fuses both additions. Restore the SDK's unfused
  ordered expression; compare the entire classified subset before refreshing
  any locks. This removes arithmetic work rather than adding camera state.
- C experiment 2 result: seven camera cases improve; CornyDelayedOkapi and
  GlaringRosyAlpaca become raw exact. Guanaco 54->3 rows, Aardvark 50->1,
  Hornet 61->2 (one position plus magnifier), Cockroach 62->2 and Tarsier
  60->2 (only magnifier). Old output locks intentionally remain until the
  complete owner correction is verified.
- Retail probe setup: compile a scratch copy of the existing Ishiiruka
  interpreter with PC-scoped GPR/FPR and argument-memory logging; relink a
  scratch archive/executable without modifying the shared reference checkout
  or its binaries. Use it to arbitrate the remaining camera/wall/input writers.
- W retail/native probe result (DK9560, 2119..2121): all 54 LeftWall
  interpolation calls reproduce retail arithmetic for identical inputs. First
  divergence at 2120 is the right-edge query X, before interpolation: retail
  -53.1047554 versus native -53.1047592. Retail desired/current ECB right width
  is 4.66883087 versus native 4.66882896; their vertical coordinates agree.
  Follow ECB bone-origin construction/animation and interpolation, not a
  blanket line-division/FMA replacement. Native diagnostic additions to
  mplib/mpcoll are temporary and must be removed after this packet.

- W experiment 2: ordinary ECB traversal reproduces the same residual. Retail
  joint-chain tracing finds the first difference in an interpolated quaternion,
  before matrix construction. HSD_QuatLib_8037EF28 0x8037EF6C..7C rounds
  the Y dot product and fuses X; both hosted scalar and batch owners reversed
  these. Correct both arithmetic copies, preserving the pose representation;
  test the seed case and full classified subset. Remove all wall diagnostics.

- W experiment 2 result: DK9560 becomes raw exact. Classified subset now
  has 19 raw-exact recordings (17 quaternion closures plus two camera),
  29 changed output identities and 30 unchanged classifications. Run the full
  supported-domain gate before regenerating identities or further changes.

- Full gate after quaternion correction: all original 470 exact recordings
  retain exact output locks; only the same 29 classified identities change.
  Provisional raw exact count is 489/529. No regressions or runtime errors.
- C experiment 3: PSMTXMultVecSR uses rounded X/Y products and sum followed
  by ps_madd for Z. Restore the missing final FMA in the existing SDK owner;
  test the classified subset for the four remaining screen-KO position rows.

- C experiment 3 result: no classified output identities change. Keep the
  source-correct final FMA; it is not this residual's seed. Aardvark retail
  C_MTXLookAt input differs upstream: eye X is 0x1.905f88p+4 versus hosted
  0x1.905f8ap+4; the other eye/interest lanes match. The residual belongs to
  camera tracking state rather than inverse multiplication. Removed temporary
  camera trace. Return to the remaining camera target calculation after G/A/P.

- G probe: compare retail and hosted it_802BA5DC input, link state, scaled
  attributes and constraint output for auto-DK 8654. Determine whether the
  discrepancy starts in the solver or is inherited from tether flight/RNG.
  Temporary tracing is limited to this source boundary and will be removed.

- G finding: auto-DK's 108 normalization calls have equal fighter anchor and
  attributes but already-different rope state at 8654. Flight/attachment is
  equal through 8644; first fork is the first random-gravity link at 8645.
  Retail consumes grIzumi_801CC358 case-0 timer draw at 0x801CC3FC before
  rope gravity; hosted stage-event early return omits it. This is a proven
  Fountain timer/RNG omission, not grapple arithmetic or emulator rounding.
- G/Fountain experiment: final owner is grIzumi_801CC358's existing xC4/xC6
  temporal machine, with recorded height overriding its canonical xD0 only at
  publication. Consumers remain JObj/mpLib and subsequent item RNG. Delete the
  early-return temporal bypass and construction-only synthetic draw helper /
  scalar-stage hook. No duplicate machine, timer cache, or new persistent state.
  Run the classified subset then full gate to test event/state agreement.

- G/Fountain result: auto-DK and the previously unexplained 1.5-unit needle
  spawn/lifetime case marios/2026-07 both become raw exact. Full 529 gate
  preserves all 470 original exact outputs/locks, changes only 31 classified
  identities, and has no errors. Provisional raw exact count is 491/529.
  The singular source machine and removal of the synthetic creation draw are
  retained. All G probes removed before this gate; subsequent G work is bounded
  to the two remaining Samus episodes.

- Remaining G: fox-d18's release row was already fixed by quaternion
  interpolation. Master-Samus's rope first diverges at 15113 after a retail
  FD background angular-boundary RNG draw (grLast_8021ADD0 +0x230), before
  the visible tail at 15146. Existing angular owner starts bit-exact and is
  still exact at frame 8192; investigate later source timeline/RNG drift.
- A source boundary: SendGamePreFrame records fp->input.cstick at 0x8006B0E0,
  before UCF's cardinal injection at 0x8006B460. A normalized 79/80 cannot
  distinguish raw 79 from a radial-clamped raw >=80 with a small perpendicular
  component. The old captures lack raw C-stick; current ASDI consumes the
  correct post-UCF field. Do not infer missing raw values from post positions.
- P probe: trace parasol animation/teardown around icies23549's 2076
  FallSpecial->Wait transition against retail; determine whether the item is
  live at the recorder boundary or the fighter cleanup callback is missing.

- P finding/experiment: retail calls it_802BDB94 from ft_8008A348 at
  0x8008A3E0 during 2076 Wait entry; hosted never calls it. The hosted
  #ifndef suppresses Peach's live-item teardown along with the source
  DownSpot/hammer neutral checks. Restore this source block (all callees
  already admitted), delete the duplicate hosted ground conversion, and test
  both parasol cases plus the classified subset. Final state remains the
  existing Fighter item references and Item/GObj lifecycle.

- P result: icies23549 is raw exact. Hearty's first remaining fork moves
  from 970 to 6482: UCF shield SDI wrongly moves a lightshielding Peach by
  3.9105 units. Its April 2023 Slippi 3.14 embedded Gecko list has classic
  shield drop at C20998A4 and no newer SDI (C208E54C), shield SDI
  (C2093294), extended shield drop (C209A0B8), or pad-buffer/cardinal hook
  (C206B460). The manifest already selected classic shield drop but left the
  newer independent flags enabled. Disabling shield SDI alone makes all
  15,634 frames exact; the complete recorded classic bundle does too. Set
  the three absent capabilities false in peach.json, as done for other
  recorded classic profiles. This is a configuration correction, not a
  scoring/mask relaxation. Provisional count: 493/529.
- G/FD bound: retail and hosted angular states match through 8192, but
  retail never invokes grLast_8021ADD0 during 12288..12290 (engine dump
  confirms playback reached that range), while hosted keeps integrating.
  It resumes before the 15113 RNG fork. Correct closure needs source
  background scene/actor activation lifetime, not another rope-math fix;
  no unproved frame/timer patch retained. All temporary probes removed.

- Final W pass: after quaternion correction, only Puff rollout, Smug's
  isolated wall row and DK/FD 152714 remain among wall candidates. Compare
  Puff's six retail ECB origins at 3630 to classify any remaining shared
  bone/constraint seed before considering another scalar collision change.

- Final W/Z probe result: Puff's residual starts in root yaw (retail
  -0x1.921fb0p+0, hosted -0x1.921fb4p+0), then propagates through three
  ECB bone origins; no new projection correction is justified. Falcon's
  signed-zero exemplar is explicitly retail fnmsubs at 0x8006B9E4 producing
  -0, matching hosted while the historical recording has +0. Keep strict
  comparison and classify that capture execution-profile difference.
- Closure checkpoint: full native gate preserves all 470 original locks,
  with 23 former exceptions raw exact and 36 remaining. No further narrow
  source-backed changes are established. Finish PPC/release, lifecycle and
  bounded throughput verification, regenerate only validated identities,
  and document remaining larger timeline/input/float work.

- Complete: final native debug and strict release supported-domain gates both
  report 493 PASS / 36 CLASSIFIED / 0 XPASS / 0 FAIL / 0 ERROR, over
  5,059,922 transitions. Original 470 exact locks preserved; 32 changed native
  identities are exclusively from the old classified set. Remaining mismatch
  rows 6,804; fields 16,067. Runtime fixes close 22 and the recorded classic
  Peach configuration closes one more.
- PPC bounded cross-check: 21 of the old 59 become exact, 38 remain. Retire
  21 records entirely and remove native snapshots from the two PPC-only camera
  records (38 manifest records / 36 native exceptions). Refresh only measured
  snapshots/locks through validation helpers; cached complete-run verification
  gives native 23 PASS/36 CLASSIFIED and PPC 21 PASS/38 CLASSIFIED.
- Final checks: source-check, native-smoke, Wasm/native state/viewer digest
  parity, 256-match large-batch/lifecycle and save/restore smoke, 31 focused
  manifest/runner tests, and format-check pass. First Wasm invocation lacked
  emcc on PATH; load installed emsdk and rerun successfully. A final gate
  invocation used an unsupported require-output-locks flag and exited before
  execution; rerun with the normal gate's built-in lock enforcement passes.
- Bounded frozen-merge throughput: 119424 -> 119336 FPS at 256 (-0.07%) and
  124215 -> 124554 at 512 (+0.27%), three alternating samples, shared 403-case
  tape workload, equal digests. Performance history/index updated.
- Deliver CLASSIFIED_REPLAY_CLOSURE_2026-09-08.md with all 59 outcomes,
  provenance and remaining owner boundaries. All temporary production probes
  removed; no commit or push. No further narrow established fix is worth
  retaining from this investigation.

# Classified replay feasibility audit — 2026-09-08

- Authorized: catalog all 59 active classified replay exceptions against current
  completed upstream; recommend source-backed closures and explain cost/limits.
  Implementation is deferred except for clearly trivial findings. Do not relax
  scoring, change output locks, or call a metadata cleanup a gameplay fix.
- Baseline: amended merge adba7f4f, 529 replays / 470 exact / 59 classified.
  Read-only upstream snapshot: 64fccd19a0e7c8d54a1da6c56235016b659c354f.
  Preserve the existing refs/melee checkout and production source lock.
- Compare source owners, retained probe evidence, snapshot shapes, and recent
  upstream matching changes. Distinguish hosted native/PPC agreement from retail
  arbitration, and source completion from absent replay capture information.
- Deliver full inventory and ranked feasibility report under agent_docs/.
  Scratch: reports/triage/classification_audit_20260908/.
- Complete: CLASSIFIED_REPLAY_AUDIT_2026-09-08.md catalogs every active entry
  and the separate unused legacy ledger. Fresh native 59/59 snapshots and
  output locks reproduce across 672239 transitions. A diagnostic-only check
  confirms three cases are purely signed zero (23840 transitions).
- Main findings: retained retail probes contradict generic Dolphin labels for
  wall/grapple; four supposed Dolphin motion entries are Nintendont screen KOs;
  two 0.0375 hitlag-exit tails point to ASDI/input reconstruction; two needle
  episodes include non-ULP spawn/lifetime decisions; old Peach tail includes a
  real missed hit. Recommend W/C then bounded A/P/G and N/R investigation.
  No runtime, scoring, classification, or output-lock changes made.

# Pre/post merge throughput check — 2026-09-08

- Measure frozen pre-merge 152f703d against merged b0601914. No gameplay changes.
- Use 403 shared recordings (pre-merge 404 minus the retired unfrozen Stadium
  capture), identical ordinary-input tapes/configuration semantics and lane order.
- Strict GCC release, CPU 0 on the 9950X3D, resident 256/512, 262144 match-frames,
  eight warmup ticks, three alternating pairs per size. Compare digests and
  report any gameplay differences separately from timing. Neither default suite
  expansion nor startup time should be mistaken for steady throughput changes.
- Scratch: reports/triage/decomp_merge_benchmark/. Record results under performance/.
- Audit follow-up: initial flattened manifest serialized absent per-replay flags
  as null; loader treats those as false and played_on null as a literal string.
  Initial measurements remain an equivalent custom-profile comparison, but not
  the intended canonical replay profile. Preserve them under initial-null-profile/
  and repeat the same bounded protocol with absent keys omitted before amending
  the benchmark record. Do not interpret this tooling mistake as runtime drift.
- Complete after the profile correction: median match-frames/s pre/post is
  121806/119942 at 256 lanes (-1.53%) and 125740/125084 at 512 (-0.52%).
  All pre/post digests and normalized tapes match. The corrected changes remain
  within the initial small differences the user accepted; no optimization
  packet. Details, original samples and correction are in performance/HISTORY.md.

# decomp-port-dev integration — 2026-09-08

- Request: evaluate and prepare integration of origin/decomp-port-dev into dev;
  prefer rebase only if clean. Complete the verified local merge; no push.
- Baselines: dev 152f703d; incoming 22136d7d; merge base 4a344d56.
  Worktree was clean. Trial merge-tree: 53 conflicting paths, 72 incoming and
  11 dev-only commits. Use one merge on integration/decomp-port-dev.
- Final owners: retain dev canonical pose/performance, Bowser, team/RL and
  Yoshi source completion; integrate incoming Ness/Link/Young Link source owners,
  per-fighter pool sizing, UCF, common gameplay fixes and validation/tooling.
- Canonical state: existing match-owned Fighter/Item/pose and generated relocation;
  combine capacity requirements inside their existing initialization owners.
- Pool resolution: retain dev source-sized Item reserve plus the source five-Heiho
  wave on Yoshi's Story; combine incoming per-tether ItemLink and per-fighter
  AObj/FObj/GObj reserves. One msl_class_reserve_pieces owner supplies both the
  incoming per-port JObj floor and dev full Peach-item graph bound. Delete the
  duplicate capped class helper and empirical per-fighter Item-count switch.
- Consumers: native/Python/PPC/Wasm/viewer, reset/copy/save/restore, validation.
- Displaced code/deletion boundary: duplicate Yoshi implementations and ID mapping
  become one reviewed owner; preserve both replay corpora. No dual pose indices,
  allocation fallback, duplicate state, restored legacy performance machinery,
  or hand-edited generated replay identities. Regenerate generated layouts/schema.
- Accessory integration found by article-pool smoke: Link's OnLoad inserts the
  sword part after the compact main/interpolation trees were registered. Native
  part flags live in pose nodes, so its unregistered JObj crashes on Landing.
  Final owner is the existing flat pose array: construction inserts the new joint
  into source preorder, repairs node pointers/parent spans/ECB indices, and binds
  the Wasm part index. The source accessory callback owns when this occurs.
  No runtime fallback, separate accessory animation state, or old traversal is
  restored. Verify mixed Link article scenarios plus cross-index save/restore.
- First combined native gate: 462 PASS / 58 CLASSIFIED / 9 output-drift / 0
  ERROR over 5,059,922 transitions. No new replay mismatch signatures. Eight
  dev Yoshi hashes and one incoming Ness hash differ. A captured server stream
  against the frozen dev binary isolates Yoshi's changed action-frame publication
  to the incoming costume-matanim no-AObj owner (100 instead of 0); item-byte
  differences are separately masked pool residue. Compare the incoming parent
  for the Ness hash before changing any retained lock. Build an isolated parent
  binary; do not infer equivalence from aggregate counts.
- Experiment plan: inspect Yoshi differences and all semantic overlaps; prepare
  merge candidate; run source/layout/fresh extraction and native lifecycle gates,
  then combined replay checks and release/Wasm/viewer checks as available.
  Record actual blockers and remaining verification in DECOMP_DEV_INTEGRATION.md.

- Final checkpoint: native and strict release both accept all 529 replays /
  5,059,922 transitions (470 PASS, 59 CLASSIFIED, zero XPASS/FAIL/ERROR).
  Eight classifications retired and one narrowed from source corrections;
  nine locks regenerated only after native/PPC verification. All 58 Python
  tests pass; source/native/article/costume, Wasm and production browser gates
  pass, plus 256-lane/lifecycle smoke and the 378-configuration census.
  Detailed feature inventory, resolutions, evidence and remaining performance/
  long-soak verification are in DECOMP_DEV_INTEGRATION.md.

# Character support — Yoshi, then Bowser (2026-09-07)

- **Viewer ID correction (user report):** Yoshi internal 14 fell through to external
  14 (Ice Climbers), selecting the Popo model. The existing converter in
  `tools/viewer/msltrace1.js` becomes the single internal-to-Slippi-ID owner for
  both trace settings and `live/viewer_adapter.js`. Complete its supported mapping,
  including Yoshi 14 -> 17, and delete the adapter's duplicate converter. No
  gameplay/wire state changes. Verify model identity against the renderer's ID
  names and the production browser's requested animation archive.
  **Result:** fixed; all supported ID/name checks pass, and the rebuilt production
  browser smoke selects Yoshi and confirms external ID 17 plus a completed
  `yoshi.zip` request. Live/Wasm smoke also passes. The previous package-presence
  assertion could accept the wrong already-packaged character archive.
- **Authorized scope:** complete both characters to the existing supported-domain parity,
  including all six stages, singles/three-player teams/doubles, C/Python/Wasm/viewer,
  original full-game replays and focused real Slippi recordings. Separate completed
  character commits are authorized. Preserve the pre-existing deletion of
  `data/stages/slippi_neutral_spawns.json`.
- **Final owners:** Yoshi's lifecycle/guard/special callbacks and egg/tongue/star articles;
  Bowser's lifecycle/special callbacks and flame article; their already-imported common
  capture/victim owners. Source completion covers complete reachable gameplay call graphs,
  not just paths exercised by replays. Current upstream is
  `32420c5f464f211842120c186390a30868b793cd`; all eleven character TUs and five article TUs
  are Matching there, including the now-matched Yoshi Egg Roll. Preserve the hosted
  source layout/names while recording newer source provenance and adaptations in the
  source manifest/delta ledger.
- **Canonical state:** source Fighter/motion/item vars and match-owned contexts remain
  singular. Immutable fighter/article DAT graphs, animation/part tables and effects
  belong to GameData; every live pointer must participate in generated relocation.
  Construction reserves the full reached pool capacity before arenas seal.
- **Consumers:** common fighter dispatch, collision/hit/damage/shield/capture, item scheduler,
  pose/attachments, observation, reset/copy/save/restore, replay validation and live viewer.
- **Displaced code/state and deletion boundary:** remove the newly reached unsupported
  abort stubs and omitted registry rows. Add no approximated moves, mirrored fighter state,
  replay-conditioned gameplay, runtime allocation, compatibility flags, or alternate
  dispatch. Extend extraction, deterministic layout, required loaders and fresh extraction
  checks together; maintain the existing authoritative regression locks.
- **Pose index capacity cut:** the expanded immutable Figa program bank has 267,980
  nodes, exceeding the prior 18-bit node token (262,143 sentinel). The sole
  `MslFighterPoseJoint.program_node_index` becomes a full `uint32_t`; its three
  flags and 11-bit last-table frame share the following `uint16_t`. This uses
  native trailing padding (56-byte joint unchanged) and grows 32-bit joints
  from 44 to 48 bytes. Consumers remain the pose attach/materialize/sample
  owners and generated snapshot layout. Delete the 18-bit ceiling; add no second
  token, extra table, lookup fallback, or gameplay allocation. Validate source,
  native/Wasm layouts, lifecycle relocation, and the complete prior replay locks.
- **Bowser math counterexample experiment:** flame wall/floor steering is exact
  in PPC but has native-only 4- and 19-frame float islands. Test the retained
  x86 `acosf` estimate-seed substitution against the Gekko seed with the same
  three source refinements. The `acosf` return remains the sole result; no
  per-character admission, alternate return evaluator, or extra state. Retain
  a correction only if the source-matched PPC/replay bits arbitrate it, and
  update indexed performance evidence if the prior exactness claim is disproved.
- **Stationary root constraint publication:** native/PPC traces show the
  CaptureWait victim's root position repeating while Bowser's constraint bone
  moves. `msl_fighter_pose_set_root_position` returned before invalidating the
  dependent RObj subtree; PPC's source setter dirties it each publication.
  Keep the canonical flat pose preorder/root matrix owner, but preserve RObj/IK
  invalidation even for identical root positions. Skip redundant ordinary
  matrix products in that case. No cached state, extra schedule, character gate,
  or restored generic traversal; the old early return is the deletion boundary.
- **Sequence and acceptance:** establish baseline; complete Yoshi source/data/registry and
  lifecycle gates; audit and record missing special-move cases; validate full games and
  microreplays; checkpoint the whole domain; then repeat for Bowser. Finish with native
  debug/release, Python lifecycle, source, Wasm/viewer and allocation/save/restore gates.
  Existing classified residuals may not be widened. New residuals require source/retail
  evidence at least as strong as the existing character packets.
- **Completed character checkpoint:** all source/data/API/viewer owners are ported.
  Added 18 full games and 20 original microreplays (190,427 transitions). Debug
  and strict release pass the expanded 404-replay gate: 339 PASS, 65 CLASSIFIED,
  0 FAIL/ERROR, 3,691,888 transitions. PPC agrees with every new residual snapshot;
  all microreplays are exact. The old doubles charged-damage classification is
  retired, with only its prior output lock refreshed. Source/native/costume/parts,
  Wasm/live and production browser, and Python lifecycle/restore checks pass. Detailed provenance and
  explicit remaining recording coverage gaps are in `YOSHI_BOWSER_SUPPORT.md`.
  The character commit excludes the concurrent reward packet's source, hooks,
  bindings and coordination notes.
- **Recording feasibility already verified:** the supplied Slippi Online doublesbot
  AppImage plus the local Slippi-AI/libmelee environment produced original 3.19.1
  Bowser grounded up-B and Yoshi Egg Roll off-edge recordings with normal -123 starts
  and no-contest ends. Offline LRAS needs a release/repress after the retail pause
  timer. Scratch evidence and the controller driver are in ignored
  `reports/triage/yoshi_bowser_recording_probe/`.

# Active performance campaign — 150k resident throughput

## Retired scalar candidate — native `acosf` estimate seed

- **Superseded 2026-09-07:** Bowser flame steering disproved the seed-equivalence claim
  below. The character packet restores the canonical Gekko seed on every backend;
  see `performance/HISTORY.md` for the counterexamples. The following is historical.
- **Owner and canonical result:** `acosf` remains the sole inverse-cosine owner and its binary32
  return remains the only result. Preserve the source radicand, three ordered binary32 Newton
  refinements, `atanf` reduction, exceptional behavior, and every consumer.
- **Displaced work and boundary:** on native x86 only, seed the unchanged refinements with the
  hardware reciprocal-square-root estimate instead of the general software Gekko estimate table.
  Both seeds supply roughly 12 bits and the retained refinements own convergence. Add no table,
  state, cache, approximation at the returned-result boundary, allocation, or compiler control;
  PPC, Wasm, arm64, and every other `__frsqrte` caller remain unchanged.
- **Acceptance:** first require exact benchmark digests and the complete 3,501,461-frame validation
  gate. Then compare the complete aggregate against frozen committed `02cfe013` at resident 256
  and 512. Reject the seed if convergence changes any output or it does not recover material cost.
- **Result:** retain. Replacing the completed reciprocal with `1.0F / sqrtf(radicand)` was not
  exact: full validation exposed eight wall-hug replay failures after quaternion interpolation
  changed two ECB origin matrices and widened the right/left ECB extrema by one ULP. Object-level
  reverse bisection isolated `runtime/math.o`, and restoring the Gekko path removed all failures.
  The final x86 form changes only the estimate seed: `rsqrtss` feeds all three original binary32
  Newton refinements. It preserves the complete 3,501,461-frame gate and both benchmark digests,
  shrinks `acosf` from 205 to 154 bytes, and removes its call to the 345-byte software estimate
  leaf. Non-x86 paths retain the Gekko seed.

## Rejected dynamics product cut — authored natural basis

- **Proposed owner and canonical state:** each hosted `DynamicsData` link would own one exact 3x3
  natural basis compiled from its immutable authored `unk_58` Euler rotation when `lb_80011710`
  binds the link.
- **Consumers and displaced work:** the natural-direction half of `lb_8001044C` consumes that
  basis directly. This displaces one complete `lbVector_CreateEulerMatrix` evaluation—six private
  quintic trig streams plus the exact Euler products—for every solved link on every frame. The
  current animated JObj rotation still uses the live evaluator, and the direction product retains
  its multiply/two-FMA/scale and transform operation order.
- **Deletion boundary:** add no cache lookup, alternate live rotation, gameplay allocation,
  approximation, compiler control, or action/character admission. PPC retains the source layout
  and evaluation. Require exact production digests and controlled gains at resident 256 and 512
  against the frozen pre-cut dynamics-product candidate.
- **Result:** reject and remove. All three exact implementations lose resident-256 throughput:
  parallel packed 3x3 Match state with an eight-argument row kernel loses about 0.3--0.6%, an
  in-place basis over three descriptor regions dead after binding loses about 1.1%, and packed 3x4
  Match state with a six-argument kernel loses about 0.75%. The last form keeps the source fields
  intact and deletes one `lbVector_CreateEulerMatrix` call, but grows `lb_8001044C` by 78 bytes and
  still adds 3 KiB per Match. The saved six polynomial streams do not repay the extra basis loads,
  footprint, and code layout in this solver. Remove the cache and restore the byte-frozen immediate
  owner exactly (SHA-256 `7e0e05b68790d5b14d0fa44f8b76a8e23a2f86682c7e9fc1f96918e247e434cf`).

## Retained collision cut — complete ceiling-pass broad phase

- **Final owner and canonical state:** `mpLib` remains the singular mutable stage-line and range
  owner; `CollData` remains the sole current/previous ECB and collision-result state. No geometry,
  result, or validity state is added.
- **Consumers and displaced work:** the grounded and airborne collision drivers repeatedly enter
  `mpColl_80044AD8_Ceiling`, while the current timed contract records no ceiling response. Before
  its full swept-segment query, test the complete current/previous top-point bounds against the
  same enabled ceiling ranges used by the source query. A negative result deletes the full remap,
  extension, and narrow intersection pass. Wall-connected ceiling resolution retains the source
  path, as do all remapped joints.
- **Deletion boundary:** use a conservative two-unit envelope required by source ceiling endpoint
  extension; preserve line/joint order and every admitted query. Add no cached bounds, index,
  alternate collision path, stage/action admission, approximation, allocation, or compiler
  control. Require exact short/reset digests and gains at both resident sizes against frozen
  immediate control SHA-256
  `7e0e05b68790d5b14d0fa44f8b76a8e23a2f86682c7e9fc1f96918e247e434cf`.
- **Result:** retain. The completed range owner keeps remapped joints and every wall-connected
  ceiling case on the source path, while rejecting only static line ranges whose raw endpoint box,
  expanded by two units for both source endpoint extensions, cannot overlap the swept top point.
  The complete supported-domain gate remains 310 pass / 56 classified / 0 fail.

## Active dynamics product cut — direct constrained/current directions

- **Final owner and canonical state:** `lb_8001044C` remains the singular exact dynamics solver;
  each link's JObj SRT, solver history, world origin, and the carried parent matrix remain the only
  mutable state. The two normalized natural/current direction vectors remain transient solver
  values.
- **Consumers and displaced work:** each live link currently materializes one complete constrained
  3x4 matrix and one complete current 3x4 matrix solely to transform the same child translation,
  subtract the already-known origin, and discard both matrices. A single exact helper now evaluates
  each local basis component with the existing multiply/two-FMA/scale order and immediately feeds
  the unchanged paired-single-shaped transform/add/subtract sequence. This deletes 24 transient
  matrix stores and their reloads per solved link while preserving every binary32 boundary.
- **Deletion boundary:** the later post-constraint parent-matrix publication, Euler/quaternion
  update, source solver order, collision/ground work, PPC/Wasm path, JObj matrices, state,
  allocation, and outputs are unchanged. Add no cached basis, alternate solver, admission,
  approximation, compiler control, or persistent product. Require exact short/reset digests and
  controlled gains at resident 256 and 512 against frozen candidate SHA-256
  `6cb8554595c554f8958b5086c8d21fa196983a1f4b0b0e62e74d697ed097a5b4`.
- **First emitted form:** both digests are exact and the resident-256 C/A/A/C center improves about
  2.0%, but resident 512 regresses about 1.3%. Generated code explains the split: GCC turns the
  local three-row loop into a 1,090-byte alias-checked two-row vector path plus one scalar row and
  grows `lb_8001044C` from 7,532 to 7,807 bytes. Retain the product boundary for one direct emitted
  refinement: express the three independent rows as three exact SSE/FMA lanes, removing runtime
  alias checks and the partial auto-vectorization. Reject the whole cut if that final form still
  loses either size.
- **Final direct form:** retain provisionally. The exact three-lane kernel is 315 bytes and leaves
  the solver's 7,807-byte extent unchanged. Its resident-256 C/A/A/C center improves about 1.5%.
  Four alternating resident-512 pairs change throughput by +4.20%, -5.18%, +2.06%, and +5.17%; the
  one negative arm is an isolated candidate-frequency outlier, while the other three pairs and the
  median center support a roughly +3.1% gain. Both digests are unchanged. Confirm the complete
  retained aggregate against committed `02cfe013` before checkpoint disposition.

## Active owner cut — ordinary compiled-table pose clock

- **Final owner and canonical state:** `MslFighterPoseJoint` remains the singular animation clock,
  and the existing immutable compact Figa samples remain the singular value source for admitted
  integer frames. No clock, pose, decoder, or matrix state is added.
- **Consumers and displaced work:** the ordinary native `framerate == 1`, nonterminal integer
  step publishes the existing table sample directly and updates the same frame/decoder/count
  fields. It displaces entry into the complete 4,150-byte loop/resync/track interpreter for this
  already-direct case. First play, rewind/loop/end transitions, fractional or negative frames,
  missing samples, descriptor animation, filtered publication, and `AOBJ_NO_UPDATE` semantics
  remain in their exact current owners.
- **Evidence and deletion boundary:** the current resident-512 profile records 1,922,947 compiled
  publications among 2,184,225 active joint evaluations (88.04%), while pose animation owns 15.23%
  of the instrumented contract. This cut adds no fallback representation or compatibility bridge:
  the fast case and the mutable decoder are the two existing mutually exclusive owners, with only
  their common clock dispatch shortened. Require exact short/reset digests and controlled gains at
  both resident sizes against frozen candidate SHA-256 `6cb8554595c554f8958b5086c8d21fa196983a1f4b0b0e62e74d697ed097a5b4`.
- **Result:** reject and remove. Both production digests remain exact and `interpret_joint` shrinks
  from 4,150 to 3,519 emitted bytes, but the added common-case admission is not profitable. A
  C/A/A/C resident-256 screen centers near 39,196 cycles/frame for the frozen owner and 39,657 for
  the direct clock (about -1.2% throughput); resident 512 centers near 38,352 versus 40,156 (about
  -4.5%), including one visibly disturbed candidate arm but no evidence of a gain. Restore the
  current singular interpreter exactly. Common-case incidence and emitted size do not outweigh its
  already well-arranged fallthrough and code layout.

## Active code-owner cut — complete decoder-block outline

- **Owner and boundary:** `interpret_joint` remains the exact node clock/table/decoder-transition
  owner. Move its complete `!used_table` decoder materialization, resynchronization, and track walk
  into one private helper; do not split or duplicate any state transition.
- **Displaced work:** the dominant compiled-table path currently enters a 4,150-byte function whose
  rare decoder block carries large stack/register and code-footprint costs. The older rejected
  experiment forced `interpret_track` out of line and added a call for every decoded track. This
  form adds no call on a table hit and at most one block call on a miss, leaving the existing track
  evaluator free to inline inside the slow owner.
- **Acceptance:** first require emitted `interpret_joint` to become materially smaller with one
  cold helper and no new fast-path call, then preserve both exact digests and beat the byte-frozen
  current aggregate at resident 256 and 512. Remove the refactor if GCC folds it back together or
  either size loses.
- **Result:** reject before timing. GCC folds the helper back into its only caller and grows
  `interpret_joint` from 4,150 to 4,200 bytes, so the fast path receives no code-footprint or
  prologue reduction. Remove the helper. Forcing this boundary would require the prohibited
  per-function attributes or a much larger translation-unit split, neither justified by this
  failed emitted-code precondition.

## Current cleaned aggregate against committed checkpoint

- Final release benchmark SHA-256 is
  `45b02500e300c850250385bf9c0dd6e34edd0bcb808f6019b47eebc81ede3318`. It restores the already-
  committed exact negative-wall gate after correctness bisection, retains the exact x86 `acosf`
  estimate seed and ceiling broad phase, and contains none of the rejected tiled/SRT/pointer-backed
  machinery. Three alternating 262,144-frame CPU-0 pairs preserve the committed digests at both
  resident sizes.
- Resident 256 control/candidate pairs are `40,151.7/38,846.5`, `41,980.1/38,234.8`, and
  `42,482.3/38,060.3` cycles/frame: paired throughput gains are +3.36%, +9.80%, and +11.62%, median
  **+9.80%**. Candidate medians are 38,234.8 cycles/frame and 112,253 FPS.
- Resident 512 control/candidate pairs are `43,706.7/40,383.1`, `41,485.3/40,117.3`, and
  `43,006.9/38,966.4`: +8.23%, +3.41%, and +10.37%, median **+8.23%**. Candidate medians are
  40,117.3 cycles/frame and 106,985 FPS.
- This is a qualifying checkpoint against `02cfe013`: both controlled median ratios exceed +5%.
  Absolute FPS is below the prior checkpoint's recorded run, but every adjacent frozen-parent run
  is also slower in this host window, and every candidate beats its paired parent. The campaign
  remains open because neither observed median reaches 150k FPS.

## Active cleanup — lossless pose-sample compaction ablation

- **Boundary:** retain the current exact compiled-Figa publisher, pose-node ownership, native JObj
  semantics, and every independent correctness fix. Remove only the construction-time rewrite
  that converts the original program-relative frame-major sample table into absolute per-node
  constant/dynamic spans.
- **Reason and acceptance:** the compact table was kept provisionally to feed the later contiguous
  tiled publisher, but that publisher was rejected and removed. The compaction itself was neutral
  at resident 256 and only tentatively positive at 512 under contention. Compare the restored
  original table directly with the byte-frozen current aggregate at both resident sizes; retain
  the simpler table only if exact digests hold and neither size loses.
- **Result:** reject the cleanup and retain compaction. The restored original table preserves the
  65,536-frame resident-256 digest, but costs 39,282.4 cycles/frame versus 37,801.5 for the
  byte-frozen compact-table control, a 3.9% throughput loss. Restore the compact representation
  exactly; a failed required size does not warrant a resident-512 run.

## Active reconciliation — exact general matrix concat owner

- **Owner and boundary:** `PSMTXConcat` remains the singular exact affine-product owner. The dirty
  tree currently carries the 12-lane AVX-512 implementation, while the same worklog contains both
  an immediate both-size retained result and a later current-layout rejection. Ablate it only by
  restoring the upstream scalar multiply/two-FMA/translation-FMA stream with complete alias
  handling; inputs, outputs, operation order, state, allocation, and non-x86 behavior are exact.
- **Acceptance:** compare the scalar ablation directly against the byte-frozen AVX-512 owner after
  inline canonical JObj matrices have been restored. Keep whichever exact form improves both
  resident sizes; do not retain contradictory provenance or tune another SIMD width.
- **Result:** retain the AVX-512 owner. The scalar ablation preserves the 131,072-frame resident-256
  digest, but its C/A/A/C symmetric center is about 38,948 cycles/frame versus 37,755 for the
  byte-frozen vector owner, a 3.1% throughput loss. Restore the exact 12-lane implementation
  byte-for-byte and close the contradictory record; a failed required size does not justify a
  resident-512 screen.

## Rejected collision cut — pre-extension line rejection

- **Owner and canonical state:** `mpLib` remains the singular stage-line topology, endpoint, and
  extension owner. `CollLine`/`CollVtx` and the source endpoint-extension arithmetic remain
  unchanged.
- **Consumers and displaced work:** the ordinary floor and ceiling scans may reject a candidate
  before `mpLib_8004ED5C` only when it has at most one connected endpoint and both query endpoints
  lie outside a conservative bound around the raw line. An admitted candidate executes the full
  extension and intersection in source order. This deletes square-root/divide/extension work for
  provably separated lines without adding geometry state, a cache, index, approximation, gameplay
  allocation, compiler control, or alternate result path.
- **Deletion boundary and acceptance:** lines extended at both ends, remapped queries, and every
  PPC/Wasm call retain the source path. Require exact short and reset-crossing digests, emitted
  rejection before `mpLib_8004ED5C`, and controlled gains at resident 256 and 512 against the
  frozen current aggregate. Remove the precheck completely if either size loses.
- **Result:** reject and remove. Both 65,536-frame digests remain exact. Against the nearest frozen
  aggregate, resident 256 improves about 1.25% at the C/A/A/C symmetric center, but resident 512
  regresses about 2.5%. Re-reading topology and raw endpoints before the source extension costs
  more than the rejected one-sided population saves at the larger working set. Restore direct
  `mpLib_8004ED5C` entry and do not add a cached bound or wider admission around this leaf.

## Rejected owner cut — direct immutable-within-frame stage bounds

- **Owner and boundary:** `StageInfo` remains the singular mutable stage-camera/blast-zone owner.
  Native callers read the four camera bounds and four blast-zone offsets through exact header
  accessors instead of crossing tiny out-of-line runtime functions millions of times. The same two
  source fields are loaded and added in the same binary32 order at every call site; no value is
  cached, copied, hoisted across a mutation, or approximated. PPC/Wasm retain the source ABI.
- **Consumers and deletion:** existing camera, collision, fighter death/offscreen, item, and stage
  consumers are unchanged except that native compilation deletes the eight getter call boundaries.
  Add no state, synchronization, allocation, compiler control, or wider camera rewrite. Require
  exact short digests and controlled gains at both resident sizes against the frozen horizontal-
  intersection candidate; remove the inline cut if either size loses.
- **Result:** reject and remove. Both short digests remain exact and resident 256's C/A/A/C center
  appears 4.39% faster, but resident 512 does not reproduce a gain: reverse pairs change from
  roughly +2.6% to -3.0% with execution order, and the four paired ratios center near neutral.
  The compiler also emits several translation-unit-local getter copies rather than deleting all
  boundaries. Restore the singular out-of-line accessors; call count alone does not justify the
  source/header expansion.

## Active collision cut — inline intersection rejection

- **Owner and boundary:** `mpLib` remains the singular source-ordered stage-line intersection
  owner. Inline only its exact four-axis endpoint rejection at the existing query sites and enter
  one out-of-line narrow owner for surviving candidates. This deletes argument setup/call/return
  work on rejected lines without adding geometry state, an index, approximation, compiler control,
  or a second collision result path; PPC/Wasm preserve the same comparisons and narrow arithmetic.
- **Acceptance:** emitted callers must contain the rejection and one shared narrow call, both
  production digests must remain exact, and controlled resident 256/512 timing must beat the
  byte-frozen immediate candidate. Remove the split if either size loses.
- **First result:** retain provisionally while accumulating. Both short and 65,536-frame digests
  remain exact. A C/A/A/C screen centers at about 38,835 cycles/frame for the immediate control
  versus 38,783 for the split at resident 256 (+0.14%), and 41,302 versus 40,053 at resident 512
  (+3.12%). The emitted 610-byte narrow owner is shared by 24 call sites; rejected candidates now
  branch before argument publication. The 256 result needs aggregate confirmation and is not a
  standalone performance claim.
- **Rejected refinement:** inlining only the X rejection and restoring Y rejection to the shared
  narrow owner reduces caller growth, but loses the resident-256 C/A/A/C center by about 2.5%.
  Restore both exact rejection axes at the call site; early Y rejection is demanded work deletion,
  not disposable code-size growth.
- **Horizontal extension:** retained provisionally. The four native call sites preserve the exact
  source comparisons and endpoint ordering before entering the narrow arithmetic. GCC profitably
  inlines the complete narrow body at these four sites rather than emitting the requested shared
  helper: text grows by 2,368 bytes, while rejected lines perform no call or narrow arithmetic.
  Both short digests remain exact. Against the frozen generic-split/part-table candidate, C/A/A/C
  screens improve from 40,108.4 to 39,228.1 cycles/frame at resident 256 (+2.24% throughput) and
  from 41,357.1 to 40,520.7 at resident 512 (+2.06%). This is a retained local result, not a
  checkpoint claim.
- **Rejected vertical extension:** the symmetric exact cut to `mpLineIntersectionV` duplicates its
  narrow body through the four wall-query owners and is not profitable. Both short digests remain
  exact, but the combined horizontal/vertical candidate loses a resident-256 C/A/A/C screen even
  against the slower pre-horizontal control: 37,794.1 versus 38,314.8 cycles/frame (-1.36%
  throughput). Restore the singular out-of-line vertical owner completely; do not spend a
  resident-512 screen on a failed required size.
- **Rejected floor/ceiling refinement:** keep one shared generic narrow owner for all wall and
  secondary query sites, but place its unchanged arithmetic directly in the four dominant ordinary
  and remapped floor/ceiling scans after the retained endpoint rejection. This mirrors the
  profitable horizontal boundary without duplicating the narrow body through every call site.
  Canonical endpoints, comparisons, binary64 FMA/divide order, outputs, and candidate order remain
  unchanged. Retain only if emitted code limits duplication to those four sites, both digests are
  exact, and both resident sizes beat the frozen immediate aggregate.
- **Refinement result:** reject before timing. Factoring the narrow body behind a selectable inline
  wrapper lets GCC propagate it through the nominally shared entry as well, growing release text
  by 9,824 bytes rather than limiting duplication to four sites. That fails the explicit code-size
  boundary and would make a timing result inseparable from broad layout churn. Restore the original
  610-byte shared narrow owner and four call names completely; do not add a compiler attribute or
  a second source copy merely to force this layout.

## Active representation cut — immutable fighter-part membership

- **Final owner and canonical state:** the loaded `PlCo.dat` special-part descriptors remain the
  source definition. Native GameData compiles their exact `(fighter kind, u8 part) -> 1 << index`
  result once into one immutable direct table; no Match or Fighter copy exists.
- **Consumers and deletion boundary:** every existing `ftParts_8007506C` caller receives the same
  value while the native hot owner deletes its repeated pointer chase and linear descriptor scan.
  Out-of-range inputs return the same zero. PPC/Wasm retain the source loop. Add no allocation,
  gameplay mutation, cache invalidation, approximation, compiler control, or alternate result.
- **Acceptance:** exact short/reset digests, then controlled resident 256/512 comparison against
  the frozen intersection candidate. Remove the table and initialization if either size loses.
- **First result:** retain provisionally. The direct owner shrinks from 152 to 73 release bytes and
  preserves both short and 65,536-frame digests. C/A/A/C geometric centers improve about 0.87% at
  resident 256 (38,820 to 38,485 cycles/frame) and 1.62% at resident 512 (39,747 to 39,114) despite
  strong frequency drift in the latter sequence. Confirm as part of the committed-parent aggregate;
  this is not an independent checkpoint.

## Rejected arithmetic cut — scalar quadrant sign publication

- **Owner and boundary:** native scalar `sinf`, `cosf`, and the retained shared-reduction `tanf`
  remain the exact reduction/polynomial owners. Their already-computed quadrant integer directly
  flips the final binary32 sign bit, deleting indexed `+1/-1/0` table loads and factor multiplies.
  PPC/Wasm retain the source path; there is no new state, approximation, call surface, or compiler
  control.
- **Acceptance:** preserve short and reset-crossing digests, then beat the byte-frozen immediate
  control at resident 256 and 512. Remove the cut if either size loses.
- **Result:** reject and remove. Both short digests and the resident-256 65,536-frame digest remain
  exact, and release `sinf`/`cosf` shrink from 576/569 to 364/359 bytes. The controlled C/A/A/C
  screen nevertheless centers at about 37,665 cycles/frame for the frozen owner versus 37,933 for
  the sign-bit form, roughly 0.7% slower. The indexed factor operations schedule better inside
  these scalar helper-heavy functions; do not infer a production gain from the wide kernel's
  independently retained sign selection.

## Rejected arithmetic packet — exact three-angle camera tangent

- **Final owner and canonical state:** scalar `tanf` remains the general exact tangent owner.
  Native x86 `Camera_80029CF8` owns two already-adjacent groups of three transient tangent inputs
  and consumes their three binary32 quotients immediately; no result becomes persistent state.
- **Consumers:** the vertical and horizontal frustum-distance/offset calculations consume the same
  six tangent values in the same source order. Every other tangent call, non-x86 native build,
  PPC, and Wasm retains scalar `tanf`.
- **Displaced work and deletion boundary:** evaluate each three-angle group's identical reduction,
  sine polynomial, cosine polynomial, parity selection, and division in three SIMD lanes. Unlike
  the rejected `msl_sincosf3`, tangent already demands both polynomial families for every scalar
  input; no mixed-lane family is speculative. Delete six scalar call/reduction chains only. Add no
  approximation, lookup table, cache, state, allocation, compiler setting/attribute, or fallback
  dispatch.
- **Acceptance:** prove all six reached tangent bits through exact short/reset digests, inspect the
  emitted kernel for one vector division and no scalar lane calls, then require controlled gains
  at both resident sizes against the frozen shared-tangent aggregate.
- **Result:** reject and remove. Both short and 65,536-frame digests remain exact, and the emitted
  379-byte kernel contains one packed division and no scalar tangent calls. Packing three lanes is
  nevertheless not a production win: resident 256 is only +0.29% on the quieter same-CCD core and
  changes sign on CPU 0, while the resident-512 eight-arm median regresses materially (35,963.1
  control versus 37,146.7 candidate cycles/frame under noisy host load). Stack packing, extraction,
  and the added call boundary outweigh polynomial overlap. Remove the API, kernel, arrays, and
  camera substitutions completely; width-three camera arithmetic is closed.

## Rejected arithmetic cut — native scalar trig helper boundaries

- **Final owner and canonical state:** scalar `sinf`, `cosf`, and the retained exact `tanf` pair
  keep the same reduction, polynomial, quadrant, and binary32 publication owners.
- **Displaced work and deletion boundary:** on native non-Wasm builds, lower the exact absolute
  value and fused negative multiply-add operations directly instead of crossing the retail ABI
  helper boundaries. The current `fabsf__Ff` path performs two calls around one sign-bit clear;
  the pre-tangent diagnostic counts 6,484,122 such calls, and retained tangent adds one per call.
  Add no approximation, alternate polynomial, compiler profile/attribute, state, or fallback.
- **Consumers and acceptance:** all scalar trig callers consume unchanged results; PPC and Wasm
  retain their existing helpers. Require exact short/reset digests and a controlled improvement at
  both resident sizes against the frozen tangent-only binary.
- **Result:** reject and remove. Exact short digests pass, but the candidate loses about 0.85% at
  resident 256 on CPU 0 and 1.64% at resident 512 on a quieter core in the same V-cache CCD.
  Removing the boundaries shrinks `sinf`, `cosf`, and `tanf` by 164 text bytes and shifts every
  downstream hot function, including wide pose trig and matrix publication. The whole-program
  layout loss exceeds the local call deletion. Restore the retail helper boundaries; do not treat
  call-count deletion as sufficient evidence in this translation unit.

## Rejected consumer cut — camera stage-bound snapshot

- **Final owner and canonical state:** `stage_info` remains the sole stage camera-bound and
  scrolling-ground owner. `Camera_8002958C` takes one value snapshot after its subject-admission
  pass and uses it only for the remainder of that synchronous camera-bounds pass.
- **Consumers:** the five boundary classifications and their immediate clamp responses for each
  admitted camera subject consume the snapshot. The public `Camera_80029124` entry point and every
  other camera/stage consumer retain their existing live reads.
- **Displaced work and deletion boundary:** delete repeated `Camera_80029124`, stage accessor, and
  `Ground_801C4368` traversals inside `Camera_8002958C`; compare against the same four binary32
  bounds and publish the same selected bound values directly. Add no persistent cache, duplicate
  state, scheduler seam, approximation, gameplay allocation, or changed public contract.
- **Evidence and acceptance:** the diagnostic 262,144-frame profile records 3,251,810
  `Camera_80029124` calls and 8,283,979 bottom-bound accessor calls. Require exact short and
  reset-crossing digests plus alternating immediate-control results at both resident sizes before
  retention.
- **Result:** reject and remove. Both 4,096-frame digests remain exact. Against a frozen
  tangent-only binary, eight alternating 32,768-frame arms put the resident-256 medians at
  37,215.0 control versus 37,206.7 candidate cycles/frame (0.02%) and resident 512 at 36,316.8
  versus 36,151.2 (0.46%). The optimized production accessors are already cheap enough that
  keeping four bounds live across the subject loop erases the deleted traversal at the smaller
  working set. The added representation is not justified; do not infer production opportunity
  from `-pg` call counts alone.

## Active arithmetic owner — exact scalar tangent pair

- **Final owner and canonical state:** `tanf` remains the singular tangent owner and returns the
  same binary32 quotient of the source-exact `sinf(x)` and `cosf(x)` results. It owns no mutable
  state and changes no caller, input, output, or exceptional-value contract.
- **Consumers:** camera projection, fighter visibility, stage/item helpers, and every other native
  tangent caller continue to call `tanf`. Standalone `sinf` and `cosf`, wide fighter-pose trig,
  PPC, and Wasm retain their existing owners.
- **Displaced work and deletion boundary:** evaluate the shared range reduction and quadrant once,
  then evaluate the same one sine and one cosine polynomial stream and divide the exact published
  pair. Delete only the second reduction, quadrant conversion, small-angle test, and associated
  table loads performed by the current `sin__Ff(x) / cos__Ff(x)` call chain. Add no approximation,
  lookup, cache, persistent state, SIMD setup, compiler control, or alternate tangent result.
- **Acceptance:** exact short and reset-crossing digests first, then alternating resident-256/512
  comparisons against the frozen cleaned aggregate. Retain only if both sizes improve; the
  campaign checkpoint still requires at least +5% controlled throughput over `02cfe013` at both
  sizes.
- **Result:** retain unstaged. The exact 4,096-frame digests are `1177a913e8074ce6` at resident
  256 and `bf73773bda54d9d4` at resident 512; the exact 65,536-frame digests are
  `e6f2a9b270b4161b` and `75046e348333b63b`. Against the frozen immediate pre-change binary, a
  65,536-frame C/A/A/C alternation changes resident-256 geometric centers from about 36,972 to
  36,465 cycles/frame (1.37% faster) and resident-512 centers from about 36,495 to 35,582
  cycles/frame (2.50% faster). The change deletes a duplicated evaluation from every tangent call
  and improves both sizes without adding state or relocating work into a consumer. It remains
  part of the dirty aggregate pending a controlled committed-parent checkpoint.
- **Rejected direct-quotient refinement:** the normal polynomial path multiplies numerator and
  denominator by the same exact quadrant factor (`+1` or `-1`) before dividing them. Publish the
  same quotient directly by parity, retaining the reduction, both polynomial operation streams,
  numerator product, and final division. In the small-angle path, use the same fused sine product
  for even quadrants and the exact signed reciprocal for odd quadrants. Delete only factor-table
  loads, cancelling sign multiplies, and local pair materialization. Require the same digests and
  an isolated both-size gain against the frozen paired tangent binary.
- **Refinement result:** reject and remove. Both short and 65,536-frame digests remain exact, and
  resident 512 improves about 1.5% in an eight-arm same-CCD screen. Resident 256, however, loses
  about 0.75--0.85% in independent eight-arm screens on CPU 0 and CPU 4. The direct quotient's
  smaller code/layout does not satisfy the required both-size throughput shape; retain the
  original shared-pair form exactly.

## Rejected owner cut — demand-owned dynamics matrix refresh

- **Final owner and canonical state:** each retained dynamics JObj keeps its existing canonical SRT,
  dirty flag, and matrix. The solver leaves matrices dirty after publishing the exact solved pose;
  `ftCo_8009CB40`, the source owner that later reads a raw translation column while transferring
  animation ownership, publishes that matrix immediately before the read.
- **Consumers:** dynamic hurt capsules continue to use the solver's exact transient products at
  their existing phase. Any ordinary JObj matrix consumer still enters `HSD_JObjSetupMatrix`.
  Save/restore and copy preserve the same singular dirty/matrix state and must continue exactly.
- **Displaced work and deletion boundary:** delete `Fighter_8006D9AC`'s unconditional post-solve
  walk over every retained dynamics link. Do not add a refresh bit, cache, second matrix, action or
  character admission, approximate transform, gameplay allocation, or alternate solver. The rare
  raw reader pays the complete existing setup; no stale clean matrix may be observed.
- **Acceptance:** exact short digests, dynamics-sensitive replays, native copy/save/restore and
  allocation gates, then controlled resident-256/512 comparison against the frozen cleaned
  aggregate. A complete supported-domain gate is required before retention because future-frame
  animation toggles are the correctness boundary.
- **Result:** reject and remove. Publishing at the later raw reader is incorrect because animation
  has already overwritten the solved JObj SRT; the resident-512 digest diverges after the first
  reset. Keeping the solver-owned `unk_2C` across that toggle restores the exact 65,536-frame
  digest, but loses in both alternating directions at both sizes. Symmetric centers change from
  37,974 to 38,420 cycles/frame at resident 256 and from 37,831 to 38,833 at resident 512. The
  existing end-of-dynamics publication is a correctness owner and is also faster than deferring
  its downstream work. Restore it completely; do not retry this demand-publication cut.

## Active owner cut — terminal ECB origin positions

- **Final owner and canonical state:** the compact fighter-pose ECB closure remains the sole
  stage-collision transform owner. JObj SRT, dirty flags, and matrices remain canonical; a terminal
  ECB origin whose matrix is dirty publishes only its demanded world position and leaves that
  matrix dirty for any later full-matrix consumer.
- **Consumers:** `mpColl_LoadECB_JObj` consumes the same six exact world positions. Any later
  hurt/hit/shield, dynamics, attachment, camera, or source JObj consumer enters the unchanged
  matrix owner and publishes the full matrix on demand.
- **Displaced work and deletion boundary:** construction marks ECB origins that have no other ECB
  origin below them and satisfy the existing ordinary-matrix invariant. At the ECB seam, compute
  their translation column directly from the already-published parent matrix and delete their
  sine/cosine evaluation, local 3x3 SRT construction, world-basis product, scale-sidecar mutation,
  and clean publication. Add no cached position, second matrix, action/character admission,
  approximation, gameplay allocation, scheduler seam, or compatibility path.
- **Result:** reject and remove. The position-only path preserved both short and 65,536-frame
  digests. Resident 256 improved at the symmetric center by about 2.2%, but resident 512 lost in
  both directions (`36,510.9 -> 36,706.4` and `37,284.0 -> 38,399.3` cycles/frame). Deferring the
  terminal matrices moves demanded work into later geometry consumers and harms the larger working
  set. Restore full matrix publication; do not extend this into another ECB product cache.

## Rejected representation packet — gameplay-live compiled Figa samples

- **Final owner and canonical state:** `MslFighterPosePrograms` remains the one immutable exact
  integer-frame sample owner. Its program/node identities and source Figa streams remain unchanged,
  but the compiled value stream contains rows only for program nodes that can bind a retained
  native fighter JObj. Omitted presentation-only joints keep no compiled samples and never acquire
  mutable pose state.
- **Consumers:** the existing compact fighter-pose publisher consumes the same exact binary32
  values through the same program/node identity. Motion binding, fractional/non-unit decoder
  transitions, JObj SRT/dirty state, matrices, collision, dynamics, contact, and output ordering do
  not change.
- **Displaced state/work:** delete exact sample rows compiled for Figa nodes whose corresponding
  fighter part is structurally replaced by the cold JObj sentinel. This is not demand caching:
  GameData construction derives the complete admitted set from every bound fighter motion and the
  construction-owned gameplay-part masks before allocating the final sample stream.
- **Deletion boundary:** no second value table, runtime cache, lazy gameplay allocation, replay or
  character heuristic, compatibility bridge, approximate encoding, or decoder fallback for an
  admitted integer row. A cold-node attachment is an initialization invariant failure. PPC/Wasm
  retain their existing source-shaped representation.
- **Acceptance:** first measure the exact row/byte ceiling from the full 16-character motion bank.
  Retain the representation only if it deletes a material fraction of the 19,855,156-value
  (75.7 MiB) stream, preserves all exact production outputs and allocation/save-restore contracts,
  and improves both resident sizes against the frozen dirty aggregate. The campaign checkpoint
  still requires at least +5% over committed `02cfe013` at both sizes.
- **Rejected encoding preflight:** the current stream has 5,989,205 globally unique float bit
  patterns. The hottest 65,535 cover only 58.18%; a lossless escape stream would average about
  3.68 bytes/value before its dictionary and add a branch to every publication. Per-program
  16-bit dictionaries are also nonviable: 3,309 dictionaries contain 9,647,090 values in total,
  reducing 79,420,624 raw bytes to only 78,298,672 bytes before alignment/metadata and adding an
  indirection to every load. Do not implement value dictionaries; remove cold rows at their real
  ownership boundary instead.
- **Result:** the complete construction-time closure retained 14,333,871 of 19,855,156 values,
  removing 5,521,285 values (21.1 MiB, 27.8%) with no gameplay cache, state, or fallback. Runtime
  admission survived all 366 benchmark pre-roll sources and both exact short digests. An isolated
  same-source all-row/compact-row ABBA, however, changed the resident-256 geometric center from
  38,747 to 39,096 cycles/frame (-0.90% throughput) and resident 512 from 38,177 to 38,063
  (+0.30%). The compact layout helps dramatically on the non-V-cache CCD, but the canonical
  V-cache benchmark already absorbs the immutable footprint; removing cold rows does not delete
  executed publication and slightly harms the smaller batch. Remove the packet completely. The
  detailed attempt record is
  `agent_docs/performance/attempts/2026-08-04-gameplay-live-figa-samples.md`.

## Active arithmetic packet — exact pose/ECB trig product

- **Final owner and canonical state:** `msl_sincosf_many` remains the one exact native wide
  sine/cosine owner; canonical JObj SRT and matrices remain unchanged. Its immediate
  `HSD_MtxSRTConcatTrig` consumers retain the source-ordered local-to-world product.
- **Consumers:** compact fighter ECB publication and the existing exact fighter-pose blend/matrix
  paths. PPC/Wasm and scalar tails retain their current owners.
- **Measured boundary:** the current 4,096-frame resident-512 Callgrind window assigns 7.44% self
  instructions to `msl_sincosf_many` and 3.62% to `HSD_MtxSRTConcatTrig`. No other untouched self
  owner reaches 1.5%. This is an instruction profile for selection, not a throughput result.
- **Displaced work:** derive the quadrant sign directly from the already-computed integer quadrant
  and select the exact sine/cosine polynomial result by parity. Delete multiply-by-zero/one
  factor construction and application in the normal wide path; then simplify only product work
  made redundant at the immediate matrix consumer. Preserve reduction, polynomial/FMA order,
  signed results, small-angle behavior, state, allocation, and publication order.
- **Deletion boundary:** no lookup/cache, precomputed stream, alternate pose, consumer bridge,
  approximation, compiler setting, phase seam, or scalar fallback is added. If exact sign-bit
  selection or the complete immediate product is not faster at both resident sizes, remove that
  portion rather than widening the packet.
- **Acceptance:** exact short production digests first. Measure against the frozen dirty aggregate,
  then run controlled alternating committed-parent/candidate comparisons at resident 256 and 512.
  The campaign checkpoint still requires at least +5% over `02cfe013` at both sizes.
- **First retained form:** exact sign-bit selection removes quadrant factor construction and six
  factor applications from each non-small wide block without changing the 612-byte kernel extent.
  Both 32,768-frame production digests remain exact and native smoke passes. A 65,536-frame ABBA
  screen against the frozen dirty aggregate improves resident 256 by 2.23% at the symmetric
  center (`40,444.3 -> 39,561.8` cycles/frame). Resident-512 controls center at 42,056.5 and the
  three candidate arms are 39,509.6, 39,765.1, and 40,205.5 cycles/frame; the candidate median is
  5.76% faster. These are immediate-candidate screens, not the committed-parent checkpoint; retain
  the exact cut while running long parent/candidate controls.
- **Isolated kernel confirmation:** a production-flag AVX-512 harness over 80 representative pose
  angles runs the frozen owner at 111.49/113.44 cycles per call and the final blend-before-sign
  form at 96.72/96.74 cycles per call, a 14.2% paired-median kernel reduction with the same output
  checksum. The production-prefix digests independently prove exact reached outputs. This confirms
  the deletion itself despite whole-program timing being temporarily contaminated by live external
  Path of Exile, Dolphin, and Chrome workloads; during that load the unchanged frozen dirty binary
  ranges from 37.7k to 42.8k cycles/frame, so no checkpoint claim uses those runs.
- **Immediate-product screen, rejected:** only 1,577 of 203,502 profiled concat calls require
  non-unit parent-scale correction. A separate exact unit-scale leaf shrinks the common function
  from 566 to 364 bytes and is bit-identical. In-process timing is 16.998--17.391 cycles for the
  generic unit-scale call and 15.291--15.568 for the leaf, but production must perform the same
  three parent-scale tests before choosing that leaf; the isolated reduction merely moves those
  tests to the caller. Its resident-256 whole-runtime symmetric center is only 0.56% faster under
  load and the 512 arms are invalid. Restore the singular matrix owner rather than keep a second
  call surface with no defensible deleted work.

### Whole-block canonical-zero admission

- **Owner and canonical state:** `msl_sincosf_many` remains the singular exact owner of every
  input angle and output pair. Its callers, ordering, arrays, and matrix/quaternion consumers do
  not change.
- **Displaced work and deletion boundary:** before range reduction, admit an AVX-512 block only
  when every active input is bitwise canonical `+0.0F`; publish the exact `sin=+0.0F` and
  `cos=1.0F` results directly. Negative zero, every nonzero value, partial mixed blocks, PPC,
  Wasm, and non-AVX-512 native execution retain the complete existing evaluator. This deletes
  four reduction FMAs, integer quadrant conversion, both polynomial streams, and lane selection
  for an admitted block without adding state, a per-lane branch, a lookup, approximation, or a
  second evaluator.
- **Why this is a distinct bounded revisit:** the rejected zero-Euler candidate placed a hot
  per-lane test in the general path and was mixed at resident 256. This test is one block-level
  predicate over the already-loaded vector and is valuable only if ordinary pose/matrix batches
  naturally contain all-zero blocks. Require exact short digests and a controlled gain at both
  resident sizes against the frozen post-sign-selection aggregate; otherwise remove it fully.
- **Result:** reject and remove. The exact resident-256 digest remains
  `e6f2a9b270b4161b`, but a 65,536-frame ABBA under the same live host load gives controls
  `37,960.8`/`40,701.5` and candidates `40,093.6`/`41,727.3` cycles/frame. Both directions lose;
  the symmetric center is about 4% slower. One extra vector test and hot-loop branch cost more
  than the admitted whole-zero blocks. Do not spend a resident-512 arm or revisit zero-angle
  admission inside this owner.

## Active structural preflight — common fighter action-phase ownership

- **Final owner:** each supported common MotionState family owns its animation, input, physics,
  and collision callbacks directly. The source scheduler retains the four existing phase cuts and
  fighter order; character-specific motions retain their distinct source owners.
- **Canonical state and consumers:** `Fighter`, its current `motion_id`, `MotionState`, input,
  motion variables, `CollData`, and pose remain the only gameplay state. Later hit/contact,
  dynamics, camera, observation, copy, and save/restore consumers continue to read those owners.
- **Candidate deletion:** for a common family with material measured coverage, replace its repeated
  callback shells and duplicated generic transition/predicate work with one purpose-built exact
  implementation per existing phase. Delete the admitted source callbacks from the hosted path;
  do not put a dispatcher in front of the same work or retain an admitted fallback.
- **Boundary:** no batch suspension, callback tape, alternate action state, synchronized summary,
  action/replay training list, gameplay allocation, compiler control, or approximate arithmetic.
  A family is admitted by source MotionState ownership, not benchmark identity. PPC and Wasm keep
  the upstream callbacks until a portable final path is justified.
- **Preflight:** attribute callback cycles and calls by current MotionState and phase on one bounded
  resident-512 production window. Proceed only if a small coherent common family owns enough of
  the complete contract for a multi-point gain after required physics/collision work is excluded.
  Remove all attribution code before production timing. If coverage is diffuse or dominated by
  irreducible shared map/pose work, close this cut without writing a callback framework.
- **Acceptance:** exact production digests, then focused family transitions and the complete
  supported-domain/allocation/copy/save/restore gates. Controlled resident-256 and resident-512
  comparisons must improve the retained dirty aggregate; the campaign commit still requires at
  least +5% over `02cfe013` at both sizes.
- **Result:** close before implementation and remove the attribution. The 16,384-frame
  resident-512 profile assigns 6.90% to action animation, 4.37% to input, 0.83% to physics, and
  12.22% to collision callbacks, but the per-motion split shows no new coherent direct owner.
  Guard/GuardOn account for about 3.6% through the already-closed exact two-blend pose pipeline;
  Wait is about 1.7% including shared stage collision, and Dash, Fall, Jump, Landing, attacks, and
  damage are diffuse. Nearly all apparent remaining scale is the same canonical pose and `mpColl`
  work measured by their existing owners. A common-action executor would therefore add dispatch
  and duplicate hundreds of callbacks without deleting a multi-point operation boundary. Do not
  build it; select the contact event lifetime below instead.

## Active representation packet — singular ordinary main-pose clock

- **Final owner and canonical state:** one compact pose span owns the ordinary main fighter Figa
  frame/rate/loop state. Its member joints own only authored channel publication and exceptional
  decoder state; they do not mirror the span clock while the span is admitted.
- **Consumers:** `msl_fighter_pose_animate_parts`, frame/end queries, animation requests/rate
  changes, transition/blend handoff, and decoder resynchronization consume that same span owner.
  JObj SRT and matrices remain the singular published geometry consumed by collision/contact.
- **Displaced work/state:** replace the measured per-node clock/loop/table-admission interpreter
  for the ordinary main-pose population with one source-ordered frame advance followed by one
  contiguous frame-row publication walk. Delete follower clock updates and repeated program/sample
  lookup; do not add a second fast-path clock, copied result cache, callback tape, or batch seam.
- **Exceptional transition:** a fractional/non-unit/descriptor or independently authored mutation
  explicitly materializes the span clock once into its member decoder states and transfers
  ownership to the existing scalar path. A complete main-tree animation bind re-establishes the
  singular owner; there is no simultaneous synchronized state.
- **Bound and acceptance:** this packet is confined to compact fighter pose state, attachment/rate
  APIs, and the existing span consumer. Prior measurement found 1,618,766 ordinary main-pose nodes
  with identical clocks/program order and assigned 9.74% self instructions to repeated node
  interpretation. Require exact production digests, full validation/copy/save-restore/allocation,
  and controlled gains at resident 256 and 512. The retained boundary must be complete; an audit,
  mirrored follower shortcut, or setup-only representation is not a result.
- **Current result, not retained:** the singular clock, explicit exceptional materialization, and
  one frame-row publisher preserve both short production digests. After removing the old per-node
  API handoff, two resident-256 65,536-frame directions improve `40,980.4 -> 40,721.0` and
  `39,758.0 -> 39,378.7` cycles/frame (+0.64%/+0.96%). The earlier clock-only form was similarly
  +0.57--0.75% at resident 256 and had a clean -3.92% resident-512 reverse, proving that clock
  deletion alone cannot realize the prior 9.74% inclusive instruction attribution. A follow-up
  omitted `HSD_JObjCheckDepend` only when publication provably dirtied the same node; it remained
  exact but changed the two resident-256 directions to -1.10%/-0.24%, so that omission is removed.
  The packet remains unstaged and unaccepted pending a stable 512 measurement, but its measured
  ceiling no longer justifies expanding pose state or geometry scope on setup value alone.
- **Final result:** reject and remove the complete packet. The 262,144-frame resident-256 ABBA
  pairs change `39,625.4 -> 40,190.9` and `39,158.6 -> 38,927.4` cycles/frame, leaving the
  symmetric center about 0.4% slower. Resident 512 changes `41,686.3 -> 44,074.5` and
  `42,962.1 -> 42,874.4`, about 2.6% slower by symmetric center. Digests remain exact. The
  singular clock deletes real interpreter work, but ownership admission, exceptional
  materialization, and the row publisher cost more at the complete working set. Remove the clock
  bit, reused track index, span publisher, API redirections, and all group branches. Do not revisit
  per-node pose clocks without also replacing the downstream JObj publication/consumer layout;
  the measured scalar clock boundary is closed.

## Active code-owner cut — resident Fighter pointer in exact native owners

- **Owner and scope:** preserve the existing `Fighter` object as the only gameplay-state owner.
  In the central O0-exact fighter scheduler, keep the already-fetched `Fighter*` in a C register
  for the lifetime of each source owner instead of reloading its stack slot before nearly every
  field access. This changes pointer storage only: no state, ordering, arithmetic, allocation,
  callback, ABI, or compiler setting changes.
- **Evidence before expansion:** `Fighter_ProcessHit_8006D1EC` shrinks from 3,492 to 2,789 native
  bytes. Two 65,536-frame resident-256 reverse pairs against the byte-frozen immediate control
  preserve digest `e6f2a9b270b4161b` and improve paired cycles/frame by 2.09% and 1.31%.
- **Bound:** apply this first to the 32 local Fighter fetches in `fighter.c`, which owns the shared
  per-frame scheduler phases. Retain or expand into other hot source owners only if alternating
  whole-program measurements show material aggregate gain; do not mechanically annotate the
  roughly 1,700 action callbacks without demand evidence.
- **Result:** reject and remove. ProcessHit alone is not portable across the two resident working
  sets: resident-256 short pairs initially improve 2.09% and 1.31%, but resident-512 reverse pairs
  are +0.16% and -0.83%. Expanding the same storage request through all 32 central fetches makes a
  clean resident-256 reverse pair 5.08% slower, showing register displacement in larger owners.
  Generated code size is not the limiting ownership boundary; do not annotate the action corpus.

## Owner audit — action collision callbacks

- **Question:** the current profile assigns roughly 13% of the complete contract to
  `Fighter_procMap`'s action-owned collision callback, but that aggregate does not identify a
  deletable representation boundary. Attribute the existing callback bodies by exact function
  pointer under the disposable subsystem profiler before selecting the next structural cut.
- **Boundary:** profiling counters only; no release code, state, allocation, callback order, or
  gameplay output changes. Remove the attribution after recording its call/cycle distribution.
- **Decision rule:** choose a direct owner only if a small callback family carries material cost
  and shares canonical state/products. A diffuse distribution means an action-callback rewrite is
  another generic scheduler project and should not be pursued.
- **Result:** remove the attribution. The current 16,384-frame resident-512 profile assigns 13.03%
  to all collision callbacks, but no action owns a useful boundary: `AttackAir` is 1.27%,
  `DamageFly` 0.87%, `Wait` 0.76%, `Dash` 0.74%, `JumpAerial` 0.70%, `Landing` 0.69%, and `Jump`
  0.69%; all others are smaller. This reproduces the earlier callback census and confirms that
  actions converge in the shared air/ground `mpColl` owners. Do not specialize action wrappers.

## Rejected representation cut — dense canonical JObj matrices

- **Final owner and canonical state:** every native `HSD_JObj` keeps one matrix, owned by the
  existing `HSD_Mtx` object pool. `HSD_JObj::mtx` becomes a pointer to that matrix on native only;
  PPC and Wasm retain the upstream inline field and layout.
- **Consumers and displaced state:** preallocate the matrix pool once before stage/fighter JObj
  construction so canonical matrices occupy a dense 48-byte stream instead of being embedded in
  184-byte graph nodes. The JObj itself shrinks to topology, SRT, flags, animation links, and one
  matrix pointer. Existing setup, collision, dynamics, hit/hurt, contact, camera, and viewer code
  continues to read and write `jobj->mtx` directly.
- **Deletion boundary:** remove every native inline main matrix. Do not add a pose matrix mirror,
  lookup table, fallback layout, lazy product, runtime allocation, changed matrix arithmetic, or
  compiler control. `JObjInit`/`JObjRelease` allocate/free the sole matrix through the existing
  source pool; the preconstruction reserve covers the configured pose capacity plus the established
  transient headroom.
- **Why this is not the rejected JObj reorder:** the prior 176/192-byte layouts kept matrices
  interleaved with graph nodes and measured at most cache-noise-scale effects. This cut separates
  the two demand streams: pose publication scans smaller JObjs, while matrix consumers traverse a
  dense matrix allocation. It must repay its pointer load and any memory growth at both resident
  sizes or be rejected.
- **Acceptance:** unchanged exact digests, allocation lock, copy/save/restore, and focused native
  gates, followed by alternating comparisons against the frozen current aggregate and committed
  checkpoint at resident 256 and 512. Profile the retained form before claiming that cycle movement
  is a gain. The campaign checkpoint still requires at least +5% at both sizes over `02cfe013`.
- **Result:** reject and remove. The dense form preserves both short digests but costs `40,447.1`
  cycles/frame at resident 256 and `39,379.9` at resident 512, roughly 9–10% slower than the frozen
  current aggregate. A source-owner ablation that removes the reserve while keeping pointer-backed
  matrices falls further to `47,679.5` cycles/frame at resident 256. The reserve therefore recovers
  real locality, but the representation still loses the profitable adjacency between each JObj's
  SRT/topology state and its demanded matrix plus a dependent pointer fetch on every matrix use.
  Restore the inline matrix and generator without carrying an alternate layout. Future canonical
  geometry work must delete product/owner work, not merely separate the same products in memory.
- **2026-08-04 salvage correction:** the pointer-backed implementation survived the later tiled-
  transform cleanup because its hunks correctly predated that packet, but the cleanup mistook
  independent ownership for positive disposition. Restore the inline matrix, remove its pool
  allocation/free and native-layout exception, and compare the repaired aggregate directly. This
  is the completed rejection above, not a new representation experiment.

## Active structural packet — packed resident Match arenas

- **Final owner and canonical state:** `MslCoreBatch::match_arenas` remains the singular contiguous
  native storage mapping, and each `MslCoreMatch::memory` arena remains the only owner of its
  source graph. The complete supported construction census is below 1 MiB, versus the current
  3 MiB fixed stride.
- **Consumers and displaced state:** bind every batch slot at a 1 MiB stride and advise the one
  Linux mapping for transparent huge pages. This deletes 2 MiB of unreachable virtual capacity
  per slot and lets each 2 MiB page cover two adjacent Matches instead of straddling sparse 3 MiB
  reservations. Mac, Wasm, and PPC keep portable ordinary-page behavior.
- **Deletion boundary:** no second arena, copied hot state, gameplay allocation, state reorder,
  changed pointer identity, compiler control, scheduler seam, or output change. Mapping and
  allocation counts remain identical; exhaustion stays a hard construction failure. Measure peak
  RSS alongside throughput and reject the advice if physical-memory growth is disproportionate.
- **Acceptance:** exact short digests and supported construction/native copy/save/restore/allocation
  gates, followed by controlled resident-256/512 comparisons against a byte-frozen pre-packet
  aggregate. The campaign checkpoint still requires at least +5% over `02cfe013` at both sizes.
- **Result:** reject and restore the 3 MiB bound. Both packed forms preserve the short digest. A
  1 MiB ordinary-page stride is 0.65% slower at resident 256 (`36,883.5 -> 37,122.8`
  cycles/frame) with unchanged roughly 448 MiB process RSS. Transparent huge-page advice is
  decisively worse: `43,029.4` cycles/frame (-14.3% throughput) and 564 MiB RSS (+116 MiB).
  Translation was not the missing owner, and physically populating sparse arena capacity evicts
  useful state. Remove the advice and capacity change; do not pursue OS page policy as the 150k
  path.

## Active owner cut — exact dynamics angle admission

- **Final owner and canonical state:** the native dynamics angle helpers remain the sole owners of
  the source-ordered squared lengths, dot product, cosine cutoff, and demanded deviation angle.
  Existing compiled cutoff bits in `DynamicsData` remain canonical.
- **Consumers and displaced work:** max-angle, convergence, and deviation predicates first compare
  the already-required binary32 dot and squared lengths against a deliberately widened IEEE error
  interval around the cutoff. A result outside that interval is mathematically on the same side as
  the source cosine and deletes both square roots, their product, division, and clamp. The narrow
  interval, non-finite/subnormal inputs, and every demanded deviation angle execute the existing
  exact path unchanged.
- **Deletion boundary:** no approximate output, changed cutoff, cached runtime result, state,
  allocation, action/character admission, compiler control, or reordered source product. The
  bound may only decide a Boolean when its margin exceeds the maximum normal binary32
  sqrt/product/divide rounding envelope; ambiguity always falls through to exact arithmetic.
- **Acceptance:** count exact fallbacks only in a disposable screen, then remove the counters.
  Require unchanged short and full-suite output plus controlled gains at resident 256 and 512
  against `reports/triage/replay-bench-pre-packed-arena`. Retain only if both sizes repay the
  extra comparison and the total aggregate advances the campaign checkpoint.
- **Result:** reject and remove after the first exact resident-256 screen. The widened proof band
  preserves digest `4124834367a202ec`, but costs `36,883.5 -> 37,337.6` cycles/frame (-1.22%
  throughput). Computing the double-precision squared comparison on every call costs more than the
  skipped scalar hardware roots. Restore the direct exact cosine helpers and do not add a more
  elaborate admission around already-cheap hardware square roots.

## Active owner cut — frame-local camera clipping

- **Final owner and canonical state:** `Camera_8002958C` remains the singular gameplay-camera
  bounds owner. Live `CmSubject` state, `stage_info`, and `CameraTransformState` remain canonical.
- **Consumers and displaced work:** resolve the stage ground limit and four immutable-within-call
  camera bounds once, then use one inline exact clamp for the base and four extent points of every
  admitted subject. This deletes five repeated `Camera_80029124` calls, ground queries, stage
  getter families, and duplicated clamp blocks per subject while retaining both source subject
  passes and their mutation order.
- **Deletion boundary:** frame-local scalars only; no persistent cache, subject copy, changed
  admission, camera approximation, stage/action allowlist, allocation, compiler control, or
  output change. PPC/Wasm retain the upstream call shape.
- **Acceptance:** exact short digests and camera-focused/full validation, then alternating
  resident-256/512 comparisons against the frozen pre-packet aggregate. Retain only if both sizes
  improve and the aggregate materially advances the +5% checkpoint.
- **Result:** reject and remove. The exact native form shrinks `Camera_8002958C` from 1,618 to
  1,264 bytes and removes 29 generated calls, but this demanded scalar work is not the whole-frame
  bottleneck. A 65,536-frame resident-256 reversal is neutral-negative (`36,596.2 -> 36,563.0`
  cycles/frame when the control follows), and resident 512 loses `35,974.0 -> 36,224.6` in the
  adjacent 32,768-frame screen. Restore the upstream owner; do not treat source call count or text
  deletion as throughput evidence.

## Checkpoint-closing operation — paired exact dynamics lengths

- **Final owner and canonical state:** the three retained native dynamics angle predicates remain
  the sole consumers of their two source-ordered vector lengths; their existing cosine/cutoff
  results and `DynamicsData` remain canonical.
- **Displaced work and boundary:** construct the two squared lengths with the existing scalar
  operation order, evaluate both nonnegative binary32 values with one two-lane hardware square
  root, then continue through the unchanged multiply, cosine, clamp, and cutoff paths. This deletes
  one scalar square-root instruction per angle call without adding state, allocation, a second
  result, approximation, compiler control, or non-x86 behavior.
- **Why this bounded revisit:** the prior exact implementation improved resident 512 by
  0.35–1.62% and was neutral at resident 256. The current aggregate is independently above +5% at
  resident 256 but immediately below it at resident 512, so this is a finite checkpoint-closing
  test, not a renewed scalar search. Compare directly against frozen pre-change binary
  `reports/triage/replay-bench-pre-dynamics-pair-current` at both sizes; remove it if either size
  has a controlled loss.
- **Result:** reject and remove. Both short digests remain exact, but the first 65,536-frame
  resident-256 adjacency worsens `36,079.9 -> 37,336.2` cycles/frame (-3.36% throughput). The prior
  mixed/neutral 256 result does not survive the current aggregate, so no resident-512 fishing is
  justified. Restore the two scalar square roots and retain no SIMD helper or include.

## 2026-08-04 reconvene — scalar search exhausted

- The current exact dirty aggregate remains below the checkpoint: a 131,072-frame ABBA comparison
  against committed `02cfe013` centers at about +4.28% throughput for resident 256 and +4.34% for
  resident 512. It is retained unstaged, but does not authorize a commit.
- A fresh 16,384-frame resident-512 production-path profile assigns 26.93% of the contract to
  `Fighter_8006A360`, 13.97% to `Fighter_procMap`, 6.23% to `Fighter_8006D9AC`, and 4.11% to the
  camera. Inside animation, pose publication is 14.57% and action animation callbacks are 6.93%.
- The recent scalar/layout screens are sub-point, mixed, or negative. Stop searching those leaves.
  Reaching 150k from the current roughly 121--127k candidate requires a bounded canonical cut that
  removes work across fighter animation and its map/geometry consumers; a pose-only scheduler seam,
  scalar product sidecar, generic operation scheduler, or another source-layout tweak is already
  disproven by retained history.

## Active operation packet — batched exact blend arccosine

- **Final owner and canonical state:** the existing native skeleton blend in `lb_00B0` remains the
  sole owner of each live JObj quaternion. Its already-contiguous temporary cosine/angle rows are
  transient operation inputs, not persistent gameplay state.
- **Consumers:** the same quaternion slerp loop consumes the angles immediately before its retained
  wide sine/cosine pass. No other animation, scheduler, pose, or geometry owner changes.
- **Displaced work and deletion boundary:** collect the already-admitted finite interior cosines,
  evaluate the exact `acosf` square-root, reduction, `atanf` polynomial, and final subtraction in
  SIMD lanes, then delete the corresponding scalar `acosf` calls. Preserve every binary32
  multiply, divide, fused polynomial operation, lookup value, comparison, result order, and scalar
  exceptional fallback. Add no state, table, allocation, approximation, dispatch flag, compiler
  setting, or scheduler seam.
- **Cost model and acceptance:** the timed resident-512 census assigned 153,817 `acosf` calls to
  this already-batched owner. Unlike the rejected cross-Match publisher, inputs and outputs are
  contiguous and the downstream trig pass already pays the same batch boundary. Require both short
  digests, focused blend equivalence, full exact validation, and controlled gains at resident 256
  and 512 against both the frozen pre-packet aggregate and committed `02cfe013`.
- **Result:** reject and remove. The exact implementation processed useful widths (most batches
  contained 12--32 interior lanes, totaling 153,817 calls in the resident-512 window), so lane
  occupancy was not the failure. Direct cycle attribution measured the complete SIMD arccosine at
  only 1.83M cycles over 32,768 match-frames, about 56 cycles/frame and 0.16% of the whole
  contract. The first uninstrumented screen was correspondingly mixed (`37,250.5 -> 38,068.2`
  cycles/frame at resident 256 and `35,306.0 -> 35,191.8` at resident 512). The scalar hardware
  square root and short fused `atanf` are already cheap; six indexed gathers and vector setup only
  trade a sub-point leaf. Remove the kernel, exported lookup, call-site packing, and every counter.
  This corrects the count-only census: blend arccosine is frequent but not a material cycle owner.

## Active operation reassessment — exact wide general matrix concat

- **Final owner and canonical state:** `PSMTXConcat` remains the singular general affine product
  owner and publishes only its existing 3x4 output. No matrix representation or caller changes.
- **Displaced work:** the previously exact AVX-512 form loads both complete inputs before any
  publication, evaluates all twelve output lanes with the source multiply/two-FMA order, and uses
  the same final `fmaf(1, translation, value)` only on columns 3/7/11. This deletes the scalar
  row/column loops and alias temporary/copy without changing any arithmetic result.
- **Why reassess:** the earlier form repeatedly improved resident 256 by 2.15--2.43%, had a clean
  +1.52% resident-512 reversal, and agrees with an older +1.27% exact SSE result; it was removed
  after frequency-correlated 512 runs and a later narrower AVX2 form failed. The current retained
  math/pose chain is materially different, and a controlled long ABBA can resolve the wide form
  directly rather than treating the failed AVX2 replacement as its result.
- **Boundary and acceptance:** native AVX-512 only; portable native, PPC, and Wasm retain the same
  scalar owner. Add no compiler setting, target attribute, state, allocation, approximation, or
  consumer specialization. Require exact digests and a controlled gain at both resident sizes
  against the frozen current aggregate; remove it if either size loses in the long alternating
  comparison.
- **Result:** retain. Both 32,768-frame digests remain exact, and the emitted owner is the intended
  twelve-lane load, three row broadcasts, multiply/two-FMA sequence, masked translation FMA, and
  one masked store. Two 65,536-frame reversals improve resident 256 from `36,646.6/36,694.6` to
  `36,279.8/36,289.2` cycles/frame (about +1.06% paired throughput) and resident 512 from
  `35,410.0/35,337.7` to `34,787.0/34,929.4` (about +1.48%). This deletes the hot scalar loop and
  alias-copy path with no new state or consumer seam; retain it for the accumulated checkpoint.
- **2026-08-04 source audit:** later rejected matrix-product refinements accidentally restored the
  scalar owner while leaving the accepted result recorded above. Reinstate the exact accepted
  twelve-lane implementation and re-run both digests plus controlled aggregate timing; do not
  treat the historical result alone as current checkpoint evidence.

## Structural preflight — resident Match hot/cold ownership

- **Candidate final owner:** `MslCoreMatch` remains the singular mutable gameplay owner, but large
  construction allocations that are immutable after sealing or never consumed in the supported
  headless runtime move to shared `MslCoreGameData` ownership or are deleted. Mutable reached
  records remain in the Match arena; no gameplay field is mirrored between owners.
- **Canonical state and consumers:** source GObj/Fighter/item/stage records, compact fighter pose,
  scheduler lists, and every replay-observed mutable byte remain authoritative. The preflight maps
  the 608,864-byte ordinary arena by actual allocation callsite and size, then traces the largest
  records to their runtime readers before selecting a cut.
- **Displaced state and deletion boundary:** a retained packet must remove a substantial replicated
  allocation and its initialization/copy/relocation footprint, not merely reserve fewer virtual
  bytes or reorder Fighter fields. Immutable state has one process-wide owner; dead state has no
  owner. Add no pointer compatibility layer, synchronized copy, per-frame lookup table, gameplay
  allocation, or unexplained memory growth.
- **Acceptance:** first inventory without changing production behavior, then admit only a boundary
  with a credible multi-point whole-runtime ceiling at both resident sizes. The implementation must
  preserve exact output, allocation sealing, arbitrary-index copy/save/restore, PPC/Wasm behavior,
  and the complete supported-domain gate. Remove all allocation diagnostics before timing.
- **Inventory result:** the ordinary arena is dominated by dormant preallocated object pools. They
  do not enter the timed frame and prior capacity cuts confirm that byte deletion alone is neutral.
  The distinct top-level `MslCoreMatch` layout does expose one live cut: its 256-entry asynchronous
  effect queue occupies 10,240 bytes immediately before the source state visited by every gameplay
  owner. A complete resident-512 workload reaches only seven simultaneous entries.

## Active state packet — supported headless effect queue

- **Final owner:** `MslCoreEffectState` remains the sole Match-owned asynchronous effect queue. It
  contains 32 fixed nodes, over four times the measured resident maximum, and retains the same
  intrusive free/list ownership and source priority-9 flush order.
- **Canonical state and consumers:** `efAsync_Spawn`, `efAsync_QueueFlush`, and
  `efAsync_QueueClear` consume the one queue directly. Camera-shake parameters and RNG-bearing
  effect dispatch remain exact; immediate effects still allocate and release through the same
  owner.
- **Displaced state and deletion boundary:** delete 224 unreachable queue nodes and 8,960 bytes
  from every top-level Match. Add no secondary queue, spill allocation, overflow fallback, runtime
  growth, or changed dispatch. Exhaustion remains a hard invariant failure.
- **Acceptance:** both exact benchmark digests and the complete supported-domain effect/action
  corpus must remain green. Retain only if the smaller top-level Match improves both resident sizes
  against the frozen aggregate; otherwise restore 256 nodes rather than keeping a memory-only cut.
- **Result:** reject and restore 256 nodes. The smaller owner preserves both short digests, but an
  adjacent frozen-control/candidate pair is effectively identical at resident 256
  (`36,683.1 -> 36,685.9` cycles/frame) and only +0.05% at resident 512
  (`35,116.8 -> 35,099.4`). This confirms that dormant top-level bytes are not the active resident
  working set. Remove the capacity change rather than retaining memory-only churn.

## Active structural packet — native stage-vertex hot/cold ownership

- **Final owner:** native `mpLib` keeps one Match-owned vertex allocation, but its three source
  products become three canonical dense streams: current world position, immutable/local position,
  and previous world position. The current-position stream is the sole hot endpoint owner for all
  floor, ceiling, wall, line, island, and stage consumers. PPC and Wasm retain the source AoS.
- **Canonical state and consumers:** vertex indices and `MapLine` topology remain unchanged. Stage
  transform/mutation owners write the corresponding current stream; speed/remap owners consume the
  previous stream; reset/transform owners consume the local stream. No endpoint or derived line
  product is copied into `CollLine`, and every existing vertex API resolves the same singular
  fields.
- **Displaced state/work:** replace the 24-byte `CollVtx` stride touched by every endpoint query
  with an 8-byte current-position stride. The local and previous streams occupy the remainder of
  the same allocation, so allocation count and total vertex bytes do not grow. This deletes cold
  local/previous cache lines from the 13% stage-collision owner and creates contiguous current
  coordinates for a later exact narrow-phase kernel without a gather cache.
- **Deletion boundary:** no source-shaped native `CollVtx` AoS, mirrored endpoint, line cache,
  setter synchronization, compatibility flag, gameplay allocation, approximation, stage/action
  allowlist, or fallback query is retained. Native save/restore relocates the three interior views
  into the one allocation. If the complete representation cannot preserve exact output and improve
  both resident sizes after its immediate consumers are converted, remove it as a unit rather than
  retaining setup-only churn.
- **Acceptance:** exact short digests, source/native/copy/save/restore/allocation and Wasm gates,
  then alternating comparisons against the frozen aggregate at resident 256 and 512. The campaign
  checkpoint remains at least +5% over committed `02cfe013` at both sizes.
- **2026-08-04 result:** reject and remove before consumer expansion. Splitting the singular native
  vertex allocation into dense current/local/previous streams preserved both short exact digests,
  but a controlled screen was only about +0.4% at resident 256 and -0.15% at resident 512. The
  intended in-place floor consumer does not supply a credible larger ceiling: the already-recorded
  live-range census found no ranges of eight lines, 62.8% of ranges contain at most three lines,
  and 44.2% of ranges require callback or remap handling; exact AABB gates, compact source-order
  plans, and direct indexed iteration have also already lost. The dense split therefore leaves
  setup-only representation churn without a complete both-size gain. Restore the source-shaped
  24-byte singular vertex record and do not revisit stage-vertex layout without a traversal that
  deletes a materially different measured owner.

## Active structural preflight — canonical pose-to-geometry deletion ceiling

- **Candidate final owner:** the registered native fighter-pose graph would remain the singular
  animation/topology owner, but one compact canonical product layout would replace admitted
  `HSD_JObj` SRT/matrix storage from source animation publication through ECB, hurt, hit, shield,
  attachment, dynamics, and camera consumption. Generic nonfighter JObjs and PPC/Wasm retain the
  source representation.
- **Canonical state and consumers:** authored pose clocks, mutable source callbacks, dynamics
  mutations, and exact operation order remain authoritative. A retained cut must give immediate
  gameplay consumers direct access to the one compact state; it may not mirror JObj products,
  retain a per-fighter sidecar, or hand every demand back through the generic matrix API.
- **Displaced work and deletion boundary:** delete the admitted JObj SRT/matrix fields' runtime
  ownership, dirty-tree traversal, scattered matrix publication, and repeated consumer lookup as
  one cut. Do not add a batch scheduler seam, fallback dispatch, action/character allowlist,
  approximate math, gameplay allocation, or compatibility bridge. Unsupported source mutation
  cannot keep both representations live; it must remain outside the admitted graph or be ported to
  the singular compact owner.
- **Required preflight:** the current runtime already compiles integer pose samples and builds a
  compact ECB ancestor closure. Attribute the remaining pose evaluator, dirty dependency,
  matrix/origin publication, and non-ECB geometry consumers separately on the timed resident-512
  contract. Compare those executed costs with the rejected compiled sidecar's 80.5M-cycle pose
  saving and downstream relocation. Proceed only with a bounded representation whose complete
  deletion ceiling can materially advance the 150k target; do not infer a double-digit gain from
  inclusive pose and stage-collision buckets.
- **Acceptance:** remove all counters/timers after the census. The eventual packet must preserve
  exact short and full-suite output, allocation/state contracts, and improve both resident sizes
  against the byte-frozen current aggregate. The campaign checkpoint remains at least +5% over
  committed `02cfe013` at both sizes.
- **2026-08-04 result:** close this representation before implementation. A timed resident-512
  census records 6,760,978 physical pose-node visits, 140,146 compact ECB publications, and
  839,526 non-ECB point/pair publications over 65,536 match-frames. With profiler serialization,
  the compact ECB seam attributes 33.7M cycles to closure scanning, 16.0M to wide trig, and 87.7M
  to demanded matrix publication; non-ECB matrix/product consumers attribute another 76.8M. The
  pose-node dependency/interpreter/RObj split confirms a large inclusive region, but its exact
  parts cannot be added to the production profile because per-node RDTSCP perturbs the loop.
  More importantly, the complete prior singular compiled-pose owner already removed this same
  source animation/geometry boundary and delivered less than 1% whole-runtime gain after its
  saved 80.5M animation cycles relocated into stage/contact consumers. Earlier direct dynamics,
  grounded-IK, secondary-pose, fractional-rate, and transition continuations likewise produced
  multi-fold isolated wins without a reliable cumulative scale gain. Replacing canonical JObj
  storage would therefore repeat a measured architecture failure, not expose a new double-digit
  ceiling. Remove every timer and profiler-target change. Any future pose proposal must change the
  downstream stage/contact algorithm or batch storage itself; it cannot be another scalar compact
  owner, sidecar, or source-to-compact handoff.

## Active structural packet — mutation-aware fixed fighter scheduling

- **Final owner:** the hosted native GObj scheduler remains the sole priority/order owner. At the
  canonical p-link-8 position it directly visits the already-canonical fighter GObj list and calls
  the fifteen fixed source fighter phases; those fighters no longer also own fifteen generic proc
  records. PPC and Wasm retain the upstream generic schedule.
- **Canonical state and consumers:** `HSD_GObj_Entities->fighters` remains the singular fighter
  order. Scheduler priority remains canonical for action/effect/item timing queries. One transient
  proc-list mutation bit, written by the existing insert/remove owners, says whether a fighter
  callback invalidated the already-known continuation of the current generic priority list.
- **Displaced work and deletion boundary:** delete 15 proc allocations per native fighter, their
  priority-list traversal, indirect owner calls, per-proc epoch/flag traffic, and the prior fixed
  attempt's unconditional rescan from the priority head. Dispatch priority once, call every fighter
  directly, and resume the saved later-p-link proc unless the mutation bit requires a live rescan.
  Add no copied fighter list, callback table, phase mask, compatibility path, allocation, or changed
  order. Current-GObj/pending mutation semantics remain published around every direct call.
- **Prior-attempt distinction:** the exact 2026-07-19 candidate lost every pair because it switched
  per fighter, factored the generic hot loop, and rescanned the live proc list from its head after
  every fighter phase. This revisit keeps the proven current generic loop intact and removes that
  rescan in the ordinary non-mutating case. If the complete form still loses either resident size,
  remove it and close fixed fighter scheduling rather than tuning another dispatch variant.
- **Acceptance:** both production digests first, then alternating frozen-aggregate comparisons at
  resident 256 and 512. A retained result must improve the whole runtime at both sizes and carry no
  native Match-state/allocation regression beyond deleting proc records; the campaign commit still
  requires at least +5% over `02cfe013` at both sizes.
- **2026-08-04 result:** reject and remove the complete revisit. It preserved both exact digests and
  passed the native scheduler/API/copy/allocation smoke after the fixed path was correctly limited
  to an all-fighter proc-free p-link-8 list. CPU-8 screens initially appeared 3--8% favorable, but
  the core was under severe frequency/contender drift and those results do not reproduce on the
  canonical CPU-0 V-Cache contract. Adjacent resident-256 CPU-0 comparisons against the frozen
  aggregate are `35,979.4 -> 36,091.5` and `35,979.4 -> 36,028.5` cycles/frame: 0.14--0.31%
  slower. Removing proc traversal is repaid by the p-link admission scan, fixed-phase dispatch,
  current-owner publication, and preserved mutation semantics even without the old unconditional
  rescan. All runtime changes are removed. Fixed fighter scheduling is closed; do not repeat it by
  changing dispatch shape again.

## Active structural preflight — batch-native pose/geometry occupancy

- **Candidate final owner:** `MslCoreBatch` would own one fixed fighter pose/geometry phase across
  selected Matches. Match pose clocks and authored mutations would remain singular state; admitted
  sampled SRT and demanded world products would move to one batch-native canonical layout, with
  the corresponding fighter JObj SRT/matrix products deleted from the admitted path rather than
  mirrored.
- **Consumers:** the bounded cut is fighter pose publication plus its immediate ECB/stage,
  hit/hurt/shield/contact, dynamics, attachment, and camera geometry consumers. Generic scene
  animation, items, arbitrary source callbacks, and the rest of gameplay are outside the cut.
- **Displaced code/state and deletion boundary:** a retained implementation must delete scalar
  direct-row publication, repeated fighter JObj matrix setup, and consumer-side reconstruction for
  admitted products. It may not add the rejected per-fighter sidecar, synchronized matrices, a
  fallback state, action/character allowlists, gameplay allocation, or a generic callback tape.
  Exceptional authored/fractional ownership must make one explicit source-shaped transition; it
  cannot keep both representations live.
- **Preflight question:** before another multi-thousand-line cut, measure per-batch direct-row keys
  and exact eight-lane occupancy at resident 256 and 512. Count both contiguous identical
  program/sample rows and looser same-shape rows that would require gathers. The prior scalar
  compiled owner proved correctness but lost its saved animation cycles in handoffs and scattered
  consumers; proceed only if current resident batches expose enough real lane density to amortize
  the already-measured 7--9% scheduler seam and fund deletion across the complete owner.
- **Acceptance:** this census is diagnostic only and must be removed after measurement. A
  production packet remains acceptable only with exact output/full validation and a controlled
  whole-runtime gain at both resident sizes; an occupancy result is not itself retained progress.
- **2026-08-04 result:** reject the contiguous program/sample kernel before production work. The
  resident-256 census observes 9,709,792 direct rows over 1,036 batch steps: exact-key eight-lane
  occupancy is 13.8%, only 0.4% of rows belong to groups of at least eight, and the largest group
  is 12. Resident 512 observes 13,147,946 rows over 972 steps: occupancy is 14.9%, 1.0% of rows
  belong to groups of at least eight, and the largest group is 18. Grouping only by publication
  shape reaches 97.4%/98.1% occupancy, but requires scattered value/state gathers and consumer
  handoffs—the rejected compiled-owner shape—while still paying the 7--9% scheduler seam. All
  census code is removed. Do not implement a contiguous batch pose kernel or infer lane density
  from repeated benchmark cases; a future batch-native proposal must first eliminate the scatter
  by changing canonical storage across a wider owner.

## Active structural packet — state-local Match execution order

- **Final owner:** `MslCoreBatch` remains the sole owner of independent-Match execution order. Each
  `MslCoreMatch`, its source scheduler, mutable state, RNG, and output slot remain canonical and
  execute one complete frame without interruption.
- **Consumers and displaced work:** the batch step may visit independent Matches in a stable
  stage/action-family order derived from their already-canonical current fighter state. This aims
  to remove repeated instruction, branch, immutable pose-program, and stage-geometry working-set
  turnover without introducing the rejected cross-Match scheduler seam. Output APIs continue to
  address Matches by public batch index.
- **Deletion boundary:** use the existing reset-index scratch after reset; add no mutable gameplay
  state, allocation, cache, callback grouping, phase split, approximation, compiler control, or
  benchmark-only admission. A Match still runs its complete source scheduler while resident. The
  order must cover every selected initialized Match exactly once and remain deterministic.
- **Acceptance:** exact short digests first, then a bounded both-size comparison against the
  byte-frozen aggregate. Retain only if locality savings clearly repay ordering work at both sizes;
  otherwise remove the experiment completely and do not turn it into a scheduling framework.
- **Result:** reject and remove. Stable stage/action-family ordering preserves exact digests but
  loses at both sizes: resident 256 changes `36,324.1 -> 36,652.7` cycles/frame (-0.90%
  throughput), and resident 512 changes `34,788.1 -> 35,166.8` (-1.08%). Sequential public-index
  order provides better Match-arena/TLB locality than whole-Match code/data grouping repays. Remove
  the bucket computation and scratch order completely; this also rules out dynamic whole-Match
  sorting as the missing batch-native gain.

## Active output-locality packet — reverse publication visitation

- **Final owner:** public batch index remains the only observation/terminal addressing owner;
  every output row and Match retains its existing canonical state and index.
- **Displaced work:** keep the source scheduler's winning forward Match-arena traversal, but visit
  independent Matches in reverse while writing the caller's large observation history and terminal
  rows. Publication then finishes from low-index Match state immediately before the next forward
  step, instead of leaving the next step's first Matches displaced by high-index state and output
  stores.
- **Boundary:** only batch visitation order changes. Output byte addresses, row format, validation,
  masks, scalar writers, state, allocation, compiler profile, and per-Match operation order remain
  unchanged. Add no cache, streaming-store path, API, or benchmark admission.
- **Acceptance:** exact short digests and a bounded frozen-candidate comparison at both sizes.
  Remove immediately if reverse output stores cost more than recovered Match locality.
- **Result:** reject and remove. Digests remain exact, but reverse publication loses against the
  frozen aggregate: resident 256 changes `36,468.7 -> 36,697.3` cycles/frame (-0.62%
  throughput), and resident 512 changes `34,860.6 -> 35,131.6` (-0.77%). Backward writes cost more
  than any next-step Match warming. Restore both forward loops; traversal reordering is closed.
- **Bounded refinement:** test only the small terminal pass in reverse after the winning forward
  observation writer. This preserves sequential 980-byte observation stores while leaving the
  low-index Match terminal fields most recently read before the next step. One line changes; reject
  on the first both-size loss and do not add explicit prefetching.
- **Refinement result:** reject without a resident-512 screen. The exact resident-256 adjacency
  loses `36,295.7 -> 36,339.3` cycles/frame. Restore forward terminal visitation. Output/traversal
  locality is fully closed; no prefetch or streaming-store follow-up is justified by these results.

## Aggregate cleanup — provisional output-context deletion

- **Control:** the byte-frozen exact aggregate at SHA-256
  `13a93b1753dea7a841bbafca35e66ad8c6e4a4ba1e66631f06da0b467a341cd3`, measured
  +3.52%/+4.43% against committed `02cfe013` at resident 256/512.
- **Question:** the direct observation/terminal context packet was retained despite timing that
  changed sign. Restore only its two owner binds, source stock reads, and bound item traversal;
  retain the separately measured zeroed-output, wire-inline, popcount, and canonicalization work.
  This is a coherent ablation of an uncertain retained packet, not a new compatibility path.
- **Decision boundary:** exact short digests first, then alternating both-size timing against the
  frozen aggregate. Keep the simpler source owner if it is faster; otherwise restore the complete
  direct packet. No state, allocation, output, compiler, or API contract may change.
- **Result:** retain the direct packet. After correcting the ablation's terminal binding, both
  digests are exact. The source-owner ablation costs `36,993.8` cycles/frame between frozen
  resident-256 controls at `36,468.2` and `36,268.2` (about 2.0% slower by symmetric center), and
  costs `35,262.3` between resident-512 controls at `34,853.9` and `34,869.2` (about 1.1% slower).
  Restore the direct item traversal, stock reads, and deleted output binds completely.

## Active structural packet — exact collision-driver hot path

- **Final owner:** native `mpColl` remains the sole fighter stage-collision owner. The existing
  `CollData`, canonical fighter JObj matrices, and Match-owned `mpLib` line graph remain the only
  mutable geometry and contact state.
- **Consumers:** ordinary MotionState collision callbacks continue to enter the same air/ground
  drivers and publish floor, wall, ceiling, ledge, ECB, environment-flag, and fighter-position
  results before later contact and camera phases.
- **Displaced work:** first measure the complete ECB, air-driver, and ground-driver costs and the
  incidence of their response-only paths. Reshape the two dominant drivers so the measured common
  path no longer carries cold squeeze, connected-surface, ledge, or floor-recovery machinery in
  its live code/data flow. Preserve every admitted query and response in source order.
- **Deletion boundary:** no second `CollData`, cached ECB/contact, stage/action admission list,
  external line index, callback bridge, persistent state, allocation, approximation, compiler
  control, or cross-Match scheduler seam. A retained result must remove executed control/spill/
  publication work from the complete source owner; merely moving it behind another wrapper or
  changing code layout without a controlled gain is not sufficient.
- **Acceptance:** exact resident-256/512 digests, focused stage-collision validation including
  hard/soft floors, ledges, walls/ceilings, and moving platforms, then alternating production
  binaries against the frozen current candidate. The campaign checkpoint still requires at least
  +5% over committed `02cfe013` at both sizes.
- **Incidence result:** over 32,768 timed resident-512 frames, 69,858 JObj ECB publications consume
  80.1M net cycles. Their bound closures average 21.31 nodes, 20.30 dirty nodes, and 18.57 direct
  Euler nodes; those direct nodes span 8.14 dependency depths per call, so cross-node SIMD exposes
  only 2.28 independent nodes per depth. The 47,315 air-driver calls consume 25.3M cycles and the
  36,662 ground-driver calls consume 47.1M. Ground never repeats and takes its current-floor
  response 36,469 times (99.47%); air repeats 2,187 times and finds only 960 floor candidates.
  Neither driver records a horizontal squeeze or ceiling response in the timed workload.
- **Design consequence:** reject a hot/cold driver split before production editing. The cold
  response blocks are not executed cost, while a depth-batched ECB matrix kernel would average
  only 28.5% occupancy at eight lanes and would repeat the already-rejected gather/scatter shape.
  Continue inside the same owner by deleting demanded projection products, not by rearranging cold
  source or building another matrix sidecar.

### Exact current-line normal product

- **Final owner:** each native `CollLine` owns the normalized current-line direction derived from
  its canonical endpoint delta. `CollVtx::pos` remains the sole endpoint state; the product is
  valid only when both exact binary32 delta bits match.
- **Consumers and deletion:** the four direct line-projection helpers and `mpLineGetNormal` consume
  that product, deleting repeated PPC reciprocal-estimate/Newton normalization when fighters or
  queries revisit an unchanged line. Translation-only moving platforms naturally reuse it;
  rotation or deformation invalidates it by delta mismatch. Remapped query-local geometry keeps
  its source normalization.
- **Boundary:** native only; PPC retains the source-sized line. No epoch heuristic, setter hook,
  stage admission, second endpoint state, allocation, approximation, or external lookup table.
  The line record grows from 16 to 32 bytes so the product remains in the already-loaded owner;
  retain only if exact gates and both resident sizes repay that explicit state cost.
- **Result:** reject and remove. Both production digests remain exact, but the first reversed
  32,768-frame screen loses at both sizes: resident 256 changes `36,344.4 -> 36,474.8`
  cycles/frame and resident 512 changes `34,892.0 -> 34,947.2`. Exact delta comparison and the
  larger mutable record cost more than the repeated normalization they remove. Do not add this
  product or broaden it to more collision-query sites.

## Rejected owner cut — axis-aligned collision-line extension

- **Final owner:** `mpLib_8004ED5C` remains the sole extended-endpoint owner. Source `CollVtx`,
  `MapLine` adjacency, and the four published endpoint scalars remain canonical.
- **Deletion boundary:** for an exactly horizontal or vertical line, the length is the absolute
  nonzero axis delta. Use that identity to delete the two squares, add, and hardware square root;
  retain the source divisions and additions so signed-zero and publication order stay unchanged.
  General lines keep the complete source expression. Add no metadata, cache, state, allocation,
  tolerance, or approximation.
- **Acceptance:** exact production digests first, then a short both-size screen against the frozen
  current candidate. Remove immediately if either size fails; do not broaden the experiment.
- **Result:** both 32,768- and 65,536-frame digests remain exact, but the longer reverse loses at
  both sizes: resident 256 changes `35,984.6 -> 36,054.4` cycles/frame and resident 512 changes
  `35,275.8 -> 35,278.0`. The axis classification and changed loop layout consume the removed
  square-root work. Remove the candidate completely and do not extend this leaf.

## Active structural packet — direct hosted map-line consumption

- **Final owner:** native `mpColl` remains the fighter collision-state owner and native `mpLib`
  remains the singular mutable stage-line owner. The packet may replace repeated public scalar
  queries between those two owners with one source-shaped direct consumer, but may not cache or
  duplicate line state.
- **Canonical state and consumers:** existing `CollData`, `CollLine`, `MapLine`, `CollVtx`, moving
  joint transforms, topology, flags, normals, and source callback order remain canonical. PPC and
  Wasm retain their upstream call graph.
- **Displaced work:** first count the native calls that repeatedly validate and re-resolve the same
  line, kind, endpoints, flags, and normal during one fighter map callback. A retained form must
  delete those API/TLS/pointer traversals from the complete dominant floor/ceiling/wall consumers;
  it may not merely add a broad phase or another line representation.
- **Deletion boundary:** no compiler-profile changes, action/character specialization, persistent
  state, allocation, approximation, scheduler seam, fallback dispatch, or dual collision path.
  Preserve exact PPC arithmetic and candidate order. Remove all census hooks before production
  timing.
- **Provenance correction:** historical `4383ba3e` measured context-access deletion and a temporary
  `mpcoll.c -O3` admission together; it does not isolate an 18% collision-source ceiling. Current
  evidence only proves a 12--13% complete stage-collision owner and repeated cross-unit scalar
  queries. Proceed from the call census and generated code, not the combined historical number.
- **Result:** reject this as the next structural owner. Over 32,768 timed resident-512 frames, the
  broad scans execute about 57,255 floor, 2,008 ceiling, 39,665 left-wall, and 40,119 right-wall
  calls. Their immediate scalar follow-ups total only 89,410 validity, 43,777 kind, 33,767 flag,
  1,423 normal, and 57,411 line-projection calls—about eleven small API entries per frame. The
  generated floor projection already hoists the active line/vertex pointers across its loop; the
  only repeated TLS resolution is on optional output paths. A combined line-view API would remove
  call shells, not the broad scans, intersections, normalization, or ECB products, and cannot
  materially move the 150k target. Remove the census and do not build another scalar map wrapper.

## Rejected structural packet — tiled batch-native fighter animation

- **Final owner:** the batch step owns a small fixed tile of independent Matches while source
  scheduler priority 1 owns `Fighter_8006A360`; within each Match, fighter and callback order stay
  unchanged.
- **Canonical state:** existing Match-owned fighter pose nodes, clocks, JObj SRT/dirty state, and
  source scheduler state. The packet may change their physical hot layout only if the old layout is
  deleted in the same cut; it may not add a synchronized pose or geometry cache.
- **Consumers:** priority-1 fighter animation and its immediate canonical pose publisher. Later
  action, map, dynamics, hit/contact, and observation owners continue only after the complete
  priority-1 tile has published the same state in the same per-Match order.
- **Displaced work:** first measure the exact cost of pausing a two/four-Match tile around priority
  1. The retained implementation must repay that seam by evaluating direct compiled pose rows as
  one batch-native operation, removing the corresponding scalar interpreter/publication work
  rather than wrapping it. Generic/fractional/decoder cases stay on their singular existing owner.
- **Deletion boundary:** no whole-batch phase grouping, compatibility path, fallback state,
  gameplay allocation, compiler control, or approximate math. A tile finishes before advancing to
  the next tile, bounding cache displacement. Do not accept a seam-only result or claim future
  vectorization as a gain; retain only a completed direct owner with exact output and a material
  net whole-runtime improvement.
- **2026-08-04 seam experiment:** implement only the exact small-tile scheduling boundary first and
  compare it to the byte-frozen retained candidate. This is cost attribution, not a retainable
  endpoint. If the seam tax is too large for the 14.34% pose owner to repay, optimize or reshape the
  boundary from the measured causes before building a large kernel.
- **Result:** a two-Match tile preserves both exact digests but costs 7.01% at resident 256
  (`36,402.9 -> 38,955.2` cycles/frame) and 8.86% at resident 512
  (`34,880.3 -> 37,970.7`). The tile is already the smallest useful cross-Match group; its two
  extra context/scheduler residencies dominate before a kernel exists. Even a zero-cost replacement
  of the complete 14.34% pose bucket leaves only 5--7 points of impossible best-case headroom, so
  a real direct kernel cannot justify this boundary. Remove the tiled scheduler completely. Any
  batch-native revisit must absorb a substantially wider canonical owner without adding another
  scheduler suspension; do not build the pose-only kernel behind this seam.

## Rejected structural packet — canonical fighter matrix validity

- **Final owner:** native registered fighter JObjs remain the sole SRT and world-matrix owners;
  their compact pose nodes may own exact parent/version validity metadata only if it replaces the
  source dirty-propagation work for the admitted tree.
- **Canonical state and consumers:** authored/live JObj SRT, exact JObj matrices, parent topology,
  and every existing ECB, hit/hurt, dynamics, attachment, and action consumer remain unchanged.
  Matrix demand must observe the same source-ordered arithmetic and bits.
- **Displaced work:** the current dense pose table republishes rows even when all authored bits are
  unchanged, then forces exact matrix reconstruction downstream. A prior unrestricted equality
  test found 25.0% repeated rows but changed output because a clean matrix can encode an older
  ancestor/dynamics product. First split repeated dominant rotation rows by current dirty/parent
  validity. Then replace that ambiguous Boolean contract with one canonical parent-validity owner,
  so an unchanged clean row is reusable only when its complete parent product is still current.
- **Deletion boundary:** the retained form must delete redundant SRT stores, dirty propagation, and
  the corresponding matrix/trig rebuild as one operation. It may not add a second matrix/SRT,
  setter-maintained compatibility state, action/character admission, fallback dispatch, gameplay
  allocation, approximate comparison, or compiler control. Generic non-fighter JObjs retain the
  source dirty owner. Version wrap must be deterministic and exact rather than assumed impossible.
- **First measurement:** on the exact retained candidate, count dominant `0x00E` direct rows that
  are bit-identical before publication and partition them by current/parent dirtiness. Remove the
  census before production timing. Proceed only if clean repeated rows expose enough downstream
  matrix work to repay a complete validity representation.
- **Result:** the timed resident-512 census records 1,229,111 dominant `0x00E` rows and 204,301
  bit-identical repeats. Every repeated row is already matrix-dirty; zero has a clean parent, a
  clean complete ancestry, or a clean root. The repeated local rotation is therefore not the
  matrix rebuild owner: an actual ancestor change already demands the downstream product. A
  generation representation cannot delete that work and would only add validity metadata around
  an existing correct dirty fact. Remove all counters/profile build hooks. This rejects the
  canonical-validity packet by measured deletion ceiling, not because an incomplete implementation
  was slow.

## Contract and current control

- Branch: `perf/decomp-throughput`; do not switch branches or create a worktree.
- Current checkpoint: the second 150k-campaign checkpoint based on `ee32fbc1`; its complete
  experiment history and qualifying evidence are in
  `agent_docs/performance/JOURNAL_2026-08-03_CHECKPOINT_2.md`.
- Release benchmark SHA-256:
  `d5de6ab2f3b66415f1cf15b129ea8d773098e2b5693015044feced5e3c6665e0`.
- Resident-256 candidate median: 36,908.0 cycles/frame and 116,288 FPS, digest
  `bdff41cf74a54850`.
- Resident-512 candidate median: 35,267.5 cycles/frame and 121,697 FPS, digest
  `ee9d93c545aa3ef9`.
- Final target: at least 150,000 controlled median FPS at both resident sizes, with exact output
  and no gameplay allocation or unexplained memory growth.
- A later checkpoint requires at least +5% controlled throughput over this checkpoint at both
  sizes, using alternating parent/candidate cycle ratios and supporting wall FPS.

## Active checkpoint-closing packet — complementary exact owner cuts

- **Reopened exact affine owner:** reassess the already-completed AVX-512 `PSMTXConcat` deletion
  as part of the aggregate checkpoint rather than requiring this operation to win each resident
  size in isolation. The current aggregate is about 1.5% short at resident 256 and 0.6% short at
  resident 512. The prior complete-alias form preserved both digests, reduced the owner from 1,279
  to 151 bytes, improved both resident-256 directions by 2.15--2.43%, and had positive historical
  and clean resident-512 evidence; it was removed only after a frequency-correlated 512 sequence
  changed sign. Canonical matrices, operation/FMA order, alias behavior, callers, non-x86 paths,
  state, and allocation remain unchanged. Use the existing native ISA with no target attribute or
  build-setting change. Retain only if the complete candidate clears +5% against `02cfe013` at
  both sizes under the campaign's controlled alternating contract.
- **Result:** exact short and 65,536-frame digests remain unchanged, but the operation no longer
  wins in the current candidate layout. At resident 256 the 32,768-frame forward pair loses
  `36,350.4 -> 36,520.1` cycles/frame and the 65,536-frame reverse pair loses
  `36,084.2 <- 36,181.7`; resident 512 likewise loses `34,834.8 -> 34,938.9` and
  `34,644.2 <- 34,666.0`. Remove the AVX-512 owner completely. The older favorable result was
  real for its binary but is not transferable evidence for this checkpoint.

- **Current aggregate:** against committed checkpoint `02cfe013`, the restored candidate's
  131,072-frame alternating centers are 36,085.1 versus 37,353.9 cycles/frame at resident 256
  (+3.52% throughput) and 34,352.5 versus 35,875.7 at resident 512 (+4.43%). The remaining
  checkpoint gaps are therefore about 1.5% and 0.6%; no individual experiment or older workload
  is a substitute for the final aggregate comparison.
- **Dynamics owner:** the three initialization-compiled `acosf` cutoffs remain the sole exact
  decision products. Construction proves their authored thresholds nonnegative; the max-angle and
  deviation helpers therefore return false for the source zero-angle case, while the convergence
  helper is called only behind the existing positive-threshold gate and returns true. Remove the
  otherwise redundant threshold argument and comparison from all three hot calls. Canonical
  vectors, cutoff bits, strictness, nonzero arithmetic, and the one demanded deviation angle stay
  unchanged. A completed earlier form measured about +1.08% at resident 256 and neutral-positive
  at 512, but was removed under an individual-both-sizes rule; it is being reassessed only as part
  of this controlled aggregate checkpoint.
- **Context owner:** `msl_core_bind_match` remains the singular complete hosted-context publisher.
  Matches in one production batch share immutable `GameData`, but every per-Match bind republishes
  its files, source tables, pose programs, native DAT, and other immutable pointers. Guard those
  stores with the already-canonical active `GameData` identity while continuing to publish every
  Match-owned pointer unconditionally. Cross-GameData and bootstrap binding remain complete. A
  prior complete screen improved resident 512 by 1.22--2.30% and was mixed at 256; the dynamics
  cut supplies the complementary measured 256 gain.
- **Deletion boundary:** no new state, cache, compatibility path, allocation, compiler control, or
  changed output. Retain both only if exact digests and a final alternating comparison against the
  actual `02cfe013` binary clear +5% at both sizes; otherwise use individual ablations to keep only
  changes with demonstrated aggregate value.
- **Result:** both short production digests remain exact, but the complementary result does not
  survive the current binary. At resident 256, two 65,536-frame controls for the context-plus-
  dynamics form cost 35,989.2/36,100.0 cycles/frame versus candidates at 36,750.1/36,519.0. After
  removing the context guard, the dynamics-only form still centers about 0.7% slower (controls
  36,442.5/36,061.4; candidates 36,894.9/36,119.1). Remove both changes completely. Earlier
  isolated screens were code-layout-sensitive and are not bankable aggregate evidence; do not
  reassemble these scalar cuts for checkpoint arithmetic.

## Active owner cut — sealed native archive lookup

- **Final owner:** `MslNativeDatContext::archive_cache` remains the singular process-wide map from
  immutable source DAT span to translated native `HSD_Archive`.
- **Canonical state and consumers:** cache entries and returned archive graphs are unchanged.
  Initialization keeps insertion-order linear lookup while it is still discovering archives.
  `msl_native_dat_finish_initialization` seals and sorts the complete cache once; later match reset
  and motion/archive consumers use exact binary lookup by source address and file size.
- **Displaced work and deletion boundary:** remove gameplay/reset-time linear scans across as many
  as 4,096 archive entries. Callgrind attributes 1.17% self instructions to
  `msl_native_archive_parse` on the production workload even though every post-seal call is a cache
  hit. Add no table, per-Match state, allocation, fallback graph, or changed translation. A miss
  after sealing remains the existing fatal invariant.
- **Acceptance:** exact digests first, then controlled both-size timing against the frozen current
  candidate. Retain only if the complete workload benefits; startup-only improvement does not
  count.
- **First form rejected:** sorting the 3,423-entry sealed cache and calling libc `bsearch` preserves
  both digests but regresses the resident-256 65,536-frame symmetric center about 1.2%. The
  workload's hot entries make the current scan cheaper than generic comparison/call overhead.
  Remove the sort and comparator. Screen one final direct form: a 16 KiB process-wide table of
  one-based cache indices, built at the existing seal and probed inline by the exact source-span
  key. This is a bounded immutable index, not another translated graph or per-Match cache.
- **Final result rejected:** the direct sealed index also preserves both digests but loses the
  resident-256 65,536-frame symmetric center by about 1.0--1.1% (controls 35,940.0/35,957.7;
  candidates 36,340.0/36,288.1 cycles/frame). The added 16 KiB shared table and hash probe are
  worse than the current workload's hot-biased linear hits. Remove the table, hash, sort, and all
  lookup changes. The Callgrind self attribution is not a useful production deletion boundary.

## Rejected structural packet — canonical fighter quaternion mode

- **Final owner:** the registered compact fighter-pose tree owns each admitted JObj's Euler versus
  quaternion interpretation; generic unregistered JObjs retain `HSD_JObj::flags`.
- **Canonical state:** one tree generation plus one generation per compact node replaces the
  repeatedly cleared `JOBJ_USE_QUATERNION` bit for native registered fighter trees. The mode is not
  mirrored in the JObj flag. Generation storage must fit existing pose-node padding and retain the
  56-byte node.
- **Consumers:** source flag APIs, exact blend publication, fighter matrix construction, ECB
  transformation, and fighter action code that tests rotation representation.
- **Displaced work and deletion boundary:** ordinary no-blend animation advances the tree
  generation once instead of recursively touching every physical JObj. Every native registered-
  fighter read/write must use the singular generation owner; no summary bit, setter-maintained
  cache, compatibility flag, allocation, approximate math, or PPC/Wasm change is admitted.
- **First measurement:** time only the existing recursive clear inside `ftAnim_8006E9B4` and count
  its calls. Implement the representation only if eliminating that complete seam has enough
  measured ceiling to materially advance the current aggregate candidate.
- **Result:** disposable RDTSCP instrumentation attributed 112.9M cycles (3.88% of its perturbed
  resident-512 contract) to 138,559 recursive clears. A complete exact generation owner filled
  existing pose-node padding, redirected every native fighter quaternion-mode reader/writer, and
  retained source classical-scale dirty propagation. The first form that fell back for classical
  trees lost about 0.9% at resident 256 and 1.4% at resident 512. A compact preorder list then
  admitted classical trees, and the final form avoided descendant visits whenever clearing a clean
  classical root had already dirtied its subtree. It preserved all production digests but improved
  only about 0.21% at resident 256 and 0.63% at resident 512 in 65,536-frame symmetric screens.
  The instrumented ceiling did not survive production integration: source-required dirty
  propagation and generation-aware representation reads consume nearly all of it. Remove the
  generation state, mode APIs, classical list, profiler bucket, and every redirected consumer.
  Do not repeat flag-clear traversal or lazy-generation variants without a canonical matrix/SRT
  representation that deletes the classical-scale dirty contract itself.

## Rejected representation packet — pose-owned part animation admission

- **Final owner:** each retained native `MslFighterPoseJoint` owns the source part's mutable
  `flags_b0/flags_b5` animation-admission byte. PPC/Wasm retain `FighterBone` because their pose
  and source layouts differ.
- **Canonical state and consumers:** the byte occupies existing native pose-node padding. The
  compact pose traversal consumes it directly; transition, blend, dynamics, and frame-query code
  resolve the physical part JObj to that same node. Omitted presentation-only cold parts have no
  mutable admission state and retain the existing zero/discard behavior.
- **Displaced state:** remove the current native JObj-padding byte and every native runtime read or
  write through it. `FighterBone::flags_b0/flags_b5` remain layout-only on native and are neither
  read nor synchronized.
- **Deletion boundary:** one singular byte per retained node, no mirror, lookup table, setter hook,
  allocation, state growth, fallback owner, or gameplay branch in the hot pose traversal. Preserve
  all source transitions, save/restore, PPC/Wasm behavior, and exact output.
- **Evidence threshold:** the earlier disposable pose-node mirror isolated about +1.4% at
  resident-256. Retain the singular form only if controlled both-size timing beats the current
  JObj owner and the aggregate candidate crosses or materially approaches the +5% checkpoint.
- **Result:** the singular pose-node owner preserves both short production digests and keeps the
  node at 56 bytes, but a 131,072-frame resident-256 ABBA screen regresses from a symmetric
  35,781.0 to 36,948.9 cycles/frame (about 3.16% lower throughput). The prior +1.4% mirror was not
  evidence for this final representation: it kept transition, frame-query, blend, and dynamics
  consumers on the JObj byte. Singular node ownership adds a dependent `JObj -> aobj -> pose node`
  load to those consumers. Remove the node byte/accessors and retain the JObj-padding owner, which
  is the only direct singular location shared by both the compact traversal and source consumers.
- **2026-08-04 provenance correction:** retain the pose-node owner. Rebuilding the stated removal
  against the byte-frozen complete owner disproves the anomalous bracket above: two adjacent
  65,536-frame resident-256 directions cost `39,507.3/39,517.5` cycles/frame without the owner
  versus `39,033.8/39,053.6` with it, a repeatable 1.19% throughput gain with the exact digest.
  The restored release binary is byte-identical to frozen control
  `4af703ae783f3d740edf3d58c8567e106dcd97f7590910111e6c3fe90379f4c4`. The older JObj-padding
  form is not present in the current source and must not be treated as a control. Keep the
  complete singular pose-node representation; reject only the misleading historical timing.

## Active structural packet — grounded collision continuity

- **Final owner:** `mpColl_8004ACE4` remains the singular grounded collision/response owner;
  `CollData` and its published floor line remain the only mutable state.
- **Canonical state:** current/previous root position and ECB, current floor identity/normal/flags,
  source line topology, transformed stage vertices, and the existing collision epoch.
- **Consumers:** grounded MotionState collision callbacks through the existing `mpColl_8004B*`
  entries and `Fighter_procMap`; no action-specific replacement is admitted.
- **Candidate deletion:** when existing state and geometry can prove that the swept ECB remains on
  its current floor and cannot touch a wall, ceiling, edge, or different floor, publish the same
  grounded result without entering the general multi-surface search. The current-floor projection
  remains source arithmetic and must execute in the same order.
- **Deletion boundary:** the retained form must remove the corresponding wall/ceiling/global-floor
  traversal as one coherent common path. It may add no cached contact, alternate collision state,
  stage/action identifiers, tolerance, approximation, gameplay allocation, or fallback
  representation. Moving or remapped surfaces must remain source-owned.
- **First measurement:** attribute grounded-driver outcomes and internal cycle groups on the timed
  production workload. Proceed only if one exact admission covers enough of the 12.75% stage-map
  owner to provide a multi-point whole-runtime ceiling; remove the census before timing production.
- **2026-08-04 — rejected by measured ceiling:** the first 20,000 grounded calls complete exactly
  one driver iteration; 19,866 (99.33%) retain the current floor, with zero wall candidates, wall
  hits, ceiling hits, or different-floor publications and only 72 air fallbacks. That population
  is real, but the timed groups cap the useful deletion: wall checks consume 4.89M cycles, ceiling
  3.94M, current-floor/connected-wall work 10.38M, and the final different-floor search 6.92M.
  Even deleting every general subpass while retaining the required current-floor projection saves
  only about 15.7M cycles, roughly 1.1% of the instrumented whole contract. Position and ECB are
  both bit-identical on only 447 calls, so there is no simple state-reuse proof covering the large
  population; the collision epoch is current on only 309 calls because source ground owners
  publish it independently. Do not build a cached collision result or unsafe static-stage/action
  admission for a one-point ceiling. Remove all counters/timers and leave `mpcoll.c` byte-clean.
- **2026-08-04 — rejected refinement:** keep the source wall broad phase as the exact
  conservative admission owner. Ground collision now evaluates that admission at the driver seam:
  an empty side does not enter the corresponding large wall-query owner, and when both sides are
  empty and the current-floor projection leaves root X/Y bit-identical, the connected-wall queries
  and second identical ceiling query are provably redundant. Any possible wall, floor projection,
  moving/remapped response, edge, or changed position retains the complete source sequence. Final
  state remains `CollData`; there is no cached contact, stage/action key, alternate geometry,
  approximation, allocation, or changed publication order. Both 32,768-frame digests remain exact.
  A first adjacent 65,536-frame screen against the frozen pre-change candidate improves resident
  256 `36,187.2 -> 35,829.8` cycles/frame (+1.00%) and resident 512
  `35,523.5 -> 34,549.0` (+2.82% in reverse order). Treat those as a positive screen, not final
  attribution. The complete supported-domain gate disproved the admission: eight wall-hug rows
  changed `pos_x` by one or two ULPs. Restoring the connected-floor queries did not remove any of
  the failures, and restoring the repeated ceiling query did not remove them either; the
  caller-side broad-phase admission itself changes the correctness-sensitive wall path. Remove the
  caller admission and both query omissions completely. The rebuilt benchmark is again
  byte-identical to the frozen pre-packet candidate, SHA-256
  `13a93b1753dea7a841bbafca35e66ad8c6e4a4ba1e66631f06da0b467a341cd3`.

## Active structural packet — shared immutable stage geometry

- **Final owner:** `MslCoreGameData` owns one sealed collision line/vertex image for each supported
  stage whose geometry is immutable after construction. Final Destination, Battlefield, frozen
  Pokemon Stadium, and Dream Land are admitted; Fountain of Dreams and Yoshi's Story retain their
  complete Match-owned moving geometry.
- **Canonical state:** the shared `CollLine`/`CollVtx` arrays are the only runtime line topology,
  flags, and vertex coordinates for an admitted static stage. Each Match retains its mutable
  `CollJoint` list, callbacks, bounds, collision epoch, and all contact state.
- **Consumers:** existing `mpLib`/`mpColl` queries consume the shared arrays through the unchanged
  bound source pointers. No query wrapper, cache, spatial index, or alternate collision path is
  added.
- **Displaced state/work:** delete one full line and vertex copy, its relocation records, and its
  resident working set from every admitted Match. Construction may use a temporary private image
  while source stage setup runs, but it must compare bit-for-bit with the sealed GameData image,
  release that temporary image before the Match is sealed, and leave no production bridge.
- **Deletion boundary:** any post-construction line/vertex mutation on an admitted stage is an
  invariant failure. Moving stages stay entirely source-owned and Match-local. Preserve line
  order, topology, flags, vertex bits, mutable joints, save/restore, output, and allocation
  behavior. Retain only if the complete suite proves the immutable-stage boundary and controlled
  resident-256/512 timing beats the frozen current candidate.
- **2026-08-04 — rejected:** the complete representation was built for the four static stages.
  GameData preload captured one sealed image per stage; later Match construction used private
  temporary arrays, required a bit-for-bit final comparison, released them before sealing, and
  left the existing query path reading only the shared image. The 32,768-frame resident-512 digest
  remains exact, but the first adjacent comparison is neutral/slower (`35,043.0 -> 35,081.4`
  cycles/frame), and two longer 65,536-frame candidate samples lose against their adjacent control
  (`34,849.4 -> 34,969.4` and `34,849.4 -> 35,122.3`). The line/vertex image is already small;
  collision remains arithmetic/control-bound, so shared residency deletes bytes but not executed
  work. Remove the GameData owner, temporary construction path, mutation assertions, and all state
  changes. The rebuilt release benchmark is again byte-identical to the frozen candidate,
  `13a93b1753dea7a841bbafca35e66ad8c6e4a4ba1e66631f06da0b467a341cd3`.

- 2026-08-04 — `rejected structural packet`: size the singular Match-owned fighter-pose track arena
  to the configured fighter population instead of reserving the four-player maximum in every
  Match. `MslFighterPose::tracks` remains the sole mutable decoder-track owner and keeps the same
  element type, indices, relocation ownership, compaction, and consumers. Construction already
  sizes the adjacent joint arena to 256 entries per configured player; use that same capacity for
  tracks, while the four-player maximum remains 1,024. Displaced state is only unreachable arena
  capacity in one- and two-player Matches; there is no second state, runtime allocation, changed
  gameplay path, fallback, or new lookup. Retain only if the complete supported-domain gate proves
  the per-player bound and controlled resident-256/512 timing improves the current candidate. The
  short digest remains exact, but resident-256 screens change sign: the first 32,768-frame pair
  loses 0.51%, while the reversed 65,536-frame pair improves 0.68%. The arena is dormant on the
  measured direct-program path, so the smaller allocation neither removes executed work nor gives
  a stable cache effect. Restore the fixed track capacity rather than carrying a validation-wide
  bound change with no demonstrated throughput value.

- 2026-08-04 — `rejected structural packet`: move ordinary hosted fighter matrix-dependency
  propagation from animation-time traversal to the canonical matrix demand. Construction proves
  every compact-pose JObj uses the ordinary parent dependency (no user matrix, IK/effector,
  independent-SRT, RObj, or custom matrix method). `HSD_JObj::flags` and `mtx` remain the sole
  dirty and world-matrix state. Animation publishes only its node's source-authored dirty bit;
  a demanded compact matrix recursively publishes dirty ancestors in parent order, while the ECB
  closure carries each node's parent slot and propagates rebuilds through its existing preorder
  batch. The one gameplay consumer that observes dirtiness itself uses the same effective
  ancestor predicate. Displaced work is `HSD_JObjCheckDepend` on every physical pose node every
  frame; consumers, matrix arithmetic/order, SRT, state size, allocation, and non-native behavior
  remain unchanged. Add no dirty summary, cache, alternate matrix, fallback topology, or phase.
  Retain only if exact full-domain output and controlled both-size timing show demanded propagation
  deletes rather than relocates the traversal. The complete path preserves the short digest, but
  the first resident-256 screen rises from roughly 36.5k to 38.1k cycles/frame. Removing the
  generic setup hook and keeping demand propagation only in compact transform/ECB consumers still
  loses 2.25% in an adjacent comparison (`36,414.2 -> 37,235.5`). The animation-time predicate is
  cheap because it walks the already-hot preorder once; moving authority to scattered capsule,
  dynamics, and ECB demands repeatedly walks ancestors and reconstructs the same dependency.
  Remove the setup API, parent-slot encoding, consumer change, invariant expansion, and traversal
  deletion completely. This closes demand-propagated dirtiness without a canonical matrix layout
  that can answer ancestor generation in constant time.

- 2026-08-04 — `rejected structural packet`: make the existing canonical JObj matrices, rather than
  a pose/product sidecar, the cross-consumer result of fighter pose evaluation. At the current ECB
  demand seam, evaluate the complete registered fighter tree's dirty ordinary matrices in preorder
  with one exact wide-trig pass. Final SRT/matrix owners remain the JObjs, and later dynamics,
  stage-collision, hit/hurt/contact, attachment, and camera consumers continue reading those same
  matrices. Any exceptional JObj uses its source setup method in the same preorder; any later SRT
  mutation dirties and rebuilds normally. The displaced boundary is repeated scattered lazy matrix
  setup and trig in immediate downstream consumers, not animation sampling or source callbacks.
  Add no matrix table, cache, alternate geometry, approximation, phase scheduler, or gameplay
  allocation. First screen the complete-tree demand shape at the existing seam; if it is positive,
  move the singular publication earlier only when measurement proves additional consumers can
  reuse it without rebuilding. The exact short digest remains `1177a913e8074ce6`, but evaluating
  the complete tree at the ECB seam costs about 38,798 cycles/frame at resident 256, roughly 7%
  slower than the current candidate. Current profiling accounts for only about 57 demanded matrix
  builds/frame: roughly 43 already covered by the retained ECB wide-trig path and 15 scattered
  elsewhere. Eagerly publishing the whole tree creates substantially more products than consumers
  request; moving the same work earlier cannot recover the observed loss from eliminating at most
  those 15 later demands. Remove the complete-tree traversal and enlarged stack packet. This
  rejects eager complete-tree materialization, not a demand-driven canonical matrix owner.

- 2026-08-04 — `rejected structural packet`: remove repeated exact endpoint-extension math from
  stage collision queries. `CollVtx` remains the sole mutable endpoint owner and `MapLine` remains
  the topology owner. Each Match's loaded stage geometry owns a compact derived record containing
  only the exact extension quotients for lines whose owning joint never transforms its vertices;
  moving/remapped joints continue through the source calculation. `mpLineGetPrev/Next` remains in
  every query so line enable/hide and adjacency changes retain source authority. Immediate floor
  and ceiling consumers use the derived quotients only after the same adjacency decisions and in
  the same float-add order. The displaced boundary is their repeated length square root and two to
  four divisions per admitted static line; there is no second mutable geometry state, gameplay
  allocation, approximation, or topology cache. First measure total/static call ownership, then
  retain the implementation only if exact validation and controlled both-size timing show that the
  demanded work is actually deleted rather than relocated. The census is substantial: about 244
  calls/frame, 92.7% on non-transformed joints, with both endpoint extensions requested by 81.6%
  of calls. All implementations preserve the short production digest. But a separate exact-product
  array loses about 1.4% at resident 256, while embedding the four required quotients doubles the
  hot `CollLine` stride and improves its ABBA center only 0.42%. The smallest form—one embedded
  distance, retaining the source divisions and deleting only the square root—loses about 1.19%.
  Exact hardware square root/division are cheaper here than expanding the per-Match geometry
  stream or adding a second indexed stream. Remove all metadata, allocation, invalidation, and
  consumer changes. This closes cached line-extension products unless the line representation is
  itself replaced as part of a wider collision owner.

- 2026-08-04 — `rejected structural packet`: make the existing native64 `Fighter` allocation one
  canonical hot-first record without adding or copying state. The final owner remains `Fighter`;
  every field keeps its name, type, lifetime, address stability, save/restore ownership, and direct
  source consumer. Move the three color overlays, authored hit/hurt capsule storage, and the large
  CPU command-state block behind fighter/motion variables, and omit the source-offset-only collider
  padding on native64. This moves the damage/shield state, callbacks, command fields, and flags used
  across animation, input, map, contact, and hit phases roughly five KiB closer to the already-hot
  identity/position/input/collision prefix. The moved hit/hurt/CPU blocks remain contiguous arrays
  or structs and are still consumed directly; there is no pointer indirection, sidecar, mirror,
  getter conversion, allocation, fallback, or changed state size apart from dead padding. PPC and
  Wasm keep source order. Generated relocation metadata follows the one actual native layout.
  Consumers are all existing Fighter field accesses; displaced work is cache-line/page traffic from
  interposing large phase-specific blocks between the common prefix and common tail. The deletion
  boundary is complete at the type layout—no API integration layer exists. Retain only if exact
  outputs, relocation/save-restore, memory/allocation gates, and controlled resident-256/512 timing
  all improve; otherwise restore the source order as one packet. Both tested layouts preserve the
  production digest. Moving every proposed block regresses the resident-256 131,072-frame ABBA
  center about 2.4%. Keeping hit/hurt capsules beside collision state and moving only overlays,
  CPU command state, and dead padding still loses the 65,536-frame center about 0.4%. The source
  placement is not merely cold interposition: hit/hurt geometry and the nominal CPU block remain
  active through contact, command scripts, and follower input, while changing their offsets also
  perturbs the large action closure. Restore the complete source order and padding. This is the
  second completed layout shape on this architectural unknown, so do not keep permuting Fighter
  fields without a measured consumer-owned split that deletes state or indirection rather than
  moving it.

- 2026-08-04 — `rejected structural packet`: delete repeated immutable camera-bound and projection
  evaluation across the singular standard-camera frame. Final mutable owners remain each
  `CmSubject`, `CameraTransformState`, and the existing stage/camera globals; canonical output
  remains the same bounds, transform, and fighter visibility bits. `Camera_8002958C` currently
  evaluates the same stage limits and pure default-ground height for each of five subject points,
  then repeats those exact reads to apply the classification. Load the immutable frame values once
  and use one compact classify-and-clamp leaf for all five points. The camera depth calculation
  also evaluates an identical half-FOV tangent twice, and the later headless visibility pass
  evaluates the same FOV tangent once per fighter; compute each identical frame-local value once
  at its existing owner and pass the scalar product to its immediate consumers. Displaced work is
  only duplicate pure ground/stage reads, duplicate comparisons/clamps, and duplicate tangent
  evaluation. Subject order, classification boundaries, float operation order, smoothing, camera
  state, visibility publication, non-native source behavior, allocation, and output remain
  unchanged. Add no cache, persistent state, approximation, compiler control, or alternate camera.
  Retain only with exact digests and a controlled gain at both resident sizes against the frozen
  current candidate; otherwise remove the packet as a unit. The complete bounds/trig form preserves
  both short digests but loses the resident-256 ABBA center by about 0.25%. Removing the bounds
  consolidation and screening the duplicate-tangent deletion alone is worse: its 65,536-frame
  resident-256 center loses about 1.3%. The compiler/source layout already makes these pure calls
  cheaper than keeping the derived scalars live across the larger camera owners. Remove the helper,
  frame locals, and both trig handoffs completely. This independently confirms the earlier mixed
  camera-limit and frustum-coefficient results; close frame-local camera commoning rather than
  spending another resident-512 screen.

- 2026-08-04 — `retained correctness integration`: native64 stores the canonical hosted fighter-
  part animation-admission byte in existing HSD_JObj alignment padding, but wasm32 has no byte
  between `flags` and `u`. Restrict that representation to non-Wasm native builds. Wasm retains
  the existing source `FighterBone` flags and construction-time pose-node part identity; no dual
  owner exists within either target. Also emit the provisional native hardware `sqrtf` as an
  explicit scalar `sqrtss`/`fsqrt` instruction so the unoptimized native smoke binary cannot lower
  its own builtin to a recursive `sqrtf` call. `wasm-smoke` now passes with matching native/Wasm
  state digest `a8a4eceda2e87198`; these are build/correctness repairs, not additional speed claims.

- 2026-08-04 — `rejected diagnostic`: test a wider pose/geometry deletion instead of another interpreter
  leaf. Count direct Figa publications whose complete authored SRT row is bit-identical to the
  canonical JObj values already present. If this population is material, the final owner remains
  the JObj SRT and dirty flag; its immediate matrix/ECB/contact consumers may reuse the existing
  matrix only when neither the node nor its parent changed. The proposed deletion boundary is the
  redundant SRT stores, dirty propagation, and downstream matrix reconstruction for an unchanged
  row. Add no cache, state, allocation, approximation, or alternate pose. Remove the counters
  before any timing and proceed only if measured reuse can delete substantial demanded work.
  Of five million direct publications, 1,247,936 (25.0%) repeat the existing SRT bits, including
  622,065 of 3,236,069 rotation triplets and 152,684 of 553,364 rotation/translation rows. But
  making the dirty publication conditional immediately changes the resident-256 digest
  `4124834367a202ec -> 40acb356c8663479`: equal authored SRT does not prove the current matrix is
  still the authored product after dynamics and other matrix owners run. The source dirty event is
  restoration authority, not redundant cache invalidation. Remove the counters and conditional
  publication completely; the rebuilt source again matches frozen candidate SHA-256
  `13a93b1753dea7a841bbafca35e66ad8c6e4a4ba1e66631f06da0b467a341cd3`.

- 2026-08-04 — `rejected structural experiment`: batch the dominant direct rotation-row publication inside
  the existing compact pose traversal. Final mutable owners remain each `MslFighterPoseJoint`
  clock and its canonical JObj SRT/dirty state; immutable frame-major program values remain the
  sole sample owner. For consecutive ordinary direct `0x00E` table frames with no RObj or special
  dependency behavior, advance the existing node clocks in source order, publish the dirty bit at
  the same dependency seam, and gather/scatter eight independent three-float rows together before
  any general node or callback. This also deletes the generic dependency call for these nodes:
  their source publication unconditionally makes the same JObj dirty, and the admitted topology
  has no other dependency side effect. Every loop/decoder/fractional/general/filtered-nonrotation,
  RObj, PPC/Wasm, and non-wide path retains the singular source owner. Displaced work is repeated
  table admission, scalar row loads/stores, and generic dependency classification for the admitted
  run. Add no persistent state, allocation, sidecar pose, cache, approximate math, or fallback
  representation. Flush before any operation that can observe an SRT value. Retain only with exact
  digests, full gates, and a material both-size gain over the frozen candidate. The complete fast
  path preserves both digests. Eight-lane AVX-512 gather/scatter loses about 1.1% at resident 256;
  hardware scatter is more expensive than the three adjacent scalar stores. Replacing the wide
  flush with scalar copies recovers most of that loss but still regresses the resident-256 ABBA
  center about 0.45%. The duplicated clock/table admission and pending-row machinery cost more
  than deleting the generic dependency shell. Remove the batch, fast interpreter, intrinsics, and
  traversal changes completely. This confirms that scattered canonical JObj publication cannot
  be repaired by a gather/scatter wrapper; a future pose rewrite would have to change the final
  canonical hot layout, not add another scalar sidecar or pending row.

- 2026-08-04 — `rejected diagnostic`: test whether the batched quaternion owner repeats complete exact
  interpolation factors across resident Matches. Final gameplay state would remain the two input
  quaternions, weight, and published JObj quaternion; a hit keyed by the exact `cosom` and weight
  bits could reuse only the pure `sp`/`sq` result and delete one `acosf` plus three sine lanes.
  First count exact keys and direct-map conflicts with fixed diagnostic storage. Add no production
  cache or state unless the measured hit population and a bounded deterministic ownership model
  can delete substantial demanded math; remove all census machinery before timing. A 65,536-slot
  exact direct map hits only 158,576 of the first 500,000 requests (31.7%) and incurs 277,906
  replacements. That ceiling applies to only the blend owner's subset of scalar angles while
  adding a large mutable process cache to every lookup; it cannot provide the missing whole-frame
  scale and is not a clean canonical-state boundary. Remove the table, counters, and reporting
  without implementing a production cache.

- 2026-08-04 — `rejected structural experiment`: extend the retained exact fighter wall broad phase to
  ceiling and airborne-floor sweeps, and complete the boundary instead of rescanning line endpoints
  before every rejection. Final geometry owners remain the source `CollLine`/`CollVtx` records;
  each constructed `CollJoint` owns one conservative immutable AABB per static line kind plus a
  dynamic-kind mask. `CollData` and the source `mpCheck*` families remain the sole mutable contact
  and response owners. Wall, ceiling, and floor consumers test their complete previous/current ECB
  bounds against that metadata; any moving/remapped joint or matching dynamic-line kind enters the
  source path. Canonical contacts, line order, transformations, tolerances, normals, and responses
  do not change. Displaced work is the broad phase's per-query joint-line/endpoint traversal and
  every rejected narrow-phase traversal/intersection. There is no allocation, mutable cache,
  alternate topology, approximation, or fallback representation; construction metadata is rebuilt
  deterministically with the existing Match. Retain only with exact digests and controlled gains
  at both resident sizes. The endpoint-scan form is exact and improves the resident-256 center
  about 0.6--0.8%, but is mixed/neutral at resident 512; adding the identical floor boundary does
  not improve it. Completing the deletion with construction-owned per-kind `CollJoint` bounds is
  also exact, but coarse joint bounds admit more false positives and growing the hot joint record
  perturbs the Match layout: its resident-256 ABBA center regresses by about 3.2%. This is the
  second failure of the same line-index/bounds unknown, consistent with the earlier rejected Y-bin
  and flat-plan work. Remove floor/ceiling admissions, metadata, construction, and generalized API
  completely. The rebuilt source again matches frozen candidate SHA-256
  `13a93b1753dea7a841bbafca35e66ad8c6e4a4ba1e66631f06da0b467a341cd3`.

- 2026-08-04 — `rejected`: keep `atanf` as the sole exact reduction, polynomial, and result owner.
  Its middle-range index currently addresses six coefficients through offsets spread across a
  192-byte source-shaped table, even though those coefficients are consumed together for exactly
  one range. Replace that immutable layout with one compact record per range and a separate common
  polynomial vector. Preserve every constant bit pattern, comparison, division, fused operation,
  accumulation order, exceptional result, caller, state, allocation, and non-native behavior.
  This completely replaces the old table—no cache, alternate evaluator, approximation, or compiler
  setting. Retain only with exact digests and controlled gains at both resident sizes against the
  frozen pre-layout candidate.
  Both short production digests remain exact. A resident-256 ABBA center improves about 0.72%, but
  both resident-512 adjacencies lose (the symmetric center is roughly 3% slower under the same
  frequency drift). Compact records save immutable bytes but make the hot indexed code worse at the
  required scale. Restore the source-shaped table and singular evaluator completely.
- 2026-08-04 — `rejected`: retain the exact four cutoff keys in `atanf`, but select the lower or upper
  two by the already-loaded absolute float word's one-bit `< 1.0F` partition. Each middle-range
  call then performs two threshold comparisons rather than four. Preserve all boundary predicates,
  reduction and polynomial operations, exceptional behavior, constants, state, allocation, and
  non-native source; add no table or approximation. Retain only with exact digests and controlled
  gains at both resident sizes against the frozen four-comparison candidate.
  The short digest remains exact, but the resident-256 ABBA center is effectively neutral/slower:
  36,747 cycles/frame for four comparisons versus 36,784 for the partitioned form. The added branch
  replaces two cheap independent integer comparisons and cannot pass the required first size.
  Restore the four-comparison count and do not spend a resident-512 screen.
- 2026-08-04 — `rejected`: keep `Fighter_ProcessHit_8006D1EC` as the sole hit-resolution and per-frame
  cleanup owner. On an ordinary hosted frame, the shield, delayed damage, contact, grab, sound,
  hitlag-entry, and attacker-shield-knockback discriminators can all prove their branches inert,
  yet the source-shaped owner still walks each decision before reaching the same canonical reset,
  afterimage, and hurt-publication tail. Admit that complete no-event predicate directly to the
  existing reset label. Preserve every reset store, shield regeneration/drain, delayed effect,
  callback, hitlag transition, afterimage, hurt publication, ordering, output, allocation, and
  non-hosted source. Add no flag, cache, duplicated tail, or approximate condition. Retain only
  with exact digests and controlled gains at both resident sizes against the frozen pre-entry
  candidate.
  Both short digests remain exact, but the complete resident-256 ABBA center regresses about 2.3%:
  36,542 cycles/frame for the source decision tree versus 37,386 for the explicit no-event entry.
  Loading every scattered discriminator up front costs more than the well-predicted source branches
  and no-op leaves. Remove the predicate and label completely; do not spend a resident-512 screen or
  add a synchronized aggregate flag merely to make this fast path cheap.
- 2026-08-04 — `not executed; superseded`: keep the source decision tree, but move each of its three
  ordinary no-op leaf predicates to the immediate call site: capture handling requires
  `x2224_b3`, pending damage sound requires `x1908 != -1 || x190C != NULL`, and afterimage work
  requires `x2100 != -1`. Each callee currently reloads that same Fighter and predicate before
  returning. Displace only those common call/return and repeated `GET_FIGHTER` shells; preserve
  predicate timing, callbacks, state, reset/publication order, output, allocation, and non-hosted
  behavior. Add no aggregate admission or state. Retain only with exact digests and controlled
  gains at both sizes; if it fails, close ProcessHit shell work.
  Do not implement this leaf. The current aggregate remains roughly +4.0% / +2.8% at resident
  256/512 while the final target needs a structural reduction; further ProcessHit shell tuning is
  local-minimum work with at most a fraction of its 2.4% inclusive owner. Close this owner and move
  to the shared resident working-set boundary.
- 2026-08-04 — `rejected diagnostic`: determine whether whole-Match execution locality has enough headroom
  for a batch-owned stable permutation. Temporarily traverse the initialized batch once per
  supported configured lead character while still executing each selected Match's complete source
  scheduler contiguously and storing every output by original lane index. This changes no Match
  state, within-Match order, phase seam, allocation, or API result. The diagnostic deliberately
  pays repeated batch scans; proceed to a real init/reset-owned permutation only if exact digests
  and both sizes show a gain materially larger than that scan cost. Otherwise remove the grouping
  loop completely.
  Both short digests remain exact, but the resident-256 ABBA center loses about 1.24%: original
  lane order centers at 36,368 cycles/frame and lead-character grouping at 36,826. Repeated scans
  are not concealing useful locality, so do not build a maintained permutation or spend a
  resident-512 screen. Restore the one-pass lane-order scheduler completely.

- 2026-08-03 — `rejected`: keep `msl_core_bind_match` as the sole hosted context publisher and the
  existing thread-local GameData pointer as the canonical identity of its immutable half. Batch
  stepping and output repeatedly bind different Matches that share one GameData, yet each bind
  republishes the same files, source data, fighter tables, pose programs, and native-DAT pointers.
  Guard only those GameData-derived stores with the existing identity comparison; continue to
  publish every Match-owned context field on every bind. Preserve cross-GameData rebinding,
  bootstrap binding, ordering, state, allocation, PPC/Wasm behavior, and all outputs. Add no cache
  or new context field, and retain only if exact digests and both resident sizes improve against
  the frozen pre-change candidate.
  The factored publisher reduces the ordinary same-GameData bind from 633 to 434 bytes. Two
  resident-512 reversals favor it by 1.22% and 2.30%, while resident 256 changes sign under the
  current frequency drift. After the output path stops rebinding, this guard applies only to the
  once-per-frame gameplay bind and its added branch/cold helper has no stable both-size gain. The
  complete context refactor is removed; keep the original unconditional singular publisher.
- 2026-08-03 — `retained candidate, below checkpoint`: direct observation and terminal projection already own their
  `MslCoreMatch`. Their only context-dependent consumers are the Match's canonical GObj item list
  and source player-stock slots, yet both currently publish the complete gameplay TLS context.
  Pass the Match explicitly to the internal item writer and read those two canonical owners
  directly, deleting both output-only full binds and the stock getter shells. Preserve item/player
  order, bytes, validation, gameplay source calls, output APIs, state, allocation, and PPC/Wasm
  behavior. This completes the output-side context deletion that the earlier stock-only test did
  not: add no partial context, copied stock state, alternate item list, or combined-output API.
  Both production-prefix digests remain exact. Alternating measurements are frequency-correlated
  and change sign, but the complete form deletes two full context publications, two stock getter
  families, and the bound-item precondition with no compensating state or branch. Keep it
  provisional for the aggregate checkpoint rather than assigning an unsupported point estimate.
- 2026-08-03 — `rejected`: keep `stage_ground` as the sole Ground owner and bind the two FoD platform
  indices or one Yoshi's Randall index once after supported-stage construction. Observation and
  viewer projection currently rescan all 64 Ground slots every frame for those unchanged actors.
  Store two byte indices in existing `MslCoreMatch` tail padding and consume the same live Ground
  fields directly, displacing only those repeated identity scans. Preserve actor state, stage
  process order, output bytes, save/restore, allocation, and every non-FoD/Yoshi path. Add no
  pointer cache or duplicate stage value; require unchanged Match size, exact digests, and gains at
  both resident sizes against the frozen direct-output candidate.
  The two indices consume only existing Match tail padding and preserve both digests, but two
  resident-512 reversals regress consistently: `37,918.0 -> 39,691.0` and
  `39,717.8 -> 41,640.5` cycles/frame (4.7--4.8%). Remove the indices, construction scan, and both
  direct consumers completely; the sparse stage-output scan is layout-cheap at batch scale.

## Next owner selection

- The final bounded resident-512 profile is 45,307.3 diagnostic cycles/frame. Pose animation owns
  14.23%, stage collision 12.75%, fighter dynamics 7.25%, input/action 6.71%, and camera 4.32%.
- The retained stage-collision fast path removes only provably duplicate negative wall queries;
  action-specific collision specialization, ECB vector reduction, and a shared wall broad phase
  are closed by this checkpoint's measurements.
- Select the next bounded owner only after searching `agent_docs/performance/`. Record its final
  owner, canonical state, consumers, displaced work/state, and complete deletion boundary here
  before implementation. Compiler-setting experiments remain excluded.
- 2026-08-04 — `diagnostic`: the current bounded profile leaves 6.71% in source-owned fighter
  input/IASA callbacks, but the retained history has no callback-identity split for that bucket.
  Temporarily attribute the existing callback invocation by function pointer over one 32,768-frame
  resident-512 window. This changes no production owner or state. Remove the timer immediately;
  only pursue a callback if it exposes repeated generic work with a complete local deletion
  boundary large enough to survive whole-runtime measurement.
  The current exact 32,768-frame resident-512 split puts the whole input-callback bucket at 4.40%.
  It is fragmented: Wait owns 0.71%, Jump 0.58%, Dash 0.52%, and every other callback is below the
  report's 0.5% cutoff. Remove the timer. Do not specialize an action callback; a useful input
  packet must delete repeated common transition checks across several source owners without adding
  another dispatcher or cached state.
- 2026-08-04 — `rejected`: keep `MslFighterPoseJoint` as the singular mutable animation owner and each
  JObj `aobj` pointer as its direct identity. The hot pose loop scans 56-byte nodes, but the
  dedicated 32-bit identity magic and final alignment padding consume eight bytes while the
  animation flags leave bits 0--25 unused. Tag those unused bits and move the existing packed
  program metadata into the vacated leading word, shrinking each node to 48 bytes. Pointer offsets,
  every clock/track/program field, canonical JObj SRT, source order, allocation count, relocation
  pointer layout, non-native behavior, and outputs remain unchanged. The old magic member and
  trailing padding are the complete deletion boundary; add no side table, cache, alternate node,
  or compatibility path. Retain only with exact digests and controlled gains at both sizes.
  The first form tags low animation-flag bits and is exact, but loses both resident-256 directions:
  36,513.5 -> 37,167.2 and 36,270.8 -> 36,536.8 cycles/frame. The masked identity test replaces a
  single magic equality inside frequently queried pose ownership. Test one final form using an
  equality tag in otherwise-unused native64 JObj padding, preserving the original flags offset and
  one-check identity shape while retaining the 48-byte node. If it also loses, restore the 56-byte
  layout and close node compaction.
  The equality-tag form improves the resident-256 symmetric center about 1.71%, but resident 512
  does not survive control reversal: the first symmetric center is only +0.22%, and a repeated
  forward pair loses 37,124.9 -> 38,303.9 cycles/frame (-3.08%). Restore the original magic,
  metadata position, padding, and 56-byte layout completely. A smaller stride is not intrinsically
  better for this interleaved batch working set; do not retry node compaction without deleting
  materially more per-node work at the same boundary.
- 2026-08-04 — `rejected`: keep the retained compiled cosine cutoffs as the sole exact decision owner.
  The three source-authored dynamics thresholds are immutable and nonnegative, but each hot helper
  still receives the float threshold solely to reproduce the zero-length `lbVector_Angle` result.
  Assert that existing data invariant while compiling the cutoffs, then make the greater helpers
  return false and the convergence less-helper return true for the same zero angle. Displace only
  the redundant float argument and exceptional comparison; preserve vector products, cutoffs,
  strictness, nonzero decisions, solver order, state, allocation, non-native source, and output.
  Add no approximation or new metadata. Retain only if exact digests and both resident sizes improve
  against the frozen current candidate.
  Both short digests remain exact and the resident-256 symmetric center improves about 1.08%.
  Resident 512 is neutral: its center changes only 35,894.1 -> 35,869.1 cycles/frame (+0.07%), with
  the forward direction slightly negative. Restore the threshold arguments and exact exceptional
  comparisons; the ABI deletion does not demonstrate a gain at both required sizes.
- 2026-08-04 — `rejected`: keep the compact pose preorder, JObj pointers, and JObj state as the sole
  canonical owners. Unlike the rejected next-Match header prefetch, the hot pose scan already knows
  the exact upcoming pointer chase, and the successful part-flag ownership cut proved that its JObj
  access is latency-sensitive at resident 512. Prefetch only the fourth upcoming JObj while walking
  the existing span, displacing no operation or state. Preserve node/JObj order, every read/write,
  allocation, non-native output, and exact behavior; add no cache, staging buffer, or persistent
  metadata. Retain only with exact digests and gains at both sizes against the frozen current binary.
  The exact resident-256 ABBA screen changes sign with order; its symmetric center regresses about
  0.57% (36,980.2 -> 37,193.6 cycles/frame). Remove the prefetch completely without spending a
  resident-512 screen. Hardware allocation/prefetch plus the established JObj layout already serves
  this stream better than an extra lookahead load and branch.
- 2026-08-04 — `rejected after completed boundary`: make one attached direct Figa program clock the canonical clock for an
  ordinary fighter-pose tree. The timed resident-512 census records 1,618,766 ordinary main-pose
  Figa node evaluations with identical program, pre-clock state, and program order; the existing
  frame-major table already makes their immutable values one contiguous row. Retain one existing
  `MslFighterPoseJoint` clock as the group owner and make the other attached nodes refer to it,
  deleting their repeated clock advance, loop/end classification, integer/table admission, and
  per-node clock writes. Consumers remain the same source-ordered JObj SRT publisher and AObj
  counters. A single-joint request, non-unit rate, different program/filter, or decoder-owned node
  splits back to independent existing clocks before mutation; no second clock, sample table,
  fallback representation, allocation, approximation, or changed output is admitted. The pointer
  index occupies existing tail padding and does not grow per-Match state. Retain only with exact
  digests, full validation, a completed owner/split boundary, and gains at both resident sizes.
  The completed implementation preserves the production digest and shares 1,865,696 of 3,367,078
  animated nodes (55.4%) in the timed resident-512 census. It advances one clock, handles
  independent-request/rate splits, and reuses the owner's frame-row base directly for followers.
  A resident-256 ABBA screen nevertheless puts the symmetric control/candidate centers at
  37,585.7/37,815.3 cycles/frame, making the candidate about 0.61% slower. Per-joint SRT
  classification/publication remains demanded and dominates the removed scalar clock work; the
  owner metadata and split boundary add net cost even at broad coverage. Remove the clock index,
  grouping/split machinery, direct-row bridge, and census completely. Do not retry per-tree clock
  ownership without also deleting a materially larger per-joint publication boundary.
- 2026-08-04 — `rejected`: keep each compact fighter JObj's existing flags, parent, and matrix as the
  canonical dependency state. `animate_node` still enters the fully generic `HSD_JObjCheckDepend`
  for every physical pose node; historical inner timing assigns 121.7M cycles to 6.49M calls. Prove
  at registration that the complete native compact roster has no user-defined matrix, effector,
  independent-SRT, or RObj dependency cases. Under that invariant the exact owner is only: when a
  clean node has a dirty parent, mark the node dirty. Perform that predicate directly in the compact
  span, displacing the generic call and its unreachable classification only. Preserve parent-first
  order, every dirty bit and matrix, non-native source behavior, output, state, and allocation. Add
  no cache, summary bit, alternate pose, or fallback. Reject on an invariant failure, digest change,
  or loss at either resident size. The stronger invariant holds across construction and both short
  production digests. The source-ordered direct predicate has a +0.97% resident-256 ABBA center but
  a -0.47% resident-512 center with directions split by the package-frequency ramp. Moving the
  predicate after animation so ordinary publications can satisfy it performs worse: both
  resident-512 directions lose, and the symmetric center regresses about 2.9%. Restore the generic
  dependency owner and the narrower retained registration assertion completely. This is consistent
  with the prior failed dirty guard/full-predicate inline: compact loop layout outweighs the small
  generic classification deletion.
- 2026-08-04 — `rejected refinement`: preserve the out-of-line dependency call boundary that keeps
  `animate_node` compact, but place one native compact-pose leaf beside the generic JObj owner. The
  registration invariant above makes its only operation the exact clean-child/dirty-parent test;
  the leaf deletes generic user-matrix/effector/RObj classification without expanding the pose loop.
  Canonical state, ordering, consumers, non-native code, allocation, and output remain unchanged.
  This is the final bounded dependency-owner shape; retain only with exact digests and controlled
  gains at both sizes, otherwise restore the generic source call and assertion. Both short digests
  remain exact, but both sides of the resident-256 ABBA screen lose; the symmetric center regresses
  from 38,820.3 to 40,350.4 cycles/frame (about 3.8%). The separate leaf perturbs the already-tuned
  JObj/pose call layout more than its predictable generic branches cost. Remove the symbol,
  declaration, stronger invariant, and call-site change completely; the dependency owner is closed.
- 2026-08-04 — `retained candidate, below checkpoint`: make each admitted physical JObj the canonical native owner of its
  source part's mutable animation-admission bits (`FighterBone.flags_b0` and `flags_b5`). The
  compact animation loop already scans contiguous `MslFighterPoseJoint` nodes, but currently uses
  `source_part_index` to fetch those two bits from a separate 24-byte-stride `FighterBone` table;
  the resident-512 diagnostic attributes 273,582 last-level misses (21.85% of the run) to
  `msl_fighter_pose_animate_parts`. Store the two bits in existing native64 JObj alignment padding,
  have the compact loop consume them directly, and redirect every native `ftanim`/`ftdynamics`
  read and write to that singular owner. The PPC/Wasm source fields remain unchanged; on native
  they become layout-only and are neither read nor synchronized. Omitted presentation-only cold
  parts share a sentinel and have no mutable admission state; writes to them remain intentionally
  discarded. Remove the part-table and source-index lookup from the hot pose loop. Preserve source operation order, all flag
  transitions, save/restore, state size, allocation, and output. The deletion boundary is complete
  only when no native runtime use of either source bit remains. Retain only with exact digests and
  controlled gains at both resident sizes against the frozen pre-change candidate.
  A complete packed-per-fighter owner preserved both digests but enlarged the source-parts pool
  object by 140 bytes; its resident-256 center lost roughly 5.5%. An exact temporary mirror into
  pose-node padding isolated the hot lookup itself at about +1.4% resident-256, proving the miss
  lead while also proving that dual state was unnecessary and could not be retained. The final
  singular JObj owner occupies bytes that were already native64 padding, leaves every allocation
  and struct offset unchanged, and preserves both 32k and 131k production digests (including a
  resident-512 reset). Its resident-256 ABBA center is 36,508.0 -> 36,322.7 cycles/frame (+0.51%;
  directions -0.45%/+1.47% under the host frequency ramp). Resident-512 centers are 37,449.7 ->
  35,518.7 (+5.44%; directions +2.75%/+8.13%). Keep this small exact 256 gain and large batch-scale
  locality gain provisional; it does not independently qualify the aggregate checkpoint.
- 2026-08-04 — `active singular-owner refinement`: make `MslFighterPoseJoint` own those two bits
  instead. The compact pose node is already the admitted physical animation owner and has one byte
  of existing alignment space beside its track count; its hot traversal can therefore consume the
  bits without following `node->joint`. Native part APIs resolve the same node through the JObj's
  existing canonical `aobj` binding, while the shared cold sentinel remains outside the admitted
  pose and reads as clear/discards writes. Delete the provisional JObj-padding owner and every
  native source-bit use in the same cut. This adds no byte, mirror, table, allocation, or fallback.
  Require exact digests and controlled gains at both resident sizes against the frozen aggregate;
  otherwise restore the singular JObj form.
  `retained below checkpoint`: both 32,768-frame digests remain exact. A resident-256 frozen-binary
  reversal centers at 36,849.5 -> 36,516.9 cycles/frame (+0.91%); resident 512 centers at
  35,124.6 -> 35,049.2 (+0.21%). Keep the simpler singular node owner, but do not claim a campaign
  checkpoint from this short screen.
- 2026-08-04 — `rejected dependency refinement`: active native pose nodes always republished dirty
  state over the complete 366-case construction/preroll and both short exact digest screens, so a
  completed candidate skipped `HSD_JObjCheckDepend` only for that population. A frozen-binary
  resident-256 reversal against the pose-node owner centers at 36,461.9 -> 36,429.7 cycles/frame,
  only +0.09%. Restore the source dependency call; that token result does not justify a stronger
  runtime invariant, and no consumer-side propagation or layout variant follows from it.
- 2026-08-04 — `rejected packed-word refinement`: placing the part bits in unused low positions of
  the node's AObj flag word also stays exact, but a frozen resident-256 reversal loses about 0.26%
  against the separate byte already occupying alignment space. Restore the byte owner; preserving
  and rewriting the mixed-purpose word perturbs the interpreter more than one nearby byte load.
- 2026-08-04 — `rejected traversal refinement`: carrying direct begin/end node pointers removes the
  indexed loop's repeated bound-pose-base reload and shrinks the native span consumer by 74 bytes,
  but a frozen resident-256 reversal loses about 0.34% against the indexed byte-owner control.
  Restore the existing indexed traversal; the extra live pointer/register pressure costs more than
  its cheap local-exec load.
- 2026-08-04 — `active hosted debug deletion`: the hosted platform has no `DbLevel` writer and
  initializes it to zero, but fighter update/map and mpColl begin/end still reload it before
  unreachable retail diagnostics. Make zero canonical at the hosted db interface so the complete
  compiler closure deletes those reports and checks. PPC retains mutable retail state; no gameplay
  branch, output, allocation, or compiler setting changes. Retain only with exact digests and a
  controlled gain at both resident sizes.
  `rejected`: both short digests remain exact and the hot diagnostics disappear from the linked
  owners, but the resident-256 reversal loses about 0.98% against the frozen pose-node control.
  Restore the ordinary db symbol; broad source-layout displacement outweighs four predictable
  zero-level branches.
- 2026-08-04 — `rejected`: when the dynamics convergence limit accepts `natural_dir`, it assigns that
  vector exactly to `link_dir`; the immediately following max-deviation owner then recomputes
  `lbVector_Angle(natural_dir, link_dir)` only to test whether two identical vectors exceed the
  positive authored deviation limit. Carry that exact convergence result across the adjacent
  source statements and omit only the redundant self-angle/deviation block. The vectors,
  convergence comparison and rotation, non-converged deviation path, solver state/order,
  PPC/Wasm behavior, allocation, and output remain unchanged. Add no cache or approximation;
  reject on either digest mismatch or a loss at either resident size.
  Both short digests remain exact, but the added carried boolean/layout costs more than the
  self-angle calls it avoids: resident-256 reversals regress `35,820.6 -> 36,156.1` and
  `35,855.1 -> 36,135.9` cycles/frame (0.9% on both sides). Restore the adjacent source owners and
  do not spend a resident-512 screen on a failed required size.
- 2026-08-04 — `rejected`: `Camera_8002958C` tests five derived positions per active subject against
  the same stage camera limits. Each `Camera_80029124` call reloads the four immutable frame-local
  limits and repeats the pure `Ground_801C4368` publication. Load those exact values once for this
  bounds traversal and use one private native predicate for its five tests; leave boundary
  clamping, subject order, transforms, smoothing history, all other callers, PPC/Wasm behavior,
  state, allocation, and operation ordering unchanged. Add no cache or approximation. Retain only
  with exact digests and gains at both resident sizes. The narrower comparison-only form preserves
  the short and 131,072-frame digests and improves the resident-256 alternating center from
  36,854.6 to 36,209.9 cycles/frame (+1.78%). Resident-512 samples are host-disturbed and do not
  establish a gain: the first adjacent pair is only +0.64%, while a later pair reverses by 2.23%.
  The consolidated history records two earlier complete variants of the same snapshot boundary as
  neutral/slower at resident 512. Remove the helper and frame-local limits completely; do not retry
  this camera owner without a different product/deletion boundary.
- 2026-08-04 — `rejected after final screen`: keep `PSMTXConcat` as the singular exact affine-matrix
  owner and its 3x4 output as canonical state. A 2026-07-19 x86 vector implementation preserved
  the production digest and measured about +1.27% at resident 512, but was removed only because a
  future source-boundary fusion was expected to displace it. The retained SRT fusion does not
  displace general concat calls in collision, dynamics, articles, and JObj consumers. Re-evaluate
  the same operation stream using the existing native build ISA, with all six input rows loaded
  before any store so every supported alias case remains exact. Displace only scalar lane
  repetition and the alias temporary/copy; preserve multiply/FMA order, output bits, all callers,
  PPC/Wasm/arm64 behavior, state, and allocation. Add no compiler flag, target attribute, alternate
  matrix state, approximation, or consumer change. Retain only with exact digests and controlled
  gains at both resident sizes against the frozen current candidate.
  The complete-alias AVX-512 form reduces the owner from 1,279 to 151 bytes and evaluates all 12
  lanes in one multiply/two-FMA stream plus the three ordered translation FMAs. Both short digests
  remain exact. Resident-256 reversals improve `37,776.4 -> 36,980.2` (+2.15%) and
  `38,118.5 -> 37,213.1` (+2.43%) cycles/frame. Resident-512 is obscured by strong run-order
  frequency movement: the cleanest reverse improves `38,932.1 -> 38,350.7` (+1.52%), while a
  later forward pair changes sign (`37,823.9 -> 38,932.1`); an earlier control was independently
  disturbed at 42,970.0 and is discarded. Historical evidence for the same smaller SSE operation
  stream was +1.27% at resident 512. The final AVX2/SSE form was screened against the frozen
  pre-change candidate over 262,144 frames: its resident-256 symmetric center is about 0.33%
  slower, while resident 512 is about 0.85% faster with the two directions disagreeing
  (+2.15%/-0.37%). It therefore fails the required both-size rule. Restore the scalar source owner
  completely; retain none of the native general-concat path.
- 2026-08-04 — `rejected refinement`: the exact AVX-512 concat deletes the scalar owner but loads four
  64-byte permutation vectors and performs four full-width indexed permutes per call. Test the same
  singular operation stream as two rows in one AVX2 register plus one SSE row: each 128-bit lane
  can broadcast its row coefficients with immediate shuffles, so no index vectors or indexed
  permutes remain. Load all input rows before publication to retain every exact alias case, and
  preserve the multiply/two-FMA/translation-FMA boundaries lane-for-lane. This changes no state,
  consumer, allocation, compiler setting, or non-x86 path. Compare directly against the frozen
  AVX-512 candidate at both resident sizes, and retain only if exact digests and controlled timing
  show the narrower kernel is better. The narrower kernel is preferable to AVX-512 in isolated
  screens, but the final comparison above shows that the complete native general-concat rewrite
  still loses at resident 256. Remove both implementations together.
- 2026-08-04 — `rejected refinement`: if the AVX2/SSE concat shape survives its isolated screen, use
  that same loaded-row helper for `HSD_MtxSRTConcatTrig`'s final parent product. Unlike the rejected
  AVX-512 reuse, its three exact local rows are consumed directly as XMM inputs and parent rows
  need only one two-row insert; there are no ZMM packs or indexed permutes. The helper replaces two
  SSE row streams with one AVX2 stream and retains the third SSE row, preserving all nine local SRT
  values and every multiply/FMA/publication bit. Add no new API, state, fallback, or compiler
  control. The exact form shrinks `HSD_MtxSRTConcatTrig` by 34 bytes, but its resident-256
  reversals follow run order at roughly -2.4%/+2.9%. Resident 512 likewise changes from a 5.2%
  candidate win to a 0.6% loss in reverse. There is no stable gain from replacing two compact SSE
  row streams. Restore the established three-row product and keep the loaded-row helper only in
  general concat, where it deletes a much larger scalar/alias owner.
- 2026-08-04 — `rejected refinement`: reuse the new 12-lane concat kernel for the final parent
  product inside `HSD_MtxSRTConcatTrig`, leaving its nine exact local Euler values unchanged. Both
  short digests remain exact and the owner shrinks only 39 bytes (`0x236 -> 0x20f`). Two
  resident-256 reversals change entirely with run order: the candidate loses
  `37,505.3 -> 38,678.0`, then wins `37,997.8 -> 37,618.1`. Packing the three freshly constructed
  XMM rows into ZMM lanes deletes little executed work and adds wide permutes. Keep the established
  compact three-row SSE/FMA product; retain AVX-512 only in the general concat where it deletes
  the scalar 12-lane owner.
- 2026-08-04 — `open`: keep `acosf` as the sole source-exact inverse-cosine owner and its
  binary32 result as the only canonical product. Its positive radicand currently enters the PPC
  estimate table and three binary32 Newton refinements before the unchanged `atanf` consumer;
  ordinary native `sqrtf` call sites already compile to a correctly rounded hardware square root.
  Test whether `1.0F / sqrtf(radicand)` produces the identical reciprocal on the complete
  production-prefix corpus, displacing only the table lookup and Newton chain. Preserve radicand
  construction, exceptional branches, `atanf`, every output, state, allocation, and non-native
  behavior. Add no approximation, cache, alternate result, or compiler setting. Reject
  immediately on either short digest; retain only if the complete exact gates and both resident
  sizes improve against the frozen pre-change candidate.
  Both 32,768- and 131,072-frame production digests remain exact at both resident sizes. Native
  `acosf` shrinks from 201 to 108 bytes: the table call and all three Newton chains become one
  `sqrtss` and one `divss`. A follow-up directly divides `x` by the square root and deletes the
  final multiply, but duplicates the `atanf`/subtraction tail and changes sign under alternating
  timing at both sizes; restore the smaller singular-tail reciprocal form. Initial end-to-end
  screens are strongly frequency-correlated but include clean favorable pairs up to 3.4%; retain
  the reciprocal form provisionally and require the complete aggregate checkpoint screen plus
  full exact validation before treating it as proven.
- 2026-08-04 — `retained candidate, below checkpoint`: keep process-wide hosted `sqrtf` as the singular square-root owner and its
  binary32 return as the sole canonical result. The retained implementation reproduces PPC's
  reciprocal-estimate call, three double-precision fused Newton steps, multiply, narrowing, and
  volatile reload, while optimized native call sites that GCC can see already issue one hardware
  square root with exact benchmark output. Test the same correctly rounded native square root in
  the out-of-line owner, displacing the estimate/Newton chain and stack publication only. Preserve
  positive/negative zero, negative values, NaN behavior, every caller and output, state,
  allocation, and PPC behavior. Add no approximation, alternate result, compiler setting, or
  per-call dispatch. Reject immediately on either short digest; retain only with the full exact
  gates and controlled gains at both resident sizes against the frozen hardware-`acosf` candidate.
  The complete 32,768-frame production digests remain exact at both resident sizes, and the owner
  shrinks from 137 bytes plus the 345-byte general estimate leaf to a 21-byte positive-test and
  `sqrtss`. Alternating initial screens are frequency-correlated; the clean pairs range from a
  small loss to +1.65% at resident 256 and +1.04%/+3.62% at resident 512. Retain provisionally for
  an aggregate screen. The only remaining positive-float estimate consumer is `asinf`'s private
  reciprocal-square-root path (14,260 calls in the prior 32,768-frame census). Give it the same
  hardware reciprocal without changing exceptional behavior; both short digests remain exact.
  This makes the specialized estimate helper dead, so restore `dolphin_mtx.c`'s original private
  tables and delete the helper and its otherwise-unnecessary table-scope churn completely. A later
  current-chain ablation restored the old estimate/Newton owner without changing any other retained
  source: it lost both resident-256 reversals by 0.97% and 0.60%, and reduced the aggregate
  checkpoint gain from roughly 2% to roughly 0.5%. Restore the hardware `sqrtf`/private `asinf`
  path; the earlier mixed screen was not representative of its interaction with the retained chain.
- 2026-08-04 — `open`: keep `atanf` as the sole exact reduction/polynomial/result owner. It already
  extracts the absolute binary32 word for the retained four-threshold middle classifier, but then
  reloads the outer silver-ratio constants and performs two ordered floating comparisons. For a
  finite nonnegative binary32 value those predicates are exactly unsigned bit comparisons. Use
  the existing absolute word for the two outer admissions and retain a cold explicit NaN path,
  displacing only the constant loads and FP comparisons. Preserve both inclusive boundaries,
  infinity/NaN/signed-zero behavior and payload flow, every reduction and polynomial operation,
  state, allocation, and non-native source. Add no table, approximation, alternate result, or
  compiler setting. Retain only with exact digests and a gain at both sizes against the frozen
  hardware-square-root candidate.
  Both short digests remain exact, but the compiler turns the two guarded bit predicates into two
  integer range checks, grows `atanf` from 329 to 345 bytes, and loses both resident-256 reversals:
  `37,888.7 -> 38,735.3` and `37,956.9 -> 39,401.2` cycles/frame. Restore the original outer float
  comparisons and all three constants completely; do not spend a resident-512 screen on a failed
  required size.
- 2026-08-04 — `open corrected revisit`: keep `PSVECNormalize` as the sole exact vector-normalization
  owner and its published vector as canonical state. The earlier direct-float attempt narrowed the
  PPC estimate to binary32 before `estimate * force_25_bit(estimate)`, even though the source keeps
  the estimate in binary64 until that product; its digest failure therefore does not reject a
  direct input classifier. For the common finite positive-normal binary32 magnitude, reconstruct
  the identical binary64 estimate bits directly from that float's exponent/interpolation, then run
  the unchanged 25-bit rounding, Newton correction, and component publications. Every zero,
  subnormal, negative, infinity, and NaN magnitude remains on the complete general emulator.
  Displace only its redundant binary64 class decode/normalization on the admitted path. Share the
  existing immutable estimate tables; add no state, approximation, alternate vector, allocation,
  compiler setting, or consumer change. Retain only with exact digests and controlled gains at
  both resident sizes against the frozen hardware-square-root candidate.
  The corrected binary64 estimate preserves both short digests. An explicit external exceptional
  leaf reduces the hot owner from 565 to 292 bytes, and simplifying the estimate exponent to the
  exact normal-float identity `1086 - ((e + 1) >> 1)` reduces it to 279 bytes. Resident-512 wins
  four consecutive reversals by 0.89--2.92%. Resident-256 does not: two 131,072-frame brackets in
  opposite orders place the candidate about 1.06% and 1.68% behind the frozen control. The required
  both-size gain is absent after completing and tightening the intended path. Restore the original
  table scope, general normalizer, and runtime fallback leaf completely; retain none of this
  mixed-workload implementation.
- 2026-08-04 — `retained after ablation`: recheck the initialization-compiled dynamics angle
  cutoffs after native `acosf` stops using the PPC estimate/Newton sequence. The cutoffs remain
  final immutable threshold products consumed by the same three solver comparisons; removing
  them restores the complete exact angle calls without changing state or allocation. Both forms
  preserve the short digests. Two resident-256 pairs favor the cutoffs by about 1.1%; resident-512
  results reverse with run order under observed package-frequency drift. Restore the cutoffs
  exactly: their local boundary still deletes demanded work and the only controlled required-size
  evidence favors retention. The rebuilt source is byte-identical to frozen pre-ablation candidate
  `939be5eaea7075c52b498a2ebc4e92d2544f4feb3e80e8234055db0295352981`.
- 2026-08-04 — `rejected`: keep `lbColl_80006E58` and `lbColl_800077A0` as the final owners of their
  three collision distances and the published binary32 values as canonical products. Each local
  source sequence accepts a positive binary32 squared distance, performs three binary64 PPC
  reciprocal-square-root refinements, multiplies by the distance, and narrows to binary32. Test
  the already-retained correctly rounded native square root at those exact publication sites,
  displacing only the estimate/classification/Newton chains. Preserve every distance expression,
  volatile publication, comparison, contact result, state, allocation, and non-native path. Add
  no cache, approximation, alternate result, or compiler setting. Reject on either short digest
  or a loss at either resident size against the frozen hardware-square-root candidate.
  Both short digests remain exact and the three local estimate/Newton chains become direct native
  square roots. The resident-256 reversal nevertheless loses on both sides: controls cost
  38,388.3 and 38,244.2 cycles/frame versus candidates at 38,969.8 and 38,571.3. Restore all three
  local source sequences without spending a resident-512 screen on a failed required size. Their
  code placement and volatile stores evidently schedule better inside these sparse collision paths.
- 2026-08-04 — `profile`: a counter-only ordinary-release caller census, reset after construction,
  preroll, and warmup, measures only the timed 32,768 resident-512 match-frames. It records about
  313,000 `acosf` calls and 250,000 `atan2f` calls, far below the earlier all-lifecycle census that
  included construction. `msl_blend_quaternion_batch` owns 153,817 `acosf` calls. Fighter dynamics
  owns about 133,000 more `acosf` calls plus three adjacent 44,184-call `atan2f` streams; camera
  owns two 37,568-call `atan2f` streams. The remaining callers are small. Remove all counters and
  reporting. Angle work is still material, but optimization must occur inside these three real
  consumers; global memoization or broad scalar-math work was justified by an inflated count.
- 2026-08-04 — `rejected`: keep hosted headless dynamics pruning as the final owner of live fighter
  chains and `Fighter::dynamics_num` as their canonical count. `ftCo_8009DD94` still publishes all
  source dynamics-collider positions and runs dynamics-animation ownership after pruning has made
  the count zero, although both products are consumed only by the absent solver/chains (or retail
  debug presentation). Return at the dynamics owner when no chain remains, displacing those dead
  walks and the empty refresh pass. Retained hurt chains, solver history, fighter pose, callbacks,
  PPC/Wasm behavior, state, and allocation remain unchanged. Add no flag, cache, approximation, or
  alternate path. Require exact digests and controlled gains at both resident sizes.
  Both short digests remain exact. The 256 bracket changes with package-frequency drift and does
  not show a stable adjacency win; the cleaner 512 sequence loses, with the frozen owner at
  38,406.8 cycles/frame and two zero-chain-exit candidates at 38,827.6 and 38,730.0. The skipped
  empty walks are cheaper than the new branch and changed hot layout. Restore the unconditional
  source owner completely.
- 2026-08-04 — `retained candidate, below checkpoint`: keep the compact pose decoder's `parse_float` as the sole owner of compressed
  scalar expansion. Non-float source encodings are signed/unsigned 8/16-bit integers divided by
  `2^(fraction & 31)`; both the integer numerator and power-of-two result are exactly representable
  in binary32. Construct that normal binary32 scale directly and multiply, displacing the variable
  integer-to-float conversion and divide while preserving every decoded bit, command/cursor state,
  publication, non-native behavior, allocation, and consumer. Add no table, cache, approximation,
  state, or compiler setting. Require exact digests and gains at both resident sizes.
  A current-chain ablation restores the source integer-to-float division and preserves the short
  digest, but loses the resident-256 ABBA center by about 1.19%: the multiply candidate centers at
  36,842 cycles/frame versus 37,286 for the division form. Restore the direct exact scale and do
  not spend a resident-512 screen on an ablation that failed one required size.
- 2026-08-04 — `open`: extend the existing compiled integer-sample owner to Figa nodes with
  repeated ordinary SRT track types. Canonical output remains the final source-ordered JObj SRT:
  construction already evaluates every track in source order and writes repeated types to the
  same immutable sample slot, so the later track owns the identical final channel value. Displace
  the mutable decoder, track-state traversal, and repeated publication only for those admitted
  integer frames. Fractional/table-exit transitions, non-SRT/path types, descriptor animations,
  JObj state, callbacks, allocation ceilings, PPC/Wasm behavior, and source order remain unchanged.
  Add no state, second evaluator, cache, approximation, action list, or compiler setting. Reject
  on either digest mismatch; retain only if a construction/runtime census confirms meaningful new
  direct coverage and both resident sizes improve against the frozen current candidate.
  `rejected by census`: the relaxed compiler remains exact, but a disposable runtime attribution
  records 13,147,946 compiled-table publications and zero publications from a repeated-type node
  over the 32,768-frame resident-512 contract. No live decoder work is displaced. Restore the
  conservative uniqueness admission and remove the descriptor marker, counters, and report; do
  not time a zero-incidence path.
- 2026-08-04 — `profile`: timed-only compact-pose attribution resets after construction, preroll,
  and warmup. Of 2,184,225 active joint evaluations in the 32,768-frame resident-512 contract,
  1,922,947 (88.04%) use the exact compiled integer table. The remaining 261,262 Figa calls are
  all already-direct programs forced through the mutable decoder by timing: 219,418 have a
  non-unit rate and 41,844 have a fractional frame at unit rate; only 16 descriptor calls remain,
  with zero non-direct, negative-frame, or out-of-range Figa calls. The expanded roster therefore
  makes fractional decoding live, but the completed 2026-07-22 compiled fractional owner already
  cost 765 source lines, 32.5 MiB shared data, and 816 bytes per fighter for a noise-bound
  whole-simulator result despite a 3.48x isolated speedup. Do not repeat that representation as a
  scalar pose-only cut. A revisit needs a materially smaller direct evaluator or a wider boundary
  that deletes downstream pose products; remove all counters, rate histogram, reset hook, and
  reporting.
- 2026-08-04 — `rejected`: keep `MslCoreMatch::fighters` as the singular convenience pointer to each
  active leader entity. Source respawn reuses leader GObjs; only `Player_SwapTransformedStates`
  changes slot-zero identity for the supported Sheik/Zelda pair, while Popo's separately owned Nana
  follower can be recreated. Refresh leaders only for configured Sheik/Zelda slots and preserve the
  existing Popo follower refresh, null validation, output/render ordering, state, allocation, and
  non-native behavior. Displace only the redundant `Player_GetEntityAtIndex(slot, 0)` lookups for
  every other fighter. Add no cache, flag, character proxy for missing gameplay state, or fallback;
  the character admission names the source transformation owner itself. Require exact digests and
  controlled gains at both resident sizes against the frozen post-matrix-cleanup candidate. Both
  short digests remain exact, but the resident-256 ABBA center regresses from 38,268.1 to 38,530.9
  cycles/frame (about 0.7%). The repeated lookup is cheaper than the new per-slot character branch;
  restore the unconditional leader refresh and do not spend a 512 screen on a failed required size.
- 2026-08-04 — `rejected`: keep `Fighter_procMap` as the source collision-callback owner for every
  supported fighter. Its unconditional post-callback call to `ftKb_SpecialN_800F1D24` can do work
  only when `Fighter::kind == FTKIND_KIRBY`; Kirby is outside the supported roster, and the hosted
  exclusion is therefore a literal empty function. Exclude that call only from the hosted supported
  runtime, displacing the call boundary and no state or gameplay operation. Preserve the source call
  for non-hosted builds, every supported collision callback and ordering, output, allocation, and
  exact digest. Add no roster test or runtime branch. Retain only if controlled measurements improve
  both resident sizes against the frozen pre-change candidate. Both short digests remain exact, but
  the resident-256 ABBA bracket follows run order: `39,121.9 -> 37,766.6`, then
  `39,061.4 -> 37,656.5` cycles/frame in the reverse direction. The symmetric center makes the
  candidate about 0.07% slower. Restore the source call and do not spend a resident-512 screen on
  a failed required size.
- 2026-08-04 — `retained candidate, below checkpoint`: keep the active `MslCoreMatch` as the singular authority for the complete
  hosted owner bundle. `msl_core_match_step_prepare` binds that bundle, and the direct scalar
  scheduler leaves the same Match active, but `msl_core_match_step_finish` unconditionally
  republishes every owner before its post-frame work. Enter finish through the existing
  `bind_scheduler_owners` identity gate instead: it performs the complete bind whenever finish is
  invoked for a different Match and otherwise deletes only the redundant same-Match publication.
  Canonical Match/TLS state, public split-scheduler behavior, ordering, outputs, allocation,
  PPC/Wasm behavior, and every owner remain unchanged. Add no cache or new state. Require exact
  digests and controlled gains at both resident sizes against the frozen current candidate.
  Both short digests remain exact. Resident-512 reversals improve `38,194.2 -> 37,998.1` and
  `37,378.9 -> 36,964.9` cycles/frame (+0.52%/+1.12%). Resident 256 is more frequency-sensitive:
  one bracket reverses sign around a disturbed 38,444.3 candidate run, but the clean forward pair
  improves `37,508.5 -> 37,226.5` (+0.76%) and a later reverse improves
  `38,922.9 -> 37,804.8` (+2.96%). Retain the exact deletion and let the aggregate checkpoint
  bracket determine its contribution rather than selecting the disturbed isolated pair.
  A later retained-source audit found this one-line cut had been displaced while its evidence
  remained in the worklog. Restoring it preserves both short digests; current resident-512 reverse
  pairs are neutral and +1.71%, consistent with the original retained result.
- 2026-08-04 — `rejected`: keep the standard camera transform/FOV as the sole canonical projection
  input for the render-owned fighter visibility pass. Every visible fighter currently recomputes
  the same ordered half-FOV conversion and exact `tanf` reciprocal even though the FOV cannot
  change during that Match-wide pass. Compute the identical cotangent once when the existing view
  is first built and pass that transient scalar to the point projections, displacing only repeated
  trig evaluation. Preserve view construction, point order and arithmetic, visibility/magnifier/
  DeadUp state, allocation, non-native behavior, and every output. Add no cache, persistent state,
  approximation, alternate camera, or compiler setting. Both short digests remain exact and the
  explicit form shrinks `msl_camera_publish_match_visibility` from 889 to 812 bytes. Disassembly,
  however, shows the pre-change compiler already hoists the one `tanf` call out of the fighter
  loop; the rewrite deletes no executed trig. Resident-256 reversals follow run order, while the
  cleaner resident-512 reverse loses `38,479.9 -> 38,865.8` cycles/frame. Restore the original
  source completely; do not retry this already-optimized loop invariant.
- 2026-08-04 — `rejected by census`: before implementing any in-place SIMD rejection for
  `mpCheckFloorRemap`, measure the live floor/dynamic range widths and callback/remap population.
  The only plausible boundary is the existing source-ordered line loop: a retained form may batch
  conservative endpoint rejection but must feed admitted lines into the unchanged scalar narrow
  phase in original order. It may add no index, cached bounds, Match state, alternate topology,
  approximation, or gameplay allocation. Do not implement if the live ranges are too short to
  amortize vector gathers/masks. The first 100,000 calls visit 108,170 admitted joint ranges:
  34,554 contain one line, 33,326 contain two or three, 40,290 contain four through seven, and none
  contain eight or more; 37,594 ranges are remapped and 10,227 calls carry callbacks. There is no
  width for a 16-lane kernel, while four-lane gathers would duplicate the exact AABB work around
  short, pointer-chasing ranges already shown to lose with scalar gates. Remove all counters and
  reporting and do not implement the vector front end. A future stage rewrite needs a different
  canonical traversal, not SIMD wrapped around these source ranges.
- 2026-08-04 — `rejected`: keep `mpCheckFloorRemap` as the singular source-ordered floor-query owner.
  Its source-shaped floor loop jumps back through the same body for each joint's dynamic range,
  but the hosted compiler emits two complete copies of the 3.2 KiB query body. Iterate the two
  existing ranges through one ordinary outer loop so both consume one scalar narrow phase in the
  identical order. Displace only duplicated native instructions; preserve callback/line order,
  all vertices/flags/intersections/results, state, allocation, PPC/Wasm behavior, and outputs. Add
  no helper, index, cache, alternate topology, approximation, or compiler setting. Both digests
  remain exact, but GCC had already shared nearly all of the apparent duplication: the outer loop
  shrinks the 3,233-byte function by only 40 bytes while adding a range branch to every short
  joint range. Resident-256 reversals follow run order rather than showing a gain. Restore the
  source-shaped range transition completely without spending a resident-512 screen on a failed
  required size.
- 2026-08-03 — `rejected by census`: test whether the three adjacent `atan2f` evaluations in
  `msl_dynamics_rotate_euler` admit one exact native three-lane evaluator. Final state remains the
  live dynamics Euler rotation; the singular consumer is the existing matrix-to-Euler conversion.
  A retained form would displace only three scalar call/classification/polynomial chains, preserve
  every per-lane operation and exceptional result, and leave PPC/Wasm on the source path. It may
  add no cache, table, mutable state, approximation, or alternate rotation. First count the
  all-positive-denominator/small-ratio population; implement only if that common path is large
  enough to repay one aggregate admission and SIMD setup. Only 26,390 / 100,000 calls (26.39%)
  put all three lanes on that common path. The other calls require per-lane quadrant/reduction and
  lookup handling, recreating the masked/gather overhead that already made the wider exact angle
  batches slower. Remove the counters and print without implementing another vector evaluator.
- 2026-08-03 — `rejected`: reassess only the always-adjacent nonsingular matrix-to-Euler triplet
  without masked lookup gathers. Keep scalar `atan2f`/`atanf` as the source definition and the live
  Euler vector as canonical state. Prepare each lane's exact scalar quadrant and reduction state,
  evaluate only the shared seven-deep polynomial in one native three-lane FMA chain, then apply the
  source corrections per lane. This displaces three serialized polynomial chains and call shells;
  matrix construction, division/reduction order, exceptional paths, state, and ARM/PPC/Wasm remain
  unchanged. Retain only if exact digests and both resident sizes beat the frozen pre-change
  candidate; otherwise remove the helper and call site completely. Both digests remain exact, but
  resident 256 changes sign across the two reversals and resident 512 loses both: the cleaner
  reverse costs `38,593.5 -> 39,091.2` cycles/frame (+1.29%). Scalar lane preparation, packing,
  and extraction outweigh overlapping the polynomial. Remove the helper, API, intrinsic include,
  and call site completely.
- 2026-08-03 — `rejected`: retain `lb_8001044C` as the dynamics owner and its normalized
  `natural_dir`, `current_dir`, `saved_dir`, and changing `link_dir` vectors as the only canonical
  state. The source recomputes the unchanged normalized `natural_dir` length for convergence and
  deviation, and the unchanged `saved_dir` length for max-angle and final angular velocity.
  Compute each of those two exact lengths once after normalization and pass it through the existing
  dynamics-only angle leaves, displacing only repeated identical square-root products. Preserve
  dot/FMA order, link-length evaluation, threshold keys, `acosf`, rotations, state, allocation,
  PPC/Wasm behavior, and every output. Add no cached Match state or alternate vector; retain only
  if both digests and both resident sizes improve against the pre-change candidate. The complete
  cut is exact at both short digests but loses decisively: resident 256 changes from 38,379.6 to
  39,394.3 cycles/frame and resident 512 from 38,227.8 to 39,433.1. Hoisting the two square roots
  into the already-large solver and widening the runtime-angle ABI costs more than recomputation in
  the compact leaves. Restore the original leaf signatures and all call sites completely; do not
  retry frame-local length caching without a wider dynamics kernel that already owns the angle
  consumers.
- 2026-08-03 — `rejected`: keep the retained positive-normal PPC `frsqrte` table helper as the sole
  `acosf`/`asinf` estimate owner. Its fixed-point table result is currently packed as binary64 and
  immediately converted to binary32; only the low three fixed-point bits participate in that
  round-to-nearest-even conversion. Pack the identical binary32 exponent/significand directly and
  displace only the temporary binary64 value and hardware narrowing. Preserve the corrected
  half-exponent rule, table/interpolation, exact rounding, Newton steps, exceptional admission,
  outputs, state, allocation, and non-native path. Retain only with exact digests and improvement
  at both resident sizes against the pre-change candidate. Direct packing preserves both digests,
  but enclosed screens lose at both sizes: resident-256 candidate 39,968.3 cycles/frame versus
  controls 38,585.2/40,448.3, and resident-512 candidate 40,084.5 versus controls
  38,349.8/40,135.8. The extra integer rounding/control chain schedules worse than the native
  narrowing despite deleting the temporary double. Restore the retained binary64 pack and cast
  completely.
- 2026-08-03 — `rejected`: keep `Fighter_Spaghetti_8006AD10` as the sole input-recurrence owner and
  every existing timer byte as canonical state. Its O0 source closure repeats the same byte
  increment, reload, compare, branch, and corrective store for each `0xFE`-saturating stick/trigger
  timer. On native builds, express the identical complete-byte transition directly: increment with
  byte wrap, then subtract one only from the `0xFF` result. This preserves even the unreachable
  incoming-`0xFF` behavior (`0xFF -> 0`), all reset/admission branches, UCF order, state, allocation,
  and non-native source operation. Displace only repeated clamp control; retain only with exact
  digests and improvement at both resident sizes. Both digests remain exact, but two 65,536-frame
  resident-512 reversals lose: candidate costs are 39,665.2 and 40,175.9 cycles/frame versus
  controls 39,077.9 and 38,878.0. Shorter screens change sign at resident 256. The saturated source
  branches are predictable and avoiding their reload/corrective store does not repay the added
  byte dependency chain. Restore the original source blocks and remove the macro completely.
- 2026-08-03 — `rejected`: test whether a construction-time static-stage spatial index has enough
  admission to justify implementation. Final gameplay owner would remain the source-ordered
  `mpCheck*` query families; canonical mutable vertices, line flags/topology, query order, and
  intersection arithmetic would not change. Immutable construction metadata would conservatively
  identify original-order static-line candidates for the query AABB, while remapped/dynamic joints
  retain the source scan. This differs from the rejected per-line AABB gate: it is useful only if
  it skips most static candidates before loading their endpoints or entering narrow phase. First
  count static floor-remap line visits and exact endpoint-AABB overlaps in one bounded ordinary
  release run. Of 1,000,000 visited eligible static lines, only 7,117 overlap both axes (0.71%);
  X alone admits 294,515 (29.45%) and Y alone admits 59,597 (5.96%). All counters and printing are
  removed. A one-dimensional immutable Y endpoint index conservatively admitted source-ordered
  static candidates; dynamic/remapped joints and callback queries retained their source scans.
  Merely testing its bitset inside the existing loop was neutral. Jumping directly between admitted
  IDs was exact at both short digests, and an internal all-lines ablation misleadingly suggested
  gains of 0.85% at resident 256 and 1.96% at resident 512. A real pre-index control, with index
  construction and query work compiled out, reverses that result: resident-512 indexed runs were
  39,281 and 40,638 cycles/frame versus pre-index runs of 38,550, 38,453, and 38,152. Resident 256
  was mixed under contention (indexed 38,900/39,683 versus pre-index 40,004), so the required
  both-size gain is absent and 512 has a repeated 4--6% regression. Extending the same index to the
  endpoint-expanded non-remap floor path was exact but slower again. Per-joint candidate iteration,
  binary searches, and roughly 4 MiB of resident-512 index state cost more than the rejected narrow
  phases. Remove the representation, allocation, and hot-loop edits completely. A revisit needs a
  compact source-order query plan that bypasses joint traversal itself and must be proven in
  isolation against an actual pre-index control; do not retry a per-line membership gate or this
  per-joint bitset iterator.
- 2026-08-03 — `rejected reassessment`: test the remaining spatial-index hypothesis with the deletion
  boundary the rejected form lacked. Final owner remains `mpCheckFloorRemap`; canonical vertices,
  line/joint state, callback order, narrow-phase arithmetic, and result publication do not change.
  Build one compact per-Match sequence in the source's exact joint/static/dynamic traversal order
  plus conservative Y-bin masks. Ordinary no-callback queries enumerate only admitted sequence
  entries globally, deleting the linked-joint and rejected-line traversal itself. Callback,
  nonzero-offset, remapped, and dynamic work remains fully admitted; moving-capable joints are
  conservative. Add no result cache, alternate topology, changed line order, approximation,
  gameplay allocation, or fallback dispatch. The complete plan must remain a few kilobytes per
  Match, preserve both digests, and beat the frozen true pre-plan binary at both resident sizes;
  otherwise remove it completely. The first binned form changed the resident-256 digest because
  collision joints acquire moving JObjs after `mpLibLoad`; `dynamic_count` therefore cannot prove
  that a joint's authored floor vertices remain static. Conservatively admitting every floor line
  restores the exact `4124834367a202ec` digest and isolates the traversal deletion, but loses
  decisively at resident 512: candidate runs cost 40,411.0 and 40,959.7 cycles/frame versus
  bracketed true pre-plan controls at 39,064.0 and 38,357.2. The linked-joint traversal is not the
  material cost, and a flat sequence plus bitset iteration adds roughly 5% without eliminating
  narrow work. Remove the plan, Match allocation, inferred-field storage, and query-loop changes
  completely. Do not retry an external line index unless an existing immutable stage owner can
  prove post-attachment world bounds without synchronization hooks; focus instead on deleting
  demanded work inside an existing hot owner.
- 2026-08-03 — `rejected`: keep the three retained dynamics angle leaves as the sole cosine and
  comparison owners. Their measured 708,501 calls per 32,768 resident-512 frames evaluate two
  exact source `sqrtf` lengths each, and each length currently enters `sqrtf` and then the general
  binary64 `__frsqrte` classifier. The squared vector lengths that pass as finite positive normals
  can enter the existing PPC estimate table directly and execute the same three binary64 Newton
  steps and final float publication; exceptional inputs retain the singular general `sqrtf` path.
  Displace only the redundant call/classification boundary inside these dynamics consumers.
  Preserve estimate bits, FMA/multiply ordering, zero/subnormal/infinity/NaN behavior, cosine
  ordering, thresholds, state, allocation, PPC/Wasm behavior, and all outputs. Add no cached
  length, alternate state, approximation, or compiler setting. Retain only if exact digests and
  both resident sizes improve against the frozen pre-change candidate. Disassembly of the true
  pre-change control disproves the presumed call boundary: GCC already recognizes each local
  `sqrtf` and emits an exact direct `vsqrtss` in all three angle leaves. The explicit source-exact
  PPC estimator instead adds a call, classification, and three binary64 Newton steps. Digests stay
  exact, but resident-512 reversals lose `38,363.7 -> 39,282.0` and
  `37,426.9 -> 38,552.4` cycles/frame. Remove the estimator export, local square-root leaf, and
  all call-site changes completely. Do not retry a dynamics square-root call optimization; the
  current generated hot path has no such call to delete.
- 2026-08-03 — `rejected`: keep the retained initialization-compiled `acosf` comparison boundary as
  the sole threshold product, but store the boundary float rather than its integer total-order
  key. Runtime cosines are clamped to finite `[-1, 1]` or handled as NaN, so native float
  comparison has exactly the same order; `acosf(-0)` and `acosf(+0)` are equal, so collapsing the
  two zero encodings also preserves the source predicate. Displace the per-call memcpy, sign test,
  complement/XOR, and integer comparison from all three angle leaves. Preserve binary-search
  construction, strict/non-strict boundaries, cosine arithmetic, NaN/zero semantics, state size,
  allocation, PPC/Wasm behavior, and all outputs. Add no table, cache, approximation, or alternate
  state. Retain only with exact digests and gains at both resident sizes against the frozen
  integer-key candidate. Both short digests remain exact, but resident-512 reversals lose
  `38,214.6 -> 38,542.0` and `38,214.5 -> 39,005.1` cycles/frame. The direct float predicate does
  not improve the generated clamp/NaN/branch schedule enough to replace the compact integer key.
  Restore the key representation, loader, helper signatures, and comparisons completely.
- 2026-08-03 — `rejected`: keep each dynamics angle leaf as the owner of its two source-ordered vector
  lengths. Current x86-64 disassembly evaluates the independent squared lengths with identical
  scalar FMA chains followed by two `sqrtss` instructions. Pack only those two lanes, retain the
  same per-lane multiply/FMA order, and issue one exact `sqrtps`; the existing cosine multiply,
  divide, clamp, cutoff, and result paths remain scalar and unchanged. ARM/PPC/Wasm retain the
  existing source expressions. This displaces one hardware square-root instruction per angle
  call without changing the representation, state, allocation, or mathematical operation within
  either lane. Retain only with exact digests and gains at both resident sizes against the frozen
  scalar-root candidate. Both digests remain exact. Resident-512 reversals improve
  `37,703.3 -> 37,103.7` and `38,910.5 -> 38,772.8` cycles/frame (+1.62%/+0.35%), but resident 256
  does not retain a gain: after one candidate-side contention outlier, the clean pairs are a 0.40%
  win and a 0.07% loss (`38,147.9 -> 38,120.3`). Packing and extracting the two lanes repays one
  square-root instruction only at the larger resident size. Restore the scalar expressions and
  remove the x86 intrinsic helper/include completely.
- 2026-08-03 — `retained candidate, below checkpoint`: keep each observation/compare record as the sole canonical output owner and
  its one full-record clear as the zero-value publication. Both internal item-writer callers clear
  their complete parent record before publication, yet the item writer clears all 768 item bytes
  again and rebinds the already-active Match; each populated 56-byte observation player is also
  cleared again. Rename the internal item boundary to state its zeroed-output precondition and
  delete only those duplicate clears and same-Match bind. Preserve every written byte, absent-slot
  zero, item ordering/canonicalization, public API validation, state, allocation, and output. Add
  no dirty tracking or alternate projection. Retain only with exact digests and gains at both
  resident sizes against the frozen pre-output-cut candidate.
  The first complete clear/bind deletion is positive at resident 512 but controlled-neutral at
  resident 256. Complete the same immediate boundary before disposition: the item writer already
  knows its dense emitted-item count, while direct production observation currently calls the misc
  canonicalizer for all 15 slots, including zeroed absent slots. Return the existing count and
  canonicalize only emitted slots in the direct path; compare-derived observation and viewer paths
  retain full-array canonicalization because they do not own that live count. This adds no state or
  scan and preserves dense item order and all absent bytes. Both short and 131,072-frame digests
  remain exact. Two resident-256 reversals improve `38,841.5 -> 38,378.5` and
  `39,286.0 -> 38,811.2` cycles/frame (+1.21%/+1.22%). Resident-512 timing is noisier under active
  machine contention: the first clean pair loses 0.57%, while subsequent reversals and low-cost
  samples favor the candidate; the lowest adjacent reverse improves `38,675.5 -> 37,107.3`
  (+4.23%), and the best candidate is below the best control. Retain this direct deletion
  provisionally because both-size evidence is positive overall and no compensating work/state is
  added; re-evaluate it in the final aggregate checkpoint rather than attributing a precise 512
  point gain now. Frozen candidate SHA-256:
  `5fee77e963a0856a7ed6c349ed154ba9130dda93ba65781bee30a85cd51f74cb`.
  Continue only within this output boundary: direct observation and terminal projection already
  own `MslCoreMatch`, whose canonical `MslPlayerState::slots` contains the exact stock byte read by
  `Player_GetStocks`. Read that field directly in output projection and remove terminal's complete
  Match bind, which exists solely for those getter calls. Apply the same direct read in the cold
  compare projector for one consistent output owner. Gameplay source calls remain unchanged; add
  no copied stock state or changed slot mapping.
  The complete direct-stock form changes sign at resident 256. Narrowing it to only terminal's
  stock read and bind deletion still changes sign in two 131,072-frame reversals: one loses about
  1.7%, and the reverse improves about 1.5% amid frequency drift. This is below stable attribution
  and does not improve the proven item-count cut. Restore every direct stock read and terminal bind;
  retain no stock-access change.
  The terminal's fixed eight-team bit-count loop is a separate pure consumer of its already-built
  canonical `team_mask`; current disassembly expands it into a long SIMD compare/mask/reduction.
  Replace only that loop with the exact population count of the same byte. This changes no team
  admission, mask construction, output width, state, or architecture contract. Disassembly
  replaces the prior SIMD compare/mask/reduction with one `popcnt`. Both digests remain exact.
  Resident-256 reversals improve +0.07% and +1.73%; resident-512 reversals improve +4.11% and
  +1.17% under the same frequency drift. Retain the one-line reduction and remeasure its aggregate
  contribution at checkpoint. Frozen candidate SHA-256:
  `c7f74b5388b7718fa8c5afed5f71f0e37f3fb768b55a281c0d330ff1ce6566b3`.
- 2026-08-03 — `retained candidate, below checkpoint`: keep the packed wire records as the canonical output and the six existing
  endian helpers as their only byte-order operation. Disassembly shows every player observation
  still makes nine `put_lef32` and four `put_le16` calls; four-player frames therefore pay up to 52
  call/return boundaries for trivial byte stores, with more in item, terminal, compare, and viewer
  projection. Define those internal primitives as the same `static inline` byte operations in
  `wire.h` and delete their out-of-line copies. Preserve exact wire bytes, unaligned access safety,
  float bit patterns, big/little-endian behavior, public structures, state, allocation, and all
  higher-level projection. Add no native-only casts, compiler attributes, or settings. Retain only
  with exact digests and gains at both resident sizes against the frozen output-popcount candidate.
  Both digests remain exact, every wire-helper call disappears from `write_player`, and the hot
  functions shrink (`write_player` 570 -> 556 bytes; `msl_core_match_write_observation` 1,266 ->
  1,234 bytes). Resident-512 reversals improve `39,174.1 -> 37,709.8` and
  `38,540.7 -> 37,923.6` cycles/frame (+3.88%/+1.63%). Resident-256 has one frequency-skewed losing
  pair, one strongly favorable reverse, and the deciding adjacent pair improves
  `38,940.3 -> 38,227.6` (+1.86%). Retain the inline primitives while accumulating and require
  both sizes again in the aggregate checkpoint. Frozen candidate SHA-256:
  `f3b0130dd321df19fa389d937425fb9f59f350ca29534b84bee74cc24c2192ad`.
  After wire inlining, `write_player` has exactly one remaining call. Isolate only its stock
  projection: the function already owns `MslCoreMatch`, and the canonical source slot contains the
  same byte returned by `Player_GetStocks`. Directly read that byte without changing terminal,
  compare, or gameplay call sites. This is narrower than the rejected broad stock-access change
  and must independently improve both sizes against the frozen wire-inline candidate. It preserves
  both digests and shrinks `write_player` by 16 bytes, but repeated resident-512 reversals simply
  follow frequency direction and flip sign by 2--3%; no stable incremental gain is attributable.
  Restore the getter and retain no direct stock read.
- 2026-08-03 — `rejected broad phase`: the remaining standard-camera owner is 4.32% of the current controlled
  profile. `Camera_80029CF8` constructs the target camera from subject bounds and immediately
  `Camera_8002A768` reconstructs four world-space frustum corners to decide whether stage-bound
  correction is required. Count actual correction admissions over the complete 366-case
  initialization/preroll before changing code. If negative frames dominate, test a conservative
  proof from the already-computed target bounds that enters the unchanged exact corner owner only
  when correction may be needed. Final mutable owner remains `CameraTransformState`; canonical
  subject bounds, transform, correction, visibility, viewer output, and scheduler order remain
  unchanged. Displace only provably unnecessary corner reconstruction; add no cached transform,
  alternate camera state, approximation, allocation, or persistent diagnostic. A correctly
  relinked 65,536-frame resident-512 census observes correction in 20,716 of the first 300,000
  calls (6.91%). Coarse interior proofs using symmetric margins of 0.75x, 1.0x, and 1.25x target
  depth admit only 6.77%, 1.83%, and 0.08% of calls; the 0.5x margin admits 28.8% but has 70 false
  negatives and is not exact. Remove all counters. A useful revisit must carry the producer's
  actual frustum products across the immediate call boundary; another coarse branch cannot delete
  enough demanded work.
- 2026-08-03 — `rejected`: keep `Camera_8002958C` as the sole subject-bounds owner, but snapshot
  its ground floor and four stage-camera bounds once before the admitted-subject traversal. Each
  subject currently runs five `Camera_80029124` probes, and every probe repeats the same
  `Ground_801C4368` plus stage getters; a reported boundary then repeats the ground query again for
  the identical bottom clamp. No callback or topology mutation occurs inside this traversal.
  Replace only those repeated reads with frame-local scalars and the same ordered comparisons and
  clamps. Preserve subject admission and mutation, source extent order, camera products, correction,
  PPC/Wasm behavior, state, allocation, and all outputs. Add no persistent cache or alternate owner;
  retain only with exact digests and gains at both resident sizes from a verified relinked binary.
  The implementation preserves both short digests and shrinks `Camera_8002958C` from 1,618 to
  1,471 bytes. A short resident-256 bracket appears positive, but the required resident-512 screen
  reverses: candidate runs at 37,353 and 38,327 cycles/frame surround a 36,868 frozen control.
  Repeated pure getter calls are layout/issue-cheap here, while folding all comparisons into the
  already large camera owner is slower. Restore the source calls, locals, and clamps completely.
- 2026-08-03 — `rejected`: keep `PSMTXConcat` as the singular exact affine-product owner and
  each destination matrix as its sole canonical result. The native compiler currently versions
  and expands the fixed 3x4 kernel because it cannot prove that the three matrix arguments do not
  partially overlap, although the source contract handles only exact destination aliasing. Load
  the complete right operand before publication and evaluate each row with the same mul/FMA order,
  displacing only runtime alias checks, scalar fallback copies, and auto-vectorization shuffles.
  Preserve exact in-place behavior, translation rounding, PPC/Wasm source, state, allocation, and
  every consumer. Add no compiler attribute or setting. This deliberately re-evaluates the 2026-07-19
  isolated affine kernel because current campaign policy accumulates exact one-point code gains;
  retain only if both current digests and both resident sizes improve against the frozen
  lbvector-inline candidate. The explicit kernel shrinks the native leaf from 1,279 to 173 bytes
  and preserves both digests, but longer 131,072-frame reversals remain mixed at both sizes:
  resident 256 changes from a 0.85% win to a 0.31% loss, and resident 512 changes from a 2.68% win
  to a 0.32% loss before a separately observed CPU-0 contention event. The change cannot support a
  retained improvement at both sizes; restore the compiler-generated exact-aliasing path and do
  not count code size as throughput evidence.
- 2026-08-03 — `rejected before implementation`: keep each dynamics JObj matrix as the sole canonical world transform
  and `lb_8001044C` as the exact solved-chain owner. The solver already constructs the final world
  basis for every rotated link in source order, but `Fighter_8006D9AC` later walks the same dirty
  links and reconstructs those matrices again solely to reproduce retail's end-of-frame render
  publication. Publish the already-computed final basis to the matching JObj as each link is
  completed, and complete the tail basis once, displacing the entire redundant post-solve matrix
  walk. Preserve the existing JObj matrix representation and dirty semantics, exact transform
  operations, re-anchor reads, hurtbox consumers, scheduler order, PPC behavior, allocation, and
  all outputs. Add no cache, alternate state, fallback path, or approximation. Retain only if the
  seven dynamics-sensitive source locks, both benchmark digests, full validation, and both
  resident sizes remain exact and improve. The consolidated August 3 journal records this exact
  boundary already: solver `unk_2C` and render-matrix translation are not interchangeable, replay
  33692 forks under the substitution, and separate attribution caps the complete demanded refresh
  at 0.55% of the contract. Preserving the distinct exact render operation stream while sharing
  the traversal cannot materially advance the target. Make no runtime change and keep the existing
  post-solve publication.
- 2026-08-03 — `retained candidate, below checkpoint`: the dominant compiled-pose masks publish rotation X on most dense table
  hits. Count the existing `PUBLISH_ROTX` entries and the subset that actually enters the
  `JOBJ_JOINT1` IK-hint lookup before adding any classification bit or specialized publisher.
  Remove the counters immediately after one bounded resident-512 census; proceed only if the
  avoided joint-flag/IK path has a material hot population and a representation-free deletion.
  The complete 366-case initialization and 200,700-frame preroll construct 86,378 retained pose
  joints and execute 9,334,593 rotation-X publications; none of the retained joints has
  `JOBJ_JOINT1`. Make absence of that unsupported IK joint class an initialization assertion and
  remove its unreachable RObj lookup from the native generic and dense pose publishers. PPC/Wasm
  retain the source path. This adds no state or gameplay branch; counters and printing are removed.
  `interpret_joint` shrinks from 4,635 to 4,182 bytes. Both short digests remain exact; enclosed
  resident-256 screens improve 0.52--3.20%, and two resident-512 reverse screens improve 1.72% and
  2.12% (a longer frequency-drifting screen is neutral). Retain while accumulating and confirm in
  the final alternating checkpoint screen.
- 2026-08-03 — `retained candidate, below checkpoint`: use the same construction census to test whether any retained native pose
  joint admits `JOBJ_MTX_INDEP_SRT`, and count actual dense publications taking that exception.
  The native publisher currently reloads and tests this flag after every successful table write.
  Remove diagnostic counters immediately; delete the branch only behind a construction invariant,
  with the source exception unchanged for PPC/Wasm and non-pose JObjs. All 86,378 constructed pose
  joints and 11,074,192 observed dense publications have the flag clear. Extend the initialization
  assertion and publish the canonical dirty bit directly in both native pose paths; retain no
  counter, print, or gameplay admission branch.
- 2026-08-03 — `rejected`: keep each active compact fighter-pose node as the source animation
  owner, but census whether any native Figa or AnimJoint attachment admits `AOBJ_NO_UPDATE` and
  whether the fighter flag API can set it. If the complete 366-case construction/preroll proves
  this source mode absent, make its absence an initialization/API invariant and remove the runtime
  publication predicate from `interpret_joint` and its dense publisher. Preserve explicit dry
  decoder walks, all clock/loop/end behavior, JObj SRT, callbacks, PPC/Wasm behavior, allocation,
  and exact outputs. Add no state or alternate path; retain only if both digests and both resident
  sizes improve over the frozen pose-invariant candidate. Native assertions survive the complete
  366-case construction/preroll, and deleting the gate shrinks `interpret_joint` by 121 bytes, but
  an enclosed resident-256 screen is neutral/slower: controls are 38,474.9 and 38,943.0
  cycles/frame around a 38,824.4 candidate with the exact digest. Restore the source flag and
  publication behavior rather than retaining an unmeasured domain restriction.
- 2026-08-03 — `rejected`: keep the dense program node's existing `type_mask` and JObj SRT as
  the sole publication inputs/output, but admit the measured `0x00E` rotation triplet before the
  generic switch. It owns 63.31% of dense publications and currently enters a range check plus an
  indirect jump whose historical instruction profile mispredicts frequently. Displace only that
  dispatch for the common mask; preserve value loads/stores, dirty publication, every other mask,
  representation, state, PPC/Wasm behavior, and exact output. Add no classifier field or duplicate
  publisher. Retain only if disassembly shows a direct common branch and both resident sizes beat
  the frozen pose-invariant candidate. The compiler emits the intended direct `0x00E` branch and
  changes `interpret_joint` by only two bytes, but alternating 131,072-frame resident-256 runs do
  not retain the apparent first-pair win. Candidate median is 38,069 cycles/frame versus 37,849 for
  the frozen control, about a 0.6% loss with the exact digest amid large machine-frequency drift.
  Restore the compact switch; do not treat dispatch shape or code size as a gain.
- 2026-08-03 — `rejected after provenance repair`: keep `lbVector_Sin`/`lbVector_Cos`'s source-authored approximate
  quintic operation streams as the exact coefficient owners for axis rotation and Euler-matrix
  construction. Native callers always require both results for the same angle, yet currently run
  the two independent scalar polynomial streams serially. Evaluate those streams in two SIMD lanes
  after the same scalar binary64 range comparisons/reductions, retaining every binary32 multiply
  and fused operation in its original lane order. Displace only serial issue across the pair;
  preserve all outputs, axis/matrix operation order, generic consumers, PPC/Wasm scalar code,
  state, and allocation. Add no cache, table, approximation, or API. Retain only if both digests,
  focused gates, and controlled resident-256/512 comparisons improve over the frozen pose candidate.
  The compiler emits the paired SIMD streams and shrinks the three callers by 32, 32, and 80 bytes,
  but the timing screen accidentally used a `replay-bench` that had not been relinked after the
  runtime-object rebuild. Its exact digests therefore prove only the frozen prior binary, and none
  of its timing is candidate evidence. Remove the SIMD source rather than retain an unmeasured
  rewrite. Any revisit must use `native-release-benchmark` and verify the executable timestamp and
  hash before timing. Reapply the same bounded source now that the explicit relink contract is
  established; compare its verified executable directly to the frozen pose-invariant binary. The
  relinked candidate preserves both 131,072-frame digests. Resident 256 is approximately +0.17%
  in one bracket. A reverse resident-512 bracket is 38,214 cycles/frame versus controls averaging
  38,236 (+0.06%), while the opposite order changes sign under measured whole-machine frequency
  drift. Packed issue is controlled-neutral on the native core. Restore the scalar source; the
  small code-size reduction does not justify ISA-specific machinery.
- 2026-08-03 — `rejected`: keep the four source-authored fused-subtract operations in
  `ftcoll.c` as the exact contact/combo owners, but expand their native intrinsics locally instead
  of calling the six-byte shared leaves from O0 fighter code. Displace only call/return and live
  spill boundaries in `comboCount_Push` and the angle-362 hurt-midpoint path. Preserve every
  operand, FMA, sign, branch, state update, PPC/Wasm path, contact result, and allocation. Add no
  helper or broader intrinsic policy; retain only with exact digests and gains at both sizes over
  the frozen pose-invariant candidate. The two-call combo leaf shrinks by two bytes, but the rare
  midpoint path grows the 6.4 KiB contact owner by eight bytes. The resident-256 digest remains
  exact while an enclosed 32,768-frame screen regresses about 1.7% against its surrounding
  controls. Restore both shared intrinsic calls and do not spend a resident-512 screen on a failed
  required size.
- 2026-08-03 — `rejected before timing`: keep `atanf` as the singular exact PPC approximation owner and
  preserve its reduction, lookup, polynomial, signs, and every caller. The mid-range transform
  pays two out-of-line `__fnmsubs` calls while keeping the reciprocal and lookup values live.
  Expand only those two authored scalar-single operations inside `atanf`, displacing their
  call/return and spills. Leave the shared intrinsic symbol, other math, PPC/Wasm behavior, state,
  allocation, and compiler profile unchanged. This is distinct from the rejected trig-tail local
  expansion; retain only with exact digests and gains at both resident sizes over the frozen
  pose-invariant candidate. Disassembly shows the existing O2 translation unit already inlines
  both calls: the function remains 329 bytes and contains the fused instructions directly. The
  explicit expression merely selects the opposite signed FMA forms and reverses the final subtract;
  it deletes no work. Restore the source intrinsic calls without benchmarking an empty rewrite.
- 2026-08-03 — `rejected`: keep the three hosted dynamics comparison leaves as the final owners
  of their source-exact `lbVector_Angle` decisions, with live vectors and compiled immutable acos
  cutoffs as canonical inputs. Before evaluating two square roots, their product, and a divide,
  compare the already source-ordered dot and squared lengths against the cutoff's squared cosine
  using binary64 products and a conservative relative exclusion band. Opposite signs decide
  directly; non-finite, tiny, or near-boundary inputs enter the complete existing exact path.
  A taken deviation that consumes the angle also enters the exact path. Displace exact sqrt/divide
  work only for decisions proven outside the band. Add no state, table, approximation, changed
  output, or alternate canonical result; PPC/Wasm remain source-shaped. First run an always-exact
  diagnostic that counts fast decisions and asserts their classifications against the existing
  result. Retain a final no-counter fast path only if the observed coverage is material, mismatches
  are zero, full validation is exact, and both resident sizes improve over the frozen pose
  candidate. With a conservative `1e-4` squared-ratio exclusion band, 492,727 of the first 500,000
  comparisons (98.55%) are decided outside the exact path and all 492,727 agree with the existing
  result. The final helper additionally admits only finite normal squared lengths, a safely valid
  length product above `1e-18`, a finite float-range product, and a boundary at least `2^-20` from
  zero; every other class falls back. Remove all counters/printing and use the fast result directly
  for the two Boolean consumers and only the false, angle-not-consumed deviation result. Exact
  deviation angles retain the original sqrt/divide/acos stream.
  The first complete form regresses the resident-256 enclosed screen: the deviation consumer is
  true often enough that its speculative squared work is then followed by the full exact stream.
  Remove that consumer from the fast path and retain the experiment only in the max/convergence
  leaves, where every successful classification deletes the complete exact computation.
  Max-plus-convergence is neutral/slightly slower at resident 256. Convergence is only 38,954 of
  the measured calls and cannot amortize another expanded classifier leaf; restore its exact path
  as well and isolate the dense max-angle consumer before disposition.
  The isolated max leaf still loses about 0.9%. Its binary64 products and conversions cost nearly
  as much as the hardware square roots they displace. Screen one final strictly conservative float
  form with a ten-times-wider `1e-3` exclusion band; binary32 product error remains far inside that
  band, and any close result retains the exact path. The compact max-only form also loses: enclosed
  resident-256 controls are 37,759.0 and 37,701.3 cycles/frame versus 38,678.4 for the candidate,
  about a 2.5% regression with the exact digest. The diagnostic's 492,727 fast decisions therefore
  did not translate into cheaper execution: expanded classification, extra products, and code
  pressure cost more than the compact hardware-square-root/divide leaf they displaced. Restore the
  original compiled cutoff keys and exact comparison leaves, including storage and APIs; retain no
  classifier, counter, or alternate path.
- 2026-08-03 — `rejected`: keep the PPC `fres` estimate as the exact reciprocal seed owner used
  by `PSMTXInverse` and `PSMTXQuat`. Both callers compute a binary32 determinant/norm and currently
  widen it to binary64 before inlining the general binary64 classification and estimate path. Map
  every binary32 class directly to the identical final binary32 seed, including the two upper
  subnormal bins, signed zero/infinity, overflow-to-zero, and NaN quieting. Displace only widening,
  64-bit classification, and double-result narrowing; preserve the estimate table, Newton steps,
  matrix operation order, PPC/Wasm behavior, state, and all consumers. Add no approximation,
  compiler attribute, setting, table, or fallback owner. The direct mapping halves
  `PSMTXInverse`/`PSMTXQuat` to 512/268 bytes and preserves both digests. An isolated affine-only
  ablation, however, shows the direct form improving resident 256 by about 1% while losing both
  131,072-frame resident-512 reversals: `35,316.2 -> 36,681.8` and
  `36,858.8 -> 37,800.6` cycles/frame. Restore the original double-domain emulator and table scope;
  retain none of this mixed-size candidate.
- 2026-08-03 — `rejected`: keep `PSVECNormalize`'s PPC `frsqrte` seed and Newton sequence as the
  exact vector-normalization owner. Its magnitude is binary32, but the current native leaf widens
  it and inlines the full binary64 instruction emulator before narrowing the seed. Classify the
  original binary32 and generate the identical binary64-table estimate narrowed to binary32,
  including signed zero, subnormals, infinity, NaN, and negative inputs; leave the existing public
  positive-normal leaf intact for its angle consumers. Displace only redundant binary64 input
  decoding. Preserve the 25-bit multiply rounding, Newton/FMA order, results, callers, PPC/Wasm,
  state, and allocation. The first resident-512 screen changes digest
  `9c1f37ab084f8ef2 -> a469d6a694cf45f1`; the direct float mapping therefore misses a source
  rounding/classification case. Restore the complete binary64 emulator and the original compact
  positive-normal helper before further timing; retain none of this experiment.
- 2026-08-03 — `rejected`: keep `PSMTXMultVec` as the singular paired-single affine/vector owner
  and its caller's `Vec` as the canonical result. Native auto-vectorization currently reconstructs
  matrix columns piecemeal and computes the third lane separately. Load all three rows before any
  publication, transpose them once in registers, and evaluate all three lanes with the same
  multiply, two FMA, and final-add boundaries. Displace only redundant loads/shuffles and scalar
  tail scheduling. Preserve complete source/destination alias behavior, exact results, PPC/Wasm,
  state, allocation, and all consumers. Add no compiler attribute or setting. Both short digests
  remain exact and the leaf shrinks from 179 to 96 bytes, but the resident-256 enclosed screen is
  neutral while resident 512 loses about 1.6--1.8%. Reconstructing all four columns costs more than
  the compiler's asymmetric two-lane/scalar schedule. Restore the source loop completely.
- 2026-08-03 — `rejected after completed deletion`: keep `msl_dynamics_rotate_euler` as the final hosted dynamics
  rotation owner and live `HSD_JObj::rotate` as the sole canonical result. Its temporary
  `PSMTXQuat` product is consumed immediately by the source-exact matrix-to-Euler sequence, which
  reads seven of twelve entries; the scratch matrix is overwritten before any later use. Compute
  exactly those seven entries from the same quaternion, PPC `fres` seed, Newton step, and ordered
  FMAs inside the dynamics owner, then run the unchanged `sqrtf`/`atan2f` conversion and publish
  the same Euler vector. Displace the general matrix call, two unused off-diagonal products, five
  unused matrix entries, all twelve stores, and the scratch argument at this one hosted call site.
  General `PSMTXQuat`, PPC/Wasm source paths, quaternion/Euler state, solver order, allocation, and
  all consumers remain unchanged. Add no cache, second representation, approximation, fallback,
  or compiler setting. Every form preserves both benchmark digests. The first direct form exposes
  an exact 84-byte positive-normal `fres` seed and removes the dead matrix entries, but grows the
  already-large solver by 162 bytes and loses both resident-256 reversals. Moving the seven-entry
  projection to one external leaf shrinks the solver by 254 bytes yet still loses both reversals
  (`37,744.1 -> 38,275.8` and `37,926.3 -> 38,139.4` cycles/frame). Moving the complete existing
  axis-angle/Euler conversion behind a coherent optimized leaf shrinks the solver by 990 bytes but
  loses more (`38,364.0 -> 38,909.3` and `38,502.2 -> 40,043.3`), confirming that its sequential
  state is scheduled better in the current inlined owner. Restore the original singular
  `PSMTXQuat` call, full scratch product, inlined conversion, private tables, and symbol surface
  completely. A worthwhile dynamics rewrite must break repeated exact angle evaluation, not prune
  a few matrix products around it.
- 2026-08-03 — `retained candidate, below checkpoint`: keep the live solver vectors and authored max/convergence/deviation
  thresholds as final owners. An ordinary-release census over 32,768 resident-512 frames observes
  335,471 solved links: max-angle comparisons execute 334,076 times, convergence comparisons
  38,954 times, and deviation comparisons 335,471 times. Max-angle is true 190,680 times;
  convergence is true only 563 times; deviation is true 162,180 times. These three source calls
  currently evaluate full exact `lbVector_Angle`; max and convergence discard every angle, while
  deviation discards it on 173,291 false branches. During existing dynamics initialization,
  compile the exact monotonic `acosf` comparison boundaries for all three immutable thresholds
  into otherwise unread hosted descriptor words. At runtime, compute each source-ordered clamped
  cosine once, compare its ordered float bits to the compiled boundary, and evaluate `acosf` only
  for a taken deviation that consumes the value. This displaces roughly 546,000 observed exact
  `acosf` calls without duplicating vector math. Preserve zero-length handling, dot/FMA and length
  order, clamp semantics, strict comparison boundaries, canonical Euler/dynamics state, PPC/Wasm
  behavior, allocation, and all consumers. This deliberately reassesses the rejected sparse
  max-angle-only cutoff as one coherent three-consumer deletion. Both production-prefix digests
  remain exact. Inlining the three cosine/cutoff paths grows the 7KB solver by 288 bytes and gives
  mixed 512 results. A shared external cosine leaf keeps the solver small but adds a second call
  per comparison and loses two resident-256 long pairs by 0.61--0.75%. The retained shape uses
  three ordinary runtime-math leaves with the cosine preparation expanded once inside each, so
  every replaced source angle remains one call and `lb_8001044C` grows only 32 bytes. Two 131,072-
  frame reversals improve resident 256 `37,000.3 -> 36,933.0` and `37,211.4 -> 37,062.3`
  cycles/frame (+0.18%/+0.40%); resident 512 improves `36,139.2 -> 35,906.2` and
  `36,035.9 -> 35,834.1` (+0.65%/+0.56%). Retain the three exact cutoffs and one-level leaves
  uncommitted while accumulating a qualifying checkpoint; retain no instrumentation or alternate
  compiler shape.
- 2026-08-03 — `retained candidate, below checkpoint`: keep `lbvector.c` as the final owner of its source-authored
  scalar-single fused subtract operations and preserve every operand, rounding, sign, and call-site
  order. The native release already expands `__fmadds` directly, but the equally exact six-byte
  `__fmsubs`/`__fnmsubs` leaves remain out of line; `lbVector_RotateAboutUnitAxis` alone pays four
  such calls around every dynamics rotation and spills its live vector/trig state across them.
  Expand only those two intrinsics inside `lbvector.c`, displacing their call/return and spill
  boundaries without changing polynomial, rotation, matrix, state, PPC/Wasm behavior, allocation,
  or any compiler setting. This is narrower than the rejected process-wide inline candidate.
  `lbVector_RotateAboutUnitAxis` shrinks from 749 to 451 bytes and contains no remaining intrinsic
  calls; both production-prefix digests stay exact. With the retained three-angle cutoff, a long
  pair against the frozen pre-cutoff angle candidate improves resident 256
  `38,173.9 -> 37,182.8` cycles/frame (+2.67%) and resident 512
  `36,193.3 -> 35,469.1` (+2.04%). Short enclosed screens agree at both sizes. Retain the two
  translation-unit-local intrinsic definitions uncommitted; do not broaden them process-wide.
- 2026-08-03 — `rejected`: apply the same source-exact local intrinsic boundary only to
  `lbcollision.c`, whose 11 fused-subtract sites include the measured hit/contact kernel. Preserve
  every expression and result while removing only the six-byte leaf calls and their live-state
  spills inside this translation unit. Do not broaden to matrix, fighter, or generic source; retain
  only if both digests and both resident sizes improve over the frozen lbvector-inline candidate.
  Both digests remain exact and resident 256 improves 0.50--0.72% in the short enclosed screen,
  but resident 512 loses 0.78% against the preceding control and 0.11% against the following
  control. The contact kernel does not execute densely enough at 512 to repay its expanded local
  code. Remove both macros completely and keep the singular shared intrinsic leaves for this
  translation unit.
- 2026-08-03 — `rejected`: apply the local fused-subtract boundary to the singular
  `sysdolphin/baselib/mtx.c` owner. `HSD_MtxInverse` is reached by hurt/contact transforms and
  currently contains 18 calls to the six-byte intrinsic leaves, with three more each in its
  inverse-concat and inverse-transpose siblings. Expand only those already-authored operations in
  this translation unit; preserve determinant/cofactor ordering, exact zeros, matrix state,
  PPC/Wasm behavior, allocation, and every caller. This tests a compact shared leaf rather than
  the rejected process-wide expansion; retain only with exact gains at both resident sizes over
  the frozen lbvector-inline candidate. `HSD_MtxInverse` shrinks from 805 to 591 bytes and all
  digests remain exact. Resident 256 improves 0.38--0.46% in the enclosed screen, but resident 512
  loses 0.84%/0.21%. The inverse family is not dense enough in the larger working set to repay its
  expanded cofactor code. Remove both local macros and retain the shared leaves.
- 2026-08-03 — `rejected`: test the same local intrinsic deletion only in `camera.c`.
  `Camera_80029CF8` is inside the sole dominant camera owner and pays four fused-subtract leaf
  calls per frustum solve. Expand those four source-authored operations in place while preserving
  every bound, smoothing input, exact zero, output, PPC/Wasm path, state, and allocation. Do not
  alter camera behavior or broaden to other files; compare exactly against the frozen
  lbvector-inline candidate at both resident sizes. The function shrinks by 55 bytes and the
  resident-256 digest remains exact, but the enclosed candidate loses 1.05% against the preceding
  control and 0.12% against the following control. Remove both macros without spending a 512
  screen on a candidate that already fails one required size.
- 2026-08-03 — `rejected`: expand `__fmsubs` only inside `lb_00B0.c`'s native batched
  Euler-to-quaternion publisher. Its hot output loop pays two leaf calls per Euler while retaining
  six trig values, two products, and the output pointer across each call. Preserve the exact two
  fused expressions, batch order, quaternion results, scalar/PPC/Wasm paths, state, and allocation;
  displace only these repeated call/spill boundaries. This does not alter the trig evaluator or
  broaden intrinsic expansion beyond the one native blend owner. Compare against the frozen
  lbvector-inline candidate at both resident sizes. The leaf shrinks from 483 to 385 bytes and
  both digests remain exact, but resident 256 changes sign around neutral and resident 512 loses
  0.57--0.97% in the enclosed screen. The batch already amortizes surrounding work, and expanding
  these two operations perturbs its compact output loop. Remove the local macro completely.
- 2026-08-03 — `rejected`: keep `lbVector_RotateAboutUnitAxis` as the final owner and preserve
  its two source range reductions plus identical fused quintic sine/cosine operation streams. The
  current scalar leaf evaluates the sine polynomial and the cosine polynomial serially even though
  their reduced inputs are independent. On native x86 FMA, pack the two already source-reduced
  floats into two lanes and execute the same three/four ordered multiplies, `fmsubs`, and `fmadds`
  per lane, then feed the unchanged scalar rotation. Displace only serial polynomial issue; retain
  scalar/PPC/Wasm/arm64 behavior, every range boundary, value, signed zero, state, and allocation.
  This differs from the rejected general masked trig batches: it has exactly two always-live lanes,
  one shared polynomial shape, no gather, and no temporary array. Require exact digests and gains
  at both resident sizes over the frozen lbvector-inline candidate. The native function shrinks
  another 32 bytes and the resident-256 digest remains exact, but the enclosed candidate loses
  5.14% against the preceding control and 1.35% against the following control. Two-lane packing,
  broadcast, and extraction cost more than the scalar compiler's scheduling. Remove the intrinsics
  and helper completely without spending a 512 screen on a failed required size.
- 2026-08-03 — `retained`: keep `acosf`/`asinf` as the final owners and preserve their exact PPC
  reciprocal-square-root estimate, three Newton steps, and `atanf` consumers. After their
  binary32 radicands pass `result > 0`, each value is necessarily finite, positive, and normal when
  promoted to binary64. Let these calls use the same estimate table and interpolation directly,
  displacing only `__frsqrte`'s unreachable zero, sign, NaN/infinity, and subnormal classification.
  General `__frsqrte`, vector normalization, all arithmetic/results, state, allocation, and
  non-native behavior remain unchanged. Add no approximation, cache, alternate output, or compiler
  attribute. An initial float-exponent compression failed both digest screens and was corrected to
  reproduce the estimate's fixed-point half-exponent boundary exactly before timing. Two clean
  reversals against the pre-change candidate preserve both digests and improve resident 256 by
  0.24%/0.61% and resident 512 by 0.27%/0.48%. Retain the shared table and 103-byte positive-float
  seed leaf.
- 2026-08-03 — `rejected`: keep `lbColl_80006E58` and `sqrtf_store` as the final owners of their
  exact contact distances. Each reciprocal-square-root seed is consumed only after its binary32
  squared distance passes `> 0`; use the same positive-normal float-to-double estimate primitive
  while preserving every double Newton operation, volatile float publication, comparison, and
  contact result. Displace only general seed classification and the caller's float-to-double
  promotion at those three sites. Add no collision admission, approximation, state, allocation,
  or non-native change; the complete boundary is the three seed calls and one shared exact helper.
  Both digests remain exact, but two reversals lose about 1.9--2.1% at resident 256
  (`37,396.1 -> 38,166.6` and `37,573.9 -> 38,275.5` cycles/frame) and are neutral/slightly worse
  at resident 512 (`36,106.0 -> 36,176.1` and `36,129.5 -> 36,148.4`). The collision seeds are too
  sparse for another out-of-line leaf and its layout displacement. Restore all three general calls
  and remove the float-to-double helper completely.
- 2026-08-03 — `rejected`: keep process-wide hosted `sqrtf` as the final owner and preserve its
  PPC seed, three fused double Newton steps, volatile float publication, and all exceptional
  behavior. Since its positive input began as binary32, test deriving the identical binary64 PPC
  seed through a positive-normal float classifier, falling back to general `__frsqrte` for zero,
  subnormal, infinity, and NaN. This displaces only the general estimator classification for
  ordinary positive inputs and adds no approximation, state, allocation, or compiler setting.
  Both benchmark digests remain exact, but resident-256 reversals change sign (+1.61% then -0.56%)
  and the completed resident-512 pair loses 0.12%. The extra leaf and its special-value admission
  do not produce a stable whole-runtime gain. Restore `sqrtf`'s direct general seed call and remove
  the double-result helper completely.
- 2026-08-03 — `retained`: keep `atanf` as the final owner and its existing reduction row,
  polynomial, corrections, and sign publication as the exact canonical result. In the middle
  magnitude range, the source exponent switch computes exactly the number of four ascending
  positive-float bit thresholds crossed. Compute that same index directly from the absolute bits,
  displacing only the switch and its duplicated index assignments. Preserve every threshold and
  strict boundary, reduction arithmetic, lookup, NaN/large/small path, state, allocation, and
  non-native source shape. Add no new table, approximation, alternate result, or compiler setting.
  The function shrinks from 709 to 329 bytes and both digests remain exact. Short reversals improve
  resident 256 by 1.26%/0.50% and are mixed at resident 512; longer 131,072-frame pairs confirm
  `37,251.0 -> 36,859.9` cycles/frame (+1.06%) at resident 256 and `35,522.1 -> 35,487.9`
  (+0.10%) at resident 512. Retain the direct four-threshold count. A follow-up skips the two
  exact-zero correction additions for the `-1` row, but grows the leaf by 24 bytes and loses both
  reversals: resident 256 changes `37,421.1 -> 37,898.7` and `37,513.5 -> 37,658.7`; resident 512
  changes `35,920.1 -> 36,129.2` and `35,975.4 -> 36,090.4`. Restore the unconditional adds; their
  straight-line scheduling is cheaper than the extra branch even when both values are zero.
  Next, test replacing the retained native four-scalar-compare/add classifier with one 128-bit
  integer threshold comparison and a four-bit population count. The four constants and inclusive
  boundaries are identical; this changes no reduction or arithmetic and adds only one 16-byte
  immutable vector, while non-native code keeps the retained scalar count. It shrinks `atanf` by
  another 32 bytes and remains exact, but the clean short reversal loses both sizes. A longer
  full-frequency pair improves resident 256 by 0.49% (`37,415.6 -> 37,234.9`) while regressing
  resident 512 by 1.21% (`36,099.7 -> 36,536.8`). Remove the vector constant/intrinsics and retain
  the four scalar comparisons, which behave better under the larger resident working set.
- 2026-08-03 — `diagnostic unavailable`: a short call-stack sample of the retained production
  binary requires Linux perf events, but this host has `perf_event_paranoid=4`. Do not trigger a
  special instrumentation rebuild as a substitute; continue from the retained bounded owner
  profile and direct binary inspection.
- 2026-08-03 — `rejected`: keep `atanf` as the final owner and preserve its retained exact
  reduction, polynomial, corrections, and result. The middle-range classifier currently evaluates
  all four ordered bit thresholds even though the answer is one of five monotonic bins. Test an
  exact two-level binary decision tree, displacing two integer comparisons per reached
  middle-range call without changing a threshold, boundary, table, arithmetic operation, state,
  allocation, non-native path, or compiler setting. Require exact digests and improvement at both
  resident sizes over the frozen lbvector-inline candidate; otherwise restore the four-count
  classifier completely. The decision tree grows the leaf from 329 to 342 bytes and preserves the
  resident-256 digest, but its enclosed screen loses 0.17% to the average of the surrounding
  controls. The branches cost more than the two displaced integer comparisons. Restore the
  straight-line four-count classifier without spending a resident-512 screen on a failed required
  size.
- 2026-08-03 — `rejected`: keep `lbVector_Sin`/`lbVector_Cos` as the final owners of their
  source-authored binary32 inputs, double-constant range reductions, and exact polynomial results.
  A binary32 input exceeds the source double `M_PI` boundary exactly when it exceeds the largest
  representable float below pi (`0x1.921fb4p+1F`), symmetrically for negative inputs. Replace only
  the promoted double comparisons with those equivalent float comparisons, retaining the original
  double add/subtract and float rounding when reduction is taken. This displaces four hot
  float-to-double conversions and double comparisons per unit-axis rotation without changing any
  result, branch boundary, canonical vector, state, allocation, PPC/Wasm path, or compiler setting.
  Require exact digests and gains at both resident sizes over the frozen lbvector-inline candidate.
  The compiler retains the conversions needed by the conditional double add/subtract, the unit-axis
  leaf does not shrink, and the resident-256 digest remains exact, but the enclosed screen loses
  0.52% against the average surrounding controls. Restore the double comparisons and remove the
  native threshold; this does not eliminate enough work to justify a resident-512 screen.
- 2026-08-03 — `rejected`: keep scalar `sinf`/`cosf` as the final owners of their source-exact
  reduction, small-angle classification, polynomial, and publication. Native release currently
  reaches the small-angle comparison through `fabsf__Ff` and then `__fabsf`, paying two call
  boundaries and spilling the reduced angle on every scalar trig evaluation even though the final
  leaf only clears the binary32 sign bit. Use the compiler's exact scalar absolute-value operation
  directly at these two comparisons on native, retaining the source call on PPC/Wasm. Displace
  only the wrapper/leaf calls and spills; preserve every bit result including zeros, infinities and
  NaN payloads, all arithmetic/order, state, allocation, and compiler settings. Require both exact
  digests and controlled gains at both resident sizes over the frozen lbvector-inline candidate.
  The same native-local boundary also expands the source-exact `fnmsubs` used by the scalar cosine
  small-angle result, displacing its remaining tail call while leaving the wide evaluator and every
  non-native path unchanged. The scalar leaves shrink by 53/43 bytes and both digests remain exact,
  but two long resident-256 reversals change from -0.19% to +0.13% and the resident-512 reversals
  are likewise unstable (+3.20% during a frequency ramp, then -0.78% after it settles). The direct
  instruction reduction does not survive whole-runtime measurement. Restore both source calls and
  remove the local macros rather than retain a neutral code divergence.
- 2026-08-03 — `rejected`: isolate the scalar trig owner's source-exact `fnmsubs` boundary after
  rejecting the combined absolute-value change. The retained 4,096-frame callgrind census records
  335,296 `__fnmsubs` calls from `msl_sincosf_many` alone, chiefly scalar tails around short exact
  wide batches, plus the scalar `cosf` small-angle call. Expand only these already-authored fused
  operations inside `trigf.c`; preserve signed-zero behavior, arguments, output, reduction,
  polynomials, wide lanes, state, allocation, non-native paths, and compiler settings. This is one
  dense measured call boundary, not the rejected process-wide expansion. Require exact digests and
  gains at both resident sizes over the frozen lbvector-inline candidate. The trig leaf shrinks by
  two bytes and scalar cosine by twenty; the resident-256 digest remains exact, but an enclosed long
  screen loses 0.71% to the average of its two controls after the preceding simple pairs track a
  strong frequency drift. The already-wide tail does not benefit from expanding this operation.
  Restore both shared intrinsic calls and remove the local macro without spending a resident-512
  screen on a failed required size.
- 2026-08-03 — `rejected`: keep the three immutable dynamics angle thresholds and their compiled
  exact boundary values as final owners. The retained cutoff compiler stores each boundary's
  monotonic ordered-float key, forcing every runtime comparison to reconstruct an ordered key with
  sign classification. Because each runtime cosine is clamped to finite `[-1, 1]` or remains NaN,
  store the float at that same boundary and use the equivalent native float comparisons directly;
  NaN remains false and the duplicate `-0/+0` acos result cannot split a boundary. Displace only
  key reconstruction and raw-word loads; preserve binary search, all vector/angle arithmetic,
  strictness, decisions, state bytes, allocation, non-native paths, and compiler settings. Require
  exact digests and gains at both resident sizes over the frozen lbvector-inline candidate. Passing
  the boundary as a float shrinks each leaf but moves the extra argument into the SIMD register
  class, perturbs the 7KB solver call sites, and repeatedly loses about 4% at resident 512 despite a
  resident-256 long win. Keep the existing integer argument ABI/storage and reinterpret the compiled
  boundary bits once inside each leaf before judging the comparison deletion itself. That form
  preserves the ABI and both digests and gives a +0.63% enclosed long resident-256 result, but two
  enclosed resident-512 screens lose 2.9% and 0.47%. The integer-key branches are evidently cheaper
  in the larger working set than the numeric comparison layout. Restore the ordered keys, loads,
  helpers, and initialization exactly.
- 2026-08-03 — `rejected`: keep `PSMTXRotAxisRad` as the final owner of axis normalization,
  scalar sine/cosine, and matrix publication. The headless camera calls this general primitive once
  every frame with the source-authored roll `-0.0F`; a retained callgrind census records 4,096 of
  4,230 calls from that one consumer. Handle exact positive/negative zero before scalar trig by
  publishing `sine = radians` and `cosine = 1.0F`, displacing only the redundant general range
  reduction/polynomials while preserving the input zero sign and all following normalization,
  matrix arithmetic/stores, state, allocation, nonzero behavior, and compiler settings. Add no
  camera-specific state or alternate matrix representation. Require exact digests and gains at both
  resident sizes over the frozen lbvector-inline candidate. The zero result is exact, but the
  general leaf grows by 37 bytes and two enclosed resident-256 screens lose 1.76% and 3.85%. The
  removed scalar trig is too cheap relative to the demanded normalization/matrix path, and the
  branch/layout expansion is harmful. Restore the unconditional source calls and original leaf
  completely without spending a resident-512 screen on a failed required size.
- 2026-08-03 — `rejected`: keep the platform PPC `frsqrte` tables as the singular immutable
  estimate owner and `acosf`/`asinf` as the only consumers of the retained positive-normal binary32
  seed. The current external 103-byte helper forces `acosf` to spill its radicand/input around about
  2.8 million calls in the 32,768-frame resident-512 census. Move that exact positive-normal body
  into the consuming runtime-math translation unit as a private inline evaluator, while the general
  platform estimator continues to use the same exported immutable tables. Displace the external
  helper symbol, call/return, and spills only; preserve bit construction, double-to-float rounding,
  Newton steps, results, exceptional admission, state, allocation, non-native paths, and compiler
  settings. Require exact digests and gains at both sizes over the frozen lbvector-inline candidate.
  The external helper disappears and inlined `acosf` grows by only 57 net bytes, but both enclosed
  resident-256 screens lose about 1.0% with the exact digest. The seed's integer/table dependency
  chain expands the already-hot angle leaf and does not schedule better than the spill/call boundary.
  Restore the private platform tables and singular external helper completely without spending a
  resident-512 screen on a failed required size.
- 2026-08-03 — `profile`: a temporary counter-only ordinary release run over 32,768 resident-512
  frames observes 4,620,300 `atanf` evaluations: 2,792,508 reached through `acosf`, 1,810,561
  through `atan2f`, 14,260 through `asinf`, and only 2,971 elsewhere. This census required one
  incremental 2.6-second build and one ordinary 2.5-second benchmark; all counters and reporting
  code were removed immediately. Exact angle evaluation is a material global owner, but its
  scalar polynomial dependency chain—not generic `sqrtf` classification—is the plausible cost.
- 2026-08-03 — `rejected`: expose the unchanged exact `atanf` body as a private inline evaluator
  for `acosf`, its 2.79-million-call consumer, while retaining the public scalar leaf for every
  other caller. This displaces only the call boundary and spills; reduction, lookup, polynomial,
  corrections, signs, and results are unchanged. The compiler does inline it, growing `acosf`
  from 201 to 565 bytes, and both digests remain exact. A longer full-frequency resident-256 pair
  loses 0.18% (`38,469.2 -> 38,539.6` cycles/frame), while the shorter resident-512 screen is
  neutral. The duplicated hot body increases instruction footprint without breaking the exact
  scalar dependency chain. Restore the singular external `atanf` owner completely.
- 2026-08-03 — `rejected by census`: an exact memo must skip enough full scalar polynomial chains
  to repay hashing, lookup, and mutable cache footprint. A one-run direct-mapped census over the
  same 32,768 resident-512 workload finds only 11.1%, 14.2%, 18.3%, and 23.8% `atanf` argument
  reuse at 256/1,024/4,096/16,384 entries. Separating its dominant owners does not reveal a hidden
  high-reuse stream: `acosf` input reuse is 8.3%/12.2%/18.4%, and paired `atan2f` input reuse is
  15.8%/18.4%/20.0% at 256/1,024/4,096 entries. This is far below the prior 95.3%-hit immutable
  quaternion memo that already regressed, while these caches would be larger mutable global state.
  Remove all census arrays, counters, hashing, and reporting; do not implement an angle memo.
- 2026-08-03 — `rejected`: keep `Fighter_Spaghetti_8006AD10` as the final input owner and its
  existing `Fighter::input` fields as canonical state. Native/Wasm `SET_STICKS` call sites assign
  the same x field and then y field directly instead of materializing and spilling two pointers to
  those fields in the source-profiled function. Preserve argument evaluation, assignment order,
  values, callback order, PPC source shape, state, allocation, and every input classification.
  Displace only the address temporaries at the eight existing call sites; add no helper, cache,
  input representation, approximation, or compiler setting. The function shrinks by 238 bytes;
  a longer pair is +0.43% at resident 256 and effectively neutral (-0.05%) at resident 512, so keep
  it provisional while completing this owner. Replacing sixteen source increment/clamp sequences
  with an exact single assignment shrinks another 98 bytes but loses both reversals, including
  0.74--1.60% at resident 512. Restore every counter sequence and retain no timer macro. The
  stick-only longer result is +0.43% at resident 256 but -0.05% at resident 512, after short
  reversals change sign. Restore the source pointer macro as well; code-size reduction alone is not
  a stable contract gain.
- 2026-08-03 — `rejected`: keep `HSD_GObj::user_data` as the sole canonical fighter-owner field.
  Within `fighter.c`'s major input, physics, map, hit, and maintenance phases, consume that field
  directly instead of calling the source-profiled four-instruction `HSD_GObjGetUserData` leaf at
  every `GET_FIGHTER`. Preserve the pointer, callback order, all state, external accessor semantics,
  PPC source shape, allocation, and consumers. Displace only the accessor calls in this translation
  unit; add no cached pointer, layout change, alternate object representation, or compiler setting.
  The major phase functions each shrink by four bytes and both digests remain exact, but resident
  256 loses both reversals (`37,617.0 -> 37,782.7` and `37,598.7 -> 37,690.8` cycles/frame), while
  resident 512 changes sign. Remove the local override and do not broaden it to the shared header.
- 2026-08-03 — `rejected`: keep `lb_8001044C` as the final fighter-dynamics owner and its local
  normalized directions plus the source `DynamicsData` chain as the only canonical state. The
  solver's exact source-ordered angle products are consumed only by its stiffness, constraint,
  convergence, deviation, and angular-velocity decisions. Test retaining each immutable normalized
  direction's length across its two angle uses, displacing three redundant `sqrtf` evaluations per
  solved link while recomputing every mutable link length at the original use. Preserve vector
  values, dot/FMA order, length-product operand order, clamps, `acosf`, decisions, public state,
  allocation, PPC/Wasm source paths, and all consumers. The complete deletion boundary is the six
  hosted angle calls plus the three local cached scalars; retain no general vector API, persistent
  cache, approximation, alternate solver, or compatibility path. The generic exact helper loses
  at both resident sizes (`37,715.7 -> 37,926.5` cycles/frame at resident 256 and
  `36,195.1 -> 36,307.9` at resident 512). Moving the remaining mutable-vector square root into
  specialized exact leaf helpers makes the 256 loss larger (`37,546.3 -> 38,129.9`). The solver
  grows by 209--264 bytes and spills the three cached scalars, while stiffness, convergence,
  max-angle, and final-air paths do not all execute on every link; the presumed three-square-root
  saving is therefore neither unconditional nor large enough to repay integration cost. Remove
  the helpers, declarations, cached scalars, and call-site split completely.
- 2026-08-03 — `rejected`: keep `lb_8001044C` and its source-local vectors as the final dynamics
  owner and canonical state. Its twelve reached cross-product/normalization pairs currently cross
  two external API boundaries and round-trip the same result through memory. Replace each adjacent
  pair with the existing source-owned `lbVector_CrossprodNormalized`, which performs the identical
  `PSVECCrossProduct` followed by the identical `lbVector_Normalize` in one leaf. Preserve argument
  order, float operations, branch order, result storage, downstream rotation, PPC/Wasm behavior,
  state, and allocation. The deletion boundary is only the twelve redundant normalize calls and
  their caller-side reload boundary; add no new helper, state, approximation, or solver split.
  Both digests remain exact and the solver shrinks by 208 bytes, but the combined leaf loses
  `37,590.6 -> 38,357.7` cycles/frame at resident 256 and `36,027.6 -> 36,625.8` at resident 512.
  Keeping the result pointer live across the internal cross-product call adds callee save/restore
  and a larger combined leaf; the existing two tiny leaves schedule better despite the extra call.
  Restore all twelve source pairs completely.
- 2026-08-03 — `diagnostic unavailable`: count the actual per-link admissions for the dynamics angle comparisons
  before considering a comparison-only replacement. Instrument only the opt-in subsystem-profile
  build; production code and output remain untouched. Distinguish stiffness, max-angle,
  convergence, and final airborne executions so any redundant-math ceiling is based on paths that
  execute twice for the same immutable direction rather than static source occurrences. The
  special profile target invalidated its compile-flag stamp and started rebuilding the full source
  closure, exceeding the bounded inner-loop budget. Stop that route and remove all counters/report
  hooks without drawing an ownership inference; do not repeat a full profiler build for this leaf.
- 2026-08-03 — `rejected`: keep each `DynamicsData` link and its authored `unk_88` max angle as
  the final owner and canonical state. The max-angle branch currently evaluates an exact `acosf`
  only to compare its result with that immutable threshold; the angle value is never consumed.
  During existing dynamics initialization, compile the exact monotonic `acosf` comparison boundary
  into the otherwise unused hosted `unk_94` word by binary-searching every possible clamped float
  input. At runtime, preserve the source length, dot/FMA, division, clamp, zero-length, NaN, and
  comparison semantics while replacing only that `acosf` with an exact cutoff comparison. PPC
  retains source behavior and layout. The deletion boundary is one comparison-only angle call and
  one already-present unused word per link; add no approximate math, persistent side table,
  alternate solver, allocation, or fallback dispatch. The cutoff preserves both benchmark digests,
  including source zero-length and NaN behavior, but loses `37,560.3 -> 37,802.8` cycles/frame at
  resident 256 and `35,984.3 -> 36,130.2` at resident 512. The max-angle admission is too sparse
  for deleting its `acosf` to repay the additional cutoff load and larger solver/control layout.
  Remove the cutoff compiler, unused-word overlay, helper, and call-site split completely.
- 2026-08-03 — `experiment`: keep the caller-provided batch mask as the sole admission owner and
  preserve every masked/unmasked loop, order, API result, and output. The release build currently
  leaves the tiny `selected` predicate out of line, so the ordinary unmasked benchmark pays two
  calls per Match in step validation/execution and one each in observation and terminal publication.
  Mark the existing leaf inline so those loops consume the same null-mask/byte test directly.
  Displace only call/return overhead; add no alternate batch path, state, allocation, assumption
  about masks, compiler flag, or changed error checking.
  Two 32,768-frame reversals preserve both digests and consistently improve throughput: resident
  256 pairs change `37,913.8 -> 37,887.0` and `38,090.4 -> 37,942.1` cycles/frame; resident 512
  changes `36,251.2 -> 36,052.6` and `36,442.6 -> 36,162.7`. Retain the one-keyword deletion.
  A null-mask outer fast path only in step deletes both remaining per-Match mask branches but grows
  the owner by 131 bytes. Despite exact digests, the resulting complete candidate loses to the
  frozen control at both sizes (`37,731.5 -> 38,221.2` cycles/frame at resident 256 and
  `36,094.9 -> 36,221.3` at resident 512), whereas the inline-only form won both reversals. Remove
  the duplicated loops and retain only the inline predicate.
- 2026-08-03 — `rejected`: keep `Player_8003248C` and `Fighter::x1A88.xC` as the sole canonical
  CPU-input ownership state. Hosted fighter maintenance, input, and hurt-publication gates call the
  tiny O0 source wrapper several times per Fighter frame. Consume the same player-kind lookup and
  state comparison through one local inline predicate at the three fighter.c call sites, while PPC
  and other source consumers retain `ftCo_800A2040`. Displace only the wrapper's stack/call shell;
  preserve the player query, order, decisions, state, and outputs, with no cached result, new state,
  supported-domain proxy, compiler attribute, or changed CPU behavior. The local function is not
  inlined by fighter.c's exact source profile; explicit source expansion preserves both digests but
  is noise-sized/mixed against the selected-inline candidate. Resident 256 changes
  `37,743.1 -> 37,669.1` then `37,669.0 -> 37,778.7` cycles/frame, while resident 512 improves only
  `36,059.9 -> 36,029.6` and `36,061.0 -> 36,002.9`. Restore all three source calls rather than
  duplicate the predicate in O0 owners.
- 2026-08-03 — `rejected`: keep `msl_core_batch_write_observation` as the final owner of the exact
  980-byte public wire row; caller memory remains the sole canonical output consumed after return.
  For the normal unmasked contiguous x86 batch, test building one row in a reused hot stack object
  and publishing it with aligned non-temporal stores, displacing only cache-allocating output
  stores that otherwise evict simulation state before the next tick. Masked/strided batches,
  singular observation, ARM/Wasm/PPC, bytes, ordering, API, allocation, and state remain unchanged.
  The deletion boundary is this one contiguous batch path. Digests remain exact, but the stack-row
  copy costs more than the avoided write allocation: `37,548.5 -> 37,980.8` cycles/frame at
  resident 256 and `36,008.7 -> 36,333.4` at resident 512. Remove the helper and fast path fully.
- 2026-08-03 — `rejected`: keep the sealed Match bump arena as the sole canonical storage owner and
  preserve every allocation, offset, relocation, and consumer. Current supported-domain census
  reaches 973,328 bytes after retained pool/pose compaction, but the native batch still spaces
  lanes by the obsolete 3 MiB construction ceiling. Test a 1 MiB hard ceiling, deleting only the
  unused virtual gap between arenas; add no padding, huge-page advice, allocation, split mapping,
  or state. The boundary is the one native capacity constant. Digests remain exact and the
  benchmark constructs successfully, but the power-of-two stride regresses both short reversals:
  `37,551.5 -> 37,836.6` cycles/frame at resident 256 and `35,897.0 -> 36,235.8` at resident 512.
  The unused virtual range is not demanded memory and its spacing is beneficial. Restore 3 MiB.
- 2026-08-03 — `rejected`: keep `HSD_MtxSRTConcatTrig` as the final owner and each JObj `mtx` as the
  sole canonical world transform consumed by ECB and other matrix readers. Test replacing only
  the x86 FMA kernel's three repeated 128-bit parent-row products with one exact 512-bit product
  over all 12 output lanes. Local SRT construction, parent-product FMA order, translation-lane
  operation, output layout, scalar/non-AVX-512 path, state, and allocation remain unchanged. The
  deletion boundary is the old three-row SIMD loop; retain only with exact digests and controlled
  improvement at both resident sizes. The first all-row AVX-512 form is exact but loses the short
  reversal (`37,768.5 -> 37,888.8` cycles/frame at resident 256 and `35,926.6 -> 37,047.7` at
  resident 512). Sustained ZMM execution and cross-lane permutes overwhelm the row-loop deletion.
  Remove that form and screen only a two-row AVX2 product plus the proven third-row SSE product,
  which avoids the wide-vector penalty while sharing half the repeated parent-product setup. That
  form is exact but neutral/mixed: `37,593.1 -> 37,677.0` cycles/frame at resident 256 and
  `35,989.7 -> 35,960.7` at resident 512. Permuting repeated parent coefficients costs the same as
  their cheap row-local broadcasts. Restore the original three-row kernel completely.
- 2026-08-03 — `rejected`: keep `HSD_JObjSetFlags`/`HSD_JObjClearFlags` as the final owners of JObj
  flag mutation and keep `HSD_JObj::flags` as the sole canonical state. Their callers and dirty
  descendants remain the only consumers. On native builds, test displacing only the duplicated
  inline recursive matrix-dirty body with the existing exact out-of-line
  `(HSD_JObjSetMtxDirty)` owner; preserve the source-shaped inline path for PPC. The deletion
  boundary is exactly the two native calls, with no new state, admission, allocation, or fallback.
  Digests remain exact, but the first 32,768-frame reversed screen is decisively slower:
  `37,522.0 -> 39,000.0` cycles/frame at resident 256 and `36,120.1 -> 37,334.6` at resident 512.
  The extra hot call/return costs much more than the duplicated code footprint. Restore both
  inlined source paths completely.
- 2026-08-03 — `rejected`: reuse only a true first-pass `Camera_8002928C` result inside the same
  `Camera_8002958C` call while preserving every false/countdown call. Subject state, bounds, and both
  digests remain exact, but two short reversals consistently regress: about 0.9% at resident-256
  (`37,532.8/37,579.4` control versus `37,883.1/37,908.0` candidate) and 0.3% at resident-512
  (`36,023.2 -> 36,128.8` in the clean reversal). The bounded stack result stream and larger camera
  owner cost more than the small predicate. Restore both source traversals and all calls.
- 2026-08-03 — `rejected`: make native `lbCopyJObjSRT` use the existing exact hosted pose-flag
  transition from the wide blend kernels. Canonical SRT, flags, propagation, and both digests remain
  exact, but two 32,768-frame reversals lose at both sizes: the second pairs change
  `37,567.1 -> 37,668.8` cycles/frame at resident-256 and `35,992.9 -> 36,067.8` at resident-512.
  Expanding the copy leaf from 115 to 155 bytes and adding its hosted dirty admission costs more
  than the hot generic Get/Set/Clear calls. Restore the source copy and private blend transition.
- 2026-08-03 — `rejected`: collapse headless item render-matrix publication from three transparency-
  pass DFS walks to one mask-carrying DFS. Final JObj matrices, dirty/visibility/transparency flags,
  and both short digests remain exact, with no added state or allocation. The complete 0.67% bucket
  was the ceiling, but the candidate regresses resident-256 by 0.86% (`37,576.0 -> 37,899.9`
  cycles/frame) and is neutral/slightly worse at resident-512 (`35,950.6 -> 35,975.4`). The active
  item trees are small and pass-major traversal/order is already favorable. Restore all three
  source display passes; retain no combined traversal.
- 2026-08-03 — `audit`: current callback attribution on the balanced 32,768-frame resident-512
  profile assigns 1.97% of the complete contract to `ftCo_Guard_Anim` and 1.14% to
  `ftCo_GuardOn_Anim`; the next targets are Wait at 0.75% and Fall at 0.65%. Remove the callback
  timer completely. Any action-animation work should stay inside Guard's existing two ordered pose
  blends and their immediate traversal/publication boundary; broad action dispatch work cannot
  materially advance the target.
- 2026-08-03 — `audit`: measure bit-identical overwrite frequency in the dominant compiled-pose
  masks before proposing any unchanged-value cull. Final state would remain canonical JObj SRT and
  dirty flags; the only possible deletion is a store of the same bit pattern while the joint is
  already dirty. Do not add per-sample metadata or invalidation machinery unless the existing
  joint values themselves prove a large, cheap admission population.
- 2026-08-03 — `rejected before implementation`: the 32,768-frame resident-512 census finds
  1,516,280 bit-identical writes among 8,449,532 dominant rotation-only publications (18.0%) and
  399,554 among 1,500,808 rotation-plus-translation publications (26.6%); all are already dirty.
  The two exact `memcmp` admissions alone raise measured pose cost by about 11.7M cycles. Hot
  same-value stores are cheaper than testing every publication, while precomputed change metadata
  would require invalidation proof for external pose writers. Remove all census code and do not
  build that table/branch.
- 2026-08-03 — `rejected`: reuse the existing private guard interpolation skeleton only when its two
  canonical directional inputs (`guard.x4` blend magnitude and `guard.x8` frame) remain bit-equal
  across `ftCo_80091BC4` and no ordinary motion-transition blend is active. Final owner is still
  `ftCo_80091E78`; canonical mutable state remains those guard fields plus the one source
  `x8AC_animSkeleton`. Consumers still copy/blend that skeleton into the main pose every callback.
  Displace only reattachment, static reset, same-frame animation, and authored blend that would
  reconstruct the already-live private skeleton. A nonzero `x8A4_animBlendFrames` always takes the
  source path because the preceding main animation phase can mutate the skeleton. Add no cache,
  marker, table, allocation, alternate pose, or action dispatch. A 32,768-frame resident-512
  census found only 269 eligible reuses among 11,320 private-pose rebuilds (2.38%). Controlled
  resident-256 measurements regressed by 2.39% and 1.14%; the broad admission cost cannot be
  repaid by this population. The implementation and census are removed.
- 2026-08-03 — `rejected before implementation`: specialize the dominant air/ground collision
  drivers for their one-pass case. The measured 1.015 average loop count does not represent
  interpolation or setup that can be bypassed: the first body is the demanded collision query,
  while the repeat machinery is only the final dirty/squeeze flag comparison. A fast path would
  duplicate the large driver to remove two comparisons, so make no source change.
- 2026-08-03 — `rejected`: compute `Camera_8002958C`'s stable stage clipping bounds once per subject-
  bounds traversal. Final state remains the source `CameraTransformState`; the subject list and
  stage state remain the only inputs, and the existing camera pipeline remains the only consumer.
  Displace only repeated `Ground_801C4368` and stage-bound accessor calls made by each of five
  point classifications per subject and again during boundary clamping. Preserve point order,
  comparison/clamp order, and float operations exactly; add no cache, Match state, approximation,
  alternate camera, or changed offline-copy scheduling. Both short digests remain exact and the
  hot function shrinks from 1,618 to 1,439 bytes, but short screens lose about 0.6% at both sizes;
  a longer resident-512 pair is neutral/slightly worse at 35,416.5 versus 35,402.1 cycles/frame.
  The stable accessors are already cheap and predictable, so remove the specialization completely.
- 2026-08-03 — `rejected`: finish each AVX-512 fused ordinary world-matrix row with one masked vector
  add of the parent translation instead of shuffling the translation lane to scalar, adding, and
  inserting it twice. Final owner remains `mtx_srt_concat_trig`; canonical JObj matrices, exact
  FMA order, inputs, outputs, and all consumers are unchanged. The mask leaves the three rotation
  lanes bit-identical and updates only lane three. Non-AVX-512 native, Wasm, and PPC retain the
  existing implementation. Both short digests remain exact and the function shrinks by 72 bytes,
  but resident-512 controlled reversals change sign (+0.33%, then -0.08%) and the short screen
  loses. Mask-register setup and merge traffic absorb the smaller instruction stream; remove it.
- 2026-08-03 — `rejected`: cache `mpLib_8004ED5C`'s exact extended collision-line endpoints for the
  current collision epoch. Profiling attributed 525,304 calls and 21.40M cycles (1.39% of the
  diagnostic contract), establishing the entire possible ceiling. Storing the product inside
  `CollLine` preserved exact output but enlarged every line from 16 to 40 bytes and regressed both
  resident sizes by about 1.5--1.8%. A separate indexed cache also preserved exact output but
  regressed short screens by 1.68% at resident-256 and 0.47% at resident-512. Cache lookup and state
  traffic cost more than the source arithmetic, including for the repeated population. Remove both
  cache forms and all profiling; retain canonical vertices/topology and on-demand construction.

# Correctness lead — stale-rationale audit of the dolphin-ulp family: six re-owned (2026-09-03, landed)

## Objective

Execute the standing lead from the HummingDismalCat pass: audit every `dolphin-ulp-*`
classification (37 entries) by diffing its birth-commit `first_mismatch_frame`/rationale
against the current record, and re-triage the current first fork of any entry whose
fingerprint moved without its prose being re-owned.

## What the archaeology showed

- Per-entry ledger timelines over all 71 commits of `melee_core_classifications.json`
  found 13 entries whose first mismatch frame moved since birth; two more (Cockroach,
  Aardvark) drifted on mismatch-count-only re-records. Four of the movers (dk 152714,
  puff rollout_ends_ys, samus master-samus, peach CapitalPristineTarsier) had been properly
  re-owned; the rest had mechanically re-recorded fingerprints under untouched or
  append-only prose.
- **Six entries were misattributed** (defect class in id/prose wrong for the current rows;
  each current fork verified by a fresh strict native run plus peppi lane dumps):
  - marios `medium-fox-2026-04` (`dolphin-ulp-dynamic-bone-capsule-gate`, rationale
    byte-identical since birth): the frame-910 capsule fork was retired by the spline
    path-evaluator fusing (`58f3f9e3`, 473 rows -> 1). What remains is one self-healing
    1-ULP pos_x row at 8813 — Dr. Mario falling down Yoshi's right side wall onto the
    absolute wall clamp. Re-owned as `dolphin-ulp-motion-profile` (wall-hug family).
  - samus `fox-d18-2026-04` (`dolphin-ulp-contact-gate`): the prose's 81-row damage-state
    flip at 9075 was retired by the smoke-puff draw consumption (`2c0c510e`, 223 rows -> 1).
    What remains is one 1-ULP pos_y row at 7389, the frame after a zair wall-hang release
    under Battlefield's ledge. Re-owned as `dolphin-ulp-motion-profile`.
  - peach `ExpertWorthlessFinch` (`dolphin-ulp-item-motion-profile`): all five remaining
    rows are whole-point (+1.0 exactly) percent rows — the hosted magnifier ticks one frame
    before the recording at three offscreen episodes, resyncing at the recorded tick. Zero
    item or motion lanes remain. Re-owned as `vanilla-magnifier-render-schedule`.
  - peach `TameEmbellishedSparrow` (`dolphin-ulp-item-and-motion-profile`, "discrete
    gameplay remains source-driven"): the current 2,285-row tail from 13507 is one
    unreturned magnifier tick on Marth (deep offscreen at (-112, -12); the recording never
    ticks — Marth regains the stage first) whose +1% persists to game end with full
    consequence: 0.54% knockback deltas, a changed landing at 13694, and a final-frame
    is_dead/stocks flip. Re-owned as `vanilla-magnifier-render-schedule`; the exact
    fingerprint pins the consequence tail.
  - peach `DisgustingLivelyRaven`: first row is a one-frame-early magnifier tick (Peach
    offscreen at (117, 16) on Yoshi's), then 23 ULP item.pos rows. Re-owned as
    `mixed-dolphin-ulp-render-profile`.
  - peach `CandidThankfulCockroach` (`dolphin-ulp-item-motion-profile`): current rows are
    80 airborne-Jigglypuff root-position ULPs plus two magnifier ticks on Peach — no item
    lane at all. Re-owned as `mixed-dolphin-ulp-render-profile`; PPC snapshot paragraph
    kept verbatim (still accurate).
- **Three carried provably stale prose under the right class** (fixed in place): peach
  `SmugConfusedTermite` ("25 rows" -> the actual 16), sheik `GlaringRosyAlpaca` and
  `WavyRundownAardvark` (core sentence still claimed thrown-needle one-byte samples — zero
  needle rows remain — and "PPC and native agree bit-for-bit" — their recorded PPC
  fingerprints differ from native).
- The magnifier identifications are lane-dump-backed: at every whole-point percent row the
  affected fighter is far offscreen and the recording ticks +1.0 exactly one frame after
  the hosted run (Finch 7208/7268/11174, Raven 7474, Cockroach 6415/10445), matching the
  `vanilla-magnifier-render-schedule` unrecorded-VI/render-boundary mechanism.

## Final boundary

- Prose/id/owner/sources re-owned on nine entries in
  `replays/suites/melee_core_classifications.json`; every `expected` block (fingerprints,
  native and PPC) untouched, no suite membership, lock, or gameplay code changed. Ids used
  are existing families only (`dolphin-ulp-motion-profile`,
  `vanilla-magnifier-render-schedule`, `mixed-dolphin-ulp-render-profile`).

## Evidence and acceptance

- Native aggregate after re-owning: 433 pass / 58 classified / 0 fail / 0 error (unchanged
  from the HummingDismalCat baseline; no outcome moved). Affected suites native: peach
  9 pass / 10 classified, marios 44/6, samus 23/2, sheik 16/4, all 0 fail. PPC
  (`--timeout 180`, 8 workers) on the same four suites: parity, 0 fail.
- Triage evidence: per-commit ledger timelines (scratchpad git archaeology), strict
  single-replay native runs for all eight suspect replays, and peppi lane dumps around
  every claimed magnifier row and both single-row residuals.
- The pikachu `master-master` entry records first_mismatch_frame -24 (countdown phase);
  it is birth-stable with a fresh rationale and was left alone, noted here as the one
  oddity outside the audit's mover criterion.

## Log

- 2026-09-03 — `retained`: birth-vs-current audit of all 37 `dolphin-ulp-*` entries, six
  re-owns + three prose corrections, suite + aggregate verification. The audit criterion
  that caught them: fingerprint moved (or count moved) while the rationale stayed
  byte-identical or append-only. Every remaining `dolphin-ulp-*` entry now has prose that
  matches its current first fork.

# Correctness lead — the peach HummingDismalCat residual is an unfrozen Stadium capture (2026-09-03, landed)

## Objective

Re-triage the largest classified residual whose rationale had no retail evidence:
`peach/HummingDismalCat` under `unmodeled-presentation-rng-stream-phase` (72,738 rows from
4177, 2,504 mismatched frames with no recovery). The ledgered story — a DamageFlyRoll
`HSD_Randf` landing one stream position off against an excluded presentation draw — was
written for a different fork and no longer described the fingerprint.

## What the traces showed

- **The rationale was stale.** At the classification's birth (peach packet, `cc1996be`) the
  first mismatch was frame 3721 and really was the DamageFlyRoll gate. `2c0c510e` (smoke-puff
  model-start draw consumption in efSync_Spawn) closed that fork and re-recorded this replay's
  fingerprint as today's `6a532077b54167b0` @4177 — the text was never re-owned.
- **The current fork is not RNG.** A hosted RNG-draw trace (temporary env-gated logging in
  `random.c`, callers resolved with addr2line) shows the fork frame consuming only the
  Stadium fly-by `randi(200)`, the `ftCo_8009F834` efAsync jitter triple, and modeled effect
  draws — no gameplay-gated draw at all. Both runs land the identical turnip hit on P1 Fox at
  4177 (`speed_x_attack[0]` agrees to 1 ULP at 4188); the fork is the post-hit floor resolve:
  retail freezes Fox at (25.0000, 1.2501) on ground 36, hosted at (24.4517, 0.0001) on
  ground 34 — and no line in the frozen Stadium set passes through (25, 1.25).
- **The recording's Stadium transforms.** The capture carries 13 `stadium_transformation`
  events: type 6 phases at Slippi 3709/3710/3711/4013/4194/4315/4316 and type 5 (revert) at
  5722..6328. The first mismatch (4177) sits between the phase-4 and phase-5 events, the
  earliest recorded standing row off the frozen height set is 4187 (P1, ground 36,
  y = 1.2501 — the transformed floor Fox lands on out of hitlag), and the recording stands
  fighters on grounds 41..50 at heights the frozen line set does not contain, all inside the
  4177..6680 mismatch window. The hosted owner deliberately disables the transformation
  loader (`grStadium_801D1518`), so divergence at first transformed-terrain contact is the
  designed behavior for an out-of-domain capture, not a defect.
- **The capture is unique.** A corpus scan of every suite entry on stage 3 finds
  HummingDismalCat is the only one recording transformation events (older captures predate
  the event lane entirely; every one of those either passes exactly or carries an unrelated
  classification).

## Final boundary

- The capture is outside the supported domain (RL 1.0 covers frozen Pokemon Stadium; later
  suites event-verify frozen at admission and would have rejected it). Retired rather than
  reclassified: removed from `replays/suites/peach.json` (20 -> 19; notes updated), its
  classification (59 -> 58) and output lock (492 -> 491) deleted, the `.slpz` deleted, and
  the two suite-count pins in `tests/` moved 492 -> 491. No gameplay code changed.

## Evidence and acceptance

- Native aggregate after retirement: **433 pass / 58 classified / 0 fail / 0 error** at
  4,869,495 frames — exactly the previous 4,876,298 minus this replay's 6,803, with no other
  outcome moved. peach suite native and PPC (`--timeout 180`, 8 workers): 9 pass /
  10 classified / 0 fail on both backends.
- Gates on this host: `source-check`, `format-check`, `native-smoke`, pytest 47 passed. The
  temporary `random.c` trace instrumentation was reverted before any result was recorded.
- Method note: no Dolphin probe was needed — the recording itself declares the
  transformation. Validator mismatch frames are Slippi frame ids (the recorded P1 row at
  Slippi 4177 is bit-identical to the reported expected row); the hosted trace stamp
  `gm_8016AEDC()` remains Slippi id + 123.

## Log

- 2026-09-03 — `retained`: re-triage, corpus transformation scan
  (`reports/triage/hdc_rng_native2.txt`, peppi lane dumps), suite/classification/lock
  retirement, test-count pins, aggregate + PPC parity, gates. The sibling
  `unmodeled-presentation-rng-stream-phase` entry (ness `2026-06`) is probe-backed and
  untouched. Standing correctness lead from this pass: classification rationales are not
  re-verified when re-records move a fingerprint — the remaining `dolphin-ulp-*` family
  (34 entries) carries the same risk and deserves the same birth-commit-vs-current-fork
  audit before any is trusted as evidence.

# Correctness lead — the two Link open port defects (2026-08-30, landed)

## Objective

Close the two classifications carried as explicit open port defects:
`link-boomerang-deflect-fork` (links `slippi-games-2025-05` @4185, 202 rows) and
`thrown-release-pose-offset` (links `slippi-2025-08` @3503, 17 rows). Neither was what its
ledger text said.

## What the traces showed

- **Boomerang:** there is no Yoshi's Story line at (-43.4, 10.7). The recording's item freezes
  for ten frames with its timer held at 246 while P1 Marth goes 369 -> 370
  (`ftMs_MS_SpecialLw` -> `SpecialLwHit`): the boomerang hit Marth's Counter. A hosted trace
  proved the hit is registered (`ftColl_80077688` runs on the frame, `xDCE_flag.b4/b5` set,
  `Item_80269DC8` dispatches the boomerang's `it_802A2320` mirror against `xC58`), and the
  hosted post-mirror velocity (-1.711, -0.523) is exactly the recording's pre-mirror velocity
  (-1.7866, -0.0937) reflected across n = (-0.1736, 0.9848); the recording's (-1.6468, 0.6991)
  is the reflection across (+0.1736, 0.9848). `p_ftCommonData->x2D0` is 80, so the sign of
  `xC58.x` is the whole defect. The DOL at 0x80077818..0x8007782C (`fcmpo pos->x, 0; cror
  eq,gt,eq; bne -> fneg`) keeps +cos on the `pos->x >= 0` side; `refs/melee`'s
  `ftColl_80077688` has the two arms swapped, so every projectile that hits a counter shield
  was mirrored into the counter's side.
- **Throw release:** P1 Young Link stands on a Fountain platform rising 0.1125/frame. The
  release vec from `ftCo_800DDDE4` lands below the platform (y -2.12) and `mpColl_800471F8`
  snaps the victim onto the line — hosted at the frame's new height (6.3376), retail at the
  previous one (6.2251 = 8.048 - 1.823). Retail's platform update is `grIzumi_801CC358`,
  registered by `grIzumi_801CBCE8` at priority 4 (after `Fighter_8006A360` at 1, before
  `Fighter_procMap` at 6), and Slippi's `SendFountainInfo.asm` hooks 0x801CC998 inside it.
  The hosted runtime published the recorded height in `apply_replay_stage_events` at frame
  start and the priority-4 callback's replay branch was a no-op (`was_applied`), so every
  mid-frame floor resolve in the fighter anim phase saw the platform one phase early.

## Final boundary

- `src/melee/ft/ftcoll.c::ftColl_80077688`: hosted branch with the DOL's arm order
  (`canonical:counter-shield-item-deflect-normal-sign`, upstream-candidate).
- `src/runtime/scalar.c::apply_replay_stage_events` no longer publishes Fountain heights;
  `src/melee/gr/grizumi.c::grIzumi_801CC358`'s replay branch owns the publication at retail's
  scheduler point; `msl_slippi_fod_platform_{was_applied,mark_applied}` and
  `MslCoreSlippiState::fod_platform_applied_mask` are deleted
  (`canonical:fountain-platform-replay-publication-phase`, hosted-replay-policy). Dream Land's
  Whispy direction publication is untouched.

## Evidence and acceptance

- Native aggregate at this change: **429 pass / 59 classified / 0 fail / 0 error**
  (4,876,298 frames). Four entries turned exact and were retired (63 -> 59):
  `links/slippi-games-2025-05` 5,275 -> 5,375/5,375; `links/slippi-2025-08` 7,106 ->
  7,123/7,123; `ganon/24891` (35-row FoD throw-release `dolphin-ulp-motion-profile`) 11,140/
  11,140; `yoshi/slippi-2025-02` (`fod-release-floor-pick`, 358 rows) 12,828/12,828. No other
  replay's outcome or lock moved; the four locks were re-recorded from the aggregate run.
- The `fod-release-floor-pick` text had already named this seed ("same mp floor-line-pick seed
  as the probed dk auto-dk-2025-04 and ganon FoD throw-release entries"); dk `auto-dk-2025-04`
  is the Samus wall-hug ULP family, not a release resolve, and is unchanged.
- Gates on this host: `source-check`, `format-check`, `native-smoke`, pytest; PPC parity on
  the four retired replays recorded in the log below. Wasm/viewer unverified (no emcc).

## Log

- 2026-08-30 — `retained`: both fixes, four classifications retired, four locks re-recorded,
  two ledger rows. Native aggregate after the lock re-record: 433 pass / 59 classified / 0
  fail / 0 error. PPC (fresh `make ppc`, `--timeout 180`): all four retired replays PASS at
  full length (links 7,123 and 5,375; ganon 11,140; yoshi 12,828). `source-check`,
  `format-check`, `native-smoke`, pytest 47 passed. Trace recipe: temporary `fprintf` at the `ftcoll.c` item-vs-shield check
  (`x221B_*`, `shield_hit` world position via `lb_8000B1CC`, `lbColl_80007BCC` result) and at
  `ftColl_80077688`; at `Item_80269DC8` (`xDCE_flag.b4/b5`, `xC58`); at `ftCo_800DDDE4`
  (thrower `cur_pos`/root translate, release vec, resolve output); and a same-file order trace
  in `msl_ground_headless_epoch_proc`, `Fighter_8006A360`, `grIzumi_801CC358`, and
  `Fighter_procMap` with `gm_8016AEDC()` as the frame stamp (Slippi id + 123).

# Correctness lead — the classic UCF shield drop is two rollouts (2026-08-26, landed)

## Objective

Close the one open residual that carried a named gameplay mechanism:
`held-down-shield-press-escape-decision` (marth `WingedGorgeousPanther` @9793, P3 Falco
shielding off a held-down rim press on the frozen Stadium main floor where the hosted
build spot-dodged). The ledgered suspect — the `x671` down-tilt timer arming a frame late —
was wrong; the vanilla gate is exact.

## What the retail probe showed

Two engine-dump probes (`reports/triage/run_hookshot_probe.py`, out under
`reports/triage/marth_escape_probe{,_b}/`) on frames 9787..9795 with PC hits at
`Fighter_8006AD10`, `ft_8008A348`, `ftCo_Wait_IASA`, `ftCo_80099794`, `ftCo_80099894`,
`ftCo_80091A4C`:

- Retail's `x671` is 0/1/2/3 on 9790..9793 exactly as hosted (reset when `lstick.y`
  crosses `-xC` at 9790); `held` carries L throughout (the trigger was held since the
  tech, not pressed at 9793); `x670` is 6; `cstick` is 0.
- At 9793 the PassiveStandF anim end runs `ft_8008A348` -> Wait, then `Wait_IASA` calls
  `ftCo_80099794`, which **enters `ftCo_80099894`** (the vanilla gate passes: `-0.7125 <=
  x314`, `3 < x318`) — and yet `ftCo_80091A4C` runs next and the frame ends in GuardOn.
- `coll_data.floor.index` is 34 with `floor.flags` **0** (main floor), so the UCF 0.84
  shield-drop code (`External/UCF 0.84/UCF/UCF Shield Drop.asm`: `floor.index != -1 &&
  floor.flags & 0x100`, i.e. `mpColl_IsOnPlatform`) could not have suppressed it.
- The replay's own embedded gecko list (event 0x3D via 0x10 splitters) carries a
  304-byte code at `800998A4` that is `External/UCF 0.8/Logic/UCF SD.asm`: c-stick gate,
  a float rim test (`((int(|v|*80 - 0x37270000) + 2) / 80)` per axis, squares summed
  `>= 1.0` in single precision), `x670 > 3`, `lstick.y > -0.8`, **no platform test**;
  its suppress path pops `ftCo_80099894`'s frame and returns to the caller's `li r3,0`,
  which is why the spot-dodge check reports false and the Guard check follows.

## Final boundary

- **Owner:** `src/runtime/match.c::msl_ucf_suppress_spotdodge` selects between the two
  codes on `ucf_shield_drop_084_enabled` (new `MslCoreMatchRules` field, wire byte 32,
  `MslCoreMatchConfig` 53 -> 54 bytes, `MslCoreStreamJobHeader` 109 -> 110). On = the 0.84
  platform-gated integer rim test (unchanged); off = the 0.8 float rim test everywhere.
- **Plumbing:** `wire.h/.c`, `match.h/.c`, `scalar.c`, `api.c` (production = 0.84),
  `tools/validation/native.c` (both entry points), `suite_io.py`, `validate_replay.py`,
  the four C smokes/benches, `tools/viewer/schema.c` + regenerated
  `schema.generated.js` + `sim.js`. Suite entries carry
  `"ucf_shield_drop_084_enabled": false` per replay, defaulting true.
- **Corpus profile:** a scan of every validation replay's embedded gecko list
  (`reports/triage/gecko_scan.txt`, driver `reports/triage/gecko_scan.py` — the 0x800998A4
  body is 200 bytes for 0.84 and 304 bytes for 0.8; only runtime scratch words, the
  `backup` frame size and the branch-back differ within a version) finds **16 recordings
  with the 0.8 code**, and each of those sixteen also matches none of the other 0.84
  bundle files (their dashback/SDI codes are the older bundle too — an open lead if any of
  them ever forks on a dashback/SDI decision). Flagged: falcon OddballCleanViper; icies
  master-diamond, platinum-platinum; luigi slippi_01.07.22; marios 6082, platinum-platinum,
  ranked-anonymized platinum-platinum, Slippi-20220321; marth BreakableMundaneElephant,
  FemaleWorthyAlpaca, WingedGorgeousPanther; peach HeartyStiffMallard, ScaryFrankPorcupine;
  pikachu diamond-platinum; puff master-diamond; samus diamond-platinum.
- **Ledger:** `canonical:ucf-classic-shield-drop-version-flag` in
  `src/upstream_delta_ledger.tsv`.

## Evidence and acceptance

- `WingedGorgeousPanther` 9,915 -> **10,625/10,625 exact**; `peach/ScaryFrankPorcupine`
  (classified `dolphin-ulp-item-motion-profile` @25891, 38 rows) turned out to be the
  same fork and is **26,844/26,844 exact**. Both classifications retired (65 -> 63) and
  their locks re-recorded (`008c3a4a80f7a6dc` -> `a2f43edd9a4b6306`, `916b55fd67d0ab1a`
  -> `ea018e8ffbc9f96e`); no other lock moved.
- Native aggregate: **429 pass / 63 classified / 0 fail / 0 error** (4,876,298 frames).
  Every other flagged replay reproduces its previous outcome, so the 0.8 predicate is
  corpus-neutral except where it closes forks.
- Gates on this host (gcc 15.2, recorded lock-clean): `source-check`, `format-check`,
  `native-smoke`, `python-library`; the Wasm/viewer targets are unverified (no emcc) but
  the schema regenerated cleanly and `sim.js` writes the new byte.
- Trap noted while triaging: running `validate_replay` on a bare positional replay path
  does not apply the suite's per-entry UCF profile, so `WingedGorgeousPanther` falsely
  forks at 3864 (`ucf_cardinals_1_0_enabled` false in its suite entry). Use `--suite`.
- `src/api.c` had set the production profile without `ucf_shield_drop_extended_enabled`
  (it stayed 0 from the memset), so production RL ran the classic shield drop only; at the
  user's direction the extended (counter-based) shield drop is now on in production too,
  matching current Slippi.

## Log

- 2026-08-26 — `retained`: probes, corpus scan, flag, suite profiles, classification
  retirement, lock re-record, ledger row. PPC parity (binary rebuilt for the 54-byte
  config): marth 17 pass / 2 classified / 0 fail with WingedGorgeousPanther PASS;
  ScaryFrankPorcupine PASS 26,844/26,844. The peach suite's other ten PPC "fails" are
  classification entries that carry only a `native` snapshot; peach was never
  PPC-recorded. (Corrected 2026-08-28: only eight of the ten PPC fingerprints equal
  native, e.g. TameEmbellishedSparrow 65e7d85cca69a2b5; CandidThankfulCockroach and
  CapitalPristineTarsier differ — see the 2026-08-28 entry.) pytest 47 passed.
  Committed as `61a2d260`.
- 2026-08-28 — `retained`: peach and marios PPC-recorded. `make ppc` on `5c0a91ac`, then
  per-suite PPC with `VALIDATION_WORKERS=8 --timeout 180` (the auto 16-worker default trips
  the 30s PPC timeout on 15k+ frame replays and reports them as `error`). peach PPC
  9 pass / 1 classified / 10 unrecorded before; eight of the ten PPC fingerprints equal
  native and are now `"ppc": "native"` aliases. Two differ and carry their own PPC snapshot:
  CandidThankfulCockroach (native 17b7be538e5e236a / 62 rows -> PPC 6002eb36f1ec85ea /
  120 rows) and CapitalPristineTarsier (851c665507f1483b / 60 -> b3f57dbd9b3539c0 / 117).
  Both are the ledgered PPC-wider `dolphin-ulp-motion-profile` shape (cf. sheik
  GlaringRosyAlpaca 38/117, WavyRundownAardvark 50/107): a ~120-row 1-ULP root-position
  window on the airborne port-2 fighter (y ~127..160) that starts 1-3 frames earlier on
  PPC; percent residuals and strict suffixes are identical across backends. marios PPC
  44 pass / 6 unrecorded, all six fingerprints equal native -> aliases. After recording:
  peach native and PPC 9 pass / 11 classified / 0 fail; marios native and PPC 44 pass /
  6 classified / 0 fail. Still native-only: aggregate_recent (2), battlefield_recent (1),
  pokemon_stadium_recent (1). Logs: `reports/triage/{peach,marios}-ppc-head-5c0a91ac.txt`.
- 2026-08-26 — `retained` (`a3158608`, follow-up): the production profile now matches the
  Slippi netplay injection list (`Output/InjectionLists/list_netplay.json`: the full UCF
  0.84 bundle) — `src/api.c` turns on the extended shield drop, and the Python package's
  `ucf_cardinals_1_0_enabled` default (`melee_sim/config.py`, `env_batch.py`) flips to
  True so bots train under 1.0 cardinals as they would see online. Validation suites are
  untouched (per-entry flags).

# Structural packet — per-fighter sealed-arena reserves (`decomp-port-mem`, landed)

## Objective

Stop the RL fleet's post-seal arena aborts. Every runtime pool reserve that
`msl_core_match_reset` buys before `msl_memory_finish_initialization` seals the Match
arena was a flat match-wide constant sized against one port's worst case (or, for FObj,
a `has_samus ? 512 : 256` branch). Four ports can each contribute the same article burst,
so the fleet hit `HSD_ObjAllocAddFree` / `HSD_MemAlloc` "arena allocation failed" aborts
mid-match on ordinary lineups: four Ness (FObj, PK Flash detonations), four Samus air
grapple-catches (GObj -> class mem-piece -> AObj -> ItemLink in turn), four Pikachu
(item pool at Thunder's four articles), Ness vs Ice Climbers on Yoshi's Story (item pool:
seven-item PK Thunder + five Blizzard puffs + stage Shy Guys), a four-Ice-Climbers
mirror (pose track arena at 1058 live tracks), and Peach vs Link on Dream Land (the
192-byte JObj mem-piece class under a lazily loaded aerial hookshot). The packet is
landed across commits `066cffdd`..`3cd7ead3` (2026-08-07..08-17); this section is its
ledger, written after the fact on 2026-08-26.

## Final boundary

- **Owner:** the reserve block in `src/runtime/scalar.c::msl_core_match_reset` (the
  `HSD_ObjAllocEnsureFree` calls, `msl_item_reserve_runtime_pools`,
  `hsdPreallocateMemPieces`, and the new `hsdPreallocateMemPiecesForClass` in
  `src/sysdolphin/baselib/class.c`), plus the per-player pose arenas in
  `src/runtime/fighter_pose.h`. Every reserve is a per-fighter term summed over the
  configured ports (bursts from different fighters overlap in time, so the operator is a
  sum, never a maximum), plus a stage term where the stage itself spawns.
- **Canonical state:** none added. The counts (`samus_count`, `ness_count`, `link_count`
  for Link/Young Link, `sheik_count` for Sheik and transform-capable Zelda, `ics_count`,
  `peach_count`, the accumulated `item_reserve`) are reset-time locals derived from
  `match->config`; the pools themselves are the pre-existing HSD ObjAlloc/mem-piece owners.
- **Consumers:** every sealed Match (native, PPC, Wasm share the reserve code on the
  `MSL_CORE_NATIVE` path), savestate size, and the runtime census.
- **Displaced:** the flat constants, the `has_samus` FObj branch, the flat 1024-track pose
  arena, and sizing the item pool from `MSL_CORE_MAX_ITEMS` (the observation array's
  width — `msl_core_match_write_items` publishes the first fifteen live items and stops —
  not a gameplay limit; the pool is now independent of it).
- **Deletion boundary:** no post-seal growth path, no fallback allocation, no lineup
  special-casing beyond the per-fighter terms. Reserves are validated only by measured
  high water against real capacity with a 20% headroom bar; no reserve is raised without
  a scenario that reaches the burst and proves the articles were live.

## The reserve terms (all `refs/melee`-owned pools, measured with the reserve raised)

| Pool | Reserve | Measured worst (four-port unless noted) |
|---|---|---|
| AObj (`HSD_AObjGetAllocData`) | 128 + 64/Samus | 151 live, four-Samus air grapple-catch (5/46/91/151 for 0/1/2/4) |
| FObj | 256 + 128/Ness + 256/Samus + 32/Link + 32/Peach | 359 four-Ness (~80 per PK Flash detonation); 362 four-Samus ground (452 air); 356..405 Ness x2 + Samus x2; 258 four-Link; 240 four-Peach |
| IDEntry | 128 (flat) | peaked <= 78% roster-wide, left alone |
| RObj | 16 + 8/Link | 16 of the former 16 for four Link (4 RObjs each vs 2 for every other fighter) |
| GObj | 128 + 64/Samus + 32/Link + 32/Sheik(+Zelda) + 64/Ice Climbers | 170 four-Samus air; 184 four-Climbers Belay (aborted at HEAD before) |
| GObjProc | 256 + 64/Ness | 365 live procs in a four-Ness chaos soak (aborted the flat 256); 267 for Ness x2 + Pikachu x2 |
| Mtx | 256 + 32/Samus | 240 of 256 four-Samus chaos |
| Vec, dynamic-bone | flat (128 / item-bound) | <= 78%, left alone |
| ItemLink | 151 + 64/Samus + 24/Link + 32/Sheik(+Zelda) + 48/Ice Climbers | 150 of 151 four-Samus air (99%); 160 four-Climbers Belay |
| Item pool | per fighter: 10 Ness/Ice Climbers, 8 Sheik/Zelda/Pikachu/Samus/Link/Young Link, 6 others; + 8 flat on Yoshi's Story | 16 four-Pikachu Thunder (structural at every cadence 20..120); 17 Ness vs Climbers on Yoshi's (7 + 5 + Shy Guy waves of 3..5) |
| Class mem-pieces, generic | 128 flat (`MAXIMUM_RESERVE` 256 -> 512) | small classes' churn only |
| JObj class (192 B) floor | 128 + 128/port (`hsdPreallocateMemPiecesForClass`, also forces the bucket to exist) | growth over seal: 98 Peach-Link (the RL crash, former flat 128), 124..146 four-Link/-Ness/-Climbers, 395 four-Samus (scripted air catch minimum 320) |
| Pose joints | 256/player (pre-existing) | 1,016 four-Sheik (census) |
| Pose tracks | 384/player (was flat 1024) | 1,058 four-Ice-Climbers (~265 per port across Popo and Nana); Zelda next at 446 |

## Evidence and acceptance

- `tests/melee_core/article_pool_smoke.c` (in `native-smoke`) drives thirteen scripted
  scenarios — four-ness-dreamland, four-samus-air-fd, ness-samus-dreamland,
  ness-link-dreamland, four-link-fd, four-pikachu-fd, four-ics-fd, ness-ics-yoshis, the
  cold-tether set (four-link-zair-cold-fd, four-ylink-zair-cold-fd, four-sheik-chain-fd,
  peach-link-zair-cold-dreamland, four-ics-belay-cold-fd) — each asserting the burst
  actually happened (live detonations, beams, RObjs, pool-counted items, tethers) and
  that every pool kept 20% headroom. The cold-tether scenarios matter because a
  move-cycling driver warms the free lists with earlier spawns and never reaches the
  seal-state lists an RL policy hits by jumping and pressing Z. Reverting each reserve
  was checked separately: each reproduces its abort or fails the headroom bar.
- `tests/melee_core/pool_chaos_soak.c` (built on demand, not in `native-smoke`): a
  deterministic 60,000-frame pseudo-random soak that prints every pool's and class
  bucket's high water; a fifteen-lineup audit shows no aborts and every pool under 80%
  of capacity, and it is the instrument for any future reserve change.
- Runtime census (`make runtime-census`, 20 characters x 6 stages x 2/4 players):
  maximum arena use 973,328 -> 1,026,296 -> 1,132,368 -> 1,195,716 -> 1,311,096 of
  3,145,728 across the series (worst config four Sheik on Yoshi's Story); relocation
  records 5,371 of 16,384; pose joints 1,016 of 1,024. The ordinary two-player lock is
  608,100 arena bytes / 868 allocations before and after gameplay, savestate 671,140
  bytes (the sized-per-player track arena more than pays for the per-port item and
  JObj terms at two ports; four-player matches pay the rest).
- Gates at each commit: source-check (until the Link-era lock drift, fixed 2026-08-26),
  native-smoke, ppc, python-library; the Wasm target was unverified on this host (no
  emcc) — the change is plain C on the shared path. Replay locks and classified
  fingerprints did not move (pool growth is layout-neutral for the corpus; the
  `b1955a02` preload-shift precedent did not recur).

## Log

- 2026-08-07 — `retained` (`066cffdd`, `b7aefa05`): FObj per Ness (four-Ness aborted at
  359 against 283), then the composition fixed from maximum to sum with Samus as another
  per-fighter term; the `has_samus` branch is deleted.
- 2026-08-07 — `retained` (`19c849be`, `0ff5bb19`): AObj/GObj/mem-piece/ItemLink per
  Samus (four-Samus air grapple-catch aborted through four pools in turn); a twenty-
  fighter four-port sweep of every remaining flat reserve found RObj at 16/16 (four Link)
  and the item pool aborting at sixteen (four Pikachu); the other five flat reserves
  peaked <= 78% and stay.
- 2026-08-07 — `retained` (`65bf041c`): AGENTS.md roster brought to twenty and the
  per-fighter-reserve rule recorded as a convention.
- 2026-08-09 — `retained` (`a4eafdf2`, `de75ca49`): pose track arena 384/player (four
  Ice Climbers asserted `allocate_tracks` at 1058 live); item pool moved from a flat
  eight per port to measured per-fighter rates plus a Yoshi's Story Shy Guy term after
  an RL abort in `itClimbersBlizzard_Spawn` (Ness vs Climbers, seventeen live items).
- 2026-08-17 — `retained` (`3cd7ead3`): the 192-byte JObj mem-piece class gets a per-port
  floor that also creates the bucket before the seal (an RL abort in
  `ftCo_AirCatch_Anim -> it_link_get_joint -> HSD_JObjLoadJoint`, Peach vs Link, Dream
  Land); GObjProc per Ness, GObj/ItemLink per Link/Sheik/Climbers, Mtx per Samus, FObj
  per Link/Peach from the new chaos soak, which found three latent aborts beside the
  reported one.
- 2026-08-26 — `retained`: ledger written; `CURRENT_BASELINE.md`'s memory-contract table
  refreshed from the census at this HEAD (it had carried the 2026-07-19 figures through
  the whole series).
- 2026-08-26 — `retained` (throughput/digest check of the packet): control `d59b65ba`
  (pre-packet) versus candidate `3cd7ead3` (packet head, before the partner-arm gameplay
  fix), same release profile, the retained 153-replay eight-character 256-batch workload,
  three alternating samples each on CPU 0 of this host. Digests are identical on every
  run (`474828382690a770`), so the reserves are gameplay-neutral. Median cycles/frame
  129,981.2 control versus 132,630.9 candidate (+2.0%), with samples 126,658..154,144 and
  128,807..136,931 — inside this host's run-to-run spread (a Ryzen 9 3950X shared with
  other jobs, load about 3), so no material cost is claimed or excluded beyond that. The
  absolute figures are not comparable to the retained 9950X3D medians; see
  `CURRENT_BASELINE.md`.

# Previous structural packet — Link and Young Link

## Objective

Add Link (internal kind FTKIND_LINK 6, public char id 6) and Young Link (FTKIND_CLINK 20,
public char id 20) to the supported domain with a 60-replay suite (five per stage for each
character; mirrors and Link-vs-Young-Link games cover both). Link is a full original: six
chara TUs (ftLk_Init, the Hylian-shield/down-air-bounce ftLk_AttackAir, bow ftLk_SpecialN,
boomerang ftLk_SpecialS, spin attack ftLk_SpecialHi, bomb pull ftLk_SpecialLw). Young Link
is the Link clone: ftCl_Init delegates 19 of 21 motion rows to ftLk handlers and adds the
milk taunt ftCl_AppealS; his OnLoad also sets can_walljump. Shared article TUs own both
kind pairs: itlinkbomb (58/59), itlinkboomerang (60/61), itlinkhookshot (62/63, previously
imported for the Samus/Sheik/Icies tether family and now given its missing manifest row),
itlinkarrow (64/65), itlinkbow (76/77), and itclinkmilk (123, Young Link only).

## Final boundary

- **Owners:** the fourteen imported TUs byte-identical to the pinned decomp except the
  ledgered deltas below; `it_803F3100`/`it_803F2F28` own the eleven new logic rows
  (including retail's Link-vs-Young-Link hookshot second-slot asymmetry);
  `MslDatLinkArticles` (shared by PlLk/PlCl) owns the seven-slot x48 list: five articles,
  the milk slot (NULL in PlLk), and the sword HSD_Joint accessory.
- **Data:** PlLk*/PlCl* (five costumes each, Nr/Re/Bu/Bk/Wh, plus AJ and DViWaitAJ) +
  EfLkData.dat; manifest 173 -> 190 files. Anim count 314 for both; ftData_UnkIntPairs
  {0,14}; both kinds map to efAsync bank 6.
- **Effects:** EfLkData.dat is MODEL-ONLY (both leading effLinkDataTable words are
  unrelocated NULLs, the EfDkData/EfNsData shape) and there is no EfClData.dat — the two
  kinds share bank 6. The spin-attack efSync ids 0x4BB/0x4BC are pure efLib model creates
  on bank-6 model ids 0x1770..0x1773; the item-side ids (0x448 arrow sparkle,
  0x41C/0x3F1 hookshot) resolve into the already-loaded common bank 0, so no generator
  RNG projection consumes bank-6 ids.
- **Parts:** derived rows 46 live / 30 cold of 76 (Link) and 46 live / 34 cold of 80
  (Young Link), with CODE_ANCHORED spin-attack joints (raw parts[24]/[26]), the
  GetBoneIndex left thumbs (35/37 — bomb, boomerang, and milk spawn anchors), and the
  sword accessory attach/part pairs (67+68 / 71+72). The accessory part must stay live so
  retail's ftAnim_8006E7B8 tree/part pairing, which counts the attached accessory node,
  stays aligned in the compact hosted tree. The hookshot's parts[139] chain-joint store
  is a runtime-anchored article slot like Samus's zair beam tip and stays outside the
  admission mask. The derive tool's Fighter_804D6540 parsing was corrected (4-byte
  {part, attach, mode, depth} records; only Kirby/Link/CLink have entries, so no prior
  mask is affected).

## Hosted representation decisions (all ledgered)

- **Hookshot attr scratch mirror** (`canonical:link-hookshot-attr-scratch-mirror`):
  it_link_attr_math recomputes the x2C..x48 attribute lanes in place at every hookshot
  spawn — retail treats the shared DAT blob as scratch. Each match owns one writable
  mirror per kind in MslSourceMatchState, seeded from the article on first use, exactly
  the samus_grapple precedent; all 14 fetch sites route through it.
- **ftLk_DatAttrs pointer-width lanes** (`canonical:link-datattrs-pointer-width`):
  x94/x9C/xA0 were UNK_T; ftCo_Attack100.c's ftCo_LinkCatchAttrs view types the same
  lanes s32, and the widened pointers shifted every later lane (da->xBC read 10 instead
  of the hookshot kind 62). Typed s32, matching the yoshi-datattrs-pointer-width
  precedent.
- **Sword accessory joint identity** (`canonical:sword-accessory-joint-identity`):
  ftParts_800753D4's retail stack copy of the isolated accessory joint becomes a Match
  arena copy at fighter construction so both loads share one relocatable HSD id.
- **OnLoad ext-attr store guards** (`canonical:link-onload-attr-store-guard`): both
  OnLoads re-derive attackairlw_hit_anim_frame_end into the sealed DAT page; the
  idempotent re-store is skipped, the donkey-onload-attr-store-guard shape.
- **Boomerang self-stores** (`canonical:link-boomerang-attr-self-store`): three retail
  `attrs->xC = attrs->xC` self-stores skipped against the sealed arena.
- **Article span-packing exemption** (native_dat.c): PlLk.dat packs an unreferenced
  hookshot joint blob between the boomerang Article and the next relocation target;
  Articles are now never span-packed (each is a single object referenced slot-by-slot).
- **Archive cache capacity** 4096 -> 8192: every fighter animation figatree slice takes
  an entry, so the census scales with summed anim counts; the 18-character domain peaked
  just under 4096.
- **ft_8008A348's kind switch unguarded**: the hosted branch had excluded the
  Link/CLink Hylian-shield arm from the Wait-enter path back when the callees were
  unsupported-fighter stubs; the SquatWait twin was already live.

## Pre-existing gaps closed while admitting

- `api.c::supported_character` was missing Ness (neither Ness commit updated it).
- `Makefile` VALIDATION_CHARACTERS was missing Ness.
- The viewer's `externalCharId` was missing Ness (8->11) and Yoshi (14->17), so both
  rendered the wrong slippilab zip; Young Link needs 20->21 and Link is identity.
- `itlinkhookshot.c` was compiled without a source_manifest row.

## Suite state — GATE (54 pass + 6 classified / 0 fail, both backends)

Staged 60 replays (screened from link_ylink_replays.zip: all singles, two human ports,
supported opponents, six legal stages, populated raw analog pad lanes, zero SHA
duplicates; all ten Stadium entries event-verified frozen — 0x41 declared, zero events).
`replays/suites/links.json` IS in the aggregate (426 -> 486 replays; test counts bumped
in test_aggregate_suite_coverage and test_melee_core_validation_runner), all 60 output
locks recorded, and the six residuals classified with both-backend snapshots
(native == ppc bit-for-bit on every one): redxlink_0310 and slippi-2025-08_0815 under
`hitlag-accumulated-pad-edge` (the Ness partner-arm debt, now measured victim-side:
the thrown victim's kb_applied is nonzero for dthrow item hits where retail's stays 0,
so the victim drives ftCo_8008EC90's partner arm and the tech press during laser
hitlag is swallowed), slippi-games-2025-05 under a new `link-boomerang-deflect-fork`
id (hosted UnkMotion2_Coll env_flags stay 0 through retail's surface deflect; needs
the Dolphin item-collision probe), and 18932 / slippi-2025-01 / slpfiles-2025-10_1020
under `dolphin-ulp-motion-profile` (1-2 ULP self-healing seeds).

Historical note — the dominant families as first triaged (all since closed):

1. **Hookshot latch one frame late** (item.state 3-vs-1, a handful of rows per replay,
   plus the action 361-vs-360 zair catch rows): the chain-link count from
   it_link_attr_math's x2C derivation is suspected one off — the expression is a lerp
   (`t*a + (1-t)*b`, a classic MWCC fmadds pair) feeding an s32 link-count compare.
   The Link TU MWCC fused-op audit has NOT been done yet; the Ness packet's asm-vs-marker
   method applies (refs/melee/config/GALE01/symbols.txt sizes + DOL disassembly).
2. **Boomerang misc1 (xDD8) families** (hundreds of rows, e.g. 219-vs-132) and 1-2 ULP
   pos_x rows — same fusion audit territory (deceleration/turn math).
3. **Fighter-attack-vs-item capsule forks with identical exported geometry** — the
   second confirmed class: in slpfiles-2025-03 (BF Young Link ditto) at frame 41 the
   hosted sim lands a 7-damage fighter attack on an item (P1 hitlag 5, item.damage 7,
   item timer frozen) that retail never lands, while every exported position matches
   through frame 40 (the only live item is P2's arrow stuck at (42.65, 0) and P1 is
   50+ units away at (-9.47, 32.5)) — so a hosted item HURT capsule must be sitting at
   a phantom position. Item hurt capsules ride `article->x8_hurtbones` bone ids through
   `item->xBBC_dynamicBoneTable->bones[]` (it_8027163C) and their world positions come
   from item model sub-bone jobj matrices — the suspect is stale/unsolved item sub-bone
   matrices for the newly admitted article models (the arrow's x8_hurtbones is 0x9dd8 in
   PlLk/PlCl). The zair whip case is the mirror image (item-side hitbox vs fighter).
   Next: dump the arrow's xACC_itemHurtbox capsule world positions at frames 39..42 in
   the hosted run and find whose matrix feeds the phantom.
4. Various replays still fail in the first ~1,500 frames — untriaged beyond the above.

## Session goal and standing (2026-08-04, in progress)

**Goal: 20/30 passing per character.** Current native: 11 pass / 49 fail (Link 6/31,
Young Link 5/34 counting mirrors both ways). PPC oracle: 31 pass / 29 fail — the
cross-partition is 11 both-pass / 20 native-only-fail (HOST-WIDTH class) / 29 both-fail
(true logic). Work the width class down first (mechanical, ~+15 passes), then the logic
class.

Width-class leads (native-only fails), from the 20-replay first-mismatch census:
- RESOLVED (was the top width family): `action 43` was ftCo_MS_LandingFallSpecial, not Turn — the spin-attack landing lag read via the Fighter-cast attr walk (facing_dir1 = ftLk_DatAttrs::x30); fixed and ledgered, 11 -> 31 passes.
- (stale text below kept for provenance) `action_id expected=43 (Turn) actual=14/18/178` in ~10 replays — the hosted native
  build ABORTS a Turn after one frame (43 -> Wait at e.g. slpfiles-2025-05_0518 frame
  1646, entered from the bomb-pull end 357) because ftAnim_IsFramesRemaining goes
  false; PPC running the identical hosted C keeps the Turn and passes, so an upstream
  width-poisoned lane (anim rate or track state, likely set during the bomb-pull /
  item-hold path) differs between the backends. Frames-limited runs (frames=1700) did
  NOT reproduce the fork while full runs do, deterministically — the input-tape length
  affects the divergence, which itself smells like reading past prepared state. Next:
  frame-stamped dual-backend traces of the ChangeMotionState args (rate/start) for the
  1645 Turn entry and of the bomb-pull end transition.
- `pos_x` one-frame ±0.12..0.18 nudges in ~9 replays — looks like fighter-vs-item
  jostle push. Both smell like more raw-offset or width-shifted reads; sweep remaining
  `(u8*) fp +`/pad-overlay casts and item-side field views in ftCommon/it TUs.

Logic-class leads (both-backend fails):
- The grab/pummel mash divergence = the Ness packet's `hitlag-accumulated-pad-edge`
  debt, now load-bearing (e.g. redxlink_0310 @301: retail Falco throw connects, hosted
  victim escapes earlier). Progress on the deferred question: retail's pummeled victim
  exits ftCo_Damage's top gate (`!fp->dmg.kb_applied` — a pummel applies no knockback),
  so the capture-partner arm must run on the GRABBER's side (fp=grabber, other=victim),
  clearing the VICTIM's x668 — matching the retail probe (victim x668=0, victim b5
  clear, x183C_applied==0 skips the b5 block). The hosted build instead runs the arm
  with fp=victim (its kb_applied is set where retail's is not) and clears the grabber.
  Root-cause why hosted computes kb_applied for a pummeled held victim.
- The hookshot latch one-frame-late family persists in some replays (item.state 3-vs-1,
  361-vs-360): the probe table (reports/triage/link_hookshot_probe.md) shows retail
  anchors + steps the head on the it_802A78B8 fire frame, steps one vel/frame, freezes
  during owner hitlag, latches after the census; the launch-speed fix (tether mirror)
  closed most of it — re-census which replays still carry it.
- 1–2 ULP families: 18932 item.pos_x 2-ULP episode; redxlink_0221 percent 1-ULP at a
  damage application (staling multiply chain is the suspect).

**GOAL MET (2026-08-05): native and PPC both 54 pass + 6 classified / 0 fail — Link
27/31 passing, Young Link 32/34 passing** (goal was 20/30 each). The three closing
fixes: (1) `link-hookshot-chain-solver-gekko-rounding` — it_802A6474's four sum-of-
squares fmadds plus the Gekko frsqrte sqrt (fused fnmsub Newton terms) replacing libc
sqrtf across the hookshot TU, which the 32106 frame-1649 retail probe localized to the
latched-hang chain-constraint (self_vel += cur_pos − pos_2 amplifies knife-edge ULPs);
this flipped 20 replays including the whole offstage-fall family. (2)
`thrown-item-damage-gekko-rounding` — it_8026B1D4's speed-scaled thrown-item damage
(Gekko sqrt + fmadds fold), closing the three percent-ULP residuals. (3)
`lbvector-cosangle-gekko-rounding` — retail-faithful CosAngle (behavior-neutral at the
current corpus, gates the boomerang deflect branch). Full native aggregate green: 396
pass + 90 classified / 0 fail across 4.81M frames; the two icies pool-residue analogs
were re-recorded for the Link preload shift (b1955a02 precedent).

(Stale text below kept for provenance.) Standing after the landing-lag fix: native 31 pass / 29 fail — Link 16/31, Young
Link 16/34 (goal 20/30 each; need +4 per side from the 29 both-backend fails). The 29
are almost all SMALL self-healing episodes now: three 1-row fails (redxlink_0221 percent
1-ULP at a damage application — staling-multiply suspect; slippi-2025-01 pos_x 1 row;
slippi-2025-08_0805 pos_y 1 row), then 2..31-row pos_y/speed_y episodes. The dominant
recurring shape: a 1-ULP pos_y with a ~32-ULP speed_y_self in fighter state 361
(ftLk_MS_AirCatchHit — the offstage zair catch fall, e.g. 32106 @1649, slpfiles-2025-10
@1650): ftCo_AirCatchHit_Phys runs `self_vel = pos_delta` + shared ftCommon_Fall, so the
seed is the catch-hit ENTRY velocity, fed by the hookshot chain geometry (the pin-walk
normalize margins measured as razor-thin earlier, e.g. +2.4e-7 at one boundary). Range
audits confirm ZERO fused ops in the whole ftLk/ftCl address ranges and none reachable
in ftCo_AirCatch (the two fmadds nearby belong to ftCo_DamageBind's 800C4550). MEASURED (32106): bit-hex dual-backend traces of ftCo_AirCatchHit_Phys inputs
(pos_delta, self_vel, cur_pos) are IDENTICAL between native and PPC for all 41
state-361 frames — including frames after the 1649 pos_y[0] 1-ULP export mismatch that
only native shows. Internal gameplay state never diverges (the wall-hug clamp
re-derives pos each frame, so nothing feeds back), which means the divergence is
confined to the exported coll-clamped position at the mismatch frames: either
mpColl_800471F8's clamp output ULP-differs between x86 and qemu-PPC without ever
persisting (suspect: helpers in runtime/math.o / MSL/trigf.o, the only -O2
-ffp-contract=fast -mfma objects, reached by a new PS-wall path), or the export
samples a lane (coll_data.cur_pos?) that differs from fp->cur_pos. Next: trace
end-of-frame fp->cur_pos vs coll_data.cur_pos vs the exported row for P1 frames
1645..1655 on both backends; then look at the mpColl wall projection expression.

Fixed this session (all committed): tether launch reads through the hookshot scratch
mirror (0 -> 11 passes; probe-verified retail derives the launch from the scaled x38
lane), the arrow spread-table pointer-width NaN (phantom item hits), the Attack100 raw
fp-offset overlays (fp+0x2340 mv lanes, fp+0x65C held inputs), the Hylian-shield arm
unguarded in ft_8008A348, thumb/accessory parts admission, and the bomb misc1 mask.

## Log

- 2026-08-23 — `closed` (partner-arm inversion): retail probe on 53362 frames 1378..1410
  (reports/triage/ness_partner_probe/README.md) showed retail ALSO runs ftCo_8008EC90 with
  fp = the pummeled victim (kb_applied 41.09 — a pummel does carry knockback; the earlier
  "bit 5 clear" reading was an MWCC MSB-first bitfield misread, 0x2219=0x07 has b5 SET), and
  that the victim's own x668 goes 0x100 -> 0 between the arm and the next input update. The
  DOL (0x8008F078..84) stores the zero to r29 = fp, not r27 = other_fp: the decomp line
  `other_fp->input.x668 = other_fp->input.x66C = 0` is a decomp error. Fixed to `fp->...`
  in ftCo_Damage.c. Effect: 53362 and redxlink_0310 become exact end-to-end (10,673 and
  8,157 rows), locks re-recorded (native==ppc); the third `hitlag-accumulated-pad-edge`
  entry (links slippi-2025-08, +0.1125 throw-release pos_y) was unaffected and is re-owned as
  `thrown-release-pose-offset` (same fingerprint, text-only). Native aggregate 492 = 427 pass
  / 65 classified / 0 fail; per-suite PPC ness 27/3/0 and links 55/5/0 (PPC needs
  `--timeout 180` with 8 workers on this host — the default 30 s trips on the longest
  replays). `make source-check` fails on the untouched HEAD too (refs/melee snapshot digest),
  unrelated. pytest 47 passed.

- 2026-08-05 — `closed` (goal met, suite gated): the retail probe on 32106 frames
  1643..1655 (hookshot latched to the Stadium wall) proved every frame-1649 input
  bit-shared and pinned the fork inside the chain-constraint step; the DOL showed the
  two hosted mis-emulations (unfused length dot products, libc sqrtf vs the Gekko
  frsqrte+fnmsub sequence) — fixing them took the suite 31 -> 51. The same class in
  it_8026B1D4 (thrown-item speed damage, traced via Fighter_TakeDamage bit-hex: raw
  dmg 0x40c820c3 vs retail 0x40c820c4) took it to 54. lbVector_CosAngle ported
  (behavior-neutral). Six residuals classified (both backends bit-identical), 60 locks
  recorded, links.json wired into the aggregate (486), test counts bumped, icies
  pool-residue analogs re-recorded for the Link preload shift. Native aggregate: 396
  pass + 90 classified / 0 fail. Open leads for a future pass: the capture/thrown
  partner-arm inversion (now measured from BOTH faces — grabber-side mash debt and
  victim-side kb_applied/tech-swallow; fix needs the full aggregate as its gate) and
  the boomerang deflect-fork Dolphin item-collision probe.

- 2026-08-04 — `checkpoint` (fused-op and mask batch): ported the audited MWCC fused-op
  set for the Link item TUs — the hookshot's distance dot products (a fused TU-local twin
  of it_802A3C98, whose out-of-line body must stay unfused for the cross-TU tether
  callers), all eighteen chain position-update triples, the chain-count lerp
  (GALE01 0x802A2680), the grip-scale double blend (0x802A2E28), the arrow's spread-angle
  draw/charge lerps/polar lanes, and the bomb drag step (0x8029F63C); the ftLk/ftCl chara
  TUs carry zero fused ops. Audit method: symbols.txt sizes + DOL disassembly via the
  PPC toolchain objdump (scripted asm census; every hookshot sqrt expansion fuses except
  the out-of-line it_802A3C98 body). Corrected the bomb misc1 mask to 0: xDDB is held-
  phase pool residue (retail exports 0x5F pre-match heap bytes) and a constant 0x00 after
  the thrown-transition sign write. Suite floor moved from 87 to ~1,130 matched frames;
  still 0 pass / 60 fail / 0 error. Dominant remaining forks: the hookshot latch running
  one frame late (item.state 3-vs-1 plus the action 361-vs-360 zair catch rows — the
  lerp port did not close it, so the census is now RULED OUT — a spawn trace shows x2C = 15 airborne / 22 for the 0x168 zair, and hand-evaluating retail's float chain gives the identical counts; the remaining lead is a ONE-STEP HEAD LAG: in slpfiles-2026-03_0330 the recording's P1 Young Link zair (state 360) lands attack 1 on P2 at frame -6, and a hosted hitbox trace shows our whip capsule riding the chain head correctly (jobj = parts[139]) but reaching P2's x one frame later; the same lag explains the latch-one-frame-late family (item.state 3-vs-1) and the 361-vs-360 zair catch rows. The state timelines themselves align (12654's state lanes match through 2909), so the head appears to gain its first velocity step one frame earlier in retail — spawn-frame or transition-frame processing order. Needs the Dolphin engine-dump probe (drivers on newchar-puff: tools/dolphin/*) for retail's per-frame head/hitbox positions) and it_802A40D0's collision step) and
  the boomerang flight/turn fork (xDD8 spin counters drift after the turn).

- 2026-08-04 — `open`: TU import + full registry/admission wiring + data extraction +
  suite staging landed; native-smoke, source-check, and the quick pytest set are green;
  the qemu-ppc toolchain fallback was cherry-picked from experiment/decomp-port at the
  user's direction. PPC parity, fusion audit, residual classification, aggregate wiring,
  and lock recording all remain.

# Previous structural packet — Ness

## Objective

Add Ness (internal kind FTKIND_NESS 8, public/CSS char id 8) to the supported domain
with an 18-replay suite. Ness is a full original: eight chara TUs (ftNs_Init, the Yo-Yo
smashes ftNs_AttackHi4/ftNs_AttackLw4, the baseball-bat ftNs_AttackS4, PK Flash
ftNs_SpecialN, PK Fire ftNs_SpecialS, PK Thunder + PK Thunder 2 ftNs_SpecialHi, PSI
Magnet ftNs_SpecialLw) and eight article TUs (itnesspkfire 66, itnesspkfirepillar 67,
itnesspkflash 68, itnesspkthunderball 69, itnesspkthundertrail 70..73,
itnesspkflashexplode 78, itnessbat 101, itnessyoyo 102). Ness is also the first
admitted absorber, so `ftData_OnAbsorb` becomes a live table.

## Final boundary

- **Owners:** the sixteen imported TUs byte-identical to the pinned decomp except the
  ledgered MWCC fusion set; `ftData_OnAbsorb` owns the PSI Magnet absorb callback;
  `MslDatNessArticles` owns the eleven-slot article list.
- **Data:** PlNs* (four costumes Nr/Ye/Bu/Gr + PlNsAJ + PlNsDViWaitAJ) + EfNsData.dat;
  manifest 165 -> 173 files. Anim count 326, ftData_UnkIntPairs 14,
  ftData_UnkBytePerCharacter 10.
- **Effects:** EfNsData.dat is MODEL-ONLY — both leading `effNessDataTable` words are
  unrelocated NULLs, the EfDkData.dat shape — so the hosted loader publishes an empty
  command bank for bank 10 and no generator RNG projection consumes bank-10 ids. Every
  Ness-reachable efSync id (0x4EE/0x4EF PK Thunder, 0x4F0 PSI Magnet) is a pure efLib
  model create; 0x406 was already modelled by the Yoshi packet.
- **Parts:** derived row 36 live / 27 cold of 63, with a new CODE_ANCHORED `ness: (61,)`
  for the raw `parts[61]` Yo-Yo hitbox transform in ftNs_AttackHi4.

## Exactness work shipped

- **Pose program-node id widened past sixteen characters** (the packet's one hosted
  representation change): `MslFighterPoseJoint.program_node_index` was an 18-bit field
  with a 0x3FFFF sentinel. Ness is the character that pushes the preloaded pose-node
  count to 265,533, past that ceiling, and every suite replay aborted on the
  fighter_pose.c admission assertion. The id is now a full `uint32_t`, which lands in
  the struct's existing 64-bit tail padding — the gate profile's `sizeof` is unchanged
  at 56 and only the 32-bit (PPC/Wasm) size moves 44 -> 48.
- **The Ness MWCC fused-op set: 42 sites across 12 TUs**, each carrying its GALE01
  address. The audit method is asm-vs-marker per function using the decomp's exact
  symbol sizes (`refs/melee/config/GALE01/symbols.txt`); remaining per-function gaps
  are all static helpers inlined into several callers (one C marker, several asm
  copies): `NessFloatMath_PKThunder2`, `ftNess_atan2`, `getAttrStuff`,
  `itNesspkflash_SetScale`, `it_802BF4A0_adjust_tail`, `ftNs_AttackHi4_YoyoApplyDamage`.
  The seed that motivated the sweep: **PK Thunder's turn-radius steps**
  (GALE01 0x802ABE08 fmadds / 0x802ABE24 fnmsubs). The ball's heading walks 90 degrees
  down to zero in fifteen 6-degree steps; only the fused rounding leaves retail's
  2^-28 residue where the unfused form collapses to an exact zero, so every steered
  PK Thunder diverged on its first horizontal frame. Porting those two sites alone took
  the suite from 0/18 to 7/18; the full set reached 11/18.
- **PK Flash's leading item lane is not gameplay state**: no owner writes
  `itPKFlush_ItemVars.xDD4` — not the constructor `it_802AAA80`, not any motion
  callback — so its sampled Slippi byte is fixed-pool residue and the projection mask
  keeps only MISC1 (the charge scale at xDD8).

## Suite state (container authoritative) — IN THE AGGREGATE

Superseded by "Second import" below: the suite is now 30 replays and the aggregate is
426 = 333/93/0, then by the linux-host certification section at the end: the aggregate is
now 426 = 342/84/0. The original eighteen-replay state is kept here for provenance.

18 replays across all six legal stages (three each) against Captain Falcon, Falco, Fox,
Jigglypuff, Marth, Peach, Pikachu, Sheik, and a Ness mirror. All three Pokemon Stadium
entries are event-verified frozen (each declares the 0x41 stadium_transformation
command and records zero events over the full game); every capture carries populated
raw analog pad lanes, so none is a 3.18-layout re-wrap. Zero replays were rejected.

**ness suite: native 12 pass / 6 classified / 0 fail; PPC 15 pass / 3 classified /
0 fail.** `ness.json` is wired into `melee_core_aggregate`, which is now
**414 = 327 pass / 87 classified / 0 fail / 0 error**, with the eighteen output locks
strictly additive (18 added, 0 changed) and the pre-Ness 396 byte-stable throughout.

### The six classified residuals, each root-caused

- `2026-06` (Dream Land Sheik/Ness) — **`unmodeled-presentation-rng-stream-phase`**.
  Settled with an engine-dump probe capture of retail's own RNG stream. Retail's frame
  3922 opens with a `randi` at LR 0x80211648, inside `grOldPupupu_802113E0`'s
  rand_range blow timer, *before* `efAsync_Dispatch`'s 0x3EC rotation draw. The hosted
  build deliberately replaces Dream Land's wind machine with the recorded Slippi
  direction stream (ledger `canonical:slippi-stage-event-publication`) precisely
  because the retail timers sample the mid-frame RNG after effect draws that do not
  exist headlessly — so the replacement consumes no RNG. That one-draw phase shift
  moves the following `it_2725_Logic109_DmgDealt` `HSD_Randi(3)` from the stream
  position returning 0 (the Sheik needle survives into its bounce state) to the
  neighbouring position returning 2 (destroyed). Every retail draw for the frame
  reproduces exactly from the recorded frame seed, including the surviving needle's
  `Randi(8)` = 5 bounce speed and its SetupBounce draws; the control frame 3919 is an
  identical needle hit with no Whispy draw and matches the hosted sequence draw for
  draw. Reinstating the retail machine is strictly worse — it forks fighter positions
  through the wind push, which is why it was replaced.
- `51444`, `2025-11`, `52757`, `53870` — **`unrecorded-item-pool-residue`**. The Ness
  Yo-Yo's `itNessYoyo_ItemVars.x4` is written by exactly one owner, `it_802BFEC4`, and
  only on the smash's final frame, so every frame the article is alive samples its
  pool slot's previous occupant. Traced end to end: the hosted sim runs all of a
  replay's Yo-Yo smashes on the same slot with retail's spawn ids and computes `x4`
  correctly (0x3D95C61D, low byte 29 — one of the values retail exports). `51444`
  additionally samples Mr. Saturn's `xDE4` y/z, which `itDosei_UnkMotion4`/`5` never
  write; the bytes the hosted run reports there are the halves of a 64-bit host
  pointer (the 0x00007FFF high word is plainly visible in the union). PPC, whose
  layout matches retail, is bit-exact on the three Yo-Yo-only replays.
- `2025-03` (Frozen Stadium Ness/Peach) — **`dolphin-ulp-motion-profile`**. One 1-ULP
  seed at frame 1471 where `speed_x_attack` and `speed_y_attack` take the same value,
  so the knockback angle is exactly 45 degrees and both lanes carry the single
  `scaled_kb` factor rather than any trig difference. Self-heals into a 3,958-frame
  exact suffix, same first row on both backends. The knockback formula owners
  `ftColl_80079AB0/80079C70/80079EA8` already carry their complete fused-op sets.

## Exactness fixes shipped while closing the residuals

- **Shield-damage fusion.** `slippi-2025-02` diverged by 1 ULP on `shield_hp` for
  2,033 rows. `Fighter_ProcessHit_8006D1EC` carries two unported MWCC fusions in the
  shield-damage expression (GALE01 0x8006D2AC and 0x8006D2CC): the light-shield lerp
  between `x2DC`/`x2E0`, and the `x284` scale over the `x288` floor; only the trailing
  subtraction from `shield_health` stays a plain `fsubs`. Porting both made that
  replay fully bit-exact, moved PPC from 14 to 15 passes, and left the 396-replay
  aggregate **byte-stable with zero output drift**.
- **Sakurai-angle ramp** (`ftCo_Damage_CalcAngle`, GALE01 0x8008D8AC) and
  **`ftCo_CalcYScaledKnockback`** (0x800CF678): asm-verified fusions, identity at
  current inputs, aggregate byte-stable.

## Residual investigation — the `2026-06` "missing item" family is an RNG-draw gap

Root-caused to a single missing RNG draw; the retail draw sequence is fully pinned.

The diverging item is not a Ness article at all: it is a **Sheik thrown needle**
(kind 79). Slippi restores the RNG seed every frame in this online capture (the
recorded seed steps by exactly 0x10000 per frame), and `frame_pre_random_seed`
is a compared lane that matches for the whole replay, so each frame's draw
stream starts identically and the divergence is strictly intra-frame.

At frame 3922 (seed 0x0FCDD4E3) the needle hits Ness and retail runs
`it_2725_Logic109_DmgDealt`, whose `HSD_Randi(3)` decides whether the needle
survives into state 4 or is destroyed. Walking the LCG forward from that seed
pins retail's sequence exactly, confirmed by three independent observables in
the replay's own item stream:

- draw 2 `Randi(3)` = 0 -> the needle survives,
- draw 4 `Randi(8)` = 5 -> `ABS(it_803F7020[5])` = 2.5 = retail's recorded vel_y,
- draws 5..10 `itSeakNeedleThrown_SetupBounce` -> `it_803F7000[5]` = +1.0 =
  retail's post-hitlag vel_x, and `it_803F7040[4]` = -0.2 = its recorded vel_y
  step.

Our frame 3922 makes five draws: `[0]` the 0x3EC hit effect's `HSD_Randf`,
`[1]` the needle's `Randi(3)` (= 2, so we destroy it), and `[2..4]` the three
`it_80278800_rand_vec` draws of the destruction effect that only happens
*because* we destroyed it. So **we are exactly one draw short before the
needle's `Randi(3)`** (retail has two consumers there, we have one), plus one
more between `Randi(3)` and `Randi(8)` that we never reach.

Ruled out along the way: the effect-generator RNG model is faithful — common
model 8's recorded generator 267 has `kind = 0x400100`, so the `kind & 0x100`
branch in `hsd_8039F05C` skips the draw in retail exactly as our predicate
does, and generator 63 has `random < 0`. The control frame 3919 is a needle
hit that both sims destroy with the identical five-draw sequence, so the
needle path itself is right; only frame 3922 carries the extra retail consumer.
Ness's `ftNs_Init_OnDamage` is not the source either: none of its four
articles (yo-yo, PK Flash, PK Thunder, bat) exists at that frame.

Next step is the Dolphin engine-dump RNG probe (`refs/README.md`) to name the
missing consumer; everything else about the episode is settled.

**Instrumentation trap (cost me two false conclusions):** the native backend
runs `melee-core-native` as a `--server` subprocess whose `stderr` is
`subprocess.PIPE` and never drained, so every temporary `fprintf` trace is
silently swallowed and the traced code looks unreachable. Set `stderr=None` in
`_NativeRunnerPool` while debugging. (`make validator` is separately required
for `item_projection.h` changes.)

## Residual investigation — the native-only Yo-Yo `misc1` family is pool residue

The three native-only failures (`2025-11`, `52757`, `53870`) all diverge on one
lane: `itNessYoyo_ItemVars.x4`, sampled as Slippi `misc1`. Traced end to end on
`52757`:

- `x4` is written by exactly one owner, `it_802BFEC4`, and only at the smash's
  final frame (`yoyoCurrentFrame == x44_UPSMASH_YOYO_NUDGE_FRAME` = 49, which is
  also the despawn frame). Nothing initializes it at spawn.
- Our sim performs all three of the replay's Yo-Yo up-smashes, walks
  `yoyoCurrentFrame` 3..49 each time, and computes `x4` correctly:
  0x3D95C61D / 0x3D95C609 / 0x3D95C61D. The low byte 0x1D = 29 is exactly one of
  the values retail exports for this lane, so the arithmetic owner is right.
- The item pointer is the same pool slot for all three yo-yos, and the spawn ids
  (51, 55, 72) match retail's.
- Every export **during** a yo-yo's life reads `x4 == 0` in our sim, because the
  single write lands on the last frame before the article despawns. Retail
  exports a stale non-zero value there, i.e. the lane is read as item-pool
  residue left by the slot's previous occupant.

PPC reproduces retail exactly on all three replays; only 64-bit native differs,
which is the signature of residue whose byte pattern depends on the preceding
occupant's item-variable layout (pointer-widened on the host). This is the same
mechanism as the existing `unrecorded-item-pool-residue` classification used for
the Ice Climbers entries, and the natural disposition is to classify it with
native and PPC snapshots rather than to "fix" it. Note `x4` is not purely an
export lane -- `it_802BF800` reads it during the swing -- so retail genuinely
consumes uninitialized memory here; in these three replays every fighter,
physics, and other item lane stays exact.

## Second import — twelve more captures, all admitted (suite 18 -> 30)

A second Ness pool (`ness_replays_2.zip`) was screened and integrated. All twelve
candidates passed admission: singles, two human ports, Ness present, all six legal
stages, versions 3.18.0/3.19.0, populated raw analog pad lanes on every capture (no
3.18-layout re-wraps), and both Pokemon Stadium entries event-verified frozen (0x41
declared, zero events over the full game). No SHA overlapped the existing eighteen.

**All twelve are in the gate.** The ness suite is **30 replays: 17 pass / 13 classified /
0 fail** on native and **24 pass / 6 classified / 0 fail** on PPC, back to five replays on
each of the six legal stages, and `melee_core_aggregate` is
**426 = 333 pass / 93 classified / 0 fail / 0 error**. Every lock was added
byte-additively; the only existing lock this work rewrote is `puff/specials`, whose
classification the standings fix retires. Five of the twelve are bit-exact on first
contact, six are new rows in two already-named families, and one (`53362`) is carried as
explicit debt under a new id.

### The six new residuals

- `2025-07`, `2025-11_20251116`, `66686`, `auto-falco-2025-12` --
  **`unrecorded-item-pool-residue`**, the established Yo-Yo `x4` lane. The diverging
  article is item type 102 on every mismatch row and only `item.misc1` moves. **All
  four are bit-exact on PPC**, which is the direct evidence that the lane is 64-bit
  layout residue rather than a gameplay fork. (In `2025-11_20251116` the Yo-Yo is item
  index 1 until a second article despawns at frame 8913 and index 0 afterwards, so both
  reported lanes are the same article.)
- `50321` -- same family, but this replay's first Yo-Yo spawns at frame 364, before any
  other article has occupied the pool slot. The hosted pool is zero there, so `misc1`
  reads 0 against the console's 82: the residue retail exports is pre-match heap content
  no hosted run can re-derive. PPC narrows it to that first spawn alone (104 rows ending
  at frame 467, leaving an 11,915-frame exact suffix) because its layout matches retail
  for every later Yo-Yo, so this entry carries its own PPC snapshot.
- `2025-02` -- **`dolphin-ulp-motion-profile`**. Peach's turnip (item type 99, id 132)
  is dropped at frame 23119 with its release y one ULP high (0x42006f2d against
  0x42006f2c). Nothing else diverges -- pos_x, both velocity lanes, timer, damage and
  every fighter lane stay exact -- so the constant-gravity fall carries the offset
  unchanged until the turnip crosses the blast zone and despawns at 23187, leaving a
  140-frame exact suffix. **Both backends produce the identical snapshot** (same first
  row, 69 rows, `f1639cfe9ff96f60`, same field digest), so it is a shared rounding step
  against Dolphin, not a host FP profile or layout artifact.

## RESOLVED — the grab-timer standings term (stock-mode scoring)

`49422_Game_20250130T192133` is **fixed and back in the suite**, bit-exact over all
12,422 frames on both backends. The chain, settled by retail probes:

- Ness is held in `CaptureWaitHi`; our sim broke the grab (grabber `CatchWait` 216 ->
  `CatchCut` 218, victim 224 -> `CaptureCut` 229) where retail held three more frames
  and threw. `ftCo_CaptureWaitHi_Anim`'s gate is `grab_timer <= 0` against
  `ftCo_804D90D0`, which the DOL confirms is exactly `0.0` with a real `<=`
  (`fcmpo` + `cror eq,lt,eq`, GALE01 0x800DB968).
- The seed value is
  `pct*x368 + (x358*(x35C - handicap) + x354) + x360*(x364 - (Player_80033BB8 + 1))`.
  Ness's percent is genuinely 0 at the grab and every capture runs handicap 9, so the
  standings term is the only lever: rank 0 seeds 75, rank 1 seeds 60.
- **Retail probe at 49422 frame 6118** (`ftCommon_InitGrab` entry, the
  `stfs f1,0x1A4C(r3)` at GALE01 0x8007DBCC): retail seeds **75** and
  `Player_80033BB8` returns **0**, where the hosted build computed 1.
- **Retail probe at 53362 frame 1374**: retail seeds **60** and the rank is **1**,
  matching the hosted build.

**The rule is stock count, not KOs minus falls.** `fn_8016588C` dispatches on the match
mode in `lbl_8046B6A0.x24C.x5`, and a probe of both queries reports **x5 = 1** -- the
stock-versus arm, not the KO-minus-falls default the hosted projection had ported. Mode 1
scores a live player by their remaining stocks and nothing else: GALE01 0x80165988 loads
`MatchPlayerData::stocks` with `lbz` and sign-extends it straight into the result. Only a
slot already out of stocks takes the second arm, where the score collapses to survival
time minus 0xFFFFFF (0x8016599C..0x801659B4) so an eliminated player always ranks last.

That explains both probes directly. At 49422 frame 6118 both players hold 2 stocks, so
the standings tie and Ness is not the loser -- while KOs minus falls put him behind,
because the Falcon's self-destruct denied him the KO credit the old expression scored by.
At 53362 frame 1374 the stocks are 4 against 3 and the rank is 1, which both rules agree
on. In matches without self-destructs the two formulas order players identically, which
is why the corpus never caught this.

**The block is refreshed live.** The same probe shows `gm_80166378` running at frame 6118
inside that very query, so the scene-guarded cache is repopulated on demand -- the value
retail served is the live one. An earlier freeze-at-first-query model was tried and
rejected: it fixes 49422 by accident and regresses six pre-existing replays
(`marth/WellWornSmallGoshawk`, `marth/VigorousRelievedLlama`, three `doubles_recent`,
`icies/dl-marth-2025-04`).

**The fix retires pre-existing debt.** `puff/specials.slpz` carried a
`scene-standings-cache-gap` classification whose rationale reads "the minimal headless
gm_8016C5C0 projection supplies a loser rank one above retail's scene-owned MatchEnd
cache, shortening one Sing timer by exactly 15 frames" -- the same off-by-one reached
through Sing instead of a grab. That replay is now bit-exact on both backends, its
classification is deleted, and its output lock is the one lock this change rewrites.

**Method note.** A first pass mis-attributed this to the self-destruct score rule
(`xC = -2`) by reasoning backwards from two rank observations instead of reading the
executed branch. Both models happen to produce identical output on the whole 425-replay
corpus, so the corpus could not tell them apart; only probing `fn_8016588C` itself showed
the default branch never executes. When a formula has a mode switch, probe which arm runs
before fitting parameters to the result.

**Traps worth remembering:** `docker cp` preserves the source mtime, so copying an edited
file into the container can leave it *older* than the existing object file and `make` will
not rebuild -- a "control test" run that way silently measures the previous binary, which
briefly inverted a conclusion here. `touch` copied sources before `make`. `make native`
also does not rebuild the PPC binary; `make ppc` is separate, and a stale
`melee-core-ppc` reproduces the old behavior long after the native fix lands.

## RESOLVED (2026-08-23) — hitlag-accumulated pad edges

**Resolved:** the arm clears the damaged fighter's own x668/x66C (DOL r29 = fp); see the
2026-08-23 log entry in the Link packet. The text below is kept for provenance.

`53362_Game_20250504T004716` is **in the suite**, carried as
`hitlag-accumulated-pad-edge` with native and PPC snapshots. It is a known hosted defect
rather than a recording artifact, classified as explicit debt so the replay keeps its
coverage and so the eventual fix announces itself by breaking the entry -- the same way
`puff/specials`'s `scene-standings-cache-gap` surfaced the standings fix.

- Retail and the hosted build agree exactly through frame 1387 (timer 47/46/45 at frames
  1382/1383/1387). At 1387 the hosted `ftCommon_GrabMash` subtracts the 6-point mash and
  retail subtracts nothing, so retail runs +6 from 1388 onward (44 against 38, and 6
  against 0 at 1408). The hosted timer lands on exactly `0.0` at 1408 and trips
  `ftCo_CaptureWaitHi_Anim`'s `grab_timer <= 0` gate one frame before the throw; the grab
  breaks, `fn_800DAD18` stops re-anchoring the pair, and the match forks to the end.
- Everything upstream is exact and retail-verified: seeded timer 60, standings rank 1, the
  1-per-frame decrement and the 6-point mash quantum.
- The trigger is `fp->input.x668`, the newly-pressed mask. The recording holds A from 1383
  through 1387, so the press edge lands inside the pummel's four-frame hitlag
  (1383..1386), during which the capture Anim -- and therefore `ftCommon_GrabMash` -- does
  not run.
- **Retail probe at `Fighter_Spaghetti_8006AD10`'s entry**: the held Ness carries
  `fp+0x2219 = 0x07` (bit 5 clear) and `input.x668 = 0` on *every* frame of the hitlag.
  The hosted build has `x2219_b5` set and `x668 = 0x100` across the same frames.
- **The input update is not at fault.** Instrumenting it with a shared sequence counter
  shows it overwriting `x668` to 0 exactly as it should on the first frame the flag
  clears, and shows the capture Anim consuming the update that immediately precedes it.
  The earlier reading of this residual -- "the priority-3 update failed to overwrite" --
  was wrong; the flag and the stale press are established earlier, when the pummel lands.
- **`ftCo_Damage.c`'s capture-partner arm is the site.** That arm always runs
  `other_fp->input.x668 = other_fp->input.x66C = 0`, with a conditional
  `other_fp->x2219_b5 = true` above it. Instrumenting it shows the hosted build reaching
  it at frame 1383 with **`fp` = Ness** (the captured fighter, whose `victim_gobj` points
  back at the grabber) and **`other_fp` = Captain Falcon**, so it clears the *grabber's*
  pressed mask and sets the *grabber's* hitlag flag while the victim's own `x668` and flag
  are left untouched -- inverted relative to the effect retail produces on the victim.
  Which fighter drives that arm for a pummel on a held opponent is the next thing to
  settle; the port's copy of the line is byte-identical to the decomp, so this is a
  branch-selection question, not a missing statement.
- Not patched under a single replay: `x668` feeds every `CheckInput` owner, so the fix
  needs the full aggregate as its gate.

## Follow-ups

1. `2025-03`'s 1-ULP knockback seed is the one residual without a named mechanism; the
   remaining lead is `kb_applied`'s own input chain (percent/staleness/weight), since
   the formula owners and every fusion reachable from the angle path are already
   complete. A retail probe on the `ftColl_80079AB0` inputs would settle it.
2. The hitlag-accumulated pad-edge defect above. Retiring the
   `hitlag-accumulated-pad-edge` classification is the acceptance test; the corpus-wide
   risk is that `x668` feeds every `CheckInput` owner, so the full aggregate is the gate.
3. Probe captures are now self-terminating (`MSL_PROBE_EXIT_FRAME`, slippi-dolphin
   80e8cd15): a bounded window finishes in seconds instead of running to the external
   alarm, so retail ground truth is cheap enough to reach for early rather than last.
2. The Mr. Saturn `xDE4` lane in `51444` is a pre-existing gap the Ness suite merely
   exposed (a Peach-pulled item), not a Ness owner; if `itDosei` states 4/5 are ever
   given an explicit past-member carve-out, that classification can be narrowed.

## Reclassified debt — marth WingedGorgeousPanther @9793

**RESOLVED (2026-08-26): not the tilt timer — the recording ran the UCF 0.8 shield drop with no platform gate; see the top section. Kept for provenance.**

`incomplete-nintendont-post-frame-stream` was a misread and is now
`held-down-shield-press-escape-decision` (owner moved from the Nintendont
recorder to `ftCo_Escape`). The recorder is live across the whole window: shield
HP decays 60.00 -> 53.28 at 0.28/frame while the action id advances GuardOn ->
Guard and instance ids increment, and `animation_index` UINT_MAX with
`state_age` -1 is just this stream's Guard/Entry/Dead representation (41 runs on
P3, 33 on P4, every earlier one inside the 9,915-row exact prefix). The real
divergence is ours: PassiveStandF ends at 9793 and the player presses shield
with the stick already held down-left (lstick -0.6875, -0.7125, past the down
threshold since 9791); retail shields, we take `ftCo_80099794`'s spot-dodge gate
(`lstick.y <= x314 && x671_timer_lstick_tilt_y < x318`) into Escape with 14
intangible frames. Suspect the x671 down-tilt timer being armed a frame late, so
a two-frame-old hold still reads as a fresh smash — a retail probe at 9793 would
settle it. Gameplay resyncs bit-exactly by 9916; the rest of the 710-row tail is
only the global attack-instance counters off by one. Not UCF: all 16 rollout-flag
combinations reproduce fingerprint `3d95a3cb4754c703`, and
`msl_ucf_suppress_spotdodge` needs `mpColl_IsOnPlatform` so it cannot fire on the
main stage floor. The `expected` snapshot is untouched, so this is a text-only
re-own: the replay still classifies at the same fingerprint and no suite count
moves. Verified on the yoshi base it was recorded against (marth stage-3 native
`pass=1 classified=2 fail=0`, msl-luigi); not re-run against this packet's build,
which the edit cannot affect.

# Previous structural packet — `decomp-port-arm64-ppc` merge polish

## Objective

Bring the reviewed `decomp-port-arm64-ppc` tree to a mergeable state without widening its
correctness policy or accepting regressions to the original eight-character runtime. The retained
ownership and experiment log live below; retained performance evidence lives in
`agent_docs/performance/HISTORY.md`.

## Final boundary

- **Final owners:** `ftCo_800DA190` owns the character-specific grab accessory correction and must
  read `Fighter.kind` rather than a PPC-width raw leading word; `ftCo_800AC5A0` owns the CPU-DI
  command bytes and must not enqueue undefined host locals on its zero-knockback path;
  fighter-pose admission owns live joint capacity; the existing fighter/dynamics publication
  phases own the matrices they consume; Makefile/CI own complete gates and host architecture
  selection.
- **Canonical state:** one source-shaped Match state, one admitted fighter-pose graph, and the
  existing JObj matrices/dynamics descriptors. No replay row, validator classification, benchmark
  mode, or second synchronized cache becomes production state.
- **Consumers:** native/PPC/Wasm simulation, 2- and 4-player reset/copy/save/restore, validation,
  observation/viewer publication, and native/macOS build profiles.
- **Displaced work/state:** remove redundant whole-chain publication and repeated match predicates
  only where equivalent source state already exists; replace unconditional character-heavy pool
  reservation with demand owned by the actual configuration if evidence permits. Do not restore
  cold presentation graphs or displaced renderer machinery.
- **Deletion boundary:** no tolerances, replay-fit clamps, re-recorded stale fingerprints, character
  compatibility flags, fallback dispatch, debug bridges, or retained experimental variants. Every
  performance candidate either passes focused equivalence gates and adjacent benchmarks or is
  removed completely.

## Acceptance

- Original 153-replay domain: zero failures/errors with retained policy; full 366 aggregate: zero
  unclassified failures/errors.
- Focused metadata-preserving PPC checks pass for every touched classification; never use the
  native-lock-only full PPC aggregate as a gate.
- Four-Sheik Final Destination reset passes, and the runtime census covers every publicly admitted
  character across 2/4-player and six-stage configurations without fixed-capacity overflow.
- Common-domain release throughput at 256/512 and lifecycle save/restore return to the comparison
  branch within normal adjacent-run variance, with stable per-ref digests and identical workloads.
- `source-check`, format, native/PPC smoke, Python tests, Wasm/viewer smoke, and complete validation
  selection pass. macOS-only behavior receives static coverage locally and collaborator execution
  on actual hardware.

## Log

- 2026-08-01 — `retained`: review head `479d847d` has one native Samus contact failure, one stale Icies
  PPC follower classification, a four-Sheik fighter-pose capacity abort, +12.85%/+13.54% release
  cycles/frame at 256/512, and +8.45% snapshot/save/restore cost. Mechanical failures are a stale
  source lock, two trailing spaces, an omitted Ice Climbers Makefile filter, inconsistent public
  fighter documentation, and incoherent Rosetta release compile flags. Branch created locally as
  `decomp-port-arm64-ppc-polish`; no remote update is authorized.
- 2026-08-01 — `retained`: pose storage is sized from the admitted player count at 256 joints per
  player, with a 1,024-joint four-player ceiling and a 16-bit capacity. A 16-character, six-stage,
  2/4-player runtime census now exercises that boundary. Four Sheiks consume the observed maximum
  of 1,016 joints; two-player snapshots fall from 686,676 to 659,348 bytes and save/restore return
  to the comparison medians.
- 2026-08-01 — `retained`: the Samus failure was not an mpColl discrepancy. `ftCo_800DA190` read
  `*(s32 *) fp` as the fighter kind, relying on the retail 32-bit leading GObj pointer layout; on
  the 64-bit host that is the pointer's upper half. Reading `fp->kind` restores the source-owned
  Link/Samus/Young Link exclusion. The 15,481-frame focused replay then has no gameplay mismatch.
- 2026-08-01 — `retained`: the Ice Climbers split was not follower collision or fma order. Retail
  `ftCo_800AC5A0` carries caller-register residue for its unassigned CPU-DI bytes when the
  pre-ProcessHit knockback vector is zero; hosted C instead consumed arbitrary uninitialized stack
  bytes. Initializing the local command to neutral removes undefined behavior. Both backends then
  retain only the already-classified item residue, with no follower mismatch.
- 2026-08-01 — `rejected`: a post-solve dynamics refresh on the PPC path and an explicit
  CaptureCut fmadd did not own the Ice Climbers mismatch and were removed completely.
- 2026-08-01 — `retained`: remove the first of two whole-chain dynamics matrix publications in
  `Fighter_ProcessHit_8006D1EC`. The later post-solve publication remains the canonical end-frame
  owner added for retail-probed correctness. Seven exact replays whose locks originally depended
  on dynamics publication remain byte-exact, while adjacent 256-batch sampling improves about
  6--7% against the redundant-publication control; 512-batch sampling is neutral within noise.
  A material common-domain throughput gap to `experiment/decomp-port` remains open.
- 2026-08-01 — `retained`: the sole post-solve dynamics publication now consumes the JObj tree's
  canonical dirty bits rather than forcing every link dirty. The solver's setters already
  propagate dirtiness down each mutated chain. All seven retail-probed locks remain exact.
- 2026-08-01 — `retained`: the larger FObj and class-piece reserves are selected only for a Match
  containing Samus, the documented sole consumer through her grapple. The all-character census
  still reaches the Samus high-water safely; the ordinary two-player snapshot is now 611,420
  bytes, below the comparison branch's 633,156, with faster save/restore medians.
- 2026-08-01 — `retained`: native release compiles the now-widespread `__fmadds` operation as the
  compiler intrinsic it represents rather than an out-of-line call from O0 source-shaped TUs.
  Debug, Python, Wasm, and PPC keep the existing external owner. Seven sensitive locks stay exact;
  extending the treatment to fmsubs/fnmsubs regressed adjacent throughput and was removed.
- 2026-08-01 — `retained`: final alternating common-domain samples close the original
  +12.85%/+13.54% regression. Batch 256 median cycles/frame are 63,765.7 comparison versus
  62,011.2 polished (-2.75%); batch 512 is 48,921.1 versus 49,625.9 (+1.44%). The per-ref digests
  are stable and workloads identical; the remaining spread is ordinary machine contention, not a
  material regression.
- 2026-08-01 — `retained`: final native aggregate after the representation changes has 286 exact
  passes / 80 classified / 0 failures / 0 errors over all 366 replays. The obsolete Samus
  classification is deleted. Twenty-three Ice Climbers snapshots were refreshed only after a
  guarded audit proved their drift was confined to the already-owned item identity/residue lanes;
  the one mixed Dream Land entry retained its byte-identical pre-existing wind-puff fields.

# Previous structural packet — Ganondorf

## Objective

Add Ganondorf (internal kind FTKIND_GANON 25, public/CSS char id 25) to the supported domain.
Ganondorf is the Captain Falcon semi-clone: every special is owned by the already-ported
`ftCa_Special*` TUs (their `FTKIND_GANON` branches select the Ganon gfx/motion variants), and
the only character TU is the Matching `ftGn_Init.c` (motion-state table delegating to ftCa
handlers, strings, item/knockback callbacks). Registry rows, PlGn extraction (five costumes
Nr/Re/Bu/Gr/La + PlGnAJ + PlGnDViWaitAJ), native DAT translation reusing the Captain ext-attr
layout (`ftCa_Init_OnLoadForGanon`/`ftCa_Init_LoadSpecialAttrs` are the shared owners; anim
count 318; `MSL_FIGHTER_ARTICLES_NONE`), derived part-admission row (54 live / 30 cold of 84),
public API/viewer/validation admission, and a 30-replay suite wired into the aggregate.

## Final boundary

- **Owner (character):** the imported `ftGn_Init.c`, byte-identical to the pinned decomp. No
  OnLoad ext-attr mutation (ftCa_Init_OnLoadForGanon only pushes attrs — no DK-style store
  guard needed); no motion/fighter-var pointers (fv/mv `.gn` alias the ftCaptain views).
- **Effects:** EfGnData.dat owns efAsync bank 19 with a real particle bank, but the hosted
  loaders do NOT load it — the Falcon precedent: every Ganon-reachable efSync id
  (0x50B..0x50F -> efLib model creates on bank-19 model ids 0x4A38..0x4A3D) is model-backed
  only, so no generator RNG projection consumes bank-19 ids (EfCaData bank 4 is likewise
  unloaded for Falcon). `MSL_CORE_EFFECT_BANK_CAPACITY` stays 19.
- **Data:** `PlGn*` + `EfGnData.dat`; manifest 146 -> 155 files; no capacity bumps needed for
  the fifteenth character (arena and archive-cache headroom from the DK packet hold).
- **Exactness fix shipped:** the ThrownLw follow `ftCo_800DE508` now ports its MWCC fusions
  (GALE01 0x800DE558/0x800DE56C fmadds both lanes — the same shape as the fused
  ftCo_800DB464 capture-follow twin); identity at current inputs (unit scale), aggregate
  byte-stable.
- **Heap-overflow fix shipped (fighter_pose.c pose-program map):** a FigaTree node with ZERO
  tracks wrote its `track_nodes` map entry at the NEXT node's first-track position; a trailing
  zero-track node in the final program wrote one uint16 past the calloc'd map. Ganondorf's
  PlGnAJ bank is the first with such a tree, so every process that ran the 15-character
  game-data preload corrupted its heap (symptom: `malloc(): invalid next size` aborts in
  later in-process work — pytest EnvBatch tests after an in-process validate_one). The map is
  only ever read at tracked nodes' first-track indices (request_figa early-returns for
  trackless nodes), so skipping the write for zero-track nodes is read-identical: native
  aggregate byte-stable after the fix, ASan-clean end-to-end (debug recipe: ASan-build
  libmelee_core via `make python-library PYTHON_BUILD=/tmp/asan_build NATIVE_OBJ_DIR=...`
  CFLAGS="-O1 -g -fsanitize=address ..."`, stub the ~34 gc-section-dead undefined symbols,
  LD_PRELOAD libasan into python).

## Suite state (container authoritative): aggregate 366 = 285 pass / 81 classified / 0 fail

30 replays (all six stages: 5 FD / 5 BF / 5 FoD / 5 YS / 5 DL / 5 event-verified frozen PS;
opponents Captain Falcon, Falco, Fox, Jigglypuff, Marth, Peach, Samus, Sheik; Slippi netplay +
anonymized ranked + mainline; zero re-wraps, raw pad lanes populated in all 30 zip files):
**container native 25 pass / 5 classified, PPC identical fingerprints on all five.**

**FROZEN-PS POLICY CORRECTED (user-directed):** the Stadium Transformations event (0x41) was
added at Slippi 3.18.0 (refs/slippi-wiki/SPEC.md) and fires on every transformation change, so
zero `stadium_transformation` events over a full game proves frozen for ANY version >= 3.18.0.
The prior "3.18 recorders are silent" note was a wrong inference from v3.18.0 controls whose
non-frozen status came from the start-block `is_frozen_ps` flag — which is only trustworthy at
3.19.0+. Empirical confirmation: the three v3.18.0 zero-event Stadium games in this zip
validate fully bit-exact over 11-13k frames each against the frozen-only hosted Stadium model
(a transforming game would diverge massively within the first ~75-second cycle). The five
classified families:

- `2025-03` @5974: single l_cancel row on Zelda's first post-transform frame (new mechanism id
  `transform-swap-stale-lcancel-byte`): Slippi keeps L-cancel status in the repurposed
  PlayerData+0x25FF byte and Zelda's freshly-swapped block carries stale retail memory (0x50)
  for one frame until the 8006c324 reset hook runs; hosted per-fighter lcancel state starts 0.
  Export-only lane, unmodelable without emulating uninitialized retail memory.
- `24891` @10479: 35-row P1 pos_y episode — Ganondorf down-throws Marth over FoD's descending
  right platform; the `ftCo_800DDDE4` mid-frame release resolve picks a floor line 0.075 below
  retail's (our resolved y 2.0251 vs retail 2.1001 back-computed; the recorded platform height
  1.19994 gives our line at +0.825, and the descending platform line vs the curved stage flank
  sit within 0.075 at that x). pos_x bit-exact throughout; heals at the landing snap. Same mp
  floor-line-pick seed as the dk `auto-dk-2025-04` entry (retail arbitration there showed
  retail == recording). Debug recipe that isolated it: temp trace in ftCo_800DE508 (follow
  inputs) + ftCo_800DDDE4 (release vec / resolve output / live platform heights via a temp
  slippi.c accessor).
- `25827` @3816 (BF x=72.84), `medium-fox` @3819..3822 (YS x=57.27), `medium-sheik` @5451
  (YS x=57.33): the documented open wall-hug clamp 1-ULP pos_x re-derivation family, all
  single-episode self-healing, identical on both backends.

## Follow-ups

- The dk suite rejected three v3.18.0 Stadium candidates under the old (wrong) "3.18 recorders
  are silent" policy; under the corrected policy they are admissible if their
  stadium_transformation streams are empty (verify with the frozen-model validation
  arbitration). Same check applies to any earlier suite's v3.18.0 PS rejections.

## Log

- 2026-08-01 — `merge` (macos-native-build update integrated): the collaborator's rebased
  macos-native-build (deterministic hosted source identities + targeted compatibility flags +
  -fpermissive removal) was linearized onto the original branch tips as
  `macos-native-build-linear` (tree-identical to their tip) and merged. Conflicts: effects.c
  (kept the DK model-only effect-bank path, adopted MSL_LBARCHIVE_END) and this doc. The
  deterministic-token representation shifted item-var pool residues on the four
  `unrecorded-item-pool-residue` icies classifications (item.misc* lanes only — every other
  snapshot stat identical; verified by full pre/post field diffs in msl-luigi); native
  snapshots + 4 output locks re-recorded. Container aggregate 366 = 285 pass / 81 classified /
  0 fail. PPC snapshots for all four verified bit-identical pre/post merge (platinum's
  aggregate-PPC snapshot was already stale beforehand — per-suite PPC remains the gate).
  macOS-native aggregate unchanged (same 122/18/201 with identical drift fingerprints
  pre/post; known non-gate signed-zero/ULP condition).

- 2026-07-30 — `open` (this packet: Ganondorf source validation packet; all 30 zip files
  admitted after the frozen-PS policy correction and wired straight into the aggregate with
  locks + five classifications; aggregate 366 = 285 pass / 81 classified / 0 fail native;
  PPC ganon suite identical fingerprints; fighter_pose track_nodes heap overflow found by the
  15th character's preload and fixed).

# Previous packet — deterministic hosted source identities

## Objective

Remove host-address identity from the native `u32` fields retained by the source-shaped runtime.
Allow GameData and native DAT arenas to map anywhere without making match construction, HSD lookup,
copy, or save/restore depend on ASLR or allocator reuse.

## Final boundary

- **Final owner:** `MslMemoryContext` owns GameData raw-file tokens; `MslNativeDatContext` owns native
  DAT descriptor tokens; the HSD ID API owns conversion from hosted pointer identity to its `u32`
  key. Callers do not manufacture hosted IDs by truncating pointers.
- **Canonical state:** true native pointer fields store full host pointers. A GameData source-width
  file address is a one-based byte offset in its GameData arena. An HSD pointer ID is a tagged byte
  offset in the native DAT arena, Match arena, or core image. Zero remains null and equivalent
  immutable GameData produces identical tokens regardless of mapping address.
- **Consumers:** `lbFile_800168A0` and fighter animation subarchive loading consume GameData tokens;
  native DAT translation, AObj/fighter-pose loading, JObj/RObj/PObj reference resolution, fighter
  metal, ground material, and `HSD_IDTable` consume HSD pointer IDs. Match copy and save/restore carry
  these stable scalar tokens unchanged while relocating only real pointer slots.
- **Displaced state/work:** delete low-32 mapping retries, low-32 pointer reconstruction, fixed/low
  address assumptions, and hosted pointer-to-`u32` casts at HSD ID call sites. No numeric-ID
  savestate relocation table is introduced.
- **Deletion boundary:** no hosted GameData file handle or HSD ID key contains bits from a native
  address. Arena mappings use the ordinary host allocator once, all token decode is range-checked
  at its owning boundary, and there is no low-address compatibility path or fallback
  representation.

## Evidence and acceptance

- Keep two equivalent GameData instances resident simultaneously so their GameData and native DAT
  mappings must differ; destroy the snapshot's source instance, restore into the other, cross an
  animation/HSD lookup continuation, and require exact state equality.
- Preserve the exact native/PPC/Wasm correctness gates and retained replay classifications/locks.
- Compare lifecycle and release replay throughput against the current `experiment/decomp-port`
  baseline under identical build, data, CPU, and benchmark inputs; any retained change must be
  throughput-neutral or better.

## Log

- 2026-07-31 — `open`
  Scope: GameData file handles, native DAT descriptor identities, HSD pointer IDs, and the existing
  cross-GameData savestate continuation.
  Hypothesis: arena-relative tokens eliminate the address-reuse hole and the 64-attempt mmap scan
  without adding gameplay work; the conversions occur during initialization or object creation.
  Evidence: PR #11 currently derives these `u32` values from arena pointer low bits. Typed
  savestate relocation correctly moves full pointers but cannot and should not infer provenance for
  numeric fields, so a restore into simultaneously resident equivalent GameData retains stale HSD
  keys under the current representation.
  Disposition: implement the singular token representation, strengthen the existing recreation
  smoke to force distinct simultaneous mappings, then checkpoint exactness and performance.
  Next: replace low-32 encode/decode and audit every hosted HSD pointer-ID cast before running the
  focused native smoke.

- 2026-07-31 — `checkpoint`
  Scope: complete hosted file/HSD token cut plus simultaneous-GameData restore smoke.
  Hypothesis: every persistent HSD pointer identity belongs to native DAT, Match, or the core image;
  temporary retail descriptor copies must resolve to an equivalent persistent source owner rather
  than acquiring a new runtime registry.
  Evidence: the first native smoke rejected `ground.c::get_jobj_inline`'s stack copy. Its source
  descriptor is the immutable `Ground_803B7E0C`; hosted loading now uses that image-relative owner
  and publishes the copied scale before consumption. No other unowned identity was reached. The
  complete native smoke, strengthened cross-GameData save/restore continuation, and PPC smoke pass.
  `source-check` verifies the canonical inventory; the locked `refs/melee` checkout is not yet
  populated in this isolated worktree.
  Disposition: retain the singular tokens and source-image identity for broader gates.
  Next: populate the local source reference, run source/Wasm/viewer/replay gates, then benchmark the
  exact candidate against `experiment/decomp-port`.

- 2026-07-31 — `experiment`
  Scope: save/restore cost after sharing ELF/Mach-O image-range discovery between HSD identity and
  savestate relocation.
  Hypothesis: the lifecycle target's four restore samples are too small to distinguish a real
  roughly 0.035 ms candidate increase from timer/layout noise. Temporarily raise only its snapshot
  sample count to 256 in the rebased PR control and candidate, rebuild the same debug profile, and
  compare adjacent pinned-core runs; remove the probe afterward.
  Evidence: moving savestate image discovery behind the shared external helper measured 0.6705 ms
  median restore versus 0.6545 ms for the rebased PR control at 256 samples (+2.4%), even after
  deleting its duplicate loader walk. That refactor was rejected. With PR #11's original local
  savestate discovery restored, adjacent candidate/current-branch medians are 0.6595/0.6580 ms
  restore and 0.4775/0.4780 ms save; snapshot size remains exactly 633,156 bytes.
  Disposition: retain deterministic tokens but leave the measured savestate path and layout intact;
  keep source-image lookup isolated to cold HSD identity construction.
  Next: remove the temporary 256-sample probe and rerun the ordinary lifecycle/correctness gates.

- 2026-07-31 — `checkpoint`
  Scope: final production throughput and lifecycle comparison against `experiment/decomp-port`.
  Hypothesis: deterministic token conversion remains outside gameplay and does not regress the
  resident 256/512 production profiles or copy/save/restore.
  Evidence: six alternating release runs preserve both benchmark digests. Median cycles/frame move
  47,900.1 to 47,326.3 at 256 (-1.20%) and 43,673.2 to 43,922.8 at 512 (+0.57%, within run/layout
  variance). The high-resolution lifecycle probe is neutral against current for save, restore,
  artifact size, initialization, and stepping. Exact commands and medians are retained in
  `agent_docs/performance/HISTORY.md`.
  Disposition: retain the identity representation as performance-neutral; make no speedup claim.
  Next: run final native/PPC/Wasm/viewer/replay gates on the exact commit candidate.

- 2026-07-31 — `checkpoint`
  Scope: hosted compiler portability at the archive variadic boundary.
  Hypothesis: PR #11's Apple-only low-word terminator check masks a caller/callee type mismatch and
  should be replaced by the pointer-width sentinel that the hosted loader actually consumes.
  Evidence: a fresh Linux Clang 18 build reproduced the upper-word failure during Fox costume
  loading (`symbol=0x7fff00000000`). Converting every archive-list terminator to
  `MSL_LBARCHIVE_END` on hosted builds removes the mismatch; the same fresh Clang native smoke now
  passes. The 32-bit source build retains its literal-zero ABI.
  Disposition: retain the typed hosted boundary and delete the Mach-O-only low-word heuristic.
  Next: rerun the exact final native/PPC/Wasm/viewer/replay gates and commit the local stack.

- 2026-07-31 — `retained`
  Scope: final deterministic hosted identity and compiler-portability boundary.
  Hypothesis: the cleanup must preserve every supported replay lock and production digest while
  allowing equivalent GameData owners to coexist at unrelated host addresses.
  Evidence: simultaneous-GameData save/restore, GCC and Clang 18 native smokes, PPC smoke, source
  lock, Wasm parity, live Chrome viewer, and formatting all pass. Debug and optimized-release gates
  both remain 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames.
  Final 256/512 release samples preserve digests `8ef126a41244d514` / `6f91f23e3553a090` at
  47,259.0 / 43,710.2 cycles per frame, inside the retained A/B envelope.
  Disposition: packet complete and ready for its local implementation/evidence commit; no remote
  branch has been updated.
  Next: verify the native macOS profiles on Apple hardware before final rebase merge.

- 2026-07-31 — `retained`
  Scope: merge-review reduction of source-image IDs and hosted compiler workarounds.
  Hypothesis: image-base equality is sufficient for the encode-only static-image owner; native DAT
  is the only inverse-ID consumer. Explicit hosted return values and volatile rounding slots can
  remove blanket diagnostics and undefined writes without changing source/PPC behavior.
  Evidence: the callsite audit found one static-image encoder and two inverse consumers, both native
  DAT descriptors; Clang exposed two legacy return diagnostics hidden by the global suppression.
  GCC, fresh Clang, PPC, Wasm/viewer, and both 153-replay gates pass. Adjacent release medians are
  neutral-to-better at 256 and 512 environments with both production digests unchanged.
  Disposition: retain the narrower decoder, defined hosted spill, and explicit hosted returns; the
  source/PPC paths remain unchanged and the general image-range module is deleted.

# Previous packet — Donkey Kong

## Objective (closed)

Add Donkey Kong (internal kind FTKIND_DONKEY 3) to the supported domain: the thirteen Matching
chara TUs (`ftDk_Init/SpecialN/SpecialS/SpecialHi/SpecialLw`, the seven `ftDk_Heavy*` cargo-hold
states, `ftDk_MS_345_0`), registry rows, PlDk extraction, native DAT translation
(`MSL_FIGHTER_ARTICLES_NONE` — DK spawns no articles), part-admission row, effect bank 8,
public API/viewer/validation admission, and a 27-replay suite (staged standalone, not yet in
the aggregate). The cargo carrier/victim common owners (`ftCo_Cargo*`, `ftCo_Shouldered`) were
already ported; DK is their first live consumer.

## Final boundary

- **Owner (character):** the imported ftDonkey TUs, byte-identical to the pinned decomp except
  the OnLoad idempotent-store guard (ledger `canonical:donkey-onload-attr-store-guard`): retail
  writes the three cargo-walk anim durations into the shared PlDk ftData ext-attr on every
  fighter load; GameData preload performs the first mutation before the native DAT arena seals
  read-only, later Match constructions recompute and skip the identical store (ftPe_Init_OnLoad
  precedent). DK motion/fighter vars carry no pointers — neither Pikachu bug class applies.
- **Effects:** EfDkData.dat is MODEL-ONLY — both leading effDonkeyDataTable words are
  unrelocated NULLs and retail efAsync_LoadSync skips psInitDataBank entirely; both hosted
  loaders now publish an empty command bank for that shape (ledger
  `canonical:model-only-effect-bank`). All DK efSync ids (0x4C6..0x4CC) are pure
  efLib model creates — no generator RNG cases needed.
- **Data:** `PlDk*` (Nr/Bk/Re/Bu/Gr — five costumes) + `EfDkData.dat`; manifest 146 files.
- **Capacity:** archive cache 3072 -> 4096 entries; shared game-data arena 64 -> 96 MiB
  (fourteenth character).

## Exactness fixes shipped with the packet

- **Pose-table dropout resync** (ledger `canonical:fighter-pose-table-dropout-resync`): the
  native pose engine re-derived track time from a fractional absolute frame when dropping from
  the integer-frame table fast path to fractional-rate decoding (cargo walk anims switch rate
  every frame). The decoder now resyncs at the recorded last table frame (integer wait
  subtraction is exact) with a dry walk, so the following rate step reproduces the source
  engine's incremental rounding bit-exactly. Root-caused from a 1-ULP carry-bone fork (victim
  XRotN is ROBJ-constrained to DK's TransN2, publishing the animated spine chain directly into
  pos). Three DK replays became fully bit-exact; two more dropped their native-only rows.
- **HSD_FMod wrap fnmsubs** (`canonical:anim-loop-wrap-fnmsubs`), **capture-follow /
  shoulder-timer / Hand Slap fusions** (`canonical:capture-follow-fmadds`) — retail-faithful
  boundaries, identity at current inputs.

## DK tie triage (2026-07-30, user viewer report — RESOLVED, no sim change)

The horizontal tie in the live viewer is not produced by the sim. Verified from extracted
data: DK's only dynamics chain is the tie (parts 66-71, rooted under chest part 26); NO
hurtbox rides it (hurtbox bone 73 attaches at part 3, near the hip) and NO subaction Create
Hitbox anchors on it (hitbox bones: 0,4,7,17,25,29,30,31,43,44,51,52,53,62). The headless
policy in ftdynamics.c therefore correctly skips its dangle solver (same class as Luigi's
hair and the capes), and the bit-exact corpus confirms zero gameplay impact. The on-screen
body outline comes from slippilab's donkeyKong.zip per-animation 2D frames (the sim supplies
only action/frame/pos/facing + hitbox capsules), and those outlines were generated without
dangle physics — the stiff tie is baked into the asset and appears in slippilab's own viewer.
If presentation parity ever matters, the lever is regenerating the zip frames, not the sim.

## Session 4 (2026-07-30): pre-fusion-era audit — Tier A ports

Corpus-wide inventory (grep fused ops per asm TU vs __f* markers per C TU) found the
pre-fusion-era gap set. Ported with per-site asm verification (126 sites): Sheik chain (35),
Ice Climbers belay string (28), Pikachu QA/Skull Bash/jolt spawns (18), Sheik
needles/chain-charge/Vanish (12), Zelda Din spawn/Farore (7), Fox reflector turn (3), Icies
squall/belay/spawns (11), ftcommon hitlag/nudge/grab-timer (12). Native aggregate byte-stable
throughout (identity at current inputs); PPC pool-residue-fed lanes shift where
backend-specific residuals are inexact (re-recorded).

REMAINING INVENTORY (asm-vs-marker gaps, supported-domain candidates, unported):
ftCo_Guard (8), itcoll (8), fighter.c (5), lbcollision (7), mplib (10 — likely inline
double-count), ftCo_0A01 (75 — needs per-fn reachability triage: DeadUpStar etc.),
Peach/Mario/Luigi SpecialS (2/2/1), Samus SpecialN/Lw_1 (2), items: din fire (3+2), toadspore
(2), missile/chargeshot (2), sword/dosei/foods/freeze (4), heiho (4), iteffect (3), it_26B1
(4), itmaplib (6), item.c (1); ft small TUs: ftCo_09F7 (6), JumpAerial (4), DamageIce (4),
Damage/DamageFall/Throw/Thrown/TurnRun/CaptureCut/Bury/FlyReflect/DamageBind/DamageSong
(2 each), ftpickupitem (3), ftlib (2), ft_07C1 (8), ft_0D4D (6), ft_0899/0C31/0D27/0DF0,
ftCo_800C7590/7CA0; infra to triage per-function: lbvector (46), spline (58 — verify the
session-6 port shape), mtx (98 — display vs gameplay split), grlib (8), grizumi (2),
grpstadium (1), ground (1), groldpupupu (4), jobj (1). Skipped as unreachable: itlinkhookshot
(113, Link), ftcpuattack (46, in-game AI), pobj/lbbgflash/ftafterimage/camera render, itzako.

## Session 3 close (2026-07-30): THE ROLLOUT FUSION SET — aggregate 336 = 260 pass / 76 classified / 0 fail

The "cargo-walk" family was misattributed: the diverging player is P1 JIGGLYPUFF in
ftPr_MS_SpecialNTurn (351) — Rollout pinned at FD's edge, turning, speed decaying. The rollout
TU carried FOURTEEN unported MWCC fusions (ftPr_SpecialN.c predates the fusion discipline):
four Turn_Phys gr_vel accumulates (0x80140934-family — THE seed), four charge-spin roll-angle
steps, two turn-spin steps, and the bounce-dust half-height (inline x4). Porting them retired
the 22-row DK family to one wall-hug pos row, promoted classified puff
Game_20260318T101341 to fully bit-exact BOTH backends (entry retired), and shrank
rollout_ends_ys 64->2 and specials 849->817. All re-records native≡ppc; locks refreshed;
the mislabeled DK classification rationale corrected.

## Session 2 close (2026-07-30): suite IN THE AGGREGATE — 336 = 259 pass / 77 classified / 0 fail

All seven residual families classified with native+PPC snapshots (identical fingerprints both
backends). Retail engine-dump playback arbitrated two of them (auto-dk-2025-04 grapple-swing
fork and the 9560 wall-hug episode): retail reproduces the RECORDING, so these are hosted
ULP residuals, not recorder profile — the wall-hug clamp's pos_x re-derivation
(mpLib_8004E398/511A4 X-at-Y + mpcoll hug candidates; inputs bit-equal, output 1 ULP) is the
documented open lever, alongside the known Samus grapple-chain integration. Trace toolkit
used: bracketing pos probes around coll_cb + an auto-instrumented MPX dump of every
mpcoll cur_pos.x writer (site L2419 = the LeftWall hug clamp) — rebuild from this description.
Fused retail-faithfully along the way (identity at current inputs): throw-release velocity
blend (0x8006BC7C/90), walk-cycle wrap (0x800E0010), plus session 1's HSD_FMod/capture-follow
set. dk.json joined include_suites with 27 output locks; tests updated to 336.

## Suite state (session 1 close, container authoritative)

27 replays (all six stages; Fox/Falco/Marth/Sheik/Jigglypuff/Samus/Captain Falcon; Slippi
netplay + anonymized ranked + mainline; both Pokémon Stadium entries event-verified frozen,
three v3.18.0 PS candidates rejected — 3.18 recorders are silent on transformation events):
**container native 20 pass / 7 fail; PPC 20 pass / 7 fail — identical failures and
fingerprints on both backends.** The seven shared families:

- `auto-dk-2025-04` @8654: 150 rows, Samus offstage low on FoD, a position resolve (wall-line
  pick on the curved underside?) shifts ~0.017 then heals — the one non-ULP family; needs a
  retail probe before classification.
- `slippi-2025-04_..152714` @2663: DK walk-speed ULP family (speed_air/ground_x_self 16 rows +
  pos_x tail).
- Five 1–6-row self-healing pos ULP families at stage-edge positions (22123 @7897, 9560 @2120,
  basic-dk-2025-04 @7265, medium-marth-0912 @7060, slippi-..141746 @9611).

(Superseded by session 2 above.)

## Log

- 2026-07-30 — `open` (this packet: DK source validation packet, suite staged standalone;
  aggregate re-verified 239 pass / 70 classified / 0 fail native after re-recording the two
  icies pool-residue analogs shifted by the arena growth — same ripple class as the Pikachu
  packet).

- 2026-07-30 — `closed` (samus suite extended 12 -> 25 from ~/SSBM/Replays/Samples/samus_replays.zip;
  aggregate 309 = 239 pass / 70 classified / 0 fail)
  **Event-based frozen-PS policy shipped**: PS admission now requires recorder version strictly
  newer than 3.18.0 and zero `stadium_transformation` events over the full game, ignoring the
  game-start `is_frozen_ps` flag. Validated against transforming controls (ics.zip v3.19 PS
  games emit 13/27 events; two v3.18.0 non-frozen PS games emit ZERO over 3-5 minutes — at
  3.18.0 exactly the recorder is silent, so absence proves nothing there). Both admitted PS
  captures (medium-fox-2025-10, medium-sheik-2026-03, v3.19.0) show zero events over ~14k
  frames.
  **Re-wrap rejection class established (5 of 18 zip files rejected)**: one PS capture at
  v3.18.0 exactly (Nicki, flag also false); one 2020 game and three anonymized ranked FoD
  games re-wrapped to the 3.18 layout — payload sizes match 3.18 but raw_analog_y and/or raw
  c-stick lanes are all-zero (genuine captures show thousands of nonzero samples) and gecko
  lists are 2.8-4.9KB vs the genuine 57KB. The three ranked re-wraps fork discrete lanes
  (action_id/facing) during the countdown under EVERY era-flag combination — the destroyed
  raw pad lanes feed the UCF ring machinery, unmodelable by flags (first fork signature was
  the cardinals one: retail speed 1.28375 = 1.3 x raw 0.9875 stick our cardinals patch snaps
  to 1.0; flags moved matched 90 -> 101 only). The existing bit-exact diamond-platinum ranked
  captures carry their TRUE older versions (peppi doesn't expose the newer lanes at all) —
  version-vs-populated-lane disagreement is the re-wrap tell.
  **FObj reserve fix (arena abort)**: medium-sheik-2026-06 (DL Sheik/Samus) aborted post-seal
  (`HSD_ObjAllocAddFree` size-64 grow at used=686912). Backtrace (new glibc-gated
  backtrace_symbols_fd dump on the msl_memory_alloc failure path, kept): Samus AIR
  grapple-catch (ftCo_AirCatch_Anim -> it_802B7C18 -> it_802B743C) runs HSD_JObjAddAnimAll
  across the doubled beam link chain, one FObj per track per link jobj, crossing the former
  256-slot reserve -> now 512 (scalar.c pre-seed block). Replay then bit-exact both backends;
  no classified fingerprint moved (pool growth is layout-neutral for the corpus).
  **PPC snapshots recorded for the two samus classified entries** (icies-precedent
  completion): fox-d18-2026-04 PPC residual is ONLY the 1-ULP pos_y @7389 row (the @9075
  contact-gate flip is native-only, consistent with dolphin-ulp-contact-gate); master-samus
  PPC keeps the taut-rope fork @15146 (390 rows vs native's 135). master-peach is PPC-exact
  (its edge-stop flip is native-only). Digest key trap: compact snapshots store
  `mismatch_fields_digest`, not a digest under `mismatch_fields` — the wrong key silently
  reads as raw fields and reports drift/FAIL.
  **Stale-PPC-binary trap**: build/melee_core/ppc/melee-core-ppc predated the Pikachu port and
  every suite replay errored with the supported-character banner; `make ppc` first.
  Gates (msl-luigi container): samus suite native 22/3/0, PPC 23/2/0; aggregate native
  239/70/0 with output locks appended for exactly the 13 new replays; pytest 44 passed
  (suite-inventory assertions bumped 296 -> 309, filtered cases stay 133 since every new
  replay contains Samus).
- 2026-07-29 — `retained` (session 3: both residual fails dispositioned; pikachu.json JOINED THE
  AGGREGATE — 296 = 226 pass / 70 classified / 0 fail)
  **master-master root-caused as a recorder-profile residual**: the drifting FoD items are Fox
  LASERS (kind 54; the static kind-74 item is his blaster gun) whose spawn X rides the
  RThumbNb muzzle transform (ftFx_SpecialN_FtGetHoldJoint -> lb_8000B1CC with offset
  (0, 1.2325, 4.2636)); every flight all game is offset ~2^-16 with the fraction preserved by
  the laser's integral +7.0/frame velocity. A new MSL_MUZZLE_PROBE in the engine-dump Dolphin
  (lb_8000B1CC entry filtered on that offset vector, dumping the out vector and the full
  ancestor jobj chain) showed retail interpreter playback of this exact recording reproducing
  OUR bits exactly at frames -24 and 78 — every rotate/scale/translate/matrix word of all 12
  chain links identical. The recording disagrees with its own canonical re-simulation: the
  anonymized ranked capture records Dolphin without its build/execution profile. Classified
  dolphin-ulp-fighter-and-item-profile (bounded laser pos/vel intervals + derived angle bytes +
  reconverging fighter-pos ULP intervals; all discrete lanes exact; native/ppc fingerprints
  identical: 040cf4c553e20933, 9,734/12,233).
  **slippi-2025-11** classified dolphin-ulp-motion-profile (one 1-ULP wall-ride pos_x row,
  3,711 strict suffix, native/ppc identical: 72651ec5c1e33764).
  Wiring: pikachu.json added to melee_core_aggregate include_suites (271->296); 25 output locks
  recorded (271 existing locks byte-stable; the two icies locks re-recorded in session 1 stay
  put); tests updated (296 pins, manifest set, classified-loader suite list, coverage
  parametrize). Container full pytest 44 green (incl. ppc-snapshot verification); Mac pytest +
  source-check green. Trap note: the container clone's git HEAD is the stale clone base —
  diff lock files against the MAC HEAD, not the container's.
  The Pikachu port is at the endgame state: 23/25 bit-exact on both backends, 2 classified,
  suite in the aggregate.

- 2026-07-29 — `retained` (session 2: extended suite to 25 + the phantom-revisit decomp fix)
  Scope: the 12 remaining usable batch replays joined pikachu.json (25 total; three anonymized
  ranked now, the v3.15/v3.16 pair carrying cardinals=false — both bit-exact, which isolates
  master-master's fork from the pre-cardinals flags). Container native 23/25, PPC 23/25,
  identical fails.
  **Fix (ledger canonical:phantom-revisit-hit-gate)**: 40830 (DL Pikachu/Falco) forked at 3702 —
  our sim landed a full usmash hit one frame before retail. Retail collision probe
  (MSL_COLLISION_PROBE on the engine-dump Dolphin) showed the frame-3702 capsule sweep
  BIT-IDENTICAL to ours (two same-group grazes, coll_distance 0.0032/0.0092, both phantom-range)
  with ftColl_80076ED8 returning (1, 0) where ours returned (1, 1). GALE01 asm
  0x8007702C..0x80077444: a phantom-range contact whose victim is already in the hit group's
  victims_2 registry (populated by the first phantom's inlineB0 across ALL same-group hitboxes)
  returns FALSE; the NonMatching decomp fell through to the full-hit path instead. One-branch
  fix; the sibling item path (ftColl_80077C60) was already faithful. 40830 → bit-exact 8,892;
  aggregate 203/68/0 byte-stable (the bad branch never fires in the 271-replay corpus);
  container pytest 43 green.
  Remaining fails: (a) master-master (FoD ambience-item ULP from -24, both backends,
  next session's target — the icies FoD construction-RNG/grIzumi toolkit applies);
  (b) slippi-2025-11 = ONE 1-ULP pos_x row (fast-falling Falco hugging the YS right underside
  wall, pos re-derived from the wall line each frame; 3,711-frame exact suffix; native and PPC
  bit-identical) — meets the dolphin-ulp-motion-profile bar; classify when pikachu.json joins
  the aggregate (a classification entry for a non-aggregate suite breaks
  test_native_validation_compares_complete_classified_replays' case lookup).

- 2026-07-29 — `retained` (session 1: the full packet, three hosted-portability fixes)
  Scope: full character packet + 13-replay suite; container gates green.
  1. **Thunder motion-vars pointer widening** (ledger pikachu-thunder-vars-pointer-widening):
     four replays segfaulted (ditto + all three mainline) — `specialhi.x4 = 1` writes landed
     in the widened `speciallw.x0` high half (0x1_00000000 dereferenced in it_802B1FE8), and
     enter-time pointer zeroing flowed through the `specialhi.x0` alias. Fixed with a hosted
     speciallw layout (flag kept at its mv+4 alias position, full-width pointer past the
     aliased pair) + two named-owner zeroing sites in ftPk_SpecialLw.c.
  2. **Jolt item-var native sampling** (scalar.c item_var_source_byte): the ground/air jolt
     structs lead with pointers, so raw retail-offset sampling read padding on 64-bit hosts —
     the whole item.misc family across 7 replays (expected float bytes vs constant 0/1).
     Pikachu cases added per the Chain/DinFire/icies precedent; this promoted the suite from
     4 to 12 native passes.
  3. **Aggregate fingerprint re-record**: the wider archive arena + grown Fighter struct
     shifted deterministic pool-residue analogs on two icies classified replays (medium-fox
     2025-08, ys-fox 2025-07) — native mismatch stats identical, fingerprints moved;
     re-recorded native snapshots + 2 output locks (PPC snapshots verified unchanged).
     Aggregate 271 = 203/68/0; container pytest 42 green; Mac pytest green; source-check
     159 deltas.

# Prior structural packet — Ice Climbers (Popo + Nana follower entity)

## Objective

Add Ice Climbers (external char, internal kinds FTKIND_POPO 10 / FTKIND_NANA 11) to the supported
domain. Unlike every prior port, one player slot owns TWO fighter entities: Nana is spawned
alongside Popo by the already-imported `Player_80031AD0` follower branch and is driven by the
retail CPU input block (AI type 6: record Popo's pads into a 30-slot ring, play back 5 frames
delayed, with follow/approach fallback). The packet has four owners: the character/article
sources, the fighter-axis widening of match/observation/compare state, the CPU input subsystem
slice, and follower-frame validation.

## Final boundary

- **Final owner (entities):** the imported `src/melee/pl/player.c` dual-entity machinery
  (`player_entity[2]`, `ftMapping_list[CKIND_POPONANA]`, `Player_8003248C` returning
  `Gm_PKind_Cpu` for the non-transforming second entity) becomes live. The runtime stops assuming
  fighters ≡ players: `MslCoreMatch` tracks per-player leader and follower fighters; every
  consumer that iterates fighters walks both entities of a slot.
- **Final owner (Nana's brain):** the retail CPU input path. `ftCo_800A2040` (headless stub
  `false` today) becomes the real predicate; the reachable AI slice — `ftCo_800B3900` tick chain,
  `ftCo_800B101C` type-6 think, `ftCo_800B0918`/`ftCo_800B0AF4` ring record/playback,
  `ftCo_800A589C` partner getter, command-execution helpers, and the `ftcpuattack.c` slice the
  type-6 graph reaches — is ported into the existing `src/runtime/ftco_0A01.c` projection or a
  full upstream import (decide at import time by measuring the reachable closure; upstream TU is
  NonMatching, so every ported function is verified against `refs/melee` asm individually).
- **Canonical state:** Nana's input state is `Fighter.x1A88` (`Fighter_x1A88_t`, 0x57C, already in
  `types.h`) exactly as retail — no hosted mirror, no synthesized controller lane. Inputs from the
  wire stay 4-wide per player; Nana never reads `HSD_PadGameStatus` and never touches the UCF pad
  ring (retail-natural: the AI branch skips the physical-pad block in
  `Fighter_Spaghetti_8006AD10`).
- **Consumers:** fighter creation/respawn (`scalar.c` bootstrap, `match.c` respawn), post-frame
  refresh, native DAT preload (already iterates entity 0..1), observation/output lanes, savestate
  copy, viewer live schema, and validation compare all address (player, entity∈{leader,follower}).
- **Displaced code/state:** the entity-0-only assumptions in `scalar.c:1074`/`:1804` (fighters[] =
  index 0, kept only for the Sheik/Zelda swap); the `ftCo_800A2040` false stub and the
  `ftCo_800B3900` abort row in `src/stubs/headless_exclusions.c`/`ledger.tsv`; validation's
  leader-only descent in `tools/validation/native.c:load_player`.
- **Deletion boundary:** no compatibility flag for "ICs without Nana", no synthesized Nana input
  lane in the wire format, no duplicate Nana state outside `Fighter`/`StaticPlayer`. The public
  input API stays `players[4]`; the observation/compare structs gain follower lanes in one cut
  (Python dtypes, native.c, wire.h move together).

## Sequencing (each step gates green before the next)

1. **Data plumbing:** `PlPp`/`PlNn` prefixes + `EfIcData.dat` in `tools/data/extract.py` and
   `raw.py`; re-extract from `./SSBM.iso`; manifest/test counts.
2. **Character packet (compile + spawn):** import 8 Matching chara TUs (`ftPopo/*.c` ×5,
   `ftNana/*.c` ×3) + 3 Matching article TUs (`itclimbersice/blizzard/string.c`), ftdata.c hosted
   registry rows, native DAT translation rows, `derive_gameplay_parts_mask.py` rows for both
   kinds, effect bank, char id 10 through core enums (public API admission deferred to step 5).
   `source_character_kind` maps id 10 → `CKIND_POPONANA` (first 1→2 expansion).
3. **Fighter-axis widening:** leader+follower fighter tracking in `MslCoreMatch`, respawn/death
   (`gm_80167320(slot, subchar)` already wired), UCF gate, savestate, observation lanes.
4. **CPU slice port:** un-stub `ftCo_800A2040`, port the type-6 reachable closure, asm-verify per
   function (NonMatching upstream).
5. **Validation + suite:** follower descent in `native.c` (peppi `ports.P{n}.follower`), follower
   compare lanes and life-cycle row carry (rows vanish while Nana is dead), public API/python/
   viewer admission, stage the icies suite (11 usable candidates in
   `~/SSBM/Replays/Samples/top14.zip`: FD/BF/YS/FoD/DL ×2 each; both PS games non-frozen,
   excluded), container aggregate gate.

## Evidence and acceptance

- Retail behaviors that must fall out of the port, not be re-implemented: Popo death kills Nana
  (`ftCo_800BFD9C`); belay partner tether (`checkNanaInRange`, rope item 113); squall sync
  (`ftNn_Init_80123954` kinematic slaving); Nana's percent-derived CPU level
  (`min(popo_percent/20, 9)`); Nana motion-table fallback to Popo (`ftData_80085FD4`).
- Acceptance: container aggregate stays 0 fail with all previously-passing replays byte-stable;
  icies suite validates with follower rows compared; full pytest green; no wire-format second cut.

## Log

- 2026-07-27 — `retained` (step 2)
  Scope: character packet committed as 5d0d3858 (11 TUs, registry rows, DAT translation, parts
  masks, extraction, effect bank 14, preload with Nana costume coverage).
  Evidence: native-smoke green including the Popo parts row; triage harness steps ICs-vs-Fox 600
  frames, Nana spawns as player_entity[1] and idles. One native delta (ledger
  nana-anim-leader-share-native): the ftData_80085CD8/E50 leader-share branch is excluded under
  MSL_CORE_NATIVE (x59C never materializes natively; x14 keys the same immutable subarchive).
  UCF finding: msl_ucf_apply_pad_buffer sits in the COMMON input path mirroring retail hook
  0x8006B460, so Nana running it is retail-faithful — no gate needed once the CPU predicate is
  real.
  Next: CPU TU import (ftCo_0A01.c + ftcpuattack.c wholesale, projection reconciliation,
  stub triage) — delegated; then fighter-axis lanes and follower validation.

- 2026-07-27 — `retained` (steps 3-5 infrastructure)
  Scope: CPU TU import committed (14de9dc2); follower compare lanes cut through
  wire.h/scalar/native.c in one step (MslCoreCompare 1022→1302, 28 follower lanes + presence);
  peppi follower descent with independent life-cycle row indexing (rows null during Nana's
  Sleep, resume at Popo's respawn — measured); ICs article registry rows fixed in items.c (NULL
  state-table segfault); public API/python/viewer admission; 12-replay icies suite staged.
  Evidence: container aggregate 183/63/0 under the new wire with all 247 output locks
  re-recorded; icies suite runs end-to-end (12/12, zero errors, four-climber ditto included),
  every entry diverging at frames 83-204 in follower lanes — the untuned Nana start-of-match AI
  decision stream.
  Next: Nana AI fidelity (retail probe of the x1A88 decision stream at the first divergence,
  fd-falco @-39 class), then classify/lock the suite and wire it into the aggregate.

- 2026-07-27 — `retained` (first Nana fidelity seed)
  Scope: session 2 traced the universal frames-83..204 follower divergence to the hosted UCF
  pad-buffer injection: it replaced Nana's AI stick with the port's cardinal-snapped raw pad and
  double-shifted the ring. The real gecko wraps its whole body in !Player_IsCPU, and Player_IsCPU
  IS ftCo_800A2040 (GALE01r2 map) — the predicate the CPU import made real.
  Evidence: with the one-line gate, container icies prefixes moved 83-204 → 95-1,416
  (medium-sheik: 21s of bit-exact two-climber play); aggregate unchanged (gate is vacuous for
  humans). Next layer, all classic per-mechanism families: itclimbersice SendItemInfo misc lanes
  (192/0 vs 64/255 @-27 bf-falco), 1-ULP Nana pos_x @98 fd-falco (fusion site), a crouch-mimic
  action pick @26 bf-falco.

- 2026-07-27 — `retained` (session 3: three source fixes, rollback boundary identified)
  Scope: (1) ftCo_800ADE48 upstream decomp had the blast-zone bounds branch INVERTED against
  GALE01 asm (800ADF50: beq skips the reset for found in-bounds targets; the C reset them) —
  every valid CPU navigation target was clobbered to self-floor, which kept Nana inert during
  the entry fall (retail general-CPU steers her toward Popo's floor position with
  LstickXTowardDestination 0x7F from frame -123 until mimic engages at landing). The sibling
  bounds-check sites (A3908 ×3, A4038 ×3, A648C, B33B0, A21FC, A8210, AE7AC, inlineD0s) were
  swept against asm and are faithful. (2) ftCo_800B0AF4's x2225_b3 mimic position lerp is a PPC
  fmadd (0.05*ring rounded once, then fused 0.95*cur + t before frsp); the C's two rounded
  products drifted 1 ulp — the whole follower_pos_x 1-ULP family. Fixed with __builtin_fma per
  the lbcollision.c precedent. (3) item_var_source_byte gained native pointer-widening cases for
  It_Kind_IceClimber_Ice (offset 3 = owner GObj low byte, 7 = live scale f32) and GumStrings
  (7/0x17 = link/joint pointer low bytes); 0x1B and ice 0x17 are pool residue.
  Evidence: container icies first-mismatch fronts moved from -39/-27-class to per-replay
  engage-adjacent fronts; e.g. ys-fox prefix 261→4,258, gm-peach first -39→378 (item.vel ULP),
  medium-sheik 1,416→316 (engage-front reshuffle). Aggregate re-verified vacuous.
  Boundary finding: the residual follower fronts are one-frame stick skews at mimic
  engage/disengage boundaries where the recorded Nana pre-joystick is a ring slot one NEWER or
  one OLDER than the (asm-verified) cursor arithmetic can produce from the finalized Popo rows
  (bf-marth @12 = popo@7 under steady d6; ics-ditto @187 = a -125 flick-transit sample where the
  straight-line ring holds -128; both directions occur). Every candidate mechanism (ring cursor
  code, B101C/B2AFC/B3900 call orders, UCF gecko rewrite timing vs the 0x8006B0E0 record hook,
  A17E4 quantizer) was verified against asm/refs and eliminated: the recordings are netplay
  rollback captures, and Nana's mimic ring is the first supported state whose content depends on
  input ARRIVAL timing, which straight-line re-simulation of finalized rows cannot reproduce.
  Disposition: treat engage-boundary skews as per-replay classifications (rollback-recording
  owner) unless a Dolphin playback probe shows Slippi playback itself reproduces the recorded
  follower rows; remaining fixable fronts are the ice-block item-motion ULP family (gm-peach
  @378) and the fod-sheik @297 item stream.

- 2026-07-27 — `retained` (session 3 continued: UCF follower retro-write, probe loop established)
  Scope: the bf-marth @12 engage-front was NOT a rollback artifact. A new Dolphin interpreter
  probe (slippi-dolphin engine-dump branch, MSL_NANA_RING_PROBE_*: logs the B0918 capture call
  and the B0AF4 playback exit with ring cursors and slot content) showed the slot recorded at
  slippi 6 with the -86 flick-transit sample being read back at slippi 12 as -128: UCF's
  dashback gecko (refs/ucf/src/dashback/dashback.cpp, Interrupt_AS_Turn+0x4C) retroactively
  rewrites the paired sub-character's freshest mimic-ring slot (full-deflection stick + new
  facing) when the leader's dashback converts. Ported into msl_ucf_apply_dashback (8a191e85);
  bf-marth prefix 134→179. Probe frame anchor: probe/dump frame N = slippi N; sim tick trace
  t = slippi + 132; ring capture is one frame stale (prio-2 think before prio-3 input
  processing), steady playback delay is 6 frames.
  Boundary of the next front (ys-fox @14, medium-sheik @194, bf-falco @71 class): retail Nana
  sits DISENGAGED in general-CPU state x18=10 emitting stick 0 while Popo airdodges (probe
  shows xFA mim bit clear, x18=10 constant); our sim demotes x18 10→1 at the frame Popo's
  stick returns to neutral and then walks toward the surviving x54 target (ftCo_800ACD5C's
  early branches differ). Islands are live natively (probe: non-NULL island, A2718=0) and x54
  correctly tracks Popo's airdodge floor in both. Suspect surface: the scene-keyed hosted
  mirrors consulted by ACD5C/ADE48 — gm_8016C75C standings-cache value at match start (x88/x8C
  timer arming) and gm_801A4310 major-scene id vs 0x1C — plus ftCo_800A21FC/A3554 gates.
  Next: probe gm_8016C75C/gm_801A4310 returns retail-side during the ys-fox window, align the
  hosted mirrors, then reassess which residual fronts remain for classification (item misc0
  pointer bytes, misc2/3 pool residue, item-motion ULP family per the dolphin-profile
  precedent).

- 2026-07-27 — `retained` (session 3 continued: CPU size-field publication)
  Scope: the state-10 front's root cause was NOT the scene mirrors: the retail probe showed
  ftCo_800A3554 passing with x38 ~= 8.9 (breathing per frame) against our 2.0. x38 comes from
  the partner's x1A88.x564 hurt-capsule half-span, published every frame by ftCo_800A0DA4 in
  retail; the hosted build had substituted the dynamics-only capsule publication and never
  wrote x55C/x560/x564/x568 (dead state before Nana). Fixed in 59b0af15: run the full retail
  publication whenever a CPU-input fighter exists (msl_fighter_cpu_input_live), keep the cheap
  path for human-only matches. Container icies prefixes moved dramatically: ys-fox 269 -> 5,694,
  bf-marth 179 -> 6,489, auto-falco 256 -> 5,731, medium-sheik 316 -> 2,643, ics-ditto
  309 -> 2,269; master-fox unchanged (@147 damage-exit family).
  Tooling note (user-reported): the extracted probe driver's `[DSP] Backend = NullSound` is an
  invalid backend name in the engine-dump Dolphin and silently falls back to the audible
  default; the correct silent config is `Backend = No Audio Output` + `Muted = True` +
  `Volume = 0` (fixed in the working copy; apply when tools/dolphin merges to a branch).

- 2026-07-27 — `retained` (session 3 finale: PlCo CPU table translation, 76c44b50)
  Scope: the B4AB0 instrument revealed the true owner of every remaining behavioral front:
  Fighter_804D64FC_t declares its table members void**, so the DWARF-driven native layout
  emitted POINTER->POINTER->VOID and the CPU attack tables/thresholds/scripts were copied as
  raw big-endian bytes (probed entry cmd=0x02000000 = swapped 2). No CPU-driven fighter could
  ever select an attack. Hand translator (native_dat.c translate_fighter_cpu_tables): per-kind
  [33] pointer arrays of cmd-terminated 0x24-byte attack entries, float[33] thresholds,
  float[6] weapon reach, and byte command scripts indexed by attack command id (count 64 — 39
  segfaulted via xA4 ids 0x28/0x29; past-array slots read non-relocated -> NULL).
  Evidence: prefixes gm-peach 747->9,966 (96% of the replay bit-exact), dl-fox 360->5,086,
  bf-falco->4,798, master-fox 269->2,434, fod-sheik->3,333, ics-ditto->3,021; aggregate
  183/63/0 unchanged. Remaining first-mismatch fronts are now dominated by the unrecordable
  item.misc0/2/3 pointer/pool-residue bytes at each ice-block spawn (classification class),
  plus fd-falco @346 (follower CliffWait pick) and the gm-peach @378 item-motion ULP family.

- 2026-07-27 — `retained` (session 3 coda: live standings, d2ab8923)
  Scope: the fd-falco @346 front (follower CliffWait pick) probe showed retail's
  gm_8016C75C returning the UPDATED KO count (1) at Nana's first post-KO state-10 tick with
  a dpad-up press (held=8) from ftCo_800ACD5C's x88 branch: retail's x24C scratch is dirtied
  between reads, so the observable behavior is a live standings recompute, not the frozen
  first-read snapshot the hosted mirror served. Made gm_8016C75C recompute per call.
  Evidence: bf-falco prefix 4,798->9,176 (95%), fd-falco 468->7,979 (95%), master-fox
  2,434->5,948, fod-sheik->4,086, ics-ditto->3,825, medium-sheik->3,962; aggregate 183/63/0.
  Note: MslCoreMatchRules.cpu_standings_snapshot_valid is now dead state (cleanup with the
  next wire/struct touch).
  Current fronts after d2ab8923 (prefixes 302-9,966): (a) item.misc0/2/3 pointer + pool
  residue lanes lead 9 of 12 replays (classification class); (b) micro AI-stick episodes:
  ics-ditto @1068 = state-4 recovery DI steering quantization phase (retail alternates
  -88/-127 with +88 up per the AA320 Nana stick=0x40 increments; ours slightly offset —
  suspect an upstream 1-ULP or an x570 draw), medium-sheik @210 = one-frame jump-decision
  skew (Landing vs KneeBend), ys-fox @5572 = ice-block scale low-byte drift (misc1 32 vs 72,
  ULP-class); (c) gm-peach @378 item-motion ULP family. The behavioral tail is now a handful
  of one-frame/1-ULP episodes per replay; classification authoring for (a)+(c) plus episode
  attribution for (b) is the remaining path to flipping the suite to CLASSIFIED.

- 2026-07-27 — `open` (gm-peach @3986 attributed to a disengaged general-CPU dash pick)
  Probe: retail Nana disengages at 3983 (xFA mim bit clear; ring slot +127 IGNORED) and the
  general CPU emits a full -127 stick at 3986 -> dash-left at 3987 (ground -1.4); ours emits
  a partial steering stick (~-45, -0.3545 momentum) -> walk. Same class as ics-ditto @1068
  and medium-sheik @210: the disengaged-CPU state/script pick differs for one frame. These
  three episodes (plus ys-fox @5572 scale-byte ULP and the item misc/motion classification
  classes) are the entire remaining icies distance. Next iteration: our-side x18/script trace
  at gm-peach 3975-3990 vs the retail x18 evolution (extend the ring probe with the ADE48
  cascade returns already wired for ACD5C) to find which state handler picks the full-stick
  dash command in retail.

- 2026-07-27 — `open` (gm-peach @3986 refined: identical stick, divergent landing physics)
  Follow-up traces (our tick + AB224 branch vs the retail ring probe) show BOTH sims emit the
  full -127 at slippi 3986 in state 1 (same-island AA42C walker) and dash at 3987; published
  lanes match through 3985. The @3986 delta (air self -0.3895 vs -0.3545 = one accel
  quantum; pos 0.035) therefore arises INSIDE frame 3986's Landing-state update with
  identical inputs — an unpublished state bit or IASA-order detail. Blocked on visibility:
  the engine dump carries only leader rows (port_count=2). Next tooling step: add follower
  fighter rows to the engine-dump writer (slippi-dolphin engine-dump branch) or an
  interpreter probe over ftCo_Landing IASA/physics for Nana at 3985-3987, then diff
  self-vel/decel inputs. gm-peach anchors: our tick t = slippi + 128 for the FIRST Nana
  lifetime; x7C resets on her respawn (filter traces by position, not t, across lifetimes).

- 2026-07-27 — `retained` (cascade-input bit-verification; episode isolated to script cadence)
  A paired probe of the ADE48 cascade's ftCo_800A3554(fp, 0) call (new kCascadeSafeCall PC
  0x800AE768 retail-side; msl_casc_trace sim-side) shows at gm-peach tick 3987 the AI inputs
  are BIT-IDENTICAL across sims: x54x 0x41321AB4, and x38 0x40C9B274 — i.e. the hosted
  hurt-capsule extents publication (59b0af15) is bit-exact against retail, and targets/x60
  match. The only difference is the post-3986 position itself: retail's walk script fired at
  tick 3986 (ring probe: ls -127 set during 3986 -> dash during 3986), ours at 3987. With
  identical state at every sampled tick, the残 divergence is the EXPIRY TICK of the recovery
  script chain built during her damage/air phase (csP/command_duration cadence) — one more
  probe layer: log ftCo_800B49F4 script-finalize ticks + durations on both sides across the
  recovery (~3950-3987) and find the first build tick that differs, then asm-diff that
  handler. All three residual behavioral episodes are this same cadence class.

- 2026-07-27 — `retained` (script-cadence layer probed; episodes converge on A2C80 recovery entry)
  The kScriptFinalize probe (0x800B49F4, logs per-build frame/x18/buffer bytes) shows retail
  building a state-4 recovery script EVERY air frame 3945-3982, demoting to x18=1 at the 3983
  landing, and building the state-1 AA42C walk script at 3984: SetLstickY 0;
  LstickXTowardDestination 0; WaitFor 2; LstickXTowardDestination 0x7F; Done - the WaitFor 2
  lands the full -127 exactly at 3986. Ours builds the same script one frame later (3985)
  because our x18 was 10 (not 4) through the recovery: our sim never entered the A2C80
  recovery state for THIS knockback while retail did (the dl-fox episode was the same gate
  agreeing; here it disagrees). All three residual behavioral episodes therefore converge on
  ftCo_800A2C80's per-knockback evaluation during the air phase - its inputs are the
  xFA_b5 in-bounds flag (B33B0's own bounds chain over the floor probe), the fall-angle
  lb_8000D008 gate, and the long recovery ray. Next: probe A2C80's ENTRY inputs (pos_delta,
  xFA_b5, the ray result) retail-side across the air phase (kA2c80Entry already logs dx/dy/
  xfa; add the mpCheckFloor ray result PC) and mirror sim-side; the first differing input
  identifies the owner (suspect: xFA_b5 phase, since B33B0's bounds chain runs every tick
  and its in_bounds -> xFA_b5 store was asm-verified but its mpCheckFloor probe inputs
  depend on cur_pos while airborne).

- 2026-07-27 — `retained` (A2C80 exonerated at the gm-peach onset; divergence is a state-4 exit)
  Paired A2C80 gate traces at the knockback onset (retail kA2c80Entry/kA2c80Ret vs sim-side
  msl_a2c80_trace): BOTH sims enter recovery state 4 at the same tick (slippi 3925, our
  t=979; angle bf8be887 passes, xFA_b5 clear, floor ray finds nothing, ret 1). The earlier
  'ours never entered 4' reading was wrong. Retail stays x18=4 through 3982 (script probe);
  ours is x18=10 by t=1038 (3984): our sim EXITS state 4 somewhere in 3925-3983 mid-air.
  Next (first action of the continuation): sample our x18 per tick across t=979-1042 (the
  tick trace prints it; one grep), find the exit tick, then diff the state-4 handlers'
  gates at that tick — case-4 of ftCo_800B2790 (grounded demote / ftCo_800A9904 air) and
  ftCo_800B24B8 (case 4 of B2AFC) against the retail probes. The one-frame walk-script
  cadence at landing (and hence the entire episode class) follows from whichever gate
  flips ours out of 4 early.

- 2026-07-27 — `retained` (the episode split is the landing-tick state demote: 4->1 vs 4->10)
  Definitive lifetime-2 timeline (gm-peach, NANA-tick t = slippi - 2945; beware TWO Nana
  lifetimes sharing t ranges — filter by position): both sims enter state 4 at slippi 3925
  with bit-matching gate inputs (angle/dx sequences identical), both steer -84 through 3982,
  both land during 3983. At tick 3983 retail demotes 4->1 (ftCo_800B2790 case-4 grounded
  path: x18 = x1C, Done) and builds the AA42C walk script at 3984; OURS lands in x18=10 at
  the same tick, spends 3984 in ACD5C (stick 0), demotes to 1 at 3985, and builds the walk
  script one frame late — the entire one-frame cadence divergence. The ONLY x18 = x20(=10)
  writer is the ADE48 cascade hop guarded by ftCo_800A3554(fp, 0), which cannot pass while
  airborne at the think; so either the hop fired against expectation (trace it: one
  msl-trace at the cascade hop and at the case-4 grounded demote, logging tick + which path)
  or an unaudited writer exists. FIRST ACTION NEXT SESSION: instrument those two sites, run
  gm-peach --frames 4115, read the single line that says which path wrote 10 at t=1038, then
  asm-diff that path. Everything else (episodes, classifications, locks, wiring) hangs off
  this one answer.

- 2026-07-27 — `retained` (the write is the cascade hop; retail never reaches it at 3983)
  The x18-write trace answered the pinned question: OUR 4->10 at the landing tick is the
  ADE48 cascade final transition (A3554(fp,0) passing because ga publishes Ground at the
  think while motion still lags in Fall, and the ADE48 head reset x54 to her own position,
  making dist 0). The retail cascade probe shows NO A3554(0) call at 3983 — its first CALL
  is 3984 — so retail's ADE48 exits BEFORE the transition cascade at exactly that tick,
  while ours falls through. Remaining search space is tiny: ADE48's pre-transition early
  exits at one tick — the x18==0x12 return, the x221A_b3 hitlag switch_cmd path
  (B4A78 + x18=0x12), and the motion-keyed 0x125/0x154 -> x11/x13 branches. NEXT: either
  sim-side log which ADE48 exit path runs at the landing tick for both x18 histories, or a
  retail PC probe over ADE48's early-return addresses at 3983. Note the ga-vs-motion phase
  at landing think (ga=Ground from the prio-6 map pass of the PREVIOUS frame while
  motion_id still Fall) is what arms A3554 grounded gates one tick before the Landing
  state exists — REFINED by direct predicate probes (kB4ab0Entry + cascade
  predicate returns): at the landing ticks retail calls NONE of A5ACC/B9CBC/B8A9C/B732C/
  A3710/B4AB0 (their first calls are 3984, all returning 0), so retail's ADE48 exits BEFORE
  the whole predicate block while ours reaches the final transition and hops. Also note the
  NANA tick-trace anchor correction: t-row(F) prints post@(F-1) motion, so our landing motion
  (42) appears at post@3982 — SAME frame as retail; the earlier one-frame-landing readings
  were anchor artifacts. The remaining search space is the ADE48 region between its head and
  the bl ftCo_800A5ACC (asm 0x800AE460-0x800AE568: the x18==0x12 return, the x221A_b3
  switch_cmd block, and the motion-keyed 0x125/0x154 -> 0x11/0x13 and 0xBF/0xB7 -> 5,
  0xFC/0xFD -> 6 sets): add a PC-trace over that range (MSL_PROBE_PC_TRACE with PC_START/
  PC_END already exists!) for one retail tick and mirror the taken branch sim-side. It is
  the last unexplained divergence gate. NOT ftCo_800B8A9C returning TRUE (x18 -> 2, returning before the final transition — matching
  the missing CALL row) with B2790's case-2 handler demoting 2 -> 1 the SAME tick (matching
  retail's end-of-tick x18=1 and the 3984 walk script). Ours' B8A9C returns FALSE at that
  tick. So the last mile is ftCo_800B4AB0's per-entry evaluation (or B8A9C's target-state
  gates A3134/A3200 on Peach) at ONE tick with known inputs — instrument the entry loop
  (which entries pass the level/cmd gates, the predicted relx/relPredY per entry, and the
  final selection) sim-side, and the same via an interpreter probe on B4AB0's entry/return
  retail-side. Suspects: the f64-mixed prediction arithmetic (lines ~161-213, fmadd-class),
  or a subtle table-translation field. This single comparison closes the episode class.

- 2026-07-27 — `retained` (BREAKTHROUGH: cascade repair landed; first replay CLASSIFIED)
  The compensator was a FOURTH upstream decomp drop: ftCo_800B2790's case 4 (and case 19)
  read special_floor UNINITIALIZED where the asm carries a zeroed register — the garbage
  routed retail-faithful landings into ftCo_800A0148's blast-zone escape script, masking the
  missing cascade exit. Landing all three repairs together (9bc1b259: the ADE48 x18==4
  cascade exit + both zero-inits) restored the retail landing sequence and moved the suite
  to matched rows 4,215-14,150: ys-fox 8,140/8,152, master-fox 8,543/8,610, gm-peach
  10,299/10,401, medium-marth 14,150/14,709. ys-fox's entire residue = 12 rows of belay
  string item.misc1/misc2 = Slippi's raw bytes over retail heap ItemLink/JObj pointer
  identities — unrecordable in principle — and is now CLASSIFIED (0a20c832,
  id unrecorded-item-pointer-identity, full native snapshot; note the manifest was
  re-serialized with indent=1, formatting churn only). SUITE: 0 pass / 1 CLASSIFIED /
  11 fail; aggregate 183/63/0 verified after both commits.
  Remaining: the other 11 replays now follow the same endgame — small residues per replay
  (master-fox 67 rows, gm-peach 102) to attribute as fix-or-classify, then locks and
  aggregate wiring.

- 2026-07-27 — `open` (ADE48 cascade truth table complete; the x18==4 exit contradiction)
  Full asm branch-skeleton extraction (0x800AE280-0x800AE7A8, saved recipe in-session): EVERY
  state guard in the transition cascade exits the function when x18 already equals it
  (fifteen exits; guard 7's exit also calls ftCo_800BB9B4 first). The C's PYRAMID NESTING
  actually implements fourteen of them (each `if (x18 != N) {` nests the whole remainder and
  the closing braces fall to returns) — the ONLY non-nesting guard is x18 != 4 at the
  ftCo_800A2C80 site: when x18==4 the C uniquely falls through into the 0xF+ guards. That is
  the single genuinely dropped exit. CONTRADICTION TO RESOLVE: adding the asm-faithful
  `else return` REGRESSED the suite (gm-peach matched rows 9,966 -> 4,077 — note the FAIL
  column is total matched rows, not prefix), which should be impossible against a retail
  recording unless (a) a compensating infidelity elsewhere currently cancels the fall-through
  bug (find it by tracing the with-fix landing sequence: ours should now build the walk
  script at 3984 and dash at 3986 exactly like the retail probe — verify, then chase where
  the tail diverges instead), WITH-FIX TRACE RESULT (the compensator, partially unmasked): with the
  else-return applied, the landing tick builds a TEN-frame WaitFor script (csp=1 dur=10) and
  x18 HOLDS 4 through 1038-1047 — the B2790 case-4 grounded demote never fires even though
  the A3554-based analysis proved ga=Ground at that think. Suspects: the script was built by
  something reached before case-4 (ftCo_800A8DE4_noinline preamble? an ACD5C-shaped WaitFor
  0xA script implies a case-10 dispatch — check whether x18 was still 10 from the previous
  frame's structures), or the csP gate sequencing. DISPATCH-TRACE RESULT (fix + case-id/ga logged): the landing tick DOES
  dispatch case 4 with ga=Ground (t=1037, the demote path executes), but NO dispatch occurs
  for at least the next 8 ticks — the post-demote script gate (csP/command_duration) stays
  blocked even though ftCo_800B3E04's Done handler (csP=NULL, dur=0) is verified correct in
  isolation and ftCo_800B49F4 runs at B2790's tail. Retail's script probe shows builds
  CONTINUING (3984+). NEXT: one run with csP/dur logged at B3E04 entry/exit and B49F4 for
  the landing tick — the blocked gate is the last link; beware the two-Nana-lifetime t-space
  collision (filter by position) and that DISP prints only when the gate passes.
  Hypothesis (b) ELIMINATED: no UCF or slippi-ssbm-asm injection touches 0x800ADE48-0x800AE7AC, so the recording's ADE48 is vanilla and (a) — a compensating infidelity that currently cancels the missing exit — is the operative theory; the with-fix landing-sequence trace comparison is the way in. The reverted change is
  one `else { return; }` at the A2C80 guard — trivial to re-apply once (a)/(b) is resolved.

- 2026-07-27 — `open` (the x18==4 cascade exit: asm-real, but the naive port regresses)
  PC-coverage trace (generic MSL_PROBE_PC_TRACE over 0x800AE0F0-0x800AE568 at the landing
  tick) shows retail's ADE48 executing: motion 0xFC/0xFD checks, x18==9 guard, IsGrabbing,
  then `lwz x18; cmpwi 4; beq .L_800AE484 -> li r3,1; b .L_800AE790` = FUNCTION EPILOGUE:
  when already in recovery state 4 the transition cascade EXITS (the asm function returns a
  transitioned flag the void C drops). The upstream C falls through — a second control-flow
  drop in this TU. HOWEVER the one-line `else return` port empirically REGRESSED the suite
  (gm-peach's landing episode grew into leader-lane divergence from 4100; master-fox/
  ics-ditto/bf-falco prefixes dropped) and was reverted (uncommitted). Hypotheses for the
  regression: the exit's r3=1 gates caller behavior not modeled by the void C; or the other
  state guards (9/5/6/...) have similar unported exits so restoring only the 4-exit skews
  the state machine asymmetrically; or downstream within-frame RNG draw-order compensations.
  NEXT SESSION: audit the ENTIRE ADE48 transition block against asm as a unit (the same
  PC-coverage probe run at a few ticks in different states gives the ground truth per
  guard), port ALL its exits together, and re-verify. The committed tree (117a2053 state)
  remains the best-verified point: prefixes 302-9,966.

- 2026-07-27 — `open` (dl-fox @238 root-cause narrowed to ftCo_800A2C80's long recovery ray)
  Scope: with the tick trace aligned (our t = slippi + 133 on dl-fox), our Nana's x18 path
  through her damage (~f210-236) is 1 -> 4 -> 10 -> 1 while retail lands in x18=2 (attack).
  Both sims run the ADE48 transition cascade (x221A_b3 hitlag arming verified); the split is
  ftCo_800A2C80 ("falling toward doom" recovery check, NOT in the earlier bounds sweep):
  ours returns 1 (-> state 4 recover) where retail returns 0 and falls through to
  ftCo_800B8A9C (attack tables ARE populated natively — probed non-NULL). A2C80 casts a
  1000-unit ray from the ECB bottom along the normalized fall direction through mpCheckFloor
  and returns 0 when it hits an in-bounds floor. REFUTED by a follow-up probe (kA2c80Entry/kA2c80Ret added
  to the Nana ring probe): retail A2C80 ALSO returns 1 at f210 — both sims enter recovery
  state 4 identically, and hosted mpCheckFloor is the verbatim retail loop (mpBoundingCheck2
  broadphase, no O1 shortcut). The real split is AFTER landing (~f223): both demote out of 4
  and re-run the ADE48 cascade; retail's ftCo_800B8A9C ground path returns TRUE (x18=2,
  attack) while ours always returns false. Gates verified equal (xF9_b2 set, target present);
  the per-kind attack table pointer ((void**)Fighter_804D64FC->x4)[FTKIND_NANA] is translated
  and non-NULL natively (probed host pointers). NEXT INSTRUMENT: ftCo_800B4AB0 (the CPU
  attack-script evaluator) — log its parse walk and result natively vs a retail interpreter
  probe at its call sites; suspects are the attack-script PAYLOAD translation (per-entry
  structures behind the per-kind table read via retail byte offsets would break under native
  widening) and its internal ftcpuattack range math reading target x55C spans. This family
  plausibly owns the remaining behavioral fronts (dl-fox @238 walk-vs-dash after the missed
  attack, ys-fox @5572+, medium-sheik @194 Landing-vs-Turn = a missed script jump,
  master-fox @147 damage-exit).
  Scope: retail ring probe on dl-fox frames 225-242 (dl_ring2.jsonl recipe): retail Nana is
  DISENGAGED (xFA mim bit clear) in general-CPU x18=2 from at least 225 through 242, in Wait
  with friction slide (-0.3498) while Popo dashes; the UCF retro-write slot (-128) is present
  in her ring but ignored. Ours re-engages/dashes at ~237 (ground -1.4). x7C phase MATCHES
  retail (x7C = slippi + 132 in both, so the %-gate phases are aligned). gm_8016C75C mirror
  re-verified faithful (one zero-KO snapshot at first read arms nothing). The divergence is in
  internal x1A88 state evolution through Nana's damage reaction (~frames 210-235): retail
  lands in x18=2 (escape) and stays; our x18 path unknown — needs the our-side tick trace
  (rebuild container with the ftCo_800B3900 tail trace from msl-icies-port-state) compared
  frame-by-frame, then an asm diff of the diverging handler. Candidate handlers: the damage
  reaction chain (ftCo_800B4A78 -> x18=0x12 -> ftCo_800AC5A0 case 18 -> demotes), case 2
  ftCo_800B04DC. Note B0E98 re-engage requires pos_delta diff below co_attrs.mid_walk_point,
  which should REJECT engage while Popo dashes (delta gap ~1.05) — whichever sim engaged there
  is the wrong one, and ours engaged.
  Evidence: dual-entity player machinery already compiled in and inert; CPU predicate stubbed
  false; validation reads only `ports.P{n}.leader`; UCF pad ring would double-shift if Nana took
  the pad path. Nana's AI is type 6 (mimic ring + follow), NOT the general CPU tree; reachable
  slice bounded inside NonMatching `ftCo_0A01.c` + `ftcpuattack.c`.
  Disposition: proceed with sequencing above; step-2 import is mechanical (all 11 chara/article
  TUs are Matching upstream).

- 2026-07-28 — `retained` (FOD CONSTRUCTION RNG FIX, commit 4fa63d66; probes 3325a6f218)
  The fod-sheik/auto-falco follower fronts traced to Nana's x1A88.x7C AI phase counter:
  its init value is (int)(10*HSD_Randf()) drawn at fighter creation, and our FoD
  construction stream sat two draws EARLY because the hosted grIzumi platform proc
  early-returns to the Slippi height stream and never consumed retail's two creation-tick
  HSD_Randi draws (grIzumi_801CC358 case 0 via rand_range; case 3 for below-ground
  starts). Nana drew x7C=7 where retail drew 0; every x7C%N gate (x56C %600 follow-radius
  redraw, x570 %30, DI %120) fired 7 ticks early, sampling different stream positions.
  Fix: msl_grizumi_consume_replay_creation_draw at match construction post-OnInit, gated
  on msl_slippi_fod_platform_height owning the creation publication; do NOT touch
  xC4/xC6 (arming them at creation broke all 27 human Fountain aggregate replays through
  mistimed platform self-motion — the machine still runs event-quiescent frames).
  VERIFIED per-stage construction draw counts vs retail (Randf probe + first-A101C
  chain distance): BF 1=1, DL 2=2, YS 1=1, FD 4=4 (grLast port), PS 1=1, FoD 2=2 after
  fix. Retail x7C inits per replay are now reproduced exactly (fod: Popo 7, Nana 0,
  Sheik 4, Zelda 0). fod-sheik 4,215 -> 5,077 matched; aggregate 183/63/0 green.
  KEY RECIPES: (a) HSD_Randi INLINES the LCG (0x80380588) — a Randf-entry probe never
  sees Randi draws; infer via chain distance. (b) CPUCore=0 in the probe user-dir
  Dolphin.ini interprets the whole boot so construction draws (frame counter -124) hit
  the interpreter probes without the EXI window. (c) Slippi ONLINE per-frame seeds are
  synthesized (base + frame<<16) — construction draw-count divergence is erased per
  frame, EXCEPT values that persist (A101C x7C, B9704 x34).
  NEXT FRONT (fod-sheik @1248): follower_speed -0.725372 vs -0.726160 while mimicking
  (stick 125 walking), NOT a /127 byte value on our side — ring/clamp interplay again;
  ring probe run at frames 1240-1256 pending analysis.

- 2026-07-28 — `retained` (UCF DASHBACK OVER-FIRE FIX, commit after 4fa63d66)
  The fod-sheik @1248 mimic front: retail ring probe showed the slot playing at x7C=1371
  held 125 = trunc(127 x 0.986) natural capture while ours held a retro-written 127. The
  gecko replaces the facing store at Interrupt_AS_Turn+0x4C, which lives INSIDE the
  if (!has_turned) branch of ftCo_Turn_IASA; our hook ran unconditionally after the if,
  so already-turned smash turns (has_turned set at frame 1) still converted at anim
  frame 2 (87/88 fires in fod-sheik were spurious). Moving the call inside the branch:
  fod-sheik 5,077 -> 9,724, medium-sheik -> 14,279, ics-ditto -> 11,693, fd-falco ->
  8,117, bf-marth -> 6,993, auto-falco -> 6,654; aggregate 183/63/0. DIAGNOSIS RECIPE:
  the DASHBACK-FIRE env trace (fire-time anim/has_turned/x670/raws) + the ring probe's
  natural-vs-retro slot values pinpointed the condition delta in minutes.

- 2026-07-28 — `retained` (BLIZZARD SPAWN FMADDS, commit 5023a036) + `open` (dl-fox @5082
  = Fox TAIL dynamics). The dl-fox item vel/pos ULP family (4476+) was the blizzard puff
  spawn angle: GALE01 0x802C2290 fuses (x10-xC)*rand+xC; __fmadds port retired the family
  on four replays. The remaining dl-fox fork @5082: Popo usmash frame 9 hits in retail,
  whiffs in ours. Collision probe (dl_coll_probe3.jsonl in job tmp 164f8743) + sim-side
  HB/HU dump (scalar.c MSL_HB_TRACE, container-only): our usmash HITBOX x4C bits are
  RETAIL-IDENTICAL (incl the z=±8.35 swing arc — verified byte-exact at the 981 usmash
  too), and 12 of Fox's 13 hurt capsules match retail x/y bits exactly. The 13th — bone_idx
  18, offsets (0,0,±0.8), scale 1.62 = FOX'S TAIL, a dynamics-driven chain — sits at
  (49.26, 5.97) in ours vs retail (50.62, 7.97, -0.65): 2.4 units of accumulated dynamics
  divergence, and retail's hit lands exactly on it (coll_distance +0.319). NEXT: the tail
  dynamics chain (lb dynamics springs + Whispy wind coupling on DL + lb_800115F4 emitter
  phase); the marios sessions' dynamics/quat toolkit applies. NOTE the retail probe
  numbering: this window's collision-probe frames aligned 1:1 with validator frames.
  TOOLING (container-only, uncommitted): MSL_ICE_TRACE_* (per-frame item kind/vel/pos
  bits at export) and MSL_HB_TRACE_* (fighter hit capsules x4C/x58 + hurt capsule a/b
  positions at export) in /work/src/runtime/scalar.c; validate_replay.py detail rows
  widened to 14 in the container copy.

- 2026-07-28 — `open` (WHISPY WIND MACHINE — dl-fox 5,086 -> 10,818, UNCOMMITTED: aggregate
  gate blocked). Root cause of the dl-fox tail divergence: hosted grOldPupupu_802113E0
  SHORT-CIRCUITED Whispy's whole machine to the replay xDC direction (the FoD-platform
  lesson one level up), so the blow loop's wind force emitters (lb_80011A50 every 10
  ticks while blowing, first at f~754 on dl-fox) never spawned and NO fighter tail/body
  dynamics chain ever felt wind. Sim-side proof: msl_emitter_trace (container-only, in
  lbspdisplay.c lb_80011A50/lb_800119DC + scalar.c helper) logged ZERO emitters pre-fix,
  243 post-fix. Fix in working tree (Mac+container, uncommitted): delete the hosted
  early-return; the retail machine runs (its rand_range draws are prio-4 post-think =
  reseed-erased; DL construction counts already matched), and apply_replay_stage_events
  still republishes recorded xDC pre-frame for wind-push consumers.
  RESULTS: dl-fox 10,818/11,083 (the 5082 tail usmash now connects); icies otherwise
  unchanged. AGGREGATE: 164/54/28 — 22/28 fails are LOCK-DRIFT ONLY (mismatch_rows=0,
  tails are in the locked output; locks need re-recording once green) + 6 REAL row fails
  (puff Game_20260602 8,613 rows; luigi medium-fox 2,022; marth Goshawk 1,766 +
  QuestionableHarmfulPanther 74; marios plat-marth 72; marios medium-fox-2025-12 ONE
  row) — knife-edge marginal outcomes flipped because wind-blown dynamics are close but
  not bit-exact yet.
  THE REMAINING ULP CLASS (pre-wind, reproducible): dl-fox tail bit-exact through frame
  46, then Fox's hit/hitlag (47-50, both sims' DD94 correctly skip — cadence verified
  identical via MSL_DD94_TRACE vs the retail MaybeCaptureTailProbe rows) — capsules
  bit-equal again at 51 — then ONE dynamics tick at 52 forks 0.05 (~100 ULPs). Suspect:
  the hitlag/damage SHAKE displacement feeding the dynamics origins/colliders at the
  resume tick (hosted may skip the model vibrate as visual-only), or the colliders
  (&fp->x1670, ftColl_8007AF60) / x2228_b1 arg at 52. NEXT: dump solver inputs at
  validator 51-52 both sides (parent_mtx origin, colliders, arg1, ret_B0) — the
  MaybeCaptureTailProbe (slippi-dolphin, PC 0x8009DD94 entry, port==1 1-BASED) plus a
  matching sim trace. Transients DAMP when quiet (exact at 200-210 & 38-46; 0.61@400
  after shine, 0.23@600) — fix the per-event seed, chaos disappears.
  TOOLING NOTES: retail probe rows appear only when DD94 runs (hitlag gaps are DATA);
  probe frame == validator frame here; our export HB/HU rows are end-of-frame; sim
  frame_id in traces = validator frame - 1 (DD94 trace f=35..45 == validator 36..46).
  Retail per-frame tail baselines saved: job tmp 164f8743/tail_*.jsonl (windows -20..-10,
  38..62, 60..70, 200..210, 400..410, 600..610) + our_tail.txt (post-whispy-fix).
  A giant interpreter window (-123..990) STALLS playback at -123 — sample 10-frame
  windows instead.

- 2026-07-28 — `open` (tail fork: SOLVER INPUTS EXONERATED). The dl-fox validator-52
  one-tick fork: retail MaybeCaptureTailProbe (extended: arg1/ncol/collider dump,
  slippi-dolphin committed) vs sim DD94IN trace shows arg1 (x2228_b1, constant 0) and
  the x1670 collider list (ncol=1, world pos + radius) BIT-IDENTICAL at every tick 44-56
  including the fork. The divergence is inside the tail chain state itself. CAVEAT: the
  capsule comparisons carry a sampling skew at the hitlag boundary (retail probe reads
  capsule 12 at DD94 ENTRY = pre-tick; our HB trace exports post-frame), so the "equal
  at 51, 0.05 apart at 52" reading may be one tick off. NEXT: dump the CHAIN STATE
  per-link (DynamicsData jobj rotate quats + desc.lb_unk0.unk_58/unk_2C) at DD94 entry
  BOTH sides for validator 46-53 and find the first diverging link+field; then audit
  that op (msl_dynamics_build_basis / transform helpers vs Gekko paired-single PSMTX).
  PROBE QUIRK (reproduced twice): a tail-probe window 44..56 yields ZERO rows while
  38..62 works — start windows earlier/wider. Sim traces: MSL_DD94_TRACE=<path> now
  dumps DD94IN inputs (ftdynamics.c container-only; needs the msl_emitter_trace helper
  in scalar.c container copy + stdio includes).

- 2026-07-28 — `retained` (WHISPY WIND + CHAIN MATRIX REFRESH LANDED, commit 637b6706;
  AGGREGATE 190/56/0 — up from 183/63/0). Final design after the live-machine attempt
  regressed Fountain knife-edges: (1) wind emitters mirrored deterministically from the
  RECORDED xDC stream (nonzero == retail xD0 in (0x2D,0x140) entering 0x2E; spawn on
  %10==0; onset tracked via this proc's own xD0-as-counter because the pre-frame replay
  pass already wrote xDC); the machine itself stays short-circuited (its rand timers are
  not replayable headlessly — retail's effect draws precede its prio-4 stream position —
  and fn_802112F4's push reads xDC post-prio-4, so a live machine forks positions by the
  0.10 wind speed). (2) msl_fighter_refresh_dynamics_matrices at the publication phase:
  retained chain-link jobj matrices rebuilt end-of-frame so ftCo_8009CB40's re-anchor
  (mtx[i][3] reads on anim changes) sees last-frame world poses like retail's render
  guarantees; previously links past the capsule bone were NEVER built (identity → tail
  tip re-anchored to x=0 exactly at dl-fox validator 51). Chain-state probe now
  BIT-IDENTICAL across the hit window. SEVEN long-classified replays (5 DL, 1 YS, 1 FD,
  all with Fox) became full passes — classifications removed, output locks re-recorded.
  dl-fox 10,818/11,083. Icies otherwise unchanged; ys-fox classification intact.
  OWED: container pytest sweep; PPC-backend lock parity check (locks re-recorded from
  native; hosted-only fixes leave PPC behavior unchanged but verify before the wire cut).

- 2026-07-28 — `retained` (SEVEN MISC-LANE CLASSIFICATIONS) + `open` (the 88/91 roll
  misalignment = A MISSING EFFECT DRAW). Icies now 0 pass / 8 classified / 4 fail.
  The four open episodes and what is known:
  * bf-marth @5605 + medium-marth @10783 (leader DamageFly 88 vs 91): ROOT-CAUSED TO THE
    RNG STREAM. The variant roll (ftCo_Damage.c:419, HSD_Randf < x240 in ftCo_8008DCE0;
    91=DamageFlyRoll picked when kb_level==3 && percent>=x23C) samples one position
    early in ours. Retail frame 5605 draw sequence (bfm_randf_probe.jsonl):
    [09F7 CPU x3 BIT-MATCH, efSync lib draw BIT-MATCH, **extra dispatcher draw at lr
    0x80063B74 inside efAsync_Dispatch = the gfx 0x3EC case: li r3,8; bl efLib create;
    HSD_Randf**, then the roll]. Ours lacks the dispatcher draw because OUR queue holds
    gfx_id 0x3F8 (1016, spawn_kind 6) at that tick — no case in effects.c's dispatcher
    switch — while retail dispatches 0x3EC (1004, model 8 + one Randf, a case we already
    model). SO: either our enqueued gfx id is wrong (enqueue caller attribution via
    __builtin_return_address lands in ftCo_0A01.c ftCo_800A05F4 region — inlining-
    degraded, re-attribute with a debugger or wider addr capture), or 0x3F8 needs its own
    dispatcher-draw case (check refs efasync.c case 0x3F8: efLib_Create_Attach_Pos(0x13)
    — MODEL-BACKED, no draw, so more likely the ID is wrong our side or retail spawned a
    DIFFERENT SECOND effect). Fixing this likely clears BOTH marth replays.
  * dl-fox @5205-5215: small item vel/pos episode (12 fields, blizzard-adjacent).
  * fod-sheik @10759+: item existence episode (item_count/type/owner from 10761).
  TOOLING ADDED (container): MSL_GFX_TRACE_PATH prints GFXQ (enqueue, caller low24 in b
  as float — decode carefully, it is exact for <2^24) and GFX (process) rows via
  msl_emitter_trace; our RNG trace pairs frames via synthesized online seeds
  (base + (frame+123)<<16 — bf-marth base 33407).

- 2026-07-29 — `closed` (session 12: master-diamond @682 root-caused — the UCF pad-ring
  patch profile; ALL 13 extended icies replays in the aggregate: 271 = 203/68/0)
  The @682 "grab-attach" was misread: Sheik was hit by an ice block at 681 and our
  runtime SDI'd her 6.0*stick = -5.85 during hitlag at 682. Trace chain: the
  displacement matched sdi_pos_scale * lstick exactly; the spaghetti trace showed her
  x670 tilt timer correctly stale (flick began on the hit frame, so the count-up
  branch ran), meaning vanilla SDI could not fire - the trigger was
  msl_ucf_sdi_check (the pad-ring f2 SDI patch), which retail provably did not run
  for this session.
  A first attempt keying all ring patches on ucf_cardinals_1_0_enabled BROKE EIGHT
  aggregate replays (2022 luigi/marios, puff master-diamond, marth x2, falcon,
  peach x2 - all cardinals-false yet bit-exact under ring-patch emulation): the
  ring-patch rollout and the cardinals rollout are INDEPENDENT eras. Reverted;
  the correct model is the existing per-replay rollout-flag scheme (wire.h already
  documents ucf_sdi_enabled/ucf_shield_sdi_enabled as "separate
  rollout/capability" for exactly this reason).
  CHANGES: new ucf_shield_drop_extended_enabled flag plumbed end to end
  (wire.h/wire.c config byte - MslCoreMatchConfig 52->53, header 108->109;
  match rules; scalar init; native.c kwargs x2; suite_io + validate_replay
  schema); msl_ucf_pass_oos_stick_check re-keyed from the session-11 cardinals
  gate to the new flag; the two ranked-anonymized icies captures
  (master-diamond, platinum-platinum) carry ucf_sdi/ucf_shield_sdi/
  ucf_shield_drop_extended = false (platinum's flags also reconcile its ppc
  snapshot history: the 1326-row follower fork seen pre-gate was sdrop-on
  behavior).
  RESULT: master-diamond 782 -> 6,186/6,460 matched, item.misc-only; classified
  (native 914 / ppc 194) + locked; icies_parked.json dissolved into
  icies_extended.json (13 replays); counts 270->271; aggregate 203/68/0; container
  pytest green; WingedGorgeousPanther ppc snapshot verified stable under the
  re-keyed gates; PPC rebuilt for the wire change.
  OPEN QUESTION (for future imports): what distinguishes the ranked-anonymized
  profile (no ring patches, 2023-24 vintage) from ordinary 2022+ netplay (ring
  patches present)? Possibly a ranked-mode gecko set. New anonymized imports
  should A/B the three flags against retail probes before classification.

- 2026-07-29 — `closed` (session 11: icies_extended wired into the aggregate — 270
  replays, 203 pass / 67 classified / 0 fail; the pre-cardinals UCF shield-drop gate)
  master-diamond's frame -19 fork root-caused with retail probes: retail Nana
  (mimic-driven off Popo's ring) dashes to the platform edge, GuardOns off the
  ring-played trigger at -19, and vanilla shield-drops (Pass) a frame later when
  the played stick crosses -0.66. Ours Passed at -19 through the UCF
  shield-drop-extended emulation: msl_ucf_pass_oos_stick_check read the PORT's
  sdrop counter (hot from Popo's own deep-down stick) with no version gate. The
  shipped gecko's counter lives in the shared UCF pad ring, which ships inside
  the combined "UCF Pad Buffer + 1.0 Cardinals" patch — recordings that predate
  1.0 cardinals (ucf_cardinals_1_0_enabled=false) never ran that buffer, so the
  counter branch cannot fire there. FIX (runtime/match.c):
  msl_ucf_pass_oos_stick_check returns false when cardinals are disabled.
  Verified: master-diamond -19 fork gone (103 -> 782 matched rows); aggregate
  unchanged (several cardinals-false replays re-validated identically).
  master-diamond's NEXT fork (frame 682) is a different class — Sheik
  grab-attached ~5.85 units from retail while BOTH climbers match bit-exact
  (pummel/throw cascade from 687); unaffected by the session-10 matrix refresh
  (A/B verified). PARKED in the new replays/suites/icies_parked.json.
  WIRING: icies_extended.json rescoped to the 12 green replays and added to
  melee_core_aggregate.json (258 -> 270); 12 unrecorded-item-pool-residue
  classifications with native + ppc snapshots; 12 output locks; coverage/runner
  tests updated (270 + manifest set + classified loader). Trace infra: negative
  frame windows now work for MSL_NANA_TRACE (match.c gate accepted only
  start >= 0); container gained AA42C-IN/RECORD/MIMIC-held/PASSGATE/GUARDCHK/
  OOSCHK probes (ftCo_0A01.c, ftCo_Pass.c, ftCo_Guard.c, container-only);
  slippi-dolphin ring probe now dumps nana x618/x671/x668.
  KEY RECIPE: Nana entry behavior = mimic ring playback of Popo's inputs
  (capture quantizes input.lstick*127 at think time, one frame stale; the ring
  x4/x5 trigger bytes truncate the merged x650 float to 0/1 — shields propagate
  through the held word's HSD_PAD_LR bit, not the analog).

- 2026-07-29 — `closed` (session 10: THE TAIL-SOLVER FORK ROOT-CAUSED AND FIXED —
  two production fixes, 4 classified replays promoted to bit-exact, aggregate
  203 pass / 55 classified / 0 fail)
  The session-9 "solver fork with bit-identical inputs" was never in the solver.
  Per-stage link_dir instrumentation (MSL_ST rows inside lb_8001044C, container)
  showed our frame-3468 YS solve matching retail's implied final dirs to 0.2-0.8
  deg — but our OWN 3469-entry anchors differed from what our solve wrote.
  ftCo_8009CB40 (the anim-ownership toggle) had rewritten the chain anchors from
  raw jobj->mtx at the taunt-exit action change. Retail probe on 0x8009CB40
  (slippi-dolphin: cb40 event in MaybeCaptureTailProbe + per-link unk_38 "av"
  dump) proved retail fires the identical release/acquire pair but reads
  END-OF-FRAME RENDER matrices (solved pose, stale through the next frame's anim)
  — our port had left the last MID-FRAME pre-solve HSD_JObjSetupMatrix in mtx.
  FIX 1 (melee/ft/fighter.c): Fighter_8006D9AC re-runs
  msl_fighter_refresh_dynamics_matrices after ftCo_8009E0A8 (post-solve), so the
  toggle samples the solved pose exactly as retail's renderer leaves it.
  Verified bit-identical CB40 rewrites vs retail at YS 3469 and 33692 frames
  36/313/677.
  That fix flipped marios/33692 (was pass) — bisecting its 380-solve fork against
  retail (windows 100..380 bit-identical in w/rot/s58/jr/jt/par/av) landed on the
  collider-avoidance stage: retail rotated link 2 by 0.1896 rad off collider
  (-43.64,10.65,0.38); ours read collider position (x14, pos.x, pos.y) because
  struct lb_Collider mirrored Fighter_x1670_t with byte-offset padding — the
  embedded HSD_JObj* widens to 8 bytes on 64-bit hosts, shifting position from
  0x18 to 0x1C and the stride from 0x28 to 0x30. Hosted collider avoidance had
  read garbage since the port began (PPC, with 4-byte pointers, was always
  correct — hence "ppc matches more rows than native").
  FIX 2 (melee/lb/lbspdisplay.c): lb_Collider declared with real field types
  (Vec3, f32, void*, f32, Vec3, s32) — layout-correct at any pointer width.
  RESULTS: 33692 passes; auto-fox-b @3503 SDI cascade gone (item.misc-only now);
  icies_extended = 12 misc-only + master-diamond @-19 (parked); icies suite 12/12
  classified; aggregate 203/55/0 with FOUR replays promoted to fully bit-exact
  (sheik MixedAllQuetzal, doubles Game_20260704T012353, marios medium-fox-2025-12
  + silver-fox-2026-06 — the last dropped a 52,031-field residual).
  Suite updates: 2 classification entries removed, 2 kept ppc-only (native dict
  dropped; test_melee_core_validation now selects native-snapshot entries only),
  4 output locks re-recorded, 2 ledger rows appended. PPC re-verified per-entry
  after the rebuild (the PPC build also defines MSL_CORE_HOSTED so FIX 1 applies
  there; snapshots re-recorded where drifted).
  KNOWN GAP: the Mac-side data/ pipeline only covers 7 characters (no ICs) — Mac
  icies runs fail on stale data, container remains the validation authority.
  Remaining icies-extended non-misc work: master-diamond @-19 (match-start
  entry-approach clamp via published extents, likely CB40-at-spawn sampling
  pre-first-refresh matrices).

- 2026-07-28 — `closed by session 10` (session 9: icies_extended staged; the tail-solver fork isolated
  to a single frame with bit-identical inputs)
  Scope: ~/SSBM/Replays/Samples/ics.zip imported (30 files = the 12 existing suite
  replays + 18 new). Excluded: 4 non-frozen-PS games (established policy) and the 2022
  FoD capture (Slippi 3.9.1 predates the animation_index stream — validator ERROR;
  samus-suite precedent). The remaining 13 staged as replays/suites/icies_extended.json
  (commit 770c8c27), NOT included by melee_core_aggregate.json — icies.json stays at 12
  so the aggregate/coverage gates stay green (258, container-verified 12 classified/0
  fail post-split). ucf refinements: master-diamond (v3.16) + platinum-platinum (v3.15)
  ranked-anonymized carry ucf_cardinals_1_0_enabled=false + played_on=network (the
  cardinals flip fixed master-diamond's frame -39 Popo fork: 0.185 vs 0.185*cardinal
  ratio signature).
  Extended-suite state: 13 FAIL. 11 are item.misc-lane-only (unrecorded-item-pool
  class, classify after fixes). Two real forks, both root-caused deep:
  * auto-fox-2025-02_Game_20250224T030855 (YS) @3503: spurious CPU-Nana SDI (+6.0
    exactly; retail frozen in hitlag). Full causal chain established with retail
    probes: Fox's PUBLISHED capsule extent x1A88.x560 (ftCo_800A0DA4 max over hurt
    capsules) diverges at the frame-3470 publication (retail 3.446 vs ours 4.260;
    3465-3469 bit-exact) because cap[12] = THE TAIL (scale 1.62) sits 0.8 too far
    out -> Nana's B8A9C/B4AB0 range check admits an attack candidate ONE THINK EARLY
    (our x18 0->2 at think 3471, retail at 3472; x7C/x80/x570/positions/colliders all
    verified bit-identical) -> different B4AB0 Randf pick (frame-seeded): ours built
    a WaitFor(26+5)+PressR+LstickXForward(0x7F) roll macro, retail a WaitFor(10)+
    Y-tap macro -> our stale macro replays +127 during Nana's 3502-3504 hitlag ->
    x670 tilt-timer resets -> ftCo_Damage_OnEveryHitlag SDI fires.
  * THE TAIL SOLVER FORK (the dl-fox @5082 class, now minimally reproduced, wind-free):
    Fox's 4-link tail chain state at ftCo_8009DD94 entry is bit-identical at frames
    3466-3468 (per-link rot/s58/unk_2C anchors/params unk_44-8C, jobj jr/jt, full
    ancestor mtx walk incl. dirty flags, colliders x1670, arg1, and retail
    lb_804D63B0 emitters = NULL on YS both sides) yet the FRAME-3468 SOLVE (Fox taunt
    -exit + movement-start jerk) forks the free links' unk_2C anchors by 14-23
    degrees of link direction (root link 0.0 stays bit-exact -> parent pose agrees;
    ours under-rotates toward the new pose = tail trails wider). lb_8001044C C source
    == refs C (only documented fmadds deltas). NOTE the jr-equality at next entry is
    NOT evidence the solved rotations matched — anim overwrites jobj->rotate before
    the next DD94; the surviving solver state is unk_2C (+un-dumped unk_38/unk_44
    angular velocity). NEXT: stage-by-stage link_dir dump inside our lb_8001044C at
    frame 3468 (after stiffness/gravity/force/angvel/max-angle(unk_88=0.0524!)/
    convergence/deviation/collider/ground stages) vs retail's implied final dirs
    (derivable from the 3469-entry anchors); suspect a branch-boundary flip or an
    op-level slip in a jerk-only stage (max-angle clamp math, RotateAboutUnitAxis,
    or the ground-collision mpCheckFloor block near the YS slope).
  * master-diamond residual @-19 (post-cardinals): Nana entry-approach clamp 125 vs
    127 (1.4*125/127 velocity signature) = the AA42C clamp fed by published capsule
    extents — plausibly the same tail/extent class (Sheik hair dynamics), park until
    the solver fix lands.
  TOOLING: slippi-dolphin 1872b778b9 (tail probe: MSL_TAIL_PROBE_PORT, per-link
  params, ancestor walk, emitter global; ring probe: x80/x84/xA4/x570 + target
  extents). Container /work (env-gated, uncommitted): SDI-CHECK in ftCo_Damage.c,
  NANAIN x18/x670 fields + THINK buffer dump in ftCo_0A01.c, B8A9C-IN in
  ftcpuattack.c, EXTENT capsule dump in ftCo_800A0DA4, DD94 trace parameterized
  (MSL_DD94_PLAYER/START/END) + PARAMS/ANC rows in ftdynamics.c, frame-gated
  msl_nana_trace_file (gm_8016AEDC-based, prints F<frame>) in match.c, rand/randf
  trace in sysdolphin/baselib/random.c. Mainline bot captures are per-frame-seeded
  like online games (verified: base+frame<<16 stride in our stream).

## Active structural packet — canonical ordinary Figa program clock

- **Final owner and canonical state:** for a native registered fighter subtree whose attached
  nodes all consume the same direct compiled Figa program, the first attached pose node owns the
  one frame, rate, rewind, end, and animation-flag state. The remaining admitted nodes retain only
  their existing program-node identity, filtered-publication bit, and canonical JObj SRT output;
  their dormant per-node clock words are not updated in parallel.
- **Consumers:** the existing fighter-parts animation traversal publishes one frame-major program
  row into the same JObjs, then runs the same dependency and RObj owners. Tree request/rate/flag
  queries consume the singular clock directly. An individual-node request, non-direct decoder,
  excluded part, or different animation owner first materializes the shared clock back into the
  existing per-node state and then continues through the singular source path.
- **Displaced work and deletion boundary:** delete repeated clock advance, loop/end admission,
  table lookup, active/ended accounting, and clock-state stores from every later node in the
  ordinary main-pose span. Encode the clock owner in the otherwise-unused `track_start` word only
  while every admitted node has zero mutable decoder tracks; add no sidecar, copied pose, setter
  hook, callback tape, action/character list, allocation, compiler control, or approximate math.
  The group is one canonical owner, not a fast-path cache: dissolution is an ownership transfer,
  and no grouped node retains independently changing clock state.
- **Bounded scope and acceptance:** native direct compiled Figa subtrees only; descriptor,
  fractional/non-table, individually requested, filtered-out, PPC, and Wasm owners remain source
  shaped. Keep the implementation only if both production digests, focused animation
  request/rate/loop transitions, copy/save/restore/allocation gates, and controlled resident-256
  and resident-512 timing improve. The campaign checkpoint still requires at least +5% over
  committed `02cfe013` at both sizes; do not justify the representation with future geometry work.

**Closed — rejected 2026-08-04.** The exact full implementation regressed controlled resident-256
throughput by 3.9--4.2% and resident-512 by 0.5--0.6%. The implementation was removed in full. The
complete ownership, correctness, binary-provenance, and ABBA evidence is retained in
`agent_docs/performance/attempts/2026-08-04-shared-figa-program-clock.md`.

## Active structural packet — resident step/output fusion

- **Final owner and canonical state:** the runtime batch loop owns iteration across independent
  Matches; each `MslCoreMatch` and the caller-owned observation/terminal rows remain the only
  mutable state. No field or output representation changes.
- **Consumers:** the existing public `msl_batch_step[_masked]` operation and the production replay
  benchmark consume the fused runtime operation. Standalone step, observe, and terminal APIs remain
  source-compatible and keep their existing semantics.
- **Displaced work and deletion boundary:** replace the production operation's three complete
  resident-batch sweeps (step all, observe all, terminal all) with one validation pass and one
  per-Match `step -> observation -> terminal` loop. Delete repeated selection, initialization,
  viewpoint, row-address, and cold Match revisit work from that operation. Do not interleave
  scheduler phases across Matches, copy state, add a cache/sidecar, allocate, or change output
  ordering within a Match.
- **Bounded scope and acceptance:** core batch runtime, public API call site, and production
  benchmark call site only. Preserve masked-step behavior (only selected Matches step, every Match
  is observed), exact digests, allocation/save-restore behavior, and standalone API gates. Retain
  only if controlled resident-256 and resident-512 throughput improves.

**Closed — rejected 2026-08-04.** Exact same-source comparisons regressed resident-256 throughput
by 0.5--1.8% and resident-512 by 3.0%. The implementation was removed in full; retained evidence
and revisit criteria are in
`agent_docs/performance/attempts/2026-08-04-resident-step-output-fusion.md`.

## Current aggregate checkpoint screen — 2026-08-04

- Frozen committed control: `02cfe013`; candidate is the clean retained dirty aggregate after
  removing both rejected structural packets. Strict native release, CPU 0, balanced 366-case
  manifest, 262,144 measured match-frames, eight warmup ticks.
- Resident 256 control/candidate pairs are `41,259.8/38,405.2`,
  `40,870.5/40,160.5`, and `39,474.1/38,353.1` cycles/frame. Paired throughput gains are 7.43%,
  1.77%, and 2.92%; median is **+2.92%**, digest `bdff41cf74a54850`.
- Resident 512 control/candidate pairs are `39,595.9/37,030.3`,
  `38,904.2/38,543.6`, and `39,066.9/36,518.7` cycles/frame. Paired throughput gains are 6.93%,
  0.94%, and 6.98%; median is **+6.93%**, digest `ee9d93c545aa3ef9`.
- Decision: no checkpoint commit. Resident 256 is below the required +5%, despite a real aggregate
  win and a qualifying resident-512 median.
- Fresh target attribution again places 17.23% in pose animation, 12.16% in stage collision, and
  7.07% in action-animation callbacks. The latter is dominated by the already-documented guard
  pipeline (`ftCo_Guard_Anim` 2.01%, `ftCo_GuardOn_Anim` 1.11%); exact guard fusion and
  frame-completion query rewrites are closed negative results, so do not retry them as the next
  packet.
- A fresh 32,768-frame resident-512 profile tested the only previously unreviewed generic owner,
  fighter command-script execution. It owns 9.92M cycles, only 0.60% of the 1.647B-cycle
  instrumented contract. The same profile places pose animation at 15.81%, stage collision at
  12.59%, action-animation callbacks at 7.12%, dynamics at 5.92%, and input/action callbacks at
  4.55%. Command-script compilation cannot materially advance the target even at a zero-cost
  ceiling; do not build it. The remaining large scalar owners have all had their narrow deletion
  boundaries exhausted in the retained history. Further work must replace a cross-owner canonical
  state/execution boundary, not continue callback, wrapper, or arithmetic-leaf accumulation.

## Rejected geometry packet — unchanged direct-pose publication

- **Final owner and canonical state:** each registered fighter JObj remains the sole owner of its
  SRT, dirty flag, and world matrix. The compact pose program remains the sole immutable sample
  owner. A pose node may omit a source dirty publication only when every authored component in the
  direct row is bit-identical to the current canonical SRT and construction has proved that no
  retained external matrix owner can make that JObj matrix disagree with its SRT.
- **Consumers:** the existing stage ECB, hurt/hit/contact, dynamics, camera, attachment, and action
  consumers continue reading the same JObj fields and matrices. No product cache or consumer API is
  introduced.
- **Displaced code/state:** direct Figa publication currently stores the same values and marks the
  matrix dirty even for repeated rows, forcing later matrix/trig reconstruction. A prior census
  measured 1,247,936 bit-identical rows among five million direct publications (25.0%). The earlier
  unconditional omission changed output because retained dynamics and other direct-matrix owners
  can leave a clean matrix that is not the authored SRT product; it did not test a complete
  construction-owned exclusion.
- **Deletion boundary:** mark every retained dynamics-chain JObj as externally matrix-authored in
  existing pose-node descriptor bits during construction. Only ordinary direct program rows on
  unmarked nodes may compare-and-omit their complete publication; marked, decoder, filtered,
  quaternion/path/RObj, blend, root-placement, and non-native owners retain source behavior. Add no
  mutable cache, synchronized state, allocation, character/action list, approximation, fallback, or
  compiler control. If the first exact screens still expose another matrix writer, attribute and
  include that finite source owner before performance disposition rather than weakening the proof.
- **Acceptance:** both production digests and the full supported-domain gate must remain exact. The
  complete candidate must delete downstream matrix setups in a fresh counter/profile and improve
  controlled resident-256 and resident-512 throughput; otherwise remove it and record the closed
  matrix-writer boundary in the indexed history.
- **Result:** remove completely. Marking dynamics-authored JObjs at construction made the omission
  exact at both short resident sizes, but the hot comparisons cost more than the downstream matrix
  work they avoided. Two resident-256 directions were noisy (`40,928.8 -> 40,954.8` and
  `42,272.7 -> 41,572.1` cycles/frame), centering about 0.8% faster. Both resident-512 directions
  were decisively slower (`42,408.5 -> 46,391.5` and `40,839.3 -> 46,131.5`), a 10.0% throughput
  regression at the symmetric center. Exact digests were `4124834367a202ec` and
  `588be8489df1a62d`. Repeated-row detection is therefore closed: changing demanded geometry must
  remove the scalar publication/consumer boundary, not put comparison work in front of it. Full
  evidence and revisit criteria are in
  `agent_docs/performance/attempts/2026-08-04-unchanged-direct-pose-publication.md`.

## Active batch packet — configuration-local resident order

- **Final owner:** `MslCoreBatch` assigns public lanes to stable physical Match slots before first
  full construction. Public lane indices, input/output rows, masks, and reset/copy/save/restore
  indices remain unchanged through two inverse index arrays; gameplay always traverses physical
  Match storage sequentially.
- **Canonical state and consumers:** each `MslCoreMatch` remains the sole mutable gameplay owner.
  The order contains indices only and is consumed by the existing Match-major production step.
  Ordering is rebuilt after a reset, copy, or restore can change a lane's configuration.
- **Displaced work:** the current manifest-order loop alternates unrelated character code, motion
  banks, and stage topology across arenas. A construction/configuration-local order groups the
  immutable owners reused by the complete frame without splitting the source scheduler or
  republishing hosted context inside a Match.
- **Deletion boundary:** no gameplay state, callback, phase, arithmetic, allocation after batch
  creation, worker, scheduler seam, compatibility path, or approximation. Match arenas never move
  after construction. The final form is two allocated inverse index arrays and a one-time stable
  assignment by supported character tuple and stage when the first reset initializes the complete
  batch; a batch first initialized partially keeps identity assignment.
- **Acceptance:** preserve both production digests plus mask, arbitrary-index copy/save/restore,
  and allocation behavior. Compare against a byte-frozen immediate dirty aggregate at resident 256
  and 512. If static configuration locality is insufficient, reassess current action/callback
  locality inside this same index owner before rejecting the packet; do not turn it into phase
  interleaving or a generic scheduler framework.
- **Rejected first form:** sorting only the hot execution index preserves both short digests but
  jumps among the existing 3 MiB physical arena strides. Resident 256 was only 0.64% faster in the
  first direction. At resident 512 the two candidate runs cost `53,278.3` and `53,872.1`
  cycles/frame against adjacent controls `44,815.4` and `47,267.3`, about 10--14% slower. This
  disproves logical-only reordering, not configuration locality: the completed representation must
  assign configurations to physical slots before Match construction and retain sequential arena
  traversal. The logical-only loop and rebuild hooks are removed rather than layered underneath it.
- **Global physical screen:** the complete two-map physical form passes native mapping/lifecycle
  smoke and exact production digests. Against committed `02cfe013`, three resident-256 pairs are
  +6.08%, +4.08%, and +7.39% (median +6.08%). Five resident-512 pairs are +2.45%, +4.39%, -5.89%,
  +4.04%, and +1.94% (median +2.45%) under measured host contention. A direct 262,144-frame
  immediate-control pair at 512 is only +0.44%. Global grouping therefore does not yet repay the
  public input/output row permutation at the larger resident size.
- **Final integration refinement:** confine physical assignment to 128-lane groups. The roughly
  128 KiB observation write window and its public index pages remain local while each group still
  contains enough repeated character/stage owners to improve source-code and immutable-data reuse.
  This is a fixed cache-derived layout, not a runtime knob. Compare it to both the global form and
  the immediate aggregate; retain only the best exact form at both sizes.
- **Result:** reject and remove the physical assignment. The final adaptive form used one global
  group through resident 256 and 128-lane groups above it. It preserved both production digests
  and all batch-index/API smoke, and one resident-512 immediate pair improved 7.1%. Isolation at
  resident 256 did not survive reversal: the symmetric 131,072-frame center regressed about 1.9%
  against the frozen immediate aggregate. The apparent +3.04% median against committed
  `02cfe013` therefore mixed this packet with earlier dirty wins and is not attribution. The two
  inverse maps, construction sort, and every API translation are removed; only the pre-existing
  aggregate remains. Full evidence and revisit criteria are indexed in
  `agent_docs/performance/attempts/2026-08-04-configuration-local-resident-order.md`.

## Stale duplicate record — dense canonical JObj matrices

- **Final owner and canonical state:** each native non-Wasm `HSD_JObj` retains one canonical matrix
  through an owned `MtxPtr`; the existing Match-local HSD matrix pool owns the 48-byte storage.
  PPC and Wasm retain the retail inline matrix. JObj SRT, dirty flags, topology, animation state,
  and all matrix values and lifetimes remain singular and unchanged.
- **Consumers:** every existing pose, dependency, ECB, dynamics, hit/hurt/contact, attachment,
  stage, item, camera, and viewer consumer continues using `jobj->mtx`. C array-to-pointer decay
  already gives these consumers the same expression type, so no lookup API, admission branch, or
  compatibility wrapper is introduced.
- **Displaced state/work:** remove the 48-byte inline matrix from the native 184-byte mixed JObj
  record and replace it with one eight-byte pointer, shrinking the hot SRT/topology object to 144
  bytes. Matrices allocated in JObj construction occupy one dense 48-byte pool stream instead of
  being interleaved at a 184-byte stride with graph, animation, and material pointers.
- **Deletion boundary:** the old inline storage is absent; there is no copied matrix, cache,
  fallback dispatch, phase seam, gameplay-time allocation, approximation, compiler control, or
  changed operation order. Construction allocates the matrix before publication and JObj release
  returns it to the same fixed pool. Generated relocation includes the one matrix pointer, and the
  sealed runtime reserve must cover all later JObj construction.
- **Why this differs from closed layout work:** the rejected 176/192-byte JObj packets merely
  reordered or aligned the same interleaved bytes. This cut changes the canonical storage boundary
  and densifies both independently consumed streams while reducing the JObj stride.
- **Acceptance:** require exact production digests, native API/copy/save/restore/allocation gates,
  and controlled improvement at both resident sizes against the frozen immediate aggregate. A
  fresh cache profile must show where matrix/JObj misses moved. Reject the complete representation
  if its extra pointer load and pool metadata outweigh the denser streams; do not follow it with
  field-padding tuning.
- **Disposition:** this duplicate proposal was superseded by the completed 9--10% negative result
  recorded earlier in this file. Its code has now been removed after the tiled-packet salvage
  error was identified; the canonical matrix is inline again.

## Active representation packet — compact resident Match arena stride

- **Final owner and canonical state:** `MslCoreBatch` retains one fixed sealed arena per Match and
  every source object, pointer, relocation token, byte, and lifetime remains unchanged. The batch
  arena mapping owns only the distance between otherwise independent Match graphs.
- **Consumers:** every Match-major animation, collision, input, dynamics, contact, camera, and
  output owner traverses the same graph at the smaller resident stride. Reset, copy, save/restore,
  arbitrary-index APIs, and Wasm use the same fixed-capacity contract.
- **Displaced state and deletion boundary:** reduce the stale 3 MiB native lane reserve to 1.25 MiB
  after exhaustive current construction measured a 1,017,744-byte maximum, leaving about 29%
  headroom. Delete only unreachable virtual lane slack; add no compactor, pointer rewrite, dynamic
  capacity, fallback allocation, gameplay allocation, scheduler change, or platform hint.
- **Acceptance:** exact digests, maximum-construction/runtime-census, native allocation and
  copy/save/restore gates, then controlled immediate A/B at resident 256 and 512 against frozen
  dense-JObj candidate `cecbc384...`. Retain only if the smaller cross-Match address stride
  improves both resident sizes; otherwise restore 3 MiB and record the result.

**Closed — rejected 2026-08-04.** Exact 65,536-frame ABBA screens center roughly 2% faster at
resident 256 but roughly 2% slower at resident 512. The one-line capacity change is removed; full
evidence and revisit criteria are in
`agent_docs/performance/attempts/2026-08-04-compact-resident-arena-stride.md`.

## Closed representation packet — canonical fighter JObj SRT

- **Final owner and canonical state:** each registered native fighter-pose node owns its one
  40-byte `MslJObjSRT`; its HSD_JObj points to that record. Non-fighter native JObjs own the same
  shape in one Match-local fixed pool. PPC and Wasm retain the inline retail fields. The existing
  dense matrix pool remains the one derived world-matrix owner.
- **Consumers:** compact Figa evaluation, dependencies, blends, dynamics, ECB, hit/hurt/contact,
  attachments, camera, and viewer all consume the singular SRT through the JObj. The hot compact
  pose loop writes SRT beside its clock/program metadata instead of scattering stores into a
  separate class-object stream.
- **Displaced state and deletion boundary:** delete the native inline JObj rotation, scale, and
  translation fields. Construction registration transfers their value into the pose node and
  returns the temporary generic record to its fixed pool. Add no shadow SRT, cache, dirty compare,
  fallback dispatch, scheduler seam, gameplay allocation, approximation, or operation reordering.
- **Bounded scope and potential:** one 40-byte record, the existing pose-node allocation, one
  generic fixed pool, and direct JObj SRT access conversion. Raw per-node state stays effectively
  flat while the 18% pose owner and its collision/dynamics/contact consumers stop traversing two
  independently allocated mutable streams. Require exact digests and full native lifecycle,
  allocation, copy/save/restore validation before controlled immediate 256/512 A/B.
- **Result:** reject and remove both complete ownership forms. Pose-node ownership enlarged the
  mandatory pose record from 56 to 96 bytes and improved the short symmetric centers only about
  0.7% at resident 256 and 0.6% at resident 512. Fixed-pool ownership restored the 56-byte pose
  record, but its 65,536-frame bracket regressed the resident-256 center about 3.2% while improving
  resident 512 only about 0.5%. Both forms preserved exact digests. The final source-timed profile
  still assigns 18.01% to pose animation, 12.02% to stage collision, and 30.86% to the inclusive
  scheduled fighter owner: SRT relocation did not delete demanded work. The pointer, pool,
  registration transfer, and all access conversions are removed. Detailed evidence is indexed in
  `agent_docs/performance/attempts/2026-08-04-canonical-jobj-srt.md`.

## Current aggregate recheck — 2026-08-04 evening

- Frozen committed control remains `02cfe013`; the candidate is the retained dirty aggregate with
  dense canonical JObj matrices and without the rejected SRT, arena-stride, resident-order,
  repeated-publication, shared-clock, or fused-output experiments. Three 262,144-frame alternating
  pairs preserved digests `bdff41cf74a54850` / `ee9d93c545aa3ef9`.
- Resident 256 paired throughput changes were +3.73%, +3.57%, and +5.91% (median **+3.73%**).
  Resident 512 changes were -5.19%, +1.68%, and +0.82% (median **+0.82%**). The first 512 pair is
  an outlier, but the remaining evidence still plainly fails the +5% checkpoint requirement.
- The host was materially contended during this screen (Path of Exile on CPU 31 and Dolphin on CPU
  28, plus browser load); cycles ratios remain the disposition metric and no absolute-FPS claim is
  drawn. There is no checkpoint commit. Stop accumulating arithmetic leaves: the retained profile
  still demands a representation with a multi-thousand-cycle ceiling.

## Active representation packet — lossless compact pose samples

- **Final owner and canonical state:** each immutable direct Figa program remains the sole owner of
  the exact sampled float bits. A node whose complete authored row is bit-identical at every sample
  owns one constant row; every other admitted node owns the same frame-major sampled rows as today.
  Mutable JObj SRT, animation clocks, decoder fallback state, and dirty-matrix semantics are
  unchanged and singular.
- **Consumers:** the existing compact pose interpreter selects the node's one exact row and feeds
  the unchanged mask-specialized JObj publication. Stage collision, dynamics, contact, hit/hurt,
  camera, attachment, PPC, and Wasm consumers remain untouched.
- **Displaced work/state and deletion boundary:** delete repeated copies of motion-wide constant
  rows from the immutable frame table. The construction census contains 19,855,156 floats
  (75.7 MiB); 46,537 direct nodes are constant for their complete motion, so node-level compaction
  removes 16.6 MiB while adding only one retained row per such node. The old duplicated rows are
  absent after construction. Add no dictionary lookup, quantization, approximate math, mutable
  cache, sidecar pose, gameplay allocation, compatibility bridge, or compiler control.
- **Bounded progression and acceptance:** implement and measure node-level compaction first. If it
  proves the table is bandwidth/cache-sensitive, extend the same singular representation to the
  39,317 mixed nodes whose constant components can remove another roughly 12 MiB. If the first
  form is neutral or slower, record that the 75.7 MiB table is not the limiting owner and remove
  the representation rather than tuning encodings. Require exact production digests, native
  smoke/allocation/save-restore, controlled immediate 256/512 A/B, then comparison with committed
  `02cfe013`; the campaign checkpoint remains +5% at both resident sizes.
- **First-form result:** the initial constant/dynamic branch was approximately -0.6% at resident
  256 and +1.0% at 512 and was replaced, not tuned. The corrected representation gives every node
  one absolute value offset and one sample stride (zero for a constant row), so publication uses a
  branchless address calculation and no longer loads the program row base/stride. It preserves the
  established 65,536-frame digests. Against the frozen immediate aggregate, the 256 C/A and A/C
  arms were `39,726.0/39,514.5` and `39,725.4/39,486.0` cycles/frame (neutral symmetric center);
  the 512 arms were `42,025.3/39,862.3` and `39,741.0/39,964.4` (about +3.0% at the symmetric
  center under heavy host contention). Keep this form provisional for the immediate contiguous
  program-span publisher: it reduces the immutable table and removes program lookup from precisely
  that consumer, but it is not independently a campaign checkpoint or justification for more
  memory-only encoding work.

## Active representation packet — tiled canonical fighter transforms

- **Measured reason for the cut:** the current resident-512 profile assigns 30.86% of the complete
  contract to `Fighter_8006A360`, including 18.01% to pose animation, and another 13.04% to
  `Fighter_procMap`. The historical exact batch-pose attempt paid about 6% for its scheduler seam
  and then lost additional time gathering and scattering SRT into independently allocated Match
  JObj graphs. Its completed result requires genuinely tiled mutable SRT before another batch
  evaluator is justified. Scalar pose clocks, sample encodings, JObj field ordering, and isolated
  ECB kernels have already failed to remove this producer/consumer cost.
- **Final owner and canonical state:** `MslCoreBatch` owns fixed eight-lane tiles of hosted fighter
  transform records. Each record contains the one exact mutable SRT and derived world matrix for a
  fighter gameplay JObj. The fighter-pose preorder supplies the stable node coordinate; each JObj
  retains topology, flags, animation attachment, and one pointer to its canonical record. No
  inline or Match-local fighter SRT/matrix remains synchronized with it. Native nonfighter JObjs
  retain the generic scalar owner; PPC and Wasm retain the retail inline representation.
- **Consumers:** main-pose publication, dependencies and blends, root placement, ECB, hit/hurt and
  shield geometry, attachments, dynamics, camera, viewer output, JObj mutation APIs, Match copy,
  and save/restore all read or mutate the same record. The batch scheduler groups the existing
  priority-one main-pose cut within each eight-Match tile and demanded fighter geometry is evaluated
  from the same tile before its existing source-ordered consumers resume.
- **Displaced work and deletion boundary:** delete scattered native fighter SRT/matrix allocation,
  the historical batch publisher's cross-arena gathers/scatters, and repeated scalar setup for
  tile-ready demanded fighter matrices. Do not introduce a pose cache, copied geometry packet,
  source/candidate flag, lazy production bridge, gameplay allocation, changed f32 ordering, or a
  generic operation scheduler. Exceptional source-authored JObj methods still operate on the same
  canonical record through the scalar exact method; they do not hand ownership to another state.
- **Lifecycle boundary:** batch creation reserves the fixed record capacity. Reset binds fighter
  construction to its lane; arbitrary-index copy and save/restore serialize the strided records as
  part of that Match's canonical graph. Partial resets may rewrite one lane but never allocate or
  repack gameplay state. The existing public lane order remains unchanged.
- **Bounded implementation order:** first move hosted fighter SRT to the tiled record and restore
  every lifecycle/correctness invariant; then land the exact tiled main-pose publisher; then move
  its demanded matrices and the immediate ECB/hit/hurt consumers into the same record owner. These
  are stages of one representation cut, not separately retainable setup. If the initial scalar
  conversion is slower, continue through the batch producer and demanded-consumer deletion before
  judging the packet.
- **Acceptance:** exact resident digests, zero gameplay allocation, arbitrary-index reset/copy/
  save/restore, full supported-domain validation, and controlled alternating comparisons against
  both frozen `02cfe013` and the frozen immediate dirty aggregate at resident 256 and 512. Retain
  only a complete ownership/deletion boundary; a qualifying commit still requires at least +5%
  throughput at both sizes, while the campaign remains open until both exceed 150k FPS.

### Completion correction — 2026-08-04

- The history already rejects generic tile-resident scheduling and a pose-only batch seam. Those
  are not new hypotheses and cannot justify this packet. The temporary single-context-pointer
  consolidation likewise repeats a closed context-layout direction and must not survive the final
  deletion boundary unless a complete transformed owner independently proves it necessary.
- The remaining materially different hypothesis is the canonical tile itself: exact publication
  shapes cover more than 97% of direct rows, while the current implementation still executes one
  scalar publisher per lane. The first retained-capable kernel is therefore the dominant `0x00E`
  rotation row (63.3% of dense publications) over eight lanes, with singular tiled JObj state and
  no scalar product cache. Clock/dependency/dirty semantics stay lane-owned and exact.
- This is only evidence for continuing the wider matrix/map boundary if it removes enough pose
  work to repay the already measured scheduler seam. A neutral scalar-equivalent result closes
  the tile representation; it does not justify another SRT layout or scheduler variation.
### 2026-08-04 tiled scheduler/direct-pose diagnostic

- The exact whole-batch phase sweep cost about 49.2k cycles/frame on the
  4,096-frame resident-256 screen. Restricting the same scheduler seam to
  eight-Match physical tiles reduced that to 45.3k without changing digest
  `1177a913e8074ce6`, proving the whole-batch working set was one real loss.
- Next diagnostic only: force admitted pose lanes through the scalar owner
  while retaining the tiled scheduler. This is an internal ownership
  ablation, not a parent control or retainable result; it separates scheduler
  seam cost from the new direct pose traversal before either is redesigned.
- Admission was high (`25,705 / 27,776`, 92.5%), so fallback was not the
  regression. Eight-lane scalar pose measured about 46.1k cycles/frame and
  direct pose about 45.3k on short screens; the direct owner recovered only
  about 0.8k inside a much larger scheduler-seam loss.
- Replacing dozens of TLS publications with one prepared context pointer did
  not change the eight-lane total (~49.2k before and after the isolated
  change). The dominant cost is therefore not TLS stores. It is scheduler
  splitting and loss of Match residency; the extra pointer layer is not
  justified as a standalone optimization.
- Completing all scheduler priorities within a bounded tile reduced the
  exact short screen from ~49.2k to ~45.3k at width eight. Width two screened
  around 44-45k and width one around 41-43k, all with exact digest
  `1177a913e8074ce6`. Short-run variance is too high to rank the last two, but
  the monotonic working-set result is sufficient: cross-Match pose batching
  does not recover its scheduler seam at this owner size. No result here is a
  performance candidate or checkpoint.

### 2026-08-04 tiled SIMD disposition

- The dominant `0x00E` publisher was completed as a real eight-lane AVX2
  gather/AVX-512 scatter kernel over the singular tiled JObj state. It preserves
  the exact 4,096-frame resident-256 digest `1177a913e8074ce6`, the 32,768-frame
  resident-256 digest `4124834367a202ec`, and the 32,768-frame resident-512
  digest `588be8489df1a62d`.
- At resident 256, reversed scalar/vector comparisons were effectively neutral:
  the clean symmetric centers were 46,177.7 scalar versus 46,204.8 vector
  cycles/frame (vector 0.06% slower). A separate bracket, excluding one obvious
  contended vector outlier, was likewise neutral.
- At resident 512, reversed pairs were 44,957.7/44,620.2 and
  43,911.5/43,146.2 scalar/vector cycles/frame. Their symmetric centers are
  44,434.6 scalar and 43,883.2 vector, so the kernel recovers about 1.25% inside
  the tiled candidate.
- That local recovery does not repay the architecture: the complete vector
  candidate remains about 43.9k cycles/frame at resident 512 versus 35,267.5
  for committed `02cfe013`, and its resident-256 screens remain around 45-47k
  versus 36,908.0. The completed kernel therefore disproves the remaining
  materially distinct tiled-publisher hypothesis. Do not spend more time on
  tile widths, scalar scheduler variants, TLS/context consolidation, or
  gather/scatter pose publication.
- No part of the tiled scheduler/context/external-JObj ownership packet is a
  retained performance result. Preserve the earlier dense canonical matrix and
  lossless compact-sample work while inventorying this packet for removal; do
  not blanket-delete it without the required salvage inventory and approval.

## 2026-09-05 Slippi-AI Python release library

Owner: root Makefile. Reuse the existing strict native release optimization
allowlists for Python PIC objects and expose a separate `python-release` target.
Consumers: Slippi-AI decomp RL on gigaserver via `MSL_CORE_LIBRARY`.
No gameplay state, source profiles, or API representation changes. Debug and
release object directories remain separate. Validation will compare bounded
native API behavior and run real Slippi-AI RL/evaluation through the release
library; every workload is guarded by Discord activity on gigaserver.

The doubles integration exposed an API constructor bug: `EnvBatch(num_players=4)`
called `_default_players(4)`, which raised before callers could configure the
match. Defaults now repeat Fox/Falco for four slots; explicit match configuration
continues to own characters and teams. The public Python API regression covers
construction, reset, and stepping. The release target shares the native target's
unsafe compiler-flag rejection. No native gameplay sources changed.

2026-09-05 long RL result: Fox/Peach doubles at 512 environments aborts a
worker around update 75 with `msl_memory_alloc(owner=1,sealed=1,size=1024)`
(`used=801036`, capacity 3 MiB). Eight bounded API tests did not reach this.
Investigate the exact runtime allocation owner with a separate debug library;
do not unseal arenas or weaken the no-allocation invariant. Preserve the
failed run at Slippi-AI `reports/triage/decomp_fp16_rl_20260905/demo.log`.

Root cause confirmed by a 64-match scripted Peach pull/throw probe in both
debug and release builds. `ftPe_SpecialLw::spawnVeg -> it_802BD4AC ->
Item_802680CC -> HSD_JObjLoadJoint -> hsdAllocMemPiece` exhausts the 128-piece
reserve. The extracted turnip graph has 17 JObjs; Peach's other reached article
graphs have at most 14. A 256-piece reserve cleared that failure but then hit
the 15-object Item pool near frame 6500. ItCo.dat permits 80 character items
and 40 common items; 15 is only the public observation slot count.

Final owners: `item.c` derives Item/DynamicBone pool capacity from the source
category limits; `scalar.c` enables common-item capacity when Peach is present
and reserves 17 joints per possible Peach item plus existing runtime headroom;
`class.c::msl_class_reserve_pieces` reserves only that reached size class.
All allocation remains in initialization. Temporary diagnostics are removed.
The 768,000-frame probe passes, as do all nine Python API tests (including
four-player construction for every supported character and a 512,000-frame
regression) and `native-smoke` with the complete construction census.

2026-09-05 terminal audit: doubles evaluation exposed `gm_80167320`
ending a match at the first player's last stock, while another member of
that team remained alive. Keep the canonical terminal flag in MatchRules;
replace the unconditional assignment with the source stock-elimination
criterion from `gm_16AE.c::{gm_GetFFAOutcome,gm_GetTeamBattleOutcome}`:
at most one player (singles) or team (doubles) retains stocks. Preserve
stock stealing and final-stock fighter teardown. Validate source callbacks
and full Python API games, then rerun the RL demonstration/evaluation.

Validation: native-smoke passed with the singles/doubles callback regression;
the scripted full Python doubles game passed. A fresh 150-policy-update RL
run completed without allocation failures. Evaluation won 128–0 plus 32–0
on a second seed set with reversed model arguments; all 160 terminal records
were independently checked to contain a fully eliminated losing team.
Final steady RL throughput was 47,449 unique environment FPS (33,722 including
startup and saves). Earlier premature-terminal results are superseded.

## Curriculum cleanup — 2026-09-07

User requested stashing the unsuccessful replay/drill experiments while retaining
the foundations for mixed singles, doubles, and asymmetric endgames. Removed the
native curriculum companion, capture/seal split, and drill-only position helper.
Canonical state remains MslBatch/Match; ordinary public save/restore owns snapshots.
Retained the pre-experiment release/item-capacity/four-player/whole-team-terminal
patch and source-owned destructor/color-stack relocation fixes with context smoke.
The relocation owners are gobjinit.c and lb/types.h; generated metadata serves all
copy/save/restore consumers without custom restore logic. No new mixed-start API.
Salvage inventory and full before-state archives: Slippi-AI
reports/triage/experiment_cleanup_20260907/. Full native stash:
165fcc34f3d5ffe4bba5d33ab6a3bec8be7708ff (baseline and relocation fixes reapplied).
Validation after cleanup: python-release, source-check, native-smoke, and all
10 native API tests passed. App learner/agent/environment checks passed; the old
mixed fake-env test was updated to pass the current player mapping and passed.
The isolated server checkout now links data/raw to the existing extracted data.

## Mixed training environments — 2026-09-07

Follow-up: user requested fresh 2v1 starts at one stock and independently random
0..100 percent. Match player configuration owns explicit starting percent;
the existing Player_SetHUDDamage initialization feeds Fighter_Create, including
paired entities, without a live-state setter. Slippi-AI samples per-lane seeded
integers for the next fresh reset; stored native endgames remain authoritative.
Update C/Python wire sizes together and test initialization, partial/automatic
resets, sharding determinism, restored-state preservation and a mixed GPU run.
Completed: native release/source-check/native-smoke, all ten API tests, eleven
app environment checks and generated viewer-schema check pass. A new mixed
512/256/512 GPU run completed 20 policy updates with 783 fresh 2v1 starts and
finite metrics; entry/partial/automatic-reset percent checks also pass.

User authorized mixed 1v1/2v1/2v2 training, plain three-player teams starts until
natural endgames are collected, and measured negligible overhead. Native owner:
existing public/runtime match configuration admits three players through the
normal source VS construction; Match remains canonical. Slippi-AI owns fixed
worker/player slices, one batched policy/learner, and the bounded event-start bank.
The bank stores full validated native saves only on natural 2v2-to-2v1 events,
plus short public observation/controller history; no periodic native snapshots,
TD-error selection, character mutation, or rewritten physics. Restored endgames
keep their original four-player Match and eliminated teammate; the three surviving
controllers use an explicit source mapping. Admission excludes stock-shareable
solo survivors so the absent policy cannot suppress a legal teammate revival.
Deletion boundary: replace fixed equal-player global batch indexing in the sim
rollout/worker adapter; retain homogeneous layouts as the same one-group case.
No deployed source or frozen experiment library changes. Before-state inventory
and patches: Slippi-AI reports/triage/mixed_envs_20260907/.
Validation: native three-player API/gameplay gates, role/observation/controller
mapping, episode/stock-share semantics, exact saved-start/prefix continuation,
mixed PPO smoke and equivalent-work CPU/GPU overhead measurements on gigaserver.

Implemented normal three-player VS construction in the existing API/runtime
validation, plus an optional Python step mask for held replay warmup. No wire ABI
or native Match representation changes. Native source-check, native-smoke and
all ten API tests pass, including three-player teams across the supported roster.
The app's paired 1,280-env frozen-policy check captured 55 natural events with
identical final trajectories, measuring +0.20% mean update time; timing noise
precludes a strict sub-1% bound. See the indexed performance history below for
the comparison contract, rejected intermediate overhead and warmup accounting.

## Linux-host certification + the pool-residue lanes are retired (2026-08-04)

Work moved to a native linux/amd64 host (Ryzen 9950X3D, Ubuntu 25.10). Certification per
`agent_docs/ENVIRONMENT.md` surfaced that four ness identities and, run-to-run, even the
same host's own fingerprints were unstable: the recorded snapshots hashed bytes that are
halves of 64-bit host pointers, so they were only ever reproducible inside the recording
container's memory layout. Measured directly: two back-to-back native runs of `50321`
produced two different mismatch fingerprints from an identical mismatch shape, and a
per-frame item-lane dump diffed across runs pinned every varying byte to one
ASLR-randomized pointer byte (0x8C vs 0x84) read through the generic
`item_var_source_byte` union path for articles whose native struct leads with widened
pointers (bat, yo-yo slot residue, PK Fire/pillar/flash, Fox Illusion).

Two projection-mask corrections retire the whole family, both inside the mask's existing
taxonomy (source-proven non-gameplay lanes):

- **Ness Yo-Yo misc1 (x4) is masked.** Its only owner, `it_802BFEC4`, writes it on the
  despawn transition, so no live export ever samples an owner-written value — the same
  never-written-at-sampling-time argument that already masks the PK Flash leading word.
  The swing read (`it_802BF800`) consumes the same residue retail does; any gameplay
  effect lands in directly compared lanes. misc0 (x0, the smash action id, written at
  spawn) stays compared.
- **Mr. Saturn misc2/3 are masked in every state.** Every `xDE4` write in `itdosei.c` is
  `= ip->pos`, a copy of the directly compared position lanes, and the writing Anim
  callbacks skip during item hitlag, so a state entered mid-hitlag (observed: state 11 at
  `51444` 10756) samples stale bytes or pool residue.
- **compare_row now requires both sides' masks to admit a misc lane** (generalizing the
  extra-needle special case): inside a classified divergence the hosted slot can hold a
  different article than the recording, and the bytes at an unowned offset are arena
  placement, not gameplay. The slot disagreement still reports through item.type/state.

Consequences, all gated: the nine `unrecorded-item-pool-residue` classifications (whole
family) are deleted because their replays are now bit-exact on both backends;
`2025-03`/`2026-06`/`53362` and peach `HeartyStiffMallard` re-recorded with **identical
native and PPC snapshots** (the masked lanes were the entire backend delta in every fork
capture); 13 output locks re-recorded (9 ness + 4 peach whose Saturn/kind-window bytes
were canonicalized). Fingerprints are now process-location independent: three consecutive
suite runs hash identically, and the aggregate reproduces bit-exactly under both gcc-15.2
and the pinned gcc-13 (local dpkg unpack, `build/host-gcc13/`).

Also fixed: `src/upstream.lock`'s tree digest was stale from the WIP import commit (its
recorded value is not reproducible from its own pinned inputs under any sha tool/format
variant; file_count was updated to 1155 but the digest was not). Re-recorded with
`tools/build/source_sync.sh`'s canonical method; `make source-check` passes with 206
local deltas.

**Suite state (linux/amd64 host + gcc-13 cross-check authoritative): ness 30 = 26 pass /
4 classified / 0 fail on native AND on PPC** (classified: `2025-02` + `2025-03`
dolphin-ULP, `2026-06` presentation-RNG draw gap, `53362` hitlag pad-edge debt).
Aggregate **426 = 342 pass / 84 classified / 0 fail / 0 error**, lock-clean, xpass-free.
Host certification: source-check, native-smoke, aggregate, per-suite ness PPC
(`--timeout 240`; qemu here needs ~100s on 17k-frame captures), pytest 45 passed /
1 skipped (no local ISO at the tested path) — all green.

## Icies misc-lane retirement + popo_replays import (2026-08-05)

The Ice Climbers article lanes joined the projection mask's source-proven non-gameplay
taxonomy, retiring the whole `unrecorded-item-pool-residue` family (24 replays) and the
belay-string `unrecorded-item-pointer-identity` classification:

- **Ice (106) keeps misc1 only.** x0 is the spawner GObj pointer written at spawn
  (`it_802C1590`) — allocator identity; x4 is the live scale, spawn-written from
  x60_scale and bounce-decayed (`itClimbersice_UnkMotion0_Anim`), so xDDB stays
  compared; the declared struct ends at xC, leaving 0x17/0x1B past-member residue.
- **Blizzard (107) keeps misc0 only.** x0 is the spawn-written scale
  (`itClimbersBlizzard_Spawn`); the only other member is the flag0 bit in xDD8's
  leading byte, so offset 7 samples an unwritten byte and 0x17/0x1B fall past the
  declared members.
- **Gum strings (113) is fully masked.** x0's only owner write is the constant 2.25f
  (`it_802C3864`) whose sampled low byte is 0x00 and the spawn constructor
  (`it_802C27D4`) leaves it unwritten — the Link-bomb direction-sign argument; x4/x8
  are the string ItemLink chain, xC the owner GObj, x14 the tail joint, and the
  struct ends there.

`dl-fox-2025-05` was a two-residue entry; its wind-machine half survives alone under a
new `dreamland-wind-rng-draw-gap` id (grOldPupupu_802113E0's idle-cycle rand_range
pair is still not consumed by the hosted wind-direction mirror; 11 forked frames,
5205-5215, identical native/PPC fingerprints so the PPC snapshot aliases native).

The 2026-08 `popo_replays.zip` import added five screened singles to the icies suite
(25 -> 30; Fountain, frozen Stadium, Dream Land, Battlefield, FD; opponents add
Donkey Kong). The Stadium capture is 3.18.0, where is_frozen_ps is untrustworthy, and
is event-verified frozen (0x41 declared and empty over the full game). The zip's sixth
game (`23549`, Ice Climbers/Peach on Yoshi's) was initially rejected for a one-frame
item_count divergence at 2076; it was triaged and admitted the next day (below).

**Suite state: icies 30 = 29 pass / 1 classified / 0 fail on native AND on PPC**
(classified: `dl-fox-2025-05` wind-RNG draw gap). Aggregate **491 = 425 pass / 66
classified / 0 fail / 0 error**, lock-clean, xpass-free (30 icies output locks
re-recorded/added; aggregate coverage tests bumped 486 -> 491). source-check,
native-smoke, format-check, and pytest green (the parallel-oracle test only fails
when run alongside a live aggregate run — worker OOM-kill, passes in isolation).

## 23549 admitted under peach-parasol-landing-teardown-window (2026-08-05)

The rejected sixth popo game is triaged and in the suite (31 replays). The frame-2076
divergence is not an extra article: the hosted projection publishes Peach's closed
parasol (spawn id 11, its frozen spawn-position row at 67.36/-11.93) exactly one item
sample longer than the recording. The recorded episode ends at 2075 and the window is
her landing out of FallSpecial (recorded post state 35 -> 14 across 2075 -> 2076),
whose transition tears the article down (`ftPe_8011D518` / `it_802BDB94`); Slippi
samples items from a player-plink GObj proc installed before fighter processing, so
the teardown lands one sample apart between the recording schedule and the hosted
publication boundary. Whether retail retires the article in the preceding item phase
or the landing frame's fighter phase needs the Dolphin scenario probe. Native and PPC
fingerprints are identical (`ed25d016f39d4dd5`), so the PPC snapshot aliases native.

**Suite state: icies 31 = 29 pass / 2 classified / 0 fail on native AND on PPC**
(classified: `dl-fox-2025-05` wind-RNG draw gap, `ys-peach-2025-02_23549` parasol
teardown window). Aggregate **492 = 425 pass / 67 classified / 0 fail / 0 error**,
lock-clean, xpass-free; the 23549 output lock is recorded and the aggregate coverage
tests bumped 491 -> 492.

- Pre-commit performance attribution: ten paired samples show -0.335% at
  resident 256 (paired log-ratio 95% interval -0.606%..-0.065%) and +0.001%
  at 512 (-0.558%..+0.563%). Diagnose the source Gekko camera square-root
  restoration using an isolated object/executable, preserving production code.
  This temporary arithmetic ablation is not a candidate correctness change.
- Attribution result: isolated hardware-sqrt camera ablation is faster by a
  median 0.55% in six 256-match pairs. Keep source Gekko arithmetic. Test only
  runtime/camera.c at strict O2 to recover added instruction/control overhead;
  canonical state, arithmetic and all other compiler profiles stay unchanged.
  Prior rejected camera O2 record concerns melee/cm/camera.c (Camera_8002B3D4),
  a different translation unit. Require full output-lock equivalence and paired
  performance before retaining this compiler admission.

- Pre-commit recovery retained: runtime/camera.c strict O2/native release ISA
  preserves all 529 locks on the fresh root build. Final eight-pair comparisons
  against frozen 24643394 have paired medians +0.353% (256) and -0.004% (512),
  unchanged digests. Raw means and intervals, including timing outliers, are
  retained in performance/HISTORY.md; no strict sub-percent latency bound claimed.
  Commit only this completed correctness packet and its validation/performance
  records before starting the newly authorized completion campaign.
- Lifecycle dependencies found before execution: FD also reads fog/background
  color when choosing later RNG colors, and its start callback unpauses the
  scene controller. Restore source fog construction and stage OnStart dispatch.
  Keep fog as the source HSD_Fog object with typed relocation; fix the source
  callback queue's three-pointer allocation/signature for native width and drain
  it at startup. Ground parameters contain writable palette fields: initialize
  one Match-owned parameter value from the immutable DAT template, then make
  stage_info.param point only to that value. Delete direct mutable use of shared
  DAT parameters. These are parts of the same source lifecycle, not new mirrors.
- Native data representation finding: grAnime_801C7C1C indexes packed stage
  animation nodes (GALE01 0x801C7CB8 stride 0x14; material/shape stride 0xC).
  Generic HSD graph translation allocated only one node, so FD part-2 indexing
  entered unrelated data. All 166 authored stage animation arrays across the
  six raw archives are connected contiguous node arrays with no overlapping
  ranges (stage-anim-layout.json). Final owner: initialization-only map_head
  translation allocates each packed array once and registers every interior
  node before translating links. Consumers retain source pointer indexing and
  traversal; delete per-node allocation for these arrays, with no runtime fixup
  or second representation. Other HSD graphs keep their existing owner.
- First FD result: master-samus-2026-03 is raw exact (135 mismatching
  transitions -> 0) with the full source scene/actor lifecycle and contiguous
  animation arrays. No rope solver changes or new replay inputs. Next challenge
  shared Ground/material changes against the remaining suite and original exact
  cases before updating any identities.
- Startup phase correction: retail controller probes at -123..-121 keep the
  paused bit set and timer zero. Ground OnStart is part of fn_8016B7F8, alongside
  the already-modeled ftLib_800868A4 ready transition before raw frame -39; it
  must not run in construction. Move it to that existing phase boundary. The
  pending callback queue now survives public save/restore during intro, so its
  canonical three-pointer node needs a typed relocation entry. The earlier
  Samus raw-exact observation is provisional until this phase is corrected.

- PPC math arbitration: all seven remaining camera cases are now exact on the
  reference backend after disabling sinf/cosf builtin substitution in the PPC
  MSL owners. Object disassembly previously called libc sincosf from tanf; now
  it calls the source MSL sinf/cosf. Native and PPC remaining mismatch families
  agree. Apply the same builtin policy to Wasm.
- Next bounded probes compare Link boomerang throw attachment/world-position
  at 6873 and the Luigi/Falco pose/collision families against retail execution.
  Keep existing JObj matrices, item position and fighter ECB as canonical state;
  diagnostic hooks are temporary and removed before validation.

- Remaining mixed probes: Link throw output is retail/native c13bbd02 against
  recorded c13bbd00. Luigi replay families at 2739/3644/8142/8204 likewise match
  retail instead of recorded bits. Falco 990 still forks before physics; trace
  that separately. Doubles 5423 forks during DI magnitude: ftCo_8008E5A4 still
  uses libc sqrtf where GALE01 8008E670..6B8 executes frsqrte plus three double
  Newton steps. Replace this one source-owned sqrt with msl_gekko_sqrtf, then
  challenge all remaining cases. No new representation or input inference.

- DI sqrt experiment did not change this case. The causal doubles input is
  earlier signed zero: retail DI sees kb_y=+0, hosted sees -0, selecting opposite
  atan2 pi branches and generating the later numeric tail. Probe equal +0 input
  at this leaf temporarily, then remove the diagnostic mutation.
- Falco 990 damage-entry position matches retail (3ff857b8); recording PreFrame
  position is 3ff857b5. Playback resynchronizes to that recorded pre-position
  before physics. Thus comparing only the later playback output gave a false
  source-defect signal. Retain before-resync source probe plus recorded pre bits.

- Equal-input DI test removes every doubles nonzero numeric mismatch: 380->322
  rows, with only signed-zero fields remaining. All 83 numeric field differences
  are downstream of the historical zero-sign input. Removed that diagnostic
  mutation and the non-causal sqrt candidate; no gameplay fitting retained.
  All source/runtime diagnostic prints are removed. Current closure remains
  503 exact/26 exceptions, with the mixed cases fully separated by causal owner.

- Final verification/performance packet: rebuild strict release and compare
  all full native output bytes before refreshing identities. Preserve the 403
  checkpoint benchmark tapes/manifest; prepare v2 tapes separately, then verify
  identical inputs and settings after removing only the new Whispy capability
  and version byte changes. Time adjacent alternating checkpoint/candidate runs
  on CPU 0 at resident 256/512, 262144 match-frames, 8 warmup ticks, 128 history.
  Finish all builds and gates before controlled timing; retain every sample.

- Controlled stage-completion performance result: eight alternating pairs at
  each resident size show 120656.5->110555 FPS (-8.37%) at 256 and
  125211->114350.5 FPS (-8.67%) at 512, with unchanged digests. This is a real
  cost and remains open. Profile callback owners before changing the completed
  representation. Keep source stage FSMs, actor lifetime, animation completion,
  color state and collision epochs; do not restore displaced controllers.
  Review found the previous epoch suppression failed 90 replays and the earlier
  Dream Land response cut already has a narrower accepted handshake boundary.

- Current subsystem profile puts Ground_801C1CD0 at 2.83% and the headless
  epoch proc at 0.50% of the instrumented contract, insufficient by themselves
  to explain the 8.7% regression. Build an isolated checkpoint profile for
  owner-by-owner attribution on identical tapes; do not infer an animation cut
  from the total timing alone. Profiling is diagnostic, not throughput evidence.

- Profile attribution: source FD now binds its collision JObj and publishes
  CollJoint_B8 through Ground_801C2ED0/2FE0. Its stationary lines consequently
  enter mpRemap2d; the old manual constructor never bound this source transform.
  Fighter collision grows 356M->459M cycles while Ground grows 38M->88M.
  First performance candidate keeps that complete binding/epoch/flag state and
  specializes mpRemap2d itself: within the nondegenerate finite branch, equal
  old/new endpoints and finite nonzero query coordinates imply all displacement
  terms are zero, so both source f64 FMAs return the original query exactly.
  Zero coordinates and degenerate/moving lines retain source evaluation. Final
  owner is mpRemap2d; no cache, mirrored state, stage-id guard or altered epoch.

- Stationary-remap candidate preserves all 529 full release output locks, but
  two reverse-order pairs recover only 0.46%/0.57% at 256/512. The larger
  collision cost remains. Use a bounded production-only gprof sampling binary
  (root build flags/objects, cold benchmark moncontrol hooks) for both arms to
  locate the remaining leaf cost; no sampling hooks enter production source.

- Stationary-remap equivalence challenge: compare the original and candidate
  source leaf on deterministic arbitrary binary32 endpoints/query points,
  stationary and moving lines, degenerate lengths, signed zero and infinities.
  Compare raw output words and retain the complete corpus seed/count/result;
  do not use this as a substitute for the full current replay locks.

- Remaining collision cause is the retained wall-pass broad phase:
  mpLib_LineRangeIntersects unconditionally admits every CollJoint_B8/B9/B10
  joint. Source FD binding now sets B8 even when its endpoints do not move,
  disabling the previously retained 8–9% wall-query reduction. Extend that
  existing predicate to stationary nondegenerate transformed lines, measured
  from canonical current/previous vertices. Their source mpRemap2d displacement
  is numerically zero (zero signs cannot affect AABB comparisons). Moving or
  degenerate lines still admit the source narrow phase. No new index, state,
  flag clearing or altered stage binding; this differs from rejected spatial
  indices and epoch suppression. Gate all current outputs before timing.

- Stationary transformed-wall admission preserves every full output lock and
  recovers 4.31%/3.83% versus the completed-stage control in reverse-order screens.
  Add source-geometry tests for distant/overlapping, moving and degenerate
  transformed walls. Remaining cost is smaller but still real. Last bounded
  compiler candidate: strict O3/native ISA for grLast, Ground, grMaterial and
  HSD AObj clock dispatch, replacing their O1 profile only. Canonical state,
  consumers and source operation order are unchanged; no floating-point
  contraction or state/dispatch mechanism is added. Reject if either full
  output identity or repeatable timing fails.

- Remap leaf differential challenge: 4,000,000 finite-input cases, 2,041,343
  stationary-path admissions, all raw outputs equal; digest 35b808398d47909a.
  An earlier arbitrary-bit run encountered a differing NaN payload in the
  unchanged fallback path due to compiler operand selection. Those invalid
  gameplay inputs are not the finite-domain identity claim; no production
  NaN or signed-zero comparison policy was changed.

- Strict stage/AObj O3 compiler screen is exact but rejected: -0.56% at 256
  and -1.01% at 512 versus the retained wall candidate. No compiler admission
  enters Makefile. Retain only the stationary query optimizations; repeat final
  native/release/PPC/Wasm gates and direct checkpoint timing. Final source
  lifecycle cost must be reported rather than described as noise.

# Game & Watch and Mewtwo admission — 2026-09-15

- Authorized scope: implement both fighters on local branch
  `feat/gamewatch-mewtwo`, with tests and replay validation before PR review.
  No commit, push, fork, or PR is authorized for this packet.
- Initial inspection: clean main; both fighter directories contain declarations
  only. Game & Watch common attack entry points abort; Mewtwo forward throw's
  Shadow Ball hook is a no-op. Neither fighter is publicly admitted.
- Final owners: pinned upstream ftGameWatch/ftMewtwo MotionState callbacks and
  their source item/article owners; common capture, reflection, absorption and
  item scheduling retain their existing ownership. Canonical mutable state is
  Fighter's source fighter/state unions and Item's source state, within each
  match arena. Immutable attributes, animation and article data come from DATs.
- Consumers: source fighter/item scheduler, native DAT relocation, runtime pool
  sizing, batch reset/copy/save/restore, public observations and replay/viewer
  projection. Do not add mirrored timers, synthetic move dispatch or replay-fit
  constants.
- Deletion boundary: remove each displaced unsupported-fighter abort/no-op when
  its complete source owner is imported. Update source inventory/provenance
  with imports. Admit each completed fighter through every gate and viewer
  mapping together; no public admission before its dependency closure is usable.
- Sequence: establish local build/data baseline, complete Mewtwo's smaller
  callback/article closure, then Game & Watch; challenge each with targeted
  mechanics and full-game recordings, followed by shared regression gates.
- Environment experiment: fetch the exact decomp pin, provision an ephemeral
  Nix compiler/Python environment, and extract the existing local Melee ISO.
  This checkout initially has no .venv, data/raw, or native build. Host gate
  certification is outstanding; do not refresh committed replay identities.
- Coverage required: Shadow Ball charge/cancel/fire/reflection/absorption,
  forward-throw projectiles, Confusion captures/reflects, Teleport transitions,
  Disable hit conditions; Judge outcomes/history, Chef RNG, Oil Panic storage
  and release, projectile eligibility, and attack-article interruption/death.
  Include four-same-fighter reserve stress and arbitrary-index copy/restore.
- Bring-up result: local ISO extraction and initial source integrity pass.
  Nix supplies GCC/Python/make/dpkg without host-wide installation. The pinned
  PPC compiler download is still running, so no native runtime result exists.
  Existing ~/git/melee (f49c14fdb) has GALE01 assembly/main.elf; imports continue
  to use the sim's exact 91b9789f pin. The user's historical gnw_vs_mewtwo file
  is a DTM recording, not a validator-ready Slippi replay.
- Mewtwo first cut: import five matching fighter sources, both matching article
  sources and their declarations, and the shared Kirby declaration dependency.
  Delete the forward-throw no-op; Kirby-only article dispatch retains explicit
  unsupported-branch aborts. Add source registry entries, costume ownership,
  DAT attribute/article translation and effect-bank loading. Private scalar
  construction is enabled for smoke development; public API admission awaits
  gameplay validation. All seven translation units compile individually.
- Fresh extraction experiment: extended Mewtwo profile contains 205 DAT files
  plus main.dol; raw manifest validation passes. Source-check passes after
  extending the pinned inventory/digest. Gameplay mask derives 46 live of 68
  parts, including the source SpecialN/Lw raw anchors 27, 32 and 35. Runtime
  construction, articles, effects, pool bounds and replay exactness remain open.
- Development-build experiment: while the pinned PPC package download retries
  a Launchpad HTTP 502, build a separate native-dev tree with the existing
  Clang PPC layout-generation fallback and GCC gameplay compilation. Use it
  only for debugging construction/smoke failures; do not record replay locks
  or claim host certification from this build. Run the new two/four-Mewtwo
  charge/release ownership smoke, then repeat with the pinned toolchain.
- Local replay scan found 268 long Mewtwo and 47 long Game & Watch candidates
  on supported stages. Initial scratch Mewtwo selection has ten completed
  games across FD/Battlefield/Fountain/Yoshi's/Dream Land, including bot
  evaluations and a mirror. File lists and provenance remain under ignored
  reports/triage/gamewatch_mewtwo; no replay bytes or suite identities have
  been published or added to the authoritative corpus.
- Native-dev result: the Mewtwo two/four-port charge/release smoke passes;
  first 1,000 transitions of Game_20260715T174042 (Samus/Mewtwo on Yoshi's)
  compare exactly. The validator had its own admission check; its private
  input boundary now accepts internal kind 16. This is development evidence,
  not an authoritative suite identity.
- Next experiment: diagnose all ten complete scratch-selected Mewtwo games
  with four native-dev runners, preserve raw mismatch reports, and identify
  first source writers. Do not alter classifiers/output locks to hide gaps.
- Ten-game development challenge: six raw mismatches and four runner exits.
  Five mismatch sets contain only Shadow Ball/Disable metadata lanes; the
  other is a Fox attack-velocity signed zero. Source item union lifetimes
  justify excluding Disable's pointer/residue lanes and Shadow Ball's
  uninitialized held angle / non-throw x0, using the existing gameplay-misc
  projection boundary. Forward-throw state 9 retains all four numeric lanes.
  No classifications or tolerance changes. Runner exits still need a stack.
- Pinned native build and Mewtwo smoke now pass after toolchain download and
  rebuilding the type object made stale by edits during initial bring-up.
  Full host certification and existing-roster regression gates remain open.
- Existing-roster challenge: native smoke and all committed aggregate output
  locks pass with no regression. Full host certification still needs PPC.
- Expanded Mewtwo smoke experiment: construct five forward-throw projectiles
  per port for four Mewtwos, then advance their lifecycles with sealed pools.
  This catches the read-only attribute regression and overlapping article
  graph demand. First run exhausts HSD_FObj through Item_80268BE0 ->
  HSD_JObjAddAnim. Add an additive 128-track reserve per Mewtwo, then repeat;
  do not weaken sealed allocation checks or use one flat match-wide reserve.
- Expanded Mewtwo smoke result: the subsequent AObj exhaustion also required
  an additive reserve (64 per Mewtwo, alongside 128 FObj tracks per Mewtwo).
  `make mewtwo-smoke` now passes, including four-port charge/release and
  overlapping forward-throw projectiles. These reserves still need review
  against the full supported article/effect concurrency before admission.
- Capture provenance clarification: the user identifies the early July
  recordings, including `mewtwo_shfair_only.slp`, as Mainline Slippi Dolphin
  started through Slippi Launcher. Exact historical executable/settings are
  not established. Local launcher directories contain multiple engine
  channels; their current contents do not prove the July recording build.
  Retain the signed-zero mismatch as an open investigation, without changing
  the replay profile, tolerances, or classifications on this evidence alone.
- Reviewed `HANDOFF_SIGNED_ZERO_EXPHIL_2026-09-15.md` and its local ExPhil
  diagnosis/results. The retained JSON confirms interpreter 1/1, corrected
  JIT 6/6, non-FMA 1/1, and source-profile-matched 13/13 exact prefix audits.
  The unconditional correction passed only 6/13. This independently locates
  that September Fox game's discrepancy at airborne knockback `fnmsubs`
  (0x8006B9E4, fighter +0x90), the same common arithmetic owner used here.
  It does not establish the first differing writer or execution profile of
  our July Mewtwo fixtures: their observed expected +0 / simulated -0 is
  opposite the September source's expected -0 / old-JIT +0. Preserve the
  user's Mainline/Launcher provenance and investigate those July inputs and
  writer directly. No runtime, comparator, or classification change follows
  from this handoff; external prefix evidence is not a simulator gate result.

### Recorded CPU controller validation — 2026-09-15

- Final owners: player configuration retains CPU slot type/level; Slippi frame
  transport owns recorded CPU processed inputs; Fighter input publication at
  0x8006B0DC consumes those inputs before edge/timer updates. Preserve the
  original AI callbacks and CPU-specific UCF/gameplay gates. No CPU-to-human
  substitution, position/action resynchronization, or float quantization.
- Canonical pending replay inputs live in the match-owned Slippi state and
  travel in frame events, separate from ordinary controller bytes. Consumers
  receive exact f32 sticks/trigger and processed buttons. Human and Nana input
  paths stay source-owned. The deletion boundary is the validator's Human-only
  admission restriction; this does not add autonomous CPU support to the API.
- Experiment: first run existing supported-character CPU captures from ExPhil
  with strict comparison, plus the ordinary human regression fixture. Inspect
  the first writer on failures. Game & Watch captures cannot pass before its
  gameplay integration. No replay identities or tolerances change.
- CPU input experiment result: all 12 ExPhil source games complete (67,867
  transitions), with mismatches confined to knockback-Y fields and first
  differences showing expected +0 versus actual -0. No classifications or
  arithmetic profiles were changed. The retained Peach fixture has a strict
  5,000-transition exact prefix; its later signed-zero failure remains visible.
- Focused validation/storage/benchmark tests: 25 passed, one PPC test skipped
  because PPC artifacts are unavailable. CPU smoke verifies exact float bits,
  processed buttons/RNG, physical P2/P3 mapping, follower exclusion, pending
  input copy/save/restore, and reset. Native smoke and Mewtwo smoke pass.
- The first native smoke exposed a stale stage-lifecycle object: its source
  was absent from NATIVE_SMOKE_SRCS, so header dependency files were not loaded.
  Added it and both new smoke sources to that list; the rebuilt test passes.
  Updated the internal viewer schema and bumped controller benchmark tapes to
  v3 for the larger player config; CPU benchmark export explicitly rejects
  the unrepresentable processed-input stream.
- Existing supported-domain challenge remains 503 exact / 26 existing
  classified / zero fail or error (5,059,922 transitions). No output locks were
  updated. Both new Game & Watch/Mewtwo captures now pass CPU metadata parsing
  and stop at the unsupported Game & Watch fighter check. CPU Ice Climbers
  remain explicitly unsupported pending follower validation. See
  CPU_REPLAY_VALIDATION.md for fixture provenance and reproducible checks.

### Targeted Mewtwo move tests — 2026-09-15

- Source owners under challenge: ftMt_SpecialS plus Capture/ThrownMewtwo and
  ftColl reflection ownership; ftMt_SpecialLw plus DamageBind and Disable item;
  ftMt_SpecialHi state/collision callbacks; ftMt_SpecialN and item/lifecycle
  callbacks. Tests use extracted move scripts and real scheduler/contact work.
- Experiment: deterministic local setups exercise Confusion reflection versus
  inactive reflection, grab range and shield interaction, grounded/airborne
  Disable and facing controls, Teleport phase/direction transitions, Shadow
  Ball cancel/interruption/death, and overlapping four-port specials. Keep
  all setup constants confined to tests; no replay-fit gameplay changes.
- Existing replay evidence reviewed: 10/12 diagnostic Mewtwo games exact;
  remaining files have 20 and 50 knockback-Y signed-zero rows respectively,
  on Fox. These are retained failures, not yet classifications or move bugs.
- Initial targeted outcomes: all contact, interruption, charge, teleport and
  copy/restore cases pass without gameplay fixes. Test setup corrected for
  Fox's faster fall into the aerial contact window. Source full-charge entry
  clears take_dmg/death2 callbacks, so interruption tests invoke common damage
  and actual death entry, allowing item cleanup and death3 to run normally.
- Capacity experiment: 20 forward-throw article factories peak at 797/825
  FObjs and 186/410 AObjs. Two/four charge-release peak at 131/569 and 205/825
  FObjs. Four simultaneous Confusion/Disable/Teleport peak at 57/825. Add the
  existing article smoke's 20% spare-capacity requirement, demonstrate its
  failure for the 20-shot stress case, then increase Mewtwo's additive FObj
  reserve from 128 to 192 per fighter. Each five-shot group contributes 185
  live tracks (37 per loaded Shadow Ball graph); it must not borrow most of
  another owner's common reserve. This stress is a conservative article burst,
  not a claim that four simultaneous grabs are possible with four fighters.
- Final targeted tests: 33 move cases plus two/four charge-release and 20-shot
  factory stress pass. Mewtwo smoke now runs under native-smoke. No gameplay
  callback changes were needed. Production change is the additive per-Mewtwo
  animation-track reserve (192 instead of 128); peak now 797/1081, with a
  retained >=20% spare-capacity assertion. Native smoke and source-check pass.
- All 12 Mewtwo diagnostic replays rerun: 10 exact, two signed-zero failures,
  zero errors, 123,473 transitions. Both mismatch fingerprints are unchanged.
  MEWTWO_MOVE_TESTS.md records coverage, remaining challenges, memory usage,
  and the limits of this evidence. The 64-byte native FObj slot means +4 KiB
  track storage per Mewtwo, plus bookkeeping; no throughput benchmark was run.

### Mewtwo replay zero-sign reproducer — 2026-09-15

- Experiment: trace airborne knockback decay operands at the earliest Fox
  Y-velocity mismatch in both July recordings (4041 and 1000). Compare retail
  negate-after-fused-subtract with historical Dolphin reverse subtraction.
  Preserve strict comparison and existing capability ownership; no replay-keyed
  gameplay branches or global zero normalization. Test both full recordings
  with the existing capability before deciding on execution-profile plumbing.
- Trace outcome: both first Y writers use a=0x3d50e560, b=+0, c=+0
  at fighter.c airborne decay (retail 0x8006B9E4). Match frame_id is the
  preceding frame: 999/4040 writes output 1000/4041. The existing native
  retail arithmetic correctly produces -0; legacy Dolphin produces +0.
- Representation: expose an explicit validator-only fnmsubs_profile selector
  (retail or dolphin-legacy), mapping directly to the existing match config
  online_fnmsubs_zero capability. Consumers are native/PPC validation jobs
  and suite entries. Omission preserves metadata defaults. No additional
  runtime state, wire fields, automatic retry, or displaced gameplay logic.
  Record profile in results; do not infer a specific historical build.
- Arithmetic experiment outcome: setting the existing match capability makes
  both full captures exact (14,020 transitions), explaining all 70 rows. Initial
  GDB argument-register mutation did not change the optimized initialization;
  setting the canonical bound rules field confirmed the profile differential.
  Explicit validator/profile plumbing then reproduced both exact results without
  debugger intervention. Retail/default negative controls retain their failures.
- Added twelve two-profile arithmetic cases and lossless full-replay fixtures;
  tests alternate profiles on one runner, reject invalid profiles, and verify
  benchmark config/cache propagation. No runtime layout or arithmetic changed.
- Final focused tests: 30 passed, one unavailable PPC-artifact skip. An earlier
  CPU test launch overlapped native relinking (posix_spawn permission denied);
  rerunning after build completion passed. Native-smoke/source-check pass.
- All twelve Mewtwo diagnostic games now exact: 123,473 transitions, two explicit
  legacy profiles and ten unchanged metadata defaults. Supported-domain gate:
  503 exact / 26 existing classified / zero failures or errors, 5,059,922
  transitions. No classification/output locks changed. Historical build remains
  unknown; the profile assignment is a source-backed arithmetic inference.

### Erickfm Mewtwo corpus expansion — 2026-09-15

- User identified additional recordings under /data. Inspect filename-selected
  Mewtwo candidates from huggingface and erickfm_ranked; deduplicate by original
  replay SHA-256 and check actual start metadata. Select full games across
  supported opponents and stages before validation, retaining failures as well
  as passes. Preserve source bytes and provenance; use metadata arithmetic
  defaults without automatic profile retries. This packet adds validation
  evidence, not gameplay or public admission.
- Scan outcome: 7,911 FOX and 93,438 partner files inspected by fixed
  game-start header, then Mewtwo candidates verified with Peppi. Named old
  fixtures fail required scene/animation fields. One ranked v3.14 Mewtwo/Fox
  recording has all frame fields but stripped metadata. Diagnostic network
  playedOn assumption is supported by scene major 8, not original provenance.
  Default cardinal profile first diverges at -39 in Fox self-speed (-.295
  recorded vs -.3 simulated). Test the existing pre-cardinal UCF profile
  explicitly; retain both outcomes rather than modify inputs or gameplay.
- Final corpus outcome: 74 unique Mewtwo recordings; actual strict probes
  reject 68 for missing scene major and five for missing animation_index.
  One v3.14 ranked game retained losslessly under mewtwo_erickfm, outside
  passing/aggregate suites. Diagnostic pre-cardinal profile extends its exact
  prefix to 477 transitions, but Fox action first differs at 355 (178 vs 235),
  leaving 8,981 mismatching rows of 9,458. This is an unresolved additional
  diagnostic, not a newly validated game or established Mewtwo bug.
- No missing fields synthesized, no classifications/defaults/gameplay changed.
  MEWTWO_ERICKFM_INVENTORY.json retains sources, hashes and exclusions;
  MEWTWO_RECORDING_PLAN.md records evidence and a targeted recording checklist.

### New Mewtwo recordings and Dolphin interaction fixtures — 2026-09-15

- Three new human Mewtwo / CPU Marth Battlefield games retained losslessly;
  all pass strict native comparison (23,445 transitions). Count special-state
  entries and ledge transitions without claiming state counts prove contacts.
- User delegates reflection and Shadow Ball absorption captures. Use the existing
  ExPhil/libmelee_ex bridge and isolated headless Dolphin sessions, scripted
  controller inputs only, both human slots. No playback/state restoration or
  game-memory mutation. Record known executable hash/settings and confirm
  actual item reversal/absorption against nearby inactive controls before
  retaining strict replay results. Production gameplay remains untouched.
- Headless recording outcome: first LRAS attempt paused the game and failed
  to finalize; discard incomplete scratch captures. Use poll:true and controller
  walk-off stock exhaustion. Retained recorder uses isolated homes/ports and
  explicit AccurateNmsub=true; executable hash/build manifest retained.
- Seven scripted Dolphin games now exact: Falco laser reflection/inactive control,
  Ness partial/full Shadow Ball absorption/inactive control, and Samus missile
  reflection/inactive/too-early controls. Missile delay40 was a real timing miss;
  delay65 gives four reversals with no Mewtwo damage. Preserve both recordings.
  Ness partial shot heals 8 -> 0 without stock loss; both partial/full articles
  terminate into SpecialLwHit, asserted by coverage tests.
- Combined targeted suite: ten exact recordings, 38,734 transitions, no errors or
  classifications. Coverage/storage pytest: 12 passed. Human games contain 92
  Teleport starts, 27 Confusion entries, seven direct Teleport-to-ledge catches.
  No gameplay/comparison/default changes; old Erickfm diagnostic still unresolved.
  Recorder, source hashes, and remaining limits documented in MEWTWO_TARGETED_REPLAYS.md.
- Supported-domain regression gate after fixture integration: 503 exact,
  26 existing classified, zero failures/errors across 5,059,922 transitions.
  git diff --check and LFS attribute checks pass; no commit or PR created.

### Game & Watch source/data integration — 2026-09-15

- Final owners: ten pinned ftGameWatch C files and ten itgamewatch article
  owners. Canonical state stays in Fighter.fv/mv.gw and Item source unions;
  consumers are fighter/item scheduler, immutable DAT loader, private scalar
  bootstrap, and validation output. Delete the five displaced attack-entry
  abort stubs and their ledger rows when importing their actual owners.
- Native data representation: ten Article pointers plus FtPartsVisLookup at
  x48_items slot10 (ftGw_Init_OnLoad), fighter attributes, and Chef's five
  inline trajectory entries (ftGw_SpecialN_CreateSausage chooses indices0..4).
  Costume graph is shared by all four source costume colors. No mirrored RNG,
  synthetic Judge outcomes, fallback dispatch, or public admission in this cut.
- Experiment: import matching source inventory, wire registry/data extraction,
  build native, fresh-extract PlGw, and smoke actual fighter/move construction.
  Retain failures and resolve source-owner issues before broader replay gates.
- Initial bootstrap required a DAT/source-derived gameplay bone mask (37 live
  of 53 parts). Both 1,000-transition prefixes then ran; differences were
  article misc pointer samples. Source audit also found Chef x0 overlays the
  shared attribute pointer and x4 owns its trajectory index. Canonical fix:
  type Chef x0 as a pointer, retaining x4 after it on both host widths; delete
  the incorrect integer declaration. No duplicate state. Project only x4 for
  Chef and no pointer/residue samples for the other nine GW articles.
- Outcome: native build/source-check, fresh extraction and native-smoke pass,
  including all four Game & Watch costume skeletons. Both full user GW/MT
  games pass exactly: 8,369 + 18,538 = 26,907 transitions, no classifications.
  GAMEWATCH_SUPPORT.md records remaining targeted/capacity/relocation coverage;
  neither fighter is publicly admitted. No commit or PR created.

### Erickfm Fox shield/dodge diagnostic — 2026-09-15

- First mismatch frame355: recorded Fox remains GuardOn; native enters EscapeN.
  Frame352 already enters shield. Main stick rotates from right to down-right,
  reaching (0.7,-0.7); C-stick stays neutral, L held. Source owner is
  ftCo_8009980C -> msl_ucf_suppress_spotdodge -> ftCo_80099894.
- Existing UCF0.8 branch suppresses held rim spot dodges on solid floors, whereas
  0.84 requires a platform. Experiment: retain pre-cardinal assumption and
  disable only existing 0.84 shield-drop option; inspect full replay and trace
  entry state. This is diagnostic profile evidence, not inferred provenance or
  permission to change defaults or mark the replay passing automatically.
- Outcome: disabling existing 0.84 shield-drop capability resolves all9,458
  transitions exactly. GDB confirms GuardOn IASA, stick(0.7,-0.7), neutral
  C-stick, tilt timers14/2, floor3 flags18 (solid). No gameplay change needed.
  Regression tests:2 passed, including old/new/old profile alternation on one
  runner and original frame355 failure control. Historical profile remains
  inferred; aggregate suite and global defaults unchanged. Durable explanation
  in FOX_UCF_SHIELD_REPRODUCER.md; inventory/recording plan updated.

### Game & Watch targeted coverage and admission — 2026-09-15

- Exercise source move owners with extracted DATs: Judge history/all nine
  outcomes on hit/miss, Chef five trajectories/history, bucket contact
  eligibility and accumulated release. Reuse sealed-arena checks and add
  four-fighter copy/save/restore and animation pool evidence before admission.
- Initial Judge hit test observed a 9 KO and respawn before its endpoint;
  assert maximum damage during the move, not the respawn's cleared percent.
- Future representation fixes must retain source unions and generated
  relocation ownership; no mirrored state or replay-specific dispatch.
- Targeted outcomes: all nine Judge outcomes tested on hit/miss, grounded and
  airborne history; five Chef trajectories; three laser absorptions ->9 stored
  damage -> full release, inactive laser and active missile negative controls.
  Initial Judge9 miss was geometry (18-unit target distance), not a KO; at10
  units it deals32. Earlier tentative KO explanation is superseded.
- Four-fighter continuation passes for live Judge, Chef, Rescue, bucket oil,
  neutral-air, partial bucket state, and20 Chef +4 Rescue articles. Count live
  source items directly: the existing exported item window caps at15. No
  runtime reserve increase needed; all measured pools retain20% headroom.
- Admission packet: add Mewtwo16 and GameWatch24 to public C/Python enums,
  both support gates, viewer selectable roster, Makefile filter, AGENTS/README,
  and aggregate suite includes. Existing viewer external mapping already maps
  both correctly. No displaced runtime state; public rejection is replaced by
  the completed private source path. Run all native/replay/browser gates before
  claiming completion. No commit/PR or output-lock edits.
- Host certification completed before writing new locks: native Linux x86_64,
  GCC15.2.0 (previously accepted compiler), native/validator/python-library build,
  source-check, original529-case aggregate503 exact/26 classified with zero
  lock failures, and pinned GCC13.3 PPC build + Bowser PPC suite16 exact/1
  classified/0 failures. PPC linker needed Ubuntu libjansson4_2.14-2build2
  extracted into the ignored toolchain root. ENVIRONMENT.md steps1-4 green;
  full pytest underway. Generate only new component-suite native locks.
- Final native and optimized aggregate gates pass:557 cases =531 exact/26
  existing classified/0 fail/0 error;5,310,479 transitions. Added28 new locks
  only after host certification; old529 locks unchanged. Both fighters now
  admitted consistently, including Python/viewer asset inventories.
- Native/WASM smoke and live Chrome viewer pass (23 characters,6 stages).
  Public Python GW/Mewtwo four-player cross-batch restore passes all6 stages.
  Coverage tests require actual absorption responses and projectile termination;
  Judge/Chef recorder needed a neutral gap to issue a new B press after Judge.
  Retained v2 has all9 Judge outcomes and Chef projectiles; v1 is scratch only.
- Full Python run initially83 pass/3 stale inventory assertions; update DAT
  counts209 ISO/210manifest and557suite inventory. Final rerun underway.
  Optimized Nix build strips -march=native/-mtune=native via its wrapper; this
  is an optimized portable build, not a CPU-tuned performance result.
- Final outcome:86 Python tests pass,2 CI-excluded timing tests deselected;
  three aerial accessory interruption checks added and gamewatch-smoke rerun
  passes. GameWatch PPC16/16 exact over197,803 transitions. Production viewer
  built and served locally at127.0.0.1:8001 for the user's manual testing.
  GameCube bridge starts with the server. No commit/push/PR created.
