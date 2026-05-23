# Combat Hit Resolution Source Completion

Scope: fighter-vs-fighter hit resolution for supported RL 1.0 gameplay. This covers collision
result consumption, BODY and SHIELD admission/selection, `ProcessHit` aftermath, damage,
knockback, hitlag/hitstun entry, shield hit, hitlists, stale/source identity, and combat-owned RNG
lanes. The shared item/projectile boundary is included only where item/projectile contacts enter the
same combat damage/shield/hitlist substrate.

Out of scope for this closure level: detailed grab/capture/pummel lifecycle
([grab_throw_capture.md](grab_throw_capture.md)), full hitbox/hurtbox geometry and movescript event
lifecycle ([hitbox_hurtbox_geometry.md](hitbox_hurtbox_geometry.md),
[movescript_events.md](movescript_events.md)), detailed item/projectile lifecycle
([items_core.md](items_core.md), [projectiles_reflect.md](projectiles_reflect.md)), camera-only hit
effects, and cosmetic SFX exactness that does not consume gameplay RNG.

Status vocabulary: **OPEN**, **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED**
only. This document has no BLOCKED rows for the stated scope.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| Fighter pair collision result consumption | CLOSED | `combat_resolve` consumes pre-combat refreshed hitboxes, hurtcaps, shield descriptors, and hitlists in a decomp-shaped shield/BODY/clank pass. |
| BODY damage and `ProcessHit` aftermath | CLOSED | BODY producers build `HitCapsule.damage`/`getEnvDmg` products, log damage, select best KB, enter Damage*, apply hitlag/hitstun, write source, and update stale/combo state. |
| SHIELD contact and GuardSetOff aftermath | CLOSED | Shield contacts use ShieldDesc overlap, per-frame x19A0/x19A4-style accumulation, powershield suppression, GuardSetOff entry, shield HP/stun, and shield hitlag side effects. |
| Hitlists and stale/source identity | CLOSED | Runtime HitCapsule victim lists, same-group registration, ring/cooldown behavior, stale queue updates, and damage source writes are explicit source-shaped state. |
| Clank/rebound subset | RETAINED SOURCE-POLICY | Supported fighter hitbox-vs-hitbox clank/rebound uses the ftColl source thresholds and hitlist side effects; full non-fighter item clank belongs to item docs. |
| Combat RNG lanes | RETAINED SOURCE-POLICY | Gameplay-relevant combat RNG consumption is explicit at source sites; SFX-only or unsupported effect details remain outside RL 1.0 unless they affect RNG phase. |
| Item/projectile shared boundary | OPEN | Shared item/projectile contacts enter the same ProcessHit/shield/stale substrate, but Fox/Falco laser non-flinch classification still uses an extracted KB-triplet-derived proxy lane. Close this with an authoritative decomp/data signal in `projectiles_reflect.md` / item hit-resolution work before marking this row retained or closed. |
| Teacher-forced seed reconstruction | RETAINED SOURCE-POLICY | Reseed-only dense hitlist and hidden source lanes initialize real hidden source state for one-step/rollout reseed, then clear or require live provenance. |

