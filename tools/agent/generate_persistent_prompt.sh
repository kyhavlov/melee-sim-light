#!/usr/bin/env bash
set -euo pipefail

# Generate a ready-to-send autonomous agent prompt with baselines pulled from
# current validation reports.
#
# Usage:
#   tools/agent/generate_persistent_prompt.sh
#   tools/agent/generate_persistent_prompt.sh --one-step <path> --rollout <path>

ONE_STEP_REPORT="reports/validation/one_step_suite_eval.txt"
ROLLOUT_REPORT="reports/validation/rollout_suite_eval.txt"
MIN_KEPT_SLICES=3
MIN_SUBSYSTEMS=3
MIN_DISCRETE_DROP=5
MIN_FLOAT_P95_DROP=0.000010
MIN_MAX_DROP=0.0100
MIN_SECONDARY_DROP=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --one-step)
      ONE_STEP_REPORT="${2:?missing value for --one-step}"
      shift 2
      ;;
    --rollout)
      ROLLOUT_REPORT="${2:?missing value for --rollout}"
      shift 2
      ;;
    --min-kept-slices)
      MIN_KEPT_SLICES="${2:?missing value for --min-kept-slices}"
      shift 2
      ;;
    --min-subsystems)
      MIN_SUBSYSTEMS="${2:?missing value for --min-subsystems}"
      shift 2
      ;;
    --min-discrete-drop)
      MIN_DISCRETE_DROP="${2:?missing value for --min-discrete-drop}"
      shift 2
      ;;
    --min-float-p95-drop)
      MIN_FLOAT_P95_DROP="${2:?missing value for --min-float-p95-drop}"
      shift 2
      ;;
    --min-max-drop)
      MIN_MAX_DROP="${2:?missing value for --min-max-drop}"
      shift 2
      ;;
    --min-secondary-drop)
      MIN_SECONDARY_DROP="${2:?missing value for --min-secondary-drop}"
      shift 2
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "$ONE_STEP_REPORT" ]]; then
  echo "missing one-step report: $ONE_STEP_REPORT" >&2
  exit 1
fi
if [[ ! -f "$ROLLOUT_REPORT" ]]; then
  echo "missing rollout report: $ROLLOUT_REPORT" >&2
  exit 1
fi

baseline_sha=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

if ! [[ "$MIN_KEPT_SLICES" =~ ^[0-9]+$ ]] || ! [[ "$MIN_SUBSYSTEMS" =~ ^[0-9]+$ ]] || \
   ! [[ "$MIN_DISCRETE_DROP" =~ ^[0-9]+$ ]] || ! [[ "$MIN_SECONDARY_DROP" =~ ^[0-9]+$ ]]; then
  echo "--min-kept-slices/--min-subsystems/--min-discrete-drop/--min-secondary-drop must be non-negative integers" >&2
  exit 2
fi
if (( MIN_KEPT_SLICES <= 0 || MIN_SUBSYSTEMS <= 0 || MIN_DISCRETE_DROP <= 0 || MIN_SECONDARY_DROP <= 0 )); then
  echo "--min-kept-slices/--min-subsystems/--min-discrete-drop/--min-secondary-drop must be > 0" >&2
  exit 2
fi

if ! [[ "$MIN_FLOAT_P95_DROP" =~ ^[0-9]*\.?[0-9]+$ ]] || ! [[ "$MIN_MAX_DROP" =~ ^[0-9]*\.?[0-9]+$ ]]; then
  echo "--min-float-p95-drop/--min-max-drop must be numeric" >&2
  exit 2
fi
if awk "BEGIN {exit !($MIN_FLOAT_P95_DROP > 0)}"; then :; else
  echo "--min-float-p95-drop must be > 0" >&2
  exit 2
fi
if awk "BEGIN {exit !($MIN_MAX_DROP > 0)}"; then :; else
  echo "--min-max-drop must be > 0" >&2
  exit 2
fi

