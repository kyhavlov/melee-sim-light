# Classified replay closure — 2026-09-08

The native GNU/Linux supported-domain suite is **493 exact / 36 classified / 0
fail / 0 error**, across 529 replays and 5,059,922 transitions. This is **23 more
exact replays** than the 470/59 baseline at `24643394`. Native debug and strict
release agree. Scoring, comparison masks, supported inventory and replay bytes
are unchanged. One closure corrects a recorded UCF configuration; 22 come from
source runtime corrections.

Mismatching transitions fell from 16,717 to 6,804 (99.86553% of all transitions
exact); mismatching fields fell from 354,198 to 16,067. The improvements are not
obtained by discarding replays or treating signed zero as equal.

This completes the bounded investigation proposed in
[the original audit](CLASSIFIED_REPLAY_AUDIT_2026-09-08.md). Remaining exceptions
are worthwhile future source-completion work in some cases, but the probes did
not establish another narrow correction worth retaining in this campaign.


## Retained corrections

| Source owner | Correction and evidence | Newly exact replays |
| --- | --- | ---: |
| `HSD_QuatLib_8037EF28`, `msl_blend_quaternion_batch` | GALE01 0x8037EF6C..7C rounds Y's product before fusing X, then Z and W. Both hosted copies had X/Y reversed. Retail tracing first located the fork in quaternion interpolation, before ECB construction; all 54 wall interpolation calls reproduced retail when supplied equal inputs. | 17 |
| `headless_camera_build_view` | SDK `C_MTXLookAt` 0x803427E8..2804 and the next two rows round three products before two additions. Remove erroneous nested FMAs. Restore the double roll predicate and source Gekko sqrt sequence; those latter changes alone did not change native outputs. | 2 |
| `grIzumi_801CC358` | Execute the existing platform temporal machine before recorded-height publication. Retail consumed its case-0 timer draw before Samus rope gravity; the hosted early return skipped it. Delete that bypass and the synthetic construction-only draw/helper hook. The same RNG correction fixes the Sheik needle spawn offset. | 2 |
| `ft_8008A348` | Restore the source Wait-entry DownSpot, hammer and Peach live-parasol checks, whose callees are already admitted. Retail calls `it_802BDB94` at 0x8008A3E0 during frame 2076; hosted skipped it. Delete the redundant hosted ground conversion. | 1 |
| Peach replay configuration | Match HeartyStiffMallard's recorded classic UCF bundle using existing manifest flags. No runtime compatibility flag added. | 1 |

`PSMTXMultVecSR` also restores the SDK's final Z FMA over the rounded X/Y sum.
It is source-backed and had no output effect on the classified subset. No new
persistent state, allocation, scheduling bridge or replay-specific gameplay rule
is introduced. The Fountain machine and published height remain their existing
canonical state, which save/restore already owns.

## Historical Peach configuration proof

HeartyStiffMallard is an April 2023 Slippi 3.14 capture. Reassemble its split
0x3D Gecko-list event using the 0x10 chunk lengths, then walk actual code records
using `CEXISlippi::prepareGeckoList`'s length rules (C0/C2, 08, 06, default).
This yields 57,288 bytes and 275 entries, rather than a byte-pattern search that
could mistake an instruction immediate for a hook. Its code list contains classic
shield drop `C20998A4`, and lacks shield SDI `C2093294`, SDI `C208E54C`, extended
shield drop `C209A0B8`, and cardinal/pad-buffer hook `C206B460`.

The manifest already disabled modern shield drop/cardinals but inadvertently
left shield SDI, SDI and extended shield drop enabled. Disable those three absent
capabilities. Shield SDI alone removes the first 6482 fork (3.9105 units during
lightshield); the complete recorded classic bundle makes all 15,634 transitions
exact after the Wait-entry fix. This resolves the formerly dominant missed-hit,
stock/death and item tail as well as the parasol lifetime error.

- Compressed replay SHA-256: `2477f3def627748c961a5495a5cbf3dd3a59d5b0b01ca245f7a80863011ee756`.
- Reassembled Gecko-list SHA-256: `1afee99737a9b9c854555a8f37697a228ebfaeaa12565da5a2168f46c0c69c10`.
- Reproducer and extracted evidence: ignored `reports/triage/classified_closure_20260908/profile_provenance.py` and `hearty-profile-provenance.json`.
- Source: `refs/slippi-ssbm-asm` recording/UCF hooks; `refs/Ishiiruka/Source/Core/Core/HW/EXI_DeviceSlippi.cpp::prepareGeckoList` (the existing shared reference checkout was read without modification).

