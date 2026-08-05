# Unchanged direct-pose publication — rejected 2026-08-04

## Hypothesis and boundary

The compact pose publisher measured 1,247,936 bit-identical rows among five million direct
publications. The candidate compared the two dominant dense masks (`0x00E` and `0x0EE`) with the
canonical JObj SRT and omitted stores plus `JOBJ_MTX_DIRTY` when the complete authored row was
unchanged. Construction marked every retained dynamics-chain JObj in an existing pose descriptor
byte so externally authored matrices could never take the omission. No cache, duplicate state,
allocation, approximation, or consumer API was added.

This was an exact completion of the earlier incorrect unconditional experiment: the JObj remained
the sole SRT/matrix owner, the compiled pose program remained the immutable sample owner, and every
known direct matrix writer was excluded before gameplay.

## Correctness

Strict native release short screens remained exact:

- resident 256: digest `4124834367a202ec`
- resident 512: digest `588be8489df1a62d`

Because the first controlled performance screen rejected the candidate decisively, the full
supported-domain gate was not spent and all implementation code was removed.

## Controlled result

The immediate pre-packet dirty aggregate and a frozen candidate binary alternated on CPU 0 over
the balanced 366-case manifest, 32,768 measured match-frames, and eight warmup ticks. Lower
cycles/frame is better.

| Resident | Pre-packet | Candidate | Candidate throughput |
|---:|---:|---:|---:|
| 256 | 40,928.8 | 40,954.8 | -0.06% |
| 256 | 42,272.7 | 41,572.1 | +1.69% |
| 512 | 42,408.5 | 46,391.5 | -8.59% |
| 512 | 40,839.3 | 46,131.5 | -11.47% |

The resident-256 symmetric center is about +0.8%, inside the observed short-run spread. The
resident-512 symmetric center is about -10.0%, which rejects the cut without a longer run.

## Disposition and revisit criterion

Remove the construction marker, publisher comparisons, and declaration completely. Comparing
every dense publication reads canonical SRT and adds hot branches whether or not the row repeats;
the downstream matrix setup avoided by the admitted minority is too sparse or too late to pay for
that work at the larger resident working set.

Do not retry per-row or per-channel dirty detection. A materially different revisit must remove
the scalar pose-publication and geometry-consumer boundary itself—for example, a singular exact
product owner directly consumed by a coherent batch phase—not put more detection in front of the
same JObj machinery.
