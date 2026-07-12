# Packet 1: Common Grounded Map Callbacks

Status: complete; committed as `452a143f`.

Baseline: `a293980533bd` (`Improve and benchmark simulator performance`).

## Scope delivered

Packet 1 cuts seven common grounded handler families over atomically for all six supported
characters. Nineteen exact extracted Coll callback identities cover 21 common action rows per
character, or 126 generated MotionState rows. The action inventory and remaining packets are in
[../callback-ledger.md](../callback-ledger.md).

The runtime path is now:

```text
MSLMSO01 exact Coll symbol -> stable live handler kind
  -> persistent CollData ECB/root/surface packet
  -> source-oriented mpLib line reference and query
  -> ordered source-grounded mpColl wrapper
  -> immediate Fall/Ottotto/StopWall motion entry
```

The migrated live identity skips the old generic ground, wall/ceiling, and locomotion collision
passes. There is no benchmark-only path and no character-ID branch in the shared owner.

## Main changes

- MSLMSO01 v24 adds `coll_handler_kind` to the complete extraction/export/read contract. Stable
  kinds are generated from exact decomp callback symbols rather than action-family inference.
- Motion entry installs a live Coll callback ID and handler kind. Explicit source overrides have a
  central write surface, and a callback-entered destination is not recursively dispatched in the
  same collision phase.
- `mpcoll_source_ground.c` ports the bounded grounded wrapper shape: persistent ECB load and
  interpolation, wall/ceiling ordering, floor projection, edge modes, disconnected-floor retry,
  moving-surface carry, environment publication, and immediate collision-owned transitions.
- `mp_lib.c` and the stage segment reference API give collision one source-oriented floor/segment
  view instead of adding another normalized graph or lookup family.
- Same-proc overlap nudge ownership now follows the generated frame-start callback owner. This
  replaces the previous Wait-to-Guard special case and correctly covers Ottotto and other migrated
  source owners.
- Dream Land wind is applied at the callback phase owned by the frame-start handler: before the new
  source callback for migrated owners and at the retained legacy point for the rest.
- One-step replay validation reconstructs only the unobservable persistent ECB packet from the
  live destination pose. Free-running rollout always consumes the retained runtime CollData packet.
- Displaced post-collision locomotion repairs and an invalid restored-state test were deleted;
  source-valid fixtures now initialize the real collision state they exercise.

## Validation result

`make validate-all` and `validation_report_diff --fail-on-regression` pass with no replay-level hard
red.

| Gate | Baseline | Packet 1 | Delta |
|---|---:|---:|---:|
| Aggregate one-step discrete mismatches | 9,948 | 9,918 | -30 |
| Aggregate one-step strict discrete mismatches | 14,951 | 14,921 | -30 |
| Aggregate rollout first mismatches | 1,674 | 1,667 | -7 |
| Aggregate rollout seeded first mismatches | 883 | 879 | -4 |
| Doubles one-step discrete mismatches | 8,349 | 8,338 | -11 |
| Doubles rollout first mismatches | 1,258 | 1,253 | -5 |

The largest rollout gains are owner-shaped rather than isolated rows: MixedAllQuetzal's best streak
grows 1,134 to 1,609, ToughOutlyingChicken grows 1,874 to 2,007, and several Falcon, Marth, Sheik,
and doubles replays lose first breaks. No replay's tracked mismatch count increases.

## Performance result

Same-machine alternating A/B runs of the 5,000-frame, batch-256 mixed-character native benchmark:

| Run | Baseline FPS | Packet 1 FPS |
|---:|---:|---:|
| 1 | 423,907 | 423,078 |
| 2 | 424,050 | 423,321 |
| 3 | 421,133 | 421,635 |

The medians are 423,907 and 423,321 FPS, a -0.14% difference. The fixed five-replay timing sample
measured 322,520 FPS on the baseline and 333,562 FPS on final Packet 1 (+3.4%). Treat both as performance
parity with a favorable replay result; neither shows a new pathological tail.

After moving one-step hidden-state reconstruction entirely out of collision dispatch, a final
three-run check measured 429,255 / 427,362 / 425,153 FPS (median 427,362) with p99/average between
1.15x and 1.28x.

## Size result

Packet 1 is infrastructure-heavy. Existing tracked `src/` changes delete 482 lines and add 347;
the six new source files add 795 lines, for a net runtime-source increase of about 660 lines. This
is expected for the first cutover: it introduces the stable live dispatch and direct path while the
legacy path must remain for Packets 2-5. It is not the program's expected steady state.

The deletion forecast remains concentrated in later packets. Packet 2 should begin deleting the
large generic air/floor-probe matrix; Packet 5 removes the residual legacy dispatcher and dead
state. The Phase 1 target remains a substantial net reduction, but correctness and single ownership
remain the gate rather than a LOC quota.

## Boundary left for Packet 2

Packet 1 intentionally does not claim common airborne, landing, AttackAir/EscapeAir, Damage,
passive/tech, CliffCatch, or special/capture identities. A migrated grounded callback can enter
Fall, but the destination's common-air collision callback remains legacy until Packet 2. This is
the next clean owner boundary, not a second grounded implementation.
