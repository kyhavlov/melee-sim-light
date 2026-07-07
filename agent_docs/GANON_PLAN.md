# Ganondorf port plan (lane: ganon)

Lane plan under `PARALLEL_PORTS.md`; methodology per `ADDING_A_CHARACTER.md`.
Drafted 2026-07-07 before Phase 0 — items marked VERIFY are to be pinned by
the lane agent from the decomp/ISO, not assumed.

## Why this lane is cheap

`refs/melee/src/melee/ft/chara/ftGanon/` contains ONLY `ftGn_Init.c/h`, and
`ftGn_Init_MotionStateTable[ftCa_MS_SelfCount]` is built from `ftCa_*`
functions (`ftCa_SpecialN/S/Hi/Lw` includes at the top of the file). Ganon's
four specials are Captain Falcon's implementations running with Ganon's
dat_attrs, animations, and TransN data:
- Warlock Punch  = ftCa_SpecialN family   (falcon_specials.c SpecialN owner)
- Gerudo Dragon  = ftCa_SpecialS family   (Raptor Boost owner + inert-detect)
- Dark Dive      = ftCa_SpecialHi family  (Falcon Dive command grab:
  SpecialHiCatch/Throw + CaptureCaptain victim + hitlag-share + donor
  figatree — ALL already modeled)
- Wizard's Foot  = ftCa_SpecialLw family  (Falcon Kick six-state family +
  wall rebound)

Port strategy: make `falcon_specials.c` drive both chars. The module already
keys behavior on extracted per-char attrs (`falcon_*` attr names) and
submotion tables; the work is admission (char gate → "char owns the ftCa
machine", following the data-driven `char_owns_*_machine` pattern from the
kind-migration commit) plus Ganon's data extraction. Expect near-zero new
gameplay code; expect DIFFERENT tuning values everywhere (Ganon is slower,
heavier, different TransN deltas).

## Phase 0 — facts (VERIFY all)

- Internal/external char ids (registry row): VERIFY from decomp
  `FTKIND_GANON` / external id tables. `PlGn.dat` / `PlGnAJ.dat` names,
  `ftDataGanon` symbol.
- Confirm `ftGn_Init_MotionStateTable` maps 1:1 onto falcon's specials
  action-id block 347-363 and victim CaptureCaptain 275 (same shared ids
  — the CA block in `action_ids.h` should be reusable as-is; decide whether
  to alias or rename the enum comments to note dual ownership).
- Ganon-specific overrides: VERIFY whether ftGn_Init overrides ANY callback
  vs falcon (grep the table diff) — e.g. taunt, entry, and whether
  SpecialHiThrow1 wall-rebound applies.
- dat_attrs layout: VERIFY Ganon's ftCaptain-typed attrs load through
  `CAPTAIN_SPECIAL_ATTRS_LAYOUT` (35 keys) unchanged; extraction should
  emit `ganon_*` or reuse `falcon_*` key names — decide with an eye on
  `validation_buffer_common.py`'s attr-keyed origin tables (falcon rows are
  keyed on `falcon_specialhi_landing_lag` presence; Ganon needs the same
  rows with his values).
- Figatree donor: CaptureCaptain victims of GANON play the GANON-file share
  anim (VERIFY name, expect `PlyGanon_Share_ACTION_TCaptainSpecialHi`-like).
  The CapturePulledHi hurtcap fallback approximation applies unchanged.

## Phases 1-5 (falcon recipe, compressed)

1. Registry row lands in the batch step-0 commit (see PARALLEL_PORTS.md).
   Extract PlGn from `_iso/`; `build_data`; byte-identity gate for existing
   chars; anim entry count (`ftData_Table_Unk0[...]`): VERIFY Ganon's.
2. Replay intake: scan the user dump with the falcon peppi recipe
   (`tools/slippi` scan script pattern) for ftCa special MotionStates
   347-363 + victim 275 coverage. Curate ~8 replays, all 6 legal stages,
   vs existing chars where possible. NEED FROM USER: dump with Ganon games.
3. Common actions: coverage suite auto-includes the char; expect the same
   2-3 test-side fixups falcon hit (runway lengths, shield-drain
   expectations) given Ganon's stats.
4. Specials: admission work in `falcon_specials.c` dispatch + machine-entry
   gates (char id 2 hardcodes → char-owns-machine predicate), Ganon attrs
   plumbed, reseed init + seed-derivation rows (grab_owner_port already
   handles 275 char-agnostically; LandingFallSpecial origin tables need
   Ganon rows), move_tables throw mapping for 356 (Dark Dive release
   payload: VERIFY damage/kbg/angle differ from falcon's 12/82/361).
   Unit tests: clone the falcon specials test file shape with Ganon
   numbers; add one cross-char test (falcon vs ganon dive interactions —
   both command grabs live in one match).
5. Burn-down with the taxonomy script (scratchpad falcon_taxonomy.py shape,
   parameterized by suite) — expect the tail to be mostly shared rows
   already classified during falcon.

## Gates

Per `ADDING_A_CHARACTER.md` Phase 7 after every retained change; plus this
lane must keep FALCON byte-stable (falcon suite reports move only with a
documented cause — the two chars share the specials module, so this is the
lane's highest regression risk; any refactor of falcon_specials.c admission
is a shared-fix-queue item, not a lane-local change).

## Progress log

- 2026-07-07: plan drafted; awaiting step-0 registration + replay dump.
