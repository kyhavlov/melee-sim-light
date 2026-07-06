# Captain Falcon Port Plan

Instantiation of `agent_docs/ADDING_A_CHARACTER.md` for Captain Falcon
(branch `newchar-falcon`). Follow that guide's phases/gates; this file records
the Falcon-specific facts, the port-specific risk areas, and progress.

## Phase 0 — Facts (verified against decomp/repo)

- **Ids**: internal `FTKIND_CAPTAIN = 2`
  (`refs/melee/src/melee/ft/forward.h`), Slippi/CSS external id **0**.
- **Archives**: `PlCa.dat` / `PlCaAJ.dat` — NOT yet in `_iso/`; extract from
  `SSBM.iso` via `tools/extraction/iso_extract.py`.
- **Decomp anchors**: `refs/melee/src/melee/ft/chara/ftCaptain/`,
  prefix `ftCa_`, ftData symbol `ftDataCaptain` (verify exact symbol name in
  the archive), own submotion enum `ftCa_SM_*` in `ftCaptain/forward.h`
  (Falcon is a donor — Ganon is his clone, not the reverse).
- **Articles**: none. `has_articles=False`,
  `exports_item_article_constants=False`.
- **Ext-attrs**: `ftCaptain_DatAttrs`
  (`refs/melee/src/melee/ft/chara/ftCaptain/types.h`) — 35-field layout
  (`specialn_*`, `specials_*`, `specialhi_*`, `speciallw_*`); hand-write the
  layout in `extract_character_attrs.py` like `MarsAttributes`.
- **Specials inventory** (17 char MotionStates from `ftCo_MS_Count`, plus the
  6 item-swing states):
  - `SpecialN` / `SpecialAirN` — Falcon Punch (stick-Y angle-diff attrs).
  - `SpecialSStart` / `SpecialS` / `SpecialAirSStart` / `SpecialAirS` —
    Raptor Boost: hit/miss branching, own grav/terminal-vel attrs, air-miss
    ends in FallSpecial (audit the `ftCo_80096900` callsite args per the
    FallSpecial `mv.co.fallspecial.xC` trap), separate miss/hit landing lag.
  - `SpecialHi` / `SpecialAirHi` / `SpecialHiCatch` / `SpecialHiThrow` /
    `SpecialHiThrow1` — Falcon Dive: **command grab**. Victims enter common
    `ftCo_MS_CaptureCaptain` / `ftCo_MS_ThrownFF`. No ported character has a
    catch-special; expect real extension of shared capture/attach/throw
    owners (`grab_attachment.c`, `throw_flow.c`), not just registry plumbing.
    Explosion release + freefall + regrab.
  - `SpecialLw` / `SpecialLwEnd` / `SpecialAirLw` / `SpecialAirLwEnd` /
    `SpecialLwEndAir` / `SpecialAirLwEndAir` — Falcon Kick: ground<->air
    crossings are distinct action states; per-state collision-callback audit
    (Sheik Chain lesson) applies directly.

## Phase 1 — Registry + data pipeline

Gate: `build_data` end-to-end; existing chars' data binaries byte-identical.

1. Registry rows: `tools/extraction/char_registry.py` `CharInfo(falcon)`,
   `src/char_registry.h` mirror, `src/ids.h` `MSL_CHAR_ID_FALCON = 2`.
2. Full `uv run python -m tools.extraction.build_data` (never keep/commit a
   `--chars` subset tree). Per-extractor watch items:
   - `extract_character_attrs.py`: the `ftCaptain_DatAttrs` layout.
   - `extract_fighter_anims.py`: anim count entry; check dynamic-chain block
     (verify whether any Falcon hurtcaps ride chain bones).
   - `extract_fighter_parts.py`: Falcon Dive catch geometry bones must be in
     the extracted pose set.
   - `extract_attack_id_move_id.py`: verify no merged-table id collisions.
   - motion-state owners: expect callback-id renumber churn (by design).
3. C loaders loop `MSL_CHAR_REGISTRY`; add Falcon ext keys to `char_params.c`
   as per-char optionals with defaults + loud diagnostics.
4. `tools/slippi/action_state_tables.py` + history helpers (article-less
   pass-throughs).

## Phase 2 — Replays + baseline

- NEED FROM USER: 2-3 `.slp` replays with Falcon on different legal stages.
- Convert (`make slpz-convert-suite`), add `replays/suites/<falcon suite>.json`.
- Record zero-char-C baseline (one-step mismatch %, rollout first-breaks) and
  the replay action inventory; everything missing (likely Falcon Punch,
  landed Falcon Dive) is decomp-tests + webplay-only verification.

## Phase 3 — Common-action coverage