## Source Phase Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Acceptance Locks |
|---|---|---|---|---|---|
| Fighter collision pair loop: `ftColl_80078C70` | `combat_resolve`, `combat_select_body_hits_one_mutating`, debug contact selectors | Active fighters, hitbox/hurtcap/shield descriptors, hitlists, pair order, stock/dead skip flags | Fixed loop over batch/player slots; primitive refresh runs immediately before combat | CLOSED | Structural order is locked by `tests/test_combat_hit_resolution_source_completion.py`. Replay/fixture locks cover contact filters, spurious hits, reciprocal hits, and debug pre-combat contracts. |
| Catch contact prepass: `ftColl_80078A2C` | `combat_select_catch_hits_one_mutating` | Catch hit element, HitCapsule victim list type 0, shield/capture exclusion state | Dedicated catch prepass before shield/BODY damage selection | RETAINED SOURCE-POLICY | Detailed grab/capture lifecycle belongs to `grab_throw_capture.md`; hit-resolution owns only the shared hitlist/admission ordering. Locks include catch/connect and guard/capture recharge tests. |
| ShieldDesc overlap: `lbColl_80007BCC`, `ftColl_80076CBC` | `shielddesc_geometry.h`, `shields_refresh`, `combat_mutations_pass1_future_apply_shield_hit` | ShieldDesc center/radius/scale, shield state flags, lightshield amount, HitCapsule damage, powershield flag | Runtime ShieldDesc lanes plus source comments and shield debug candidates | CLOSED | Shield hit path is source-shaped: shield before BODY, same-group registration, powershield x19A0 suppression, GuardSetOff entry. Locks include shield contact, GuardReflect, laser shield, powershield, and item shield recoil tests. |
| BODY hurt collision: `lbColl_8000805C -> lbColl_80006E58`, `ftColl_80076ED8` | BODY narrowphase in `combat_select_body_hits_one_mutating`; geometry helper calls | HitCapsule world sphere, hurtcap endpoints/radius, matrix overlap scalar, hurtbox state/no-damage/intangible state | Pre-combat hurtcap metadata/contact geometry plus source-shaped matrix/scaffold gates | CLOSED | Combat consumes geometry state; remaining pose extraction exactness belongs to `hitbox_hurtbox_geometry.md`. Locks include contact filter, combat geometry, guard tilt, tip-log/BODY boundary, and no-damage hitlag tests. |
| BODY damage producer and log: `ftColl_80076ED8`, `ftColl_8007891C`, `ftColl_8007A06C` | `combat_body_damage_producer_build`, `combat_body_damage_log_record`, `combat_body_damage_log_apply` | Staled HitCapsule damage, raw int damage, attacker/victim identity, damage log entries, best-KB selection | `MslCombatDamageProduct` and fixed per-defender scratch logs | CLOSED | Damage is accumulated before best-KB selection; stale/combo side effects use pre-combat attack/source snapshots. Locks include reciprocal BODY hit, stale owner, attack id snapshot, and ProcessHit source-clear tests. |
| `Fighter_ProcessHit_8006D1EC` damage consume | `combat_processhit_apply_resolved_damage`, `combat_processhit_consume` | x1838/x183C damage lanes, KB, hitlag, hitstun, source owner, percent temp, phantom timer | Explicit resolved event packet and ProcessHit cleanup pass in scheduler | CLOSED | Percent, KB, Damage* entry, hitlag/hitstun flags, source writes, and per-frame cleanup are packetized. Locks include damage followup, source clear, hitlag exit, and state-flag tests. |
| Knockback / hitlag / hitstun math: `ftCo_8008DCE0`, `ftCommon_CalcHitlag`, KB helpers | `combat_damage_calc_*`, `combat_damage_enter_state`, `combat_processhit_apply_hitlag_after_entry` | Common params, character params, percent/temp percent, stale damage, collision damage ratio, electric hitlag multiplier | Extracted common/character params and explicit damage event fields | CLOSED | The combat side owns math inputs and event ordering; detailed damage-state collision belongs to collision/knockdown systems. Locks cover KB percent gates, electric hitlag, DamageFlyRoll RNG, and hitlag source lanes. |
| Phantom/tip-log lane: `checkTipLog`, `inlineB1`, `ftColl_8007BE3C` | `combat_mutations_pass1_future_apply_body_phantom_hit`, `combat_processhit_apply_expired_phantom_damage` | HitCapsule `coll_distance`, x7A8 phantom threshold, x1898/x189C source/timer state | Explicit phantom pending damage/timer/source lanes | CLOSED | Phantom/tip-log contact is not treated as alternate full BODY damage. Locks cover shield-poke/tip-log boundaries and delayed phantom percent application. |
| Invincible/no-damage BODY contact | `combat_mutations_pass1_future_apply_body_hit_invincible` | Hurtbox no-damage/intangible state, attacker-side `dmg.x1914`, hitlag | Attacker-side hitlag-only path | CLOSED | Invincible BODY contact can give attacker hitlag without defender damage/KB. Locks cover combat hit-status ownership and no-damage contact cases. |
| HitCapsule same-group victim registration: `ftColl_80076808`, `lbColl_80008688`, `lbColl_80008820` | `hitlist.c`, `hitlist_register_fighter_group`, `hitlist_register_fighter_group_v2` | victims_1/victims_2 arrays, ring indices, insert type, rehit cooldown | `MslHitlistCapsule` with decomp-sized lists and type refresh policy | CLOSED | Runtime hitlist insert/copy/tick behavior is source-shaped. Locks cover ring determinism, death/respawn clear, per-hitbox seed gates, and BODY/SHIELD/clank group registration. |
| Hitbox copy/clear on create: `ftColl_800768A0`, `lbColl_CopyHitCapsule`, `lbColl_80008440` | Hitbox refresh / seed materialization owners in `hitboxes.c` and `hitlist.c` | Same-group copy, clear/create bands, script create/clear events, active HitCapsule state | Runtime hitlist capsules plus reseed-only dense/materialized seed state | RETAINED SOURCE-POLICY | Free-running runtime uses live capsules. Dense seed reconstruction is retained only for teacher-forced reseed where replay lacks per-HitCapsule lists. Locks cover AttackAir no-clear/post-clear, same-source carry, and dense seed negative cases. |
| Stale move queue: `plStale_UpdateStaleMovesFromFighter`, `ftColl_8007891C` | `staling_queue_update`, `staling_multiplier_for_move*` | Move id, attack instance, stale queue, source GObj identity | Attack id/instance lanes and pre-combat snapshots | CLOSED | BODY/throw/item producers use the appropriate source snapshot/exclusion policy. Locks include stale owner timing, AttackAirB continuation, throw laser stale, and item stale boundary tests. |
| Damage source clear / source identity: `Fighter_ProcessHit_8006D1EC`, `ftCommon_800804FC`, `Fighter_8006A360` source clear | `damage_source.h`, `combat_processhit_commit_source_owner`, source-clear seed lanes | Last-hit port, instance-hit-by, source-clear timers/phases, grounded no-KB clear | Explicit source-owner helpers and one-frame seed lanes | CLOSED | Source clear/write ownership is represented as state, not replay row logic. Locks include processhit pending clear, source port, terminal clear, and grounded damage clear tests. |
| Shield HP / GuardSetOff / GuardReflect side effects: `ftColl_80076CBC`, `ftCo_80092F2C`, `ftCo_80093240`, GuardReflect anim/coll paths | Shield hit application helpers, `shields.c`, `reflector_bubbles.c`, state flags | Shield HP, lightshield, x19A0/x19A4, GuardSetOff timers, reflector state flags | Explicit shield HP/timer/flag lanes | CLOSED | Fighter and item shield contacts share shield depletion/stun source shapes, with powershield and GuardReflect policies explicit. Locks cover GuardSetOff, GuardReflect x221C, shield HP recharge, and item shield recoil sign. |
| Clank/rebound: `ftColl_8007699C`, `inlineA0/inlineA1`, `ftCo_80099D9C` | Clank section in `combat_select_body_hits_one_mutating` | Hitbox-vs-hitbox overlap, clank flags, damage threshold, same-group registration, rebound flag | Once-per-unordered-pair clank packet with per-hitbox suppression and hitlag/rebound fields | RETAINED SOURCE-POLICY | Fighter clank behavior needed for RL 1.0 is modeled; full item-vs-item clank is item-core scope. The once-per-pair policy is documented and covered by combat/clank locks. |
| Combat RNG: electric clank SFX, DamageFlyRoll gate, random hit effects where gameplay-visible | `combat_rng_consume_*_site`, debug RNG traces, explicit RNG site ids | HSD RNG seed, source function/site, per-frame consume order | Source-site wrappers and seed-owned frame RNG policy | RETAINED SOURCE-POLICY | Gameplay RNG lanes are explicit. Cosmetic SFX-only exactness remains out of RL 1.0 unless it affects RNG phase. Locks include DamageFlyRoll RNG and electric clank site tests. |
| Item/projectile shared BODY and SHIELD boundary: `itcoll.c::it_80272460`, `ftColl_80077688`, item stale helpers | `combat_apply_item_body_hit`, `combat_apply_item_shield_hit`, item post-combat callbacks | Item owner, reflected damage, item hitbox damage, item attack id/instance, fighter victim state, authoritative no-flinch/item damage-class signal | Shared combat damage products and shield application helpers; MSLLASR1 `non_flinch` is currently derived from `kbg/wsk/bkb == 0` | OPEN | Shared shield/BODY/stale substrate is modeled, but laser non-flinch damage-class gating remains a documented proxy in `tools/extraction/extract_lasers.py`, `src/laser_params.h`, and `src/combat.c::combat_apply_item_hit`. Detailed item/projectile lifecycle belongs to item/projectile docs; this row should close only after the authoritative no-flinch owner is identified or the damage-class gate is proven from source. |
| Throw-release damage that uses shared hit-resolution substrate | `combat_apply_throw_release_body_hit`, throw damage product helpers | Throw hitbox damage, thrower source, stale exclusion, attached victim suppression | Shared combat damage product and resolved event path | RETAINED SOURCE-POLICY | Shared damage math is closed here; throw/capture lifecycle and release scheduling belong to `grab_throw_capture.md`. Locks include throw release, attached stale carry, and throw pulse tests. |

