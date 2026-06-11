# Marth-related row triage on VictoriousSpitefulAlpaca (pre-burn)

State: uncommitted on `newchar` (pending review). Rollout first-breaks 55 -> 33; one-step
discrete 238 -> 225; median streak 231 -> 230. Fox/Falco validate-all byte-stable
("no suite total changes", no reds); full suite 3259 passed.

## Fixes retained (decomp/data-backed owner families)

1. opcode-52 x221C_u16_y event-floor on frame-preserving swaps (-22 rows; the dominant
   marth family: records 6352-6373, one episode).
   - Witness: air Shield Breaker End0 air->ground swap mid-script. The op52 "T" window
     [5,28) was re-asserted by the sim's stateless level query after the swap, while vanilla's
     ChangeMotionState (no Ft_MF_Unk24) clears fp->x221C_u16_y and only re-applies events the
     script crosses after entry.
   - Fix: move_tables_state_flags_221c_y_with_event_frame (level + source-event frame) + an
     x221c_y_event_floor lane set by the marth swap helper; the state_flags consumer suppresses
     levels whose source event predates the floor. Lane defaults 0: all other chars/paths
     unchanged.
   - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
2. Ledge-grab `cd->ecb.bottom.x` term (the Dolphin Slash under-lip sweetspot family;
   records 1860/2205 structurally fixed).
   - The decomp ledge AABB acceptance uses `cur_pos.x + cd->ecb.bottom.x` vs the ledge x - the
     POSED bottom-point x (displaced several units in flip poses like DS). The sim used raw
     cur_pos.x (bottom.x == 0, the desired_ecb approximation), so under-lip DS grabs never
     fired.
   - Fix: extract_ecb_bottom.py emits a parallel `<char>_bottom_x.bin` (the min-ty ECB joint's
     model-Z per frame); ecb_tables loads it optionally (absent => 0, old behavior); the
     mpcoll_env ledge gates use the worldized bottom-x per mpColl_80044164/800443C4.
   - Scope caveat: the generic ledge ECB bottom-X term is NOT globally solved by this change.
     Marth gets the source-backed data lane now; Fox/Falco bottom-X tables are intentionally
     not generated (enabling the source-correct term moves reviewed rows, e.g. the Yoshi
     platform-drop Pass grab manual-repro lock) and remain a documented future fox/falco
     ledge-geometry pass. The consumer code is generic; only the data is Marth-scoped.
   - refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044164,mpColl_800443C4}
3. Dolphin Slash cliffcatch cmd1 sequencing.
   - ftMs_Special(Air)Hi_Coll arms cmd_vars[1] on the first descending collision pass and only
     runs the cliffcatch-enabled wrapper from the second. Implemented via the special_cmd1 lane
     (armed pre-physics from start-of-frame vel.y, mirroring the previous frame's Coll view) and
     consumed by the ledge admission.
   - Also fixed: the check-in-3 marth ledge clause was dead code (it sat inside fox case labels
     that do not include 367/368; the numeric collision maps them to FX_AIR_LW_HIT/END).

## Marth-related rows remaining (boundary/float class, for the burn)

- 1859/2204 (368 -> 252 one frame early): DS ledge grab timing under rollout positional
  drift; the structural never-grab is fixed, the residue is apex-frame float drift.
- 6035 chain (6042/6044 over-grab in FallSpecial, 6079/2616 ref deaths): a one-frame DS air
  anim-end boundary at 6035 (out stays 368 one frame longer than ref) cascades into the
  failed-recovery episode. Needs witness-level anim-end boundary work; not fixed blind here.
- 6351 (op52 blip, out 0 ref 1): the single arming-frame boundary left from family 1.
- 8763 (Squat -> air DB1): vanilla performs Squat -> Pass -> air side-B in one frame
  (Squat_IASA special chain + same-frame platform drop). Marth-adjacent: the special is
  Marth's but the multi-transition frame is common Squat/Pass machinery.
- 3416 (hitstun 33 vs 34): marth-victim hitstun off-by-one - KB/hitstun rounding on one hit;
  could be marth weight path or staling; single row.

## Likely generic/common debt (deprioritized per scope)

- Guard family (1580, 5757, 6687, 6700): GuardOn/Guard/GuardSetOff blend timing - the same
  family as the fox MAJ burn's GuardOn-blend debt.
- Grab/catch flow (387, 879, 6788, 7162, 4317, 1629): catch/throw/damage-action selection
  around grabs; common combat/grab machinery.
- Platform drop (91, 3269 p1 falco SquatWait->Pass; 5072 p0 Wait->ref Fall): common
  squat/pass input parsing (same family as 8763's first transition).
- 5278/7954 (Fall -> ref 252): missed under-lip grabs from plain Fall, where bottom-x is
  ~0; generic ledge contact/obstruction geometry on the FD underside (affects fox too).
- p1 falco rows (1936, 2540, 4767, 7649): falco landing/jab/phantasm-victim debt.
- 2008 (Fall -> out damage): combat hit admission ref doesn't take; common combat.
- 3980/4209 (state_flags bytes 0): 2218-byte bits; common state-flag lanes.

## Validation

- make build BUILD_FORCE=1 / make fmt-check / diff checks: pass.
- Full make test: 3259 passed, 1183 skipped.
- Fox/Falco validate-all + report diff: no suite total changes, no regressions, no reds.
- Marth smoke: one-step 238 -> 225 / 1,079,562; rollout first-breaks 55 -> 33; median 230.
