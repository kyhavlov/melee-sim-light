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
