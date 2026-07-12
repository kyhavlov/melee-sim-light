# Reference checkouts

Reference repositories are local-only and ignored by Git. The primary gameplay source is
[`doldecomp/melee`](https://github.com/doldecomp/melee), pinned by `src/upstream.lock`.

Expected checkouts:

- `melee/`: decomp source and GALE01 assembly.
- `slippi-ssbm-asm/`: replay, UCF, neutral-spawn, and other gameplay patches.
- `Ishiiruka/`: pinned Slippi Dolphin fork used only for playback forensics.
- `slippi-dolphin/`: mainline-based Slippi Dolphin (project-slippi/dolphin `engine-dump`
  port) with the same MSL_* interpreter probes; builds on modern macOS/ARM64 where
  Ishiiruka does not.
- `ucf/`: readable reference implementation for UCF behavior.
- `slippi-wiki/`: replay format documentation.
- `slippilab/`: optional source for viewer character assets.

```bash
git clone https://github.com/doldecomp/melee.git refs/melee
git clone https://github.com/project-slippi/slippi-ssbm-asm.git refs/slippi-ssbm-asm
git clone https://github.com/project-slippi/slippi-wiki.git refs/slippi-wiki
git clone https://github.com/UnclePunch/UCF.git refs/ucf
git clone https://github.com/frankborden/slippilab.git refs/slippilab
git submodule update --init refs/Ishiiruka
git submodule update --init refs/slippi-dolphin
```

Use `refs/melee/src/` first, its matching assembly when operation order or an incomplete function
matters, and then Slippi/UCF sources for replay-runtime modifications. For exact-frame playback
forensics, keep outputs under ignored `reports/triage/`; no Dolphin probe code is part of the
production simulator.

## Engine-dump playback probes

When decomp/asm cannot answer "what did the game compute on this exact frame", play the
replay back through a probe Dolphin and dump the engine state. On modern macOS/ARM64 use
`refs/slippi-dolphin` (see its `build-mac.sh`; the binary lands in
`build-engine-dump/Binaries/dolphin-emu-nogui`). The MSL_* probe environment variables
match the Ishiiruka fork, plus an extended `MSL_PROBE_PC_TRACE` that dumps caller
registers and script bytes at the range-start PC.

macOS/ARM64 driver quirks: run `dolphin-emu-nogui` with `-p headless` (the default opens
a window), set `CPUCore = 4` (ARM64 JIT), the null audio backend is named
`No Audio Output` (not `NullSound`), and `Binaries/Contents/Resources/Sys` must exist
(symlink to `Binaries/Sys`) or boot fails the Melee GameSettings check.
