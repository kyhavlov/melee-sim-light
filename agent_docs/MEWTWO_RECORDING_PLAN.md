# Mewtwo corpus expansion and recording plan

## Local Erickfm search — 2026-09-15

Inspected ExPhil's `scripts/build_replay_registry.exs`, `query_replays.exs`,
`scan_replays.exs`, and Python replay scanner. No suitable existing replay
registry was located. Used Peppi's Python bindings for metadata verification and
validation. A fixed game-start header prefilter inspected all 7,911 FOX files and
93,438 partner files; named Mewtwo candidates also came from the Hugging Face
folder. This is not an exhaustive search of every dataset on the computer.

SHA-256 deduplication yielded **74 Mewtwo recordings**:

- 68 lack the required start scene field (protocol 1.7.1 or 2.0.1).
- 5 lack the required `animation_index` frame field (protocol 3.9.0).
- 1 protocol 3.14 recording can run with an explicit recording-profile assumption,
  but does not pass. Its metadata is empty; scene major 8 supports the diagnostic
  network profile, without establishing an exact recorder build.

Each old recording was probed against the strict validator and its actual
rejection retained in [MEWTWO_ERICKFM_INVENTORY.json](MEWTWO_ERICKFM_INVENTORY.json).
Do not fill missing fields with simulated values or count these as strict passes.
They remain useful for watching interactions and selecting future demonstrations.

## Retained historical-profile recording

`replays/validation/mewtwo_erickfm/master-master-5de7543e8002c09425f1f2d4.slpz`
is a lossless copy of the ranked Mewtwo/Fox game on Yoshi's Story. Original source
path and SHA-256 are in the inventory. It is outside the aggregate/pass suites.

Diagnostic full runs (9,458 transitions each):

| Assumed profile | Exact prefix | First mismatch | Mismatching rows |
| --- | ---: | --- | ---: |
| network, current cardinal patch | 83 transitions | -39: Fox self-speed and position | 9,271 |
| network, pre-cardinal UCF, modern shield drop | 477 transitions | 355: Fox action 178 expected, 235 actual | 8,981 |
| network, pre-cardinal UCF, UCF0.8 shield drop | 9,458 transitions | None | 0 |

The older shield-drop capability resolves the entire recording. Fox rotates a
held shield input to (0.7,-0.7) on solid ground; UCF0.8 suppresses the spot dodge
there, while 0.84 requires a platform. A debugger trace confirms the source
branch inputs. Missing raw C-stick data does not cause this decision.

Reproduce with `validate_one(..., played_on="network",
ucf_cardinals_1_0_enabled=False, ucf_shield_drop_084_enabled=False,
frames=0, start_frame=None, backend="native")`.

This is exact under an inferred profile, not recovered original build provenance.
It remains outside aggregate suites. See `FOX_UCF_SHIELD_REPRODUCER.md` and
`tests/test_mewtwo_erickfm_profile.py` for source evidence and regression controls.

## Useful new recordings

Use normal Slippi games and retain the full original `.slp`. Short games with
repeated attempts are sufficient; no need to reset settings or determine the old
Dolphin version. Record failed attempts as well as successful ones.

1. **Teleport and ledges:** on Battlefield or Yoshi's Story, teleport toward each
   ledge from below and from outside; include successes and misses, different
   heights, and facing toward/away from the stage.
2. **Teleport and surfaces:** teleport into a wall, upward into a platform's
   underside, onto a platform from above, and diagonally across a platform edge.
3. **Confusion and projectiles:** use Falco laser or Samus missiles/charge shot;
   try grounded and airborne Confusion, with hits inside and outside its reflect
   window. A human-controlled second port is easiest for repeatable timing.
4. **Shadow Ball absorption:** Ness uses PSI Magnet against partial/full Shadow
   Balls; include a control hit with Magnet inactive.

These challenge the remaining coverage gaps; they are not claims of known bugs.
Ordinary full matches on several stages also help, but targeted interactions are
more useful now than adding many more Mewtwo-versus-Fox matches on FD.

## September 15 follow-up

Three new human recordings and seven scripted Dolphin recordings now pass exactly.
They cover seven direct Teleport-to-ledge catches, grounded Confusion against Falco
laser/Samus missile with controls, and partial/full Shadow Ball absorption by Ness.
See [MEWTWO_TARGETED_REPLAYS.md](MEWTWO_TARGETED_REPLAYS.md). The old-corpus
failure is now resolved under the inferred historical UCF profile above. No additional user recording is required for
the projectile-reflection/absorption cases delegated to the assistant.
