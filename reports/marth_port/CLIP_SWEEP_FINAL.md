# Stage-clip deterministic sweep — final matrix artifact (2026-06-11, post-fix)

## Exact command

```
nice uv run python -m tools.eval.fuzz_live_clip --mode sweep --matrix \
  --save-violations reports/marth_port/clip_sweep_final_violations.json \
  > reports/marth_port/clip_sweep_final.log 2>&1
```

- Raw output: `reports/marth_port/clip_sweep_final.log`
- Violation records: `reports/marth_port/clip_sweep_final_violations.json` (`[]`)
- **Exit code: 0.** The matrix is fully clean.

## Counts by char / stage / family

3 chars (fox, falco, marth) x 6 stages (fd, bf, dl, ys, fod, ps) x 3 families
(ledgedash, fall-into-stage, boundary-approach) = 54 cells.
**Every cell: 0 violations. TOTAL: 0.**

Burn-down across the effort: 370 -> 68 -> 26 -> 14 -> 3 -> **0**.

## The final scenario (was: 3 residual cases) — engine bug, fixed

The last 3 violations were one scenario: Yoshi's Story ledgedash tuple
`(side, hang=4, dj_delay=1, dodge_af_delay=13, angle=(127, 0))` — a pure-horizontal
airdodge from ledge release that transited under the lip, descended into the keel,
and **lost a stock from inside the stage** (verified in a live rollout: ~75-frame
interior fall, death at y≈−91).

Root cause (no "maybe vanilla" — this was an MSL engine bug): the dodge's pose ECB
bottom crosses the sloped ledge floor line (segment 6) mid-transit, but rises by
0.002 units of animation jitter on the crossing frame. Two stacked non-source
exclusions dropped the landing:

1. the EscapeAir hard-floor producer required `cur_bottom_y <= prev_bottom_y`;
   decomp `mpCheckFloor` applies its `ay >= by` descent gate only in the
   horizontal-line branch — the sloped-line branch uses `mpLineIntersection`
   with no direction gate (only the 0.1 half-space slop);
2. `msl_mpcheck_hard_floor` excludes `is_ledge` floor lines; `mpCheckFloor` has
   no ledge filter — a ledge-grabbable strip is an ordinary landable floor.

Fix (`src/mpcoll_ground.c` + shared `msl_mplib_line_intersection`): a sibling
EscapeAir ledge-strip hard-floor producer covering exactly the excluded family,
swept with the faithful per-branch source gates; acceptance mirrors the direct
ft_CheckGroundAndLedge producer (dd90 projection, else contact snap per
`mpColl_80044628_Floor`). Post-fix behavior: waveland onto the lip slope at the
crossing frame — the known real-Melee ledgedash outcome. Full detail in
`MANUAL_REPRO_CATALOG.md`.

Regression lock: `test_ys_ledgedash_horizontal_airdodge_wavelands_on_lip_slope`
(both sides; verified to fail on the pre-fix engine).

## Gate battery at this state

- `make build BUILD_FORCE=1` — exit 0
- `make test` — full suite green (includes the new YS lock and
  `test_fall_carried_same_ledge_floor_in_span_relands`)
- `make validate-all` — exit 0, no hard reds
- `validation_report_diff --before HEAD --after reports/validation --top 220` —
  no suite total changes; no hard/distribution reds; only the two pre-existing
  float-noise p95 notes (DistinctCaringCobra +5e-8, PutridJoyousOryx +1e-8)
- `make fmt-check` — exit 0
- `git diff --check && git diff --cached --check` — clean
- Full sweep matrix — **TOTAL: 0 violations, exit 0**

Scope note: "clean" means the three modelled clip families over the deterministic
grids on all six stages for fox/falco/marth. Random soak mode remains
lead-generation, not proof.
