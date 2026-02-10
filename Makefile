.PHONY: build test preprocess validate validate-rollout rollout-capture rollout-summary rollout-diff build_data fmt fmt-check check guardrail-preflight guardrail-preflight-full guardrail-baseline forensic-rows dolphin-engine-dump dolphin-extract dolphin-forensic-row

PY := uv run python
DATASETS_DIR ?= datasets
SUITE ?= replays/suites/fox_falco_fd_ucf084_recent.json
CHUNK ?= 4096
OUT ?=
FIELDS ?= action_id,animation_index,on_ground,hitlag,hitstun,state_flags
ROLLOUT_JSON ?= reports/triage/current_rollout_streaks.json
ROLLOUT_BEFORE ?= reports/triage/rollout_streaks.json
ROLLOUT_AFTER ?= reports/triage/current_rollout_streaks.json
ROLLOUT_TOP ?= 8
ROLLOUT_SUMMARY_OUT ?=
ROLLOUT_DIFF_OUT ?=
ARGS ?=
VERBOSE ?=
CLANG_FORMAT ?= clang-format

ifneq ($(strip $(OUT)),)
VALIDATE_OUT := --out $(OUT)
ROLLOUT_OUT := --out $(OUT)
endif
ifneq ($(strip $(ROLLOUT_SUMMARY_OUT)),)
ROLLOUT_SUMMARY_OUT_ARG := --out $(ROLLOUT_SUMMARY_OUT)
endif
ifneq ($(strip $(ROLLOUT_DIFF_OUT)),)
ROLLOUT_DIFF_OUT_ARG := --out $(ROLLOUT_DIFF_OUT)
endif

ifeq ($(strip $(VERBOSE)),)
BUILD_STDOUT := >/dev/null
else
BUILD_STDOUT :=
endif

build:
	@$(PY) python/setup.py build_ext --inplace --force $(BUILD_STDOUT)

test: build
	@$(PY) -m pytest

preprocess:
	@$(PY) -m tools.slippi.preprocess_suite --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)"

validate: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" $(VALIDATE_OUT)

# Rollout text validation report (parallel to make validate); OUT=... controls report path.
validate-rollout: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" $(ROLLOUT_OUT)

# Always writes to ROLLOUT_JSON (independent of OUT=...).
rollout-capture: build
	@$(PY) -m tools.eval.run_longest_rollout_streaks --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(ROLLOUT_JSON)"

rollout-summary:
	@$(PY) -m tools.eval.summarize_rollout_streaks --in "$(ROLLOUT_JSON)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_SUMMARY_OUT_ARG)

rollout-diff:
	@$(PY) -m tools.eval.diff_rollout_streaks --before "$(ROLLOUT_BEFORE)" --after "$(ROLLOUT_AFTER)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_DIFF_OUT_ARG)

build_data:
	@$(PY) -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco

fmt:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || (echo "Missing clang-format (set CLANG_FORMAT=... or install it)."; exit 1)
	@find src python -type f '(' -name '*.c' -o -name '*.h' ')' -print0 | xargs -0 "$(CLANG_FORMAT)" -i

fmt-check:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || (echo "Missing clang-format (set CLANG_FORMAT=... or install it)."; exit 1)
	@find src python -type f '(' -name '*.c' -o -name '*.h' ')' -print0 | xargs -0 "$(CLANG_FORMAT)" --dry-run --Werror

check: fmt-check test

guardrail-baseline: build
	@$(PY) -m tools.eval.generate_guardrail_baseline --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)"

guardrail-preflight: build
	@$(PY) -m tools.eval.run_guardrail_preflight --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)"

guardrail-preflight-full: test
	@$(PY) -m tools.eval.run_guardrail_preflight --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" --skip-lock-pack

forensic-rows: build
	@$(PY) -m tools.eval.run_forensic_rows $(ARGS)

dolphin-engine-dump:
	@$(PY) -m tools.dolphin.dolphin_engine_dump $(ARGS)

dolphin-extract:
	@$(PY) -m tools.dolphin.extract_engine_dump_rows $(ARGS)

dolphin-forensic-row:
	@$(PY) -m tools.dolphin.forensic_row_dump $(ARGS)