Remove falcon from the exclude set in
`tests/test_char_common_action_coverage.py` (~84 tests). Expect new
stage-geometry/ECB boundaries rather than the already-fixed hardcode classes;
consult the trap catalog in ADDING_A_CHARACTER.md Phase 3 when rows diverge.

## Phase 4 — Specials, decomp-first

1. `src/falcon_specials.c/.h` wired via `fighter_callbacks.c` / `action.c`.
2. **Action-id collision audit**: every `MSL_ACT_*`-keyed site in `src/*.c`,
   headers, bindings gated/table-widened; produce the classified table.
3. Verify `ms_b_entry_mask` B-dispatch surface; wire grounded phys owners
   (`ft_80084F3C` / `ft_80084FA8`) for Falcon Kick / Raptor Boost.
4. Ground<->air swaps per family; Falcon Kick's explicit crossover states
   need their `MSLMSO01` callback classes locked.
5. **Falcon Dive catch/throw** — the genuinely new mechanic; touches shared
   capture/throw owners, so existing-char byte-stability after every change
   is the safety net. Audit `ftCa_SpecialHi.c` (`ftCo_AirCatchHit_Coll`,
   `doCatchAnim`, catch gravity attr), victim `CaptureCaptain` attach,
   `SpecialHiThrow/Throw1` release + explosion hit, victim `ThrownFF`, regrab.
6. Decomp-anchored unit tests per special, positives AND negatives
   (Marth scale: ~130 char tests).

## Phase 5 / 5.5 — Replay burn-down, Dolphin ground truth

Classify remaining rows char-boundary vs shared debt. When analysis ties,
use the pinned `refs/Ishiiruka` probe workflow (NEED FROM USER: working
Dolphin build environment if probes become necessary).

## Phase 6 — Live-path verification

- Viewer: `tools/viewer/assets/character_zips.tsv` (NEED: falcon Slippi
  renderer zip + sha), `tools/viewer/live/schema.js`, `build_wasm.sh`
  `viewer_chars`, main.js wiring; keep the live-viewer schema tests green.
- Play every special ground/air/ledge/slope/vs-shield; capture msltrace on
  anything weird. Falcon Dive at ledge and Falcon Kick at walk-offs are the
  obvious live-only risk spots.
- `tools/eval/fuzz_live_clip.py --mode sweep --matrix` must exit 0.

## Phase 7 — Standing gates (after EVERY retained change)

- `uv run python -m pytest -n 4 -q`
- `make validate-all` + `tools.eval.validation_report_diff` (no unexplained
  existing-char movement; restore unintended report churn deliberately)
- `make fmt` / `make fmt-check`; `git diff --check`
- `make build BUILD_FORCE=1` before final review.

## Progress log

- 2026-07-06: plan written; Phase 0 facts verified.
- 2026-07-06: Phase 1 registry + pipeline done: PlCa* extracted to `_iso/`;
  registry rows (py + C + ids.h); `ftData_Table_Unk0[2].count = 318` anim
  entry; `CAPTAIN_SPECIAL_ATTRS_LAYOUT` (35 keys, `falcon_*`) + layout test;
  fixed `_stable_update` dropping non-legacy ext-attr prefixes. `build_data`
  end-to-end; existing-char data byte-identical except documented
  motion-state callback-id renumber (verified pure via symbol-level diff).
  Source artifacts refreshed (owners all chars + falcon; added missing
  zelda attack_id/staling fallback bins). Binding smoke: falcon vs fox
  init/reseed/step, dash/run/jumpsquat(4f)/jab all correct via generic path.
  Coverage suite auto-included falcon: 82/84 passed immediately; fixed 2
  test-side issues (FD runway too short for Falcon's run speed; shield
  radius expectation ignored 12-frame hold drain — now uses measured hp).
  Repinned EscapeAir_Coll callback id 339->400. Full pytest green except
  environmental failures (missing local replays/heldout + replays/debug
  trees, unrelated to port).
- 2026-07-06: rebased `newchar-falcon` onto origin/dev (19 commits: rollout/
  combat/mpcoll owner fixes, extract_motion_state_owners extension, new
  `escapeair_carried_floor_wall_source` overlay). Regenerated data with dev
  extractors; byte-identity gate re-verified on the new base (attack_id/
  staling bins identical to dev's tracked artifacts; owner churn proven pure
  renumber; EscapeAir_Coll still 400). Fixed one more registry hardcode:
  `test_known_data_artifacts` private characters dir omitted falcon. Suite:
  2091 passed; only failures are the owner-confirmed ignorable environmental
  set (replays/heldout, replays/debug, manual_repros/set13 — repo owner says
  validation replays are enough).
