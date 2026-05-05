# Guard / Shield / Reflector Source-First Pass

Status: checkpoint, not guard/shield/reflector owner-family closure.

## Starting Evidence

Input census: `reports/triage/post_pass_floor_skip_census/`.

- Guard/shield/reflector was the best bounded implementation target after the Pass floor-skip
  checkpoint: 265 scored direct first rows.
- Dominant first fields were `action_frame` (130), `state_flags[3]` (48), `last_hit_by` (35),
  and `item_exists` (26).
- Dominant direct source buckets:
  - `F01_guard_release_collision`: 212 rows.
  - `F02_guard_timer_flags`: 51 rows.
  - `F20_speciallw_shine_reflector`: 2 rows.

## Source Map

Audited:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c`
  - `ftCo_GuardReflect_Anim`
  - `ftCo_80093BC0`
  - `ftCo_GuardReflect_IASA`
  - `ftCo_GuardSetOff_Anim`
  - `ftCo_80092F2C`
  - `ftCo_8009370C`
  - `ftCo_8009388C`
  - `ftCo_80093A50`
- `refs/melee/src/melee/ft/ftcoll.c`
  - `ftColl_80076CBC`
  - `ftColl_80077464`
  - `ftColl_80077688`
  - shield, powershield, and reflect contact paths around `lbColl_80007BCC`.
- `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c`
  - reflector state setup/lifecycle.
- Current sim:
  - `src/state_flags.c`
  - `src/combat.c`
  - `src/items.c`
  - `src/shine.c`
  - `src/timers.c`
  - `src/hitboxes.c`

Retained source owner:

- `Fighter_procUpdate` runs `GuardReflect_Anim` before `GuardReflect_IASA`.
- `GuardReflect_Anim` calls `ftCo_80093BC0`.
- `ftCo_80093BC0` clears x221C_b3 immediately and decrements/owns the x14/x18 timer lanes:
  - x221C_b1 follows `mv.co.guard.x14`.
  - x221C_b2 follows `mv.co.guard.x18`.
- `GuardReflect_IASA` can then exit same-frame into destinations such as `KneeBend` and `Pass`.
  The destination post-frame exposes the timer state produced by the preceding anim callback tick,
  not a hardcoded destination-action mask.

## Implemented Owner

Implemented in `src/state_flags.c`:

- Added a source-backed GuardReflect post-anim timer helper for x14/x18.
- Generalized GuardReflect exit x221C_b1/x221C_b2 ownership for non-GuardReflect,
  non-GuardSetOff destinations.
- This replaces the previously narrow KneeBend-only stale x221C_b1 clear with the shared
  `GuardReflect_Anim -> ftCo_80093BC0 -> GuardReflect_IASA` callback ordering.

Covered by retained locks:

- Synthetic hard-floor GuardReflect -> KneeBend where x14/x18 both remain live after the anim tick.
- Synthetic hard-floor GuardReflect -> KneeBend where x14 expires and x18 remains live after the
  anim tick.
- Replay-real soft-platform GuardReflect -> Pass expired-x14/live-x18 row.
- Replay-real GuardReflect -> KneeBend live-x14/live-x18 row.
- Replay-real FD/cardinal GuardReflect -> KneeBend expired-x14/live-x18 row.

## Rejected Bridge Attempts

Rejected:

- Broad late GuardSetOff x221C_b1/x221C_b2 clear when timers appeared expired.

Reason:

- It improved aggregate counts but regressed FD/cardinal primary one-step rows (+5 primary
  one-step mismatches).
- The failures showed the attempted broadening was over-clearing rows still owned by existing
  GuardSetOff hitlag/steady callback bridges and/or missing hidden guard hitlag timer substrate.
- The retained patch therefore leaves the existing narrow GuardSetOff post-hitlag and first-steady
  locks intact.

## Residuals By Source Owner

Remaining guard/shield/reflector direct residuals are source-named, but not closed:

- `F01_guard_release_collision`: still dominant. These rows are mostly action-frame/contact
  ordering around GuardSetOff, GuardReflect admission, shield/BODY/contact precedence, and
  projectile/item contact. Several look more like combat/item-contact narrowphase ownership than
  pure guard timer ownership.
- `F02_guard_timer_flags`: reduced by this pass but not closed. Remaining rows are primarily
  GuardSetOff steady/post-hitlag timer substrate and callback-handoff rows where source ownership is
  not yet represented by a complete hidden guard/CollData seed surface.
- `F20_speciallw_shine_reflector`: still belongs to SpecialLw/shine reflector lifecycle and
  contact ordering, not the retained GuardReflect timer-bit owner.

This pass does not close GuardSetOff writeback, shield damage/hitlag lanes, powershield contact
classification, reflector item ordering, or same-frame shield/BODY precedence.

## Metrics

Before: current HEAD after the Pass floor-skip checkpoint.

After: retained slice is GuardReflect exit x221C timer-bit ownership.

- Aggregate one-step discrete mismatches: 6381 -> 6357.
- Aggregate strict mismatches: 7753 -> 7729.
- Aggregate rollout first mismatch total: 1382 -> 1362.
- Aggregate rollout first mismatch seeded: 815 -> 793.
- Aggregate rollout streak count: 1242 -> 1224.
- Aggregate rollout median first mismatch: 131 -> 133.
- Guard timer direct first rows (`F02_guard_timer_flags`): 51 -> 29.
- Raw guard first rows (`F01 + F02 + F20` in taxonomy): 280 -> 258.
- Census-equivalent guard/shield/reflector residual estimate: 265 -> 243.
- No one-step replay-level regressions.
- No FD/cardinal one-step regressions.

Rollout-only note:

- `validation_report_diff` reports one aggregate rollout p90 decrease for
  `PhysicalElectricCapybara.msl` (`rollout.streak_len.p90`: 296 -> 257).
- The same replay improves direct/count metrics: one-step, strict, rollout first-mismatch total,
  seeded first mismatches, and streak count all decrease; best/max exact window improves.
- This is treated as a distribution-only rollout shift, not a retained owner regression.

## Validation

Completed:

- `make build`: passed.
- Focused guard/shield/reflector/combat/item tests:
  - `uv run pytest tests/test_guard_reflect_exit_timer_bits.py tests/test_guard_reflect_timer.py tests/test_guard_callback_order_replay_real_locks.py tests/test_guardreflect_laser_shieldhit_replay_real_locks.py tests/test_laser_reflect_identity.py tests/test_laser_shield_contact_replay_real_locks.py tests/test_guardsetoff_post_hitlag_b2_narrow_replay_real_locks.py tests/test_guardsetoff_post_hitlag_b1_replay_real_locks.py tests/test_guardsetoff_post_hitlag_owner_seed_replay_real_locks.py tests/test_guardsetoff_hitlag_exit_phase_seed_replay_real_locks.py tests/test_shield_contact_seed_owner_replay_real_locks.py tests/test_combat_mutations_pass1.py tests/test_guard.py tests/test_shine_reflect_bit.py -q`
  - 132 passed, 33 skipped for unavailable local debug datasets.
- Forced aggregate preprocess:
  - `uv run python -m tools.slippi.preprocess_suite --suite replays/suites/aggregate_recent.json --datasets-dir datasets --force`
  - Built 29 datasets, skipped 0, stale 0, missing 0.
- `make test`:
  - 1686 passed, 1183 skipped for unavailable optional local debug datasets/artifacts.
- `make validate-all`: passed.
- `make fmt-check`: passed.
- `git diff --check && git diff --cached --check`: passed.
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 180`:
  - No one-step replay-level regressions.
  - Suite-level count metrics improved as listed above.
  - One rollout-only replay-level p90 decrease remains documented above as distribution-only because
    the same replay's one-step, strict, first-mismatch, seeded, streak-count, best, and max metrics
    improve.
