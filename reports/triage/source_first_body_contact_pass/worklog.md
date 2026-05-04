# Source-first BODY/contact pass worklog

## Checkpoint scope

This is a BODY/contact source-first checkpoint, not a closed BODY/contact family.

Closed in this checkpoint:
- fighter shield/BODY source ordering from `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`
- same-frame shield/BODY hit-group continuation and same-group victim-ring suppression
- regular fighter BODY deferred damage-log / best-KB / stale/combo/source writeback from:
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8`
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007891C`
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C`
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C`
  - `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`
- narrow SpecialLw item-contact ordering slice: `ReflectDesc` before item shield/BODY in `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`
- narrow shield-context laser item HitCapsule ordering slice before item shield/BODY
- debug-only hitbox group/setter fixture support used by synthetic locks

Not closed:
- full BODY/contact
- full item contact ordering under `ftColl_8007925C` / `ftColl_80077C60` / `itcoll.c`

No dataset/record/frame predicates, stage/line allowlists, char-id proxies, replay-future lanes, or Python hot-path derivation were added.

## Source map audited

Primary source surfaces:
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`
  - fighter/item contact order is `ReflectDesc`, `AbsorbDesc`, item HitCapsule vs fighter HitCapsule catch/clank path, shield, BODY.
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_80077C60`
  - adjacent fighter/item contact owner surface still not fully ported.
- `refs/melee/src/melee/ft/ftcoll.c::ftColl_80077970`
  - fighter HitCapsule vs item HitCapsule owner/check path used before later shield/BODY branches.
- `refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_802706D0,it_80272460}`
  - item HitCapsule callbacks, reciprocal item/fighter loops, BODY callback/lifetime, and article provenance remain item/article-owned surfaces for the next pass.
- `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007AFC`
  - capsule overlap basis for HitCapsule-vs-HitCapsule contact.
- related lbColl victim-ring helpers
  - used to keep same-group continuation/suppression source-shaped rather than row-shaped.

## Narrow item-contact slice added

Added a laser/item-contact ordering gate in `src/items.c`:
- after existing `ReflectDesc` handling,
- before item shield and BODY handling,
- only in shield context (`shield_radius > 0.0f`),
- tests defender fighter HitCapsules against the laser/item HitCapsule sweep,
- suppresses later item shield/BODY side effects for that fighter when the source clank/catch path owns contact.

This is intentionally a narrow shield-context item-contact ordering slice. It is not a full port of `ftColl_8007925C`, `ftColl_80077C60`, or `itcoll.c`.

The gate is constrained by data-backed hitbox fields:
- `MSL_HITBOX_FLAG_CLANK`
- `MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION`
- non-catch/non-inert hit element
- fighter HitCapsule damage must be positive
- fighter HitCapsule damage must not exceed item damage
- active shield context is required so the slice does not steal ordinary item BODY hits

The damage and shield-context guards were added after replay-real BODY locks showed broad clank suppression could wrongly steal real laser BODY hits from stronger fighter attacks and non-shield BODY rows. This is a source-shaped correction, not a row predicate: `ftColl_80077970` owns the fighter HitCapsule vs item HitCapsule check, while full item-contact callback/lifetime behavior remains open.

## Locks added / exercised

Synthetic owner lock added:
- `tests/test_item_reflector_contact_order.py::test_item_hitcapsule_clank_precedes_shield_and_body_contact`
  - debug-only hitbox/hurtcap fixture keeps item HitCapsule, fighter HitCapsule, and BODY overlap colocated.
  - verifies fighter HitCapsule clank ordering suppresses later item shield/BODY side effects in shield context.

Replay-real locks exercised after the new slice:
- `tests/test_laser_grounded_body_segment_replay_real_locks.py`
- `tests/test_laser_hitcap_prev_scale_replay_real_locks.py`
- existing shield/reflect/BODY replay locks in the focused contact batch

## Rejected bridge attempts / corrections

Rejected broad fighter-hitbox overlap suppression:
- First implementation suppressed later item BODY for any clankable overlapping fighter HitCapsule.
- `make test` caught replay-real regressions:
  - `AttackHi3 grounded laser segment admits BODY hit`
  - `following airborne laser full BODY hit still consumes`
- Fixed by constraining to item-interaction/clank data and damage ordering instead of adding record/action predicates.

Rejected broad non-shield item HitCapsule suppression:
- Validation report diff showed one-step replay-level regressions in non-shield contexts.
- Final gate is shield-context-only (`shield_radius > 0.0f`).
- Final validation report diff after this narrowing has no replay-level regressions.

No narrow laser/body bridge was removed yet. This checkpoint adds the next shared item-contact ordering slice and keeps existing bridges until fuller item-contact callback/provenance ownership supersedes them.

## Residuals / not closed

Still open, source-named residuals:
- full item contact ordering under `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}`
- reciprocal fighter-hitbox-to-item-hurtbox and item-hitbox-to-fighter loops in `refs/melee/src/melee/it/itcoll.c`
- item shield/bounce internals and flags
- `AbsorbDesc` ordering after reflect and before item/body paths
- item BODY damage/lifetime callback ordering
- item source/owner/instance/stale/combo provenance writeback
- powershield / GuardSetOff internals
- broader candidate narrowphase ownership
- deletion of narrow laser/body/item bridges once shared item-contact owner is comprehensive

This remains a checkpoint only. BODY/contact/item-contact is not closed.

## Validation

Final validation after the shield-context narrowing:
- `make build` -> passed
- `uv run pytest tests/test_item_reflector_contact_order.py tests/test_laser_grounded_body_segment_replay_real_locks.py tests/test_laser_hitcap_prev_scale_replay_real_locks.py -q` -> 20 passed
- focused contact/replay lock batch before final report refresh:
  - `tests/test_item_reflector_contact_order.py`
  - `tests/test_shield_contacts.py`
  - `tests/test_combat_mutations_pass1.py`
  - `tests/test_laser_shield_contact_replay_real_locks.py`
  - `tests/test_guardreflect_laser_shieldhit_replay_real_locks.py`
  - `tests/test_body_contact_geometry_entry_hitbox_replay_real_locks.py`
  - `tests/test_laser_body_hurtcap_z_replay_real_locks.py`
  - `tests/test_reciprocal_body_hit_stale_hitlag_replay_real_locks.py`
  - result: 130 passed, 5 skipped
- `make test` -> 1658 passed, 1183 skipped
- `make validate-all` -> passed
- `make fmt-check` -> passed
- `git diff --check && git diff --cached --check` -> passed
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 180` -> no replay-level regressions

Final metrics from validation_report_diff:
- aggregate one-step: 6628 -> 6575
- strict: 8000 -> 7947
- primary one-step: 124 -> 118
- primary strict: 210 -> 204
- aggregate rollout first mismatch: 1416 -> 1415
- replay-level regressions: none

Largest replay-level movements are all improvements; no replay-level regression remained after the shield-radius narrowing.
