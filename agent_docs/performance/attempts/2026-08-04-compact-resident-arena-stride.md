# Compact resident Match arena stride — rejected 2026-08-04

The native Match lane capacity was reduced from 3 MiB to 1.25 MiB after the exhaustive current
construction census measured a 1,017,744-byte maximum. This changed only unused virtual spacing;
all objects, pointers, tokens, allocation order, and gameplay remained exact.

Against frozen immediate control `cecbc384...`, 65,536-frame ABBA screens preserved digests
`e6f2a9b270b4161b` and `75046e348333b63b`. Resident-256 control arms were 41,446.3/40,528.9 and
candidate arms 39,324.9/41,057.7 cycles/frame, centering about 2% faster. Resident-512 control
arms were 42,184.0/41,819.6 and candidate arms 42,406.2/43,242.5, centering about 2% slower.

The 3 MiB capacity is restored. Unused address spacing is not a stable locality lever; revisit only
if live Match state is actually repacked or the allocation model changes, not with another fixed
capacity, padding, page-size, or stride-color choice.
