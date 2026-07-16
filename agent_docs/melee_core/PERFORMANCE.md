# Melee core performance worklog

This is the durable evidence log for the maximum-throughput program in `README.md`. Benchmark
results are accepted only when they use the production observation/terminal contract, retain the
same digest for the same workload, and pass the complete correctness gates without new or widened
classifications.

## Phase 7 baseline — 2026-07-16

### Workload and commands

`tools/melee_core/prepare_replay_benchmark.py` decodes the complete aggregate suite once through
Peppi, then asks the existing native Arrow preprocessor to write a packed match config and
controller-input tape. Cached cases are under ignored `build/melee_core/benchmark/`; checking a
warm cache takes about 0.7 seconds. The current corpus is 153 cases and 1,415,629 input frames.
Python and Arrow are absent from the timed workload.

`tests/melee_core/replay_bench.c` mmaps every tape before timing, cycles them over logical
environments, runs ordinary free-running gameplay, and writes `MslCoreObservation` plus
`MslCoreTerminal` into a caller-owned 128-frame ring. It does not use validation comparison,
per-frame replay RNG authority, stage-event teacher forcing, or viewer output. One resident match
per shard starts from a real near-end savestate, so the timed region includes ordinary replay-end
masked reset and restart. A second pass reports step-only throughput. The observation/terminal ring
is hashed after timing.

Build or refresh the cache:

```bash
make -f src/melee_core/Makefile benchmark-prepare
```

Run one bounded baseline at a time; each target pins one physical core and completes in less than
ten seconds on the baseline host:

```bash
make -f src/melee_core/Makefile benchmark-9950x3d-frequency-256
make -f src/melee_core/Makefile benchmark-9950x3d-frequency-512
make -f src/melee_core/Makefile benchmark-9950x3d-vcache-256
make -f src/melee_core/Makefile benchmark-9950x3d-vcache-512
```

The default timed sample is 32,768 complete logical environment-frames with eight warmup ticks.
The ring writes 996 bytes/environment-frame: a 980-byte observation and 16-byte terminal result.
At history 128 this is 127,488 bytes/environment, 31.12 MiB for 256, and 62.25 MiB for 512.

### Host and release profile

- AMD Ryzen 9 9950X3D, Linux 6.17.0-14-generic, SMT siblings excluded.
- CPU 0 is a physical core in the 96 MiB V-cache L3 domain; CPU 8 is a physical core in the
  32 MiB frequency L3 domain.
- `amd-pstate-epp` with the `performance` governor on both measured cores.
- GCC 13.3.0. Source-shaped gameplay TUs retain the bit-exact reference flags; audited host/API,
  observation, relocation, savestate, wire, generated relocation, and benchmark TUs use `-O3
  -march=native -mtune=native`. Section garbage collection remains enabled. Unsafe floating-point
  flags are rejected by `native-release`.

The accepted measurements are:

| Core/domain | Logical envs | Complete FPS | Step-only FPS | Resets | Digest |
|---|---:|---:|---:|---:|---|
| CPU 8 / 32 MiB frequency | 256 | 16,722 | 16,568 | 16 | `7f31cdd8104cfc62` |
| CPU 8 / 32 MiB frequency | 512 | 20,305 | 20,435 | 32 | `cf13aa808327311f` |
| CPU 0 / 96 MiB V-cache | 256 | 16,412 | 16,593 | 16 | `7f31cdd8104cfc62` |
| CPU 0 / 96 MiB V-cache | 512 | 20,202 | 20,304 | 32 | `cf13aa808327311f` |

CPU 8 is the current official single-core target. Production and step-only differences are within
roughly one percent at this sample duration; observation/terminal packing is not the dominant
current cost.

These are explicitly **sharded logical-batch measurements**, not resident public batches. Each
`MslCoreMatch` currently reserves a 32 MiB `MAP_32BIT` arena, so the low 2 GiB mapping window cannot
construct 256 or 512 resident matches. The benchmark reuses 16 resident public matches in explicit
logical shards and reports `mode=sharded`, `logical_matches`, and `resident_matches` separately.
This preserves the replay mix, output-ring traffic, resets, and complete-frame accounting, but does
not claim cache behavior equivalent to the future resident batch. Phase 8 must remove the low-address
and per-match arena constraints before a true 256/512 baseline can replace these numbers.

Hardware performance counters are unavailable on this host because
`/proc/sys/kernel/perf_event_paranoid` is 4 and the session lacks `CAP_PERFMON`; consequently Phase 7
does not invent cycles/environment-frame or cache-miss estimates from nominal clocks. After counter
access is enabled, rerun the same pinned commands under `perf stat` for cycles, instructions,
branches, and cache events and append the results here.

