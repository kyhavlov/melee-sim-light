# Reference checkouts

Reference repositories are local-only and ignored by Git. The primary gameplay source is
[`doldecomp/melee`](https://github.com/doldecomp/melee), pinned by `src/upstream.lock`.

Expected checkouts:

- `melee/`: decomp source and GALE01 assembly.
- `slippi-ssbm-asm/`: replay, UCF, neutral-spawn, and other gameplay patches.
- `Ishiiruka/`: pinned Slippi Dolphin fork used only for playback forensics.
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
```

Use `refs/melee/src/` first, its matching assembly when operation order or an incomplete function
matters, and then Slippi/UCF sources for replay-runtime modifications. For exact-frame playback
forensics, keep outputs under ignored `reports/triage/`; no Dolphin probe code is part of the
production simulator.
