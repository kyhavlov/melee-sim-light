# Active performance packet — supported dynamics-pool capacity

## Objective

Replace the retail 320-node per-Match fighter-dynamics pool with the exact fixed capacity required by
supported hosted construction and gameplay. This is a final capacity owner, not a solver rewrite;
retain only if complete construction/gameplay gates prove the bound and throughput does not regress.

## Final boundary

- **Final owner:** the hosted Match owns one fixed-capacity `DynamicsData` pool sized for the maximum
  simultaneously live supported chains plus one fighter's transient pre-prune construction chains.
- **Canonical state:** the existing source `DynamicsDesc`/`DynamicsData` nodes and free list remain
  singular. Solver order, node layout, JObj ownership, save/restore, and gameplay callbacks do not
  change.
- **Consumers:** supported Fox/Falco/Marth/Falcon/Sheik/Zelda/Puff/Peach singles and doubles,
  including four-player Peach construction and Sheik/Zelda secondary entities.
- **Displaced state:** unused retail capacity above the source-backed hosted high-water.
- **Deletion boundary:** native/Wasm hosted builds allocate and relocate only the supported capacity;
  PPC keeps the retail 0x140 layout. No dynamic growth, fallback allocation, overflow list, replay
  key, or post-initialization allocation is permitted.

## Source evidence

- Retail `lb_8000FCDC/lb_8000FA94` allocates and links 0x140 nodes for the complete game roster and
  presentation chains.
- Hosted `Fighter_Create` constructs one fighter at a time and immediately calls
  `ftCo_HeadlessPruneDynamics`; rejected chains return to the same free list before the next fighter.
- The complete supported-data census reaches only one four-node Fox gameplay chain and one three-node
  Puff chain per fighter after pruning. The largest transient source construction is Peach's nine
  four/five-node presentation chains; a 64-node pool covers that construction plus prior live chains.

## Sequence

1. Parameterize the pool entry count by hosted/source target and make initialization, relocation, and
   layout assertions use the same owner constant.
2. Run the maximum runtime census (four Peach plus all supported configs), native smoke, both
   production digests, and the complete 153-replay gate. Any exhaustion rejects the proposed bound;
   do not add dynamic overflow.
3. Measure arena/savestate and 512/256 throughput. Retain a substantial per-environment capacity cut
   only if throughput is neutral or better and all output locks remain exact.
4. Run native API/copy/save-restore/allocation, PPC, Wasm/viewer, source, and formatting gates;
   update `PERFORMANCE.md`; send Discord evidence; atomically commit code+evidence.

## Baseline

- Runtime: `fe25fec5`
- 256: 74,466 FPS, digest `8ef126a41244d514`
- 512: 70,468 FPS, digest `6f91f23e3553a090`
- Ordinary arena/savestate: 676,440 / 738,056 bytes.
- Current native dynamics pool: 320 x 168 bytes = 53,760 bytes per Match.

## Log

- 2026-07-19 — `open`
  Scope: fixed hosted dynamics pool capacity only.
  Hypothesis: 64 nodes cover the supported construction high-water and remove 43,008 bytes from
  every environment without touching the exact solver.
  Evidence: source construction is sequential with immediate hosted pruning; supported live chains
  are at most four nodes per fighter, while the largest transient Peach construction is below 48
  nodes.
  Disposition: implement the single capacity constant and prove it with maximum construction plus
  full replay/save-restore/Wasm gates.
  Next: cut 320 to 64 in hosted builds and run the maximum census.
- 2026-07-19 — `retained`
  Scope: 64-node native/Wasm fighter-dynamics pool with hard exhaustion enforcement; PPC retains
  the retail 320-node layout.
  Hypothesis: the supported construction and live-chain high-water fits the fixed bound, deleting
  unused replicated capacity without adding a hot-path cost.
  Evidence: every supported maximum construction passes with the exhaustion assertion active. The
  ordinary arena/savestate falls 676,440/738,056 to 633,432/695,048 bytes (-43,008 each), and the
  four-player maximum falls 1,004,908 to 961,900 bytes. Three valid adjacent 512 control/candidate
  pairs are 69,765/70,102, 69,982/69,944, and 70,153/70,185 FPS with exact digest
  `6f91f23e3553a090`; raw medians improve +0.17% and median paired change is +0.05%. The 256 digest
  remains `8ef126a41244d514`. The complete gate is 63 PASS / 90 unchanged CLASSIFIED / zero failures,
  and native allocation/API/copy/save-restore, PPC, Wasm/viewer, source, and formatting gates pass.
  Disposition: retain as a final supported-domain capacity owner with neutral throughput and a
  43,008-byte per-environment reduction.
  Next: commit implementation and evidence atomically, refresh the profile-backed owner queue, and
  start the next bounded final-form packet.