discrete=$(
  awk '/^overall\.discrete_mismatch:/ {v=$2} END {print v}' "$ONE_STEP_REPORT"
)
float_p95=$(
  awk '/^overall\.float_norm_mae_p95:/ {v=$2} END {print v}' "$ONE_STEP_REPORT"
)
hitlag=$(
  awk '/^mismatch\.hitlag:/ {s+=$2} END {print s+0}' "$ONE_STEP_REPORT"
)
hitstun=$(
  awk '/^mismatch\.hitstun:/ {s+=$2} END {print s+0}' "$ONE_STEP_REPORT"
)
rollout_seeded=$(
  awk '/^overall\.rollout\.first_mismatch_seeded_total:/ {v=$2} END {print v}' "$ROLLOUT_REPORT"
)
posx_max=$(
  awk '
    /^err\.pos_x:/ {
      for (i=1; i<=NF; i++) {
        if ($i ~ /^max=/) {
          split($i, a, "=");
          if (a[2] + 0 > m) m = a[2] + 0;
        }
      }
    }
    END { printf "%.6f\n", m + 0 }
  ' "$ONE_STEP_REPORT"
)
posy_max=$(
  awk '
    /^err\.pos_y:/ {
      for (i=1; i<=NF; i++) {
        if ($i ~ /^max=/) {
          split($i, a, "=");
          if (a[2] + 0 > m) m = a[2] + 0;
        }
      }
    }
    END { printf "%.6f\n", m + 0 }
  ' "$ONE_STEP_REPORT"
)

