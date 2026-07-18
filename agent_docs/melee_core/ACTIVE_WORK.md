# Active packet — gameplay-pose admission ownership

Replace optional `MSLPART1` files with one required, source-audited immutable admission table while
preserving the retained native pose cull. This packet changes metadata ownership, not the surviving
JObj/FighterBone representation or frame scheduler.

## Scope

- In: supported-fighter part-owner audit, immutable native/Wasm table, direct runtime cutover,
  complete `MSLPART1` deletion, structural checks, exact replay/Wasm gates, and retained benchmarks.
- Out: compact-pose program work, SIMD/AoSoA changes, scheduler changes, new fighters, or restoration
  of any stashed pose experiment.

## Ownership and deletion boundary

- Final owner: hosted fighter-pose construction in `melee/ft/ftparts.c`.
- Canonical state: one compile-time read-only supported-fighter part-admission table; no mutable or
  per-GameData copy.
- Consumers: compact main/interpolation JObj construction and post-construction cold-subtree
  animation pruning.
- Source oracle: PPC deliberately retains the complete source JObj graph through compile-time target
  ownership, not runtime fallback dispatch.
- Displaced state/code: `tools/data/gameplay_parts.{py,json}`, `data/model_parts`, scalar file
  parsing, `MslCoreGameData::gameplay_parts`, its fingerprint bytes, optional availability state,
  runtime context lookup, artifact tests, and every `MSLPART1` reference.
- Completion: current native output contract or better, native API/save-restore gates, fresh
  extraction containing no pose artifact, Wasm/viewer parity, and no retained 256/512 regression.

## Action items

- [x] Audit decomp/DAT owners and supported costume topology without treating the deleted legacy
  extractor as authority.
- [x] Install the immutable table at the final fighter-parts owner and cut native/Wasm consumers over
  directly.
- [x] Delete the complete artifact/loader/fallback boundary.
- [x] Add structural coverage for supported fighters, valid part ids, ancestor closure, costume
  topology, and meaningful cold-node removal.
- [x] Run focused native, extraction, source-sync, and Wasm checks.
- [x] Run the complete 153-replay gate and retained 256/512 benchmarks.
- [x] Record final retained evidence and leave the coherent packet uncommitted for review.

## Log

- 2026-07-18: Packet opened from committed runtime `4a3340b0`. Existing unrelated cleanup remains
  uncommitted in `Makefile` and the replay-storage skill. The approved final representation and
  deletion boundary above are fixed before implementation.
- 2026-07-18: Retained the direct table-owned cutover. Native and Wasm now read the immutable table
  in `ftparts.c`; PPC keeps source construction through compile-time target ownership. Removed the
  artifact serializer, extractor emission, parser, mutable GameData copy/fingerprint bytes, context
  lookup, and missing-file/full-graph fallback. Incremental native compile and `native-smoke` are
  green. Next: prove every supported costume's topology and constructed cold/live ownership.
- 2026-07-18: Retained `gameplay_parts_smoke.c`. It constructs all 39 supported fighter costumes
  and proves mask bounds, source-descriptor topology equality, ancestor closure, live instance
  targets, actual live/cold JObj ownership, and nonempty deletion. Reached physical/live/cold
  counts were Fox 73/36/37, Falcon 63/33/30, Sheik 58/34/24, Peach 114/89/25, Puff 50/34/16,
  Marth 90/51/39, Zelda 118/88/30, and Falco 67/34/33. Next: source/extraction and API gates.
- 2026-07-18: Packet complete. Fresh empty-root extraction took 0.86 seconds and emitted no pose
  artifact; source sync, native/PPC/Python APIs, save/restore/batch smokes, Wasm parity, live viewer,
  browser smoke, and all 39 costume checks are green. The full replay gate remains 63 exact, 90
  unchanged classified, zero XPASS/fail/error, and 1,415,476 compared frames. Interleaved exact-HEAD
  A/B retained identical digests and candidate medians of 60,911 FPS at 256 (+1.47%) and 80,780 at
  512 (+0.92%); this is performance-neutral. Shared GameData is 8,488 bytes smaller. The coherent
  packet remains uncommitted for review.
- 2026-07-18: Final review removed the inherited bottom-up ancestor repair pass: all-costume
  structural coverage proves closure before runtime, so production no longer compensates for an
  incomplete admission table. Native, Python arbitrary-index save/restore, 153-replay, Wasm/viewer,
  and browser gates remain green. Final spot checkpoints were 61,449 FPS at 256 and 81,222 at 512
  with the retained digests.
