# Supported-domain correctness completion — 2026-09-08

The current source candidate reaches **503 exact / 26 classified / 0 failures /
0 errors** across all 529 supported replays and 5,059,922 transitions. Native
GNU/Linux debug and strict release output fingerprints agree for every replay.
The 26 exceptions cover missing capture information or historical execution that
contradicts source/retail arithmetic. Every mixed exception has been checked for
an independently fixable field; there is no remaining implementable exception
identified in the current corpus.

The completed stage lifecycles retain a measured 3.74%/4.29% throughput cost at
resident 256/512 after exact query optimizations recovered roughly half the
initial cost. The previously committed packet was performance-neutral within
its measured spread.

The checkpoint requested before this campaign is `5f097b2a`. Its 23 improvements
and performance verification are documented in the earlier closure report. This
campaign adds ten exact replays and removes twelve classification records,
including the last two PPC-only records. Together these campaigns take the
470/59 integration baseline to 503/26. Implementation, generated replay
expectations and retained correctness/performance evidence form one completion
commit.

There are 6,342 mismatching transitions (99.874662% exact) and 11,228 mismatching
fields. Scoring, zero-sign comparison, gameplay masks, captured inputs and the
529-replay inventory are unchanged. This is a completion claim for the admitted
roster, rules and existing corpus, not a proof for every possible game state.

## Completed source owners

| Owner | Final behavior | Newly exact |
| --- | --- | ---: |
| FD `grLast` / Ground / HSD | Source actors exist only during their authored phases; execute animation completion, start callback, fog/color and RNG lifetimes. Delete the manual FD controller and construction branch. | 1 |
| Shared stage DAT translation | Pre-register contiguous animation descriptor arrays so direct part indexing and graph pointers address the same nodes. Ground owns its mutable parameter value; shared DAT parameters stay immutable. | Supports FD closure |
| Dream Land `grOldPupupu` | Execute the complete blink/turn/blow FSM before publishing recorded direction. Delete the replay early return and synthetic force-emitter counter. Whispy's explicit Slippi capability excludes dead fighters before tie RNG. | 2 |
| `Camera_8002A0C0` | Fuse both source depth-dependent pan factors before CObj publication. | 2 |
| `ft_800CB6EC` | Preserve the source fused aerial-turn yaw step and JObj dirty publication. | 3 |
| `ftCo_SpecialS::doEnter` | Round the negative momentum product, then fuse friction scale and prior ground velocity. | 2 |
| MSL compiler ownership | Prevent PPC/Wasm compiler substitution of source sinf/cosf calls with libc sincosf. All seven PPC camera residuals close. | Native unchanged |

FD also removes HumiliatingCreamyReindeer's 152-row item lifetime tail,
leaving its single magnifier row. The camera correction removes Hornet's one
screen-KO position row, leaving its single magnifier row.

All 85 Dream Land captures' embedded hook lists were checked: the Whispy death
vote fix is present in all 78 Dolphin/mainline captures and absent from all seven
Nintendont captures. Public RL/viewer construction selects the current Slippi
rule. The private config grows 58 to 59 bytes; decoder, producers, generated
viewer schema and benchmark v2/cache identity change together. Public API
structures do not change.

The final-representation ledger and instruction addresses are in
`ACTIVE_WORK.md` and `src/upstream_delta_ledger.tsv`. Fresh stage extraction and
native loading pass. There are no duplicate controllers, replay-position
corrections, synthetic counters or diagnostic hooks in production.

## Remaining capture boundaries

| Boundary | Replays | Why it remains |
| --- | ---: | --- |
| Magnifier VI/render phase | 12 | The recording omits the boundary that publishes the damage gate. Most residuals are isolated one-percent ticks. TameEmbellishedSparrow also inherits later knockback/action/death differences from that tick. |
| Historical float/zero execution | 9 | Retail/source arithmetic agrees with hosted output instead of the capture at the independent seeds. The recordings do not specify the original JIT floating-point behavior. |
| Missing raw C-stick before UCF | 2 | PreFrame records the normalized C-stick before cardinal injection; these old captures lack the raw bytes needed to resolve the 79-versus-clamped-80 ambiguity. Both 0.0375 ASDI tails begin on hitlag exit. |
| Unwritten source bytes | 3 | Two Chain preassignment fields and a transform-swap L-cancel byte expose allocator/previous-life contents absent from the capture. Live written fields remain strictly compared. |

These are explicit exceptions, not relaxed tolerances. An original execution
profile or additional raw capture information could make one worth revisiting;
there is no reason to infer missing inputs from the recorded result.

### Mixed-case arbitration

- **Link boomerang 6873:** native and retail `it_80273B50` / `lb_8000B1CC`
  produce X `0xc13bbd02`; the capture contains `0xc13bbd00`. The local attachment
  input and all ten inherited flight rows identify a historical transform seed.
- **Falco/Luigi:** the independent seeds at 2739, 3644, 8142 and 8204 agree with
  retail rather than the recording. At 990, retail and hosted damage-entry Y
  both equal `0x3ff857b8`. Recorded PreFrame Y is `0x3ff857b5`;
  `Playback/Core/RestoreGameFrame.asm` writes it at +0xB4 before physics.
  Comparing only playback's later output would falsely implicate source math.
- **Doubles 5423:** a preceding signed-zero difference feeds DI: +0 selects the
  positive pi branch of atan2; -0 selects the negative branch. A temporary
  equal-input +0 probe removes all 83 nonzero numeric field differences,
  leaving only signed-zero rows. The probe is removed. The tested sqrt variant
  did not explain the residual and was removed as well.
- **Historical Fox laser profiles:** retained native/PPC and retail probes cover
  the muzzle transform inputs and ancestors before publication; both large
  residual recordings favor the hosted source bits at those seeds.

## Verification and evidence

- Full native debug and strict release: all 529 output fingerprints agree.
- Remaining 38-record PPC audit: the same 26 exceptions as native, with identical
  snapshots; twelve records retire. The final PPC gate covers 425,203 transitions
  with zero failures/errors. This is not a full 529-replay PPC claim.
- Direct checkpoint byte audit: twelve replays contain the intended gameplay
  changes above; 71 further changed locks differ only in offscreen flag bit
  0x80. Raw non-gameplay item pointer/residue bytes are canonicalized using the
  existing unchanged output-lock projection.
- Native source, archive, stage lifecycle, gameplay parts and reserve smokes pass.
  The new stage test crosses all 17 FD phases and actor destruction/recreation,
  checks pending Ready callbacks/fog/color pointers, and tests copy/save/restore
  at different match indices under sealed allocation counts. The collision smoke
  also covers distant/overlapping stationary transformed walls, and conservative
  admission of moving and degenerate lines.
- The 256-match lifecycle and 128-frame observation-ring smoke passes: 0.58 GiB
  RSS after reset, 0.61 GiB with the ring, and 0.04 GiB after destruction.
- Wasm/native state and viewer digest parity, live browser and production viewer
  pass. The focused API, replay-cache, runner, suite and schema tests pass (47).
- Controlled performance evidence, exact query recovery and rejected compiler
  experiments are recorded under
  `performance/HISTORY.md#correctness-completion--2026-09-08`.

Ignored reproductions and logs are under
`reports/triage/correctness_done_20260908/`: complete native/release/PPC results,
retail probes, embedded-hook scan, byte audit, extraction/smokes, benchmark tapes,
all timing samples and profiler output. Generated classifications/output locks
are refreshed from those complete measured results, never edited numerically.
