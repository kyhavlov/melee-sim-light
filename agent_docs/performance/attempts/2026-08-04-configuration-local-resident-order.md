# Configuration-local resident order — rejected 2026-08-04

## Boundary

`MslCoreBatch` assigned first-reset configurations to stable physical Match slots while preserving
public lane identity through two inverse index arrays. Gameplay still traversed sequential physical
arenas; input, mask, output, copy, save, and restore indices mapped back to public lanes. The final
layout sorted the complete resident-256 batch and independent 128-lane groups above that size by
player character tuple and stage. It added no gameplay state, post-create allocation, phase seam,
callback change, or approximation.

## Correctness

Both production digests remained exact (`bdff41cf74a54850` and `ee9d93c545aa3ef9`). Native batch
smoke covered a non-identity mapping, masks, arbitrary save/restore and copy, observation,
terminal, viewer, repeated reset, and item lifecycle.

## Performance and decision

Logical-only sorting was immediately bad because it jumped among 3 MiB arena strides. Physical
global sorting produced noisy favorable parent comparisons but was weak at 512, so the completed
form bounded large batches to 128-lane groups. One 262,144-frame resident-512 immediate pair
improved `42,997.3 -> 40,141.1` cycles/frame (+7.1%). Resident 256 did not hold: the two
131,072-frame directions were `38,294.0 -> 39,735.8` and `39,063.7 <- 39,057.9`, a symmetric
center of roughly 38,675 control versus 39,394 candidate cycles/frame (about 1.9% lower
throughput). The packet was removed completely.

Configuration locality can help the larger cache-pressure case, but public-row permutation and
altered immutable working-set order cost too much at 256. Revisit only if Match hot state itself is
made materially smaller or the public input/output representation is changed as the canonical
batch layout; do not retry key or block-size tuning on the current representation.
