# Marth boundary-row pass (continuation of TRIAGE_MARTH_ROWS.md)

State: uncommitted on `newchar` (pending review). Scope: only the five rows previously
classified "still plausibly Marth". Method: clean-reseed each witness in isolation first to
separate real owner faults from inherited rollout drift.

## Row-by-row

1. 1859/2204 (DS ledge grab one frame early) - MARTH FAULT, FIXED.
   - Clean reseed reproduced: my cmd1 arming ran pre-physics on stale start-of-frame vel.y,
     so the launch frame (script cmd0 activates while entry-fall vy is still negative) armed
     cmd1 a phase early and the first descending frame grabbed.
   - Fix: arm cmd_vars[1] in the Phys descent branch (which first runs on the SECOND
     descending frame, cmd2 having been set by the previous frame's launch branch) - exactly
     ftMs_Special(Air)Hi_Coll's "first descending pass arms, second grabs". Both witnesses
     now byte-clean from clean reseeds, including the downstream CliffWait rows.
   - One-step companion: teacher-forced rows mid-descent could never grab (cmd lanes are not
     in the Slippi seed). Added a decomp-backed reseed derivation: action 367/368 + airborne +
     vy<0 + script cmd0 active at the seed frame => cmd1=cmd2=1
     (refs ftMs_SpecialHi_{Phys,Coll}).

2. 8763 (tumble -> air side-B) - MARTH-DISPATCH FAULT, FIXED. (My earlier "Squat" read was
   wrong: action 38 = DamageFall.)
   - ftCo_DamageFall_IASA delegates to ftCo_SpecialAir_CheckInput (decomp line cited in the
     fix); the marth B-dispatch allow-list lacked DamageFall, so post-hitstun tumble B-inputs
     were eaten. Added with the existing hitstun gate. Witness rows 8763-8768 now exact.
   - Residue at 8769+: ref takes falco's uair tipper (~21.6u range) during the side-B; we
     miss the contact. Matched states/positions; combat overlap epsilon at tipper range -
     classified generic combat-geometry boundary.

3. 6035 chain (DS air anim-end) - PROVEN INHERITED DRIFT, NOT A MARTH FAULT.
   - Clean reseed at 5980 reproduces the entire DS + FallSpecial episode byte-exactly,
     including the 6035 exit row (anim end 40.0 from the tracks table; our boundary math is
     vanilla-exact). The rollout break inherits a one-frame skew from an earlier generic
     break in the same streak (the 5757 Guard-family row). Reclassified: generic upstream.

4. 6351/6352 (op52 blip + swap row) - GENERIC ECB-INTERPOLATION BOUNDARY, marth-exposed.
   - Clean reseed shows the air->ground End0 swap one frame late: the landing is an ECB
     BOTTOM vs BF top-platform contact from a tuck pose where the diamond bottom rides ~4u
     ABOVE the root. Vanilla's interpolated cd->ecb crosses the platform a frame before our
     floor(frame) table sample. Changing global ECB frame sampling for one row risks fox
     stability; documented as shared ECB-interpolation debt (the op52 bit is correct once the
     swap frame aligns).

5. 3416 (hitstun 33 vs 34) - GENERIC FLOAT-PARITY BOUNDARY.
   - Applied damage and percent match exactly (44.97 -> 56.37); only the initial hitstun
     differs by one => the computed knockback sits a fraction below an (int)(kb*0.4) floor
     boundary. The KB formula is the shared validated path (bit-exact across the fox
     suites); proving the marth-weight float op order would need asm-level KB review.
   - Classified: shared KB float-parity debt, marth-exposed; single row.

## Metrics

- Rollout first-breaks: 33 -> 31; median streak 231 -> 285 (best streak grew through the
  former 1859/2204 breaks).
- One-step discrete: 225 -> 210 / 1,079,562 (transient 233 mid-pass before the cmd-lane
  reseed derivation landed).
- Fox/Falco validate-all + report diff: no suite total changes, no regressions, no reds.
- Full make test: 3259 passed; marth tests 114; fmt/diff checks clean.

## Remaining-row split (delta from TRIAGE_MARTH_ROWS.md)

Marth-specific remaining: NONE - the five investigated rows are now fixed (1859/2204, 8763)
or proven generic/inherited (6035 chain, 6351/6352, 3416).
Generic/common (unchanged list + reclassified): Guard blends, grab/catch flow, platform-drop
parsing, FD-underside Fall grabs, p1 falco rows, combat admissions (now incl. 8769/8614
tipper-range contacts), ECB-interpolation platform landings (6351/6352), KB float parity
(3416), and the inherited-drift rows.
