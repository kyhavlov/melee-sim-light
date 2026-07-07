# Mario port plan (lane: mario)

Lane plan under `PARALLEL_PORTS.md`; methodology per `ADDING_A_CHARACTER.md`.
Drafted 2026-07-07 before Phase 0 — items marked VERIFY are to be pinned by
the lane agent from the decomp/ISO, not assumed.

## Shape of the work

`refs/melee/src/melee/ft/chara/ftMario/` has its own specials modules
(`ftMr_SpecialN/S/Hi/Lw.c`) — this is a real port, not a clone lane. The
payoff multiplier is Dr. Mario: `ftDrMario/` contains only Init + AppealS,
i.e. Doc reuses Mario's special code the way Ganon reuses falcon's. Land
Mario, then Doc is a registry row + data extraction + attr diffs + small
admission work (fold Doc into this lane as a fast follow-on once Mario's
Phase 5 stabilizes).

Specials inventory (new module `src/mario_specials.c`):
- Fireball (SpecialN): projectile ARTICLE with gravity/bounce. Substrate:
  the items system already carries fox/falco lasers and sheik needles;
  fireball adds a bouncing-projectile item class. VERIFY the item script /
  `itFireMario` decomp owner and lifetime/bounce params. This is the lane's
  only article and its main risk; do it LAST among the four specials.
- Cape (SpecialS): reflect behavior (item reflect + victim facing-flip).
  Substrate: reflector machinery exists (fox/falco shine ReflectDesc,
  `derive_item_reflect_damage_mul`); cape adds the facing-flip-on-fighter
  effect: VERIFY ftMr_SpecialS handling of fighters vs projectiles.
- Super Jump Punch (SpecialHi): multi-hit rising coin punch; freefall exit
  → LandingFallSpecial (add Mario rows to the origin lag/allow tables in
  `validation_buffer_common.py`, keyed on his attrs, and the api.c
  prev-action lag branch pattern).
- Mario Tornado (SpecialLw): mash-to-rise multi-hit; VERIFY the mash input
  counter mechanism in ftMr_SpecialLw (likely x2114-style press counter —
  the button-timer derivations in `tools/slippi/seed_history.py` may need a
  lane row) and the grounded/aerial split.

## Phase 0 — facts (VERIFY all)

- Registry: internal/external ids, `PlMr.dat`/`PlMrAJ.dat`, `ftDataMario`,
  decomp prefix `ftMr_`, submotion prefix `ftMr_SM_`, `has_articles=True`.
- Action-id blocks for the four specials + any Doc divergences (Doc's
  SpecialN is Megavitamins — same code, different article params: VERIFY
  whether the article differs beyond visuals/damage).
- Attr layout: build `MARIO_SPECIAL_ATTRS_LAYOUT` from `ftMr_*` dat_attrs
  reads (mirror the falcon 35-key recipe; VERIFY count).
- Fireball article: item kind id, spawn offsets, bounce restitution,
  lifetime, owner-hit interactions; where the existing item collision phase
  needs a new class vs reuses laser paths.

## Phases 1-5 (falcon recipe)

1. Registry row in the step-0 batch commit; PlMr extraction; build_data
   byte-identity gate; anim entry count VERIFY.
2. Replay intake: peppi scan of the user dump for the specials' MotionState
   ranges + fireball item presence in item slots (validation compares item
   lanes — curate replays WITH fireballs on screen). NEED FROM USER: dump
   with Mario (Doc games useful later too).
3. Common actions via coverage suite (expect wall-jump: Mario can walljump
   — `can_walljump` attr TRUE: VERIFY threshold attr).
4. Specials order: SpecialHi → SpecialLw → Cape → Fireball (article last;
   everything before it is fighter-only and unblocks burn-down early).
   Per-special unit tests as falcon did (grounded/aerial/edge cases).
5. Burn-down via the taxonomy script. Expect item-lane mismatches to
   dominate the tail (fireball timing/ownership) — budget accordingly.

## Gates

`ADDING_A_CHARACTER.md` Phase 7 per retained change; existing chars (now
including any lanes already landed) byte-stable; item-lane changes are
shared-fix-queue items (items code is shared with fox/falco/sheik).

## Progress log

- 2026-07-07: plan drafted; awaiting step-0 registration + replay dump.
