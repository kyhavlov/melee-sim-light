# Source-Completion Progress

Durable tracker for source-first closure passes. Status vocabulary:

- **OPEN**: work remains.
- **INVENTORY NEEDED**: no source-completion inventory has been built yet.
- **CLOSED**: modeled and covered for the stated RL 1.0 scope.
- **RETAINED SOURCE-POLICY**: conditional behavior intentionally remains because source/data says it should.
- **BLOCKED**: only for exceptional evidence-backed source/data blockers.

| System | Status | Doc | Current Closure Level | Next Action |
|---|---|---|---|---|
| Collision | CLOSED | [collision.md](collision.md) | Fighter floor/wall/ceiling collision plus Randall/FoD moving floor publication are closed for supported RL 1.0 fighter gameplay on legal stages. | Reopen only from a source inventory delta or a validated trace that contradicts the closed owner model. |
| Scheduler | CLOSED | [scheduler.md](scheduler.md) | Frame order, fighter callback order, MotionState dispatch, scheduler-owned transient lifetime, and scheduler-visible RNG clocks are closed for supported RL 1.0 gameplay. | Reopen only from a source inventory delta or a validated phase-order trace that contradicts the closed scheduler spine. |
| Combat Hit Resolution | CLOSED | [combat_hit_resolution.md](combat_hit_resolution.md) | Fighter-vs-fighter BODY/SHIELD result consumption, ProcessHit aftermath, hitlists, stale/source identity, shield side effects, and the shared Fox/Falco laser item BODY zero-KB damage class are closed for supported RL 1.0 gameplay. | Reopen only from a source inventory delta or a validated hit-resolution trace that contradicts the closed combat owner model. |
| Grab / Throw / Capture | CLOSED | [grab_throw_capture.md](grab_throw_capture.md) | Catch/capture/pummel/throw/thrown-victim state machines, throw release/damage, attached-victim placement, breakout/mash, and retained seed-only hidden owner lanes are closed for supported RL 1.0 Fox/Falco gameplay. | Reopen only from a source inventory delta or a validated grab/throw/capture trace that contradicts the closed owner model. |
| Movescript Events | CLOSED | [movescript_events.md](movescript_events.md) | MSLFTSC1 extraction/schema/runtime loading, script event ordering, hitbox create/clear/damage cache products, script variables, throw flags/hitboxes/projectile pulses, state flags, and seed/reseed interaction are closed for supported RL 1.0 gameplay. | Reopen only from a source inventory delta or a validated script-event trace that contradicts the closed owner model. |
| Hitbox / Hurtbox Geometry | CLOSED | [hitbox_hurtbox_geometry.md](hitbox_hurtbox_geometry.md) | HitCapsule lifecycle/geometry, hurtcap init/state masks, body-part/source-pose provenance, and seed/reseed geometry reconstruction are closed or retained source-policy for supported RL 1.0 fighter gameplay. | Reopen only from a source inventory delta or a validated geometry trace that contradicts the closed owner model; promote hitbox size/interaction mutation if supported scripts begin emitting it. |
| Items Core | OPEN | [items_core.md](items_core.md) | INVENTORY NEEDED | Inventory item lifecycle, update order, ownership, item state publication, item hitlists, and lifecycle RNG. |
| Projectiles / Reflect | CLOSED | [projectiles_reflect.md](projectiles_reflect.md) | Fox/Falco laser article data, projectile spawn/update/collision, BODY/SHIELD contact, reflect/powershield transfer, shield bounce, hitlists, zero-KB/Falco-KB damage behavior, source identity, and seed/reseed reconstruction are closed or retained source-policy for supported RL 1.0 gameplay. | Reopen only from a source inventory delta or a validated projectile/reflect trace that contradicts the closed owner model; generic item lifecycle remains in items_core.md. |
| Input | OPEN | [input.md](input.md) | INVENTORY NEEDED | Inventory controller preprocessing, UCF/cardinals, analog/lightshield lanes, input latches, and action-input priority. |
| Match Flow | OPEN | [match_flow.md](match_flow.md) | INVENTORY NEEDED | Inventory entry/death/respawn, camera/magnify gameplay effects, stocks, match timers, teams, and public-state publication. |
| Data Extraction | OPEN | [data_extraction.md](data_extraction.md) | INVENTORY NEEDED | Inventory extracted table contracts, regeneration paths, schema/versioning, and generated-data ownership boundaries. |