## Why the remaining owners stop here

- **Camera:** native Guanaco has three position rows, Aardvark one, Hornet one
  plus magnifier. Aardvark's retail eye X is already `0x1.905f88p+4` versus hosted
  `0x1.905f8ap+4` before LookAt; the other eye/interest inputs agree. Further work
  belongs to camera tracking. Nintendont metadata disproves the old blanket
  Dolphin explanation for several captures.
- **Samus/FD:** the remaining rope state first forks after an FD background RNG
  draw at 15113. Its angular state matches initialization and frame 8192, but
  retail does not invoke the background callback during 12288..12290 while hosted
  continues integrating. Closure needs background scene/actor activation lifetime,
  not different rope arithmetic or a frame-specific timer correction.
- **Dream Land and needle lifetime:** omitted wind/effect temporal RNG remains a
  real source gap. Completing particle lifetimes, child emissions and conditional
  draws is a larger runtime/state packet. HumiliatingCreamyReindeer's needle
  survival difference still needs a contact/RNG trace; it is not an ULP tail.
- **Old ASDI input:** the two 0.0375 tails begin on hitlag exit. SendPreFrame
  records C-stick before UCF cardinal injection. A normalized 79/80 cannot reveal
  whether the raw input was 79 or a radially clamped value of at least 80 with a
  perpendicular component. These captures lack raw C-stick. Do not infer the
  missing input from the recorded resulting position.
- **Remaining float rows:** Puff's wall example starts in root yaw
  (`-0x1.921fb0p+0` retail versus `-0x1.921fb4p+0` hosted), then propagates into
  three ECB origins. Smug and DK/FD retain isolated wall rows. Other article,
  friction and slope residues have no further established source correction.
- **Magnifier:** replay data omits the VI/render boundary that publishes the
  damage gate. These rows sometimes have downstream gameplay effects, notably
  TameEmbellishedSparrow, and are not dismissed as display-only noise.
- **Historical execution profiles / unwritten bytes:** retained retail interpreter
  laser probes agree with hosted output rather than two old recordings. The new
  Falcon probe explicitly observes retail `fnmsubs` producing -0, like hosted,
  where the recording has +0. Uninitialized chain/article bytes also lack their
  original allocation history. Keep strict comparison and explicit exceptions.

## Backend and verification scope

The 59 originally classified cases were also rerun on hosted PPC: 21 become exact
and 38 remain classified after refreshing measured identities. Two camera replays
are now exact only on native; keep their records with **PPC-only snapshots**.
Thus the manifest has 38 records but only 36 native exceptions. Several PPC
camera residuals predate this campaign, and four original records had no PPC
snapshot at all. PPC is a cross-check with its own residuals, not retail hardware.
No full 529-replay PPC claim is made here.

