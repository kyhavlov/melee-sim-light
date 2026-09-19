# Native simulator regressions

`yoshi_vector_pool.npz` contains 2,260 frames of controller inputs for a
four-Yoshi Final Destination game, captured on September 19, 2026 with the
v49 Slippi-AI policy at 21,596 updates. The old simulator aborts on the final
frame when fighter hurtbox traversal exhausts its 128 spare scale vectors.

The fixture contains only match configuration, inputs, and a SHA-256 of the
2,259 pre-crash observations from unpatched main (`6d55f60e`, Linux x86-64,
GCC 13.3 release build). That prefix was recorded before applying the fix;
main also aborts on frame 2,260. It requires no policy checkpoint. The regression
checks the unchanged observation prefix, completion of the formerly failing
frame, and save/restore continuation at another batch index.

The construction census separately forces every registered fighter joint to
own a scale vector for all supported fighters, all six stages, and 2/3/4
players. This includes Nana and both transformation halves, preserves at least
128 free vectors for other owners, and checks that the sealed arena never grows.

Capture evidence is retained on gigaserver under
`/mnt/nvme-data/slippi-ai/reports/triage/v49_crash_recovery_20260919/`;
main before/after checks are under `reports/triage/vector_pool_main_20260919/`
in the same Slippi-AI install.
