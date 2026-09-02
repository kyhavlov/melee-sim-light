# Correctness lead — the two Link open port defects (2026-08-30, landed)

## Objective

Close the two classifications carried as explicit open port defects:
`link-boomerang-deflect-fork` (links `slippi-games-2025-05` @4185, 202 rows) and
`thrown-release-pose-offset` (links `slippi-2025-08` @3503, 17 rows). Neither was what its
ledger text said.

## What the traces showed

- **Boomerang:** there is no Yoshi's Story line at (-43.4, 10.7). The recording's item freezes
  for ten frames with its timer held at 246 while P1 Marth goes 369 -> 370
  (`ftMs_MS_SpecialLw` -> `SpecialLwHit`): the boomerang hit Marth's Counter. A hosted trace
  proved the hit is registered (`ftColl_80077688` runs on the frame, `xDCE_flag.b4/b5` set,
  `Item_80269DC8` dispatches the boomerang's `it_802A2320` mirror against `xC58`), and the
  hosted post-mirror velocity (-1.711, -0.523) is exactly the recording's pre-mirror velocity
  (-1.7866, -0.0937) reflected across n = (-0.1736, 0.9848); the recording's (-1.6468, 0.6991)
  is the reflection across (+0.1736, 0.9848). `p_ftCommonData->x2D0` is 80, so the sign of
  `xC58.x` is the whole defect. The DOL at 0x80077818..0x8007782C (`fcmpo pos->x, 0; cror
  eq,gt,eq; bne -> fneg`) keeps +cos on the `pos->x >= 0` side; `refs/melee`'s
  `ftColl_80077688` has the two arms swapped, so every projectile that hits a counter shield
  was mirrored into the counter's side.
- **Throw release:** P1 Young Link stands on a Fountain platform rising 0.1125/frame. The
  release vec from `ftCo_800DDDE4` lands below the platform (y -2.12) and `mpColl_800471F8`
  snaps the victim onto the line — hosted at the frame's new height (6.3376), retail at the
  previous one (6.2251 = 8.048 - 1.823). Retail's platform update is `grIzumi_801CC358`,
  registered by `grIzumi_801CBCE8` at priority 4 (after `Fighter_8006A360` at 1, before
  `Fighter_procMap` at 6), and Slippi's `SendFountainInfo.asm` hooks 0x801CC998 inside it.
  The hosted runtime published the recorded height in `apply_replay_stage_events` at frame
  start and the priority-4 callback's replay branch was a no-op (`was_applied`), so every
  mid-frame floor resolve in the fighter anim phase saw the platform one phase early.

## Final boundary

- `src/melee/ft/ftcoll.c::ftColl_80077688`: hosted branch with the DOL's arm order
  (`canonical:counter-shield-item-deflect-normal-sign`, upstream-candidate).
- `src/runtime/scalar.c::apply_replay_stage_events` no longer publishes Fountain heights;
  `src/melee/gr/grizumi.c::grIzumi_801CC358`'s replay branch owns the publication at retail's
  scheduler point; `msl_slippi_fod_platform_{was_applied,mark_applied}` and
  `MslCoreSlippiState::fod_platform_applied_mask` are deleted
  (`canonical:fountain-platform-replay-publication-phase`, hosted-replay-policy). Dream Land's
  Whispy direction publication is untouched.

## Evidence and acceptance

- Native aggregate at this change: **429 pass / 59 classified / 0 fail / 0 error**
  (4,876,298 frames). Four entries turned exact and were retired (63 -> 59):
  `links/slippi-games-2025-05` 5,275 -> 5,375/5,375; `links/slippi-2025-08` 7,106 ->
  7,123/7,123; `ganon/24891` (35-row FoD throw-release `dolphin-ulp-motion-profile`) 11,140/
  11,140; `yoshi/slippi-2025-02` (`fod-release-floor-pick`, 358 rows) 12,828/12,828. No other
  replay's outcome or lock moved; the four locks were re-recorded from the aggregate run.
- The `fod-release-floor-pick` text had already named this seed ("same mp floor-line-pick seed
  as the probed dk auto-dk-2025-04 and ganon FoD throw-release entries"); dk `auto-dk-2025-04`
  is the Samus wall-hug ULP family, not a release resolve, and is unchanged.
- Gates on this host: `source-check`, `format-check`, `native-smoke`, pytest; PPC parity on
  the four retired replays recorded in the log below. Wasm/viewer unverified (no emcc).

## Log

- 2026-08-30 — `retained`: both fixes, four classifications retired, four locks re-recorded,
  two ledger rows. Native aggregate after the lock re-record: 433 pass / 59 classified / 0
  fail / 0 error. PPC (fresh `make ppc`, `--timeout 180`): all four retired replays PASS at
  full length (links 7,123 and 5,375; ganon 11,140; yoshi 12,828). `source-check`,
  `format-check`, `native-smoke`, pytest 47 passed. Trace recipe: temporary `fprintf` at the `ftcoll.c` item-vs-shield check
  (`x221B_*`, `shield_hit` world position via `lb_8000B1CC`, `lbColl_80007BCC` result) and at
  `ftColl_80077688`; at `Item_80269DC8` (`xDCE_flag.b4/b5`, `xC58`); at `ftCo_800DDDE4`
  (thrower `cur_pos`/root translate, release vec, resolve output); and a same-file order trace
  in `msl_ground_headless_epoch_proc`, `Fighter_8006A360`, `grIzumi_801CC358`, and
  `Fighter_procMap` with `gm_8016AEDC()` as the frame stamp (Slippi id + 123).

# Correctness lead — the classic UCF shield drop is two rollouts (2026-08-26, landed)

## Objective

Close the one open residual that carried a named gameplay mechanism:
`held-down-shield-press-escape-decision` (marth `WingedGorgeousPanther` @9793, P3 Falco
shielding off a held-down rim press on the frozen Stadium main floor where the hosted
build spot-dodged). The ledgered suspect — the `x671` down-tilt timer arming a frame late —
was wrong; the vanilla gate is exact.

## What the retail probe showed

Two engine-dump probes (`reports/triage/run_hookshot_probe.py`, out under
`reports/triage/marth_escape_probe{,_b}/`) on frames 9787..9795 with PC hits at
`Fighter_8006AD10`, `ft_8008A348`, `ftCo_Wait_IASA`, `ftCo_80099794`, `ftCo_80099894`,
`ftCo_80091A4C`:

- Retail's `x671` is 0/1/2/3 on 9790..9793 exactly as hosted (reset when `lstick.y`
  crosses `-xC` at 9790); `held` carries L throughout (the trigger was held since the
  tech, not pressed at 9793); `x670` is 6; `cstick` is 0.
- At 9793 the PassiveStandF anim end runs `ft_8008A348` -> Wait, then `Wait_IASA` calls
  `ftCo_80099794`, which **enters `ftCo_80099894`** (the vanilla gate passes: `-0.7125 <=
  x314`, `3 < x318`) — and yet `ftCo_80091A4C` runs next and the frame ends in GuardOn.
- `coll_data.floor.index` is 34 with `floor.flags` **0** (main floor), so the UCF 0.84
  shield-drop code (`External/UCF 0.84/UCF/UCF Shield Drop.asm`: `floor.index != -1 &&
  floor.flags & 0x100`, i.e. `mpColl_IsOnPlatform`) could not have suppressed it.
- The replay's own embedded gecko list (event 0x3D via 0x10 splitters) carries a
  304-byte code at `800998A4` that is `External/UCF 0.8/Logic/UCF SD.asm`: c-stick gate,
  a float rim test (`((int(|v|*80 - 0x37270000) + 2) / 80)` per axis, squares summed
  `>= 1.0` in single precision), `x670 > 3`, `lstick.y > -0.8`, **no platform test**;
  its suppress path pops `ftCo_80099894`'s frame and returns to the caller's `li r3,0`,
  which is why the spot-dodge check reports false and the Guard check follows.

## Final boundary

- **Owner:** `src/runtime/match.c::msl_ucf_suppress_spotdodge` selects between the two
  codes on `ucf_shield_drop_084_enabled` (new `MslCoreMatchRules` field, wire byte 32,
  `MslCoreMatchConfig` 53 -> 54 bytes, `MslCoreStreamJobHeader` 109 -> 110). On = the 0.84
  platform-gated integer rim test (unchanged); off = the 0.8 float rim test everywhere.
- **Plumbing:** `wire.h/.c`, `match.h/.c`, `scalar.c`, `api.c` (production = 0.84),
  `tools/validation/native.c` (both entry points), `suite_io.py`, `validate_replay.py`,
  the four C smokes/benches, `tools/viewer/schema.c` + regenerated
  `schema.generated.js` + `sim.js`. Suite entries carry
  `"ucf_shield_drop_084_enabled": false` per replay, defaulting true.
- **Corpus profile:** a scan of every validation replay's embedded gecko list
  (`reports/triage/gecko_scan.txt`, driver `reports/triage/gecko_scan.py` — the 0x800998A4
  body is 200 bytes for 0.84 and 304 bytes for 0.8; only runtime scratch words, the
  `backup` frame size and the branch-back differ within a version) finds **16 recordings
  with the 0.8 code**, and each of those sixteen also matches none of the other 0.84
  bundle files (their dashback/SDI codes are the older bundle too — an open lead if any of
  them ever forks on a dashback/SDI decision). Flagged: falcon OddballCleanViper; icies
  master-diamond, platinum-platinum; luigi slippi_01.07.22; marios 6082, platinum-platinum,
  ranked-anonymized platinum-platinum, Slippi-20220321; marth BreakableMundaneElephant,
  FemaleWorthyAlpaca, WingedGorgeousPanther; peach HeartyStiffMallard, ScaryFrankPorcupine;
  pikachu diamond-platinum; puff master-diamond; samus diamond-platinum.
- **Ledger:** `canonical:ucf-classic-shield-drop-version-flag` in
  `src/upstream_delta_ledger.tsv`.

## Evidence and acceptance

- `WingedGorgeousPanther` 9,915 -> **10,625/10,625 exact**; `peach/ScaryFrankPorcupine`
  (classified `dolphin-ulp-item-motion-profile` @25891, 38 rows) turned out to be the
  same fork and is **26,844/26,844 exact**. Both classifications retired (65 -> 63) and
  their locks re-recorded (`008c3a4a80f7a6dc` -> `a2f43edd9a4b6306`, `916b55fd67d0ab1a`
  -> `ea018e8ffbc9f96e`); no other lock moved.
- Native aggregate: **429 pass / 63 classified / 0 fail / 0 error** (4,876,298 frames).
  Every other flagged replay reproduces its previous outcome, so the 0.8 predicate is
  corpus-neutral except where it closes forks.
- Gates on this host (gcc 15.2, recorded lock-clean): `source-check`, `format-check`,
  `native-smoke`, `python-library`; the Wasm/viewer targets are unverified (no emcc) but
  the schema regenerated cleanly and `sim.js` writes the new byte.
- Trap noted while triaging: running `validate_replay` on a bare positional replay path
  does not apply the suite's per-entry UCF profile, so `WingedGorgeousPanther` falsely
  forks at 3864 (`ucf_cardinals_1_0_enabled` false in its suite entry). Use `--suite`.
- `src/api.c` had set the production profile without `ucf_shield_drop_extended_enabled`
  (it stayed 0 from the memset), so production RL ran the classic shield drop only; at the
  user's direction the extended (counter-based) shield drop is now on in production too,
  matching current Slippi.

## Log

- 2026-08-26 — `retained`: probes, corpus scan, flag, suite profiles, classification
  retirement, lock re-record, ledger row. PPC parity (binary rebuilt for the 54-byte
  config): marth 17 pass / 2 classified / 0 fail with WingedGorgeousPanther PASS;
  ScaryFrankPorcupine PASS 26,844/26,844. The peach suite's other ten PPC "fails" are
  classification entries that carry only a `native` snapshot; peach was never
  PPC-recorded. (Corrected 2026-08-28: only eight of the ten PPC fingerprints equal
  native, e.g. TameEmbellishedSparrow 65e7d85cca69a2b5; CandidThankfulCockroach and
  CapitalPristineTarsier differ — see the 2026-08-28 entry.) pytest 47 passed.
  Committed as `61a2d260`.
- 2026-08-28 — `retained`: peach and marios PPC-recorded. `make ppc` on `5c0a91ac`, then
  per-suite PPC with `VALIDATION_WORKERS=8 --timeout 180` (the auto 16-worker default trips
  the 30s PPC timeout on 15k+ frame replays and reports them as `error`). peach PPC
  9 pass / 1 classified / 10 unrecorded before; eight of the ten PPC fingerprints equal
  native and are now `"ppc": "native"` aliases. Two differ and carry their own PPC snapshot:
  CandidThankfulCockroach (native 17b7be538e5e236a / 62 rows -> PPC 6002eb36f1ec85ea /
  120 rows) and CapitalPristineTarsier (851c665507f1483b / 60 -> b3f57dbd9b3539c0 / 117).
  Both are the ledgered PPC-wider `dolphin-ulp-motion-profile` shape (cf. sheik
  GlaringRosyAlpaca 38/117, WavyRundownAardvark 50/107): a ~120-row 1-ULP root-position
  window on the airborne port-2 fighter (y ~127..160) that starts 1-3 frames earlier on
  PPC; percent residuals and strict suffixes are identical across backends. marios PPC
  44 pass / 6 unrecorded, all six fingerprints equal native -> aliases. After recording:
  peach native and PPC 9 pass / 11 classified / 0 fail; marios native and PPC 44 pass /
  6 classified / 0 fail. Still native-only: aggregate_recent (2), battlefield_recent (1),
  pokemon_stadium_recent (1). Logs: `reports/triage/{peach,marios}-ppc-head-5c0a91ac.txt`.
- 2026-08-26 — `retained` (`a3158608`, follow-up): the production profile now matches the
  Slippi netplay injection list (`Output/InjectionLists/list_netplay.json`: the full UCF
  0.84 bundle) — `src/api.c` turns on the extended shield drop, and the Python package's
  `ucf_cardinals_1_0_enabled` default (`melee_sim/config.py`, `env_batch.py`) flips to
  True so bots train under 1.0 cardinals as they would see online. Validation suites are
  untouched (per-entry flags).

# Structural packet — per-fighter sealed-arena reserves (`decomp-port-mem`, landed)

## Objective

Stop the RL fleet's post-seal arena aborts. Every runtime pool reserve that
`msl_core_match_reset` buys before `msl_memory_finish_initialization` seals the Match
arena was a flat match-wide constant sized against one port's worst case (or, for FObj,
a `has_samus ? 512 : 256` branch). Four ports can each contribute the same article burst,
so the fleet hit `HSD_ObjAllocAddFree` / `HSD_MemAlloc` "arena allocation failed" aborts
mid-match on ordinary lineups: four Ness (FObj, PK Flash detonations), four Samus air
grapple-catches (GObj -> class mem-piece -> AObj -> ItemLink in turn), four Pikachu
(item pool at Thunder's four articles), Ness vs Ice Climbers on Yoshi's Story (item pool:
seven-item PK Thunder + five Blizzard puffs + stage Shy Guys), a four-Ice-Climbers
mirror (pose track arena at 1058 live tracks), and Peach vs Link on Dream Land (the
192-byte JObj mem-piece class under a lazily loaded aerial hookshot). The packet is
landed across commits `066cffdd`..`3cd7ead3` (2026-08-07..08-17); this section is its
ledger, written after the fact on 2026-08-26.

## Final boundary

- **Owner:** the reserve block in `src/runtime/scalar.c::msl_core_match_reset` (the
  `HSD_ObjAllocEnsureFree` calls, `msl_item_reserve_runtime_pools`,
  `hsdPreallocateMemPieces`, and the new `hsdPreallocateMemPiecesForClass` in
  `src/sysdolphin/baselib/class.c`), plus the per-player pose arenas in
  `src/runtime/fighter_pose.h`. Every reserve is a per-fighter term summed over the
  configured ports (bursts from different fighters overlap in time, so the operator is a
  sum, never a maximum), plus a stage term where the stage itself spawns.
- **Canonical state:** none added. The counts (`samus_count`, `ness_count`, `link_count`
  for Link/Young Link, `sheik_count` for Sheik and transform-capable Zelda, `ics_count`,
  `peach_count`, the accumulated `item_reserve`) are reset-time locals derived from
  `match->config`; the pools themselves are the pre-existing HSD ObjAlloc/mem-piece owners.
- **Consumers:** every sealed Match (native, PPC, Wasm share the reserve code on the
  `MSL_CORE_NATIVE` path), savestate size, and the runtime census.
- **Displaced:** the flat constants, the `has_samus` FObj branch, the flat 1024-track pose
  arena, and sizing the item pool from `MSL_CORE_MAX_ITEMS` (the observation array's
  width — `msl_core_match_write_items` publishes the first fifteen live items and stops —
  not a gameplay limit; the pool is now independent of it).
- **Deletion boundary:** no post-seal growth path, no fallback allocation, no lineup
  special-casing beyond the per-fighter terms. Reserves are validated only by measured
  high water against real capacity with a 20% headroom bar; no reserve is raised without
  a scenario that reaches the burst and proves the articles were live.

## The reserve terms (all `refs/melee`-owned pools, measured with the reserve raised)

| Pool | Reserve | Measured worst (four-port unless noted) |
|---|---|---|
| AObj (`HSD_AObjGetAllocData`) | 128 + 64/Samus | 151 live, four-Samus air grapple-catch (5/46/91/151 for 0/1/2/4) |
| FObj | 256 + 128/Ness + 256/Samus + 32/Link + 32/Peach | 359 four-Ness (~80 per PK Flash detonation); 362 four-Samus ground (452 air); 356..405 Ness x2 + Samus x2; 258 four-Link; 240 four-Peach |
| IDEntry | 128 (flat) | peaked <= 78% roster-wide, left alone |
| RObj | 16 + 8/Link | 16 of the former 16 for four Link (4 RObjs each vs 2 for every other fighter) |
| GObj | 128 + 64/Samus + 32/Link + 32/Sheik(+Zelda) + 64/Ice Climbers | 170 four-Samus air; 184 four-Climbers Belay (aborted at HEAD before) |
| GObjProc | 256 + 64/Ness | 365 live procs in a four-Ness chaos soak (aborted the flat 256); 267 for Ness x2 + Pikachu x2 |
| Mtx | 256 + 32/Samus | 240 of 256 four-Samus chaos |
| Vec, dynamic-bone | flat (128 / item-bound) | <= 78%, left alone |
| ItemLink | 151 + 64/Samus + 24/Link + 32/Sheik(+Zelda) + 48/Ice Climbers | 150 of 151 four-Samus air (99%); 160 four-Climbers Belay |
| Item pool | per fighter: 10 Ness/Ice Climbers, 8 Sheik/Zelda/Pikachu/Samus/Link/Young Link, 6 others; + 8 flat on Yoshi's Story | 16 four-Pikachu Thunder (structural at every cadence 20..120); 17 Ness vs Climbers on Yoshi's (7 + 5 + Shy Guy waves of 3..5) |
| Class mem-pieces, generic | 128 flat (`MAXIMUM_RESERVE` 256 -> 512) | small classes' churn only |
| JObj class (192 B) floor | 128 + 128/port (`hsdPreallocateMemPiecesForClass`, also forces the bucket to exist) | growth over seal: 98 Peach-Link (the RL crash, former flat 128), 124..146 four-Link/-Ness/-Climbers, 395 four-Samus (scripted air catch minimum 320) |
| Pose joints | 256/player (pre-existing) | 1,016 four-Sheik (census) |
| Pose tracks | 384/player (was flat 1024) | 1,058 four-Ice-Climbers (~265 per port across Popo and Nana); Zelda next at 446 |

## Evidence and acceptance

- `tests/melee_core/article_pool_smoke.c` (in `native-smoke`) drives thirteen scripted
  scenarios — four-ness-dreamland, four-samus-air-fd, ness-samus-dreamland,
  ness-link-dreamland, four-link-fd, four-pikachu-fd, four-ics-fd, ness-ics-yoshis, the
  cold-tether set (four-link-zair-cold-fd, four-ylink-zair-cold-fd, four-sheik-chain-fd,
  peach-link-zair-cold-dreamland, four-ics-belay-cold-fd) — each asserting the burst
  actually happened (live detonations, beams, RObjs, pool-counted items, tethers) and
  that every pool kept 20% headroom. The cold-tether scenarios matter because a
  move-cycling driver warms the free lists with earlier spawns and never reaches the
  seal-state lists an RL policy hits by jumping and pressing Z. Reverting each reserve
  was checked separately: each reproduces its abort or fails the headroom bar.
- `tests/melee_core/pool_chaos_soak.c` (built on demand, not in `native-smoke`): a
  deterministic 60,000-frame pseudo-random soak that prints every pool's and class
  bucket's high water; a fifteen-lineup audit shows no aborts and every pool under 80%
  of capacity, and it is the instrument for any future reserve change.
- Runtime census (`make runtime-census`, 20 characters x 6 stages x 2/4 players):
  maximum arena use 973,328 -> 1,026,296 -> 1,132,368 -> 1,195,716 -> 1,311,096 of
  3,145,728 across the series (worst config four Sheik on Yoshi's Story); relocation
  records 5,371 of 16,384; pose joints 1,016 of 1,024. The ordinary two-player lock is
  608,100 arena bytes / 868 allocations before and after gameplay, savestate 671,140
  bytes (the sized-per-player track arena more than pays for the per-port item and
  JObj terms at two ports; four-player matches pay the rest).
- Gates at each commit: source-check (until the Link-era lock drift, fixed 2026-08-26),
  native-smoke, ppc, python-library; the Wasm target was unverified on this host (no
  emcc) — the change is plain C on the shared path. Replay locks and classified
  fingerprints did not move (pool growth is layout-neutral for the corpus; the
  `b1955a02` preload-shift precedent did not recur).

## Log

- 2026-08-07 — `retained` (`066cffdd`, `b7aefa05`): FObj per Ness (four-Ness aborted at
  359 against 283), then the composition fixed from maximum to sum with Samus as another
  per-fighter term; the `has_samus` branch is deleted.
- 2026-08-07 — `retained` (`19c849be`, `0ff5bb19`): AObj/GObj/mem-piece/ItemLink per
  Samus (four-Samus air grapple-catch aborted through four pools in turn); a twenty-
  fighter four-port sweep of every remaining flat reserve found RObj at 16/16 (four Link)
  and the item pool aborting at sixteen (four Pikachu); the other five flat reserves
  peaked <= 78% and stay.
- 2026-08-07 — `retained` (`65bf041c`): AGENTS.md roster brought to twenty and the
  per-fighter-reserve rule recorded as a convention.
- 2026-08-09 — `retained` (`a4eafdf2`, `de75ca49`): pose track arena 384/player (four
  Ice Climbers asserted `allocate_tracks` at 1058 live); item pool moved from a flat
  eight per port to measured per-fighter rates plus a Yoshi's Story Shy Guy term after
  an RL abort in `itClimbersBlizzard_Spawn` (Ness vs Climbers, seventeen live items).
- 2026-08-17 — `retained` (`3cd7ead3`): the 192-byte JObj mem-piece class gets a per-port
  floor that also creates the bucket before the seal (an RL abort in
  `ftCo_AirCatch_Anim -> it_link_get_joint -> HSD_JObjLoadJoint`, Peach vs Link, Dream
  Land); GObjProc per Ness, GObj/ItemLink per Link/Sheik/Climbers, Mtx per Samus, FObj
  per Link/Peach from the new chaos soak, which found three latent aborts beside the
  reported one.
- 2026-08-26 — `retained`: ledger written; `CURRENT_BASELINE.md`'s memory-contract table
  refreshed from the census at this HEAD (it had carried the 2026-07-19 figures through
  the whole series).
- 2026-08-26 — `retained` (throughput/digest check of the packet): control `d59b65ba`
  (pre-packet) versus candidate `3cd7ead3` (packet head, before the partner-arm gameplay
  fix), same release profile, the retained 153-replay eight-character 256-batch workload,
  three alternating samples each on CPU 0 of this host. Digests are identical on every
  run (`474828382690a770`), so the reserves are gameplay-neutral. Median cycles/frame
  129,981.2 control versus 132,630.9 candidate (+2.0%), with samples 126,658..154,144 and
  128,807..136,931 — inside this host's run-to-run spread (a Ryzen 9 3950X shared with
  other jobs, load about 3), so no material cost is claimed or excluded beyond that. The
  absolute figures are not comparable to the retained 9950X3D medians; see
  `CURRENT_BASELINE.md`.

# Previous structural packet — Link and Young Link

## Objective

Add Link (internal kind FTKIND_LINK 6, public char id 6) and Young Link (FTKIND_CLINK 20,
public char id 20) to the supported domain with a 60-replay suite (five per stage for each
character; mirrors and Link-vs-Young-Link games cover both). Link is a full original: six
chara TUs (ftLk_Init, the Hylian-shield/down-air-bounce ftLk_AttackAir, bow ftLk_SpecialN,
boomerang ftLk_SpecialS, spin attack ftLk_SpecialHi, bomb pull ftLk_SpecialLw). Young Link
is the Link clone: ftCl_Init delegates 19 of 21 motion rows to ftLk handlers and adds the
milk taunt ftCl_AppealS; his OnLoad also sets can_walljump. Shared article TUs own both
kind pairs: itlinkbomb (58/59), itlinkboomerang (60/61), itlinkhookshot (62/63, previously
imported for the Samus/Sheik/Icies tether family and now given its missing manifest row),
itlinkarrow (64/65), itlinkbow (76/77), and itclinkmilk (123, Young Link only).

## Final boundary

- **Owners:** the fourteen imported TUs byte-identical to the pinned decomp except the
  ledgered deltas below; `it_803F3100`/`it_803F2F28` own the eleven new logic rows
  (including retail's Link-vs-Young-Link hookshot second-slot asymmetry);
  `MslDatLinkArticles` (shared by PlLk/PlCl) owns the seven-slot x48 list: five articles,
  the milk slot (NULL in PlLk), and the sword HSD_Joint accessory.
- **Data:** PlLk*/PlCl* (five costumes each, Nr/Re/Bu/Bk/Wh, plus AJ and DViWaitAJ) +
  EfLkData.dat; manifest 173 -> 190 files. Anim count 314 for both; ftData_UnkIntPairs
  {0,14}; both kinds map to efAsync bank 6.
- **Effects:** EfLkData.dat is MODEL-ONLY (both leading effLinkDataTable words are
  unrelocated NULLs, the EfDkData/EfNsData shape) and there is no EfClData.dat — the two
  kinds share bank 6. The spin-attack efSync ids 0x4BB/0x4BC are pure efLib model creates
  on bank-6 model ids 0x1770..0x1773; the item-side ids (0x448 arrow sparkle,
  0x41C/0x3F1 hookshot) resolve into the already-loaded common bank 0, so no generator
  RNG projection consumes bank-6 ids.
- **Parts:** derived rows 46 live / 30 cold of 76 (Link) and 46 live / 34 cold of 80
  (Young Link), with CODE_ANCHORED spin-attack joints (raw parts[24]/[26]), the
  GetBoneIndex left thumbs (35/37 — bomb, boomerang, and milk spawn anchors), and the
  sword accessory attach/part pairs (67+68 / 71+72). The accessory part must stay live so
  retail's ftAnim_8006E7B8 tree/part pairing, which counts the attached accessory node,
  stays aligned in the compact hosted tree. The hookshot's parts[139] chain-joint store
  is a runtime-anchored article slot like Samus's zair beam tip and stays outside the
  admission mask. The derive tool's Fighter_804D6540 parsing was corrected (4-byte
  {part, attach, mode, depth} records; only Kirby/Link/CLink have entries, so no prior
  mask is affected).

## Hosted representation decisions (all ledgered)

- **Hookshot attr scratch mirror** (`canonical:link-hookshot-attr-scratch-mirror`):
  it_link_attr_math recomputes the x2C..x48 attribute lanes in place at every hookshot
  spawn — retail treats the shared DAT blob as scratch. Each match owns one writable
  mirror per kind in MslSourceMatchState, seeded from the article on first use, exactly
  the samus_grapple precedent; all 14 fetch sites route through it.
- **ftLk_DatAttrs pointer-width lanes** (`canonical:link-datattrs-pointer-width`):
  x94/x9C/xA0 were UNK_T; ftCo_Attack100.c's ftCo_LinkCatchAttrs view types the same
  lanes s32, and the widened pointers shifted every later lane (da->xBC read 10 instead
  of the hookshot kind 62). Typed s32, matching the yoshi-datattrs-pointer-width
  precedent.
- **Sword accessory joint identity** (`canonical:sword-accessory-joint-identity`):
  ftParts_800753D4's retail stack copy of the isolated accessory joint becomes a Match
  arena copy at fighter construction so both loads share one relocatable HSD id.
- **OnLoad ext-attr store guards** (`canonical:link-onload-attr-store-guard`): both
  OnLoads re-derive attackairlw_hit_anim_frame_end into the sealed DAT page; the
  idempotent re-store is skipped, the donkey-onload-attr-store-guard shape.
- **Boomerang self-stores** (`canonical:link-boomerang-attr-self-store`): three retail
  `attrs->xC = attrs->xC` self-stores skipped against the sealed arena.
- **Article span-packing exemption** (native_dat.c): PlLk.dat packs an unreferenced
  hookshot joint blob between the boomerang Article and the next relocation target;
  Articles are now never span-packed (each is a single object referenced slot-by-slot).
- **Archive cache capacity** 4096 -> 8192: every fighter animation figatree slice takes
  an entry, so the census scales with summed anim counts; the 18-character domain peaked
  just under 4096.
- **ft_8008A348's kind switch unguarded**: the hosted branch had excluded the
  Link/CLink Hylian-shield arm from the Wait-enter path back when the callees were
  unsupported-fighter stubs; the SquatWait twin was already live.

## Pre-existing gaps closed while admitting

- `api.c::supported_character` was missing Ness (neither Ness commit updated it).
- `Makefile` VALIDATION_CHARACTERS was missing Ness.
- The viewer's `externalCharId` was missing Ness (8->11) and Yoshi (14->17), so both
  rendered the wrong slippilab zip; Young Link needs 20->21 and Link is identity.
- `itlinkhookshot.c` was compiled without a source_manifest row.

## Suite state — GATE (54 pass + 6 classified / 0 fail, both backends)

Staged 60 replays (screened from link_ylink_replays.zip: all singles, two human ports,
supported opponents, six legal stages, populated raw analog pad lanes, zero SHA
duplicates; all ten Stadium entries event-verified frozen — 0x41 declared, zero events).
`replays/suites/links.json` IS in the aggregate (426 -> 486 replays; test counts bumped
in test_aggregate_suite_coverage and test_melee_core_validation_runner), all 60 output
locks recorded, and the six residuals classified with both-backend snapshots
(native == ppc bit-for-bit on every one): redxlink_0310 and slippi-2025-08_0815 under
`hitlag-accumulated-pad-edge` (the Ness partner-arm debt, now measured victim-side:
the thrown victim's kb_applied is nonzero for dthrow item hits where retail's stays 0,
so the victim drives ftCo_8008EC90's partner arm and the tech press during laser
hitlag is swallowed), slippi-games-2025-05 under a new `link-boomerang-deflect-fork`
id (hosted UnkMotion2_Coll env_flags stay 0 through retail's surface deflect; needs
the Dolphin item-collision probe), and 18932 / slippi-2025-01 / slpfiles-2025-10_1020
under `dolphin-ulp-motion-profile` (1-2 ULP self-healing seeds).

Historical note — the dominant families as first triaged (all since closed):

1. **Hookshot latch one frame late** (item.state 3-vs-1, a handful of rows per replay,
   plus the action 361-vs-360 zair catch rows): the chain-link count from
   it_link_attr_math's x2C derivation is suspected one off — the expression is a lerp
   (`t*a + (1-t)*b`, a classic MWCC fmadds pair) feeding an s32 link-count compare.
   The Link TU MWCC fused-op audit has NOT been done yet; the Ness packet's asm-vs-marker
   method applies (refs/melee/config/GALE01/symbols.txt sizes + DOL disassembly).
2. **Boomerang misc1 (xDD8) families** (hundreds of rows, e.g. 219-vs-132) and 1-2 ULP
   pos_x rows — same fusion audit territory (deceleration/turn math).
3. **Fighter-attack-vs-item capsule forks with identical exported geometry** — the
   second confirmed class: in slpfiles-2025-03 (BF Young Link ditto) at frame 41 the
   hosted sim lands a 7-damage fighter attack on an item (P1 hitlag 5, item.damage 7,
   item timer frozen) that retail never lands, while every exported position matches
   through frame 40 (the only live item is P2's arrow stuck at (42.65, 0) and P1 is
   50+ units away at (-9.47, 32.5)) — so a hosted item HURT capsule must be sitting at
   a phantom position. Item hurt capsules ride `article->x8_hurtbones` bone ids through
   `item->xBBC_dynamicBoneTable->bones[]` (it_8027163C) and their world positions come
   from item model sub-bone jobj matrices — the suspect is stale/unsolved item sub-bone
   matrices for the newly admitted article models (the arrow's x8_hurtbones is 0x9dd8 in
   PlLk/PlCl). The zair whip case is the mirror image (item-side hitbox vs fighter).
   Next: dump the arrow's xACC_itemHurtbox capsule world positions at frames 39..42 in
   the hosted run and find whose matrix feeds the phantom.
4. Various replays still fail in the first ~1,500 frames — untriaged beyond the above.

## Session goal and standing (2026-08-04, in progress)

**Goal: 20/30 passing per character.** Current native: 11 pass / 49 fail (Link 6/31,
Young Link 5/34 counting mirrors both ways). PPC oracle: 31 pass / 29 fail — the
cross-partition is 11 both-pass / 20 native-only-fail (HOST-WIDTH class) / 29 both-fail
(true logic). Work the width class down first (mechanical, ~+15 passes), then the logic
class.

Width-class leads (native-only fails), from the 20-replay first-mismatch census:
- RESOLVED (was the top width family): `action 43` was ftCo_MS_LandingFallSpecial, not Turn — the spin-attack landing lag read via the Fighter-cast attr walk (facing_dir1 = ftLk_DatAttrs::x30); fixed and ledgered, 11 -> 31 passes.
- (stale text below kept for provenance) `action_id expected=43 (Turn) actual=14/18/178` in ~10 replays — the hosted native
  build ABORTS a Turn after one frame (43 -> Wait at e.g. slpfiles-2025-05_0518 frame
  1646, entered from the bomb-pull end 357) because ftAnim_IsFramesRemaining goes
  false; PPC running the identical hosted C keeps the Turn and passes, so an upstream
  width-poisoned lane (anim rate or track state, likely set during the bomb-pull /
  item-hold path) differs between the backends. Frames-limited runs (frames=1700) did
  NOT reproduce the fork while full runs do, deterministically — the input-tape length
  affects the divergence, which itself smells like reading past prepared state. Next:
  frame-stamped dual-backend traces of the ChangeMotionState args (rate/start) for the
  1645 Turn entry and of the bomb-pull end transition.
- `pos_x` one-frame ±0.12..0.18 nudges in ~9 replays — looks like fighter-vs-item
  jostle push. Both smell like more raw-offset or width-shifted reads; sweep remaining
  `(u8*) fp +`/pad-overlay casts and item-side field views in ftCommon/it TUs.

Logic-class leads (both-backend fails):
- The grab/pummel mash divergence = the Ness packet's `hitlag-accumulated-pad-edge`
  debt, now load-bearing (e.g. redxlink_0310 @301: retail Falco throw connects, hosted
  victim escapes earlier). Progress on the deferred question: retail's pummeled victim
  exits ftCo_Damage's top gate (`!fp->dmg.kb_applied` — a pummel applies no knockback),
  so the capture-partner arm must run on the GRABBER's side (fp=grabber, other=victim),
  clearing the VICTIM's x668 — matching the retail probe (victim x668=0, victim b5
  clear, x183C_applied==0 skips the b5 block). The hosted build instead runs the arm
  with fp=victim (its kb_applied is set where retail's is not) and clears the grabber.
  Root-cause why hosted computes kb_applied for a pummeled held victim.
- The hookshot latch one-frame-late family persists in some replays (item.state 3-vs-1,
  361-vs-360): the probe table (reports/triage/link_hookshot_probe.md) shows retail
  anchors + steps the head on the it_802A78B8 fire frame, steps one vel/frame, freezes
  during owner hitlag, latches after the census; the launch-speed fix (tether mirror)
  closed most of it — re-census which replays still carry it.
- 1–2 ULP families: 18932 item.pos_x 2-ULP episode; redxlink_0221 percent 1-ULP at a
  damage application (staling multiply chain is the suspect).

**GOAL MET (2026-08-05): native and PPC both 54 pass + 6 classified / 0 fail — Link
27/31 passing, Young Link 32/34 passing** (goal was 20/30 each). The three closing
fixes: (1) `link-hookshot-chain-solver-gekko-rounding` — it_802A6474's four sum-of-
squares fmadds plus the Gekko frsqrte sqrt (fused fnmsub Newton terms) replacing libc
sqrtf across the hookshot TU, which the 32106 frame-1649 retail probe localized to the
latched-hang chain-constraint (self_vel += cur_pos − pos_2 amplifies knife-edge ULPs);
this flipped 20 replays including the whole offstage-fall family. (2)
`thrown-item-damage-gekko-rounding` — it_8026B1D4's speed-scaled thrown-item damage
(Gekko sqrt + fmadds fold), closing the three percent-ULP residuals. (3)
`lbvector-cosangle-gekko-rounding` — retail-faithful CosAngle (behavior-neutral at the
current corpus, gates the boomerang deflect branch). Full native aggregate green: 396
pass + 90 classified / 0 fail across 4.81M frames; the two icies pool-residue analogs
were re-recorded for the Link preload shift (b1955a02 precedent).

(Stale text below kept for provenance.) Standing after the landing-lag fix: native 31 pass / 29 fail — Link 16/31, Young
Link 16/34 (goal 20/30 each; need +4 per side from the 29 both-backend fails). The 29
are almost all SMALL self-healing episodes now: three 1-row fails (redxlink_0221 percent
1-ULP at a damage application — staling-multiply suspect; slippi-2025-01 pos_x 1 row;
slippi-2025-08_0805 pos_y 1 row), then 2..31-row pos_y/speed_y episodes. The dominant
recurring shape: a 1-ULP pos_y with a ~32-ULP speed_y_self in fighter state 361
(ftLk_MS_AirCatchHit — the offstage zair catch fall, e.g. 32106 @1649, slpfiles-2025-10
@1650): ftCo_AirCatchHit_Phys runs `self_vel = pos_delta` + shared ftCommon_Fall, so the
seed is the catch-hit ENTRY velocity, fed by the hookshot chain geometry (the pin-walk
normalize margins measured as razor-thin earlier, e.g. +2.4e-7 at one boundary). Range
audits confirm ZERO fused ops in the whole ftLk/ftCl address ranges and none reachable
in ftCo_AirCatch (the two fmadds nearby belong to ftCo_DamageBind's 800C4550). MEASURED (32106): bit-hex dual-backend traces of ftCo_AirCatchHit_Phys inputs
(pos_delta, self_vel, cur_pos) are IDENTICAL between native and PPC for all 41
state-361 frames — including frames after the 1649 pos_y[0] 1-ULP export mismatch that
only native shows. Internal gameplay state never diverges (the wall-hug clamp
re-derives pos each frame, so nothing feeds back), which means the divergence is
confined to the exported coll-clamped position at the mismatch frames: either
mpColl_800471F8's clamp output ULP-differs between x86 and qemu-PPC without ever
persisting (suspect: helpers in runtime/math.o / MSL/trigf.o, the only -O2
-ffp-contract=fast -mfma objects, reached by a new PS-wall path), or the export
samples a lane (coll_data.cur_pos?) that differs from fp->cur_pos. Next: trace
end-of-frame fp->cur_pos vs coll_data.cur_pos vs the exported row for P1 frames
1645..1655 on both backends; then look at the mpColl wall projection expression.

Fixed this session (all committed): tether launch reads through the hookshot scratch
mirror (0 -> 11 passes; probe-verified retail derives the launch from the scaled x38
lane), the arrow spread-table pointer-width NaN (phantom item hits), the Attack100 raw
fp-offset overlays (fp+0x2340 mv lanes, fp+0x65C held inputs), the Hylian-shield arm
unguarded in ft_8008A348, thumb/accessory parts admission, and the bomb misc1 mask.

## Log

- 2026-08-23 — `closed` (partner-arm inversion): retail probe on 53362 frames 1378..1410
  (reports/triage/ness_partner_probe/README.md) showed retail ALSO runs ftCo_8008EC90 with
  fp = the pummeled victim (kb_applied 41.09 — a pummel does carry knockback; the earlier
  "bit 5 clear" reading was an MWCC MSB-first bitfield misread, 0x2219=0x07 has b5 SET), and
  that the victim's own x668 goes 0x100 -> 0 between the arm and the next input update. The
  DOL (0x8008F078..84) stores the zero to r29 = fp, not r27 = other_fp: the decomp line
  `other_fp->input.x668 = other_fp->input.x66C = 0` is a decomp error. Fixed to `fp->...`
  in ftCo_Damage.c. Effect: 53362 and redxlink_0310 become exact end-to-end (10,673 and
  8,157 rows), locks re-recorded (native==ppc); the third `hitlag-accumulated-pad-edge`
  entry (links slippi-2025-08, +0.1125 throw-release pos_y) was unaffected and is re-owned as
  `thrown-release-pose-offset` (same fingerprint, text-only). Native aggregate 492 = 427 pass
  / 65 classified / 0 fail; per-suite PPC ness 27/3/0 and links 55/5/0 (PPC needs
  `--timeout 180` with 8 workers on this host — the default 30 s trips on the longest
  replays). `make source-check` fails on the untouched HEAD too (refs/melee snapshot digest),
  unrelated. pytest 47 passed.

- 2026-08-05 — `closed` (goal met, suite gated): the retail probe on 32106 frames
  1643..1655 (hookshot latched to the Stadium wall) proved every frame-1649 input
  bit-shared and pinned the fork inside the chain-constraint step; the DOL showed the
  two hosted mis-emulations (unfused length dot products, libc sqrtf vs the Gekko
  frsqrte+fnmsub sequence) — fixing them took the suite 31 -> 51. The same class in
  it_8026B1D4 (thrown-item speed damage, traced via Fighter_TakeDamage bit-hex: raw
  dmg 0x40c820c3 vs retail 0x40c820c4) took it to 54. lbVector_CosAngle ported
  (behavior-neutral). Six residuals classified (both backends bit-identical), 60 locks
  recorded, links.json wired into the aggregate (486), test counts bumped, icies
  pool-residue analogs re-recorded for the Link preload shift. Native aggregate: 396
  pass + 90 classified / 0 fail. Open leads for a future pass: the capture/thrown
  partner-arm inversion (now measured from BOTH faces — grabber-side mash debt and
  victim-side kb_applied/tech-swallow; fix needs the full aggregate as its gate) and
  the boomerang deflect-fork Dolphin item-collision probe.

- 2026-08-04 — `checkpoint` (fused-op and mask batch): ported the audited MWCC fused-op
  set for the Link item TUs — the hookshot's distance dot products (a fused TU-local twin
  of it_802A3C98, whose out-of-line body must stay unfused for the cross-TU tether
  callers), all eighteen chain position-update triples, the chain-count lerp
  (GALE01 0x802A2680), the grip-scale double blend (0x802A2E28), the arrow's spread-angle
  draw/charge lerps/polar lanes, and the bomb drag step (0x8029F63C); the ftLk/ftCl chara
  TUs carry zero fused ops. Audit method: symbols.txt sizes + DOL disassembly via the
  PPC toolchain objdump (scripted asm census; every hookshot sqrt expansion fuses except
  the out-of-line it_802A3C98 body). Corrected the bomb misc1 mask to 0: xDDB is held-
  phase pool residue (retail exports 0x5F pre-match heap bytes) and a constant 0x00 after
  the thrown-transition sign write. Suite floor moved from 87 to ~1,130 matched frames;
  still 0 pass / 60 fail / 0 error. Dominant remaining forks: the hookshot latch running
  one frame late (item.state 3-vs-1 plus the action 361-vs-360 zair catch rows — the
  lerp port did not close it, so the census is now RULED OUT — a spawn trace shows x2C = 15 airborne / 22 for the 0x168 zair, and hand-evaluating retail's float chain gives the identical counts; the remaining lead is a ONE-STEP HEAD LAG: in slpfiles-2026-03_0330 the recording's P1 Young Link zair (state 360) lands attack 1 on P2 at frame -6, and a hosted hitbox trace shows our whip capsule riding the chain head correctly (jobj = parts[139]) but reaching P2's x one frame later; the same lag explains the latch-one-frame-late family (item.state 3-vs-1) and the 361-vs-360 zair catch rows. The state timelines themselves align (12654's state lanes match through 2909), so the head appears to gain its first velocity step one frame earlier in retail — spawn-frame or transition-frame processing order. Needs the Dolphin engine-dump probe (drivers on newchar-puff: tools/dolphin/*) for retail's per-frame head/hitbox positions) and it_802A40D0's collision step) and
  the boomerang flight/turn fork (xDD8 spin counters drift after the turn).

- 2026-08-04 — `open`: TU import + full registry/admission wiring + data extraction +
  suite staging landed; native-smoke, source-check, and the quick pytest set are green;
  the qemu-ppc toolchain fallback was cherry-picked from experiment/decomp-port at the
  user's direction. PPC parity, fusion audit, residual classification, aggregate wiring,
  and lock recording all remain.

# Previous structural packet — Ness

## Objective

Add Ness (internal kind FTKIND_NESS 8, public/CSS char id 8) to the supported domain
with an 18-replay suite. Ness is a full original: eight chara TUs (ftNs_Init, the Yo-Yo
smashes ftNs_AttackHi4/ftNs_AttackLw4, the baseball-bat ftNs_AttackS4, PK Flash
ftNs_SpecialN, PK Fire ftNs_SpecialS, PK Thunder + PK Thunder 2 ftNs_SpecialHi, PSI
Magnet ftNs_SpecialLw) and eight article TUs (itnesspkfire 66, itnesspkfirepillar 67,
itnesspkflash 68, itnesspkthunderball 69, itnesspkthundertrail 70..73,
itnesspkflashexplode 78, itnessbat 101, itnessyoyo 102). Ness is also the first
admitted absorber, so `ftData_OnAbsorb` becomes a live table.

## Final boundary

- **Owners:** the sixteen imported TUs byte-identical to the pinned decomp except the
  ledgered MWCC fusion set; `ftData_OnAbsorb` owns the PSI Magnet absorb callback;
  `MslDatNessArticles` owns the eleven-slot article list.
- **Data:** PlNs* (four costumes Nr/Ye/Bu/Gr + PlNsAJ + PlNsDViWaitAJ) + EfNsData.dat;
  manifest 165 -> 173 files. Anim count 326, ftData_UnkIntPairs 14,
  ftData_UnkBytePerCharacter 10.
- **Effects:** EfNsData.dat is MODEL-ONLY — both leading `effNessDataTable` words are
  unrelocated NULLs, the EfDkData.dat shape — so the hosted loader publishes an empty
  command bank for bank 10 and no generator RNG projection consumes bank-10 ids. Every
  Ness-reachable efSync id (0x4EE/0x4EF PK Thunder, 0x4F0 PSI Magnet) is a pure efLib
  model create; 0x406 was already modelled by the Yoshi packet.
- **Parts:** derived row 36 live / 27 cold of 63, with a new CODE_ANCHORED `ness: (61,)`
  for the raw `parts[61]` Yo-Yo hitbox transform in ftNs_AttackHi4.

## Exactness work shipped

- **Pose program-node id widened past sixteen characters** (the packet's one hosted
  representation change): `MslFighterPoseJoint.program_node_index` was an 18-bit field
  with a 0x3FFFF sentinel. Ness is the character that pushes the preloaded pose-node
  count to 265,533, past that ceiling, and every suite replay aborted on the
  fighter_pose.c admission assertion. The id is now a full `uint32_t`, which lands in
  the struct's existing 64-bit tail padding — the gate profile's `sizeof` is unchanged
  at 56 and only the 32-bit (PPC/Wasm) size moves 44 -> 48.
- **The Ness MWCC fused-op set: 42 sites across 12 TUs**, each carrying its GALE01
  address. The audit method is asm-vs-marker per function using the decomp's exact
  symbol sizes (`refs/melee/config/GALE01/symbols.txt`); remaining per-function gaps
  are all static helpers inlined into several callers (one C marker, several asm
  copies): `NessFloatMath_PKThunder2`, `ftNess_atan2`, `getAttrStuff`,
  `itNesspkflash_SetScale`, `it_802BF4A0_adjust_tail`, `ftNs_AttackHi4_YoyoApplyDamage`.
  The seed that motivated the sweep: **PK Thunder's turn-radius steps**
  (GALE01 0x802ABE08 fmadds / 0x802ABE24 fnmsubs). The ball's heading walks 90 degrees
  down to zero in fifteen 6-degree steps; only the fused rounding leaves retail's
  2^-28 residue where the unfused form collapses to an exact zero, so every steered
  PK Thunder diverged on its first horizontal frame. Porting those two sites alone took
  the suite from 0/18 to 7/18; the full set reached 11/18.
- **PK Flash's leading item lane is not gameplay state**: no owner writes
  `itPKFlush_ItemVars.xDD4` — not the constructor `it_802AAA80`, not any motion
  callback — so its sampled Slippi byte is fixed-pool residue and the projection mask
  keeps only MISC1 (the charge scale at xDD8).

## Suite state (container authoritative) — IN THE AGGREGATE

Superseded by "Second import" below: the suite is now 30 replays and the aggregate is
426 = 333/93/0, then by the linux-host certification section at the end: the aggregate is
now 426 = 342/84/0. The original eighteen-replay state is kept here for provenance.

18 replays across all six legal stages (three each) against Captain Falcon, Falco, Fox,
Jigglypuff, Marth, Peach, Pikachu, Sheik, and a Ness mirror. All three Pokemon Stadium
entries are event-verified frozen (each declares the 0x41 stadium_transformation
command and records zero events over the full game); every capture carries populated
raw analog pad lanes, so none is a 3.18-layout re-wrap. Zero replays were rejected.

**ness suite: native 12 pass / 6 classified / 0 fail; PPC 15 pass / 3 classified /
0 fail.** `ness.json` is wired into `melee_core_aggregate`, which is now
**414 = 327 pass / 87 classified / 0 fail / 0 error**, with the eighteen output locks
strictly additive (18 added, 0 changed) and the pre-Ness 396 byte-stable throughout.

### The six classified residuals, each root-caused

- `2026-06` (Dream Land Sheik/Ness) — **`unmodeled-presentation-rng-stream-phase`**.
  Settled with an engine-dump probe capture of retail's own RNG stream. Retail's frame
  3922 opens with a `randi` at LR 0x80211648, inside `grOldPupupu_802113E0`'s
  rand_range blow timer, *before* `efAsync_Dispatch`'s 0x3EC rotation draw. The hosted
  build deliberately replaces Dream Land's wind machine with the recorded Slippi
  direction stream (ledger `canonical:slippi-stage-event-publication`) precisely
  because the retail timers sample the mid-frame RNG after effect draws that do not
  exist headlessly — so the replacement consumes no RNG. That one-draw phase shift
  moves the following `it_2725_Logic109_DmgDealt` `HSD_Randi(3)` from the stream
  position returning 0 (the Sheik needle survives into its bounce state) to the
  neighbouring position returning 2 (destroyed). Every retail draw for the frame
  reproduces exactly from the recorded frame seed, including the surviving needle's
  `Randi(8)` = 5 bounce speed and its SetupBounce draws; the control frame 3919 is an
  identical needle hit with no Whispy draw and matches the hosted sequence draw for
  draw. Reinstating the retail machine is strictly worse — it forks fighter positions
  through the wind push, which is why it was replaced.
- `51444`, `2025-11`, `52757`, `53870` — **`unrecorded-item-pool-residue`**. The Ness
  Yo-Yo's `itNessYoyo_ItemVars.x4` is written by exactly one owner, `it_802BFEC4`, and
  only on the smash's final frame, so every frame the article is alive samples its
  pool slot's previous occupant. Traced end to end: the hosted sim runs all of a
  replay's Yo-Yo smashes on the same slot with retail's spawn ids and computes `x4`
  correctly (0x3D95C61D, low byte 29 — one of the values retail exports). `51444`
  additionally samples Mr. Saturn's `xDE4` y/z, which `itDosei_UnkMotion4`/`5` never
  write; the bytes the hosted run reports there are the halves of a 64-bit host
  pointer (the 0x00007FFF high word is plainly visible in the union). PPC, whose
  layout matches retail, is bit-exact on the three Yo-Yo-only replays.
- `2025-03` (Frozen Stadium Ness/Peach) — **`dolphin-ulp-motion-profile`**. One 1-ULP
  seed at frame 1471 where `speed_x_attack` and `speed_y_attack` take the same value,
  so the knockback angle is exactly 45 degrees and both lanes carry the single
  `scaled_kb` factor rather than any trig difference. Self-heals into a 3,958-frame
  exact suffix, same first row on both backends. The knockback formula owners
  `ftColl_80079AB0/80079C70/80079EA8` already carry their complete fused-op sets.

## Exactness fixes shipped while closing the residuals

- **Shield-damage fusion.** `slippi-2025-02` diverged by 1 ULP on `shield_hp` for
  2,033 rows. `Fighter_ProcessHit_8006D1EC` carries two unported MWCC fusions in the
  shield-damage expression (GALE01 0x8006D2AC and 0x8006D2CC): the light-shield lerp
  between `x2DC`/`x2E0`, and the `x284` scale over the `x288` floor; only the trailing
  subtraction from `shield_health` stays a plain `fsubs`. Porting both made that
  replay fully bit-exact, moved PPC from 14 to 15 passes, and left the 396-replay
  aggregate **byte-stable with zero output drift**.
- **Sakurai-angle ramp** (`ftCo_Damage_CalcAngle`, GALE01 0x8008D8AC) and
  **`ftCo_CalcYScaledKnockback`** (0x800CF678): asm-verified fusions, identity at
  current inputs, aggregate byte-stable.

## Residual investigation — the `2026-06` "missing item" family is an RNG-draw gap

Root-caused to a single missing RNG draw; the retail draw sequence is fully pinned.

The diverging item is not a Ness article at all: it is a **Sheik thrown needle**
(kind 79). Slippi restores the RNG seed every frame in this online capture (the
recorded seed steps by exactly 0x10000 per frame), and `frame_pre_random_seed`
is a compared lane that matches for the whole replay, so each frame's draw
stream starts identically and the divergence is strictly intra-frame.

At frame 3922 (seed 0x0FCDD4E3) the needle hits Ness and retail runs
`it_2725_Logic109_DmgDealt`, whose `HSD_Randi(3)` decides whether the needle
survives into state 4 or is destroyed. Walking the LCG forward from that seed
pins retail's sequence exactly, confirmed by three independent observables in
the replay's own item stream:

- draw 2 `Randi(3)` = 0 -> the needle survives,
- draw 4 `Randi(8)` = 5 -> `ABS(it_803F7020[5])` = 2.5 = retail's recorded vel_y,
- draws 5..10 `itSeakNeedleThrown_SetupBounce` -> `it_803F7000[5]` = +1.0 =
  retail's post-hitlag vel_x, and `it_803F7040[4]` = -0.2 = its recorded vel_y
  step.

Our frame 3922 makes five draws: `[0]` the 0x3EC hit effect's `HSD_Randf`,
`[1]` the needle's `Randi(3)` (= 2, so we destroy it), and `[2..4]` the three
`it_80278800_rand_vec` draws of the destruction effect that only happens
*because* we destroyed it. So **we are exactly one draw short before the
needle's `Randi(3)`** (retail has two consumers there, we have one), plus one
more between `Randi(3)` and `Randi(8)` that we never reach.

Ruled out along the way: the effect-generator RNG model is faithful — common
model 8's recorded generator 267 has `kind = 0x400100`, so the `kind & 0x100`
branch in `hsd_8039F05C` skips the draw in retail exactly as our predicate
does, and generator 63 has `random < 0`. The control frame 3919 is a needle
hit that both sims destroy with the identical five-draw sequence, so the
needle path itself is right; only frame 3922 carries the extra retail consumer.
Ness's `ftNs_Init_OnDamage` is not the source either: none of its four
articles (yo-yo, PK Flash, PK Thunder, bat) exists at that frame.

Next step is the Dolphin engine-dump RNG probe (`refs/README.md`) to name the
missing consumer; everything else about the episode is settled.

**Instrumentation trap (cost me two false conclusions):** the native backend
runs `melee-core-native` as a `--server` subprocess whose `stderr` is
`subprocess.PIPE` and never drained, so every temporary `fprintf` trace is
silently swallowed and the traced code looks unreachable. Set `stderr=None` in
`_NativeRunnerPool` while debugging. (`make validator` is separately required
for `item_projection.h` changes.)

## Residual investigation — the native-only Yo-Yo `misc1` family is pool residue

The three native-only failures (`2025-11`, `52757`, `53870`) all diverge on one
lane: `itNessYoyo_ItemVars.x4`, sampled as Slippi `misc1`. Traced end to end on
`52757`:

- `x4` is written by exactly one owner, `it_802BFEC4`, and only at the smash's
  final frame (`yoyoCurrentFrame == x44_UPSMASH_YOYO_NUDGE_FRAME` = 49, which is
  also the despawn frame). Nothing initializes it at spawn.
- Our sim performs all three of the replay's Yo-Yo up-smashes, walks
  `yoyoCurrentFrame` 3..49 each time, and computes `x4` correctly:
  0x3D95C61D / 0x3D95C609 / 0x3D95C61D. The low byte 0x1D = 29 is exactly one of
  the values retail exports for this lane, so the arithmetic owner is right.
- The item pointer is the same pool slot for all three yo-yos, and the spawn ids
  (51, 55, 72) match retail's.
- Every export **during** a yo-yo's life reads `x4 == 0` in our sim, because the
  single write lands on the last frame before the article despawns. Retail
  exports a stale non-zero value there, i.e. the lane is read as item-pool
  residue left by the slot's previous occupant.

PPC reproduces retail exactly on all three replays; only 64-bit native differs,
which is the signature of residue whose byte pattern depends on the preceding
occupant's item-variable layout (pointer-widened on the host). This is the same
mechanism as the existing `unrecorded-item-pool-residue` classification used for
the Ice Climbers entries, and the natural disposition is to classify it with
native and PPC snapshots rather than to "fix" it. Note `x4` is not purely an
export lane -- `it_802BF800` reads it during the swing -- so retail genuinely
consumes uninitialized memory here; in these three replays every fighter,
physics, and other item lane stays exact.

## Second import — twelve more captures, all admitted (suite 18 -> 30)

A second Ness pool (`ness_replays_2.zip`) was screened and integrated. All twelve
candidates passed admission: singles, two human ports, Ness present, all six legal
stages, versions 3.18.0/3.19.0, populated raw analog pad lanes on every capture (no
3.18-layout re-wraps), and both Pokemon Stadium entries event-verified frozen (0x41
declared, zero events over the full game). No SHA overlapped the existing eighteen.

**All twelve are in the gate.** The ness suite is **30 replays: 17 pass / 13 classified /
0 fail** on native and **24 pass / 6 classified / 0 fail** on PPC, back to five replays on
each of the six legal stages, and `melee_core_aggregate` is
**426 = 333 pass / 93 classified / 0 fail / 0 error**. Every lock was added
byte-additively; the only existing lock this work rewrote is `puff/specials`, whose
classification the standings fix retires. Five of the twelve are bit-exact on first
contact, six are new rows in two already-named families, and one (`53362`) is carried as
explicit debt under a new id.

### The six new residuals

- `2025-07`, `2025-11_20251116`, `66686`, `auto-falco-2025-12` --
  **`unrecorded-item-pool-residue`**, the established Yo-Yo `x4` lane. The diverging
  article is item type 102 on every mismatch row and only `item.misc1` moves. **All
  four are bit-exact on PPC**, which is the direct evidence that the lane is 64-bit
  layout residue rather than a gameplay fork. (In `2025-11_20251116` the Yo-Yo is item
  index 1 until a second article despawns at frame 8913 and index 0 afterwards, so both
  reported lanes are the same article.)
- `50321` -- same family, but this replay's first Yo-Yo spawns at frame 364, before any
  other article has occupied the pool slot. The hosted pool is zero there, so `misc1`
  reads 0 against the console's 82: the residue retail exports is pre-match heap content
  no hosted run can re-derive. PPC narrows it to that first spawn alone (104 rows ending
  at frame 467, leaving an 11,915-frame exact suffix) because its layout matches retail
  for every later Yo-Yo, so this entry carries its own PPC snapshot.
- `2025-02` -- **`dolphin-ulp-motion-profile`**. Peach's turnip (item type 99, id 132)
  is dropped at frame 23119 with its release y one ULP high (0x42006f2d against
  0x42006f2c). Nothing else diverges -- pos_x, both velocity lanes, timer, damage and
  every fighter lane stay exact -- so the constant-gravity fall carries the offset
  unchanged until the turnip crosses the blast zone and despawns at 23187, leaving a
  140-frame exact suffix. **Both backends produce the identical snapshot** (same first
  row, 69 rows, `f1639cfe9ff96f60`, same field digest), so it is a shared rounding step
  against Dolphin, not a host FP profile or layout artifact.

## RESOLVED — the grab-timer standings term (stock-mode scoring)

`49422_Game_20250130T192133` is **fixed and back in the suite**, bit-exact over all
12,422 frames on both backends. The chain, settled by retail probes:

- Ness is held in `CaptureWaitHi`; our sim broke the grab (grabber `CatchWait` 216 ->
  `CatchCut` 218, victim 224 -> `CaptureCut` 229) where retail held three more frames
  and threw. `ftCo_CaptureWaitHi_Anim`'s gate is `grab_timer <= 0` against
  `ftCo_804D90D0`, which the DOL confirms is exactly `0.0` with a real `<=`
  (`fcmpo` + `cror eq,lt,eq`, GALE01 0x800DB968).
- The seed value is
  `pct*x368 + (x358*(x35C - handicap) + x354) + x360*(x364 - (Player_80033BB8 + 1))`.
  Ness's percent is genuinely 0 at the grab and every capture runs handicap 9, so the
  standings term is the only lever: rank 0 seeds 75, rank 1 seeds 60.
- **Retail probe at 49422 frame 6118** (`ftCommon_InitGrab` entry, the
  `stfs f1,0x1A4C(r3)` at GALE01 0x8007DBCC): retail seeds **75** and
  `Player_80033BB8` returns **0**, where the hosted build computed 1.
- **Retail probe at 53362 frame 1374**: retail seeds **60** and the rank is **1**,
  matching the hosted build.

**The rule is stock count, not KOs minus falls.** `fn_8016588C` dispatches on the match
mode in `lbl_8046B6A0.x24C.x5`, and a probe of both queries reports **x5 = 1** -- the
stock-versus arm, not the KO-minus-falls default the hosted projection had ported. Mode 1
scores a live player by their remaining stocks and nothing else: GALE01 0x80165988 loads
`MatchPlayerData::stocks` with `lbz` and sign-extends it straight into the result. Only a
slot already out of stocks takes the second arm, where the score collapses to survival
time minus 0xFFFFFF (0x8016599C..0x801659B4) so an eliminated player always ranks last.

That explains both probes directly. At 49422 frame 6118 both players hold 2 stocks, so
the standings tie and Ness is not the loser -- while KOs minus falls put him behind,
because the Falcon's self-destruct denied him the KO credit the old expression scored by.
At 53362 frame 1374 the stocks are 4 against 3 and the rank is 1, which both rules agree
on. In matches without self-destructs the two formulas order players identically, which
is why the corpus never caught this.

**The block is refreshed live.** The same probe shows `gm_80166378` running at frame 6118
inside that very query, so the scene-guarded cache is repopulated on demand -- the value
retail served is the live one. An earlier freeze-at-first-query model was tried and
rejected: it fixes 49422 by accident and regresses six pre-existing replays
(`marth/WellWornSmallGoshawk`, `marth/VigorousRelievedLlama`, three `doubles_recent`,
`icies/dl-marth-2025-04`).

**The fix retires pre-existing debt.** `puff/specials.slpz` carried a
`scene-standings-cache-gap` classification whose rationale reads "the minimal headless
gm_8016C5C0 projection supplies a loser rank one above retail's scene-owned MatchEnd
cache, shortening one Sing timer by exactly 15 frames" -- the same off-by-one reached
through Sing instead of a grab. That replay is now bit-exact on both backends, its
classification is deleted, and its output lock is the one lock this change rewrites.

**Method note.** A first pass mis-attributed this to the self-destruct score rule
(`xC = -2`) by reasoning backwards from two rank observations instead of reading the
executed branch. Both models happen to produce identical output on the whole 425-replay
corpus, so the corpus could not tell them apart; only probing `fn_8016588C` itself showed
the default branch never executes. When a formula has a mode switch, probe which arm runs
before fitting parameters to the result.

**Traps worth remembering:** `docker cp` preserves the source mtime, so copying an edited
file into the container can leave it *older* than the existing object file and `make` will
not rebuild -- a "control test" run that way silently measures the previous binary, which
briefly inverted a conclusion here. `touch` copied sources before `make`. `make native`
also does not rebuild the PPC binary; `make ppc` is separate, and a stale
`melee-core-ppc` reproduces the old behavior long after the native fix lands.

## RESOLVED (2026-08-23) — hitlag-accumulated pad edges

**Resolved:** the arm clears the damaged fighter's own x668/x66C (DOL r29 = fp); see the
2026-08-23 log entry in the Link packet. The text below is kept for provenance.

`53362_Game_20250504T004716` is **in the suite**, carried as
`hitlag-accumulated-pad-edge` with native and PPC snapshots. It is a known hosted defect
rather than a recording artifact, classified as explicit debt so the replay keeps its
coverage and so the eventual fix announces itself by breaking the entry -- the same way
`puff/specials`'s `scene-standings-cache-gap` surfaced the standings fix.

- Retail and the hosted build agree exactly through frame 1387 (timer 47/46/45 at frames
  1382/1383/1387). At 1387 the hosted `ftCommon_GrabMash` subtracts the 6-point mash and
  retail subtracts nothing, so retail runs +6 from 1388 onward (44 against 38, and 6
  against 0 at 1408). The hosted timer lands on exactly `0.0` at 1408 and trips
  `ftCo_CaptureWaitHi_Anim`'s `grab_timer <= 0` gate one frame before the throw; the grab
  breaks, `fn_800DAD18` stops re-anchoring the pair, and the match forks to the end.
- Everything upstream is exact and retail-verified: seeded timer 60, standings rank 1, the
  1-per-frame decrement and the 6-point mash quantum.
- The trigger is `fp->input.x668`, the newly-pressed mask. The recording holds A from 1383
  through 1387, so the press edge lands inside the pummel's four-frame hitlag
  (1383..1386), during which the capture Anim -- and therefore `ftCommon_GrabMash` -- does
  not run.
- **Retail probe at `Fighter_Spaghetti_8006AD10`'s entry**: the held Ness carries
  `fp+0x2219 = 0x07` (bit 5 clear) and `input.x668 = 0` on *every* frame of the hitlag.
  The hosted build has `x2219_b5` set and `x668 = 0x100` across the same frames.
- **The input update is not at fault.** Instrumenting it with a shared sequence counter
  shows it overwriting `x668` to 0 exactly as it should on the first frame the flag
  clears, and shows the capture Anim consuming the update that immediately precedes it.
  The earlier reading of this residual -- "the priority-3 update failed to overwrite" --
  was wrong; the flag and the stale press are established earlier, when the pummel lands.
- **`ftCo_Damage.c`'s capture-partner arm is the site.** That arm always runs
  `other_fp->input.x668 = other_fp->input.x66C = 0`, with a conditional
  `other_fp->x2219_b5 = true` above it. Instrumenting it shows the hosted build reaching
  it at frame 1383 with **`fp` = Ness** (the captured fighter, whose `victim_gobj` points
  back at the grabber) and **`other_fp` = Captain Falcon**, so it clears the *grabber's*
  pressed mask and sets the *grabber's* hitlag flag while the victim's own `x668` and flag
  are left untouched -- inverted relative to the effect retail produces on the victim.
  Which fighter drives that arm for a pummel on a held opponent is the next thing to
  settle; the port's copy of the line is byte-identical to the decomp, so this is a
  branch-selection question, not a missing statement.
- Not patched under a single replay: `x668` feeds every `CheckInput` owner, so the fix
  needs the full aggregate as its gate.

## Follow-ups

1. `2025-03`'s 1-ULP knockback seed is the one residual without a named mechanism; the
   remaining lead is `kb_applied`'s own input chain (percent/staleness/weight), since
   the formula owners and every fusion reachable from the angle path are already
   complete. A retail probe on the `ftColl_80079AB0` inputs would settle it.
2. The hitlag-accumulated pad-edge defect above. Retiring the
   `hitlag-accumulated-pad-edge` classification is the acceptance test; the corpus-wide
   risk is that `x668` feeds every `CheckInput` owner, so the full aggregate is the gate.
3. Probe captures are now self-terminating (`MSL_PROBE_EXIT_FRAME`, slippi-dolphin
   80e8cd15): a bounded window finishes in seconds instead of running to the external
   alarm, so retail ground truth is cheap enough to reach for early rather than last.
2. The Mr. Saturn `xDE4` lane in `51444` is a pre-existing gap the Ness suite merely
   exposed (a Peach-pulled item), not a Ness owner; if `itDosei` states 4/5 are ever
   given an explicit past-member carve-out, that classification can be narrowed.

## Reclassified debt — marth WingedGorgeousPanther @9793

**RESOLVED (2026-08-26): not the tilt timer — the recording ran the UCF 0.8 shield drop with no platform gate; see the top section. Kept for provenance.**

`incomplete-nintendont-post-frame-stream` was a misread and is now
`held-down-shield-press-escape-decision` (owner moved from the Nintendont
recorder to `ftCo_Escape`). The recorder is live across the whole window: shield
HP decays 60.00 -> 53.28 at 0.28/frame while the action id advances GuardOn ->
Guard and instance ids increment, and `animation_index` UINT_MAX with
`state_age` -1 is just this stream's Guard/Entry/Dead representation (41 runs on
P3, 33 on P4, every earlier one inside the 9,915-row exact prefix). The real
divergence is ours: PassiveStandF ends at 9793 and the player presses shield
with the stick already held down-left (lstick -0.6875, -0.7125, past the down
threshold since 9791); retail shields, we take `ftCo_80099794`'s spot-dodge gate
(`lstick.y <= x314 && x671_timer_lstick_tilt_y < x318`) into Escape with 14
intangible frames. Suspect the x671 down-tilt timer being armed a frame late, so
a two-frame-old hold still reads as a fresh smash — a retail probe at 9793 would
settle it. Gameplay resyncs bit-exactly by 9916; the rest of the 710-row tail is
only the global attack-instance counters off by one. Not UCF: all 16 rollout-flag
combinations reproduce fingerprint `3d95a3cb4754c703`, and
`msl_ucf_suppress_spotdodge` needs `mpColl_IsOnPlatform` so it cannot fire on the
main stage floor. The `expected` snapshot is untouched, so this is a text-only
re-own: the replay still classifies at the same fingerprint and no suite count
moves. Verified on the yoshi base it was recorded against (marth stage-3 native
`pass=1 classified=2 fail=0`, msl-luigi); not re-run against this packet's build,
which the edit cannot affect.

# Previous structural packet — `decomp-port-arm64-ppc` merge polish

## Objective

Bring the reviewed `decomp-port-arm64-ppc` tree to a mergeable state without widening its
correctness policy or accepting regressions to the original eight-character runtime. The retained
ownership and experiment log live below; retained performance evidence lives in
`agent_docs/PERFORMANCE.md`.

## Final boundary

- **Final owners:** `ftCo_800DA190` owns the character-specific grab accessory correction and must
  read `Fighter.kind` rather than a PPC-width raw leading word; `ftCo_800AC5A0` owns the CPU-DI
  command bytes and must not enqueue undefined host locals on its zero-knockback path;
  fighter-pose admission owns live joint capacity; the existing fighter/dynamics publication
  phases own the matrices they consume; Makefile/CI own complete gates and host architecture
  selection.
- **Canonical state:** one source-shaped Match state, one admitted fighter-pose graph, and the
  existing JObj matrices/dynamics descriptors. No replay row, validator classification, benchmark
  mode, or second synchronized cache becomes production state.
- **Consumers:** native/PPC/Wasm simulation, 2- and 4-player reset/copy/save/restore, validation,
  observation/viewer publication, and native/macOS build profiles.
- **Displaced work/state:** remove redundant whole-chain publication and repeated match predicates
  only where equivalent source state already exists; replace unconditional character-heavy pool
  reservation with demand owned by the actual configuration if evidence permits. Do not restore
  cold presentation graphs or displaced renderer machinery.
- **Deletion boundary:** no tolerances, replay-fit clamps, re-recorded stale fingerprints, character
  compatibility flags, fallback dispatch, debug bridges, or retained experimental variants. Every
  performance candidate either passes focused equivalence gates and adjacent benchmarks or is
  removed completely.

## Acceptance

- Original 153-replay domain: zero failures/errors with retained policy; full 366 aggregate: zero
  unclassified failures/errors.
- Focused metadata-preserving PPC checks pass for every touched classification; never use the
  native-lock-only full PPC aggregate as a gate.
- Four-Sheik Final Destination reset passes, and the runtime census covers every publicly admitted
  character across 2/4-player and six-stage configurations without fixed-capacity overflow.
- Common-domain release throughput at 256/512 and lifecycle save/restore return to the comparison
  branch within normal adjacent-run variance, with stable per-ref digests and identical workloads.
- `source-check`, format, native/PPC smoke, Python tests, Wasm/viewer smoke, and complete validation
  selection pass. macOS-only behavior receives static coverage locally and collaborator execution
  on actual hardware.

## Log

- 2026-08-01 — `retained`: review head `479d847d` has one native Samus contact failure, one stale Icies
  PPC follower classification, a four-Sheik fighter-pose capacity abort, +12.85%/+13.54% release
  cycles/frame at 256/512, and +8.45% snapshot/save/restore cost. Mechanical failures are a stale
  source lock, two trailing spaces, an omitted Ice Climbers Makefile filter, inconsistent public
  fighter documentation, and incoherent Rosetta release compile flags. Branch created locally as
  `decomp-port-arm64-ppc-polish`; no remote update is authorized.
- 2026-08-01 — `retained`: pose storage is sized from the admitted player count at 256 joints per
  player, with a 1,024-joint four-player ceiling and a 16-bit capacity. A 16-character, six-stage,
  2/4-player runtime census now exercises that boundary. Four Sheiks consume the observed maximum
  of 1,016 joints; two-player snapshots fall from 686,676 to 659,348 bytes and save/restore return
  to the comparison medians.
- 2026-08-01 — `retained`: the Samus failure was not an mpColl discrepancy. `ftCo_800DA190` read
  `*(s32 *) fp` as the fighter kind, relying on the retail 32-bit leading GObj pointer layout; on
  the 64-bit host that is the pointer's upper half. Reading `fp->kind` restores the source-owned
  Link/Samus/Young Link exclusion. The 15,481-frame focused replay then has no gameplay mismatch.
- 2026-08-01 — `retained`: the Ice Climbers split was not follower collision or fma order. Retail
  `ftCo_800AC5A0` carries caller-register residue for its unassigned CPU-DI bytes when the
  pre-ProcessHit knockback vector is zero; hosted C instead consumed arbitrary uninitialized stack
  bytes. Initializing the local command to neutral removes undefined behavior. Both backends then
  retain only the already-classified item residue, with no follower mismatch.
- 2026-08-01 — `rejected`: a post-solve dynamics refresh on the PPC path and an explicit
  CaptureCut fmadd did not own the Ice Climbers mismatch and were removed completely.
- 2026-08-01 — `retained`: remove the first of two whole-chain dynamics matrix publications in
  `Fighter_ProcessHit_8006D1EC`. The later post-solve publication remains the canonical end-frame
  owner added for retail-probed correctness. Seven exact replays whose locks originally depended
  on dynamics publication remain byte-exact, while adjacent 256-batch sampling improves about
  6--7% against the redundant-publication control; 512-batch sampling is neutral within noise.
  A material common-domain throughput gap to `experiment/decomp-port` remains open.
- 2026-08-01 — `retained`: the sole post-solve dynamics publication now consumes the JObj tree's
  canonical dirty bits rather than forcing every link dirty. The solver's setters already
  propagate dirtiness down each mutated chain. All seven retail-probed locks remain exact.
- 2026-08-01 — `retained`: the larger FObj and class-piece reserves are selected only for a Match
  containing Samus, the documented sole consumer through her grapple. The all-character census
  still reaches the Samus high-water safely; the ordinary two-player snapshot is now 611,420
  bytes, below the comparison branch's 633,156, with faster save/restore medians.
- 2026-08-01 — `retained`: native release compiles the now-widespread `__fmadds` operation as the
  compiler intrinsic it represents rather than an out-of-line call from O0 source-shaped TUs.
  Debug, Python, Wasm, and PPC keep the existing external owner. Seven sensitive locks stay exact;
  extending the treatment to fmsubs/fnmsubs regressed adjacent throughput and was removed.
- 2026-08-01 — `retained`: final alternating common-domain samples close the original
  +12.85%/+13.54% regression. Batch 256 median cycles/frame are 63,765.7 comparison versus
  62,011.2 polished (-2.75%); batch 512 is 48,921.1 versus 49,625.9 (+1.44%). The per-ref digests
  are stable and workloads identical; the remaining spread is ordinary machine contention, not a
  material regression.
- 2026-08-01 — `retained`: final native aggregate after the representation changes has 286 exact
  passes / 80 classified / 0 failures / 0 errors over all 366 replays. The obsolete Samus
  classification is deleted. Twenty-three Ice Climbers snapshots were refreshed only after a
  guarded audit proved their drift was confined to the already-owned item identity/residue lanes;
  the one mixed Dream Land entry retained its byte-identical pre-existing wind-puff fields.

# Previous structural packet — Ganondorf

## Objective

Add Ganondorf (internal kind FTKIND_GANON 25, public/CSS char id 25) to the supported domain.
Ganondorf is the Captain Falcon semi-clone: every special is owned by the already-ported
`ftCa_Special*` TUs (their `FTKIND_GANON` branches select the Ganon gfx/motion variants), and
the only character TU is the Matching `ftGn_Init.c` (motion-state table delegating to ftCa
handlers, strings, item/knockback callbacks). Registry rows, PlGn extraction (five costumes
Nr/Re/Bu/Gr/La + PlGnAJ + PlGnDViWaitAJ), native DAT translation reusing the Captain ext-attr
layout (`ftCa_Init_OnLoadForGanon`/`ftCa_Init_LoadSpecialAttrs` are the shared owners; anim
count 318; `MSL_FIGHTER_ARTICLES_NONE`), derived part-admission row (54 live / 30 cold of 84),
public API/viewer/validation admission, and a 30-replay suite wired into the aggregate.

## Final boundary

- **Owner (character):** the imported `ftGn_Init.c`, byte-identical to the pinned decomp. No
  OnLoad ext-attr mutation (ftCa_Init_OnLoadForGanon only pushes attrs — no DK-style store
  guard needed); no motion/fighter-var pointers (fv/mv `.gn` alias the ftCaptain views).
- **Effects:** EfGnData.dat owns efAsync bank 19 with a real particle bank, but the hosted
  loaders do NOT load it — the Falcon precedent: every Ganon-reachable efSync id
  (0x50B..0x50F -> efLib model creates on bank-19 model ids 0x4A38..0x4A3D) is model-backed
  only, so no generator RNG projection consumes bank-19 ids (EfCaData bank 4 is likewise
  unloaded for Falcon). `MSL_CORE_EFFECT_BANK_CAPACITY` stays 19.
- **Data:** `PlGn*` + `EfGnData.dat`; manifest 146 -> 155 files; no capacity bumps needed for
  the fifteenth character (arena and archive-cache headroom from the DK packet hold).
- **Exactness fix shipped:** the ThrownLw follow `ftCo_800DE508` now ports its MWCC fusions
  (GALE01 0x800DE558/0x800DE56C fmadds both lanes — the same shape as the fused
  ftCo_800DB464 capture-follow twin); identity at current inputs (unit scale), aggregate
  byte-stable.
- **Heap-overflow fix shipped (fighter_pose.c pose-program map):** a FigaTree node with ZERO
  tracks wrote its `track_nodes` map entry at the NEXT node's first-track position; a trailing
  zero-track node in the final program wrote one uint16 past the calloc'd map. Ganondorf's
  PlGnAJ bank is the first with such a tree, so every process that ran the 15-character
  game-data preload corrupted its heap (symptom: `malloc(): invalid next size` aborts in
  later in-process work — pytest EnvBatch tests after an in-process validate_one). The map is
  only ever read at tracked nodes' first-track indices (request_figa early-returns for
  trackless nodes), so skipping the write for zero-track nodes is read-identical: native
  aggregate byte-stable after the fix, ASan-clean end-to-end (debug recipe: ASan-build
  libmelee_core via `make python-library PYTHON_BUILD=/tmp/asan_build NATIVE_OBJ_DIR=...`
  CFLAGS="-O1 -g -fsanitize=address ..."`, stub the ~34 gc-section-dead undefined symbols,
  LD_PRELOAD libasan into python).

## Suite state (container authoritative): aggregate 366 = 285 pass / 81 classified / 0 fail

30 replays (all six stages: 5 FD / 5 BF / 5 FoD / 5 YS / 5 DL / 5 event-verified frozen PS;
opponents Captain Falcon, Falco, Fox, Jigglypuff, Marth, Peach, Samus, Sheik; Slippi netplay +
anonymized ranked + mainline; zero re-wraps, raw pad lanes populated in all 30 zip files):
**container native 25 pass / 5 classified, PPC identical fingerprints on all five.**

**FROZEN-PS POLICY CORRECTED (user-directed):** the Stadium Transformations event (0x41) was
added at Slippi 3.18.0 (refs/slippi-wiki/SPEC.md) and fires on every transformation change, so
zero `stadium_transformation` events over a full game proves frozen for ANY version >= 3.18.0.
The prior "3.18 recorders are silent" note was a wrong inference from v3.18.0 controls whose
non-frozen status came from the start-block `is_frozen_ps` flag — which is only trustworthy at
3.19.0+. Empirical confirmation: the three v3.18.0 zero-event Stadium games in this zip
validate fully bit-exact over 11-13k frames each against the frozen-only hosted Stadium model
(a transforming game would diverge massively within the first ~75-second cycle). The five
classified families:

- `2025-03` @5974: single l_cancel row on Zelda's first post-transform frame (new mechanism id
  `transform-swap-stale-lcancel-byte`): Slippi keeps L-cancel status in the repurposed
  PlayerData+0x25FF byte and Zelda's freshly-swapped block carries stale retail memory (0x50)
  for one frame until the 8006c324 reset hook runs; hosted per-fighter lcancel state starts 0.
  Export-only lane, unmodelable without emulating uninitialized retail memory.
- `24891` @10479: 35-row P1 pos_y episode — Ganondorf down-throws Marth over FoD's descending
  right platform; the `ftCo_800DDDE4` mid-frame release resolve picks a floor line 0.075 below
  retail's (our resolved y 2.0251 vs retail 2.1001 back-computed; the recorded platform height
  1.19994 gives our line at +0.825, and the descending platform line vs the curved stage flank
  sit within 0.075 at that x). pos_x bit-exact throughout; heals at the landing snap. Same mp
  floor-line-pick seed as the dk `auto-dk-2025-04` entry (retail arbitration there showed
  retail == recording). Debug recipe that isolated it: temp trace in ftCo_800DE508 (follow
  inputs) + ftCo_800DDDE4 (release vec / resolve output / live platform heights via a temp
  slippi.c accessor).
- `25827` @3816 (BF x=72.84), `medium-fox` @3819..3822 (YS x=57.27), `medium-sheik` @5451
  (YS x=57.33): the documented open wall-hug clamp 1-ULP pos_x re-derivation family, all
  single-episode self-healing, identical on both backends.

## Follow-ups

- The dk suite rejected three v3.18.0 Stadium candidates under the old (wrong) "3.18 recorders
  are silent" policy; under the corrected policy they are admissible if their
  stadium_transformation streams are empty (verify with the frozen-model validation
  arbitration). Same check applies to any earlier suite's v3.18.0 PS rejections.

## Log

- 2026-08-01 — `merge` (macos-native-build update integrated): the collaborator's rebased
  macos-native-build (deterministic hosted source identities + targeted compatibility flags +
  -fpermissive removal) was linearized onto the original branch tips as
  `macos-native-build-linear` (tree-identical to their tip) and merged. Conflicts: effects.c
  (kept the DK model-only effect-bank path, adopted MSL_LBARCHIVE_END) and this doc. The
  deterministic-token representation shifted item-var pool residues on the four
  `unrecorded-item-pool-residue` icies classifications (item.misc* lanes only — every other
  snapshot stat identical; verified by full pre/post field diffs in msl-luigi); native
  snapshots + 4 output locks re-recorded. Container aggregate 366 = 285 pass / 81 classified /
  0 fail. PPC snapshots for all four verified bit-identical pre/post merge (platinum's
  aggregate-PPC snapshot was already stale beforehand — per-suite PPC remains the gate).
  macOS-native aggregate unchanged (same 122/18/201 with identical drift fingerprints
  pre/post; known non-gate signed-zero/ULP condition).

- 2026-07-30 — `open` (this packet: Ganondorf source validation packet; all 30 zip files
  admitted after the frozen-PS policy correction and wired straight into the aggregate with
  locks + five classifications; aggregate 366 = 285 pass / 81 classified / 0 fail native;
  PPC ganon suite identical fingerprints; fighter_pose track_nodes heap overflow found by the
  15th character's preload and fixed).

# Previous packet — deterministic hosted source identities

## Objective

Remove host-address identity from the native `u32` fields retained by the source-shaped runtime.
Allow GameData and native DAT arenas to map anywhere without making match construction, HSD lookup,
copy, or save/restore depend on ASLR or allocator reuse.

## Final boundary

- **Final owner:** `MslMemoryContext` owns GameData raw-file tokens; `MslNativeDatContext` owns native
  DAT descriptor tokens; the HSD ID API owns conversion from hosted pointer identity to its `u32`
  key. Callers do not manufacture hosted IDs by truncating pointers.
- **Canonical state:** true native pointer fields store full host pointers. A GameData source-width
  file address is a one-based byte offset in its GameData arena. An HSD pointer ID is a tagged byte
  offset in the native DAT arena, Match arena, or core image. Zero remains null and equivalent
  immutable GameData produces identical tokens regardless of mapping address.
- **Consumers:** `lbFile_800168A0` and fighter animation subarchive loading consume GameData tokens;
  native DAT translation, AObj/fighter-pose loading, JObj/RObj/PObj reference resolution, fighter
  metal, ground material, and `HSD_IDTable` consume HSD pointer IDs. Match copy and save/restore carry
  these stable scalar tokens unchanged while relocating only real pointer slots.
- **Displaced state/work:** delete low-32 mapping retries, low-32 pointer reconstruction, fixed/low
  address assumptions, and hosted pointer-to-`u32` casts at HSD ID call sites. No numeric-ID
  savestate relocation table is introduced.
- **Deletion boundary:** no hosted GameData file handle or HSD ID key contains bits from a native
  address. Arena mappings use the ordinary host allocator once, all token decode is range-checked
  at its owning boundary, and there is no low-address compatibility path or fallback
  representation.

## Evidence and acceptance

- Keep two equivalent GameData instances resident simultaneously so their GameData and native DAT
  mappings must differ; destroy the snapshot's source instance, restore into the other, cross an
  animation/HSD lookup continuation, and require exact state equality.
- Preserve the exact native/PPC/Wasm correctness gates and retained replay classifications/locks.
- Compare lifecycle and release replay throughput against the current `experiment/decomp-port`
  baseline under identical build, data, CPU, and benchmark inputs; any retained change must be
  throughput-neutral or better.

## Log

- 2026-07-31 — `open`
  Scope: GameData file handles, native DAT descriptor identities, HSD pointer IDs, and the existing
  cross-GameData savestate continuation.
  Hypothesis: arena-relative tokens eliminate the address-reuse hole and the 64-attempt mmap scan
  without adding gameplay work; the conversions occur during initialization or object creation.
  Evidence: PR #11 currently derives these `u32` values from arena pointer low bits. Typed
  savestate relocation correctly moves full pointers but cannot and should not infer provenance for
  numeric fields, so a restore into simultaneously resident equivalent GameData retains stale HSD
  keys under the current representation.
  Disposition: implement the singular token representation, strengthen the existing recreation
  smoke to force distinct simultaneous mappings, then checkpoint exactness and performance.
  Next: replace low-32 encode/decode and audit every hosted HSD pointer-ID cast before running the
  focused native smoke.

- 2026-07-31 — `checkpoint`
  Scope: complete hosted file/HSD token cut plus simultaneous-GameData restore smoke.
  Hypothesis: every persistent HSD pointer identity belongs to native DAT, Match, or the core image;
  temporary retail descriptor copies must resolve to an equivalent persistent source owner rather
  than acquiring a new runtime registry.
  Evidence: the first native smoke rejected `ground.c::get_jobj_inline`'s stack copy. Its source
  descriptor is the immutable `Ground_803B7E0C`; hosted loading now uses that image-relative owner
  and publishes the copied scale before consumption. No other unowned identity was reached. The
  complete native smoke, strengthened cross-GameData save/restore continuation, and PPC smoke pass.
  `source-check` verifies the canonical inventory; the locked `refs/melee` checkout is not yet
  populated in this isolated worktree.
  Disposition: retain the singular tokens and source-image identity for broader gates.
  Next: populate the local source reference, run source/Wasm/viewer/replay gates, then benchmark the
  exact candidate against `experiment/decomp-port`.

- 2026-07-31 — `experiment`
  Scope: save/restore cost after sharing ELF/Mach-O image-range discovery between HSD identity and
  savestate relocation.
  Hypothesis: the lifecycle target's four restore samples are too small to distinguish a real
  roughly 0.035 ms candidate increase from timer/layout noise. Temporarily raise only its snapshot
  sample count to 256 in the rebased PR control and candidate, rebuild the same debug profile, and
  compare adjacent pinned-core runs; remove the probe afterward.
  Evidence: moving savestate image discovery behind the shared external helper measured 0.6705 ms
  median restore versus 0.6545 ms for the rebased PR control at 256 samples (+2.4%), even after
  deleting its duplicate loader walk. That refactor was rejected. With PR #11's original local
  savestate discovery restored, adjacent candidate/current-branch medians are 0.6595/0.6580 ms
  restore and 0.4775/0.4780 ms save; snapshot size remains exactly 633,156 bytes.
  Disposition: retain deterministic tokens but leave the measured savestate path and layout intact;
  keep source-image lookup isolated to cold HSD identity construction.
  Next: remove the temporary 256-sample probe and rerun the ordinary lifecycle/correctness gates.

- 2026-07-31 — `checkpoint`
  Scope: final production throughput and lifecycle comparison against `experiment/decomp-port`.
  Hypothesis: deterministic token conversion remains outside gameplay and does not regress the
  resident 256/512 production profiles or copy/save/restore.
  Evidence: six alternating release runs preserve both benchmark digests. Median cycles/frame move
  47,900.1 to 47,326.3 at 256 (-1.20%) and 43,673.2 to 43,922.8 at 512 (+0.57%, within run/layout
  variance). The high-resolution lifecycle probe is neutral against current for save, restore,
  artifact size, initialization, and stepping. Exact commands and medians are retained in
  `agent_docs/PERFORMANCE.md`.
  Disposition: retain the identity representation as performance-neutral; make no speedup claim.
  Next: run final native/PPC/Wasm/viewer/replay gates on the exact commit candidate.

- 2026-07-31 — `checkpoint`
  Scope: hosted compiler portability at the archive variadic boundary.
  Hypothesis: PR #11's Apple-only low-word terminator check masks a caller/callee type mismatch and
  should be replaced by the pointer-width sentinel that the hosted loader actually consumes.
  Evidence: a fresh Linux Clang 18 build reproduced the upper-word failure during Fox costume
  loading (`symbol=0x7fff00000000`). Converting every archive-list terminator to
  `MSL_LBARCHIVE_END` on hosted builds removes the mismatch; the same fresh Clang native smoke now
  passes. The 32-bit source build retains its literal-zero ABI.
  Disposition: retain the typed hosted boundary and delete the Mach-O-only low-word heuristic.
  Next: rerun the exact final native/PPC/Wasm/viewer/replay gates and commit the local stack.

- 2026-07-31 — `retained`
  Scope: final deterministic hosted identity and compiler-portability boundary.
  Hypothesis: the cleanup must preserve every supported replay lock and production digest while
  allowing equivalent GameData owners to coexist at unrelated host addresses.
  Evidence: simultaneous-GameData save/restore, GCC and Clang 18 native smokes, PPC smoke, source
  lock, Wasm parity, live Chrome viewer, and formatting all pass. Debug and optimized-release gates
  both remain 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames.
  Final 256/512 release samples preserve digests `8ef126a41244d514` / `6f91f23e3553a090` at
  47,259.0 / 43,710.2 cycles per frame, inside the retained A/B envelope.
  Disposition: packet complete and ready for its local implementation/evidence commit; no remote
  branch has been updated.
  Next: verify the native macOS profiles on Apple hardware before final rebase merge.

- 2026-07-31 — `retained`
  Scope: merge-review reduction of source-image IDs and hosted compiler workarounds.
  Hypothesis: image-base equality is sufficient for the encode-only static-image owner; native DAT
  is the only inverse-ID consumer. Explicit hosted return values and volatile rounding slots can
  remove blanket diagnostics and undefined writes without changing source/PPC behavior.
  Evidence: the callsite audit found one static-image encoder and two inverse consumers, both native
  DAT descriptors; Clang exposed two legacy return diagnostics hidden by the global suppression.
  GCC, fresh Clang, PPC, Wasm/viewer, and both 153-replay gates pass. Adjacent release medians are
  neutral-to-better at 256 and 512 environments with both production digests unchanged.
  Disposition: retain the narrower decoder, defined hosted spill, and explicit hosted returns; the
  source/PPC paths remain unchanged and the general image-range module is deleted.

# Previous packet — Donkey Kong

## Objective (closed)

Add Donkey Kong (internal kind FTKIND_DONKEY 3) to the supported domain: the thirteen Matching
chara TUs (`ftDk_Init/SpecialN/SpecialS/SpecialHi/SpecialLw`, the seven `ftDk_Heavy*` cargo-hold
states, `ftDk_MS_345_0`), registry rows, PlDk extraction, native DAT translation
(`MSL_FIGHTER_ARTICLES_NONE` — DK spawns no articles), part-admission row, effect bank 8,
public API/viewer/validation admission, and a 27-replay suite (staged standalone, not yet in
the aggregate). The cargo carrier/victim common owners (`ftCo_Cargo*`, `ftCo_Shouldered`) were
already ported; DK is their first live consumer.

## Final boundary

- **Owner (character):** the imported ftDonkey TUs, byte-identical to the pinned decomp except
  the OnLoad idempotent-store guard (ledger `canonical:donkey-onload-attr-store-guard`): retail
  writes the three cargo-walk anim durations into the shared PlDk ftData ext-attr on every
  fighter load; GameData preload performs the first mutation before the native DAT arena seals
  read-only, later Match constructions recompute and skip the identical store (ftPe_Init_OnLoad
  precedent). DK motion/fighter vars carry no pointers — neither Pikachu bug class applies.
- **Effects:** EfDkData.dat is MODEL-ONLY — both leading effDonkeyDataTable words are
  unrelocated NULLs and retail efAsync_LoadSync skips psInitDataBank entirely; both hosted
  loaders now publish an empty command bank for that shape (ledger
  `canonical:model-only-effect-bank`). All DK efSync ids (0x4C6..0x4CC) are pure
  efLib model creates — no generator RNG cases needed.
- **Data:** `PlDk*` (Nr/Bk/Re/Bu/Gr — five costumes) + `EfDkData.dat`; manifest 146 files.
- **Capacity:** archive cache 3072 -> 4096 entries; shared game-data arena 64 -> 96 MiB
  (fourteenth character).

## Exactness fixes shipped with the packet

- **Pose-table dropout resync** (ledger `canonical:fighter-pose-table-dropout-resync`): the
  native pose engine re-derived track time from a fractional absolute frame when dropping from
  the integer-frame table fast path to fractional-rate decoding (cargo walk anims switch rate
  every frame). The decoder now resyncs at the recorded last table frame (integer wait
  subtraction is exact) with a dry walk, so the following rate step reproduces the source
  engine's incremental rounding bit-exactly. Root-caused from a 1-ULP carry-bone fork (victim
  XRotN is ROBJ-constrained to DK's TransN2, publishing the animated spine chain directly into
  pos). Three DK replays became fully bit-exact; two more dropped their native-only rows.
- **HSD_FMod wrap fnmsubs** (`canonical:anim-loop-wrap-fnmsubs`), **capture-follow /
  shoulder-timer / Hand Slap fusions** (`canonical:capture-follow-fmadds`) — retail-faithful
  boundaries, identity at current inputs.

## DK tie triage (2026-07-30, user viewer report — RESOLVED, no sim change)

The horizontal tie in the live viewer is not produced by the sim. Verified from extracted
data: DK's only dynamics chain is the tie (parts 66-71, rooted under chest part 26); NO
hurtbox rides it (hurtbox bone 73 attaches at part 3, near the hip) and NO subaction Create
Hitbox anchors on it (hitbox bones: 0,4,7,17,25,29,30,31,43,44,51,52,53,62). The headless
policy in ftdynamics.c therefore correctly skips its dangle solver (same class as Luigi's
hair and the capes), and the bit-exact corpus confirms zero gameplay impact. The on-screen
body outline comes from slippilab's donkeyKong.zip per-animation 2D frames (the sim supplies
only action/frame/pos/facing + hitbox capsules), and those outlines were generated without
dangle physics — the stiff tie is baked into the asset and appears in slippilab's own viewer.
If presentation parity ever matters, the lever is regenerating the zip frames, not the sim.

## Session 4 (2026-07-30): pre-fusion-era audit — Tier A ports

Corpus-wide inventory (grep fused ops per asm TU vs __f* markers per C TU) found the
pre-fusion-era gap set. Ported with per-site asm verification (126 sites): Sheik chain (35),
Ice Climbers belay string (28), Pikachu QA/Skull Bash/jolt spawns (18), Sheik
needles/chain-charge/Vanish (12), Zelda Din spawn/Farore (7), Fox reflector turn (3), Icies
squall/belay/spawns (11), ftcommon hitlag/nudge/grab-timer (12). Native aggregate byte-stable
throughout (identity at current inputs); PPC pool-residue-fed lanes shift where
backend-specific residuals are inexact (re-recorded).

REMAINING INVENTORY (asm-vs-marker gaps, supported-domain candidates, unported):
ftCo_Guard (8), itcoll (8), fighter.c (5), lbcollision (7), mplib (10 — likely inline
double-count), ftCo_0A01 (75 — needs per-fn reachability triage: DeadUpStar etc.),
Peach/Mario/Luigi SpecialS (2/2/1), Samus SpecialN/Lw_1 (2), items: din fire (3+2), toadspore
(2), missile/chargeshot (2), sword/dosei/foods/freeze (4), heiho (4), iteffect (3), it_26B1
(4), itmaplib (6), item.c (1); ft small TUs: ftCo_09F7 (6), JumpAerial (4), DamageIce (4),
Damage/DamageFall/Throw/Thrown/TurnRun/CaptureCut/Bury/FlyReflect/DamageBind/DamageSong
(2 each), ftpickupitem (3), ftlib (2), ft_07C1 (8), ft_0D4D (6), ft_0899/0C31/0D27/0DF0,
ftCo_800C7590/7CA0; infra to triage per-function: lbvector (46), spline (58 — verify the
session-6 port shape), mtx (98 — display vs gameplay split), grlib (8), grizumi (2),
grpstadium (1), ground (1), groldpupupu (4), jobj (1). Skipped as unreachable: itlinkhookshot
(113, Link), ftcpuattack (46, in-game AI), pobj/lbbgflash/ftafterimage/camera render, itzako.

## Session 3 close (2026-07-30): THE ROLLOUT FUSION SET — aggregate 336 = 260 pass / 76 classified / 0 fail

The "cargo-walk" family was misattributed: the diverging player is P1 JIGGLYPUFF in
ftPr_MS_SpecialNTurn (351) — Rollout pinned at FD's edge, turning, speed decaying. The rollout
TU carried FOURTEEN unported MWCC fusions (ftPr_SpecialN.c predates the fusion discipline):
four Turn_Phys gr_vel accumulates (0x80140934-family — THE seed), four charge-spin roll-angle
steps, two turn-spin steps, and the bounce-dust half-height (inline x4). Porting them retired
the 22-row DK family to one wall-hug pos row, promoted classified puff
Game_20260318T101341 to fully bit-exact BOTH backends (entry retired), and shrank
rollout_ends_ys 64->2 and specials 849->817. All re-records native≡ppc; locks refreshed;
the mislabeled DK classification rationale corrected.

## Session 2 close (2026-07-30): suite IN THE AGGREGATE — 336 = 259 pass / 77 classified / 0 fail

All seven residual families classified with native+PPC snapshots (identical fingerprints both
backends). Retail engine-dump playback arbitrated two of them (auto-dk-2025-04 grapple-swing
fork and the 9560 wall-hug episode): retail reproduces the RECORDING, so these are hosted
ULP residuals, not recorder profile — the wall-hug clamp's pos_x re-derivation
(mpLib_8004E398/511A4 X-at-Y + mpcoll hug candidates; inputs bit-equal, output 1 ULP) is the
documented open lever, alongside the known Samus grapple-chain integration. Trace toolkit
used: bracketing pos probes around coll_cb + an auto-instrumented MPX dump of every
mpcoll cur_pos.x writer (site L2419 = the LeftWall hug clamp) — rebuild from this description.
Fused retail-faithfully along the way (identity at current inputs): throw-release velocity
blend (0x8006BC7C/90), walk-cycle wrap (0x800E0010), plus session 1's HSD_FMod/capture-follow
set. dk.json joined include_suites with 27 output locks; tests updated to 336.

## Suite state (session 1 close, container authoritative)

27 replays (all six stages; Fox/Falco/Marth/Sheik/Jigglypuff/Samus/Captain Falcon; Slippi
netplay + anonymized ranked + mainline; both Pokémon Stadium entries event-verified frozen,
three v3.18.0 PS candidates rejected — 3.18 recorders are silent on transformation events):
**container native 20 pass / 7 fail; PPC 20 pass / 7 fail — identical failures and
fingerprints on both backends.** The seven shared families:

- `auto-dk-2025-04` @8654: 150 rows, Samus offstage low on FoD, a position resolve (wall-line
  pick on the curved underside?) shifts ~0.017 then heals — the one non-ULP family; needs a
  retail probe before classification.
- `slippi-2025-04_..152714` @2663: DK walk-speed ULP family (speed_air/ground_x_self 16 rows +
  pos_x tail).
- Five 1–6-row self-healing pos ULP families at stage-edge positions (22123 @7897, 9560 @2120,
  basic-dk-2025-04 @7265, medium-marth-0912 @7060, slippi-..141746 @9611).

(Superseded by session 2 above.)

## Log

- 2026-07-30 — `open` (this packet: DK source validation packet, suite staged standalone;
  aggregate re-verified 239 pass / 70 classified / 0 fail native after re-recording the two
  icies pool-residue analogs shifted by the arena growth — same ripple class as the Pikachu
  packet).

- 2026-07-30 — `closed` (samus suite extended 12 -> 25 from ~/SSBM/Replays/Samples/samus_replays.zip;
  aggregate 309 = 239 pass / 70 classified / 0 fail)
  **Event-based frozen-PS policy shipped**: PS admission now requires recorder version strictly
  newer than 3.18.0 and zero `stadium_transformation` events over the full game, ignoring the
  game-start `is_frozen_ps` flag. Validated against transforming controls (ics.zip v3.19 PS
  games emit 13/27 events; two v3.18.0 non-frozen PS games emit ZERO over 3-5 minutes — at
  3.18.0 exactly the recorder is silent, so absence proves nothing there). Both admitted PS
  captures (medium-fox-2025-10, medium-sheik-2026-03, v3.19.0) show zero events over ~14k
  frames.
  **Re-wrap rejection class established (5 of 18 zip files rejected)**: one PS capture at
  v3.18.0 exactly (Nicki, flag also false); one 2020 game and three anonymized ranked FoD
  games re-wrapped to the 3.18 layout — payload sizes match 3.18 but raw_analog_y and/or raw
  c-stick lanes are all-zero (genuine captures show thousands of nonzero samples) and gecko
  lists are 2.8-4.9KB vs the genuine 57KB. The three ranked re-wraps fork discrete lanes
  (action_id/facing) during the countdown under EVERY era-flag combination — the destroyed
  raw pad lanes feed the UCF ring machinery, unmodelable by flags (first fork signature was
  the cardinals one: retail speed 1.28375 = 1.3 x raw 0.9875 stick our cardinals patch snaps
  to 1.0; flags moved matched 90 -> 101 only). The existing bit-exact diamond-platinum ranked
  captures carry their TRUE older versions (peppi doesn't expose the newer lanes at all) —
  version-vs-populated-lane disagreement is the re-wrap tell.
  **FObj reserve fix (arena abort)**: medium-sheik-2026-06 (DL Sheik/Samus) aborted post-seal
  (`HSD_ObjAllocAddFree` size-64 grow at used=686912). Backtrace (new glibc-gated
  backtrace_symbols_fd dump on the msl_memory_alloc failure path, kept): Samus AIR
  grapple-catch (ftCo_AirCatch_Anim -> it_802B7C18 -> it_802B743C) runs HSD_JObjAddAnimAll
  across the doubled beam link chain, one FObj per track per link jobj, crossing the former
  256-slot reserve -> now 512 (scalar.c pre-seed block). Replay then bit-exact both backends;
  no classified fingerprint moved (pool growth is layout-neutral for the corpus).
  **PPC snapshots recorded for the two samus classified entries** (icies-precedent
  completion): fox-d18-2026-04 PPC residual is ONLY the 1-ULP pos_y @7389 row (the @9075
  contact-gate flip is native-only, consistent with dolphin-ulp-contact-gate); master-samus
  PPC keeps the taut-rope fork @15146 (390 rows vs native's 135). master-peach is PPC-exact
  (its edge-stop flip is native-only). Digest key trap: compact snapshots store
  `mismatch_fields_digest`, not a digest under `mismatch_fields` — the wrong key silently
  reads as raw fields and reports drift/FAIL.
  **Stale-PPC-binary trap**: build/melee_core/ppc/melee-core-ppc predated the Pikachu port and
  every suite replay errored with the supported-character banner; `make ppc` first.
  Gates (msl-luigi container): samus suite native 22/3/0, PPC 23/2/0; aggregate native
  239/70/0 with output locks appended for exactly the 13 new replays; pytest 44 passed
  (suite-inventory assertions bumped 296 -> 309, filtered cases stay 133 since every new
  replay contains Samus).
- 2026-07-29 — `retained` (session 3: both residual fails dispositioned; pikachu.json JOINED THE
  AGGREGATE — 296 = 226 pass / 70 classified / 0 fail)
  **master-master root-caused as a recorder-profile residual**: the drifting FoD items are Fox
  LASERS (kind 54; the static kind-74 item is his blaster gun) whose spawn X rides the
  RThumbNb muzzle transform (ftFx_SpecialN_FtGetHoldJoint -> lb_8000B1CC with offset
  (0, 1.2325, 4.2636)); every flight all game is offset ~2^-16 with the fraction preserved by
  the laser's integral +7.0/frame velocity. A new MSL_MUZZLE_PROBE in the engine-dump Dolphin
  (lb_8000B1CC entry filtered on that offset vector, dumping the out vector and the full
  ancestor jobj chain) showed retail interpreter playback of this exact recording reproducing
  OUR bits exactly at frames -24 and 78 — every rotate/scale/translate/matrix word of all 12
  chain links identical. The recording disagrees with its own canonical re-simulation: the
  anonymized ranked capture records Dolphin without its build/execution profile. Classified
  dolphin-ulp-fighter-and-item-profile (bounded laser pos/vel intervals + derived angle bytes +
  reconverging fighter-pos ULP intervals; all discrete lanes exact; native/ppc fingerprints
  identical: 040cf4c553e20933, 9,734/12,233).
  **slippi-2025-11** classified dolphin-ulp-motion-profile (one 1-ULP wall-ride pos_x row,
  3,711 strict suffix, native/ppc identical: 72651ec5c1e33764).
  Wiring: pikachu.json added to melee_core_aggregate include_suites (271->296); 25 output locks
  recorded (271 existing locks byte-stable; the two icies locks re-recorded in session 1 stay
  put); tests updated (296 pins, manifest set, classified-loader suite list, coverage
  parametrize). Container full pytest 44 green (incl. ppc-snapshot verification); Mac pytest +
  source-check green. Trap note: the container clone's git HEAD is the stale clone base —
  diff lock files against the MAC HEAD, not the container's.
  The Pikachu port is at the endgame state: 23/25 bit-exact on both backends, 2 classified,
  suite in the aggregate.

- 2026-07-29 — `retained` (session 2: extended suite to 25 + the phantom-revisit decomp fix)
  Scope: the 12 remaining usable batch replays joined pikachu.json (25 total; three anonymized
  ranked now, the v3.15/v3.16 pair carrying cardinals=false — both bit-exact, which isolates
  master-master's fork from the pre-cardinals flags). Container native 23/25, PPC 23/25,
  identical fails.
  **Fix (ledger canonical:phantom-revisit-hit-gate)**: 40830 (DL Pikachu/Falco) forked at 3702 —
  our sim landed a full usmash hit one frame before retail. Retail collision probe
  (MSL_COLLISION_PROBE on the engine-dump Dolphin) showed the frame-3702 capsule sweep
  BIT-IDENTICAL to ours (two same-group grazes, coll_distance 0.0032/0.0092, both phantom-range)
  with ftColl_80076ED8 returning (1, 0) where ours returned (1, 1). GALE01 asm
  0x8007702C..0x80077444: a phantom-range contact whose victim is already in the hit group's
  victims_2 registry (populated by the first phantom's inlineB0 across ALL same-group hitboxes)
  returns FALSE; the NonMatching decomp fell through to the full-hit path instead. One-branch
  fix; the sibling item path (ftColl_80077C60) was already faithful. 40830 → bit-exact 8,892;
  aggregate 203/68/0 byte-stable (the bad branch never fires in the 271-replay corpus);
  container pytest 43 green.
  Remaining fails: (a) master-master (FoD ambience-item ULP from -24, both backends,
  next session's target — the icies FoD construction-RNG/grIzumi toolkit applies);
  (b) slippi-2025-11 = ONE 1-ULP pos_x row (fast-falling Falco hugging the YS right underside
  wall, pos re-derived from the wall line each frame; 3,711-frame exact suffix; native and PPC
  bit-identical) — meets the dolphin-ulp-motion-profile bar; classify when pikachu.json joins
  the aggregate (a classification entry for a non-aggregate suite breaks
  test_native_validation_compares_complete_classified_replays' case lookup).

- 2026-07-29 — `retained` (session 1: the full packet, three hosted-portability fixes)
  Scope: full character packet + 13-replay suite; container gates green.
  1. **Thunder motion-vars pointer widening** (ledger pikachu-thunder-vars-pointer-widening):
     four replays segfaulted (ditto + all three mainline) — `specialhi.x4 = 1` writes landed
     in the widened `speciallw.x0` high half (0x1_00000000 dereferenced in it_802B1FE8), and
     enter-time pointer zeroing flowed through the `specialhi.x0` alias. Fixed with a hosted
     speciallw layout (flag kept at its mv+4 alias position, full-width pointer past the
     aliased pair) + two named-owner zeroing sites in ftPk_SpecialLw.c.
  2. **Jolt item-var native sampling** (scalar.c item_var_source_byte): the ground/air jolt
     structs lead with pointers, so raw retail-offset sampling read padding on 64-bit hosts —
     the whole item.misc family across 7 replays (expected float bytes vs constant 0/1).
     Pikachu cases added per the Chain/DinFire/icies precedent; this promoted the suite from
     4 to 12 native passes.
  3. **Aggregate fingerprint re-record**: the wider archive arena + grown Fighter struct
     shifted deterministic pool-residue analogs on two icies classified replays (medium-fox
     2025-08, ys-fox 2025-07) — native mismatch stats identical, fingerprints moved;
     re-recorded native snapshots + 2 output locks (PPC snapshots verified unchanged).
     Aggregate 271 = 203/68/0; container pytest 42 green; Mac pytest green; source-check
     159 deltas.

# Prior structural packet — Ice Climbers (Popo + Nana follower entity)

## Objective

Add Ice Climbers (external char, internal kinds FTKIND_POPO 10 / FTKIND_NANA 11) to the supported
domain. Unlike every prior port, one player slot owns TWO fighter entities: Nana is spawned
alongside Popo by the already-imported `Player_80031AD0` follower branch and is driven by the
retail CPU input block (AI type 6: record Popo's pads into a 30-slot ring, play back 5 frames
delayed, with follow/approach fallback). The packet has four owners: the character/article
sources, the fighter-axis widening of match/observation/compare state, the CPU input subsystem
slice, and follower-frame validation.

## Final boundary

- **Final owner (entities):** the imported `src/melee/pl/player.c` dual-entity machinery
  (`player_entity[2]`, `ftMapping_list[CKIND_POPONANA]`, `Player_8003248C` returning
  `Gm_PKind_Cpu` for the non-transforming second entity) becomes live. The runtime stops assuming
  fighters ≡ players: `MslCoreMatch` tracks per-player leader and follower fighters; every
  consumer that iterates fighters walks both entities of a slot.
- **Final owner (Nana's brain):** the retail CPU input path. `ftCo_800A2040` (headless stub
  `false` today) becomes the real predicate; the reachable AI slice — `ftCo_800B3900` tick chain,
  `ftCo_800B101C` type-6 think, `ftCo_800B0918`/`ftCo_800B0AF4` ring record/playback,
  `ftCo_800A589C` partner getter, command-execution helpers, and the `ftcpuattack.c` slice the
  type-6 graph reaches — is ported into the existing `src/runtime/ftco_0A01.c` projection or a
  full upstream import (decide at import time by measuring the reachable closure; upstream TU is
  NonMatching, so every ported function is verified against `refs/melee` asm individually).
- **Canonical state:** Nana's input state is `Fighter.x1A88` (`Fighter_x1A88_t`, 0x57C, already in
  `types.h`) exactly as retail — no hosted mirror, no synthesized controller lane. Inputs from the
  wire stay 4-wide per player; Nana never reads `HSD_PadGameStatus` and never touches the UCF pad
  ring (retail-natural: the AI branch skips the physical-pad block in
  `Fighter_Spaghetti_8006AD10`).
- **Consumers:** fighter creation/respawn (`scalar.c` bootstrap, `match.c` respawn), post-frame
  refresh, native DAT preload (already iterates entity 0..1), observation/output lanes, savestate
  copy, viewer live schema, and validation compare all address (player, entity∈{leader,follower}).
- **Displaced code/state:** the entity-0-only assumptions in `scalar.c:1074`/`:1804` (fighters[] =
  index 0, kept only for the Sheik/Zelda swap); the `ftCo_800A2040` false stub and the
  `ftCo_800B3900` abort row in `src/stubs/headless_exclusions.c`/`ledger.tsv`; validation's
  leader-only descent in `tools/validation/native.c:load_player`.
- **Deletion boundary:** no compatibility flag for "ICs without Nana", no synthesized Nana input
  lane in the wire format, no duplicate Nana state outside `Fighter`/`StaticPlayer`. The public
  input API stays `players[4]`; the observation/compare structs gain follower lanes in one cut
  (Python dtypes, native.c, wire.h move together).

## Sequencing (each step gates green before the next)

1. **Data plumbing:** `PlPp`/`PlNn` prefixes + `EfIcData.dat` in `tools/data/extract.py` and
   `raw.py`; re-extract from `./SSBM.iso`; manifest/test counts.
2. **Character packet (compile + spawn):** import 8 Matching chara TUs (`ftPopo/*.c` ×5,
   `ftNana/*.c` ×3) + 3 Matching article TUs (`itclimbersice/blizzard/string.c`), ftdata.c hosted
   registry rows, native DAT translation rows, `derive_gameplay_parts_mask.py` rows for both
   kinds, effect bank, char id 10 through core enums (public API admission deferred to step 5).
   `source_character_kind` maps id 10 → `CKIND_POPONANA` (first 1→2 expansion).
3. **Fighter-axis widening:** leader+follower fighter tracking in `MslCoreMatch`, respawn/death
   (`gm_80167320(slot, subchar)` already wired), UCF gate, savestate, observation lanes.
4. **CPU slice port:** un-stub `ftCo_800A2040`, port the type-6 reachable closure, asm-verify per
   function (NonMatching upstream).
5. **Validation + suite:** follower descent in `native.c` (peppi `ports.P{n}.follower`), follower
   compare lanes and life-cycle row carry (rows vanish while Nana is dead), public API/python/
   viewer admission, stage the icies suite (11 usable candidates in
   `~/SSBM/Replays/Samples/top14.zip`: FD/BF/YS/FoD/DL ×2 each; both PS games non-frozen,
   excluded), container aggregate gate.

## Evidence and acceptance

- Retail behaviors that must fall out of the port, not be re-implemented: Popo death kills Nana
  (`ftCo_800BFD9C`); belay partner tether (`checkNanaInRange`, rope item 113); squall sync
  (`ftNn_Init_80123954` kinematic slaving); Nana's percent-derived CPU level
  (`min(popo_percent/20, 9)`); Nana motion-table fallback to Popo (`ftData_80085FD4`).
- Acceptance: container aggregate stays 0 fail with all previously-passing replays byte-stable;
  icies suite validates with follower rows compared; full pytest green; no wire-format second cut.

## Log

- 2026-07-27 — `retained` (step 2)
  Scope: character packet committed as 5d0d3858 (11 TUs, registry rows, DAT translation, parts
  masks, extraction, effect bank 14, preload with Nana costume coverage).
  Evidence: native-smoke green including the Popo parts row; triage harness steps ICs-vs-Fox 600
  frames, Nana spawns as player_entity[1] and idles. One native delta (ledger
  nana-anim-leader-share-native): the ftData_80085CD8/E50 leader-share branch is excluded under
  MSL_CORE_NATIVE (x59C never materializes natively; x14 keys the same immutable subarchive).
  UCF finding: msl_ucf_apply_pad_buffer sits in the COMMON input path mirroring retail hook
  0x8006B460, so Nana running it is retail-faithful — no gate needed once the CPU predicate is
  real.
  Next: CPU TU import (ftCo_0A01.c + ftcpuattack.c wholesale, projection reconciliation,
  stub triage) — delegated; then fighter-axis lanes and follower validation.

- 2026-07-27 — `retained` (steps 3-5 infrastructure)
  Scope: CPU TU import committed (14de9dc2); follower compare lanes cut through
  wire.h/scalar/native.c in one step (MslCoreCompare 1022→1302, 28 follower lanes + presence);
  peppi follower descent with independent life-cycle row indexing (rows null during Nana's
  Sleep, resume at Popo's respawn — measured); ICs article registry rows fixed in items.c (NULL
  state-table segfault); public API/python/viewer admission; 12-replay icies suite staged.
  Evidence: container aggregate 183/63/0 under the new wire with all 247 output locks
  re-recorded; icies suite runs end-to-end (12/12, zero errors, four-climber ditto included),
  every entry diverging at frames 83-204 in follower lanes — the untuned Nana start-of-match AI
  decision stream.
  Next: Nana AI fidelity (retail probe of the x1A88 decision stream at the first divergence,
  fd-falco @-39 class), then classify/lock the suite and wire it into the aggregate.

- 2026-07-27 — `retained` (first Nana fidelity seed)
  Scope: session 2 traced the universal frames-83..204 follower divergence to the hosted UCF
  pad-buffer injection: it replaced Nana's AI stick with the port's cardinal-snapped raw pad and
  double-shifted the ring. The real gecko wraps its whole body in !Player_IsCPU, and Player_IsCPU
  IS ftCo_800A2040 (GALE01r2 map) — the predicate the CPU import made real.
  Evidence: with the one-line gate, container icies prefixes moved 83-204 → 95-1,416
  (medium-sheik: 21s of bit-exact two-climber play); aggregate unchanged (gate is vacuous for
  humans). Next layer, all classic per-mechanism families: itclimbersice SendItemInfo misc lanes
  (192/0 vs 64/255 @-27 bf-falco), 1-ULP Nana pos_x @98 fd-falco (fusion site), a crouch-mimic
  action pick @26 bf-falco.

- 2026-07-27 — `retained` (session 3: three source fixes, rollback boundary identified)
  Scope: (1) ftCo_800ADE48 upstream decomp had the blast-zone bounds branch INVERTED against
  GALE01 asm (800ADF50: beq skips the reset for found in-bounds targets; the C reset them) —
  every valid CPU navigation target was clobbered to self-floor, which kept Nana inert during
  the entry fall (retail general-CPU steers her toward Popo's floor position with
  LstickXTowardDestination 0x7F from frame -123 until mimic engages at landing). The sibling
  bounds-check sites (A3908 ×3, A4038 ×3, A648C, B33B0, A21FC, A8210, AE7AC, inlineD0s) were
  swept against asm and are faithful. (2) ftCo_800B0AF4's x2225_b3 mimic position lerp is a PPC
  fmadd (0.05*ring rounded once, then fused 0.95*cur + t before frsp); the C's two rounded
  products drifted 1 ulp — the whole follower_pos_x 1-ULP family. Fixed with __builtin_fma per
  the lbcollision.c precedent. (3) item_var_source_byte gained native pointer-widening cases for
  It_Kind_IceClimber_Ice (offset 3 = owner GObj low byte, 7 = live scale f32) and GumStrings
  (7/0x17 = link/joint pointer low bytes); 0x1B and ice 0x17 are pool residue.
  Evidence: container icies first-mismatch fronts moved from -39/-27-class to per-replay
  engage-adjacent fronts; e.g. ys-fox prefix 261→4,258, gm-peach first -39→378 (item.vel ULP),
  medium-sheik 1,416→316 (engage-front reshuffle). Aggregate re-verified vacuous.
  Boundary finding: the residual follower fronts are one-frame stick skews at mimic
  engage/disengage boundaries where the recorded Nana pre-joystick is a ring slot one NEWER or
  one OLDER than the (asm-verified) cursor arithmetic can produce from the finalized Popo rows
  (bf-marth @12 = popo@7 under steady d6; ics-ditto @187 = a -125 flick-transit sample where the
  straight-line ring holds -128; both directions occur). Every candidate mechanism (ring cursor
  code, B101C/B2AFC/B3900 call orders, UCF gecko rewrite timing vs the 0x8006B0E0 record hook,
  A17E4 quantizer) was verified against asm/refs and eliminated: the recordings are netplay
  rollback captures, and Nana's mimic ring is the first supported state whose content depends on
  input ARRIVAL timing, which straight-line re-simulation of finalized rows cannot reproduce.
  Disposition: treat engage-boundary skews as per-replay classifications (rollback-recording
  owner) unless a Dolphin playback probe shows Slippi playback itself reproduces the recorded
  follower rows; remaining fixable fronts are the ice-block item-motion ULP family (gm-peach
  @378) and the fod-sheik @297 item stream.

- 2026-07-27 — `retained` (session 3 continued: UCF follower retro-write, probe loop established)
  Scope: the bf-marth @12 engage-front was NOT a rollback artifact. A new Dolphin interpreter
  probe (slippi-dolphin engine-dump branch, MSL_NANA_RING_PROBE_*: logs the B0918 capture call
  and the B0AF4 playback exit with ring cursors and slot content) showed the slot recorded at
  slippi 6 with the -86 flick-transit sample being read back at slippi 12 as -128: UCF's
  dashback gecko (refs/ucf/src/dashback/dashback.cpp, Interrupt_AS_Turn+0x4C) retroactively
  rewrites the paired sub-character's freshest mimic-ring slot (full-deflection stick + new
  facing) when the leader's dashback converts. Ported into msl_ucf_apply_dashback (8a191e85);
  bf-marth prefix 134→179. Probe frame anchor: probe/dump frame N = slippi N; sim tick trace
  t = slippi + 132; ring capture is one frame stale (prio-2 think before prio-3 input
  processing), steady playback delay is 6 frames.
  Boundary of the next front (ys-fox @14, medium-sheik @194, bf-falco @71 class): retail Nana
  sits DISENGAGED in general-CPU state x18=10 emitting stick 0 while Popo airdodges (probe
  shows xFA mim bit clear, x18=10 constant); our sim demotes x18 10→1 at the frame Popo's
  stick returns to neutral and then walks toward the surviving x54 target (ftCo_800ACD5C's
  early branches differ). Islands are live natively (probe: non-NULL island, A2718=0) and x54
  correctly tracks Popo's airdodge floor in both. Suspect surface: the scene-keyed hosted
  mirrors consulted by ACD5C/ADE48 — gm_8016C75C standings-cache value at match start (x88/x8C
  timer arming) and gm_801A4310 major-scene id vs 0x1C — plus ftCo_800A21FC/A3554 gates.
  Next: probe gm_8016C75C/gm_801A4310 returns retail-side during the ys-fox window, align the
  hosted mirrors, then reassess which residual fronts remain for classification (item misc0
  pointer bytes, misc2/3 pool residue, item-motion ULP family per the dolphin-profile
  precedent).

- 2026-07-27 — `retained` (session 3 continued: CPU size-field publication)
  Scope: the state-10 front's root cause was NOT the scene mirrors: the retail probe showed
  ftCo_800A3554 passing with x38 ~= 8.9 (breathing per frame) against our 2.0. x38 comes from
  the partner's x1A88.x564 hurt-capsule half-span, published every frame by ftCo_800A0DA4 in
  retail; the hosted build had substituted the dynamics-only capsule publication and never
  wrote x55C/x560/x564/x568 (dead state before Nana). Fixed in 59b0af15: run the full retail
  publication whenever a CPU-input fighter exists (msl_fighter_cpu_input_live), keep the cheap
  path for human-only matches. Container icies prefixes moved dramatically: ys-fox 269 -> 5,694,
  bf-marth 179 -> 6,489, auto-falco 256 -> 5,731, medium-sheik 316 -> 2,643, ics-ditto
  309 -> 2,269; master-fox unchanged (@147 damage-exit family).
  Tooling note (user-reported): the extracted probe driver's `[DSP] Backend = NullSound` is an
  invalid backend name in the engine-dump Dolphin and silently falls back to the audible
  default; the correct silent config is `Backend = No Audio Output` + `Muted = True` +
  `Volume = 0` (fixed in the working copy; apply when tools/dolphin merges to a branch).

- 2026-07-27 — `retained` (session 3 finale: PlCo CPU table translation, 76c44b50)
  Scope: the B4AB0 instrument revealed the true owner of every remaining behavioral front:
  Fighter_804D64FC_t declares its table members void**, so the DWARF-driven native layout
  emitted POINTER->POINTER->VOID and the CPU attack tables/thresholds/scripts were copied as
  raw big-endian bytes (probed entry cmd=0x02000000 = swapped 2). No CPU-driven fighter could
  ever select an attack. Hand translator (native_dat.c translate_fighter_cpu_tables): per-kind
  [33] pointer arrays of cmd-terminated 0x24-byte attack entries, float[33] thresholds,
  float[6] weapon reach, and byte command scripts indexed by attack command id (count 64 — 39
  segfaulted via xA4 ids 0x28/0x29; past-array slots read non-relocated -> NULL).
  Evidence: prefixes gm-peach 747->9,966 (96% of the replay bit-exact), dl-fox 360->5,086,
  bf-falco->4,798, master-fox 269->2,434, fod-sheik->3,333, ics-ditto->3,021; aggregate
  183/63/0 unchanged. Remaining first-mismatch fronts are now dominated by the unrecordable
  item.misc0/2/3 pointer/pool-residue bytes at each ice-block spawn (classification class),
  plus fd-falco @346 (follower CliffWait pick) and the gm-peach @378 item-motion ULP family.

- 2026-07-27 — `retained` (session 3 coda: live standings, d2ab8923)
  Scope: the fd-falco @346 front (follower CliffWait pick) probe showed retail's
  gm_8016C75C returning the UPDATED KO count (1) at Nana's first post-KO state-10 tick with
  a dpad-up press (held=8) from ftCo_800ACD5C's x88 branch: retail's x24C scratch is dirtied
  between reads, so the observable behavior is a live standings recompute, not the frozen
  first-read snapshot the hosted mirror served. Made gm_8016C75C recompute per call.
  Evidence: bf-falco prefix 4,798->9,176 (95%), fd-falco 468->7,979 (95%), master-fox
  2,434->5,948, fod-sheik->4,086, ics-ditto->3,825, medium-sheik->3,962; aggregate 183/63/0.
  Note: MslCoreMatchRules.cpu_standings_snapshot_valid is now dead state (cleanup with the
  next wire/struct touch).
  Current fronts after d2ab8923 (prefixes 302-9,966): (a) item.misc0/2/3 pointer + pool
  residue lanes lead 9 of 12 replays (classification class); (b) micro AI-stick episodes:
  ics-ditto @1068 = state-4 recovery DI steering quantization phase (retail alternates
  -88/-127 with +88 up per the AA320 Nana stick=0x40 increments; ours slightly offset —
  suspect an upstream 1-ULP or an x570 draw), medium-sheik @210 = one-frame jump-decision
  skew (Landing vs KneeBend), ys-fox @5572 = ice-block scale low-byte drift (misc1 32 vs 72,
  ULP-class); (c) gm-peach @378 item-motion ULP family. The behavioral tail is now a handful
  of one-frame/1-ULP episodes per replay; classification authoring for (a)+(c) plus episode
  attribution for (b) is the remaining path to flipping the suite to CLASSIFIED.

- 2026-07-27 — `open` (gm-peach @3986 attributed to a disengaged general-CPU dash pick)
  Probe: retail Nana disengages at 3983 (xFA mim bit clear; ring slot +127 IGNORED) and the
  general CPU emits a full -127 stick at 3986 -> dash-left at 3987 (ground -1.4); ours emits
  a partial steering stick (~-45, -0.3545 momentum) -> walk. Same class as ics-ditto @1068
  and medium-sheik @210: the disengaged-CPU state/script pick differs for one frame. These
  three episodes (plus ys-fox @5572 scale-byte ULP and the item misc/motion classification
  classes) are the entire remaining icies distance. Next iteration: our-side x18/script trace
  at gm-peach 3975-3990 vs the retail x18 evolution (extend the ring probe with the ADE48
  cascade returns already wired for ACD5C) to find which state handler picks the full-stick
  dash command in retail.

- 2026-07-27 — `open` (gm-peach @3986 refined: identical stick, divergent landing physics)
  Follow-up traces (our tick + AB224 branch vs the retail ring probe) show BOTH sims emit the
  full -127 at slippi 3986 in state 1 (same-island AA42C walker) and dash at 3987; published
  lanes match through 3985. The @3986 delta (air self -0.3895 vs -0.3545 = one accel
  quantum; pos 0.035) therefore arises INSIDE frame 3986's Landing-state update with
  identical inputs — an unpublished state bit or IASA-order detail. Blocked on visibility:
  the engine dump carries only leader rows (port_count=2). Next tooling step: add follower
  fighter rows to the engine-dump writer (slippi-dolphin engine-dump branch) or an
  interpreter probe over ftCo_Landing IASA/physics for Nana at 3985-3987, then diff
  self-vel/decel inputs. gm-peach anchors: our tick t = slippi + 128 for the FIRST Nana
  lifetime; x7C resets on her respawn (filter traces by position, not t, across lifetimes).

- 2026-07-27 — `retained` (cascade-input bit-verification; episode isolated to script cadence)
  A paired probe of the ADE48 cascade's ftCo_800A3554(fp, 0) call (new kCascadeSafeCall PC
  0x800AE768 retail-side; msl_casc_trace sim-side) shows at gm-peach tick 3987 the AI inputs
  are BIT-IDENTICAL across sims: x54x 0x41321AB4, and x38 0x40C9B274 — i.e. the hosted
  hurt-capsule extents publication (59b0af15) is bit-exact against retail, and targets/x60
  match. The only difference is the post-3986 position itself: retail's walk script fired at
  tick 3986 (ring probe: ls -127 set during 3986 -> dash during 3986), ours at 3987. With
  identical state at every sampled tick, the残 divergence is the EXPIRY TICK of the recovery
  script chain built during her damage/air phase (csP/command_duration cadence) — one more
  probe layer: log ftCo_800B49F4 script-finalize ticks + durations on both sides across the
  recovery (~3950-3987) and find the first build tick that differs, then asm-diff that
  handler. All three residual behavioral episodes are this same cadence class.

- 2026-07-27 — `retained` (script-cadence layer probed; episodes converge on A2C80 recovery entry)
  The kScriptFinalize probe (0x800B49F4, logs per-build frame/x18/buffer bytes) shows retail
  building a state-4 recovery script EVERY air frame 3945-3982, demoting to x18=1 at the 3983
  landing, and building the state-1 AA42C walk script at 3984: SetLstickY 0;
  LstickXTowardDestination 0; WaitFor 2; LstickXTowardDestination 0x7F; Done - the WaitFor 2
  lands the full -127 exactly at 3986. Ours builds the same script one frame later (3985)
  because our x18 was 10 (not 4) through the recovery: our sim never entered the A2C80
  recovery state for THIS knockback while retail did (the dl-fox episode was the same gate
  agreeing; here it disagrees). All three residual behavioral episodes therefore converge on
  ftCo_800A2C80's per-knockback evaluation during the air phase - its inputs are the
  xFA_b5 in-bounds flag (B33B0's own bounds chain over the floor probe), the fall-angle
  lb_8000D008 gate, and the long recovery ray. Next: probe A2C80's ENTRY inputs (pos_delta,
  xFA_b5, the ray result) retail-side across the air phase (kA2c80Entry already logs dx/dy/
  xfa; add the mpCheckFloor ray result PC) and mirror sim-side; the first differing input
  identifies the owner (suspect: xFA_b5 phase, since B33B0's bounds chain runs every tick
  and its in_bounds -> xFA_b5 store was asm-verified but its mpCheckFloor probe inputs
  depend on cur_pos while airborne).

- 2026-07-27 — `retained` (A2C80 exonerated at the gm-peach onset; divergence is a state-4 exit)
  Paired A2C80 gate traces at the knockback onset (retail kA2c80Entry/kA2c80Ret vs sim-side
  msl_a2c80_trace): BOTH sims enter recovery state 4 at the same tick (slippi 3925, our
  t=979; angle bf8be887 passes, xFA_b5 clear, floor ray finds nothing, ret 1). The earlier
  'ours never entered 4' reading was wrong. Retail stays x18=4 through 3982 (script probe);
  ours is x18=10 by t=1038 (3984): our sim EXITS state 4 somewhere in 3925-3983 mid-air.
  Next (first action of the continuation): sample our x18 per tick across t=979-1042 (the
  tick trace prints it; one grep), find the exit tick, then diff the state-4 handlers'
  gates at that tick — case-4 of ftCo_800B2790 (grounded demote / ftCo_800A9904 air) and
  ftCo_800B24B8 (case 4 of B2AFC) against the retail probes. The one-frame walk-script
  cadence at landing (and hence the entire episode class) follows from whichever gate
  flips ours out of 4 early.

- 2026-07-27 — `retained` (the episode split is the landing-tick state demote: 4->1 vs 4->10)
  Definitive lifetime-2 timeline (gm-peach, NANA-tick t = slippi - 2945; beware TWO Nana
  lifetimes sharing t ranges — filter by position): both sims enter state 4 at slippi 3925
  with bit-matching gate inputs (angle/dx sequences identical), both steer -84 through 3982,
  both land during 3983. At tick 3983 retail demotes 4->1 (ftCo_800B2790 case-4 grounded
  path: x18 = x1C, Done) and builds the AA42C walk script at 3984; OURS lands in x18=10 at
  the same tick, spends 3984 in ACD5C (stick 0), demotes to 1 at 3985, and builds the walk
  script one frame late — the entire one-frame cadence divergence. The ONLY x18 = x20(=10)
  writer is the ADE48 cascade hop guarded by ftCo_800A3554(fp, 0), which cannot pass while
  airborne at the think; so either the hop fired against expectation (trace it: one
  msl-trace at the cascade hop and at the case-4 grounded demote, logging tick + which path)
  or an unaudited writer exists. FIRST ACTION NEXT SESSION: instrument those two sites, run
  gm-peach --frames 4115, read the single line that says which path wrote 10 at t=1038, then
  asm-diff that path. Everything else (episodes, classifications, locks, wiring) hangs off
  this one answer.

- 2026-07-27 — `retained` (the write is the cascade hop; retail never reaches it at 3983)
  The x18-write trace answered the pinned question: OUR 4->10 at the landing tick is the
  ADE48 cascade final transition (A3554(fp,0) passing because ga publishes Ground at the
  think while motion still lags in Fall, and the ADE48 head reset x54 to her own position,
  making dist 0). The retail cascade probe shows NO A3554(0) call at 3983 — its first CALL
  is 3984 — so retail's ADE48 exits BEFORE the transition cascade at exactly that tick,
  while ours falls through. Remaining search space is tiny: ADE48's pre-transition early
  exits at one tick — the x18==0x12 return, the x221A_b3 hitlag switch_cmd path
  (B4A78 + x18=0x12), and the motion-keyed 0x125/0x154 -> x11/x13 branches. NEXT: either
  sim-side log which ADE48 exit path runs at the landing tick for both x18 histories, or a
  retail PC probe over ADE48's early-return addresses at 3983. Note the ga-vs-motion phase
  at landing think (ga=Ground from the prio-6 map pass of the PREVIOUS frame while
  motion_id still Fall) is what arms A3554 grounded gates one tick before the Landing
  state exists — REFINED by direct predicate probes (kB4ab0Entry + cascade
  predicate returns): at the landing ticks retail calls NONE of A5ACC/B9CBC/B8A9C/B732C/
  A3710/B4AB0 (their first calls are 3984, all returning 0), so retail's ADE48 exits BEFORE
  the whole predicate block while ours reaches the final transition and hops. Also note the
  NANA tick-trace anchor correction: t-row(F) prints post@(F-1) motion, so our landing motion
  (42) appears at post@3982 — SAME frame as retail; the earlier one-frame-landing readings
  were anchor artifacts. The remaining search space is the ADE48 region between its head and
  the bl ftCo_800A5ACC (asm 0x800AE460-0x800AE568: the x18==0x12 return, the x221A_b3
  switch_cmd block, and the motion-keyed 0x125/0x154 -> 0x11/0x13 and 0xBF/0xB7 -> 5,
  0xFC/0xFD -> 6 sets): add a PC-trace over that range (MSL_PROBE_PC_TRACE with PC_START/
  PC_END already exists!) for one retail tick and mirror the taken branch sim-side. It is
  the last unexplained divergence gate. NOT ftCo_800B8A9C returning TRUE (x18 -> 2, returning before the final transition — matching
  the missing CALL row) with B2790's case-2 handler demoting 2 -> 1 the SAME tick (matching
  retail's end-of-tick x18=1 and the 3984 walk script). Ours' B8A9C returns FALSE at that
  tick. So the last mile is ftCo_800B4AB0's per-entry evaluation (or B8A9C's target-state
  gates A3134/A3200 on Peach) at ONE tick with known inputs — instrument the entry loop
  (which entries pass the level/cmd gates, the predicted relx/relPredY per entry, and the
  final selection) sim-side, and the same via an interpreter probe on B4AB0's entry/return
  retail-side. Suspects: the f64-mixed prediction arithmetic (lines ~161-213, fmadd-class),
  or a subtle table-translation field. This single comparison closes the episode class.

- 2026-07-27 — `retained` (BREAKTHROUGH: cascade repair landed; first replay CLASSIFIED)
  The compensator was a FOURTH upstream decomp drop: ftCo_800B2790's case 4 (and case 19)
  read special_floor UNINITIALIZED where the asm carries a zeroed register — the garbage
  routed retail-faithful landings into ftCo_800A0148's blast-zone escape script, masking the
  missing cascade exit. Landing all three repairs together (9bc1b259: the ADE48 x18==4
  cascade exit + both zero-inits) restored the retail landing sequence and moved the suite
  to matched rows 4,215-14,150: ys-fox 8,140/8,152, master-fox 8,543/8,610, gm-peach
  10,299/10,401, medium-marth 14,150/14,709. ys-fox's entire residue = 12 rows of belay
  string item.misc1/misc2 = Slippi's raw bytes over retail heap ItemLink/JObj pointer
  identities — unrecordable in principle — and is now CLASSIFIED (0a20c832,
  id unrecorded-item-pointer-identity, full native snapshot; note the manifest was
  re-serialized with indent=1, formatting churn only). SUITE: 0 pass / 1 CLASSIFIED /
  11 fail; aggregate 183/63/0 verified after both commits.
  Remaining: the other 11 replays now follow the same endgame — small residues per replay
  (master-fox 67 rows, gm-peach 102) to attribute as fix-or-classify, then locks and
  aggregate wiring.

- 2026-07-27 — `open` (ADE48 cascade truth table complete; the x18==4 exit contradiction)
  Full asm branch-skeleton extraction (0x800AE280-0x800AE7A8, saved recipe in-session): EVERY
  state guard in the transition cascade exits the function when x18 already equals it
  (fifteen exits; guard 7's exit also calls ftCo_800BB9B4 first). The C's PYRAMID NESTING
  actually implements fourteen of them (each `if (x18 != N) {` nests the whole remainder and
  the closing braces fall to returns) — the ONLY non-nesting guard is x18 != 4 at the
  ftCo_800A2C80 site: when x18==4 the C uniquely falls through into the 0xF+ guards. That is
  the single genuinely dropped exit. CONTRADICTION TO RESOLVE: adding the asm-faithful
  `else return` REGRESSED the suite (gm-peach matched rows 9,966 -> 4,077 — note the FAIL
  column is total matched rows, not prefix), which should be impossible against a retail
  recording unless (a) a compensating infidelity elsewhere currently cancels the fall-through
  bug (find it by tracing the with-fix landing sequence: ours should now build the walk
  script at 3984 and dash at 3986 exactly like the retail probe — verify, then chase where
  the tail diverges instead), WITH-FIX TRACE RESULT (the compensator, partially unmasked): with the
  else-return applied, the landing tick builds a TEN-frame WaitFor script (csp=1 dur=10) and
  x18 HOLDS 4 through 1038-1047 — the B2790 case-4 grounded demote never fires even though
  the A3554-based analysis proved ga=Ground at that think. Suspects: the script was built by
  something reached before case-4 (ftCo_800A8DE4_noinline preamble? an ACD5C-shaped WaitFor
  0xA script implies a case-10 dispatch — check whether x18 was still 10 from the previous
  frame's structures), or the csP gate sequencing. DISPATCH-TRACE RESULT (fix + case-id/ga logged): the landing tick DOES
  dispatch case 4 with ga=Ground (t=1037, the demote path executes), but NO dispatch occurs
  for at least the next 8 ticks — the post-demote script gate (csP/command_duration) stays
  blocked even though ftCo_800B3E04's Done handler (csP=NULL, dur=0) is verified correct in
  isolation and ftCo_800B49F4 runs at B2790's tail. Retail's script probe shows builds
  CONTINUING (3984+). NEXT: one run with csP/dur logged at B3E04 entry/exit and B49F4 for
  the landing tick — the blocked gate is the last link; beware the two-Nana-lifetime t-space
  collision (filter by position) and that DISP prints only when the gate passes.
  Hypothesis (b) ELIMINATED: no UCF or slippi-ssbm-asm injection touches 0x800ADE48-0x800AE7AC, so the recording's ADE48 is vanilla and (a) — a compensating infidelity that currently cancels the missing exit — is the operative theory; the with-fix landing-sequence trace comparison is the way in. The reverted change is
  one `else { return; }` at the A2C80 guard — trivial to re-apply once (a)/(b) is resolved.

- 2026-07-27 — `open` (the x18==4 cascade exit: asm-real, but the naive port regresses)
  PC-coverage trace (generic MSL_PROBE_PC_TRACE over 0x800AE0F0-0x800AE568 at the landing
  tick) shows retail's ADE48 executing: motion 0xFC/0xFD checks, x18==9 guard, IsGrabbing,
  then `lwz x18; cmpwi 4; beq .L_800AE484 -> li r3,1; b .L_800AE790` = FUNCTION EPILOGUE:
  when already in recovery state 4 the transition cascade EXITS (the asm function returns a
  transitioned flag the void C drops). The upstream C falls through — a second control-flow
  drop in this TU. HOWEVER the one-line `else return` port empirically REGRESSED the suite
  (gm-peach's landing episode grew into leader-lane divergence from 4100; master-fox/
  ics-ditto/bf-falco prefixes dropped) and was reverted (uncommitted). Hypotheses for the
  regression: the exit's r3=1 gates caller behavior not modeled by the void C; or the other
  state guards (9/5/6/...) have similar unported exits so restoring only the 4-exit skews
  the state machine asymmetrically; or downstream within-frame RNG draw-order compensations.
  NEXT SESSION: audit the ENTIRE ADE48 transition block against asm as a unit (the same
  PC-coverage probe run at a few ticks in different states gives the ground truth per
  guard), port ALL its exits together, and re-verify. The committed tree (117a2053 state)
  remains the best-verified point: prefixes 302-9,966.

- 2026-07-27 — `open` (dl-fox @238 root-cause narrowed to ftCo_800A2C80's long recovery ray)
  Scope: with the tick trace aligned (our t = slippi + 133 on dl-fox), our Nana's x18 path
  through her damage (~f210-236) is 1 -> 4 -> 10 -> 1 while retail lands in x18=2 (attack).
  Both sims run the ADE48 transition cascade (x221A_b3 hitlag arming verified); the split is
  ftCo_800A2C80 ("falling toward doom" recovery check, NOT in the earlier bounds sweep):
  ours returns 1 (-> state 4 recover) where retail returns 0 and falls through to
  ftCo_800B8A9C (attack tables ARE populated natively — probed non-NULL). A2C80 casts a
  1000-unit ray from the ECB bottom along the normalized fall direction through mpCheckFloor
  and returns 0 when it hits an in-bounds floor. REFUTED by a follow-up probe (kA2c80Entry/kA2c80Ret added
  to the Nana ring probe): retail A2C80 ALSO returns 1 at f210 — both sims enter recovery
  state 4 identically, and hosted mpCheckFloor is the verbatim retail loop (mpBoundingCheck2
  broadphase, no O1 shortcut). The real split is AFTER landing (~f223): both demote out of 4
  and re-run the ADE48 cascade; retail's ftCo_800B8A9C ground path returns TRUE (x18=2,
  attack) while ours always returns false. Gates verified equal (xF9_b2 set, target present);
  the per-kind attack table pointer ((void**)Fighter_804D64FC->x4)[FTKIND_NANA] is translated
  and non-NULL natively (probed host pointers). NEXT INSTRUMENT: ftCo_800B4AB0 (the CPU
  attack-script evaluator) — log its parse walk and result natively vs a retail interpreter
  probe at its call sites; suspects are the attack-script PAYLOAD translation (per-entry
  structures behind the per-kind table read via retail byte offsets would break under native
  widening) and its internal ftcpuattack range math reading target x55C spans. This family
  plausibly owns the remaining behavioral fronts (dl-fox @238 walk-vs-dash after the missed
  attack, ys-fox @5572+, medium-sheik @194 Landing-vs-Turn = a missed script jump,
  master-fox @147 damage-exit).
  Scope: retail ring probe on dl-fox frames 225-242 (dl_ring2.jsonl recipe): retail Nana is
  DISENGAGED (xFA mim bit clear) in general-CPU x18=2 from at least 225 through 242, in Wait
  with friction slide (-0.3498) while Popo dashes; the UCF retro-write slot (-128) is present
  in her ring but ignored. Ours re-engages/dashes at ~237 (ground -1.4). x7C phase MATCHES
  retail (x7C = slippi + 132 in both, so the %-gate phases are aligned). gm_8016C75C mirror
  re-verified faithful (one zero-KO snapshot at first read arms nothing). The divergence is in
  internal x1A88 state evolution through Nana's damage reaction (~frames 210-235): retail
  lands in x18=2 (escape) and stays; our x18 path unknown — needs the our-side tick trace
  (rebuild container with the ftCo_800B3900 tail trace from msl-icies-port-state) compared
  frame-by-frame, then an asm diff of the diverging handler. Candidate handlers: the damage
  reaction chain (ftCo_800B4A78 -> x18=0x12 -> ftCo_800AC5A0 case 18 -> demotes), case 2
  ftCo_800B04DC. Note B0E98 re-engage requires pos_delta diff below co_attrs.mid_walk_point,
  which should REJECT engage while Popo dashes (delta gap ~1.05) — whichever sim engaged there
  is the wrong one, and ours engaged.
  Evidence: dual-entity player machinery already compiled in and inert; CPU predicate stubbed
  false; validation reads only `ports.P{n}.leader`; UCF pad ring would double-shift if Nana took
  the pad path. Nana's AI is type 6 (mimic ring + follow), NOT the general CPU tree; reachable
  slice bounded inside NonMatching `ftCo_0A01.c` + `ftcpuattack.c`.
  Disposition: proceed with sequencing above; step-2 import is mechanical (all 11 chara/article
  TUs are Matching upstream).

- 2026-07-28 — `retained` (FOD CONSTRUCTION RNG FIX, commit 4fa63d66; probes 3325a6f218)
  The fod-sheik/auto-falco follower fronts traced to Nana's x1A88.x7C AI phase counter:
  its init value is (int)(10*HSD_Randf()) drawn at fighter creation, and our FoD
  construction stream sat two draws EARLY because the hosted grIzumi platform proc
  early-returns to the Slippi height stream and never consumed retail's two creation-tick
  HSD_Randi draws (grIzumi_801CC358 case 0 via rand_range; case 3 for below-ground
  starts). Nana drew x7C=7 where retail drew 0; every x7C%N gate (x56C %600 follow-radius
  redraw, x570 %30, DI %120) fired 7 ticks early, sampling different stream positions.
  Fix: msl_grizumi_consume_replay_creation_draw at match construction post-OnInit, gated
  on msl_slippi_fod_platform_height owning the creation publication; do NOT touch
  xC4/xC6 (arming them at creation broke all 27 human Fountain aggregate replays through
  mistimed platform self-motion — the machine still runs event-quiescent frames).
  VERIFIED per-stage construction draw counts vs retail (Randf probe + first-A101C
  chain distance): BF 1=1, DL 2=2, YS 1=1, FD 4=4 (grLast port), PS 1=1, FoD 2=2 after
  fix. Retail x7C inits per replay are now reproduced exactly (fod: Popo 7, Nana 0,
  Sheik 4, Zelda 0). fod-sheik 4,215 -> 5,077 matched; aggregate 183/63/0 green.
  KEY RECIPES: (a) HSD_Randi INLINES the LCG (0x80380588) — a Randf-entry probe never
  sees Randi draws; infer via chain distance. (b) CPUCore=0 in the probe user-dir
  Dolphin.ini interprets the whole boot so construction draws (frame counter -124) hit
  the interpreter probes without the EXI window. (c) Slippi ONLINE per-frame seeds are
  synthesized (base + frame<<16) — construction draw-count divergence is erased per
  frame, EXCEPT values that persist (A101C x7C, B9704 x34).
  NEXT FRONT (fod-sheik @1248): follower_speed -0.725372 vs -0.726160 while mimicking
  (stick 125 walking), NOT a /127 byte value on our side — ring/clamp interplay again;
  ring probe run at frames 1240-1256 pending analysis.

- 2026-07-28 — `retained` (UCF DASHBACK OVER-FIRE FIX, commit after 4fa63d66)
  The fod-sheik @1248 mimic front: retail ring probe showed the slot playing at x7C=1371
  held 125 = trunc(127 x 0.986) natural capture while ours held a retro-written 127. The
  gecko replaces the facing store at Interrupt_AS_Turn+0x4C, which lives INSIDE the
  if (!has_turned) branch of ftCo_Turn_IASA; our hook ran unconditionally after the if,
  so already-turned smash turns (has_turned set at frame 1) still converted at anim
  frame 2 (87/88 fires in fod-sheik were spurious). Moving the call inside the branch:
  fod-sheik 5,077 -> 9,724, medium-sheik -> 14,279, ics-ditto -> 11,693, fd-falco ->
  8,117, bf-marth -> 6,993, auto-falco -> 6,654; aggregate 183/63/0. DIAGNOSIS RECIPE:
  the DASHBACK-FIRE env trace (fire-time anim/has_turned/x670/raws) + the ring probe's
  natural-vs-retro slot values pinpointed the condition delta in minutes.

- 2026-07-28 — `retained` (BLIZZARD SPAWN FMADDS, commit 5023a036) + `open` (dl-fox @5082
  = Fox TAIL dynamics). The dl-fox item vel/pos ULP family (4476+) was the blizzard puff
  spawn angle: GALE01 0x802C2290 fuses (x10-xC)*rand+xC; __fmadds port retired the family
  on four replays. The remaining dl-fox fork @5082: Popo usmash frame 9 hits in retail,
  whiffs in ours. Collision probe (dl_coll_probe3.jsonl in job tmp 164f8743) + sim-side
  HB/HU dump (scalar.c MSL_HB_TRACE, container-only): our usmash HITBOX x4C bits are
  RETAIL-IDENTICAL (incl the z=±8.35 swing arc — verified byte-exact at the 981 usmash
  too), and 12 of Fox's 13 hurt capsules match retail x/y bits exactly. The 13th — bone_idx
  18, offsets (0,0,±0.8), scale 1.62 = FOX'S TAIL, a dynamics-driven chain — sits at
  (49.26, 5.97) in ours vs retail (50.62, 7.97, -0.65): 2.4 units of accumulated dynamics
  divergence, and retail's hit lands exactly on it (coll_distance +0.319). NEXT: the tail
  dynamics chain (lb dynamics springs + Whispy wind coupling on DL + lb_800115F4 emitter
  phase); the marios sessions' dynamics/quat toolkit applies. NOTE the retail probe
  numbering: this window's collision-probe frames aligned 1:1 with validator frames.
  TOOLING (container-only, uncommitted): MSL_ICE_TRACE_* (per-frame item kind/vel/pos
  bits at export) and MSL_HB_TRACE_* (fighter hit capsules x4C/x58 + hurt capsule a/b
  positions at export) in /work/src/runtime/scalar.c; validate_replay.py detail rows
  widened to 14 in the container copy.

- 2026-07-28 — `open` (WHISPY WIND MACHINE — dl-fox 5,086 -> 10,818, UNCOMMITTED: aggregate
  gate blocked). Root cause of the dl-fox tail divergence: hosted grOldPupupu_802113E0
  SHORT-CIRCUITED Whispy's whole machine to the replay xDC direction (the FoD-platform
  lesson one level up), so the blow loop's wind force emitters (lb_80011A50 every 10
  ticks while blowing, first at f~754 on dl-fox) never spawned and NO fighter tail/body
  dynamics chain ever felt wind. Sim-side proof: msl_emitter_trace (container-only, in
  lbspdisplay.c lb_80011A50/lb_800119DC + scalar.c helper) logged ZERO emitters pre-fix,
  243 post-fix. Fix in working tree (Mac+container, uncommitted): delete the hosted
  early-return; the retail machine runs (its rand_range draws are prio-4 post-think =
  reseed-erased; DL construction counts already matched), and apply_replay_stage_events
  still republishes recorded xDC pre-frame for wind-push consumers.
  RESULTS: dl-fox 10,818/11,083 (the 5082 tail usmash now connects); icies otherwise
  unchanged. AGGREGATE: 164/54/28 — 22/28 fails are LOCK-DRIFT ONLY (mismatch_rows=0,
  tails are in the locked output; locks need re-recording once green) + 6 REAL row fails
  (puff Game_20260602 8,613 rows; luigi medium-fox 2,022; marth Goshawk 1,766 +
  QuestionableHarmfulPanther 74; marios plat-marth 72; marios medium-fox-2025-12 ONE
  row) — knife-edge marginal outcomes flipped because wind-blown dynamics are close but
  not bit-exact yet.
  THE REMAINING ULP CLASS (pre-wind, reproducible): dl-fox tail bit-exact through frame
  46, then Fox's hit/hitlag (47-50, both sims' DD94 correctly skip — cadence verified
  identical via MSL_DD94_TRACE vs the retail MaybeCaptureTailProbe rows) — capsules
  bit-equal again at 51 — then ONE dynamics tick at 52 forks 0.05 (~100 ULPs). Suspect:
  the hitlag/damage SHAKE displacement feeding the dynamics origins/colliders at the
  resume tick (hosted may skip the model vibrate as visual-only), or the colliders
  (&fp->x1670, ftColl_8007AF60) / x2228_b1 arg at 52. NEXT: dump solver inputs at
  validator 51-52 both sides (parent_mtx origin, colliders, arg1, ret_B0) — the
  MaybeCaptureTailProbe (slippi-dolphin, PC 0x8009DD94 entry, port==1 1-BASED) plus a
  matching sim trace. Transients DAMP when quiet (exact at 200-210 & 38-46; 0.61@400
  after shine, 0.23@600) — fix the per-event seed, chaos disappears.
  TOOLING NOTES: retail probe rows appear only when DD94 runs (hitlag gaps are DATA);
  probe frame == validator frame here; our export HB/HU rows are end-of-frame; sim
  frame_id in traces = validator frame - 1 (DD94 trace f=35..45 == validator 36..46).
  Retail per-frame tail baselines saved: job tmp 164f8743/tail_*.jsonl (windows -20..-10,
  38..62, 60..70, 200..210, 400..410, 600..610) + our_tail.txt (post-whispy-fix).
  A giant interpreter window (-123..990) STALLS playback at -123 — sample 10-frame
  windows instead.

- 2026-07-28 — `open` (tail fork: SOLVER INPUTS EXONERATED). The dl-fox validator-52
  one-tick fork: retail MaybeCaptureTailProbe (extended: arg1/ncol/collider dump,
  slippi-dolphin committed) vs sim DD94IN trace shows arg1 (x2228_b1, constant 0) and
  the x1670 collider list (ncol=1, world pos + radius) BIT-IDENTICAL at every tick 44-56
  including the fork. The divergence is inside the tail chain state itself. CAVEAT: the
  capsule comparisons carry a sampling skew at the hitlag boundary (retail probe reads
  capsule 12 at DD94 ENTRY = pre-tick; our HB trace exports post-frame), so the "equal
  at 51, 0.05 apart at 52" reading may be one tick off. NEXT: dump the CHAIN STATE
  per-link (DynamicsData jobj rotate quats + desc.lb_unk0.unk_58/unk_2C) at DD94 entry
  BOTH sides for validator 46-53 and find the first diverging link+field; then audit
  that op (msl_dynamics_build_basis / transform helpers vs Gekko paired-single PSMTX).
  PROBE QUIRK (reproduced twice): a tail-probe window 44..56 yields ZERO rows while
  38..62 works — start windows earlier/wider. Sim traces: MSL_DD94_TRACE=<path> now
  dumps DD94IN inputs (ftdynamics.c container-only; needs the msl_emitter_trace helper
  in scalar.c container copy + stdio includes).

- 2026-07-28 — `retained` (WHISPY WIND + CHAIN MATRIX REFRESH LANDED, commit 637b6706;
  AGGREGATE 190/56/0 — up from 183/63/0). Final design after the live-machine attempt
  regressed Fountain knife-edges: (1) wind emitters mirrored deterministically from the
  RECORDED xDC stream (nonzero == retail xD0 in (0x2D,0x140) entering 0x2E; spawn on
  %10==0; onset tracked via this proc's own xD0-as-counter because the pre-frame replay
  pass already wrote xDC); the machine itself stays short-circuited (its rand timers are
  not replayable headlessly — retail's effect draws precede its prio-4 stream position —
  and fn_802112F4's push reads xDC post-prio-4, so a live machine forks positions by the
  0.10 wind speed). (2) msl_fighter_refresh_dynamics_matrices at the publication phase:
  retained chain-link jobj matrices rebuilt end-of-frame so ftCo_8009CB40's re-anchor
  (mtx[i][3] reads on anim changes) sees last-frame world poses like retail's render
  guarantees; previously links past the capsule bone were NEVER built (identity → tail
  tip re-anchored to x=0 exactly at dl-fox validator 51). Chain-state probe now
  BIT-IDENTICAL across the hit window. SEVEN long-classified replays (5 DL, 1 YS, 1 FD,
  all with Fox) became full passes — classifications removed, output locks re-recorded.
  dl-fox 10,818/11,083. Icies otherwise unchanged; ys-fox classification intact.
  OWED: container pytest sweep; PPC-backend lock parity check (locks re-recorded from
  native; hosted-only fixes leave PPC behavior unchanged but verify before the wire cut).

- 2026-07-28 — `retained` (SEVEN MISC-LANE CLASSIFICATIONS) + `open` (the 88/91 roll
  misalignment = A MISSING EFFECT DRAW). Icies now 0 pass / 8 classified / 4 fail.
  The four open episodes and what is known:
  * bf-marth @5605 + medium-marth @10783 (leader DamageFly 88 vs 91): ROOT-CAUSED TO THE
    RNG STREAM. The variant roll (ftCo_Damage.c:419, HSD_Randf < x240 in ftCo_8008DCE0;
    91=DamageFlyRoll picked when kb_level==3 && percent>=x23C) samples one position
    early in ours. Retail frame 5605 draw sequence (bfm_randf_probe.jsonl):
    [09F7 CPU x3 BIT-MATCH, efSync lib draw BIT-MATCH, **extra dispatcher draw at lr
    0x80063B74 inside efAsync_Dispatch = the gfx 0x3EC case: li r3,8; bl efLib create;
    HSD_Randf**, then the roll]. Ours lacks the dispatcher draw because OUR queue holds
    gfx_id 0x3F8 (1016, spawn_kind 6) at that tick — no case in effects.c's dispatcher
    switch — while retail dispatches 0x3EC (1004, model 8 + one Randf, a case we already
    model). SO: either our enqueued gfx id is wrong (enqueue caller attribution via
    __builtin_return_address lands in ftCo_0A01.c ftCo_800A05F4 region — inlining-
    degraded, re-attribute with a debugger or wider addr capture), or 0x3F8 needs its own
    dispatcher-draw case (check refs efasync.c case 0x3F8: efLib_Create_Attach_Pos(0x13)
    — MODEL-BACKED, no draw, so more likely the ID is wrong our side or retail spawned a
    DIFFERENT SECOND effect). Fixing this likely clears BOTH marth replays.
  * dl-fox @5205-5215: small item vel/pos episode (12 fields, blizzard-adjacent).
  * fod-sheik @10759+: item existence episode (item_count/type/owner from 10761).
  TOOLING ADDED (container): MSL_GFX_TRACE_PATH prints GFXQ (enqueue, caller low24 in b
  as float — decode carefully, it is exact for <2^24) and GFX (process) rows via
  msl_emitter_trace; our RNG trace pairs frames via synthesized online seeds
  (base + (frame+123)<<16 — bf-marth base 33407).

- 2026-07-29 — `closed` (session 12: master-diamond @682 root-caused — the UCF pad-ring
  patch profile; ALL 13 extended icies replays in the aggregate: 271 = 203/68/0)
  The @682 "grab-attach" was misread: Sheik was hit by an ice block at 681 and our
  runtime SDI'd her 6.0*stick = -5.85 during hitlag at 682. Trace chain: the
  displacement matched sdi_pos_scale * lstick exactly; the spaghetti trace showed her
  x670 tilt timer correctly stale (flick began on the hit frame, so the count-up
  branch ran), meaning vanilla SDI could not fire - the trigger was
  msl_ucf_sdi_check (the pad-ring f2 SDI patch), which retail provably did not run
  for this session.
  A first attempt keying all ring patches on ucf_cardinals_1_0_enabled BROKE EIGHT
  aggregate replays (2022 luigi/marios, puff master-diamond, marth x2, falcon,
  peach x2 - all cardinals-false yet bit-exact under ring-patch emulation): the
  ring-patch rollout and the cardinals rollout are INDEPENDENT eras. Reverted;
  the correct model is the existing per-replay rollout-flag scheme (wire.h already
  documents ucf_sdi_enabled/ucf_shield_sdi_enabled as "separate
  rollout/capability" for exactly this reason).
  CHANGES: new ucf_shield_drop_extended_enabled flag plumbed end to end
  (wire.h/wire.c config byte - MslCoreMatchConfig 52->53, header 108->109;
  match rules; scalar init; native.c kwargs x2; suite_io + validate_replay
  schema); msl_ucf_pass_oos_stick_check re-keyed from the session-11 cardinals
  gate to the new flag; the two ranked-anonymized icies captures
  (master-diamond, platinum-platinum) carry ucf_sdi/ucf_shield_sdi/
  ucf_shield_drop_extended = false (platinum's flags also reconcile its ppc
  snapshot history: the 1326-row follower fork seen pre-gate was sdrop-on
  behavior).
  RESULT: master-diamond 782 -> 6,186/6,460 matched, item.misc-only; classified
  (native 914 / ppc 194) + locked; icies_parked.json dissolved into
  icies_extended.json (13 replays); counts 270->271; aggregate 203/68/0; container
  pytest green; WingedGorgeousPanther ppc snapshot verified stable under the
  re-keyed gates; PPC rebuilt for the wire change.
  OPEN QUESTION (for future imports): what distinguishes the ranked-anonymized
  profile (no ring patches, 2023-24 vintage) from ordinary 2022+ netplay (ring
  patches present)? Possibly a ranked-mode gecko set. New anonymized imports
  should A/B the three flags against retail probes before classification.

- 2026-07-29 — `closed` (session 11: icies_extended wired into the aggregate — 270
  replays, 203 pass / 67 classified / 0 fail; the pre-cardinals UCF shield-drop gate)
  master-diamond's frame -19 fork root-caused with retail probes: retail Nana
  (mimic-driven off Popo's ring) dashes to the platform edge, GuardOns off the
  ring-played trigger at -19, and vanilla shield-drops (Pass) a frame later when
  the played stick crosses -0.66. Ours Passed at -19 through the UCF
  shield-drop-extended emulation: msl_ucf_pass_oos_stick_check read the PORT's
  sdrop counter (hot from Popo's own deep-down stick) with no version gate. The
  shipped gecko's counter lives in the shared UCF pad ring, which ships inside
  the combined "UCF Pad Buffer + 1.0 Cardinals" patch — recordings that predate
  1.0 cardinals (ucf_cardinals_1_0_enabled=false) never ran that buffer, so the
  counter branch cannot fire there. FIX (runtime/match.c):
  msl_ucf_pass_oos_stick_check returns false when cardinals are disabled.
  Verified: master-diamond -19 fork gone (103 -> 782 matched rows); aggregate
  unchanged (several cardinals-false replays re-validated identically).
  master-diamond's NEXT fork (frame 682) is a different class — Sheik
  grab-attached ~5.85 units from retail while BOTH climbers match bit-exact
  (pummel/throw cascade from 687); unaffected by the session-10 matrix refresh
  (A/B verified). PARKED in the new replays/suites/icies_parked.json.
  WIRING: icies_extended.json rescoped to the 12 green replays and added to
  melee_core_aggregate.json (258 -> 270); 12 unrecorded-item-pool-residue
  classifications with native + ppc snapshots; 12 output locks; coverage/runner
  tests updated (270 + manifest set + classified loader). Trace infra: negative
  frame windows now work for MSL_NANA_TRACE (match.c gate accepted only
  start >= 0); container gained AA42C-IN/RECORD/MIMIC-held/PASSGATE/GUARDCHK/
  OOSCHK probes (ftCo_0A01.c, ftCo_Pass.c, ftCo_Guard.c, container-only);
  slippi-dolphin ring probe now dumps nana x618/x671/x668.
  KEY RECIPE: Nana entry behavior = mimic ring playback of Popo's inputs
  (capture quantizes input.lstick*127 at think time, one frame stale; the ring
  x4/x5 trigger bytes truncate the merged x650 float to 0/1 — shields propagate
  through the held word's HSD_PAD_LR bit, not the analog).

- 2026-07-29 — `closed` (session 10: THE TAIL-SOLVER FORK ROOT-CAUSED AND FIXED —
  two production fixes, 4 classified replays promoted to bit-exact, aggregate
  203 pass / 55 classified / 0 fail)
  The session-9 "solver fork with bit-identical inputs" was never in the solver.
  Per-stage link_dir instrumentation (MSL_ST rows inside lb_8001044C, container)
  showed our frame-3468 YS solve matching retail's implied final dirs to 0.2-0.8
  deg — but our OWN 3469-entry anchors differed from what our solve wrote.
  ftCo_8009CB40 (the anim-ownership toggle) had rewritten the chain anchors from
  raw jobj->mtx at the taunt-exit action change. Retail probe on 0x8009CB40
  (slippi-dolphin: cb40 event in MaybeCaptureTailProbe + per-link unk_38 "av"
  dump) proved retail fires the identical release/acquire pair but reads
  END-OF-FRAME RENDER matrices (solved pose, stale through the next frame's anim)
  — our port had left the last MID-FRAME pre-solve HSD_JObjSetupMatrix in mtx.
  FIX 1 (melee/ft/fighter.c): Fighter_8006D9AC re-runs
  msl_fighter_refresh_dynamics_matrices after ftCo_8009E0A8 (post-solve), so the
  toggle samples the solved pose exactly as retail's renderer leaves it.
  Verified bit-identical CB40 rewrites vs retail at YS 3469 and 33692 frames
  36/313/677.
  That fix flipped marios/33692 (was pass) — bisecting its 380-solve fork against
  retail (windows 100..380 bit-identical in w/rot/s58/jr/jt/par/av) landed on the
  collider-avoidance stage: retail rotated link 2 by 0.1896 rad off collider
  (-43.64,10.65,0.38); ours read collider position (x14, pos.x, pos.y) because
  struct lb_Collider mirrored Fighter_x1670_t with byte-offset padding — the
  embedded HSD_JObj* widens to 8 bytes on 64-bit hosts, shifting position from
  0x18 to 0x1C and the stride from 0x28 to 0x30. Hosted collider avoidance had
  read garbage since the port began (PPC, with 4-byte pointers, was always
  correct — hence "ppc matches more rows than native").
  FIX 2 (melee/lb/lbspdisplay.c): lb_Collider declared with real field types
  (Vec3, f32, void*, f32, Vec3, s32) — layout-correct at any pointer width.
  RESULTS: 33692 passes; auto-fox-b @3503 SDI cascade gone (item.misc-only now);
  icies_extended = 12 misc-only + master-diamond @-19 (parked); icies suite 12/12
  classified; aggregate 203/55/0 with FOUR replays promoted to fully bit-exact
  (sheik MixedAllQuetzal, doubles Game_20260704T012353, marios medium-fox-2025-12
  + silver-fox-2026-06 — the last dropped a 52,031-field residual).
  Suite updates: 2 classification entries removed, 2 kept ppc-only (native dict
  dropped; test_melee_core_validation now selects native-snapshot entries only),
  4 output locks re-recorded, 2 ledger rows appended. PPC re-verified per-entry
  after the rebuild (the PPC build also defines MSL_CORE_HOSTED so FIX 1 applies
  there; snapshots re-recorded where drifted).
  KNOWN GAP: the Mac-side data/ pipeline only covers 7 characters (no ICs) — Mac
  icies runs fail on stale data, container remains the validation authority.
  Remaining icies-extended non-misc work: master-diamond @-19 (match-start
  entry-approach clamp via published extents, likely CB40-at-spawn sampling
  pre-first-refresh matrices).

- 2026-07-28 — `closed by session 10` (session 9: icies_extended staged; the tail-solver fork isolated
  to a single frame with bit-identical inputs)
  Scope: ~/SSBM/Replays/Samples/ics.zip imported (30 files = the 12 existing suite
  replays + 18 new). Excluded: 4 non-frozen-PS games (established policy) and the 2022
  FoD capture (Slippi 3.9.1 predates the animation_index stream — validator ERROR;
  samus-suite precedent). The remaining 13 staged as replays/suites/icies_extended.json
  (commit 770c8c27), NOT included by melee_core_aggregate.json — icies.json stays at 12
  so the aggregate/coverage gates stay green (258, container-verified 12 classified/0
  fail post-split). ucf refinements: master-diamond (v3.16) + platinum-platinum (v3.15)
  ranked-anonymized carry ucf_cardinals_1_0_enabled=false + played_on=network (the
  cardinals flip fixed master-diamond's frame -39 Popo fork: 0.185 vs 0.185*cardinal
  ratio signature).
  Extended-suite state: 13 FAIL. 11 are item.misc-lane-only (unrecorded-item-pool
  class, classify after fixes). Two real forks, both root-caused deep:
  * auto-fox-2025-02_Game_20250224T030855 (YS) @3503: spurious CPU-Nana SDI (+6.0
    exactly; retail frozen in hitlag). Full causal chain established with retail
    probes: Fox's PUBLISHED capsule extent x1A88.x560 (ftCo_800A0DA4 max over hurt
    capsules) diverges at the frame-3470 publication (retail 3.446 vs ours 4.260;
    3465-3469 bit-exact) because cap[12] = THE TAIL (scale 1.62) sits 0.8 too far
    out -> Nana's B8A9C/B4AB0 range check admits an attack candidate ONE THINK EARLY
    (our x18 0->2 at think 3471, retail at 3472; x7C/x80/x570/positions/colliders all
    verified bit-identical) -> different B4AB0 Randf pick (frame-seeded): ours built
    a WaitFor(26+5)+PressR+LstickXForward(0x7F) roll macro, retail a WaitFor(10)+
    Y-tap macro -> our stale macro replays +127 during Nana's 3502-3504 hitlag ->
    x670 tilt-timer resets -> ftCo_Damage_OnEveryHitlag SDI fires.
  * THE TAIL SOLVER FORK (the dl-fox @5082 class, now minimally reproduced, wind-free):
    Fox's 4-link tail chain state at ftCo_8009DD94 entry is bit-identical at frames
    3466-3468 (per-link rot/s58/unk_2C anchors/params unk_44-8C, jobj jr/jt, full
    ancestor mtx walk incl. dirty flags, colliders x1670, arg1, and retail
    lb_804D63B0 emitters = NULL on YS both sides) yet the FRAME-3468 SOLVE (Fox taunt
    -exit + movement-start jerk) forks the free links' unk_2C anchors by 14-23
    degrees of link direction (root link 0.0 stays bit-exact -> parent pose agrees;
    ours under-rotates toward the new pose = tail trails wider). lb_8001044C C source
    == refs C (only documented fmadds deltas). NOTE the jr-equality at next entry is
    NOT evidence the solved rotations matched — anim overwrites jobj->rotate before
    the next DD94; the surviving solver state is unk_2C (+un-dumped unk_38/unk_44
    angular velocity). NEXT: stage-by-stage link_dir dump inside our lb_8001044C at
    frame 3468 (after stiffness/gravity/force/angvel/max-angle(unk_88=0.0524!)/
    convergence/deviation/collider/ground stages) vs retail's implied final dirs
    (derivable from the 3469-entry anchors); suspect a branch-boundary flip or an
    op-level slip in a jerk-only stage (max-angle clamp math, RotateAboutUnitAxis,
    or the ground-collision mpCheckFloor block near the YS slope).
  * master-diamond residual @-19 (post-cardinals): Nana entry-approach clamp 125 vs
    127 (1.4*125/127 velocity signature) = the AA42C clamp fed by published capsule
    extents — plausibly the same tail/extent class (Sheik hair dynamics), park until
    the solver fix lands.
  TOOLING: slippi-dolphin 1872b778b9 (tail probe: MSL_TAIL_PROBE_PORT, per-link
  params, ancestor walk, emitter global; ring probe: x80/x84/xA4/x570 + target
  extents). Container /work (env-gated, uncommitted): SDI-CHECK in ftCo_Damage.c,
  NANAIN x18/x670 fields + THINK buffer dump in ftCo_0A01.c, B8A9C-IN in
  ftcpuattack.c, EXTENT capsule dump in ftCo_800A0DA4, DD94 trace parameterized
  (MSL_DD94_PLAYER/START/END) + PARAMS/ANC rows in ftdynamics.c, frame-gated
  msl_nana_trace_file (gm_8016AEDC-based, prints F<frame>) in match.c, rand/randf
  trace in sysdolphin/baselib/random.c. Mainline bot captures are per-frame-seeded
  like online games (verified: base+frame<<16 stride in our stream).

## Linux-host certification + the pool-residue lanes are retired (2026-08-04)

Work moved to a native linux/amd64 host (Ryzen 9950X3D, Ubuntu 25.10). Certification per
`agent_docs/ENVIRONMENT.md` surfaced that four ness identities and, run-to-run, even the
same host's own fingerprints were unstable: the recorded snapshots hashed bytes that are
halves of 64-bit host pointers, so they were only ever reproducible inside the recording
container's memory layout. Measured directly: two back-to-back native runs of `50321`
produced two different mismatch fingerprints from an identical mismatch shape, and a
per-frame item-lane dump diffed across runs pinned every varying byte to one
ASLR-randomized pointer byte (0x8C vs 0x84) read through the generic
`item_var_source_byte` union path for articles whose native struct leads with widened
pointers (bat, yo-yo slot residue, PK Fire/pillar/flash, Fox Illusion).

Two projection-mask corrections retire the whole family, both inside the mask's existing
taxonomy (source-proven non-gameplay lanes):

- **Ness Yo-Yo misc1 (x4) is masked.** Its only owner, `it_802BFEC4`, writes it on the
  despawn transition, so no live export ever samples an owner-written value — the same
  never-written-at-sampling-time argument that already masks the PK Flash leading word.
  The swing read (`it_802BF800`) consumes the same residue retail does; any gameplay
  effect lands in directly compared lanes. misc0 (x0, the smash action id, written at
  spawn) stays compared.
- **Mr. Saturn misc2/3 are masked in every state.** Every `xDE4` write in `itdosei.c` is
  `= ip->pos`, a copy of the directly compared position lanes, and the writing Anim
  callbacks skip during item hitlag, so a state entered mid-hitlag (observed: state 11 at
  `51444` 10756) samples stale bytes or pool residue.
- **compare_row now requires both sides' masks to admit a misc lane** (generalizing the
  extra-needle special case): inside a classified divergence the hosted slot can hold a
  different article than the recording, and the bytes at an unowned offset are arena
  placement, not gameplay. The slot disagreement still reports through item.type/state.

Consequences, all gated: the nine `unrecorded-item-pool-residue` classifications (whole
family) are deleted because their replays are now bit-exact on both backends;
`2025-03`/`2026-06`/`53362` and peach `HeartyStiffMallard` re-recorded with **identical
native and PPC snapshots** (the masked lanes were the entire backend delta in every fork
capture); 13 output locks re-recorded (9 ness + 4 peach whose Saturn/kind-window bytes
were canonicalized). Fingerprints are now process-location independent: three consecutive
suite runs hash identically, and the aggregate reproduces bit-exactly under both gcc-15.2
and the pinned gcc-13 (local dpkg unpack, `build/host-gcc13/`).

Also fixed: `src/upstream.lock`'s tree digest was stale from the WIP import commit (its
recorded value is not reproducible from its own pinned inputs under any sha tool/format
variant; file_count was updated to 1155 but the digest was not). Re-recorded with
`tools/build/source_sync.sh`'s canonical method; `make source-check` passes with 206
local deltas.

**Suite state (linux/amd64 host + gcc-13 cross-check authoritative): ness 30 = 26 pass /
4 classified / 0 fail on native AND on PPC** (classified: `2025-02` + `2025-03`
dolphin-ULP, `2026-06` presentation-RNG draw gap, `53362` hitlag pad-edge debt).
Aggregate **426 = 342 pass / 84 classified / 0 fail / 0 error**, lock-clean, xpass-free.
Host certification: source-check, native-smoke, aggregate, per-suite ness PPC
(`--timeout 240`; qemu here needs ~100s on 17k-frame captures), pytest 45 passed /
1 skipped (no local ISO at the tested path) — all green.

## Icies misc-lane retirement + popo_replays import (2026-08-05)

The Ice Climbers article lanes joined the projection mask's source-proven non-gameplay
taxonomy, retiring the whole `unrecorded-item-pool-residue` family (24 replays) and the
belay-string `unrecorded-item-pointer-identity` classification:

- **Ice (106) keeps misc1 only.** x0 is the spawner GObj pointer written at spawn
  (`it_802C1590`) — allocator identity; x4 is the live scale, spawn-written from
  x60_scale and bounce-decayed (`itClimbersice_UnkMotion0_Anim`), so xDDB stays
  compared; the declared struct ends at xC, leaving 0x17/0x1B past-member residue.
- **Blizzard (107) keeps misc0 only.** x0 is the spawn-written scale
  (`itClimbersBlizzard_Spawn`); the only other member is the flag0 bit in xDD8's
  leading byte, so offset 7 samples an unwritten byte and 0x17/0x1B fall past the
  declared members.
- **Gum strings (113) is fully masked.** x0's only owner write is the constant 2.25f
  (`it_802C3864`) whose sampled low byte is 0x00 and the spawn constructor
  (`it_802C27D4`) leaves it unwritten — the Link-bomb direction-sign argument; x4/x8
  are the string ItemLink chain, xC the owner GObj, x14 the tail joint, and the
  struct ends there.

`dl-fox-2025-05` was a two-residue entry; its wind-machine half survives alone under a
new `dreamland-wind-rng-draw-gap` id (grOldPupupu_802113E0's idle-cycle rand_range
pair is still not consumed by the hosted wind-direction mirror; 11 forked frames,
5205-5215, identical native/PPC fingerprints so the PPC snapshot aliases native).

The 2026-08 `popo_replays.zip` import added five screened singles to the icies suite
(25 -> 30; Fountain, frozen Stadium, Dream Land, Battlefield, FD; opponents add
Donkey Kong). The Stadium capture is 3.18.0, where is_frozen_ps is untrustworthy, and
is event-verified frozen (0x41 declared and empty over the full game). The zip's sixth
game (`23549`, Ice Climbers/Peach on Yoshi's) was initially rejected for a one-frame
item_count divergence at 2076; it was triaged and admitted the next day (below).

**Suite state: icies 30 = 29 pass / 1 classified / 0 fail on native AND on PPC**
(classified: `dl-fox-2025-05` wind-RNG draw gap). Aggregate **491 = 425 pass / 66
classified / 0 fail / 0 error**, lock-clean, xpass-free (30 icies output locks
re-recorded/added; aggregate coverage tests bumped 486 -> 491). source-check,
native-smoke, format-check, and pytest green (the parallel-oracle test only fails
when run alongside a live aggregate run — worker OOM-kill, passes in isolation).

## 23549 admitted under peach-parasol-landing-teardown-window (2026-08-05)

The rejected sixth popo game is triaged and in the suite (31 replays). The frame-2076
divergence is not an extra article: the hosted projection publishes Peach's closed
parasol (spawn id 11, its frozen spawn-position row at 67.36/-11.93) exactly one item
sample longer than the recording. The recorded episode ends at 2075 and the window is
her landing out of FallSpecial (recorded post state 35 -> 14 across 2075 -> 2076),
whose transition tears the article down (`ftPe_8011D518` / `it_802BDB94`); Slippi
samples items from a player-plink GObj proc installed before fighter processing, so
the teardown lands one sample apart between the recording schedule and the hosted
publication boundary. Whether retail retires the article in the preceding item phase
or the landing frame's fighter phase needs the Dolphin scenario probe. Native and PPC
fingerprints are identical (`ed25d016f39d4dd5`), so the PPC snapshot aliases native.

**Suite state: icies 31 = 29 pass / 2 classified / 0 fail on native AND on PPC**
(classified: `dl-fox-2025-05` wind-RNG draw gap, `ys-peach-2025-02_23549` parasol
teardown window). Aggregate **492 = 425 pass / 67 classified / 0 fail / 0 error**,
lock-clean, xpass-free; the 23549 output lock is recorded and the aggregate coverage
tests bumped 491 -> 492.
