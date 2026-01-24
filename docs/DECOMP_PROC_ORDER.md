# GALE01 Fighter Proc Ordering (Update + Collision)

This document is a decomp-backed reference for **when** GALE01 runs the major fighter update/collision phases, based on:
- where fighter procs are registered (priority numbers), and
- how the HSD `GObj` proc scheduler executes those priorities.

The focus here is the ordering that matters for “translation timing” (pre/post physics) and for fighter-vs-fighter collision.

## How proc priority implies execution order (HSD/GObj)

Fighter procs are registered via `HSD_GObjProc_8038FD54(gobj, func, pri)`, which stores the priority in `gproc->s_link`.  
See `refs/melee/src/sysdolphin/baselib/gobjproc.c:150` (`HSD_GObjProc_8038FD54`). In particular, `gproc->s_link = pri;` at `refs/melee/src/sysdolphin/baselib/gobjproc.c:160`.

The proc runner loops priorities **from low to high**:
`for (i = 0; i <= ...gproc_pri_max; i++) { ... proc = HSD_GObj_804D7840[i]; ... proc->on_invoke(...) }`  
See `refs/melee/src/sysdolphin/baselib/gobj.c:88` (`HSD_GObj_80390CFC`), especially `refs/melee/src/sysdolphin/baselib/gobj.c:101-115`.

So: **smaller priority executes earlier in the frame** (globally), and the fighter’s own proc list is a set of entries into that
global priority schedule.

## Where GALE01 registers fighter procs + priorities

### Normal fighter creation path (`Fighter_Create`)

The main registration table is in `refs/melee/src/melee/ft/fighter.c:857` (`Fighter_Create`), where `HSD_GObjProc_8038FD54` is called
repeatedly with fixed priority numbers (`refs/melee/src/melee/ft/fighter.c:903-917`).

| Priority | Proc (function) | Key calls / role (high-level) |
|---:|---|---|
| 0 | `Fighter_8006A1BC` | hitlag + misc timers decrement, hitlag end handling (`refs/melee/src/melee/ft/fighter.c:1402`) |
| 1 | `Fighter_8006A360` | sets `pos_delta`, copies `prev_pos = cur_pos`, advances anim, calls `anim_cb` (`refs/melee/src/melee/ft/fighter.c:1453`, `:1682-1705`) |
| 2 | `Fighter_8006ABA0` | pad-related helper (`ftCo_800B3900`) gating on `ftCo_800A2040` (`refs/melee/src/melee/ft/fighter.c:1712`) |
| 3 | `Fighter_Spaghetti_8006AD10` | reads pad (`HSD_PadGameStatus[...]`), then calls motion-state `input_cb` (`refs/melee/src/melee/ft/fighter.c:1785`, `:1827-1844`, `:2109-2119`) |
| 4 | `Fighter_procUpdate` | calls motion-state `phys_cb`, updates/integrates `fp->cur_pos` (and several timers) (`refs/melee/src/melee/ft/fighter.c:2132`, `:2147-2159`, `:2333-2347`) |
| 6 | `Fighter_procMap` | sets model translate from `cur_pos`, then calls motion-state `coll_cb` (map/stage collision) (`refs/melee/src/melee/ft/fighter.c:2459`, `:2473-2478`) |
| 7 | `Fighter_8006C5F4` | calls `ft_80089B08` (`refs/melee/src/melee/ft/fighter.c:2501`) |
| 8 | `Fighter_CallAcessoryCallbacks_8006C624` | accessory callbacks that may also call `HSD_JObjSetTranslate(...,&cur_pos)` (`refs/melee/src/melee/ft/fighter.c:2509`, `:2523-2531`) |
| 9 | `Fighter_8006C80C` | updates hit capsule world endpoints (per-bone), plus other collision-related helpers (`refs/melee/src/melee/ft/fighter.c:2535`, `:2549-2552`) |
| 12 | `Fighter_UnkProcessGrab_8006CA5C` | grab/item grab collision pass (`refs/melee/src/melee/ft/fighter.c:2572`) |
| 13 | `Fighter_8006CB94` | fighter-vs-fighter collision pass (`ftColl_80078C70`), plus several followups (`refs/melee/src/melee/ft/fighter.c:2604`, `:2610-2621`) |
| 14 | `Fighter_ProcessHit_8006D1EC` | processes/consumes collision results (damage/shield/hitlag) and clears flags like `x221C_b5` (`refs/melee/src/melee/ft/fighter.c:2793`, `:3014`) |
| 16 | `Fighter_8006D9AC` | calls `ftCo_8009E0A8` (post-hit processing) (`refs/melee/src/melee/ft/fighter.c:3037`) |
| 18 | `Fighter_UnkCallCameraCallback_8006D9EC` | camera callback (`refs/melee/src/melee/ft/fighter.c:3048`) |
| 22 | `Fighter_8006DA4C` | writes player-facing + position to player system (`refs/melee/src/melee/ft/fighter.c:3060`) |

