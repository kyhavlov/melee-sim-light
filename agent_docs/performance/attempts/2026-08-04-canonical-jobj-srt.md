# Canonical native JObj SRT relocation

Status: rejected and removed. Date: 2026-08-04.

## Hypothesis and boundary

The retained dense-matrix candidate shrinks native `HSD_JObj` by moving its derived 48-byte world
matrix into the existing fixed matrix pool. This follow-up tested whether moving the remaining
40-byte scale/rotation/translation record out of the mixed JObj graph could likewise make fighter
pose publication and its geometry consumers more local without changing values, ordering, dirty
flags, allocation timing, PPC, or Wasm.

Two complete singular-state forms were tested against frozen pre-packet binary
`build/melee_core/perf-dense-jobj-control/replay-bench` (`cecbc384...`):

- Registered fighter pose nodes owned their JObj SRT inline; non-fighter JObjs used a Match-local
  fixed pool. Registration transferred the value and deleted the temporary pool record.
- Every native JObj used the fixed SRT pool, leaving the existing 56-byte compact pose node intact.

Neither form kept an inline JObj SRT, shadow state, compatibility path, gameplay allocation, or
arithmetic change. Both preserved the 32,768-frame production digests `4124834367a202ec` and
`588be8489df1a62d`.

## Performance result

Pose-node ownership enlarged the mandatory compact pose record from 56 to 96 bytes. Its
65,536-frame C-A-A-C bracket centered about 0.7% faster at resident 256 and 0.6% faster at resident
512—far below the churn and profile ceiling.

Pool ownership restored the 56-byte node. In its final 65,536-frame C-A-A-C bracket, resident-256
controls were 38,605.2 and 38,469.6 cycles/frame while candidates were 38,629.9 and 41,045.6; the
symmetric center regressed about 3.2%. Resident-512 controls were 40,063.9 and 43,677.1 while
candidates were 41,882.3 and 41,396.9; the symmetric center improved only about 0.5%.

The final source-timed resident-512 profile assigned 18.01% to pose animation, 12.02% to stage
collision, and 30.86% to the inclusive `Fighter_8006A360` owner. Moving SRT storage did not lower
the pose share or delete downstream products; it only traded JObj stride for another pointer and
allocation stream.

## Decision and revisit rule

Remove the SRT type, pool, transfer lifetime, access macros, and all consumer conversions. Keep the
independently measured dense-matrix representation under review. Do not revisit SRT relocation as
another JObj or pose-node layout. A future canonical fighter-state cut may contain SRT only if it
also deletes a material producer/consumer boundary and directly reduces a several-thousand-cycle
owner; co-location by itself is disproven.
