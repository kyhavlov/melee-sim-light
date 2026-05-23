# Collision System Source Completion

Scope: supported RL 1.0 fighter floor/wall/ceiling gameplay on legal stages, including
Yoshi's Story Randall and Fountain of Dreams moving floor publication. Runtime owners should stay
character-general unless the decomp callback or extracted fighter data is character-specific.

Out of scope for this closure level: item-only collision, camera/cosmetic stage effects, full
global Melee `mpLib` parity for unsupported line kinds, and non-legal-stage behavior unless it
feeds supported fighter floor/wall/ceiling state.

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only. This
document has no BLOCKED rows for the stated scope.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| Fighter floor/wall/ceiling source phases | CLOSED | Shared loaded-ECB, floor, wall, ceiling, final-publication, and reject/source-policy packets cover supported legal-stage fighter collision. |
| Stage topology and line metadata | CLOSED | Supported legal-stage line behavior is generated from `MSLSTG01`; stage distinctions are data-shaped, not stage-id proxies. |
| Moving stage-object floor publication | CLOSED | Randall and FoD platform transforms publish through generated stage metadata and runtime stage state. |
| Platform pass / floor skip | RETAINED SOURCE-POLICY | Soft-platform pass and floor-skip remain conditional because vanilla callbacks intentionally reject or skip floors. |
| Special callback policies | RETAINED SOURCE-POLICY | SpecialHi/JObj, SpecialLw platform-pass, ledge/cliff, locked desired ECB, and active Damage stay-airborne policies remain explicit source policies. |

## Source Phase Inventory

| Source Owner | Current MSL Owner | Required Source State | Status | Decision / Locks |
|---|---|---|---|---|
| `ft_80081B38`, `ft_80081C88`: fixed/JObj ECB load | `MslMpcollLoadedEcb`, SpecialHi JObj helpers, Damage hitlag ECB lanes | JObj/fixed ECB source, fighter scale, source joint, previous/current/desired ECB | CLOSED | Shared loaded-ECB packet prevents same-callback floor/wall/ceiling divergence. Locks cover SpecialHi ECB and Damage hitlag facing/side ECB cases. |
| `ft_80081D0C`, `ft_80082C74` -> `mpColl_800471F8`: common airborne wrappers | Generated `MSL_MPCOLL_PHASE_AIR_471F8`, `mpcoll_ground_apply`, `mpcoll_wall_ceil_apply` | CollData prev/current position, loaded ECB, floor skip, wall/ceil/floor indices, env flags | CLOSED | Generated MSLMSO01 source phases route common air/EscapeAir owners. Locks cover AttackAir, JumpAerial, EscapeAir, ledgedash, and platform cases. |
| `ft_80081DD4` Damage dispatcher -> `mpColl_800477E0` / `473CC` | Damage source phases, loaded ECB, active-hitlag floorhug, hitlag-exit floor owners | SDI-mutated position, loaded ECB bottom, bottom-sweep result, stay-airborne FloorPush/FloorHug state | RETAINED SOURCE-POLICY | Visible root-below-floor is not sufficient. Damage floor publication requires loaded-ECB bottom/source floor acceptance or source stay-airborne state. Locks: trace66/trace103 DamageAir, CDO negative, TVR/FSP positives, DamageFly hitlag-exit locks. |
| `mpColl_80044628_Floor`: bottom sweep producer | `MslMpcollFloorSweepResult`, bottom-sweep collectors | Previous/current loaded ECB bottom, floor skip, candidate floor, line metadata | CLOSED | `BOTTOM_SWEEP` is the only bottom-sweep proof. Root, endpoint, stage-object, and retry paths have distinct result modes. |
| `mpColl_80044838_Floor`, `mpColl_80044948_Floor`: projection/remap | Floor-result mode plumbing, edge snap/root projection/stay-airborne modes | Accepted floor, bottom/root projection point, endpoint fallback, source mode | CLOSED | Result modes distinguish `BOTTOM_SWEEP`, `ROOT_PROJECTION`, `EDGE_SNAP`, `STAGE_OBJECT_CARRY`, `DIRECT_PUBLICATION`, `STAY_AIRBORNE`, and `4A908`. Mode tests lock each supported mode. |
| `mpColl_8004A908_Floor`: disconnected-floor retry | Floor mode `4A908_RETRY` | Later retry candidate, disconnected-line policy, previous bottom/side midpoint | CLOSED | Modeled as explicit later source phase, not an unrelated helper. Locks cover BF/PS/FoD retry positives and seam negatives. |
| `mpColl_8004ACE4`: grounded ordered wall/ceiling/floor | `MslMpcollOrderedWallCeilResult`, grounded wall/ceil ordered pass, final floor packet | Grounded floor carry, wall/ceil/floor order, squeeze, env/contact flags | CLOSED | Ordered wall/ceil packets feed floor fallback, squeeze, and final env lifetime. Locks cover simultaneous wall/floor/ceiling and squeeze cases. |
| Wall producers/resolution: airborne and grounded left/right wall phases | `MslMpcollWallResult`, `mpcoll_wall_ceil.c` producers, packet commit | Loaded ECB side/bottom/top points, wall graph candidate, push/contact/env flags | CLOSED | Wall candidates are fixed-capacity and allocation-free; contact/env flags commit from wall packets. Locks cover airborne/grounded wall and wall/floor corner behavior. |
| Ceiling producer/resolution phases | `MslMpcollCeilingResult`, ceiling sweep/project helpers, packet commit | Loaded ECB top, ceiling graph candidate, adjacent wall packet, env flags | CLOSED | Ceiling uses the shared loaded ECB and commits edge/contact state through ceiling packets. Locks cover ceiling hits and ceiling/wall fallback. |
| Platform pass / floor skip (`mpUpdateFloorSkip`, `ftCo_8009A184`, platform-pass callbacks) | Generated platform-pass source phases, floor-skip lanes, transformed-platform predicates | Passable floor metadata, input down, floor skip segment, callback source phase | RETAINED SOURCE-POLICY | Soft-platform rejection and floor-skip are real source callback behavior. Locks cover FoD/Yoshi/Randall pass-through, Shine, AttackAir, EscapeAir, Fall, and FallSpecial cases. |
| `mpLib_8004DD90_Floor` and legal-stage graph helpers | `MSLSTG01` floor/wall/ceiling graphs, line metadata helpers, topology predicates | Endpoint links, active/fighter-solid policy, material/flags, platform/ledge/slope transforms | CLOSED | Supported legal-stage graph behavior is table-backed. Exact global `mpLib` behavior outside supported legal-stage fighter collision is out of RL 1.0 scope. |
| MotionState collision callback selection | `MSLMSO01`, `msl_motion_state_class_bits`, source phase bits | Callback owner identity and action-family class bits | CLOSED | Source phase routing is generated. New callback distinctions should extend `MSLMSO01`, not local action-id lists. |

