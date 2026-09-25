# Native simulator regressions

`yoshi_vector_pool.npz` contains 2,260 frames of controller inputs for a
four-Yoshi Final Destination game. An undersized vector pool aborts on the
final frame when fighter hurtbox traversal exhausts its 128 spare scale vectors.
The test checks the exact 2,259-frame pre-crash prefix, completion of the formerly
failing frame, and save/restore continuation at another batch index.

The inputs are unchanged from the original capture. The configuration now pins
its original directions explicitly: player 0 faces right and players 1–3 face
left. This preserves the captured matchup after correcting automatic facing.
The prefix reference uses the corrected reset semantics: frame -123 is published
after the first neutral tick. Its hash was recorded from a diagnostic build with
only the vector reservation reverted to 128. That build still aborts on input
2,260 through `lbColl_TransformHurt -> HSD_JObjMakeMatrix -> HSD_VecAlloc`, while
the production build matches every preceding observation and completes the tape.

`legacy_prefix_sha256` preserves the original reference from unpatched main
(`6d55f60e`, Linux x86-64, GCC 13.3 release build). A diagnostic build restoring
the earlier public reset/facing behavior reproduces that hash; it is not the
reference for the corrected startup semantics.

The empty `observation_schema` array records the original observation dtype.
The prefix check copies those named fields into that fixed layout before hashing,
so adding observations does not change the reference bytes. Missing fields still fail.

The construction census separately forces every registered fighter joint to
own a scale vector for all supported fighters, all six stages, and 2/3/4
players. This includes Nana and both transformation halves, preserves at least
128 free vectors for other owners, and checks that the sealed arena never grows.