### Compiler candidates

| Candidate | Result | Decision |
|---|---|---|
| Global `-O3 -march=native` plus LTO | Cross-TU decomp type UB caused a native crash; LTO also exposed incompatible declarations | Rejected; no flags retained |
| Global `-O3 -march=native`, no LTO | First trig drift was fixed by blocking builtin trig substitution, but the full suite still had 26 severe/control-flow failures | Rejected; no partial workaround retained |
| Global `-O1` | Crashed on a long aggregate replay | Rejected |
| O3 host allowlist on a changed global base profile | Ten aggregate failures remained | Rejected |
| Reference gameplay profile plus audited O3 host/API allowlist | Full 153-case gate: 63 exact pass, 90 existing classified, zero XPASS/fail/error, 1,415,476 compared frames | Retained baseline |

The conservative allowlist is intentional evidence, not the optimization ceiling. Phase 8 should
close optimizer-sensitive source ownership and type boundaries one bounded owner at a time; each
allowlist expansion needs the full output-lock gate.

### Runtime and reachability census

Run:

```bash
make -f src/melee_core/Makefile runtime-census
```

The census initializes all eight currently admitted characters over all six supported legal stages,
then runs a Peach/FD item scenario. Its high-water and allocation-lock results are:

- `MslCoreMatch`: 781,768 bytes; embedded relocation registry: 524,296 bytes; embedded memory
  allocation ledger: 196,648 bytes; source match state: 34,808 bytes.
- Per-match arena: 32 MiB reserved, 17,122,944 bytes maximum touched, 3,090 initial allocations,
  and 11,435 relocation records in the measured maximum configuration.
- A representative active match used 16,565,472 arena bytes and 2,321 allocations both before and
  after 306 gameplay steps, confirming no gameplay-path arena growth.
- Representative savestate: 17,347,368 bytes = 128-byte header + 781,768-byte match value +
  16,565,472 touched arena bytes.
- Shared `GameData`: 568,296-byte value, 64 MiB archive arena with 42,117,736 bytes used, and a
  256 MiB native-DAT address arena with 51,030,048 bytes used. These are shared rather than copied
  into each match.
- Representative scheduler: 15 live/peak GObjs and 53 live/peak GObjProcs. The general object
  pools still preallocate 256 or more entries per type; reached fighter and item pools contain two
  fighters and one Peach article in this scenario.
- At the measured high-water, the current struct plus arena costs about 4.27 GiB touched / 8.19 GiB
  reserved for 256 matches and 8.54 GiB touched / 16.37 GiB reserved for 512, before observation
  history. The 32-bit mapping window fails first.

The release build compiles 349 objects (318 source-shaped gameplay C files plus local/generated
runtime code). Section garbage collection reduces the replay benchmark to about 1.82 MiB text,
99 KiB initialized data, and 11 KiB BSS, but still retains roughly 6,895 text and 1,710 data symbols.
Large retained owners include fighter motion-state tables, item tables, fighter collision/update,
camera, stage, collision, and common animation code. Compilation alone is not treated as runtime
cost.

The reached scheduler callback set includes the source fighter phase chain, item phase chain,
`Fighter_ProcessHit_8006D1EC`, `mpLib_800587FC`, the camera callback, ten
`headless_ground_anim_proc` instances on FD, and FD background rotation. The scalar step also always
builds the 1,022-byte forensic `MslCoreCompare`, walks visible fighter/item JObj trees to publish lazy
render matrices, and publishes camera visibility. Some matrix publication is observed by later
hit/hurt capsules and cannot simply be deleted; the forensic row and genuinely presentation-only
scheduled work are prime Phase 8 reachability/cost boundaries.

### Phase 8 boundary implied by the evidence

1. Remove the 32-bit pointer/address assumption and replace the 32 MiB arena with bounded measured
   storage. Compact or externalize the 524 KiB relocation registry and 197 KiB allocation ledger;
   size fixed pools from observed/source-backed maxima rather than uniform reserves.
2. Stop producing the forensic compare row on the production observation path. Keep validation as
   an explicit projection and make the compact observation read canonical state directly.
3. Separate gameplay-observed lazy transform publication from DObj/GX/presentation traversal, then
   remove scheduled callbacks proven irrelevant to headless gameplay. Preserve source ordering and
   every transform actually consumed by collision/hit/hurt logic.
4. Once hundreds of matches are genuinely resident, replace this sharded baseline with resident
   256/512 measurements, add 4,096/16,384 memory gates, and profile the canonical batch by source
   scheduler phase before choosing SoA/AoSoA fields.

