# Parallel character ports — lane protocol

Protocol for running multiple character ports concurrently (one agent per
character "lane") with occasional syncs. Written 2026-07-07 after the falcon
port reached sheik-level one-step parity; grounded in what actually contended
during that port. The per-phase methodology stays `ADDING_A_CHARACTER.md`;
this document only covers *coordination*.

## Pilot lanes

| Lane  | Char (internal deps)        | Plan file       | Difficulty | Replays needed |
|-------|-----------------------------|-----------------|------------|----------------|
| ganon | Ganondorf (falcon code)     | `GANON_PLAN.md` | LOW — `ftGn_Init.c` builds its MotionState table from `ftCa_*` functions; specials are falcon's implementations with Ganon dat_attrs/anims | Ganon vs existing chars, all 6 legal stages |
| mario | Mario (unlocks Dr. Mario)   | `MARIO_PLAN.md` | MEDIUM — own `ftMr_*` specials; fireball is a light projectile article (laser/needle substrate exists); cape reflect; Doc afterwards reuses Mario's code (`ftDrMario/` is Init + AppealS only) | Mario (and some Doc) vs existing chars |
| puff  | Jigglypuff                  | `PUFF_PLAN.md`  | MEDIUM — article-free; 5 midair jumps, Rollout charge machine, Rest, Sing; light-char KB/weight edge cases | Puff vs existing chars (Rest/Rollout/Sing coverage matters) |

Swappable: if a different char is preferred, keep the mix rule — at least one
code-clone lane, at most one article-heavy lane at a time. Article-heavy
(Link, Peach, Ness, Samus) and Ice Climbers are NOT pilot material.

## Ownership map

**Lane-owned (freely editable in parallel; merge conflicts ~impossible):**
- `src/<char>_specials.c/h` (new module per lane)
- `tests/test_<char>_specials.py`, coverage-suite additions for the char
- `replays/validation/<char>/`, `replays/suites/<char>.json`
- `reports/validation/<char>_one_step.txt`, `<char>_rollout.txt`
- `agent_docs/<CHAR>_PLAN.md`
- extracted data under `data/**/<char>.*` (regenerated, not hand-edited)

**Shared / contended (serialize through the queue below):**
- `src/combat*.c`, `src/throw_flow.c`, `src/grab_attachment.c`,
  `src/grab_flow.c`, `src/anim_timebase.c`, `src/physics.c`, `src/ledge.c`,
  `src/hitboxes.c`, `src/hurtboxes.c`, `src/locomotion*.c`, `src/api.c`,
  `src/move_tables.c`, `src/state_fields.inc` + `src/state.h`
- `src/action_ids.h` (enum block additions are lane-scoped but same-file)
- `tools/slippi/seed_history.py`, `tools/slippi/validation_buffer_*.py`
  (seed derivations)
- `tools/extraction/char_registry.py` + regenerated shared data artifacts

Calibration from the falcon burn-down: 4 of 6 Phase-5 fixes touched shared
surfaces. Expect the same ratio; the queue is the normal path, not the
exception.

## Step 0 — batch registration (do ONCE, before lanes fork)

Adding a char to `char_registry.py` renumbers motion-state callback ids in
the shared data artifacts (documented during falcon Phase 1). To avoid every
lane's merge churning the artifacts:

1. Add ALL pilot chars' registry rows in a single commit on `main`'s
   integration branch (internal/external ids verified against the decomp,
   `PlXx.dat` names, decomp dirs/prefixes, `has_articles`).
2. Run `build_data`; verify the existing chars' bins are byte-identical
   except the documented callback-id renumber (symbol-level diff proof, as
   done for falcon).
3. Land it with `validate-all` + full pytest green.

Every lane then forks from this base and the artifact numbering never moves
again during the pilot.

## Lane mechanics

- One git worktree + branch per lane: `newchar-<char>` off the post-step-0
  integration base.
- A lane runs Phases 0-5 per `ADDING_A_CHARACTER.md`, committing at check-in
  boundaries on its own branch only.
- Lane-local gates per retained change: own suite + own tests + validate-all
  byte-stability for everyone else (this is cheap insurance that the lane
  never silently leans on another lane's uncommitted state).
- Phase 6 (webplay) stays serialized and interactive — it needs the
  user-supplied Slippi renderer zip per char and live play.

## Shared-fix queue (the serialization point)

When a lane needs to change ANY shared-surface file:

1. Extract the change into a minimal, decomp-cited diff — no lane-local code
   in the same commit.
2. Rebase it onto the current integration branch alone; run `validate-all`
   (all existing chars byte-identical unless the fix deliberately and
   explicitly improves them — document report movement in the commit) and
   full pytest.
3. Land it on the integration branch. One shared fix in flight at a time.
4. Every other lane rebases onto the new base and reruns its OWN suite
   before its next retained change (a shared fix can legitimately move a
   lane's numbers — usually down; falcon's throw wait-timer fix would have
   improved any weight-scaled-throw char).

Priority rule: shared fixes queue in whatever order they're ready; a lane
blocked on the queue keeps working its char-local tail (there is always
char-local tail).

Known shared-debt magnets to expect (from falcon):
- consume-once cmd-var latch reconstruction on reseed (now falcon-scoped in
  `falcon_specials_reseed_init`; generalize per-char as lanes hit it)
- seed derivations missing new capture/thrown action ids
  (`derive_grab_owner_port` etc.)
- LandingFallSpecial origin lag/allow tables (`validation_buffer_common.py`
  keyed on char attrs — each lane with special freefall adds its rows)
- movescript f32 wait-timer edges at non-unit anim rates
- element/mask admission gaps in combat passes exposed by new hitbox shapes

## Sync cadence

- Rebase-on-shared-landing (event-driven), not clock-driven.
- A lightweight cross-lane sync when any lane finishes a phase: one message
  summarizing landed shared fixes + open queue entries + artifact/regeneration
  implications. The lane plan files' progress logs are the durable record.

## What the user supplies per lane

- A replay dump containing the character (netplay `.slp` — the falcon recipe
  scans a dump with peppi for action-state coverage and curates ~8 replays
  jointly covering every special MotionState).
- For Phase 6 only: the character's Slippi renderer zip (+ sha) for
  `tools/viewer/assets/character_zips.tsv`.
- Review at lane check-in boundaries.

## Pilot status

- 2026-07-07: protocol written; lane plans drafted (`GANON_PLAN.md`,
  `MARIO_PLAN.md`, `PUFF_PLAN.md`). Waiting on: char confirmation + replay
  dumps. Step 0 (batch registration) not yet run — do it once chars are
  confirmed, before forking lanes.
