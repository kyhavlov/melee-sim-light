# Marth Port Plan (worktree: melee-sim-light-newchar, branch: newchar)

Goal: Marth fully implemented decomp-first; VictoriousSpitefulAlpaca (Marth/Falco) burned to
0/near-0 rollout mismatches. Two clean change subsets: (1) refactors that make new characters
minimal/smooth, (2) truly Marth-specific code that cannot come from movescripts/data.

The single replay is verification-only at the start - it will NOT cover all moves (upsmash,
side-B variants, down-B likely missing). Port from decomp by first principles; replay verifies.

Facts: Marth = Melee internal id 18, PlMs.dat, ftDataMarth, decomp ftMarth/ (ftMs_*). No
articles/projectiles. Tipper = authored hitbox placement (data). Specials: SpecialN Shield
Breaker (charge), SpecialS Dancing Blade (4 stages x up/mid/down x air), SpecialHi Dolphin
Slash, SpecialLw Counter (+CounterAttack).

## Check-ins
1. APPROVED+IN PROGRESS - Replay intake + data pipeline generalized.
   Scope: slp->slpz->suite->preprocess; build_data --chars fox,falco,marth end to end; all
   Marth structural artifacts (characters/anims/hurtcaps/ecb/moves/shields/attack_id/
   motion_state/staling/special_msids); engine init/reseed/step/write_compare on the Marth
   replay without crashes. NO specials. Deliver: file split (extractor-tooling vs generated
   data vs runtime loader), fox/falco assumptions removed, replay action inventory
   (specials observed/missing), first validation numbers (allowed bad; tooling must run),
   fox/falco baseline clean or movement explained. Commit only after review approval.
2. Common-action Marth one-step burn (movement/aerials/smashes/shield/grab/ledge/tech);
   sharp-edge ledger; fox/falco suites green throughout.
3. Specials from decomp first principles + decomp-anchored unit tests (frame windows,
   velocities, transition gates) since the replay lacks coverage.
4. Rollout burn to 0/near-0 (locate -> witness -> owner methodology); final split summary.

## Rules
- Decomp/data-backed only; no replay/record-id bridges; no row-shaped thresholds.
- Tag every change refactor-vs-marth in the progress log below.
- After each retained change: pytest -n 4 (never more), validate-all (workers 3), diff vs the
  worktree baseline, err.pos_x scan. Fox/falco aggregate baseline must not move unexplained.
- Worktree has own venv/build/data/datasets; refs/ + _iso/ symlinked to the main repo.
- Commit on newchar ONLY at approved check-in boundaries; otherwise patch snapshots in
  reports/marth_port/.

## Progress log
- (start) Worktree green at HEAD ae3b571f: 3145 passed / 1183 skipped; own data (96M) +
  datasets (1.7G) regenerated locally.
- (ci1.a) build_data --chars fox,falco,marth end to end. Registry modules added
  (tools/extraction/char_registry.py + src/char_registry.h). Extractors generalized: build_data,
  shield_tilt_table, staling_move_id, attack_id_move_id (merged per-char MF namespaces),
  motion_state_owners (registry specs + submotion enums; callback ids renumber by design -
  manifest-driven regen in tests/conftest), fighter_script_timeline, fighter_anims (anim count
  table + owner-gated SSDYNN01: chars without collision-owner msids emit empty dyn; marth cape
  hurtcap caveat noted), fighter_parts, item_articles (skip article-less chars).
  C loaders generalized to MSL_CHAR_REGISTRY loops: anim_pose, anim_table, hitboxes_tables,
  hurtcaps_tables, shield_tilt_table, char_params (spacie keys now per-char optional with
  defaults + loud parse diagnostics), special_msids, ecb_tables (bottom+extents), staling_tables,
  script_events, attack_id_tables, motion_state_owners (+common_class_has loops all chars),
  api.c match-init gate. MSL_CHAR_ID_MARTH=18 added.
  Verified: binding init/destroy + marth pose reads OK; 3145 tests pass; fox/falco staling +
  attack_id + articles binaries byte-identical.
- (ci1.b) Replay intake: VictoriousSpitefulAlpaca.slp -> slpz (suite marth_falco_recent.json,
  ports [1,2], Marth/Falco, Battlefield 31) -> preprocessed first try: 8922 samples, marth
  char_id 18, action ids 0-368. Engine smoke: init/reseed/step/write_compare on marth rows OK
  (5/5 sampled rows even match one-step).
- (ci1.c) First numbers: marth one-step discrete 1158/1079562 (0.107%) with ZERO marth-specific
  C; rollout 697 first-mismatches, median streak 49, best 359. Fox/falco validate-all vs HEAD:
  "no suite total changes" - byte-stable baseline through all generalization.
