# Phase 1 Residual Validation Ledger

Status: validation evidence after the packets 1–5 map-collision cutover; not a repair queue.

Baseline: Packet 1 / `HEAD` reports at `a293980533bd` plus committed Packet 1.
Current reports: `reports/validation/*`, regenerated after the complete Phase-1 cutover.

## Acceptance decision

Phase 1 is accepted on architectural completion, with broad one-step and rollout improvements as
supporting evidence. Restoring the deleted coordinator or adding replay/action-specific predicates
to make this ledger green is out of scope. These rows are hypotheses about connected or incomplete
source owners; they should close naturally as those owners are ported.

Suite-level improvements:

- aggregate one-step: 9,918 -> 8,689 (-1,229);
- aggregate rollout first mismatches: 1,667 -> 1,396 (-271);
- aggregate rollout seeded first mismatches: 879 -> 742 (-137);
- doubles one-step/rollout: 8,332 -> 7,713 and 1,252 -> 1,193;
- Falcon one-step/rollout: 3,143 -> 2,537 and 596 -> 526;
- Sheik one-step/rollout: 3,069 -> 2,711 and 462 -> 441.

Primary Fox/Falco remains the strict control: one-step is 30 -> 46 and rollout first mismatches are
0 -> 9. The remaining delta is important evidence when auditing the relevant source owners, but it
does not define a row-by-row cleanup phase.

## Post-review source-owner cleanup

The staged review checkpoint had drifted to 9,077 aggregate one-step mismatches and 1,692 rollout
first mismatches. Three bounded source-owner repairs brought those totals to 8,689 and 1,396:

- Rebirth now initializes CollData once at the stock-reset teleport boundary, including the source
  8x8 ECB, instead of clearing collision state after every Rebirth map phase;
- CapturePulledHi/WaitHi/DamageHi consume 477E0 FloorMask in their installed map callback and enter
  the corresponding Lw family there; the later grab-flow floor reprobe was deleted;
- `msl_mplib_project_fighter_floor` now traverses the generated active fighter-floor graph. Frozen
  Stadium lip-to-main-floor motion no longer reuses raw transformation links and falsely enters
  Ottotto.

The cleanup is general callback/topology ownership, not replay fitting. Synthetic positive locks
cover the capture continuation and Stadium connected-floor projection.

## Largest/earliest clusters

| Replay / first useful row | Observed boundary | Source-owner hypothesis |
|---|---|---|
| `QuerulousGrandDinosaur`, ~455 | DamageAir3 root remains below FD while reference publishes the carried floor/root | Damage entry ECB/root publication and stay-airborne floor projection |
| `LawfulInsistentMeerkat`, 259 | Yoshi terminal floor/wall endpoint admits PassiveStand while reference remains airborne DamageFly | exact `mpCheckFloor` endpoint extension/remap/order |
| `ImpassionedAlarmedTarsier`, 8904 | AttackDash/DownBound overlap and floor-loss transition | DownBound/overlap motion callback owner |
| `ThisVioletRaccoon`, 11776 | late combat hit changes Damage destination | Phase-2 fighter-contact/ProcessHit owner, not map geometry |
| `AttachedGoodNaturedGuanaco`, 2532 plus one later break | throw/combat/RNG aftermath | Phase-2 contact/throw/RNG ordering |
| `PositiveRevolvingHyena`, ~365 | GuardReflect remains/enters GuardSetOff differently | guard animation/contact handoff |
| `GrowlingBogusAlbatross`, ~542 | hit enters DamageAir while reference remains GuardSetOff | fighter-contact/ProcessHit owner |

The report diff also contains many distribution metrics for these same first breaks; they are not
independent bugs. Use `reports/triage/core_rewrite_validation_diff.txt` for the complete local diff.

## How to use this evidence

1. Build upcoming work from the decomp inventory. Consult this table only when the selected source
   boundary reaches one of the hypothesized owners.
2. If a row falls entirely inside an owner claimed source-complete, use it as an entry point to
   audit the source boundary and port—not as the patch boundary.
3. Fix `mpLib`/`mpColl`, MotionState, or contact ownership generally; never key on replay, record,
   dataset, character-as-proxy, or a validation lifecycle flag.
4. Keep the new `mp_lib`/`mp_coll`/source-air/source-ground runtime as the only collision path.
5. Require focused positive/negative source-owner tests and regenerate reports after each coherent
   cutover. Treat float-only movement as secondary unless it changes a discrete branch.
