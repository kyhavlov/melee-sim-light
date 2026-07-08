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