## Retained Source Policies

| Policy | Status | Why It Remains |
|---|---|---|
| Dense hitlist seed reconstruction | RETAINED SOURCE-POLICY | Slippi/replay rows do not expose decomp raw `HitVictim*` pointers or full per-HitCapsule victim lists. Reseed materialization initializes hidden HitCapsule state for the seeded frame and then requires live source proof or clears. |
| Geometry fallback boundaries | RETAINED SOURCE-POLICY | Combat owns result consumption. Exact live JObj/hurtcap/ShieldDesc pose extraction is tracked in `hitbox_hurtbox_geometry.md`; combat fallbacks are bounded and source-commented where pose data is missing. |
| Item/projectile shared-boundary only | OPEN | Item/projectile contacts can enter shared `ProcessHit`/shield/stale logic, but the Fox/Falco laser non-flinch damage-class gate still uses a KB-triplet-derived MSLLASR1 proxy. Item lifecycle, projectile ownership, reflection/bounce, item-only clank, and the authoritative no-flinch owner are separate system work. |
| Throw-release shared-boundary only | RETAINED SOURCE-POLICY | Throw-release damage can use shared hit resolution while grab/capture/pummel/throw state machines remain in `grab_throw_capture.md`. |
| Combat RNG source-site ownership | RETAINED SOURCE-POLICY | Exact RNG is modeled where combat gameplay consumes it; cosmetic effect exactness is intentionally outside this closure level unless it shifts gameplay RNG phase. |

