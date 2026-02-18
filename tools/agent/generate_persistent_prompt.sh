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
- Revert failed attempts immediately and continue; do not stop on first failure.

Pinned baseline for this run:
- baseline git SHA: ${baseline_sha}
- overall.discrete_mismatch: ${discrete}
- mismatch.hitlag: ${hitlag}
- mismatch.hitstun: ${hitstun}
- overall.rollout.first_mismatch_seeded_total: ${rollout_seeded}
- overall.float_norm_mae_p95: ${float_p95}
- err.pos_x max: ${posx_max}
- err.pos_y max: ${posy_max}

Mandatory success criteria for final uncommitted diff:
1) overall.discrete_mismatch decreases
2) overall.float_norm_mae_p95 decreases
3) at least one of err.pos_x max or err.pos_y max decreases
4) You may satisfy (1)-(3) via one slice or multiple slices, but final combined diff must satisfy all.

Priority order:
- A-class first (likely dual-impact: discrete + float)
- then B-class only if still compatible with mandatory success criteria
- then C-class only if still compatible with mandatory success criteria

Hard constraints:
- C gameplay logic only (no gameplay logic in Python)
- decomp/ASM/ISO/data citations near changed gameplay branches/constants
- no replay-fit heuristics in C
- no xfail
- no hand-editing reports

Bridge/heuristic ablation rule (mandatory when applicable):
- If a gameplay change introduces any micro-bridge behavior (epsilon nudges, snapshot-only branches, no-submotion handoff logic, carry-share tuning, or new C-only fallback lanes), run A/B/C ablation before finalizing:
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
- If unexpected files are touched (e.g., wrapper/API/tooling not required by the slice), either revert them or explicitly justify them in the report.

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
7) if bridge/heuristic ablation rule was triggered: include A/B/C table + chosen variant rationale
8) git status --porcelain -b
EOF
