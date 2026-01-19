# Data Contract (living)

This doc defines what **ISO-derived game data** we extract and how it is laid out on disk so the C core can load it.

Policy:
- All gameplay-critical numbers come from **game files** (`SSBM.iso` / `_iso/*.dat`) and/or decomp.
- Extracted outputs live under `data/` and are **generated artifacts** (gitignored).
- The C core should load these at initialization (no parsing during `step`).

## Canonical paths (current target domain)

Stage (Final Destination):
- `_iso/GrNBa.dat` (source)
- `data/stages/final_destination.json` (extracted collision segments; decomp-first)
  - Note: the C core currently parses this JSON **once at init** (temporary). We will move to a compact
    binary artifact for stage collision later to avoid JSON parsing overhead/complexity.
  - Safety guard: the loader caps `line_count` at 4096 to prevent unbounded allocations on malformed files.

Common constants:
- `_iso/PlCo.dat` (source)
- `data/common/ft_common_data.json` (ftCommonData constants; decomp-first)

Characters (Fox/Falco):
- `_iso/PlFx.dat`, `_iso/PlFc.dat` (source)
- `data/characters/fox.json`, `data/characters/falco.json` (movement/ecb/laser/reflector attrs; decomp-first)
- `data/hurtcaps/fox.json`, `data/hurtcaps/falco.json` (hurt capsule init tables; decomp-first)
- `data/moves/fox.json`, `data/moves/falco.json` (subaction timelines for key motions + specials; decomp-first)
- `data/anims/fox.bin`, `data/anims/falco.bin` (per-msid bone matrices + TransN; decomp-first)
- `data/anims/fox.blend.bin`, `data/anims/falco.blend.bin` (blend/dynamics bytes; decomp-first)

Notes:
- `animation_index` in Slippi post-frames includes `0xFFFFFFFF` as a sentinel; treat that as “no animation”.
- Item reference ordering in `.msl` datasets is **sorted by item `instance_id`**, then `id`, then `type`. The sim should follow the same stable ordering for its fixed 15 slots.

## What the C core should load (minimum)

To avoid “mystery drift”, prefer loading the following early:
- **Stage collision**: segments + ledge flags + platform flags + a stable `ground_id` mapping.
- **Fighter attributes**: gravity/terminal velocity, traction/friction, jump velocities, ledge snap params, ECB joints/offsets.
- **Move event tables**: hitbox/hurtbox state changes, IASA/interrupt windows, articles (laser), throw flags, etc.
- **Animation attachment data**:
  - Either (A) full per-bone matrices per frame for the bones we need, or
  - (B) precomputed per-frame attachment transforms for hitboxes/hurtboxes/ECB sources.
  - The extractor currently outputs (A)-style matrices in `data/anims/*.bin` for Fox/Falco.

`ground_id` semantics (current assumption):
- Our datasets store `ground_id` directly from Slippi post-frame `ground` (see `tools/slippi/make_dataset_from_slp.py`).
- The simulator treats `ground_id` as the **stage collision segment/line index** from the stage’s collision table.
  - For Final Destination this matches the ISO-extracted indices in `data/stages/final_destination.json` (`segments[].i`), e.g. the main floor uses segment indices `0`, `1`, and `5`.
  - If we discover a stage-specific remapping (decomp / Slippi schema clarification), update this note and the stage loader accordingly.

## Build the data (one-shot / cached)

Generate all required artifacts for the current target domain:

```bash
uv run python -m tools.extraction.build_data \
  --iso-dir _iso \
  --stage grnba \
  --chars fox,falco
```

If `_iso/` is missing required `.dat` files, extract them from `SSBM.iso` first:

```bash
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*GrNBa.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlCo.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlFx.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlFc.dat'
```

## Schema notes / stability

This project avoids explicit “versioning” in naming. Instead:
- Generated files should include enough **self-checks** (magic strings, record sizes, offsets) to fail loudly when incompatible.
- Contract changes should be reflected here immediately, and extraction scripts updated accordingly.

## Fast consistency check (no rebuild)

If you already have local `data/` artifacts, you can validate basic consistency without re-extracting:

```bash
uv run pytest -m integration
```
