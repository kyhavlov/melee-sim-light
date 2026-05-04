# AGG Rollout Median Regression Autopsy

## Question

`validation_report_diff --before HEAD --after reports/validation --top 180` reports:

- Primary rollout median: `351 -> 346`.
- `AttachedGoodNaturedGuanaco.msl` rollout median: `341 -> 314`.

This autopsy checks whether that is a real regression from the new capture/catch logic or a distribution shift caused by removing an earlier seeded first break.

## Inputs

Primary suite AGG dataset:

- `datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl`

Triage artifacts:

- `before_primary_rollout_streaks.json`: HEAD code in a detached worktree, current generated data, refreshed FD `.msl` cache.
- `after_primary_rollout_streaks.json`: current working tree, same `.msl` cache.
- `before_agg_rollout_locate.tsv`
- `after_agg_rollout_locate.tsv`
- `primary_rollout_streak_diff.json`
- `agg_rollout_locate_diff.json`

The HEAD worktree was run with `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data` because generated data artifacts are ignored and not present in a detached worktree by default.

## Before / After Metrics

AGG rollout streak histogram, before:

```text
[32, 99, 131, 201, 216, 289, 314, 341, 448, 456, 510, 554, 985, 1282, 1435]
median = 341
```

AGG rollout streak histogram, after:

```text
[32, 99, 131, 201, 216, 289, 291, 314, 341, 448, 456, 774, 985, 1282, 1435]
median = 314
```

Suite diff:

```text
suite_delta: total_streaks=0 median=-5 p90=0 p95=0 max=0 best_max=0
suite_first_mismatch_delta: raw=0 seeded=-1
first_mismatch_field_delta: action_id:-1 hitlag:+1
seeded_mismatch_field_delta: action_id:-1
```

AGG one-step / count metrics improve:

```text
primary one-step AGG discrete: 15 -> 11
primary one-step AGG strict discrete: 34 -> 30
primary rollout AGG first_mismatch_seeded_total: 2 -> 1
```

No one-step replay-level regression is reported for AGG.

## Changed Rollout Windows

Before:

```text
window [2694,3204) len=510
break  3204 action_id p0 seed/out/ref=366/223/226
seeded break 3204 action_id p0 seed/out/ref=366/223/226
window [3205,3759) len=554
break  3759 hitlag p0 seed/out/ref=0/3/4
```

After:

```text
window [2694,3468) len=774
break  3468 hitlag p0 seed/out/ref=0/6/7
window [3468,3759) len=291
break  3759 hitlag p0 seed/out/ref=0/3/4
```

The changed locate diff has no new cluster. It removes the capture action-id cluster:

```text
gone impact=148 freq=2 seeded=1 action_id[-1] seed/out/ref=366/223/226 example=AttachedGoodNaturedGuanaco.msl:rec=3204:p=0
changed impact=+149 freq=0 seeded=0 state_flags[4] seed/out/ref=0/0/128 example=rec=1772:p=1
```

The `state_flags[4]` change is the ignored camera/magnifying-glass bit count, not a scored gameplay first break.

## Local Checks

Current-code rollout probes:

```text
start 2694 -> first break 3468, len 774, hitlag p0 seed/out/ref=0/6/7
start 3204 -> first break 3759, len 555, hitlag p0 seed/out/ref=0/3/4
start 3205 -> first break 3759, len 554, hitlag p0 seed/out/ref=0/3/4
start 3468 -> first break 3759, len 291, hitlag p0 seed/out/ref=0/3/4
```

At record 3468:

```text
seed p0 action=65 anim=68 on_ground=0 hitlag=0 hitstun=0
ref  p0 action=65 anim=68 on_ground=0 hitlag=7 hitstun=0
ref  p1 action=88 anim=178 on_ground=0 hitlag=7 hitstun=41

rollout start 2694 target 3468:
  p0 out action=65 anim=68 on_ground=0 hitlag=6 hitstun=0
  p1 out action=88 anim=178 on_ground=0 hitlag=6 hitstun=41

rollout start 3205 target 3468:
  p0 out action=65 anim=68 on_ground=0 hitlag=7 hitstun=0
  p1 out action=88 anim=178 on_ground=0 hitlag=7 hitstun=41

seeded at 3468:
  p0 out action=65 anim=68 on_ground=0 hitlag=7 hitstun=0
  p1 out action=88 anim=178 on_ground=0 hitlag=7 hitstun=41
```

Interpretation: the new code removes the real 3204 capture action-id seeded break. Continuing the longer un-reseeded rollout from 2694 exposes an existing downstream hitlag/contact timing residual at 3468. The residual is not a one-step error and is not caused locally by catch/capture logic: seeding at 3204, 3205, or 3468 reaches the 3468 state exactly.

## Suspect Audit

- Catch wall obstruction gate:
  - No new rollout first-break cluster appears.
  - Added synthetic lock proves the wall-separated catch negative while same-side and above-wall controls still connect.
- Guard-family no-submotion catch fallback:
  - Not involved in the changed AGG records; no guard-family first-break cluster changed.
- Catch radius / model-scale compensation:
  - Existing focused catch locks still pass; no new catch connect cluster appears.
- Locked CollData floor handoff:
  - This is the intended owner that fixes record 3204.
  - Replay-real lock now covers record 3204 and the post-capture window through 3468.
- `input_buttons_pressed` CaptureWait jump latch:
  - No changed first-break cluster maps to CaptureWait jump latch behavior.
  - The changed scored cluster is exactly the removed 3204 action-id capture break and a downstream hitlag exposure.

## Conclusion

The rollout median drop is a harmless distribution shift from fixing an earlier capture-state first break. It is not an over-broad capture/catch regression.

The median moves down because the old seeded failure at 3204 split the rollout into `[2694,3204)=510` and `[3205,3759)=554`; after the fix, the continuous rollout becomes `[2694,3468)=774` and `[3468,3759)=291`. That inserts a 291-length streak below the median and removes 510/554, while one-step and seeded first-mismatch counts improve.

This remains a capture/throw checkpoint, not owner-family closure. Source-named residuals remain in `CollData/mpColl_800477E0` floor provenance, `ShieldDesc` / guard-family pose substrate, `ftCo_CaptureWaitHi/Lw` first-steady ownership, and `ftCo_Passive_*` / locomotion rollout ownership.