## Moving Stage-Object Publication

| Source/Data Owner | Current MSL Owner | Required Source State | Status | Decision / Locks |
|---|---|---|---|---|
| Yoshi's Story Randall: `grStory_801E3370 -> grStory_801E33E0 -> Ground_801C2FE0` | `stage_collision_floor_line_world`, `stage_collision_get_randall_position`, `stage_collision_floor_line_motion_delta` | Post-start frame clock, generated line 1000, path frame, transformed endpoints | CLOSED | Runtime collision, support carry, and debug/viewer stage-state consume `data/stages/bin/grst.bin::MSLSTG01 platform_path`. Locks include Randall ride, blaster, downed, and `test_runtime_randall_stage_state_uses_generated_path_clock`. |
| Randall public ground id | `compare_public_ground_id` | Internal generated floor id and replay-visible raw line id | RETAINED SOURCE-POLICY | Runtime uses generated line 1000 while public compare maps back to raw Yoshi line 0. This matches replay publication rather than gameplay CollData. |
| FoD side-platform JObj heights: `grIzumi_801CCBDC/grIzumi_801CC358 -> mpLib_80055E9C` | `stage_collision_floor_line_world`, `stage_collision_floor_line_height_platform_state_is_source_trusted`, `MslFodHeightPlatformLineState` | Platform id, height, velocity, scheduler phase/timer, source bits, named source poses, hidden-return timer, transform coefficient | CLOSED | FoD current-source authority flows through one stage-collision packet. Locks cover scheduler/current-source/deferred-velocity/hidden-return and platform carry. |
| FoD hidden target / hidden wait | `stage_collision_floor_line_world` | Hidden target height, live velocity, scheduler hidden-return state | RETAINED SOURCE-POLICY | The moving frame that reaches hidden target remains visible; hidden-wait parks the collision line below main floor. This is source phase behavior from `grIzumi_801CC358`. |
| Moving-platform support carry | `stage_collision_floor_line_motion_delta`, `mpcoll_ground.c` stage-object carry mode | Current internal floor id, transformed line, current/next path or grIzumi velocity, callback source phase | CLOSED | Grounded/current-floor callbacks inherit Randall/FoD motion through generated stage metadata. Locks cover Landing, Blaster, downed/passive, FoD prefix velocity, and same-step deferred velocity. |
| Trace/viewer stage-state export | `debug_write_stage_state`, trace viewer stage fields | Same runtime platform state used by collision | CLOSED | Debug/viewer stage-state is publication of runtime stage truth, not a separate gameplay path. |

## Retained Reject / Suppression Policies

