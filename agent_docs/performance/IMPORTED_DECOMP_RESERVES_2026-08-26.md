# Imported decomp-port-dev reserve evidence — 2026-08-26

Historical evidence from `22136d7d:agent_docs/CURRENT_BASELINE.md`, imported during
integration into dev on 2026-09-08. These measurements belong to the incoming
branch, not the combined runtime. Its July baseline was already consolidated
under this directory on dev; only the new reserve/measurement section is retained
here. The integration's current construction census is in `HISTORY.md`.

Standing as of 2026-08-26 (not a refresh): the throughput contract above is still the 2026-07-19
9950X3D evidence and has not been re-measured on comparable hardware since. Three things a reader
must know before comparing against it. (1) The default `VALIDATION_CHARACTERS` now admits twenty
fighters, so the unqualified benchmark targets pack **492** cases (4,876,790 frames; digest
`af9b25381779d47f` at `c86bc762`), not the 153 above; pass
`VALIDATION_CHARACTERS='Fox,Falco,Marth,Captain Falcon,Sheik,Zelda,Jigglypuff,Peach'` to reproduce
the 153-replay selection. (2) On that 153 selection the production digest at `c86bc762` is
`474828382690a770`: gameplay fixes since 07-19 (re-recorded locks) moved it, and the per-fighter
reserve packet itself is digest-neutral (control `d59b65ba` and candidate `3cd7ead3` agree on every
run) and within run-to-run spread on cost (+2.0% median over three alternating samples). (3) The
host these were taken on is a shared Ryzen 9 3950X, where the 153-workload 256 batch runs at about
27k FPS / 130k cycles/frame; those figures are host-bound and are not evidence of a code regression
against the 102,667 FPS above, but a same-host comparison to the 07-19 commit was not possible
(its data-check rejects the current extracted-data profile). Refreshing this contract needs the
9950X3D host or a fresh baseline recorded on the new one.

## Current memory contract

The compact pose and stage-line owners are initialized before gameplay and participate in typed
relocation, arbitrary-index copy, and save/restore. Refreshed 2026-08-26 at `c86bc762` from
`make runtime-census` (20 characters x 6 stages x 2/4 players) after the per-fighter reserve
packet ([historical work log](https://github.com/kyhavlov/melee-sim-light/blob/5018738c8b2da68330823bb20fee37a5044841cd/agent_docs/ACTIVE_WORK.md),
"per-fighter sealed-arena reserves"): every pool reserve is
now a sum of per-fighter terms plus a Yoshi's Story item term, the pose arenas are 256 joints and
384 tracks per player, and the 192-byte JObj mem-piece class has a 128 + 128/port floor. The
ordinary two-player allocation lock is exactly 608,100 arena bytes and 868 allocations before and
after gameplay. Four Sheiks reach 1,016 pose joints inside the 1,024-joint four-player capacity;
the four-player maximum arena is four Sheik on Yoshi's Story.

| Measure | Current | 2026-07-19 |
|---|---:|---:|
| Ordinary stepped arena | 608,100 B | 631,820 B |
| Ordinary savestate | 671,140 B | 693,972 B |
| Initialization allocations | 868 | 824 |
| Maximum reached arena | 1,311,096 B of 3,145,728 | 960,000 B |
| Maximum relocation records | 5,371 of 16,384 | — |
| Maximum reached pose joints | 1,016 / 1,024 | 976 / 1,000 |
| Hosted fighter-dynamics pool | 10,752 B (64 nodes) | 10,752 B (64 nodes) |
