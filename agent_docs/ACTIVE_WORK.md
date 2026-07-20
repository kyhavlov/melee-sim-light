# Active performance packet — fighter contact empty-owner cull

## Objective

Delete always-scheduled fighter contact passes when their exact source-owned contact class has no
live producer or receiver in the bound Match. Preserve the complete authored hitbox/hurtbox,
shield, reflect, absorb, grab, item, wind, and damage narrow phases whenever a compatible owner is
present.

## Final boundary

- **Final owner:** `Fighter_8006CB94` owns source-order admission to its eight `ftColl` contact
  classes; each retained cull is expressed from canonical live collision descriptors, not action or
  character ids.
- **Canonical state:** fighter/item hitboxes, hurtboxes/BODY state, shield/reflect/absorb/grab
  descriptors, DmgLog fields, GObj entity lists, and source scheduler order remain the only mutable
  and save-state-visible combat authority.
- **Consumers:** hit/no-hit and target selection, shield/reflect/absorb contact, grab/item contact,
  damage/hitlag/hitstun/knockback, stale queue, callbacks, and source writeback keep the exact source
  narrow phases and tie order.
- **Displaced work:** a contact-class pass does not enumerate geometry or targets when its required
  source producer/receiver set is empty. The final packet may delete multiple empty classes but may
  not merge their distinct source semantics.
- **Deletion boundary:** one conservative admission predicate per source contact class adjacent to
  `Fighter_8006CB94`; admitted cases enter the unmodified source call. There is no generic radius,
  cached broad-phase state, replay/action/character list, changed ordering, approximation, or
  replacement combat representation.

## Source and profile evidence

- The corrected 512 profile assigns 2.80% to always-scheduled `Fighter_8006CB94`, 2.21% to
  `Fighter_ProcessHit_8006D1EC`, and 0.65% to contact publication. `ProcessHit` is mostly idle
  bookkeeping; detection admission is the cleaner deletion boundary.
- `Fighter_8006CB94` invokes eight distinct collision owners for every active fighter. Competitive
  frames commonly have no live grab, reflect/absorb, item-contact, wind, or compatible hit owner,
  but admission must be read from their real descriptors/lists.
- The retained wall broad phase proves that conservative empty-set rejection can remove most
  collision enumeration while leaving exact source narrow phase and order untouched.

## Acceptance

- Attribute exclusive cost and call population first, then add only source-backed predicates whose
  empty/entered census shows material work deletion. Temporary instrumentation must be removed.
- Both digests and the complete replay/API/copy/save-restore/allocation/PPC/Wasm/viewer gates remain
  unchanged. No new Match/shared state, gameplay allocation, or widened validation classification.
- Retain only a repeatable whole-frame gain at resident 512 and 256; reject predicates that merely
  move scans or add hot admission cost.

## Log

- 2026-07-19 — `open`
  Scope: the eight contact-class calls inside `Fighter_8006CB94`.
  Hypothesis: one or more uncommon contact classes still enumerate empty producer/receiver sets on
  most of the 166,272 scheduled fighter visits.
  Evidence: the enclosing owner is 2.80% of the complete contract, but current profiling does not
  separate its source calls or empty-set population.
  Disposition: use opt-in exclusive function attribution plus source descriptor census; do not alter
  production scheduling until a specific class and predicate are proven.
  Next: instrument `ftcoll.c` under the diagnostic build, resolve exclusive cycles/calls for all
  eight entries, then inspect the dominant source owner's real producer/receiver authority.

- 2026-07-19 — `open`
  Scope: exclusive `ftcoll.c` attribution for the priority-13 contact owner.
  Hypothesis: a specific empty producer scan dominates the enclosing callback.
  Evidence: `ftColl_80078C70` alone accounts for 143.2M exclusive diagnostic cycles (3.40% of the
  instrumented contract) across 143,822 entries. The next priority-13 class is
  `ftColl_8007925C` at 23.9M (0.57%). `ftColl_80078C70` enumerates every fighter's four authored
  hit capsules before any hit/shield/hurt/clank path can proceed.
  Disposition: remove the function instrumentation and add one conservative live-producer scan at
  this exact owner. Do not touch the seven minor contact classes in this packet.
  Next: return immediately when every fighter hit capsule is disabled, preserve the complete source
  body otherwise, then measure exact 512/256 output.

- 2026-07-19 — `retained`
  Scope: per-attacker empty hit-capsule admission inside `ftColl_80078C70`.
  Hypothesis: rejecting an attacker before team/throw/clank/hurt enumeration avoids the dominant
  empty source path without rescanning the whole Match on active frames.
  Evidence: the census finds 109,740 of 143,822 owner entries (76.3%) have no live fighter hit
  capsule. Three adjacent 512 control/candidate medians are 44,873.7/44,505.4 cycles per frame
  (-0.82%); two 256 medians are 42,247.1/41,804.5 (-1.05%). Both digests remain exact.
  Disposition: retain the local producer admission. The rejected Match-wide prescan duplicated cold
  state loads and regressed; the final per-attacker cut reads the same four source states immediately
  before the source would enumerate them and adds no state.
  Next: run the full material gate, record memory/source evidence, and commit atomically if green.

- 2026-07-19 — `retained`
  Scope: complete material correctness/build/state gate.
  Hypothesis: the local disabled-producer predicate must preserve every live combat path and all
  platform/API contracts.
  Evidence: debug and release validation remain 63 PASS / 90 unchanged CLASSIFIED across 1,415,476
  frames. Native API/copy/save-restore/allocation, PPC, Wasm parity, viewer/browser, Python tests,
  source sync, and formatting are green; Match/shared memory are unchanged.
  Disposition: packet complete and ready for its atomic implementation/evidence commit.
  Next: commit, notify the retained win, then profile/select the next bounded owner.
