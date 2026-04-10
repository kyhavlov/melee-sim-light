# Rollout Desync Triage Snapshot

Archived baseline diagnosis for rollout-first sim work. This snapshot was generated from the
row-level rollout locate artifact that reported:

```text
reports/triage/current_rollout_desyncs.tsv
rows=782 clusters=308
```

The x221F_b0 camera/dead-flow family is intentionally preserved here as historical evidence, but
camera bounds tracking is tabled for now because it is low value for RL compared with combat and
guard/shield correctness.

## A) Top Clusters Snapshot

1. impact=3097 freq=92 seeded=46 state_flags[4] 128/128/0, x221F_b0 clear miss, example AGG rec=1890 p1 Rebirth->Rebirth
2. impact=2580 freq=86 seeded=41 state_flags[4] 0/0/128, x221F_b0 set miss, example AGG rec=1772 p1 DamageFlyRoll->DamageFlyRoll
3. impact=1443 freq=27 seeded=0 action_id 178/178/179, GuardOn held too long, example AGG rec=1346 p1
4. impact=936 freq=14 seeded=1 action_id 182/182/179, GuardReflect held too long, example AGG rec=1605 p1
5. impact=905 freq=10 seeded=0 state_flags[3] 130/2/130, x221C_b0 drops in DamageFlyTop, example AGG rec=998 p0
6. impact=715 freq=8 seeded=0 action_id 90/90/86, DamageFlyTop misses DamageAir3 hit entry, example AGG rec=660 p1
7. impact=702 freq=12 seeded=0 action_id 88/88/183, DamageFlyN misses DownBoundU, example AGG rec=4553 p0
8. impact=543 freq=7 seeded=0 action_id 88/38/27, DamageFlyN exit chooses DamageFall vs JumpAerialF, example AGG rec=3515 p1
9. impact=512 freq=10 seeded=3 action_id 182/182/181, GuardReflect misses GuardSetOff, example AGG rec=549 p1
10. impact=490 freq=10 seeded=1 action_id 212/212/213, Catch misses CatchPull, example AGG rec=564 p0
11. impact=471 freq=6 seeded=0 action_id 20/20/75, Dash misses DamageHi1, example AGG rec=178 p1
12. impact=462 freq=8 seeded=0 action_id 20/29/20, Dash incorrectly enters Fall, example AGG rec=2313 p1
13. impact=402 freq=4 seeded=0 action_id 20/20/78, Dash misses DamageN1, example AGG rec=6916 p1
14. impact=396 freq=10 seeded=0 action_id 86/42/86, DamageAir3 incorrectly lands, example AGG rec=678 p1
15. impact=347 freq=2 seeded=0 action_id 43/178/43, LandingFallSpecial incorrectly shields, example GAT rec=5930 p1

## B) Family Table

| Rank | Family | Impact rows | Impact sum | Seeded breaks | Dataset coverage | Confidence | Implementation risk |
|---:|---|---:|---:|---:|---:|---|---|
| 1 | Combat hit application and damage-state selection | 232 | 13026 | 35 | 4/4 | High | High |
| 2 | x221F_b0 camera/dead-flow visibility | 178 | 5677 | 87 | 4/4 | High | Medium |
| 3 | Guard/shield lifecycle and shield-hit response | 107 | 5508 | 15 | 4/4 | High | Medium |
| 4 | Spacie special collision exits | 49 | 2952 | 8 | 4/4 | Medium | High |
| 5 | Damage-fly landing/tech/downbound exits | 49 | 2639 | 4 | 4/4 | Medium | Medium |
| 6 | Grab/catch acquisition and owner latch | 18 | 895 | 1 | 4/4 | High | Medium |

Recommended next top-3 after tabling camera work:

1. Combat hit application and damage-state selection.
2. Guard/shield lifecycle and shield-hit response.
3. Spacie special collision exits or damage-fly landing/tech/downbound exits, depending on desired risk.

## C) Owner Diagnosis Per Top Family

### 1. Combat Hit Application And Damage-State Selection

Evidence:

- AGG rec=1227 p1: KNEE_BEND->DAMAGE_FLY_TOP, cluster 24/26/90, hitlag 0->7, hitstun 0->58, streak 229.
- GAT rec=7215 p0: DASH->DAMAGE_N_1, cluster 20/18/78, hitlag 0->3, hitstun 0->9, streak 225.
- QGD rec=6421 p1: LANDING->DAMAGE_N_1, cluster 42/42/78, hitlag 0->4, streak 244.
- TBK rec=6760 p0: ATTACK_HI3->ATTACK_HI3, cluster 56/75/56; sim spuriously enters DamageHi1 while ref stays AttackHi3, streak 215.

Owners:

- `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC`
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C`
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AB48`
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AB80`
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0`
- `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll`

Missing or approximate behavior:

Hit ownership, hitbox-vs-hurtbox/shield eligibility, and ProcessHit state selection are still
approximate enough to both miss real hits and create spurious hits. The evidence includes both
directions: missed damage entries such as Dash/KneeBend/Landing -> Damage*, and false damage such
as AttackHi3 -> DamageHi1 while ref remains AttackHi3.

Why it explains the pattern:

The ref rows change action_id, hitlag, hitstun, and state_flags[1]/[3] together. That bundle is
owned by collision/contact plus `Fighter_ProcessHit_8006D1EC`, not by an Anim-only timer.
Confidence: high.

### 2. x221F_b0 Camera/Dead-Flow Visibility

Evidence:

- Cluster `state_flags[4] seed/out/ref=128/128/0`: 92 rows, impact 3097, seeded 46.
- Representative rows: AGG rec=1890 p1 Rebirth->Rebirth, QGD rec=7946 p0 Rebirth->Rebirth.
- Cluster `state_flags[4] seed/out/ref=0/0/128`: 86 rows, impact 2580, seeded 41.
- Representative rows: GAT rec=7985 p0 DamageFlyHi->DamageFlyHi, TBK rec=4566 p0 FxSpecialAirSStart->FxSpecialAirSStart.

Owners:

- `refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C`
- `refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC`
- `refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam`
- `refs/melee/src/melee/cm/camera.c::Camera_80030CD8`
- `refs/melee/src/melee/cm/camera.c::Camera_80030CFC`

Missing or approximate behavior:

The sim carries or clears fp+0x221F_b0 as a state-flag approximation instead of reproducing the
camera-subject visibility test each frame. Seeded breaks are high because reseeding at the row
still leaves the runtime unable to compute the next visibility bit.

Why it explains the pattern:

Only state_flags[4] differs; action, hitlag, hitstun, and grounded fields already match before
this compare field. `ftLib_80086A8C` explicitly writes x221F_b0 from camera subject visibility, and
`ftCo_Rebirth_Cam` updates the camera subject during Rebirth. Confidence: high.

Status:

Camera bounds/projection tracking is tabled. A later implementation should not use the static FD
world camera bounds as a substitute for `Camera_80030BBC`; that decomp path depends on active CObj
world-to-screen projection and scissor state.

### 3. Guard/Shield Lifecycle And Shield-Hit Response

Evidence:

- GuardOn->Guard: cluster 178/178/179, 27 rows, impact 1443; reps include GAT rec=3053 p1, AGG rec=1346 p1.
- GuardReflect->Guard: cluster 182/182/179, 14 rows, impact 936; reps include AGG rec=1605 p1.
- GuardReflect->GuardSetOff: cluster 182/182/181, 10 rows, impact 512; reps include AGG rec=549 p1, GAT rec=1290 p0.
- GuardOn->GuardSetOff: representative QGD rec=3676 p0, hitlag 0->6, ref flags state_flags[1] gains hitlag bit.

