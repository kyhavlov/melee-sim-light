# Active performance packet — demand-owned hurt-capsule publication

## Objective

Delete unconditional hosted hurt-capsule matrix publication from `Fighter_ProcessHit_8006D1EC`.
The source collision primitives already publish an invalid capsule exactly once on first use through
`HurtCapsule::skip_update_pos`; the headless human runtime has no CPU targeting consumer for the
eager `ftCo_800A0DA4` pass. Frames with no actual contact candidate should do no hurtbox geometry
work. Retain only if exact correctness is green and the 512-environment gain is at least 2%.

## Final boundary

- **Final owner:** `lbColl` hit/hurt, grab/hurt, shield, reflect, and item contact primitives publish
  each demanded hurt capsule immediately before its first exact narrow-phase read.
- **Canonical state:** the existing capsule offsets, positions, matrix, and `skip_update_pos` bit.
  `Fighter_procUpdate` invalidates the bit after movement; no demand mask, cache, or second capsule
  representation is added.
- **Consumers:** fighter hit/grab/throw/reflect/absorb contact, item contact, damage resolution, and
  viewer output retain their existing source paths.
- **Displaced work:** hosted `Fighter_ProcessHit_8006D1EC` no longer walks every hurt capsule and
  sets up its bone matrix unconditionally. The non-hosted source oracle retains `ftCo_800A0DA4`.
- **Deletion boundary:** no hosted eager hurt publication remains. Every gameplay consumer either
  uses an existing lazy collision primitive or must explicitly demand the capsule at its source
  owner; restoring the unconditional pass is not an accepted correctness fix.

## Sequence

1. Remove the hosted eager call and require unchanged benchmark digest plus a focused replay gate.
   If red, identify the missing gameplay consumer and demand publication there.
2. Measure adjacent 512 control/candidate throughput. Reject below 2%; do not tune collision leaves.
3. If retained, require unchanged 256/512 digests, 63 PASS / 90 unchanged CLASSIFIED / zero full-
   suite failures, native API/copy/save-restore/allocation, PPC, Wasm/viewer, source, and formatting.
   Record evidence, send Discord progress, and commit implementation and evidence atomically.

## Baseline and evidence

- Runtime: `98c78d85`
- 256: 68,850 FPS median, digest `8ef126a41244d514`
- 512: 65,652 FPS candidate median, digest `6f91f23e3553a090`
- Fresh exact profile: `Fighter_ProcessHit_8006D1EC` is 4.55% of the contract. A prior idle-path
  census found 96.0% of visible calls otherwise idle, but retained `ftCo_800A0DA4` because its
  hurtbox output was assumed mandatory. `lbColl_800083C4` and all hit/hurt primitives already test
  and set the same invalidation bit lazily; hosted `ftCo_800A0DA4` explicitly discards the only
  other output, the CPU targeting box.

## Log

- 2026-07-19 — `open`
  Scope: hosted eager hurt publication deletion only.
  Hypothesis: collision demand is sparse enough that source-owned lazy publication removes at least
  two whole-frame points from the measured 4.55% ProcessHit owner.
  Evidence: `Fighter_procUpdate` clears every capsule's `skip_update_pos` after movement;
  `lbColl_800083C4` and contact primitives publish only when that bit is clear. All supported
  participants are externally controlled humans, so the CPU box discarded by the hosted function
  is dead.
  Disposition: cut only the unconditional hosted call; preserve every source collision primitive.
  Next: run exact digest and focused validation, then adjacent 512 A/B if green.
- 2026-07-19 — `rejected`
  Scope: make every hosted hurt capsule fully lazy without preserving dynamic-chain publication.
  Hypothesis: all gameplay consumers enter a lazy `lbColl` primitive, so no eager matrix side effect
  remains live.
  Evidence: benchmark digest stayed exact and throughput reached 68,590 FPS, but the complete suite
  found seven missed-contact failures. Keeping the original pass only for fighters with surviving
  dynamics restored all outputs, proving that the next-frame dynamic solver consumes matrices
  published through hurt capsules even on frames with no contact.
  Disposition: reject the all-lazy boundary, not the packet. Publish only hurt capsules whose bone
  is actually present in a surviving canonical `DynamicsDesc`; ordinary capsules stay lazy.
  Next: derive dynamic capsule demand from the live descriptor graph, preserve hurt-capsule order,
  and rerun the full gate and adjacent A/B.
- 2026-07-19 — `retained`
  Scope: final demand-owned ordinary publication plus exact dynamic-chain publication.
  Hypothesis: deriving eager demand from canonical dynamics JObj identity preserves solver state
  while deleting every ordinary unconditional matrix transform.
  Evidence: four adjacent 512 control/candidate pairs preserve digest `6f91f23e3553a090`; raw
  medians improve 65,032 to 66,953 FPS (+2.95%) and median paired improvement is +2.89%. Three 256
  pairs preserve digest `8ef126a41244d514`; raw medians improve 68,518 to 70,847 FPS (+3.40%) and
  paired median is +3.53%. The complete gate is 63 PASS / 90 unchanged CLASSIFIED / zero failures
  across 1,415,476 frames; native API/copy/save-restore/allocation, source sync, PPC, Wasm parity,
  viewer, and formatting all pass.
  Disposition: retain and atomically commit. No demand state, cache, allocation, character branch,
  or changed narrow phase remains.
  Next: refresh the committed profile and select the next whole-work deletion or production-shaped
  batch/data-layout owner.
