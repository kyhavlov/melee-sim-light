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
