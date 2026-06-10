# Check-in 2 Report: systematic common-action Marth coverage

State: uncommitted on `newchar` (pending review approval).
Snapshot: reports/marth_port/checkin2.patch
Matrix: reports/marth_port/COVERAGE.md (full per-bucket table)
Progress log: reports/marth_port/PLAN.md

## The deliverable
tests/test_char_common_action_coverage.py - 84 tests, parameterized over the registry character
set minus falco (kept out only to focus the matrix; the param derives from
tools.extraction.char_registry.CHARS so new ports join automatically).
Expectations derive from each character's EXTRACTED DATA (data/characters/<ch>.json attrs,
data/moves/<ch>.json movescripts, anim tables) plus shared ftCommonData constants - never
per-character literals. Fox rows validate the harness against the long-validated character;
marth rows validate the port. This file IS the reusable porting checklist: a future character
is common-action complete when it passes for them.

## Coverage matrix summary (full table in COVERAGE.md)
- Movement/physics: TESTED - wait/walk/turn/dash/run/runbrake/squat cycle/kneebend jump frames/
  double-jump multiplier/fall/fastfall/terminal/air-drift/friction, closed-form vs marth attrs
  (dash 1.5 -> run 1.8, jump_v 2.4, grav 0.085, fastfall 2.5, walk 1.6).
- Attacks: jab carries the CONTACT-DEPTH test (script-frame damage on a live victim + hitlag on
  both sides); tilts and up/down smash are ENTRY-TESTED only (state entry verified, sharing the
  jab's lifecycle machinery); fsmash adds the charge-hold test; NAir has lifecycle + landing lag
  == landing_airn_lag_frames; MSLACID1 stale/move-id tables verified present.
- Shield/defense: TESTED - GuardOn/Guard/GuardOff cycle, steady radius == inlineB0 closed form
  (init 11.75), GuardSetOff with shield HP drain and zero percent leak, rolls/spotdodge/airdodge.
- Grab/throw: TESTED - Z->Catch, grab connect + fthrow executes (throw hitboxes via movescripts).
- Damage/tech: TESTED - DamageFly hold + hitstun tick, DownBoundU -> DownWaitU, tech (Passive)
  with the x680/x684 window/debounce gates seeded correctly.
- Ledge: TESTED - CliffCatch with marth's own ledge_snap 12/17, CliffWait, climb + attack options.
- Collision: TESTED - ECB grounded rest, BF platform land at y=27.2, platform drop-through,
  hurtcaps presence + consumption, ledge-grab AABB.

## Generic refactors vs Marth-specific code
Refactors (2 files):
1. src/move_tables.c - THE find of this check-in: char_slot() hardcoded {1->0, 22->1} with a
   2-char cache array; every movescript-driven mechanic (dash->run cmd0, runbrake, turnrun,
   jab combos, smash charge, throw hitboxes/pulses, catchattack windows, hit_status,
   hurtbox_can_hit masks, sfx/projectile pulses) silently returned NULL for marth. Now
   MSL_CHAR_REGISTRY-driven (slot = registry index, count = registry count, init loops registry).
2. src/motion_state_owners.c - msl_motion_state_common_class2_has/class3_has fox&&falco
   intersections now loop all registry characters.
Marth-specific C: ZERO. All marth common-action behavior comes from his extracted data.

## Remaining gaps (deferred with reasons; also in COVERAGE.md)
- Cliff roll/jump option rows (same selector as the tested climb/attack options).
- Dedicated pummel (CatchAttack) timing test (windows verified loaded via move_tables).
- Wall/ceiling char-specific rows (no per-char data exists; generic mpcoll already covered by
  fox/falco suites).
- PROMINENT carry-over: marth cape/hair dynamic chains suppressed (empty SSDYNN01); hurtcaps on
  bones 60/70/71 ride them in vanilla. Revisit on any head/cape geometry witness.
- Specials (action ids 341-372) = check-in 3, decomp-first (replay covers only
  343/345/346/347/358/368).

## Numbers
- Marth replay smoke (verification only, NOT the work queue): one-step discrete 1150 -> 684
  (/1,079,562 = 0.063%); rollout first-mismatches 697 -> 404; median streak 49 -> 75. The
  entire improvement came from the move-tables generalization - no replay-row-driven fixes.
- Fox/falco baseline: validate-all diff vs HEAD = "no suite total changes", no replay-level
  regressions, no reds of any class.
- Full test suite: 3229 passed (3145 prior + 84 new), 1183 skipped.

Proposed commit message:
  tests: add char-parameterized common-action coverage suite; generalize move tables