## Runtime Ordering Map

| Runtime Boundary | Owner | Policy |
|---|---|---|
| Primitive refresh before combat | `fighter_callbacks_primitive_refresh_phase` | Refresh hitboxes, hurtcaps, and contact geometry after Anim/Phys/Coll and before item/fighter combat. |
| Hitlist tick / shield refresh | `fighter_callbacks_item_collision_and_combat_phase` | Tick HitCapsule victim cooldowns and refresh ShieldDesc/reflector bubbles before item and fighter combat. |
| Item collision before fighter combat | `items_update_collision_phase` | Item/projectile contacts feed shared combat lanes before fighter-vs-fighter `combat_resolve`. |
| Fighter-vs-fighter combat | `combat_resolve` | Resolve catch, clank, SHIELD, BODY, logs, ProcessHit-style damage, hitlists, stale, source, and hitlag/hitstun state. |
| ProcessHit cleanup | `combat_processhit_consume` in the next pre-input Anim phase | Apply delayed phantom damage, clear transient hitbox-touching-shield flags, and reset percent-temp accumulator. |
| Post-frame seed transients | `clear_seed_owned_transients_post_frame` | Reseed-only lanes are consumed within the frame unless a source-owned continuity rule keeps them. |

## Validation And Performance Evidence

Evidence from this closure pass:

- `make build BUILD_FORCE=1`: passed.
- `make validate-all`: passed.
- `make test`: passed.
- `make fmt-check`: passed.
- `git diff --check && git diff --cached --check`: passed.
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`: no hard reds and no unclassified reds.
- No hot combat runtime code changed in this pass, so no benchmark was required.

## Historical References

- `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8,ftColl_8007699C,ftColl_80076808,ftColl_800768A0,ftColl_8007A06C,ftColl_8007BE3C}`
- `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`
- `refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000805C,lbColl_80008688,lbColl_80008820}`
- `refs/melee/src/melee/it/itcoll.c::{it_80272460,ftColl_80077688 call paths}`
- Existing focused locks under `tests/test_*combat*`, `tests/test_*hitlist*`, `tests/test_*shield*`, and shared item/projectile boundary tests.