- (ci1.d) Replay specials coverage: PRESENT 343 SpecialNEnd0(28), 345-347 air ShieldBreaker
  start/loop/end0, 358 SpecialAirS1(325!), 368 SpecialAirHi(154). MISSING: ALL ground Dancing
  Blade 349-357, all DB followups 359-366, ground DolphinSlash 367, ALL Counter 369-372, ground
  SB start/loop 341-342, SpecialNEnd1 344. Confirms decomp-first porting requirement.
- (ci1.e) Review-clean pass: clean-state regen of data/ from empty verified (numbers identical);
  tools/slippi/action_state_tables.py load_action_state_tables generalized via registry (was
  {1,22} only; marth x9_b1 lanes now correct - one-step 1158 -> 1150). Committed at this
  boundary per review approval.

## Intentionally deferred fox/falco assumptions (post check-in 1)
- DYNAMIC CHAINS (PROMINENT): marth's 3 cape/hair chains (12 nodes) exceed the 4-node runtime
  state contract; extractor emits empty SSDYNN01 for owner-less chars. Marth HURTCAPS on bones
  60/70/71 likely ride these chains - static rest pose may misplace them. Revisit the moment a
  validation row implicates head/cape hurtcap geometry; do NOT treat as irrelevant.
- char_params spacie ext-attrs: marth's ftMarthAttributes (ftData.x4 ext block: sword/charge/
  dancing-blade constants) NOT yet extracted - needed at check-in 3 (specials).
- damage_post_hitlag_sfx_{mid,high}_num: fox/falco literals in extract_character_attrs; marth
  defaults absent (sfx-only).
- Runtime char-special behavior gates correctly remain fox/falco: blaster.c, shine.c,
  reflector_bubbles.c, instance_id.c special-instance policy, laser/illusion lanes in items.c,
  state_flags.c falco-specific bits; ANIM_DYN collision owners (fox tail).
- tools/slippi preprocessing: combat/anim/staling history modules have fox-named helpers that
  are char-id-keyed pass-throughs for marth rows (no marth articles); item_article_data.py is
  fox/falco by design.
- ecb anims "DamageAir2/3" extra-msid comment block is fox/falco-suite-motivated but applied
  to all chars (harmless).
- (ci2 COMMITTED d53af62d)  Coverage suite tests/test_char_common_action_coverage.py: 84 tests parameterized over
  the registry characters that have full data artifacts (fox + marth this check-in; falco's data
  is identical-shape and can be added to the param list at zero cost), expectations derived from
  extracted data (the reusable porting checklist). Sharp edge found+
  fixed: src/move_tables.c char_slot/{1,22} hardcode + 2-char cache (gated dash->run, runbrake,
  turnrun, jab combos, smash charge, throw hitboxes, catchattack, hit_status, hurtbox masks -
  ALL silently NULL for marth). Also motion_state_owners common_class2/3 registry loops.
  Replay smoke: one-step 1150 -> 684 (0.063%), rollout 697 -> 404 first-breaks, median 49 -> 75.
  Fox/falco validate-all: "no suite total changes". Full suite 3229 passed (3145 prior + 84 new).
- (ci3 COMMITTED 811608a5) Specials implemented decomp-first: MarsAttributes extraction (28 keys, mars_sword
  layout), src/marth_specials.c (DS/SB/DB/Counter state machines + phys), Counter combat
  intercept, SB charge-damage hitbox override, DS TransN launch + 34f landing lag. THE major
  generic refactor: msl_char_id_is_spacie gating of ~60 char-blind MSL_ACT_FX_* sites (marth
  specials were being driven by fox's special state machines). 17 specials tests + 101 total
  marth tests green; full suite 3246. Replay smoke: one-step 684 -> 241, rollout 404 -> 55
  first-breaks (median 75 -> 231). Fox/falco validate-all: no suite total changes. See
  CHECKIN3_REPORT.md.
- (ci3 review-fix pass) Completed the full MSL_ACT_FX_* audit (every runtime site in src/
  classified gated / char-param-widened / documented-safe; table in CHECKIN3_REPORT.md),
  fixed the reviewer's locomotion landing-selector + shine platform-pass blockers, and
  implemented the Counter AbsorbDesc descriptor-sphere intercept geometry (bone-posed, with a
  back-hit test). 18 specials tests; suite 3247; fox/falco validate-all still byte-stable;
  marth smoke unchanged (241 one-step / 55 rollout).
- (ci3 review-fix pass 2) Audit extended to headers + bindings (action_ids.h fastfall,
  damage_terminal_owner.h, specialhi_pose.h, shielddesc_geometry.h, reflector_bubbles.c,
  items.c boundary-encoded owner invariants). Counter descriptor now FAIL-CLOSED (no body-
  admission fallback) with the desc bone added to the pose extraction (marth 22 joints) and a
  sentinel-seed proof test. Suite 3248; fox/falco byte-stable; marth smoke 241/55 unchanged.