Full native debug and release gates preserve all 470 original exact output locks.
Only the originally classified set receives refreshed identities. Source and native
smokes, Wasm/native state and viewer digest parity, the 256-match lifecycle smoke,
and save/restore/lifecycle checks pass. Initial Wasm invocation lacked `emcc` on
PATH; rerunning with the installed Emscripten environment passes. Throughput
measurements include a pre-commit exact runtime-camera O2 recovery; final paired
medians are +0.353%/-0.004% against the frozen merge at resident 256/512. Raw
samples, timing uncertainty and controls are retained in
[performance history](performance/HISTORY.md#classified-replay-closure--2026-09-08).

All diagnostics are removed from production. Original captures, pinned upstream
source and shared emulator checkout remain unchanged. Ignored probes and gate
logs live under `reports/triage/classified_closure_20260908/`.

## Per-replay native outcome

The table inventories all 59 original exceptions. Counts are strict mismatching
transitions after the retained corrections, generated from full validation results.

| Replay | Remaining transitions |
| --- | ---: |
| `aggregate_recent/HilariousVillainousGiraffe.slpz` | 1 |
| `aggregate_recent/PositiveRevolvingHyena.slpz` | 1353 |
| `battlefield_recent/DelayedSuperbGuanaco.slpz` | 3 |
| `fountain_of_dreams_recent/ElatedWearyTermite.slpz` | 1 |
| `pokemon_stadium_recent/CornyDelayedOkapi.slpz` | Exact |
| `marth/InternalPowerlessWallaby.slpz` | 2 |
| `marth/ExtraLargeScaryHornet.slpz` | 2 |
| `sheik/GlaringRosyAlpaca.slpz` | Exact |
| `sheik/WavyRundownAardvark.slpz` | 1 |
| `sheik/sheik_demo_game.slpz` | 15 |
| `sheik/sheik_demo_game_2.slpz` | 55 |
| `falcon/StraightScratchyCheetah.slpz` | 1 |
| `falcon/EmotionalNaturalLemur.slpz` | 2 |
| `falcon/falcon_demo.slpz` | 37 |
| `puff/rollout_ends_ys.slpz` | 2 |
| `puff/diamond-diamond-78251f79d12df3bcdb1d4abe.slpz` | 370 |
| `peach/HeartyStiffMallard.slpz` | Exact |
| `peach/TameEmbellishedSparrow.slpz` | 723 |
| `peach/DisgustingLivelyRaven.slpz` | 1 |
| `peach/SmugConfusedTermite.slpz` | 1 |
| `peach/HappyGoLuckyScentedButterfly.slpz` | 4 |
| `peach/CandidThankfulCockroach.slpz` | 2 |
| `peach/CapitalPristineTarsier.slpz` | 2 |
| `peach/ExpertWorthlessFinch.slpz` | 5 |
| `peach/HumiliatingCreamyReindeer.slpz` | 153 |
| `peach/peach_demo_1.slpz` | 10 |
| `doubles_recent/Game_20260509T152622.slpz` | 380 |
| `luigi/platinum-platinum-c20d9507b199f01838e901d0.slpz` | 144 |
| `marios/2026-07_Game_20260704T135409.slpz` | Exact |
| `marios/34027_Game_20250422T224627.slpz` | Exact |
| `marios/4308_Game_20250404T231356.slpz` | Exact |
| `marios/6082_Game_20231226T153224.slpz` | 62 |
| `marios/64303_master-platinum-f57bf677ef721a6e844c0398.slpz` | 651 |
| `marios/medium-fox-2026-04_Game_20260413T180551.slpz` | Exact |
| `samus/fox-d18-2026-04_Game_20260419T223341.slpz` | Exact |
| `samus/master-samus-2026-03_Game_20260302T125058.slpz` | 135 |
| `icies/dl-fox-2025-05_Game_20250504T134832.slpz` | 11 |
| `icies/ys-peach-2025-02_23549_Game_20250221T133850.slpz` | Exact |
| `pikachu/master-master-97beba9993588c0e724b06c2.slpz` | 2499 |
| `pikachu/slippi-2025-11_Game_20251114T162828.slpz` | Exact |
| `dk/22123_Game_20250505T215606.slpz` | Exact |
| `dk/9560_Game_20250405T084316.slpz` | Exact |
| `dk/auto-dk-2025-04_Game_20250408T015844.slpz` | Exact |
| `dk/basic-dk-2025-04_Game_20250427T125907.slpz` | Exact |
| `dk/medium-marth-2025-09_Game_20250912T002037.slpz` | Exact |
| `dk/slippi-2025-04_Game_20250422T141746.slpz` | Exact |
| `dk/slippi-2025-04_Game_20250422T152714.slpz` | 1 |
| `ganon/2025-03_Game_20250309T055449.slpz` | 1 |
| `ganon/25827_Game_20250701T195419.slpz` | Exact |
| `ganon/medium-fox-2026-01_Game_20260122T160100.slpz` | Exact |
| `ganon/medium-sheik-2026-02_Game_20260218T175200.slpz` | Exact |
| `yoshi/ClumsyImaginaryOkapi.slpz` | Exact |
| `bowser/SophisticatedGiganticHeron.slpz` | Exact |
| `bowser/MildMurkyNewt.slpz` | 10 |
| `ness/2025-02_Game_20250223T112902.slpz` | Exact |
| `ness/2026-06_Game_20260616T195545.slpz` | 152 |
| `links/18932_Game_20250611T185955.slpz` | 10 |
| `links/slippi-2025-01_Game_20250129T125630.slpz` | Exact |
| `links/slpfiles-2025-10_Game_20251020T000422.slpz` | 2 |
