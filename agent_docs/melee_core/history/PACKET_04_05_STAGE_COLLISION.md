# Packet 4–5 — compact stage collision and spatial candidates

## Proposed final boundary

Queue items 4 and 5 were evaluated together because a compact topology whose hot queries still
walked the retail pointer graph would have been an intermediate representation, while a spatial
index bolted onto that graph would have been compatibility machinery. The proposed final owner was
immutable stage-collision data in `MslCoreGameData`: stable line ids, local endpoints, topology,
material/flags, joint membership, and static spatial candidates. A bounded per-Match overlay would
have owned only transformed/current and previous dynamic endpoints, line enable/hidden/kind bits,
joint flags/bounds, stage callbacks, and dynamic candidate membership.

Every supported hosted query would have consumed candidate ids from that representation and run
the exact source narrow phase in stable retail order. The deletion boundary included per-Match
copies of immutable `MapLine`, pointer-bearing `CollLine`, immutable vertex coordinates,
pointer-linked static-joint traversal, and full per-kind scans. Fighter `CollData`, exact
intersection/projection math, soft platforms, line-id tie breaking, ledges, line stitching, and
moving-platform semantics were explicit exclusions.

## Evidence and disposition

The supported extracted stages contain 11 (Final Destination), 23 (Battlefield), 34 (Fountain of
Dreams), 136 (frozen Pokemon Stadium), 29/30 extracted segments (Yoshi's Story), and 16 (Dream Land
N64). Their existing hosted dense collision owners consume under roughly 9 KiB per Match at the
largest stage. The later AoSoA packet will replace much larger surrounding per-Match state, so this
topology is not presently a material memory owner.

A temporary macro-gated census preserved the production schedule and exact canonical digest
`3fb5823d90657775`. Over 32,768 representative match-frames it observed:

| Internal stage | Stage | Bounding calls | Active-line visits | Ideal swept-AABB candidates | Exact intersection calls |
|---:|---|---:|---:|---:|---:|
| 10 | Yoshi's Story | 13,078 | 379,262 | 1,392 | 3,012,642 |
| 12 | Fountain of Dreams | 5,946 | 202,164 | 88 | 28,596 |
| 16 | frozen Pokemon Stadium | 10,007 | 220,154 | 1,525 | 409,662 |
| 28 | Dream Land N64 | 7,650 | 84,150 | 108 | 78,866 |
| 36 | Battlefield | 15,212 | 349,876 | 1,118 | 1,951,943 |
| 37 | Final Destination | 18,451 | 295,216 | 175 | 313,321 |
| **Total** | | **70,344** | **1,530,822** | **4,406** | **5,795,030** |

The candidate reduction looks large, but the existing exact intersection owner already performs
the same AABB rejection in its first comparisons. The preserved production Callgrind run attributes
about 5.28 million of 270.06 million retired instructions (1.95%) to all floor/ceiling/wall scan
functions, special remap scans, intersection helpers, and bounding traversal together. Deleting the
entire measured boundary has only a roughly 1.02x instruction-count ceiling; a real index must keep
candidate lookup, dynamic/remap handling, exact ordering, and narrow-phase work.

This potential packet is therefore **rejected before production cutover**. It cannot justify a
cross-cutting rewrite of the 7,000-line mpLib consumer surface now, and such a cut would be replaced
again by the later AoSoA/homogeneous collision kernel. The temporary counters and build switch were
removed completely; no runtime candidate, bridge, second state, or compatibility path remains.
Spatial selection should be designed inside queue items 8–10's final cross-environment collision
kernel, where the same candidate masks can feed SIMD lanes and avoid scalar pointer topology
altogether.

## Chronological log

- **2026-07-17 — opened (`open`).** Declared the immutable shared topology, bounded mutable overlay,
  complete consumer/deletion boundary, exclusions, and early proof plan before runtime edits.
- **2026-07-17 — production census (`rejected`).** The exact-digest diagnostic found a large raw
  line-attempt reduction but only a 1.95% whole-program retired-instruction owner and a ~1.02x
  deletion ceiling. Removed the diagnostic completely. Next: queue item 6, with spatial collision
  deferred to the final AoSoA/SIMD execution boundary rather than implemented as scalar machinery.