### Demo creation path (`ftdemo.c`)

`refs/melee/src/melee/ft/ftdemo.c:115` registers a *subset* of procs with slightly different priorities (notably `Fighter_procMap` at `5`),
so it should not be used as the canonical “normal match” ordering. (`refs/melee/src/melee/ft/ftdemo.c:115-119`)

## Decomp-proven ordering for the “timing controversy” items

Below is the **effective per-fighter ordering** implied by the priority schedule above (smaller pri runs earlier), using concrete
call sites.

### 1) Timers decrement

There is not a single “timers proc”; timers decrement in multiple procs:
- Hitlag + related timers decrement at priority `0` (`Fighter_8006A1BC`), e.g. `dmg.x195c_hitlag_frames -= 1.0f`.  
  See `refs/melee/src/melee/ft/fighter.c:1402` (`Fighter_8006A1BC`), e.g. `:1422-1434`.
- `prev_pos` bookkeeping happens at priority `1` (`Fighter_8006A360`) via `prev_pos = cur_pos` before physics runs.  
  See `refs/melee/src/melee/ft/fighter.c:1458-1463`.
- Some “movement/interaction” timers decrement inside the physics proc (priority `4`), e.g. `x2064_ledgeCooldown--`, `capture_timer--`.  
  See `refs/melee/src/melee/ft/fighter.c:2147-2153` (`Fighter_procUpdate`).

### 2) Input processing

Input is processed in priority `3` (`Fighter_Spaghetti_8006AD10`):
- It reads pad state from `HSD_PadGameStatus[...]`. See `refs/melee/src/melee/ft/fighter.c:1827-1844`.
- It then calls the motion-state input callback `fp->input_cb(gobj)`. See `refs/melee/src/melee/ft/fighter.c:2117-2119`.

### 3) Animation advance + motion-state `anim_cb`

Animation advance occurs in priority `1` (`Fighter_8006A360`):
- `ftAnim_8006EBA4(gobj)` is called at `refs/melee/src/melee/ft/fighter.c:1693`.
- `fp->anim_cb(gobj)` (motion-state anim callback) is called after that at `refs/melee/src/melee/ft/fighter.c:1702-1704`.

### 4) Physics/integration updating `fp->cur_pos`

Physics/integration happens in priority `4` (`Fighter_procUpdate`):
- The motion-state physics callback runs near the start: `fp->phys_cb(gobj)`. See `refs/melee/src/melee/ft/fighter.c:2157-2159`.
- `fp->cur_pos` is updated (integration) directly inside this proc via `PSVECAdd(...,&fp->cur_pos)` and direct component adds.  
  See `refs/melee/src/melee/ft/fighter.c:2333-2347` (and surrounding).

### 5) Shield bubble + hit capsule world position updates

Two separate mechanisms exist:

**Hit capsule endpoints (hitboxes)** are updated in priority `9`:
- `Fighter_8006C80C` calls `ftColl_8007AE80(gobj)` at `refs/melee/src/melee/ft/fighter.c:2549`.
- `ftColl_8007AE80` iterates `fp->x914[]` and updates each capsule from its bone via `ftColl_8007AD18`.  
  See `refs/melee/src/melee/ft/ftcoll.c:1264-1271` (`ftColl_8007AE80`).

**Shield bubble world position** is updated lazily inside the collision check:
- `lbColl_80007BCC` updates `shield_hit->pos` from `shield_hit->bone` when `skip_update_pos` is false, then sets it true.  
  See `refs/melee/src/melee/lb/lbcollision.c:1510` (`lbColl_80007BCC`), especially `:1519-1525`.
- The per-frame reset of that `skip_update_pos` is motion-state dependent (e.g. many guard/wait states call `ftColl_8007AEE0`).  
  The reset function itself is `refs/melee/src/melee/ft/ftcoll.c:1273` (`ftColl_8007AEE0`).

### 6) Fighter-vs-fighter collision (incl. shield collision `ftColl_80076CBC`)

The fighter-vs-fighter collision pass is in priority `13`:
- `Fighter_8006CB94` calls `ftColl_80078C70(gobj)` at `refs/melee/src/melee/ft/fighter.c:2611`.
- `ftColl_80078C70` contains the shield overlap check and calls `ftColl_80076CBC(...)` for non-`HitElement_Inert` shield hits.  
  See `refs/melee/src/melee/ft/ftcoll.c:1004` (`ftColl_80078C70`), and the relevant shield branch at `refs/melee/src/melee/ft/ftcoll.c:1098-1104`.
