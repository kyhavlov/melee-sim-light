# Jigglypuff port plan (lane: puff)

Lane plan under `PARALLEL_PORTS.md`; methodology per `ADDING_A_CHARACTER.md`.
Drafted 2026-07-07 before Phase 0 — items marked VERIFY are to be pinned by
the lane agent from the decomp/ISO, not assumed.

## Shape of the work

`refs/melee/src/melee/ft/chara/ftPurin/` has its own specials modules and NO
articles — the whole port stays inside the fighter pipeline (no item work).
The risk concentrates in movement/aerial edge cases instead: Puff is the
lightest, floatiest char with FIVE midair jumps, so KB float parity, jump
bookkeeping, and air-drift caps get exercised harder than any existing char.

Specials inventory (new module `src/puff_specials.c`):
- Rollout (SpecialN): the big one — multi-state charge machine
  (charge/roll/turn/end, grounded+aerial), stored charge across states,
  wall/ledge interactions while rolling. VERIFY the state family + hidden
  charge lanes in ftPr_SpecialN (charge level likely an mv union float +
  cmd vars; reseed reconstruction needed — same class as the smash-charge
  seed lanes that already exist: see `derive_smash_charge_seed_lanes`).
- Pound (SpecialS): horizontal hit with drift; simplest of the four.
- Sing (SpecialHi): applies SLEEP to grounded victims — VERIFY whether the
  FuraSleep/DamageSong victim states (actions ~0x161 range? VERIFY) are
  already expressible; likely a new victim-state family analogous to
  CaptureCaptain work (state entry, mash-out timer, hurtcaps). NO freefall.
- Rest (SpecialLw): 1-frame invincible sleep-self; the attacker-side state
  machine is trivial but the self-sleep duration + invincibility windows
  and the flower element on victims: VERIFY element handling (flower =
  element id? — combat element tables may need a row).

Note Puff has no up-B recovery freefall and no wall-jump; LandingFallSpecial
origin-table work is likely NOT needed (VERIFY: does any Puff special enter
FallSpecial?).

## Phase 0 — facts (VERIFY all)

- Registry: internal/external ids, `PlPr.dat`/`PlPrAJ.dat` (VERIFY names),
  `ftDataPurin`, prefixes `ftPr_`/`ftPr_SM_`, `has_articles=False`.
- Jumps: `jumps_max = 6` (5 midair) — VERIFY how `jumps_left` serializes in
  Slippi for >2 jumps and that nothing in locomotion/state assumes 2.
- Action-id blocks for the specials + victim Sleep states; Rollout family
  size.
- Attr layout from `ftPr_*` dat_attrs reads (rollout charge frames, roll
  speed curve, sing radius, rest invincibility frames...).

## Phases 1-5 (falcon recipe)

1. Registry row in the step-0 batch commit; extraction; byte-identity gate.
2. Replay intake: peppi scan for Rollout family + Sing + Rest + victim
   sleep states; Rest usage is common in real games, Sing is rare — accept
   thinner Sing coverage and lean on unit tests there. NEED FROM USER:
   dump with Puff games.
3. Common actions: the 5-jump chain, fastfall/drift caps, and the coverage
   suite. Expect jump bookkeeping test fixups.
4. Specials order: Pound → Rest → Rollout → Sing (Rollout before Sing so
   the charge-machine reseed substrate matures early; Sing last because the
   victim-state family may queue shared fixes — victim state entries touch
   shared action/damage tables).
5. Burn-down via the taxonomy script; expect KB-float and multi-jump rows
   to dominate rather than specials rows.

## Gates

`ADDING_A_CHARACTER.md` Phase 7 per retained change; existing chars
byte-stable; any victim-sleep-state or element-table work goes through the
shared-fix queue.

## Progress log

- 2026-07-07: plan drafted; awaiting step-0 registration + replay dump.
- 2026-07-07: **Phases 0-2 done (solo lane; parallel pilot deferred by user).**
  Phase 0 facts: internal=external id 15; PlPr.dat/ftDataPurin/ftPr_;
  anim count 327 (ftData_Table_Unk0[15]); action map 341-345 JumpAerialF1-5,
  346-353 grounded Rollout, 354-361 air Rollout, 362 NHit, 363/364 Pound,
  365-368 Sing (L/R x ground/air), 369-372 Rest; NO ThrowN part
  (part_to_joint[51]=0xFF), TransN2 -> parts idx 48; NULL ftData.x24 (no
  Wait roulette). ftPurinAttributes is 0x100 bytes, mostly unnamed —
  PURIN_SPECIAL_ATTRS_LAYOUT still to build during Phase 4 consumer reads.
  Phase 1: registered end-to-end; build_data byte-identity gate held
  (owners renumber proven pure at symbol level: +80 symbols = 76 ftPr_* +
  common JumpAerialF1_* set). Three pipeline fixes: anim extractor raw
  Fighter_Part enum ids -> part_to_joint mapping (appended post-legacy to
  keep existing bake order), NULL-x24 wait-roulette guard (empty arrays),
  char_params count-0 acceptance. Coverage suite 42/42 through the
  generic path (one test fix: dash entry velocity is UNclamped
  dash_initial_velocity per ftCo_Dash_Enter; puff 1.4 > run terminal 1.1).
  Phase 2: 11-replay suite from the user's top14 dump (fox x6 incl
  frozen-PS, falco, marth, sheik, falcon, ditto; all six stages).
  Coverage gaps for unit tests: air Rollout (only instance vs
  unregistered Doc), SpecialNFull/EndL. BASELINE (generic path only):
  one-step discrete 8221 / 11717761 (7.0e-4), float p95 84.44.
  Next: Phase 3 multi-jump (341-345 dominate every replay), then
  specials Pound -> Rest -> Rollout -> Sing per the plan order.
