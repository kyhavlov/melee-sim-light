---
name: melee-validation-replay-storage
description: Use when changing validation replay storage, suite replay paths, Slippi .slp/.slpz handling, replay conversion tooling, or validation commands that read replay files.
---

# Melee Validation Replay Storage

## Purpose
Use this skill for storage-format or path-resolution work around validation replays. The goal is to preserve validation behavior while changing how replay bytes are stored or found.

## Stable Workflow
- Treat validation replay storage changes as tooling/data plumbing, not gameplay changes.
- Keep manual one-off validation compatible with both `.slp` and `.slpz` paths.
- When validation suites move to `.slpz`, old `.slp` paths should resolve to an existing `.slpz` sibling where practical.
- If a downstream tool needs a physical `.slp` file, decompress `.slpz` into a temporary `.slp` at the tool boundary and remove it afterward.
- Do not update generated validation reports unless the relevant validation command was rerun.

## Compatibility Checklist
1. Check suite loaders, direct CLI replay arguments, tests, and forensic/Dolphin tooling for direct `.slp` assumptions.
2. Add regression tests for:
   - `.slpz` round-trip/path-helper behavior,
   - legacy `.slp` path resolving to `.slpz`,
   - direct parser/tool callers that need a temporary physical `.slp`.
3. Confirm new validation replay files match `.gitattributes` LFS rules.
4. Confirm old validation `.slp` files are staged as deleted when replacing them with `.slpz`.
5. Run focused changed-path tests before claiming compatibility.
6. Run `make validation-supported-domain` when suite replay paths change.

## Benchmark Guidance
- Benchmark direct `.slp` versus `.slpz` validation with equivalent suites and warmed comparable inputs.
- Store benchmark scratch data under `reports/triage/`.
- Expect `.slpz` to save storage but not necessarily runtime: if the parser consumes `.slp`, `.slpz` adds decompression/reconstruction before normal parsing.
