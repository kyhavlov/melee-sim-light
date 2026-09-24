# Reference checkouts

Reference repositories are optional developer tools, not runtime dependencies.
Most are ignored local checkouts; the two Dolphin forks are pinned Git submodules. The primary gameplay source is
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
git -C refs/melee checkout 91b9789fa6539ea847998a6ed330fa748fa200ec
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

Invocation is `-p headless -e SSBM.iso -u <userdir> -i <userdir>/Slippi/playback.txt`, with
`engineDumpPath` in the playback JSON. Interpreter probes fire only inside
`MSL_PROBE_INTERPRETER_FRAME_START/END`; the EXI device swaps the CPU to the interpreter for
that window. Set `MSL_PROBE_EXIT_FRAME` to the last frame of interest and the capture
terminates itself — a bounded few-frame window then finishes in seconds, so retail ground
truth is worth reaching for early rather than last. Without it the process never exits and
needs an external alarm wrapper. Flush probe output per line; a killed process loses buffered
rows. Frame numbering: the probe counter at `0x804D6CF4`, the dump `frame_index`, and the
validator frame agree; the sim's `match->frame_id` at `msl_core_match_step_prepare` is one
behind the validator frame being produced. Anchor by values, not labels.

The probe driver scripts (`tools/dolphin/*.py`, including the engine-dump reader) are not
present on `decomp-port-*` branches; take them from a branch that has them, e.g.
`git show newchar-puff:tools/dolphin/engine_dump_io.py`. Some carry stale flags — check the
Dolphin invocation and the generated `Dolphin.ini` before trusting a run. Audio must be
`Backend = No Audio Output` (`NullSound` is not a valid name in this fork and falls back to
an audible default), and a fresh `--user-dir` yields an empty probe file on its first run;
rerun against the warm user dir.

macOS/ARM64 driver quirks — Linux hosts need none of these: set `CPUCore = 4` (ARM64 JIT),
`Binaries/Contents/Resources/Sys` must exist (symlink to `Binaries/Sys`) or boot fails the
Melee GameSettings check, and `[Core] GFXBackend = Null` avoids Metal, whose IOGPU shared
memory leaks across repeatedly killed runs until Dolphin aborts at boot.