Each retained floor/wall/ceiling reject bit is classified as source policy, not a replay bridge.
They enforce vanilla callback preconditions: accepted bottom sweep, platform-pass/floor skip,
locked desired ECB, stage-object current-source provenance, SpecialHi JObj source phases, Cliff
ledge policy, or active Damage stay-airborne.

| Policy Family | Status | Why It Remains |
|---|---|---|
| EscapeAir transformed remap and locked desired ECB | RETAINED SOURCE-POLICY | EscapeAir may preserve desired ECB/floor ownership, but publication still requires the source callback bottom/root owner. |
| SpecialHi / SpecialAirHi platform, under-stage, floor-angle, and JObj cases | RETAINED SOURCE-POLICY | SpecialHi has callback/JObj-specific collision behavior distinct from ordinary floor publication. |
| Fall/FallSpecial same-floor and platform first-sustained cases | RETAINED SOURCE-POLICY | Platform-pass and bottom-sweep preconditions are real source callback gates. |
| AttackAir transformed-platform, floor-skip, hard-slope, and endpoint cases | RETAINED SOURCE-POLICY | AttackAir publication is gated by the accepted bottom/edge source result and script/callback phase. |
| JumpAerial transformed/static-platform cases | RETAINED SOURCE-POLICY | JumpAerial platform-pass behavior requires current source contact/root crossing, not stale carried platform state. |
| Damage active-hitlag root/bottom and downward-SDI stay-airborne | RETAINED SOURCE-POLICY | FloorPush/FloorHug and loaded-ECB bottom acceptance are source mechanics. |
| Cliff/ledge floor suppression | RETAINED SOURCE-POLICY | Cliff/ledge owners suppress floor publication until source ledge handoff permits it. |
| SpecialAirLw startup stale platform | RETAINED SOURCE-POLICY | Reflector startup cannot publish a stale previous platform before current callback ECB bottom accepts it. |

No retained bit is documented as an approximation. Future changes should update this table when a
source policy is deleted, split, or newly modeled.

## Env / Contact Flag Lifetime

- Frame-start clearing remains direct and source-shaped.
- Floor env/contact bits commit through floor result publication or documented no-floor edge
  suppression source paths.
- Wall env/contact bits commit through `MslMpcollWallResult`.
- Ceiling env/contact bits commit through `MslMpcollCeilingResult`.
- Remaining direct env writes are frame-start clearing or named source-policy writes.

## Hitlag / Frozen CollData Lifetime Audit

This audit was added after `modelplay_selfplay_bfloor_5006m_local/trace_seed103` exposed a live
Damage hitlag continuation case that the previous closure table did not force: a first frozen
Damage map callback produced floor contact while staying airborne, and a later SDI hitlag callback
needed that runtime-produced CollData floor/contact provenance.