- `ftColl_80076CBC` itself is defined at `refs/melee/src/melee/ft/ftcoll.c:434` and mutates shield/damage bookkeeping. (`refs/melee/src/melee/ft/ftcoll.c:434-494`)

### 7) When `x221C_b5` is set/cleared

`x221C_b5` is set during the priority `13` collision pass, specifically in the shield-branch of `ftColl_80078C70` when the hit capsule’s
element is `HitElement_Inert`:
- `victim_fp->x221C_b5 = true;` at `refs/melee/src/melee/ft/ftcoll.c:1102`.

It is then cleared later in the same frame by the priority `14` post-collision consumer (`Fighter_ProcessHit_8006D1EC`):
- `fp->x221C_b5 = 0;` at `refs/melee/src/melee/ft/fighter.c:3014`.

## Implications for sim step order (translation timing)

### Proven “translation” ordering around collision

The decomp-backed ordering relevant to “does collision use pre- or post-physics translation?” is:
1) **Anim pose timebase** advances at priority `1` (`ftAnim_8006EBA4`), before physics. (`refs/melee/src/melee/ft/fighter.c:1693`)
2) **Input callback** runs at priority `3`, still before physics. (`refs/melee/src/melee/ft/fighter.c:2117-2119`)
3) **Physics/integration** mutates `fp->cur_pos` at priority `4`. (`refs/melee/src/melee/ft/fighter.c:2333-2347`)
4) **Model root translate** is then set from `fp->cur_pos` at priority `6` via `HSD_JObjSetTranslate(...,&fp->cur_pos)`. (`refs/melee/src/melee/ft/fighter.c:2473`)
5) **Hit capsule endpoints** update from bones at priority `9` (`ftColl_8007AE80`). (`refs/melee/src/melee/ft/fighter.c:2549`)
6) **Fighter-vs-fighter collision** (including shield overlap) runs at priority `13` (`ftColl_80078C70`). (`refs/melee/src/melee/ft/fighter.c:2611`)

Taken together: fighter-vs-fighter collision uses **post-integration** translation (the `fp->cur_pos` *after* `Fighter_procUpdate`),
because the pipeline updates `fp->cur_pos` (pri `4`) *before* it sets the model translation (pri `6`) and refreshes hit capsule world
endpoints (pri `9`), all of which precede collision (pri `13`).

Additionally, because the scheduler iterates priorities globally (`for (i = 0; i <= ...; i++)`), **all fighters’** pri `4` procs run
before **any** pri `6` procs, etc. (`refs/melee/src/sysdolphin/baselib/gobj.c:101-115`). So by the time any fighter enters the pri `13`
collision pass, the whole roster has already completed pri `4` integration and pri `6` translation update (subject to their per-proc
gates like `x221F_b3` / hitlag checks).

### What this says about the “(prev_pos - pos) translation shift” workaround

In GALE01, `prev_pos` is updated to `cur_pos` at priority `1` (before integration) (`refs/melee/src/melee/ft/fighter.c:1458-1463`), while
collision uses the post-integration `cur_pos` (see ordering above).

So a workaround that shifts already-computed world primitives by `(prev_pos - cur_pos)` is (mechanically) shifting them back toward the
**pre-physics** translation. That behavior is **not** supported by the proc ordering above as “what GALE01 collision uses”; it is more
consistent with compensating for a simulator ordering mismatch (e.g. if the sim is colliding using the wrong frame’s translation).

## Potential doc corrections (not applied here)

These are “likely wrong vs decomp” items found while assembling the references above:
- `SPEC.md` currently states that `0x221C` bit `0x04` / `x221C_b5` is set “when we resolve a shield contact”. However, decomp shows
  `x221C_b5` being set specifically on the `HitElement_Inert` shield-overlap path (`refs/melee/src/melee/ft/ftcoll.c:1098-1104`) and then
  cleared in `Fighter_ProcessHit_8006D1EC` (`refs/melee/src/melee/ft/fighter.c:3014`). This note is specifically about **our sim’s**
  `state_flags` byte-3 / `0x221C` index mapping (i.e., “which bit we label as `x221C_b5`”), so readers don’t confuse GALE01’s
  `x221C_b5` behavior with our current generic “touching shield bubble” bit usage.
- `SPEC.md` describes an “ordering workaround” that shifts primitives back to a pre-physics translation using `(prev_pos - pos)`. The proc
  ordering here indicates collision should be evaluated at post-physics translation (pri `4`→`6`→`9`→`13`), so that workaround is not a
  decomp-backed rule.
