# Adding a Character

Exhaustive checklist for porting a new character, distilled from the Marth port
(branch `newchar`, commits `3a2d8813..d25e6882`). Follow the phases in order; each
has a hard gate. The repeated theme: **the pipeline is registry-driven, but every
port so far has found char-blind hardcodes that silently no-op for the new
character** — the verification steps exist to surface those, do not skip them.

Conventions used below: `<char>` is the pipeline name (e.g. `marth`), `ftXx_` the
decomp prefix, `PlXx.dat` the fighter archive.

---

## Phase 0 — Facts and prerequisites

Collect before writing any code:

- **Ids**: Melee internal id (ft/types.h FighterKind order; Fox=1, Marth=18,
  Falco=22) and Slippi/CSS external id (Fox=2, Marth=9, Falco=20). These are
  different id spaces; seeds/binding use internal, replay metadata external.
- **Archives**: `PlXx.dat` + `PlXxAJ.dat` must exist in `_iso/` (extract via
  `tools/extraction/iso_extract.py` if missing).
- **Decomp anchors**: `refs/melee/src/melee/ft/chara/ftXxx/` directory, the
  `ftDataXxx` public symbol, the per-char function prefix, and — for clone
  characters — the **donor submotion enum** (Falco reuses ftFox's `ftFx_SM_*`;
  a clone's submotion_dir/prefix point at the donor).
- **Articles**: does the character spawn items/articles (lasers, projectiles,
  turnips...)? Sets `has_articles` and decides whether the item pipeline needs
  work (Phase 4).
- **Specials inventory**: list every special action state from the decomp
  (`ftCo`-range ids are shared; char ranges start at 341+) with ground/air
  variants, charge states, followups. Marth: SpecialN ShieldBreaker
  (charge, 341-348), SpecialS Dancing Blade (4 stages x 3 directions x air,
  349-366), SpecialHi Dolphin Slash (367-368), SpecialLw Counter (369-372).
- **A validation replay**: at least one `.slp` containing the character (ideally
  vs an already-supported character). Accept up front that one replay will NOT
  cover all moves — Marth's replay had zero ground specials, zero Counter. The
  decomp is the implementation source; the replay only verifies.

## Phase 1 — Registry + data pipeline (gate: build_data end-to-end, fox/falco bins byte-identical)

1. **Registry rows**:
   - `tools/extraction/char_registry.py`: add a `CharInfo` (all fields above).
   - `src/char_registry.h`: mirror the entry in `MSL_CHAR_REGISTRY`.
   - `src/ids.h`: `MSL_CHAR_ID_<CHAR> = <internal id>`.
2. **Run** `uv run python -m tools.extraction.build_data --chars fox,falco,<char>`
   and chase failures extractor by extractor. Every extractor is registry-driven
   but has per-char caveats:
   - `extract_character_attrs.py` — common attrs come from ftData; the
     **char-specific ext-attr block (`ftData.x4`)** needs an explicit layout
     (Marth: 28-key `MarsAttributes` mars_sword layout). Write the layout from
     the decomp's `ftXxxAttributes` struct; this is hand work per character.
     Also: `damage_post_hitlag_sfx_{mid,high}_num` are per-char literals
     (sfx-only; defaults exist).
   - `extract_fighter_anims.py` — anim count table entry; SSDYNN01 dynamic
     chains are **owner-gated**: chars without collision-owner msids emit an
     empty dyn block. KNOWN LIMIT: the runtime dynamic-chain state contract is
     4 nodes; Marth's cape/hair chains (3 chains, 12 nodes) exceed it and were
     proven cosmetic via a lineage+collider data test. For a character whose
     hurtboxes/collision ride long chains, this contract must be extended.
   - `extract_fighter_hurtcapsules.py` — check whether any hurtcaps sit on
     dynamic-chain bones (Marth: bones 60/70/71); static rest pose may misplace
     them. Prove it harmless (data test) or fix the pose source.
   - `extract_ecb_bottom.py` / `extract_ecb_extents.py` — per-action ECB tables.
     The extra-msid comment blocks (e.g. DamageAir2/3) apply to all chars.
   - `extract_fighter_parts.py` — joint/bone tables. If the character has a
     special needing descriptor geometry (Marth Counter's AbsorbDesc bone),
     make sure that bone is in the extracted pose set (Marth: 22 joints).
   - `extract_fighter_moves.py` + `extract_fighter_script_timeline.py` —
     movescripts; allow_interrupt frames feed the IASA dispatch later.
   - `extract_attack_id_move_id.py` — per-char MF namespaces are merged; verify
     no id collisions in the merged table.
   - `extract_motion_state_owners.py` — registry specs + submotion enums.
     **Callback ids renumber by design** when a char is added; the manifest
     regen in `tests/conftest.py` handles it — expect motion-state-owner test
     churn, not breakage.
   - `extract_special_msids.py`, `extract_shield_tilt_table.py`,
     `extract_staling_move_id.py`, staling weights — straightforward registry
     loops.
   - `extract_item_articles.py` — skipped for article-less chars.
3. **C loaders**: all loop `MSL_CHAR_REGISTRY` (anim_pose, anim_table,
   hitboxes_tables, hurtcaps_tables, shield_tilt_table, char_params,
   special_msids, ecb_tables, staling_tables, script_events, attack_id_tables,
   motion_state_owners, api.c match-init gate). `char_params.c` treats
   char-family-specific keys (e.g. spacie ext keys) as per-char optional with
   defaults + loud parse diagnostics — add the new char's ext keys the same way.
4. **tools/slippi**: `action_state_tables.py` is registry-driven (it was the
   last `{1,22}` hardcode found; the x9_b1 lanes were silently wrong for marth).
   The combat/anim/staling history helpers are char-id-keyed pass-throughs for
   article-less chars; an article-having char needs its article data module
   (cf. `item_article_data.py`).
5. **GATE**: clean-state regen of `data/` from empty produces identical numbers;
   binding `init/destroy` + pose reads for the new char work;
   **fox/falco data binaries are byte-identical** to before your change (this
   catches generalization mistakes immediately and is non-negotiable).

## Phase 2 — Replay intake + first numbers (gate: pipeline runs, baseline documented)

1. `.slp` -> `.slpz` (`make slpz-convert-validation` / `slpz-convert-suite`),
   place under `replays/validation/<suite>/`, add
   `replays/suites/<suite>.json` (ports, chars, stage).
2. `make preprocess SUITE=replays/suites/<suite>.json` into `datasets/`.
3. Engine smoke: init/reseed/step/write_compare on sampled rows.
4. Record the **first numbers** (one-step mismatch count + rollout
   first-breaks/median) with ZERO char-specific C — this is your baseline.
   Marth: 0.107% one-step from generic code alone.
5. Build the **replay action inventory**: which action ids appear, which
   specials are present/missing. This scopes what the replay can and cannot
   verify (everything missing must be verified by decomp-anchored unit tests
   and live play instead).

## Phase 3 — Common-action coverage (gate: coverage suite green for the char)

1. Add the character to `tests/test_char_common_action_coverage.py` (the CHARS
   dict is registry-driven minus an exclude set — usually just remove the
   exclusion). ~84 parameterized tests with expectations derived from the
   char's own extracted data: dash/run physics, jumps, aerials, smash charge,
   throws, shield, ledge, tech, etc. This suite IS the porting checklist.
2. Expect to find char-blind hardcodes. The class found for Marth:
   `src/move_tables.c` had a `char_slot` `{1,22}` hardcode plus a 2-entry cache —
   gated dash->run, runbrake, turnrun, jab combos, smash charge, throw
   hitboxes, catchattack, hit_status, hurtbox masks were ALL silently NULL for
   the new char. Grep for fixed-size per-char caches and `{1,22}`/`char_slot`
   patterns when a test fails mysteriously.
3. Re-run the replay numbers; common-action fixes should cut them sharply
   (Marth: one-step 1150 -> 684, rollout median 49 -> 75).

## Phase 4 — Specials, decomp-first (gate: per-special unit tests + full audit table)

This is the bulk of the work. Implement from the decomp; the replay will not
cover most of it.

1. **State machines**: `src/<char>_specials.c/.h` — enter/phys/coll/IASA per
   special, driven by the extracted ext-attrs. Wire dispatch in
   `fighter_callbacks.c` / `action.c`.
2. **THE ACTION-ID COLLISION AUDIT** (the single biggest source of bugs):
   char-range action ids overlap across characters (341+ means different moves
   per char). Before Marth, ~60 runtime sites keyed on `MSL_ACT_FX_*` were
   char-blind — fox's special state machines were driving marth's ids. Every
   such site must be gated (`msl_char_id_is_spacie(...)` or per-char) or
   widened to char-parameterized data. Audit `src/*.c` AND headers AND bindings
   (`action_ids.h` fastfall set, `damage_terminal_owner.h`,
   `specialhi_pose.h`, `shielddesc_geometry.h`, `reflector_bubbles.c`,
   `items.c`, `state_flags.c`, `instance_id.c`). Produce a complete table:
   every site classified gated / char-param-widened / documented-safe.
3. **B-dispatch surface** (ftCommon side, per new char only needs verifying but
   was built during Marth):
   - `ms_b_entry_mask`: per-action-per-direction entry table from the full
     ftCommon `*_IASA` enumeration. Direction masks are action-specific
     (Dash=side-only, KneeBend=up-only post-entry, SquatWait=up/down,
     Turn=no-neutral, RunBrake/TurnRun=none, RunDirect/SquatRv/OttottoWait
     included; AppealS blocked when data-absent).
   - Grounded resolution order is source order: SpecialS -> up -> neutral
     (D6824) -> down (D68C0). Watch same-frame races (Run_IASA dispatches
     before RunBrake entry — the brake entry frame honors Run's chain).
   - Attack-IASA specials (AttackS4 direct + Wait_IASA delegators) gate on the
     script's allow_interrupt frame via move_tables.
   - Post-hitstun delegates: grounded Damage, airborne DamageAir/DamageFly,
     DamageFall (tumble side-B). PassiveWall with walltech-timer block.
   - B-reverse: ftCo handling, NOT nested under cmd0==0.
   - Aerial up-B input buffer (x68B lane) — currently a locked test + source
     TODO; check whether the new char's timing exposes it.
   - GuardOff x1C window.
4. **Grounded phys owners**: ground specials use ft_80084F3C/ft_80084FA8
   (friction + anim-root-motion exchange) — wire for all the char's ground
   specials or they'll slide/stick wrongly.
5. **Ground<->air swaps**: frame-preserving action swaps for every special that
   can cross the boundary mid-move (walk-off, landing, Stadium transform).
   Test air->ground AND ground->air per family; document the ones with no
   reachable seed surface.
6. **Per-special mechanics** found for Marth that generalize:
   - charge specials: charge-damage hitbox override (SB), charge persistence;
   - launch specials: TransN-driven launch + special landing lag (DS 34f);
   - ledge interaction: stop-at-ledge classification from ftXx coll data (DB),
     ledge-grab `cd->ecb.bottom.x` term with per-char bottom_x tables (under-lip
     sweetspot);
   - cliffcatch cmd sequencing: cmd1 arms in the Phys descent branch (vanilla
     two-pass Coll), teacher-forced cmd1/cmd2 reseed derivation;
   - counter/absorb specials: combat intercept (`combat.c`), descriptor-sphere
     geometry bone-posed and FAIL-CLOSED (no body-admission fallback), real
     projectile geometry threaded through ALL `combat_apply_item_hit` callers
     (8 of them; geometry-less callers fail closed), shield-strength hitlag
     floor (x60/x1964);
   - per-special hitlag/SFX attrs.
7. **Tests**: decomp-anchored unit tests per special — frame windows,
   velocities, transition gates, full-chain dispatch tests (e.g. down-B during
   run). Marth ended at ~130 char tests. Every reviewer pass found real bugs;
   write the negative tests too (e.g. "no special from jab IASA").
8. **GATE**: all specials tests green; fox/falco validate-all "no suite total
   changes" (byte-stable) after every retained change.

## Phase 5 — Replay burn-down (gate: remaining rows classified char-boundary vs shared debt)

Methodology: locate -> witness -> owner (`make rollout-locate`,
`reports/triage/` tooling).

- Re-derive reseed lanes the preprocessor can't see (op52 `x221C_u16_y` event
  floor on frame-preserving swaps, teacher-forced cmd lanes).
- Expect a long tail of SHARED debt that the new char merely exposes
  (ECB-interpolation platform-landing rows, KB float parity, inherited drift
  chains — prove with clean-reseed byte-exactness). Classify and document;
  don't force char-specific fixes onto shared rows.
- Keep fox/falco byte-stable throughout.

## Phase 6 — Live-path verification (gate: webplay clean + clip matrix exits 0)

**The most important lesson of the port.** Replay validation is structurally
blind to live-path bugs: reseeding fills CollData/lock lanes that live play
leaves stale, so an entire class of collision/dispatch bugs never appears in
replay numbers. Marth shipped replay-clean with: broken ground-special
dispatch (no ground specials in the replay), stuck DS landings, dead
B-reverse, and airdodge-through-stage kills. You MUST:

1. **Webplay viewer** (`make viewer`): add the character —
   `tools/viewer/assets/character_zips.tsv` (zip + sha + slippilab URL),
   `tools/viewer/live/schema.js`, main.js character wiring. Then actually play:
   every special, ground and air, at ledges, on slopes, vs shield. Capture
   anything weird with the viewer's msltrace recording.
2. **Trace-replay debugging**: a captured `.msltrace.json` (sparse-delta-v1)
   replays natively — seed from the trace's own row near the failure and feed
   its recorded inputs (reproduces to ~0.04 units when synthetic repro guesses
   miss). This is the primary live-bug debugging loop.
3. **Stage-clip fuzz matrix** (`tools/eval/fuzz_live_clip.py`, dev-only): add
   the char (registry-driven), run
   `uv run python -m tools.eval.fuzz_live_clip --mode sweep --matrix
   --save-violations <json>`; the deterministic sweep (ledgedash /
   fall-into-stage / boundary-approach x 6 stages) must exit 0. Harness realism
   rules if you extend it: airborne seeds carry ground_id 0xFFFF (zero fakes a
   carried strip), spawns must not pre-penetrate the hull, boundary cases come
   from live run-off prefixes, and the oracle is trajectory-resolution based —
   validate any oracle change against the known pre-fix kills.
4. **Hidden-lane debugging tools**: `msl_binding.debug_write_colldata_ecb` +
   `tests/test_colldata_ecb_substrate._colldata_ecb_dtype` dump the CollData
   ECB/floor-probe lanes per frame (this cracked both airdodge clip bugs);
   the floor probe lanes record owner/reject-reason per frame. For wall-side
   issues there are no probe lanes yet — temporary `__LINE__`-printing macro
   wrappers around the probe/begin helpers work (build with
   `setup.py build_ext --inplace`, revert after).

## Phase 7 — Standing gates (run after EVERY retained change)

- `uv run python -m pytest -n 4 -q` (never more than 4 workers).
- `make validate-all` (workers 3) + `uv run python -m
  tools.eval.validation_report_diff --before HEAD --after reports/validation
  --top 220`: fox/falco aggregate baseline must not move unexplained ("no
  suite total changes", no hard reds). Afterwards, inspect the report diffs:
  if the reports are not part of the intended change, restore only the
  generated validation report files after confirming they contain no intended
  updates; if core sim logic changed and refreshed reports are required, keep
  them.
- `make fmt` / `make fmt-check`; `git diff --check`.
- err.pos_x scan on the char's suite when touching physics/collision.
- `make build BUILD_FORCE=1` before final review.
- Commit ONLY at approved check-in boundaries.

## Known traps, quick list

- Two id spaces (internal vs external char id) — never mix.
- Clone chars reuse the donor's submotion enum.
- Char-range action ids collide across chars; audit every action-id-keyed site
  including headers and bindings.
- `{1,22}`-style hardcodes and fixed-2-slot caches silently NULL for new chars.
- Motion-state callback ids renumber on regen by design (conftest manifest).
- Dynamic chains: 4-node runtime contract; hurtcaps on chain bones need proof.
- Ext-attrs block needs a hand-written per-char layout from the decomp struct.
- One replay never covers ground specials/counters — decomp tests + webplay.
- Reseed-based repros mask live-path bugs; verify fixes against live lane
  evolution (trace replay), and prefer last-resort owners over relaxing
  validated gates when scoping collision changes.
- Stage geometry JSON is UNSCALED dat coords; world = json x `unit_scale`.
- Counter/absorb intercepts: fail closed when geometry is missing.
- After `make validate-all`, review `reports/validation/` diffs deliberately -
  restore only unintended regenerated reports, keep intended ones.
