# Stage-clip deterministic sweep — final matrix artifact (2026-06-11)

## Exact command

```
nice uv run python -m tools.eval.fuzz_live_clip --mode sweep --matrix \
  --save-violations reports/marth_port/clip_sweep_final_violations.json \
  > reports/marth_port/clip_sweep_final.log 2>&1
```

- Raw output: `reports/marth_port/clip_sweep_final.log`
- Violation records (case tuples + per-frame evidence): `reports/marth_port/clip_sweep_final_violations.json`
- **Exit code: 1 (nonzero).** The command exits nonzero because the 3 residual
  violations below remain; a fully clean matrix exits 0. This is intentional —
  the residual is pinned, not suppressed.

## Counts by char / stage / family

3 chars (fox, falco, marth) x 6 stages (fd, bf, dl, ys, fod, ps) x 3 families
(ledgedash, fall-into-stage, boundary-approach) = 54 cells. Every cell is
**0 violations** except:

| cell                  | violations |
|-----------------------|------------|
| marth / ys / ledgedash | 2 |
| falco / ys / ledgedash | 1 |
| all other 52 cells     | 0 |

**TOTAL: 3 violations** (burn-down across the session: 370 -> 68 -> 26 -> 14 -> 3).

## The 3 residual cases — one scenario

All three are the same ledgedash tuple `(side, hang=4, dj_delay=1,
dodge_af_delay=13, angle=(127, 0))` — a pure-horizontal airdodge from ledge
release on Yoshi's Story, mirrored across sides/chars:

```
VIOLATION ledgedash marth/ys case=(1, 4, 1, 13, 127, 0)  [('hull-interior', 59, 5.81, -71.24, 35)]
VIOLATION ledgedash marth/ys case=(-1, 4, 1, 13, 127, 0) [('hull-interior', 58, -4.91, -69.17, 35)]
VIOLATION ledgedash falco/ys case=(1, 4, 1, 13, 127, 0)  [('hull-interior', 58, 7.69, -92.9, 0)]
```

(Fox's tuple stays clean because his dodge displacement differs.)

The horizontal dodge transits at world y = -8, under Yoshi's sloped upper strip;
the collision diamond legally passes above every fighter_solid segment (only the
skeletal root is inside the hull silhouette), and the post-dodge FallSpecial
descends into the keel interior. Under-lip entries on Yoshi's are a known Melee
phenomenon, so this may be vanilla-faithful.

## Claim scope

The deterministic sweep is clean **except** this pinned Yoshi
horizontal-airdodge residual, whose vanilla status is unknown until
Dolphin-probed. The probe recipe (setup, inputs, classification criteria) is in
`MANUAL_REPRO_CATALOG.md` under "DOLPHIN PROBE RECIPE".

## Gate battery at this state (all rerun after the final diff)

- `make build BUILD_FORCE=1` — exit 0
- `make test` (pytest -n 4): **3280 passed, 1183 skipped** (includes the new
  `test_fall_carried_same_ledge_floor_in_span_relands` regression lock and an
  order-dependence hygiene fix in `tests/test_melee_sim_api.py`)
- `make validate-all` — exit 0, no hard reds
- `validation_report_diff --before HEAD --after reports/validation --top 220` —
  no suite total changes; no hard/distribution reds; only two float-noise p95
  notes (DistinctCaringCobra +5e-8, PutridJoyousOryx +1e-8)
- `make fmt-check` — exit 0
- `git diff --check && git diff --cached --check` — clean