cat <<EOF
Work from current \`main\` tip in autonomous persistent mode.

Important workflow:
- DO NOT COMMIT in this run.
- Keep iterating until you have a review-ready uncommitted diff that is usable.
- Do not keep failed attempts in the final diff; continue iterating instead of stopping on first failure.
- When a lane is exhausted/regressive, pivot to the next ranked lane in the same run.
- Do not return with an empty diff.
- Keep working until you have a non-empty, landable multi-fix diff that satisfies this prompt.
- This is campaign mode: do not return early after one small win; build a substantial package.

Pinned baseline for this run:
- baseline git SHA: ${baseline_sha}
- overall.discrete_mismatch: ${discrete}
- mismatch.hitlag: ${hitlag}
- mismatch.hitstun: ${hitstun}
- overall.rollout.first_mismatch_seeded_total: ${rollout_seeded}
- overall.float_norm_mae_p95: ${float_p95}
- err.pos_x max: ${posx_max}
- err.pos_y max: ${posy_max}

Campaign success rule (final combined diff vs pinned baseline):
- Keep full guardrails green.
- Keep at least ${MIN_KEPT_SLICES} gameplay slices in the final diff across >= ${MIN_SUBSYSTEMS} distinct gameplay subsystems.
- Metric requirements:
  1) overall.discrete_mismatch must decrease by >= ${MIN_DISCRETE_DROP}, AND
  2) float headline must improve by a meaningful amount:
     - overall.float_norm_mae_p95 decreases by >= ${MIN_FLOAT_P95_DROP}, OR
     - err.pos_x max decreases by >= ${MIN_MAX_DROP}, OR
     - err.pos_y max decreases by >= ${MIN_MAX_DROP}, AND
  3) at least one secondary headline must improve by >= ${MIN_SECONDARY_DROP}:
     - mismatch.hitlag decreases, OR
     - mismatch.hitstun decreases, OR
     - overall.rollout.first_mismatch_seeded_total decreases.
- Metrics not directly targeted by a kept slice must be non-increasing (tiny numeric noise only):
  - float_p95 noise tolerance: <= 0.000001 increase
  - err.pos_x/err.pos_y max noise tolerance: <= 0.0001 increase
- Final deliverable requirement: non-empty landable campaign package only.
- Do not return a blocker-only report. Keep iterating and pivoting until campaign success rule is met.

Priority order:
- A-class first: changes likely to reduce discrete AND (float p95 or float max).
- B-class next: changes likely to reduce discrete only.
- C-class next: changes likely to reduce float (p95 or max) only.
- If a lane fails, immediately pivot to the next lane and continue.
- Pivot rule:
  - after 2 failed/regressive attempts in one lane, move to a different lane.
  - do not end run until campaign success rule is met.

Hard constraints:
- C gameplay logic only (no gameplay logic in Python)
- decomp/ASM/ISO/data citations near changed gameplay branches/constants
- no replay-fit heuristics in C
- never implement replay-driven heuristic gameplay behavior in C
- no xfail
- no hand-editing reports
- do not refresh guardrail baseline fixtures during normal iteration
- do not edit prompt/tooling files under tools/agent/* unless explicitly requested
- no new C-side seed-bridge/snapshot-repair behavior in src/* (seed bridges belong in Python tooling unless explicitly requested)

Bridge/heuristic ablation rule:
- For any gameplay change that includes micro-bridge behavior (epsilon nudges, snapshot-only branches, no-submotion handoff logic, carry-share tuning, or new C-only fallback lanes), run A/B/C ablation before finalizing:
  - A: full candidate
  - B: candidate without the new micro-bridge subpiece
  - C: both new bridge pieces removed (or nearest neutral baseline)
- Prefer the narrowest variant that preserves meaningful gains on core metrics.
- If A and B are effectively tied on core suite metrics, choose B.

Lock-first rules:
- add strict replay-real locks for targeted rows (exact parity or tight epsilon)
- add adjacent context controls (stability/shape only)
- no “improved-to-threshold” lock assertions

Test hygiene rules (integration tests):
- use pytest.importorskip("msl_binding")
- use required-artifact skip helper
- always destroy handles in finally

Prohibited content in final diff:
- Edits under tests/fixtures/guardrails/current_main/* (except when the task explicitly requests baseline refresh).
- Baseline-fixture edits used to make preflight pass.
- Edits to tests/fixtures/hard_row_lock_pack_seedref.json that are not tied to the final kept gameplay subset with explicit row-level/decomp justification.
- C gameplay formulas that are replay-fit/approximation without decomp/data ownership (e.g., ad-hoc trigonometric mixes, unexplained epsilon nudges, fitted blend weights).
- Runtime branches keyed on replay-snapshot ambiguity as shortcuts (e.g., no-submotion snapshot suppression, last_attack_landed fallback for gameplay ownership, seed-only carry suppression in src/*).
- Unexpected cross-layer API/wrapper churn not required by the kept slice.
- Report-only diffs (validation files changed with no gameplay/test changes), unless explicitly requested.

Required gates on final diff:
- make test
- make validate OUT=reports/validation/one_step_suite_eval.txt
- make validate-rollout OUT=reports/validation/rollout_suite_eval.txt
- make guardrail-preflight
- seed==ref new=0 for action_id/hitlag/hitstun/state_flags/instance_id/on_ground/ground_id
- float top-key drift: pos_x new=0 gone=0, pos_y new=0 gone=0
- mismatch.hitlag/hitstun non-increasing
- rollout seeded non-increasing

Diff hygiene:
- Keep the final diff minimal and on-slice.
- Do not keep unexpected files (e.g., wrapper/API/tooling not required by the slice) in the final diff; justify only if explicitly required by the kept slice.
- Before returning final results, ensure disallowed paths have no staged/tracked edits:
  - tests/fixtures/guardrails/current_main/*
  - reports/triage/* (untracked artifacts are fine; never stage them)

Return only when done, with:
1) git diff --name-only
2) before/after table vs baseline:
   - overall.discrete_mismatch
   - mismatch.hitlag
   - mismatch.hitstun
   - overall.rollout.first_mismatch_seeded_total
   - overall.float_norm_mae_p95
   - err.pos_x max
   - err.pos_y max
3) top-20 key overlap/new/gone for pos_x and pos_y
4) targeted row outcomes (before -> after)
5) guardrail checklist pass/fail
6) class (A/B/C) for each slice in the final diff + rationale
6b) per changed gameplay file: exact decomp/data anchors for each kept branch/constant (file:line + reference)
7) if bridge/heuristic ablation rule was triggered: include A/B/C table + chosen variant rationale
8) fix ledger for all kept slices:
   - row/family id
   - before -> after targeted lanes
   - lock test reference added/updated
9) subsystem coverage summary (which ${MIN_SUBSYSTEMS}+ subsystems are touched and why)
10) git status --porcelain -b
EOF