Owners:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Anim`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_IASA`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_Anim`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0`
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC` for shield-hit contact routing

Missing or approximate behavior:

GuardOn duration, GuardReflect timer expiry, GuardSetOff admission, and shield-hit handoff are not
consistently owned by the correct Guard Anim/IASA/contact callbacks. The sim often preserves
GuardOn or GuardReflect when ref has advanced to Guard, GuardOff, or GuardSetOff.

Why it explains the pattern:

The cluster IDs are almost entirely in 178..182, and ref rows match shield bit changes such as
state_flags[1] hitlag and state_flags[2] shield-active changes. Those are guard callbacks plus
shield contact, not generic locomotion. Confidence: high.

## D) Top Implementation Sequence

### 1. Combat Hit Application And Damage-State Selection

Likely files:

- `src/combat.c`
- `src/hitboxes.c`
- `src/hurtboxes.c`
- `src/hitlist.c`
- `src/timers.c`
- `src/action.c`
- possibly `src/items.c`

Add invariants/tests:

- Add replay-real locks for AGG rec=1227 p1, GAT rec=7215 p0, QGD rec=6421 p1, TBK rec=6760 p0.
- Each test should assert action_id, hitlag, hitstun, state_flags[1], state_flags[3], and attacker/victim hitlist state if exposed.
- Add one negative control for the spurious-hit case: TBK rec=6760 p0 must remain ATTACK_HI3.

Done in rollout-locate diff:

- Combat family impact drops materially.
- Clusters 24/26/90, 20/18/78, 42/42/78, and 56/75/56 are gone or lower.
- No new `--only-seed-equals-ref` one-step locate rows.

Rollback/safety:

- If total combat hit application seeded breaks increase, rollback.
- If `diff_rollout_locate` shows new high-impact action_id damage clusters, rollback.

### 2. Guard/Shield Lifecycle And Shield-Hit Response

Likely files:

- `src/shields.c`
- `src/state_flags.c`
- `src/action.c`
- `src/timers.c`
- `src/hurtboxes.c`
- `src/combat.c`

Add invariants/tests:

- GAT rec=3053 p1: GuardOn -> Guard.
- AGG rec=1605 p1: GuardReflect -> Guard.
- AGG rec=549 p1 and GAT rec=1290 p0: GuardReflect -> GuardSetOff.
- QGD rec=3676 p0: GuardOn -> GuardSetOff with hitlag gain.
- Assert state_flags[1], state_flags[2], state_flags[3], shield_hp, hitlag, and action_id.

Done in rollout-locate diff:

- Clusters 178/178/179, 182/182/179, 182/182/181, and 178/178/181 reduce or disappear.
- No new high-impact guard clusters with out=181 or out=178.

Rollback/safety:

- If shield-poke or GuardSetOff seeded-break rows increase, rollback.
- Run existing guard tests plus full `make test`.

### 3. Spacie Special Collision Exits Or Damage-Fly Landing/Tech/Downbound Exits

Likely files:

- `src/locomotion.c`
- `src/physics.c`
- `src/mpcoll_ground.c`
- `src/knockdown.c`
- `src/action.c`
- possibly `src/blaster.c` / `src/items.c` for spacie-specific side-B/blaster interactions

Add invariants/tests:

- Use representative clusters from this snapshot after the first combat/guard patch refreshes the
  rollout-locate baseline.
- Keep collision-exit tests scoped to action_id, action_frame, on_ground, ground_id, speed, and
  state_flags rows that the owning callback actually writes.

Done in rollout-locate diff:

- Relevant action-exit clusters reduce without introducing new high-impact DamageFall,
  LandingFallSpecial, or downbound clusters.

Rollback/safety:

- If ground collision or ledge-related guardrails move, rollback and isolate the collision owner.

## E) Validation Commands

Baseline before each implementation patch:

```bash
make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/baseline_rollout_desyncs.tsv
make rollout-locate-summary ROLLOUT_LOCATE_TSV=reports/triage/baseline_rollout_desyncs.tsv ROLLOUT_TOP=30
```

After patch:

```bash
make test
make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv
make rollout-locate-summary ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv ROLLOUT_TOP=30
make rollout-locate-diff ROLLOUT_LOCATE_BEFORE=reports/triage/baseline_rollout_desyncs.tsv ROLLOUT_LOCATE_AFTER=reports/triage/current_rollout_desyncs.tsv ROLLOUT_TOP=30
```

One-step safety gate for "seed==ref new rows must stay 0":

```bash
make guardrail-preflight
```

Full sim-logic validation before review:

```bash
make validate OUT=reports/validation/one_step_suite_eval.txt
make validate-rollout OUT=reports/validation/rollout_suite_eval.txt
make rollout-locate-diff ROLLOUT_LOCATE_BEFORE=reports/triage/baseline_rollout_desyncs.tsv ROLLOUT_LOCATE_AFTER=reports/triage/current_rollout_desyncs.tsv ROLLOUT_TOP=30
```