| Source State / Lifetime | Source Write Phase | Source Read Phase | Source Clear Phase | Current MSL Representation | Status / Locks |
|---|---|---|---|---|---|
| Active Damage hitlag SDI position | `ftCo_Damage_OnEveryHitlag` mutates `fp->cur_pos` when `allow_sdi` and x670/x671 stick windows admit SDI. | `Fighter_procMap -> ftCo_Damage_Coll -> ft_80081DD4` copies `fp->cur_pos` into `coll_data.cur_pos`. | Hitlag ends when fighter clears `allow_sdi` in `fighter.c`; ordinary frame update proceeds without OnEveryHitlag SDI. | Existing hitlag input/tilt timer lanes and active Damage SDI helpers. | CLOSED. Locks cover seed25/seed26/trace66/trace103 DamageAir floorhug windows. |
| Active Damage hitlag loaded ECB envelope | `mpColl_LoadECB_inline` / `mpCollInterpolateECB` inside `mpColl_800477E0` loads callback-current `CollData.ecb/prev_ecb/desired_ecb`. | Later floor/wall/ceil producers consume the loaded ECB in the same map callback; MSL also carries explicit hidden ECB for teacher-forced one-step/reseed rows. | Non-Damage/non-hitlag callbacks stop carrying the Damage hitlag ECB; `reseed_seed` clears then explicitly initializes only seed-backed ECB. | `coll_damage_hitlag_ecb_valid` plus current/prev/desired CollData ECB SoA lanes. | CLOSED. Locks include Damage hitlag facing/side ECB and negative rows where seed ECB alone is not floor authority. |
| Active Damage hitlag runtime floor/contact provenance | `mpColl_80044628_Floor` writes `coll->contact`, `coll->floor.index/flags/normal`, and FloorPush/FloorHug bits; `mpColl_80044948_Floor` may project while `CollisionFlagAir_StayAirborne` keeps `GA_Air`. | Later frozen Damage map callbacks consume callback-current `CollData.floor/contact/ecb` after `ftCo_Damage_OnEveryHitlag` moves the root again. | `inline0` clears `env_flags` each map callback, but floor/contact/ECB remain callback-current CollData. MSL clears the runtime authority on non-hitlag, non-Damage, and reseed boundaries. | `coll_damage_hitlag_floor_contact_runtime`, runtime-only and never initialized from teacher-forced seed rows. | CLOSED. Positive: trace103 multi-row carry. Negative: HIS seed-authority guard proves seed ECB does not become runtime floor contact. |
| DamageFly/DamageFall hitlag-exit floor publication | `Fighter_8006D10C` runs `post_hitlag_cb`; Damage/Fly collision then reaches `ft_80081DD4`. | Hitlag-exit callbacks consume Damage-specific floorhug latch or exact bottom-sweep result. | Latch is cleared on landing/publication or when Damage hitlag owner no longer applies. | `damage_hitlag_floorhug_latch` and source result modes. | RETAINED SOURCE-POLICY. DamageAir active-hitlag floorhug intentionally does not seed broad hitlag-exit landing authority. |
| GuardSetOff / GuardReflect hitlag SDI | `ftCo_80093240` mutates grounded position along `coll_data.floor.normal`; GuardSetOff collision dispatches through `ft_800845B4`/`ft_80084104`, not Damage stay-airborne `mpColl_800477E0`. | Guard callbacks consume grounded floor normal and shield state; they do not require active Damage airborne floor-contact carry. | Fighter hitlag exit clears callbacks via `Fighter_8006D10C` / motion-state callback reset. | Guard/shield lifecycle lanes, shielddesc state, and grounded collision owners. | CLOSED for collision lifetime; detailed shield combat ownership is tracked under Combat Hit Resolution. |
| Throw-release / item-hit hitlag into Damage | Throw/item combat enters Damage/Fly and may install hitlag callbacks. | Shared Damage collision path consumes `ft_80081DD4`; thrown-release floor owner remains a separate bounded source policy because it is not OnEveryHitlag SDI-produced contact. | Damage action/hitlag exit clears the owner. | `active_damage_thrown_release_floor_owner` plus Damage hitlag latch policy. | RETAINED SOURCE-POLICY. Locks cover throw-release floorhug/landing boundaries. |
| Floor/wall/ceiling env flags | `inline0`/`inline1` copy `env_flags` to `prev_env_flags` and clear current flags before each mpColl wrapper; producers then set Floor/Wall/Ceiling push/hug bits. | Motion callbacks inspect `coll_data.env_flags` immediately after their map callback. | Next mpColl wrapper invocation clears current env flags. | `coll_env_flags`, floor/wall/ceil result packets, and final commit helpers. | CLOSED. The Damage runtime-contact lane carries floor/contact provenance, not stale env flags. |
| Floor skip / platform pass | `mpUpdateFloorSkip` and platform-pass callbacks update floor skip and callback filter state. | Floor producers reject skipped/passable floors according to source callback policy. | Source callback changes or platform-pass end clear/replace the skip. | `floor_skip_segment_id`, generated phase bits, platform-pass lanes. | RETAINED SOURCE-POLICY. |
| Ledge/cliff handoff | `mpColl_80046904` writes ledge bits; `ftCliffCommon_*` consumes them and owns cliff floor/ledge state. | Cliff actions and post-collision ledge catch consume ledge side/floor owner. | Cliff exit/drop/release sets cooldown/skip and clears ledge side as source does. | Ledge/cliff owner lanes in `ledge.c` plus collision env bits. | RETAINED SOURCE-POLICY. |
| Reseed vs live runtime authority | Replay seed rows initialize explicit hidden state only when a source lane is present. | One-step reseeds consume seed-backed hidden ECB/owner lanes; free-running runtime consumes live callback-produced state. | `reseed_seed` clears runtime-only state before applying explicit seed lanes. | Seed lanes for ECB/owners; runtime-only `coll_damage_hitlag_floor_contact_runtime` for live Damage contact. | CLOSED. This prevents replay-visible hidden ECB from becoming floor-contact authority without a live mpColl floor producer. |

## Validation And Performance Evidence

Evidence from the closure passes:

- `make build_data`: passed when generated metadata changed.
- `make build BUILD_FORCE=1`: passed.
- `make validate-all`: passed.
- `make test`: passed.
- `make fmt-check`: passed.
- `git diff --check && git diff --cached --check`: passed.
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`: no hard reds and no unclassified reds for retained collision packets; moving-platform closure reported no suite movement.
- Clean-vs-dirty light moving-platform benchmark, same command/env:
  - clean HEAD: `2574.000 ns/env_step`
  - dirty moving-platform packet: `2562.897 ns/env_step`

## Historical References

These ignored triage files were consolidated here and are references only:

- `reports/triage/fighter_collision_source_completion_inventory.md`
- `reports/triage/mpcoll_source_completion_inventory.md`
- `reports/triage/moving_platform_publication_inventory.md`