- 2026-07-07 (later): **Multi-jump + Pound + Rest + Sing(attacker) live;
  one-step 8221 -> 3948.** Commits: multi-jump ladder (8221->4884; fp->x2D0
  block, chain window cmd0@28 on F1-F4, held-input chaining, ft_800CB6EC
  turnaround with the entry-tick convention, ft_80084E1C scaled drift +
  x258 threshold, MissFoot-family Coll classing); Pound (->4467; falcon
  aerial-punch shape: cmd0 impulse@12 stick-angled, cmd1 ladder 0/1/2,
  ground FA8 root-motion phys, phase flips — the swap wiring initially had
  an operator-precedence bug, unit test caught it); Rest (->3990; L/R
  facing variants, script-driven frame-0 invincibility verified by test);
  Sing attacker-side (->3948; same shape, element-6 sleep hitbox rides
  generic machinery).
  REMAINING for falcon-level parity:
  1. DamageSong victim family (actions 297/298/299 = DamageSong/Wait/Rv,
     SM 206/207/208 FuraSleepStart/Loop/End): combat element-6 handler on
     GROUNDED victims -> ftCo_800C318C entry; sleep duration formula
     inlineA0 uses p_ftCommonData x624/x628/x62C/x630/x634/x638 (+x63C
     decrement, x640 mash step, x644 arg1 mul) — needs common-param
     extraction; mash decrement reuses the grab-mash lanes. Producer-
     scoped placement in puff_specials.c is fine (Sing is the ONLY
     DamageSong producer in melee).
  2. Rollout (346-362; ftPr_SpecialN.c): the charge machine — largest
     remaining chunk. Grounded suite coverage exists only in the
     FoD-vs-Fox game (346/348/350/351); air rollout has NO eligible
     replay coverage (unit tests must carry it).
  3. Phase 5 burn-down via the taxonomy recipe (falcon round-1 script
     shape: collect_mismatch_events buckets by field x seed/ref/out
     action), then classify the tail. Float p95 is still ~82 — dominated
     by rollout-family rows most likely; re-check after Rollout lands.
  4. Then TWO MORE chars from ~/replays/top14 (user goal): easiest by
     census + article-count are DK (10 games, article-free, cargo-carry
     reuses the falcon capture substrate) and Doc (10 games, ftDrMario
     reuses ftMario code; megavitamin article like laser substrate) —
     Luigi/Pikachu/Yoshi/Peach/Samus/ICs are heavier. Recommend DK then
     Doc; both dumps already extracted at ~/replays/top14.

