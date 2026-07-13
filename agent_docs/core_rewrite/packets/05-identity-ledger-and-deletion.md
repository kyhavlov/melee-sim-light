# Packet 5: Identity Ledger and Deletion

Status: complete.

## Identity closure

The six supported ISO tables contain 302 distinct non-null Coll pointers. MSLMSO01 v28 separates
their executable meaning into 32 pointer-derived wrapper-selector families (including `NONE`) and
a compact post-mpColl policy. Fixed selectors identify one exact low-level wrapper; procedural
selectors resolve live GA, SDI, ledge cooldown, command, capture, or match-flow state inside the
installed callback. Geometry is no longer reconstructed from policy-bit combinations.

The compact policy retains only same-callback continuations: cliff catch, walljump/walltech, basic
landing, platform-pass callback, Falcon Throw0, floor loss, and supported special ground-to-air
transitions. Jump/Fall floor-skip carry is a pre-callback owner recorded in generated class3 bits;
validation preprocessing no longer consumes deleted v27 plan positions. Zero-selector callbacks
are empty, attachment/item-owned, or outside the supported map-geometry boundary and never invoke a
legacy fallback.

## Deleted runtime

The following displaced files are deleted and absent from `setup.py`:

- `mpcoll_floor.c` / `.h`;
- `mpcoll_floor_callbacks.c`;
- `mpcoll_ground.c` / `.h`;
- `mpcoll_wall_ceil.c` / `.h`.

The 41-name `MSL_MPCOLL_REJECT_*` taxonomy, its always-written debug/API lane, collision semantic
query APIs, duplicated post-collision passes, and evaluator-lifecycle gameplay gates are also
deleted. The extension builds one map-collision implementation: `mp_lib`, `mp_coll`, and the source
air/ground owners.

Tracked runtime changes are 20,155 net deleted lines before adding the six new source/header files;
including those files, runtime `src/` is approximately 17.1k lines smaller than Packet 1. The
complete tree is approximately 26.2k net lines smaller after new files are included.

## Correctness and performance

Compared with Packet 1:

| Gate | Before | After | Delta |
|---|---:|---:|---:|
| aggregate one-step | 9,918 | 8,689 | -1,229 (-12.4%) |
| aggregate rollout first mismatches | 1,667 | 1,396 | -271 (-16.3%) |
| doubles one-step | 8,332 | 7,713 | -619 |
| doubles rollout | 1,252 | 1,193 | -59 |
| Falcon one-step / rollout | 3,143 / 596 | 2,537 / 526 | -606 / -70 |
| Sheik one-step / rollout | 3,069 / 462 | 2,711 / 441 | -358 / -21 |

The primary Fox/Falco control moves from 30 to 46 one-step mismatches and from 0 to 9 rollout first
breaks; this and the remaining replay-level redistributions are explicitly retained in
`../residuals/phase1-validation.md` for follow-up rather than masked with old code.

Final bounded ThinLTO sanity gates on the documented Ryzen 9 9950X3D host (the preceding full-LTO
cutover measurements were 429–433k random and 325–327k replay):

- mixed random, 5,000 frames x 256: 424,937 FPS; p99/average 1.30x;
- fixed five-replay sample, 15,000 records x 10: 311,709 FPS; p99/average 4.17x.

The random workload exceeds its 400k gate. The replay workload is 2.6% below its 320k gate and
3.8% below the preceding committed report; neither workload shows a pathological tail.
