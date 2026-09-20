# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Completed: #30 `59251229`, #26 `9ce438d3`, #27 `12f2522f`, each after local
checks and fresh CI.

Current owner: Pichu admission through upstream `ftPc_Init` and shared `ftPk_*`
move and Pikachu item owners. GameData owns Pichu archives, four costumes and
translated Pikachu-shaped attributes; Match owns fighter and article state.
API, scalar, batch, extraction, validation and viewer registries consume the
admission. Replace only the unsupported-kind boundary; retain source-authored
recoil, item kinds and callbacks without copied move logic. Verify four-Pichu
pool bounds and combined Roy/Pichu preload inside the existing shared reserve.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.

# Kirby admission packet — 2026-09-19

- Final owner: `src/melee/ft/chara/ftKirby/*` (22 upstream sources) plus the
  five Kirby item owners (`itkirby_2F23`, cutter beam, hammer, Game & Watch chef
  pan, Yoshi egg lay) imported verbatim from the pinned decomp. Kirby's copy
  abilities are those files; nothing is re-implemented on the sim side.
- Canonical state: `ft_80459B88` (per-kind copy archives and hat structs),
  `fp->fv.kb`, the single 0x424-byte `ftKb_DatAttrs` block, and the copy-ability
  archives `PlKbCp*.dat` with roots `ftDataKirbyCopy<Name>`, all resident in the
  game-data arena from init. Retail loads copy archives lazily at inhale; the
  sealed arena forbids that, so every copy archive, per-costume hat file and
  EfKb effect bank is preloaded and translated with the fighter data.
- Consumers: inhale/spit/swallow states on victims (`ftCo_CaptureKirby`,
  `ftCo_ThrownKirby`, already compiled), the 145 `ftKb_*` call sites across
  ftCommon/items/player, item logic rows for the 30 `It_Kind_Kirby_*` kinds,
  effects banks 5 and 20-48, the validator, viewer and public APIs.
- Displaced code: the 45 `ftKb_*` ABORT_STUBs in `src/stubs/unresolved_abort.c`
  and the two `ftKb_SpecialN_800F5BA4/800F5C34` no-op shims in `runtime/match.c`.
- Deletion boundary: those stubs and shims go when the sources land; no
  fallback dispatch, no partial-hat mode. Copying a fighter absent from the
  roster is impossible by construction (the fighter cannot be in the match).
- Copy roots: `KirbyHatStruct` is not one layout. The 25 `PlKbCp*.dat` roots
  fall into two families (20 start with the hat joint and a parts desc; the
  five with per-costume hat models, DK/Puff/Mewtwo/Falco/G&W, start with two
  parts descs and a bit mask) and every `hat_dynamics[]` slot is typed only
  by the code that reads it (articles, dynamics, accessory joints, anim
  joints, a visibility lookup). Hosted per-hat views in
  `tools/build/native_dat_types.c` (`MslDatKirbyHat<Name>`) keep the struct's
  host shape and type each slot; unread slots stay raw words. Slot k of
  `ft_80459B88` is fighter kind k; the decomp's `hats[FTKIND_X]` names are
  off by one and the loader for kind k reads `hats[k - 1]`.
- Sequence: (1) import sources, extend extraction, make it link — done; (2)
  DAT translation for `ftDataKirby` and the 25 copy roots, hat preload — done
  (copy articles are published into the sealed item catalog at preload, and
  every Kirby reserves hat pieces, x2040 records, AObj/FObj/Mtx/Vec headroom
  at construction; the Kirby moves smoke swallows Fox and Bowser and fires
  both copies); (3) Kirby alone passing corpus games with no inhale; (4) copy
  abilities in corpus order (Fox/Falco, Zelda, Sheik, Marth, Peach, Mewtwo,
  G&W, ICs, Falcon), then the rest via scripted headless Dolphin captures;
  (5) the user's Kirby vs CPU Bowser game (2026-09-19, Yoshi's) as the first
  locked recording.
- Outcome (2026-09-19, late): steps 3-5 done. `replays/suites/kirby.json`
  locks 43 games (42 exact, one classified missing-raw-cstick-asdi): 26 corpus games covering
  the Zelda, Ice Climbers, Game & Watch, Falco, Marth, Captain Falcon, Peach
  and Bowser copies plus Final Cutter, Hammer, Stone and the spit star, and
  17 scripted headless Dolphin captures (tools/validation/record_kirby_copies.exs)
  that swallow, fire, taunt-drop, re-swallow and lose the Mario, Luigi, Dr.
  Mario, Link, Young Link, Samus, Sheik, Ness, Pikachu, Jigglypuff, Mewtwo, DK, Yoshi,
  Ganondorf, Fox and Kirby-mirror copies plus a Battlefield moves capture;
  the aggregate grows to 592 (the copied Thunder Jolt's misc lanes needed the
  origin kind's pointer-widening remap in the item-var sampler); Pichu and Roy
  copies wait for #28/#27. The suite also passes on the PPC backend after one
  oracle-only fix: hosted construction never runs retail's ftLib_80087508 ->
  ftData_800857E0 -> ftKb_Init_UnkMotionStates5 chain, so the PPC oracle now
  loads each Kirby's copy archives for the present kinds at match
  construction (native keeps its GameData preload); the PPC scalar-api smoke
  segfaults identically on main (pre-existing). Hosted policies settled on
  the way: the headless
  DAT translation nulls every joint's draw-object union, so the per-costume
  hat mesh lists (`ftKb_SpecialN_800EF0E4/EF438`) are empty and the costume
  texture loop records nothing; hat accessories loaded mid-match take
  isolated joints from an eight-per-Kirby reserve (Mewtwo inserts seven) and
  stay cold, outside the shared pose array; `msl_fighter_pose_erase_joint`
  unregisters an accessory before `HSD_JObjRemove`; the two copy-star item
  kinds 52/53 carry logic rows and the hat-drop star's itUnkAttributes; the
  Kirby efAlt effect ids draw their generator gates. The first CI run of the
  suite drifted on the render-visibility lane only: ftCo_CaptureKirby scaled
  the victim's root joint through the decomp's `mv.co.guard.x2C` alias, which
  shares fp+0x236C with `capturekirby.scale.x` in retail but lands on a
  pointer in the hosted 64-bit union, so the x scale was address-dependent
  and the camera subject went off-screen on machines where the bits were
  large; it now reads the capturekirby field. Excluded with reasons in
  `agent_docs/validation/kirby_provenance.json`: a live-transformation
  Stadium recording, a pre-cardinal-patch Dolphin walk (raw (95,-1) not
  snapped by the console), a Final Cutter init_pos.y low-byte lane on FD, the
  D20 #32 "network" captures (need the Roy PR's validator fix) and the
  ranked exports without playedOn.