- 2026-07-08: **Rollout live (346-362); one-step 3948 -> 3826.** Module:
  neutral-B dispatch zone, the full frozen-anim state machine (KEY replay
  finding: every internal ChangeMotionState passes anim rate 0 — the 5th
  arg IS the rate — so Loop/Full/Release/Turn/NHit hold state_age 0 and
  ALL exits are stateful; Start/End animate at rate 1), phys ownership
  (charge states own an UNPROJECTED self_vel = facing*1e-4 — new
  physics.c gate `puff_rollout_ground_charge_state` bypasses the generic
  gr_vel->self_vel floor projection; Release slope-influenced
  charge-scaled speed with double clamp; Turn material-scaled x1C accel
  with the zero-crossing xD0 exit; air release x58 decel to the x5C
  floor; NHit drift-at-terminal + rollout Fall), locomotion hooks
  (floor-loss swap family WITHOUT the falcon-style gr_vel->self transfer
  per 7D5D4; air-release landing/bounce fork; wall bounce off env-flag
  wall masks — dir==+1 checks the LEFT-facing 0x3F wall on the right),
  combat deal_dmg hook (8013D764 -> NHit with the -0.13/+1.6 backward
  hop; ref-confirmed jumps_left drops to max-1 and ground_id persists),
  pre-combat velocity-scaled hitbox pass (8013D8E4: disable below xCC=2,
  damage (s32)(3*(2+speed)) — a minimum-charge roll at 0.3 speed whiffs
  entirely, matching source; also gates Release hb1/hb2 off per the
  script's set_hitbox_interaction x42_b5=0, an event kind the MSLHITB1
  artifact does not carry).
  SHARED-MACHINERY fix (char-gated): hitboxes.c frozen-rate-zero seeded
  prev-capsule bridge — the frame-0 create on a frozen anim looked like a
  fresh create edge on every teacher-forced row, clearing HitCapsule
  victims_1 and re-hitting a shielding victim every seeded frame (found
  via the fox GuardSetOff segment: melee shield hits take the x19A4
  branch and never fire deal_dmg_cb, so puff keeps rolling).
  Reseed: dir from letter/velocity-sign/facing; charge inverted from the
  velocity formula (slope factor from the seeded normal; Turn-exit rows
  invert the xD0 threshold instead); budget reseeds at init (the natural
  end row is unpredictable from a seed — the anim carries no progress);
  Turn pre_turn = v before the zero crossing (proportional decel keeps
  the exit un-fired) and 8*|v| after it (any single-row estimate either
  always or never exits; never-exit costs 1 row per turn vs 4+); frozen
  states force frame_speed 0 (builder defaults entry rows to 1).
  Unit tests: +7 (entry/frozen-loop, Full+release speed, budget end ->
  Wait, Turn decel/reversal exact-step, air entry/charge-fall/landing
  transfer, air budget end -> FallSpecial -> LandingFallSpecial, on-hit
  NHit backward hop) = 22 puff tests total. Gates: validate-all
  byte-identical for existing chars; full pytest green (3 known
  environmental failures only).
  Documented residual debt (rollout family, ~31 rows in the one FoD
  game): release-entry rows seeded from Loop can't see the hidden charge
  (loop holds age 0); the one true Turn-exit row per turn; slope-boundary
  rows carry a one-frame floor ambiguity (seeded velocity was produced
  with the PREVIOUS frame's floor normal); NHit hit_group toggle (x9C)
  re-arm rehits and rollout-vs-shield x18F4/x21F8 spin-reversal
  (8014222C) unmodeled (victim-side weak-hit turnaround machinery).

- 2026-07-08 (later): **DamageSong victim family live (297-299).** Sing's
  sleep now has a consumer: combat routes element-6/7 hits into DamageSong
  at the damage-state selection (ftCo_8008E908 shape; the x2228_b2 gate is
  sandbag-only, and the grounded-only behavior is the Sing hitbox's own
  hit_grounded-only payload). Duration = the inlineA0 formula (x624-x644
  extracted to common params; slot = victim index + 1 per the capture
  convention; element 7 scales by x644=4). The machine mirrors Furafura:
  shared capture_grab_timer lane, -x63C/frame, grab-mash -x640, lazy
  timer reconstruction from action_frame for reseed (the Wait loop adds
  the per-char FuraSleepStart anim length), FuraSleepEnd -> Wait.
  Phys = ft_80084F3C via the common-ground-friction-only list.
  COMBAT-SHARED change (element-scoped): 0-damage capsules with sleep
  elements now pass the zero-damage admission gates (the +18 kb constant
  keeps kb_applied nonzero so the full path runs); no existing char
  authors 0-damage element-6/7 hitboxes, and validate-all stayed
  byte-identical. NO replay in the suite lands a Sing — 4 unit tests own
  the family (formula duration incl. percent scaling, mash shortening,
  airborne immunity via the hitbox flag). Suite total unchanged (3826).

- 2026-07-08 (later): **Phase 5 burn-down: damage jump-out fix (one-step
  3826 -> 3610) + tail classified; puff is at falcon-level.**
  Taxonomy (field x seed/ref/out action over all 11 replays, 3067 scored
  discrete): the one live puff-owned bucket was seeds in DamageFly states
  entering rung F2 with jumps 5->3 where ref enters F1 (46+ rows): the
  knockdown damage jump-out (`damage_air_try_jump_aerial`) entered the
  COMMON JumpAerialF and the ladder fork then chained a second rung the
  same frame (common JumpAerial is outside the fork's cmd0 ladder gate).
  Source routes ftCo_Damage's inlineC0 through ftCo_800CB870, which sends
  multi-jump chars into ftCo_800D74A4 — the entry tail is now exported
  (`msl_locomotion_puff_multijump_enter`) and knockdown routes
  has_multijump chars into it. validate-all byte-identical (path is
  has_multijump-gated).
  Remaining tail classification (shared debt, same classes as falcon):
  early/late landing detection swaps (FALL/AttackAir <-> Landing rows,
  the largest class), cliff/ledge rows, GuardSetOff shield rows, falcon
  SpecialHi family rows (vs-falcon game), fox laser item rows. Puff-owned
  documented residue: mjump turnaround-window facing (~30 rows; reseed
  cannot see the armed window), rollout release-entry hidden charge
  (~2 rows/use), the one true Turn-exit row per turn, slope-boundary
  one-frame floor ambiguity (float-only).
  Float p95 (82.8) is a METRIC ARTIFACT, not puff float error: the norm
  divides each lane's error sum by the lane's |ref| p95 floored at 1e-6;
  in 4 games the item lanes are ~always empty (ref_p95 = 0) and a handful
  of fox-laser mismatch rows (types 54/74/56, owner fox — shared blaster
  substrate) divide by the floor into 1e6+ contributions. Per-fighter
  float lanes match falcon-suite levels (per-game p95 0.0002-0.0004 in
  the 7 unaffected games).

### Rollout survey (2026-07-07 decomp skim; implement next)

ftPr_SpecialN.c is ~1470 lines. mv.pr.specialn lanes (types.h): x0 charge
counter (init da->x34, decremented by da->x38 in the 8013D764 turnaround),
x4/x8 = -1-initialized frame cursors (x8 drives scaleAnimStep), xC flag,
x14 roll angle (radians, normalized 0..2pi, drives FtPart_YRotN rot — the
visual roll; scalar gameplay lanes are what we model), x24/x28 counters,
x2C = da->xA0 (init), x30, x34 Vec (x34.x latches facing at entry),
facing_dir latch (restored by ftPr_SpecialS_8013D658 on exit). Charge init
ftPr_SpecialS_8013DC64. Entries via ftData dispatch: grounded StartR/L 346/
347 by facing, air 354/355; charge Loop 348/356 (+Full 349/357 at max
charge); release -> Release/ChargeRelease 350/358 (the ROLL - hidden roll
speed scales with stored charge); Turn 351/359 on stick reversal mid-roll;
End R/L 352/353 (+360/361 air); NHit 362 on connect. The da attrs x34/x38/
xA0 + roll speed/decay constants live in the unexplored middle of
ftPurinAttributes (0x00-0x33 = mjump, 0xDC-0xF4 = pound; rollout block is
likely x34-0xA4) — name them from the consumers during implementation.
Reseed: charge/roll-speed lanes need reconstruction (same class as
smash-charge seed lanes; roll speed may be recoverable from seeded
self_vel like falcon's dive vel lanes).

Concrete entry/charge mechanics (read 2026-07-07):
- Enter (346/347 grounded, 354/355 air by facing; x34.x latches facing):
  cmd_vars 0-3 cleared, ftPr_SpecialS_8013DC64 inits lanes (x0=da->x34
  turnaround budget, x2C=da->xA0 charge, x1C=da->x44 grounded / da->x54
  air), grounded zeroes vy; air sets x74_anim_vel.y=da->x3C.
- Start anim end -> Loop (348/356): cur_anim_frame=0 with anim rate 0
  (the loop HOLDS frame 0 while charging), gr_vel=self_vel.x=facing*1e-4,
  model rot cosmetic.
- Loop per frame: charge x2C += da->xA8, capped at da->xA4; deal_dmg_cb =
  ftPr_SpecialS_8013D764 (on-hit turnaround consuming x0 -= da->x38);
  hitCapsuleToggle flips hb0 x4 alternating every da->x9C frames during
  the roll (dual-sided roll hit).
- Release: B released during Loop -> Release/ChargeRelease (350/358);
  roll speed presumably scales with x2C (read the Release Phys next);
  Full (349/357) at x2C == xA4.
- Attr offsets so far: x34 (turn budget), x38 (turn cost), x3C (air entry
  vy), x44/x54 (x1C init g/air), x9C (hit toggle period), xA0 (charge
  init), xA4 (charge max), xA8 (charge rate).

More Rollout mechanics (read continued):
- Loop -> Full (349/357) when charge x2C hits xA4 (keeps cur_anim_frame);
  x30=1 marks full (colanim pulse 5 once via ftCo_800BFFD0).
- Loop/Full roll-angle accum per frame: x14 += facing * x2C * deg2rad(xAC)
  (cosmetic; scalar model can skip x14 EXCEPT Release's turnaround gate
  reads it — see below).
- Release_Anim (350/358): angle delta = deg2rad(x2C * 0.2 * da->x98 *
  facing); x0 (turn budget, init da->x34) decrements per frame; when
  x0 <= 0 AND the roll angle crosses pi (model upside-down) ->
  ftPr_SpecialS_8013DA24(end handoff 0x40012). So the END trigger needs
  the x14 angle lane modeled (charge-scaled angular speed).
- Turn_Anim (351/359): angle -= 0.2 * da->x6C * facing; x0 decrements; at
  x0 <= 0 flip x34.x (roll direction) and hand to 8013DA24.
- End_Anim (352/353/360/361): scale cosmetic; anim end -> facing restore
  (8013D658 from mv facing latch) + ft_8008A2BC Wait.
Rollout survey COMPLETE (2026-07-08; full ftPr_SpecialN.c read + helper
bodies + ftPurinAttributes struct from ftPurin/types.h):
- Release trigger: Loop/Full IASA fires when B NOT held
  (!(held_inputs & 0x200)) -> Release(350)/ChargeRelease(358) at preserved
  frame with initial vel = x34.x * (da->xC0 * (x2C - da->xA0)).
- Release grounded Phys per frame: base = x34.x * xC0 * (x2C - xB8)
  +/- slope influence xC8 * base * |floor.normal.x|; clamped to x4C then
  x50; ApplyGroundMovement. Charge decays x2C -= xB4/frame while rolling;
  roll ends (8013DA24 ground-end) when x2C < xB8.
- Release_Anim end gate: x0-- per frame; when x0 <= 0 AND roll angle x14
  crosses pi (delta = deg2rad(0.2 * x98 * x34.x * x2C * xBC) per frame) ->
  8013DA24(air variant for 358). Turn budget x0 init da->x34; on-hit
  costs x38.
- Release IASA -> Turn(351): |stick.x| > x68 opposite x34.x; latches
  x10 = gr_vel, x1C = -0.05 * gr_vel. Turn Phys: gr_vel += xC4 *
  mpLib_800569EC(floor.flags) * (x1C +/- influence); exits back to Release
  when velocity crosses zero with |gr_vel| >= |x10 * xD0|. Turn_Anim
  exhaustion (x0 <= 0): flip x34.x, 8013DA24 end.
- Air ChargeRelease Phys: self_vel.x = base (same formula), decel x58
  toward min x5C, ftCommon_Fall(x3C gravity, x40 terminal).
- On-hit callback 8013D764 (deal_dmg_cb, active in Release/Turn/
  AirChargeRelease/AirStartTurn): xC=0; x0 -= x38; facing = x34.x; remove
  hitboxes -> NHit(362) at preserved frame. Ground: self_vel.x = gr_vel *
  specialn_vel.x (attr x88) + go airborne; air: self_vel.x *= x88. Always
  self_vel.y = specialn_vel.y (x8C); anim_vel/xE4/gr_vel zeroed;
  deal_dmg_cb=NULL. NHit Phys = Fall + drift when vy <= -x40; NHit_Coll
  landing -> facing restore + (xD8==0 ? normal landing : LandingFallSpecial
  lag xD8). NHit_Anim end (read earlier): air -> Fall.
- Damage updater 8013D8E4 (per-frame in Release/Turn/AirChargeRelease):
  speed = |gr_vel| ground / |self_vel.x| air; below xCC -> hitbox disabled
  (+colanim reset); else enabled with damage = (s32)(x84 * (x80 + speed)),
  min 1. hitCapsuleToggle: xC counter flips hb0 x4 side-bit every x9C
  frames.
- Wall bounce (Release_Coll / AirChargeRelease_Coll): rolling right checks
  env_flags & 0x3F, left & 0xFC0; on hit: x2C *= xD4 (clamp >= 0),
  x18 *= xD4, gr_vel/self_vel.x = -vel * xD4, x34.x flips (ground: =
  SIGNF(gr_vel); air: = -x34.x).
- Air release landing (AirChargeRelease_Coll): vy' = |self_vel.y * x78|;
  if vy' < x7C -> ground Release at preserved frame, gr_vel = self_vel.x =
  x18 * x34.x, x1C = da->x44; else BOUNCE (self_vel.y = vy') with optional
  stick re-aim (|stick.x| > x68 -> x34.x = sign, vel = x18 * x34.x).
  x18 = |roll speed| lane (kept in sync by the Phys callbacks).
- AirStartTurn_Coll landing -> ground Turn at preserved frame, gr_vel =
  self_vel.x. Ground colls (Start/Loop/Full/Release/Turn/End) floor-loss ->
  air variant at preserved frame (7D5D4); air Start/ChargeLoop/Full/End
  landing -> ground variant (7D7FC).
- End handoff 8013DA24(is_air): facing = x34.x; remove hitboxes; ground ->
  EndR/L(352/353) with gr_vel *= x90, vy = 0; air -> AirEndR/L(360/361)
  with self_vel.x *= x90, self_vel.y *= x94, gr_vel = 0. AirEnd_Anim end:
  facing restore + (xD8 == 0 ? Fall : ftCo_80096900 special-fall with lag
  xD8).
- Facing restore 8013D658 (death2/take_dmg cb + End/exit): if mv facing
  latch != 0, fp->facing_dir = latch; latch = 0 (model scale/rot cosmetic).
- Shield-hit callback x21F8 = 8014222C: negates self_vel.x, gr_vel, xE4,
  x10, x14, x18, x1C, facing latch, x34.x, x34.y (full direction
  reversal on shield contact).
- ftPurinAttributes types: x34/x38/x70/x9C are s32, specialn_vel Vec2 at
  0x88, rest floats. Rollout attr map: x34 turn budget, x38 on-hit cost,
  x3C air gravity, x40 terminal, x44/x54 x1C init g/air, x4C/x50 vel
  clamps, x58 air decel, x5C air min, x68 stick threshold, x6C turn roll
  rate, x74 Turn_Coll vel threshold (fast colls use ft_80082888 above it),
  x78 landing vy scale, x7C bounce threshold, x80/x84 damage base/scale,
  x88/x8C NHit vel scale/set, x90/x94 end vel scales, x98 release roll
  rate, x9C hit toggle period, xA0 charge init, xA4 charge max, xA8 charge
  rate, xAC loop roll rate (deg), xB4 charge decay, xB8 min rolling
  charge, xBC roll-rate scale, xC0 release vel scale, xC4 turn accel, xC8
  slope influence, xCC min-damage speed, xD0 turn exit ratio, xD4 wall
  bounce decay, xD8 landing lag.
- 2026-07-09: **specials.slpz intake + burn-down: puff suite one-step 3958 -> 2365
  (specials.slpz alone 361 -> 56; strict 377 -> 72).** User-recorded Puff-ditto
  Battlefield session registered in replays/suites/puff.json; it filled the air
  Rollout (354-358), SpecialNFull (349), air Sing/Rest, and DamageSong victim
  gaps. Six owners fixed off the collect_mismatch_events taxonomy:
  1. Cliff-catch Coll family (~130 events): air Sing (366/368) Coll ends in
     ftCliffCommon_80081298 with CLIFFCATCH_BOTH (fd=0 both ledge sides), the
     multijump ladder shares ftCo_JumpAerialF1_Coll (ft_80082F28, facing-dir
     boxes), and Rollout AirChargeRelease (358) passes the ROLL direction
     (mv x34.x) into ft_8008239C — eligibility in ledge.c, direction modes in
     mpcoll_env.c, catch-tail latch consume via puff_rollout_on_cliff_catch.
  2. NTurn edge cling (30 events): ftPr_SpecialNTurn_Coll picks its wrapper by
     |gr_vel| vs new attr x74 (puff_rollout_turn_coll_vel_threshold, extracted
     end-to-end); at/below it mpColl_8004A45C_Floor endpoint-snaps and STAYS
     GROUNDED — wired as a dynamic MSL_MPCOLL_PHASE_EDGE_SNAP OR at the
     mpcoll_ground phase derivation (the static MSLMSO01 table cannot carry a
     velocity-conditional owner). Ref never enters AirNTurn (359).
  3. DamageSong entry side effects (~40 events): Fighter_ProcessHit gates BOTH
     sides' hitlag on nonzero applied damage (Sing is 0-damage), and
     ftCo_8008E908's element-6/7 arm enters via ftCo_800C318C which bypasses
     ftCo_8008DCE0 entirely — no KB install, no self-vel clear, no hitstun, no
     victim facing flip, no post-entry anim tick. New sleep_element_entry
     routing in combat.c (combat_damage_enter_damagesong).
  4. GLOBAL ECB desired-bottom clamp (~50 events incl. the dair-early-landing
     family): mpColl_LoadECB_JObj clamps `bottom_y < 0 -> 0` and fighters have
     no Fixed-source writers; Puff's dair/uair posed joints dip ~2 below TransN
     and were landing 2 units early. Clamped in msl_ecb_world_points_sample /
     msl_ecb_bottom_world_point_sample / mpcoll_pose_ecb_bottom_rel_y.
     fox/falco FD one-step verified byte-identical to HEAD.
  5. ftCommon_8007D5D4 floor-loss bundle on ALL char-special ground->air swaps
     (marth/falcon/sheik-zelda/puff rollout): consume the ground jump
     (jumps_left = max-1) + 10-frame ECB bottom lock.
  6. RebirthWait jump exit (ftCo_800CB870 closes the IASA priority group):
     modeled subset is MULTIJUMP chars on an X/Y press edge only (puff's halo
     Y-press enters the F1 ladder; new exported
     msl_locomotion_try_enter_jump_aerial_from_iasa wired into match_flow).
     The common JumpAerial arm and the tap-up arm are deliberately unmodeled:
     both regressed the locked fox manual trace (an X edge on the halo did
     NOT jump in the real game — unexplained by the plain ft_did_jump chain).
  REMAINING specials.slpz tail (72 strict / 56 normal): 8 JumpF facing rows
  (turnaround window reseed debt — reconstruction from the current stick was
  tried and measured WORSE: entry-time arming is not derivable per-row), 3
  NLoop->NFull (Loop charge reseed debt; needs a smash-charge-style seed lane),
  ~13 state_flags[4]&0x80 rows (ignored bit), 3 RebirthWait->Fall timeout af
  convention, 2 AirNRelease landing fork, 2 NTurn true-exit rows (documented
  pre_turn one-miss), singles. Env note: this machine's suite baselines carry
  a two-float-digit item_vel_x delta vs committed reports (verified HEAD
  reproduces it; compiler FP environment). Cross-suite: aggregate one-step
  6526 -> 6525; sheik/marth/doubles one-step discrete unchanged (small pos
  float MAE improvements); sheik free-running rollout first-mismatch 103 ->
  105 (+2 streak breaks, one replay's best streak lengthened — downstream
  divergence wash of the source-verified ECB clamp, kept per the
  source-clear retention policy).
- 2026-07-10: **Float-p95 triage + two owners: puff one-step 2365 -> 2324;
  clean-row float error roughly halved.** Round-2 tooling: per-field
  normalized-contribution split + clean-vs-discrete row split + per-action
  float buckets (scratch under reports/triage/). VERDICT on the long-standing
  "float p95 ~88" mystery: it is a METRIC ARTIFACT — the per-field normalizer
  is ref-p95 clamped to 1e-6, and in Puff games speed_ground_x_self /
  speed_*_attack are ~all-zero at ref-p95, so a few units of absolute error
  (mostly ON discrete-mismatch rows) divide by 1e-6. Track absolute per-field
  sums for puff instead. Two real clean-row owners fixed:
  1. Thrown-victim x1A70 (~780 err, the largest float owner; ThrownLw victim
     placed ~10 units high under falcon/sheik dthrow):
     Fighter_UnkUpdateVecFromBones_8006876C's x1A70 is the WORLD delta
     TransN(part 1) - XRotN(part 2) at the create pose; the older shortcut
     read the mapped grab-anchor part's local SRT lane, which coincides on
     spacie skeletons (-8.3 == the Dolphin probe) but is a different basis on
     Puff (anchor part 48 hangs under part 3). thrown_static_x1a70_offsets now
     computes the same TransN-XRotN quantity as the thrown-entry static path.
     fox/falco -8.3 identical by construction.
  2. fall_fast seed-lane latch (~150 rows: fastfall pinning vy to -1.6 on
     RISING aerials): the input-history suffix derivation's jump-entry set
     lacked the multijump rungs (341-345), so a pre-ladder fastfall stayed
     latched through the whole ladder. ftCo_800D74A4 rung entries are fresh
     jumps (clear fall_fast) and ft_80084E1C runs CheckFallFast; the native
     derivation now takes an (act_multijump_first, count) range from a new
     per-char ladder LUT (keys on puff_mjump_turn_frames like has_multijump).
  Also: reports/validation/puff_rollout.txt added to the standing reports
  (free-running first-mismatch 501 over 12 replays; falcon-suite ballpark).
  REMAINING float buckets, all classified: Whispy wind-phase 0.2 rows in the
  DL game (~250 rows; shared dream_whispy stage lane, NOT puff-owned),
  DeadUpFallHitCamera drift (120 rows, cosmetic dead-state), CatchDash
  constant 1.016 (78 rows, dash-grab momentum), fox-laser +3% percent rows on
  KneeBend/Escape (missed weak-hit family), marth SpecialAirHi (marth lane).
- 2026-07-10 (later): **Suite-wide discrete taxonomy round (Phase 5 gate):
  3003 strict / 2324 normal events classified.** ~600 strict-only rows are
  the ignored state_flags[4]&0x80 bit. Top normal owners, each probed to a
  witness:
  1. GuardSetOff floor-loss keep-motion (143 rows, ditto YS rec 760-761):
     shield pushback slides past a platform edge; ftCo_GuardSetOff_Coll's
     !allow_sdi branch (ft_800845B4) LEAVES the motion on floor loss — ref
     stays in 183 airborne with y pinned (Phys ft_80084F3C has no gravity)
     until anim end -> Fall; our sim stays grounded. Positions already exact;
     only the grounding bit + downstream Fall-entry timing differ. SHARED
     common owner (also in the falcon plan tail as "GuardSetOff shield
     rows"); the allow_sdi-conditional wrapper choice is another
     branch-shaped owner the static class table cannot carry.
  2. Fox-laser item rows (~280 events, vs-fox BF game rec 1713): item lane
     family in one game; not yet triaged beyond the bucket.
  3. Match-start Fall->Wait pin (150 events, ditto rec 105+): pre-frame-0
     rows where the replay has players already falling around, but our
     match-flow pins p1 to spawn Wait at x=+28. Match-entry model gap.
  4. Fall->Landing platform re-land from below (60 events, vs-fox BF rec
     3696): fastfalling puff BELOW the side platform (root 26.1 < 27.2)
     "lands" UP onto it — the posed Fall ECB bottom sits above the root, so
     the bottom sweep crosses the platform from above while the ref (which
     dropped through it earlier) passes through. Likely the drop-through
     floor_skip lane not persisting into the post-Pass Fall rows.
  5. Rung facing rows (45) — the documented turnaround reseed debt.
  Everything else is a long tail of <=12-event buckets. Next round should
  take owners 1 (shared, biggest) and 4 (platform correctness) first.
- 2026-07-10 (later still): **Owner 1 landed — it was NOT GuardSetOff: action 183
  is DownBoundU (missed-tech bounce), and the mechanism is the movescript.
  Puff one-step 2324 -> 2162 (strict 3003 -> 2841); puff free-running rollout
  first-mismatch 501 -> 375.** The previous entry's owner-1 attribution
  (ftCo_GuardSetOff_Coll / ft_800845B4 keep-motion) is WRONG on both counts:
  183 = DownBoundU, and ft_800845B4/DownBound_Coll DO enter Fall on floor
  loss. The real mechanism: the DownBoundU/D subaction scripts carry opcode-25
  set_airborne_state events (ftAction_80071998 -> ftCommon_8007D5D4/8007D7FC):
  the bounce pops the fighter script-airborne mid-state (puff U@1/D@2 ->
  reground @17; fox/falcon @4 -> @22; sheik @4 -> @23) with NO motion change —
  that's the observed gnd 1->0 + jl 6->5 + y pinned (DownBound_Phys ft_80084F3C
  has no gravity) while ftCo_DownBound_Coll's grounded mpColl core keeps
  holding the floor line until the edge crossing enters Fall. Data + dispatch
  already existed end-to-end (scripts/{ch}.bin MSLFTSC1 events; the opcode-25
  dispatch in action_update_anim_callback_pre_input_fighter); the only gap was
  mpcoll_ground re-grounding script-airborne rows at floor publication. Fix:
  move_tables_script_airborne_latch_at_frame (last set_airborne_state event at
  or before the anim frame) + a DownBound keep-airborne owner at the floor
  publication flip in mpcoll_ground.c. The owner requires an ALREADY-airborne
  fighter (batch on_ground==0): the ground->air flip itself belongs to the
  pre-input dispatch, which keys on raw animation_index BEFORE mid-step
  normalization — an initial force-flip variant broke puff's
  test_downbound_to_downwait via the 0xFFFFFFFF-anim seed path (dispatch saw
  invalid anim, coll saw normalized 183, flip without D5D4 side effects ->
  spurious floor-loss Fall). Cross-suite: sheik 3406->3404, marth 1871->1870
  (rollout ±1 streak wash), aggregate 6525->6523, doubles/fox-falco
  byte-identical. FALCON ENV NOTE: local HEAD-vs-fix is byte-identical on
  every falcon replay, but the committed falcon_one_step.txt (last written by
  the falcon lane machine) does NOT reproduce here even at HEAD (jumps_left
  13/12 committed vs 12/16 local on two replays) — this machine's known
  compiler-FP baseline delta extends to DISCRETE lanes; do not chase falcon
  deltas against the committed report, compare stash-HEAD vs fix locally.
- 2026-07-10 (owner-4 recon, no fix yet): **The Fall->Landing family is an ECB
  bottom-lane split, not floor_skip.** The previous entry's floor_skip
  hypothesis is refuted at the source: Fighter_ChangeMotionState calls
  mpClearFloorSkip on every state change, and ftCo_8009A228 (Pass entry)
  re-arms it AFTER the change — the drop-through skip only survives within the
  Pass state itself. Suite-wide collection (Fall(29) <-> Landing(42) action
  flips, both directions, ~19 instances) splits cleanly in two:
  (a) MISSED main-floor landings on short-hop-fastfall rows (falco
  master-diamond rec 168: Fall@0, vy -3.5, root 0.425 -> ref lands 0.003; our
  out falls through; falcon diamond-diamond 1061/1151, puff 3559): the source
  10-frame CollData_X130 bottom lock from the jump's ftCommon_8007D5D4 is
  still live, so the source sweep uses the LOCKED (jump-entry, ~= root)
  bottom; our sweep used the aerial pose bottom (falco Fall pose ~4.02) and
  missed the crossing entirely.
  (b) ONE-FRAME-EARLY platform landings on long-airborne stale-pose rows (fox
  vs-fox BF rec 3696: uair 40+ frames then Fall@4-5 fastfall past the BF side
  plat at 27.2; ref passes 22.718 then lands UP at 27.200 the next step; our
  out lands a frame early; also falco 5507/5508 implying an effective bottom
  ~6 units above root): the ref's effective sweep bottom (>= 4.482) exceeds
  EVERY pose-table value (Fall 4.02-4.07, uair tail 4.0-4.4) — mechanism not
  pinned; locks are expired (10 frames << 40), stick was neutral (not the
  ftCo_80096CC8 hold-down platform-drop gate), and mpCollInterpolateECB
  resolves fully at steps=1. Suspects for next round: the seed-side
  ecb_lock_bottom_rel_y derivation (valid=0 on these rows — maybe it should
  not be), and the desired-vs-current ECB handoff in mpColl_80047E14
  (LoadECB_inline mode 6) around the aerial->Fall anim switch.
  ADDENDUM (same day): sub-shape (a) is NOT the SHFF lock after all — the
  falco rec-168 and falcon rec-1061 witnesses jumped 12+ frames before
  landing (locks legitimately expired; both initiate fastfall exactly 4
  frames before the missed landing; ftCommon_FallFast does NOT touch the ECB
  in source). Sharper falcon fact: falcon's Fall@0 pose bottom is 2.06, so
  our own pose-bottom sweep (0.256+2.06 -> -3.244+2.06) CROSSES y=0, yet the
  runtime still published airborne — the miss is a floor-ADMISSION rejection
  on the wrapped-anim fastfall Fall row, not a bottom-value question. Both
  witnesses also sit exactly on the Fall anim loop wrap (af 7 -> 0), so
  ecb_frame/ecb_frame_prev biasing around AObj wrap is in play. Seed-lane
  detail for whoever picks this up: derive_ecb_lock_bottom_rel_y publishes
  valid=1 only for JumpAerialF/B lock episodes; ground-jump episodes track
  desired_bottom=0 but never publish, and all these witness rows carry
  valid=0, so the runtime is on its pose/admission path throughout. Bigger
  owners remain first per the taxonomy: fox-laser item rows (~280) and the
  match-start Fall->Wait pin (150).
- 2026-07-11: **Replay intake: three user-recorded YS/BF ditto sessions close the
  remaining coverage gaps; suite 12 -> 15 replays, one-step baseline 2267 / 13814691
  (strict 2988).** Registered in replays/suites/puff.json: rollout_ends_ys.slpz
  (347 SpecialNStartL + 353 SpecialNEndL on both ports, multi-Turn budget-exhaust
  chains, five platform/edge roll-offs into air release, long NHit segments),
  rollout_air_bf.slpz (357 AirNFull, 361 AirNEndL twice — CONFIRMS the shared
  90-frame budget exhausts airborne, ~100f total release before the end fires,
  matching the modeled gate), wallbounce_teeter_ys.slpz (360 AirNEndR, two
  air-release wall bounces off the YS side walls, isolated OttottoWait->Fall teeter
  walk-off witness f3-101 on the left plat inner edge). ONLY remaining
  zero-coverage state: 359 AirNTurn — unreachable in play (entered solely via
  floor-loss during a >6.5-vel ground Turn; suite notes updated). New-coverage
  taxonomy: almost all documented debt (NLoop->NFull charge reseed, one row per
  NTurn exit, air natural-end reseed, mjump turnaround facing); wall bounce
  produced NO mismatch family. Rollout baseline on the expanded suite:
  first_mismatch_total 412 / seeded 277. New witnesses queued: the teeter
  walk-off re-ground (rec 224) and more Fall->Landing owner-4 rows.
- 2026-07-11 (later): **Teeter walk-off platform endpoint phantom fixed; puff
  one-step 2267 -> 2075 (strict 2988 -> 2796).**
  BUG (user-reported: cannot walk off a platform while teetering; witness f3-101
  wallbounce_teeter_ys): OttottoWait->Walk->Fall walk-off works, but the NEXT
  frame's raw bottom sweep re-hit the plat and the mpColl_80044838_Floor platform
  endpoint magnet snapped the fighter back to the endpoint into Wait —
  free-running oscillates Fall/Wait at the edge forever. ROOT CAUSE (bigger than
  the bug): the sim's floor-sweep prev endpoint is ONE FRAME STALER than source
  mpCollPrev everywhere — source promotes coll.last_pos = coll.cur_pos (the
  previous frame's Coll root, ft_80081DD4/ft_80081D0C), while the builder seeds
  pos[i-1] and the runtime promotes the pre-Phys prev_pos (= post(N-2) at use
  time). A fighter that left an endpoint last frame still sweeps FROM the
  endpoint. A full convention flip (builder pos[i] + promoting
  coll_stage_cur_pos) fixes puff (-226) but regresses fox_falco +22 / sheik +52 /
  aggregate +326 — below-floor ledge-snap and damage owners are calibrated
  against the stale prev (e.g. previous_below_source_floor_depth bounds) —
  REVERTED; do not flip globally without re-calibrating those families.
  RETAINED narrow owner (mpcoll_ground.c): the platform endpoint magnet re-runs
  mpLineIntersectionH with the sweep start rebased onto the frame-start root
  (prev_pos, same bottom offset) and only admits the snap when the fresh sweep
  still hits the line; damage/hitlag rows are exempt (hitlag-exit ASDI keeps
  source-preserved sweep roots; a marth DamageN-on-endpoint row regressed without
  the exemption). Gates: fox_falco/marth one-step byte-identical, sheik/aggregate
  discrete identical (one 4e-5 pos_x mae float shift on one sheik replay),
  doubles 9727 -> 9702 (two replays improved), puff free-running teeter segment
  now tracks ref exactly. Fix also cleared platform-endpoint phantom re-grounds
  across the suite (biggest: Game_20260612T031848 PS -72, master-diamond -36).
  Full pytest green modulo known environmental failures; standing puff reports
  regenerated. Free-running rollout on the 15-replay suite: first_mismatch_total
  412 -> 379, seeded 277 -> 244.
