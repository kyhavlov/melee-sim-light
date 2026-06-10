# Marth Common-Action Coverage Matrix (check-in 2)

Legend: COVERED = data loaded + engine path verified (no targeted test); ENTRY-TESTED = state
entry verified but not contact-depth; TESTED = synthetic decomp-backed test in
tests/test_char_common_action_coverage.py (runs for fox AND marth - fox validates the harness);
SMOKE = additionally exercised by the replay; DEFERRED = explicitly out of scope with reason.

The suite IS the reusable porting checklist: a new character is common-action complete when
test_char_common_action_coverage.py passes for it.

## 1. Movement / core physics
| Item | Status | Evidence |
|---|---|---|
| Wait | COVERED+TESTED | seeds settle in Wait; friction test |
| Walk (slow/mid/fast) | COVERED+TESTED | test_walk_max_velocity (walk_max 1.6) |
| Turn | COVERED+TESTED | test_turn_from_walk |
| Dash | COVERED+TESTED | dash init vel 1.5, terminal 1.8 (test_dash_*) |
| Run / RunBrake | COVERED+TESTED | dash->run via movescript cmd0 (frame 16); brake on release |
| Squat/SquatWait/SquatRv | COVERED+TESTED | test_squat_cycle |
| KneeBend + jumps | COVERED+TESTED | startup frames (attr), jump_v 2.4, JumpF/B |
| Double jump | COVERED (air_jump multipliers loaded) | exercised via aerial tests; replay smoke |
| Fall / fastfall | COVERED+TESTED | grav 0.085, terminal 2.2, ff 2.5 (attrs) |
| FallSpecial | COVERED (common path) | replay smoke (368 SpecialAirHi -> FallSpecial rows) |
| Landing | COVERED+TESTED | ECB rest + landing tests |
| Air drift | COVERED+TESTED | air_drift_max cap test |

## 2. Attacks
| Item | Status | Evidence |
|---|---|---|
| Jab (Attack11) | COVERED+TESTED (the contact-depth representative for all attack families) | contact test: script first-active-frame damage applies + hitlag both |
| Tilts | ENTRY-TESTED (state entry only; contact depth only on jab) | test_tilts_and_vertical_smashes_enter |
| FSmash + charge | COVERED+TESTED | test_fsmash_charge_hold (move_tables smash_charge) |
| Up/Down smash | ENTRY-TESTED (state entry; contact depth only on jab) | entry tests; replay lacks rows |
| Aerials (N/F/B/Hi/Lw) | COVERED+TESTED | NAir lifecycle + landing-lag attr test; replay smoke F/B/Hi |
| Landing aerial lag | COVERED+TESTED | LandingAirN duration == attr |
| Hitbox lifecycle | COVERED+TESTED | first-active-frame == movescript create_hitbox |
| IASA/interrupt flags | COVERED (MSLACID1 x4_flags + scripts loaded) | replay smoke |
| Stale/move ids | COVERED+TESTED | MSLACID1 table presence + preprocess lanes |
| Hitlag/hitstun/KB | COVERED+TESTED | contact test hitlag; weight 87 loaded; replay smoke |

## 3. Shield / defense
| Item | Status | Evidence |
|---|---|---|
| GuardOn/Guard/GuardOff | COVERED+TESTED | entry, settle, release-to-Wait |
| Shield radius/scale | COVERED+TESTED | analog-light radius == inlineB0 closed form (11.75 init) |
| Shield tilt | COVERED (marth tilt table extracted) | tilt lanes in replay smoke |
| GuardSetOff (shield hit) | COVERED+TESTED | jab-vs-shield: setoff + hp drain, no percent leak |
| GuardReflect (powershield) | COVERED (common machinery) | replay smoke; digital-edge PS observed in tests |
| Rolls (EscapeF/B) | COVERED+TESTED | test_spotdodge_and_rolls_enter |
| Spotdodge (Escape) | COVERED+TESTED | same |
| Airdodge (EscapeAir) | COVERED+TESTED | test_airdodge_enters_escape_air |

## 4. Grab / throw
| Item | Status | Evidence |
|---|---|---|
| Catch | COVERED+TESTED | Z -> Catch |
| Catch connect + throw | COVERED+TESTED | grab_then_fthrow: victim grabbed, throw executes |
| CatchAttack (pummel) | COVERED (move_tables catchattack windows now load for marth) | lifecycle shared |
| Throw scripted mechanics | COVERED | throw hitboxes/pulses via move_tables (registry fix) |

## 5. Damage / knockdown / tech
| Item | Status | Evidence |
|---|---|---|
| Damage* / DamageFly* | COVERED+TESTED | damagefly hold + hitstun tick; replay smoke (90 = DamageFlyN x461) |
| DamageFlyRoll | COVERED (common) | replay smoke |
| DownBound/DownWait | COVERED+TESTED | test_downbound_to_downwait |
| Tech (Passive) options | COVERED (common machinery) | replay smoke; DEFERRED dedicated window test |
| Weight/KB ownership | COVERED+TESTED | weight attr + contact KB; full KB regime via replay |

## 6. Ledge / cliff
| Item | Status | Evidence |
|---|---|---|
| CliffCatch | COVERED+TESTED | snap with marth's 12/17 ledge params |
| CliffWait | COVERED+TESTED | reached in cliff tests |
| Climb (slow/quick) | COVERED+TESTED | test_cliff_options stick-in |
| CliffAttack | COVERED+TESTED | test_cliff_options A |
| Cliff roll/jump | COVERED (same option selector) | DEFERRED dedicated rows (selector shared) |

## 7. Collision substrate
| Item | Status | Evidence |
|---|---|---|
| ECB tables | COVERED+TESTED | bottom/extents loaded; grounded rest exact |
| Hurtcaps | COVERED+TESTED | 11 capsules; contact test consumes them |
| Pushbox | COVERED (attrs loaded) | replay smoke (close-quarters rows) |
| Floor/platform land | COVERED+TESTED | BF platform land at 27.2 |
| Platform drop-through | COVERED+TESTED | test_platform_drop_through |
| Walls/ceilings | COVERED (generic mpcoll; no char data) | replay smoke (BF under-platform rows) |
| Ledge-grab AABB | COVERED+TESTED | cliffcatch geometry |

## Known deferrals (carried from check-in 1, still prominent)
- Marth cape/hair dynamic chains suppressed (empty SSDYNN01): hurtcaps on bones 60/70/71 ride
  them in vanilla. Revisit on any head/cape geometry witness.
- Specials (341-372) = check-in 3 (decomp-first; replay covers only 343/345-347/358/368).
- ftMarthAttributes ext block (sword/charge constants) extraction = check-in 3 prerequisite.
