# Packet 3 — gameplay-observed headless camera

- **Queue item / confidence:** item 3; definite for presentation-only/dead standard-mode work,
  potential for the complete packet.
- **Final owners:** the source fighter camera-box callback publishes each `CmSubject`; one hosted
  standard-camera callback advances the gameplay-observed transform; one post-gameplay headless
  render owner publishes visibility/magnifier history and offline DeadUp position for the Match.
- **Canonical state:** source `CmSubject` records; the main `CameraTransformState`; the offline-only
  DeadUp transform copy; stage camera translation; bounded camera-effect lifetimes; fighter
  `x221F_b0`; and previous-render magnifier bits.
- **Consumers:** standard camera tracking and viewer output; vanilla/offline magnifier damage;
  Slippi BrawlOffscreenDamage; camera-bound death and stage queries; render visibility output; and
  offline DeadUp inverse-view position publication.
- **Displaced state/code:** fighter model-shift calculation used only by fighter/item rendering;
  hosted standard-mode zoom branches unreachable with `gm_8016B41C == 0`; their dead correction;
  presentation-only quake-frame copies; and repeated construction of an identical view per fighter.
- **Deletion boundary:** hosted execution directly owns the standard camera and performs one
  per-Match visibility publication. No HSD CObj/draw graph, runtime flag, fallback, or parallel
  camera state remains.
- **Explicit exclusions:** source subject tracking, stage-induced camera translation, camera-bound
  gameplay queries, effect/RNG producer ordering, offline DeadUp semantics, and nonstandard camera
  modes outside the declared RL match domain.

## Chronological log

- **2026-07-17 23:20 PDT — packet opened.** Scope: queue item 3 across `runtime/camera.c`, the hosted
  standard branch in `cm/camera.c`, the fighter camera callback, and `runtime/scalar.c`'s render
  publication. Hypothesis: deleting presentation/general-mode work and sharing the exact view
  across fighters should recover a bounded part of the 4.5% owner. Evidence: unchanged-schedule
  Callgrind attributes roughly 3% of instructions to the global standard camera, 0.9% to fighter
  callbacks, and 0.9% to visibility; `Camera_8002A28C` alone is 1.24M/270M instructions.
  Disposition: **open**. Next: direct final cut and adjacent A/B.
- **2026-07-17 23:32 PDT — retained.** Scope: complete final owner cut plus production gates.
  Result: one Match-wide view replaces repeated projections; renderer-only model shift and quake
  copies are gone; dead hosted zoom work is gone; gameplay camera/effect state remains source-owned.
  Two adjacent pairs average 83,916 to 85,156 FPS at 512 (+1.48%) and 63,137 to 63,315 at 256
  (+0.28%) with exact digests. Complete replay, native API/save-restore/allocation/restore, PPC,
  Wasm, and viewer gates pass. Disposition: **retained** and ready for commit. Next: queue items 4–5,
  treated as one packet if separating representation from traversal would create a bridge.

Detailed ignored evidence: `reports/triage/perf_candidates/packet3_headless_camera.md`.
