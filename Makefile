.PHONY: build build-native clean-native-shadow test test-parallel test-serial package-smoke preprocess preprocess-aggregate slpz-convert-validation slpz-convert-suite validate validate-aggregate validate-marth validate-sheik validate-rollout validate-rollout-aggregate validate-rollout-marth validate-rollout-sheik validate-all rollout-capture rollout-summary rollout-diff rollout-locate rollout-locate-summary rollout-locate-diff rollout-disruptive rollout-disruptive-rerank build_data viewer-build viewer fmt fmt-check check guardrail-preflight guardrail-preflight-full guardrail-baseline forensic-rows dolphin-engine-dump dolphin-extract dolphin-forensic-row build-bench-sim build-bench-sim-native bench-sim bench-sim-native FORCE

PY := uv run python
DATASETS_DIR ?= datasets
SUITE ?= replays/suites/fox_falco_fd_ucf084_recent.json
AGG_SUITE ?= replays/suites/aggregate_recent.json
DOUBLES_SUITE ?= replays/suites/doubles_recent.json
MARTH_SUITE ?= replays/suites/marth.json
SHEIK_SUITE ?= replays/suites/sheik.json
CHUNK ?= 4096
OUT ?=
FIELDS ?= action_id,animation_index,on_ground,hitlag,hitstun,state_flags
PRIMARY_ONE_STEP_OUT ?= reports/validation/one_step_suite_eval.txt
PRIMARY_ROLLOUT_OUT ?= reports/validation/rollout_suite_eval.txt
AGG_ONE_STEP_OUT ?= reports/validation/aggregate_recent_one_step_suite_eval.txt
AGG_ROLLOUT_OUT ?= reports/validation/aggregate_recent_rollout_suite_eval.txt
DOUBLES_ONE_STEP_OUT ?= reports/validation/doubles_recent_one_step_suite_eval.txt
DOUBLES_ROLLOUT_OUT ?= reports/validation/doubles_recent_rollout_suite_eval.txt
MARTH_ONE_STEP_OUT ?= reports/validation/marth_one_step.txt
MARTH_ROLLOUT_OUT ?= reports/validation/marth_rollout.txt
SHEIK_ONE_STEP_OUT ?= reports/validation/sheik_one_step.txt
SHEIK_ROLLOUT_OUT ?= reports/validation/sheik_rollout.txt
ROLLOUT_JSON ?= reports/triage/current_rollout_streaks.json
ROLLOUT_BEFORE ?= reports/triage/rollout_streaks.json
ROLLOUT_AFTER ?= reports/triage/current_rollout_streaks.json
ROLLOUT_TOP ?= 8
ROLLOUT_SUMMARY_OUT ?=
ROLLOUT_DIFF_OUT ?=
ROLLOUT_LOCATE_TSV ?= reports/triage/current_rollout_desyncs.tsv
ROLLOUT_LOCATE_BEFORE ?= reports/triage/baseline_rollout_desyncs.tsv
ROLLOUT_LOCATE_AFTER ?= reports/triage/current_rollout_desyncs.tsv
ROLLOUT_LOCATE_SUMMARY_JSON ?=
ROLLOUT_LOCATE_DIFF_JSON ?=
DISRUPTIVE_OUT_DIR ?= reports/triage/disruptive_rollout_desyncs
DISRUPTIVE_HORIZONS ?= 10,20,60
DISRUPTIVE_BATCH_SIZE ?= 512
DISRUPTIVE_WORKERS ?= 8
DISRUPTIVE_CHUNK_RECORDS ?= 1024
DISRUPTIVE_ROWS_IN ?= $(DISRUPTIVE_OUT_DIR)/rows.tsv
ARGS ?=
TEST_ARGS ?=
TEST_WORKERS ?= auto
PREPROCESS_WORKERS ?= 0
VALIDATE_WORKERS ?= 4
VERBOSE ?=
CLANG_FORMAT ?= clang-format
CC ?= cc
CFLAGS ?= -O3 -Wall -Wextra -std=c11 -ffp-contract=off
NATIVE_OPT ?= 0
LTO ?= 0
export MSL_NATIVE_OPT := $(strip $(NATIVE_OPT))
export MSL_LTO := $(strip $(LTO))
ifeq ($(strip $(NATIVE_OPT)),1)
CFLAGS += -march=native
endif
ifeq ($(strip $(LTO)),1)
CFLAGS += -flto
endif
BENCH_SIM ?= build/bench/bench_sim
BENCH_SIM_SRCS := $(wildcard src/*.c) src/decomp/lb/lb_00ce.c tools/bench/bench_sim.c
BUILD_FORCE ?= 0
BUILD_STAMP ?= build/msl_binding.stamp
NATIVE_EXT_GLOB := melee_sim/_native*.so
LEGACY_ROOT_EXT_GLOB := msl_binding*.so
BUILD_SRCS := $(shell find src bindings -type f '(' -name '*.c' -o -name '*.h' ')' -print; printf '%s\n' setup.py pyproject.toml)
VIEWER_PORT ?= 8001
HOST ?= 127.0.0.1
OPEN ?= 1

VALIDATE_OUT := --out "$(PRIMARY_ONE_STEP_OUT)"
ROLLOUT_OUT := --out "$(PRIMARY_ROLLOUT_OUT)"
ifneq ($(strip $(OUT)),)
VALIDATE_OUT := --out "$(OUT)"
ROLLOUT_OUT := --out "$(OUT)"
endif
ifneq ($(strip $(ROLLOUT_SUMMARY_OUT)),)
ROLLOUT_SUMMARY_OUT_ARG := --out $(ROLLOUT_SUMMARY_OUT)
endif
ifneq ($(strip $(ROLLOUT_DIFF_OUT)),)
ROLLOUT_DIFF_OUT_ARG := --out $(ROLLOUT_DIFF_OUT)
endif
ifneq ($(strip $(ROLLOUT_LOCATE_SUMMARY_JSON)),)
ROLLOUT_LOCATE_SUMMARY_JSON_ARG := --json-out $(ROLLOUT_LOCATE_SUMMARY_JSON)
endif
ifneq ($(strip $(ROLLOUT_LOCATE_DIFF_JSON)),)
ROLLOUT_LOCATE_DIFF_JSON_ARG := --json-out $(ROLLOUT_LOCATE_DIFF_JSON)
endif

ifeq ($(strip $(VERBOSE)),)
BUILD_STDOUT := >/dev/null
else
BUILD_STDOUT :=
endif
ifeq ($(strip $(BUILD_FORCE)),1)
BUILD_FORCE_ARG := --force
BUILD_STAMP_DEPS := FORCE
else
BUILD_FORCE_ARG :=
BUILD_STAMP_DEPS :=
endif

build: clean-native-shadow $(BUILD_STAMP)
	@if ! ls $(NATIVE_EXT_GLOB) >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory BUILD_FORCE=1 "$(BUILD_STAMP)"; \
	fi

build-native:
	@$(MAKE) --no-print-directory NATIVE_OPT=1 build

clean-native-shadow:
	@rm -f $(LEGACY_ROOT_EXT_GLOB)

$(BUILD_STAMP): $(BUILD_SRCS) $(BUILD_STAMP_DEPS)
	@mkdir -p "$(@D)"
	@$(PY) setup.py build_ext --inplace $(BUILD_FORCE_ARG) $(BUILD_STDOUT)
	@touch "$@"

FORCE:

test: build
	@mkdir -p reports/triage
	@$(PY) -m pytest -n "$(TEST_WORKERS)" $(TEST_ARGS)

test-parallel: test

test-serial: build
	@mkdir -p reports/triage
	@$(PY) -m pytest $(TEST_ARGS)

package-smoke:
	@$(PY) scripts/package_smoke.py

preprocess:
	@$(PY) -m tools.slippi.preprocess_suite --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --workers "$(PREPROCESS_WORKERS)"

preprocess-aggregate:
	@$(PY) -m tools.slippi.preprocess_suite --suite "$(AGG_SUITE)" --datasets-dir "$(DATASETS_DIR)" --workers "$(PREPROCESS_WORKERS)"

slpz-convert-suite:
	@$(PY) -m tools.slippi.convert_replay_storage --suite "$(SUITE)" $(ARGS)

slpz-convert-validation:
	@$(PY) -m tools.slippi.convert_replay_storage --all-validation-suites $(ARGS)

validate: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" $(VALIDATE_OUT)

validate-aggregate: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(AGG_SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" --out "$(AGG_ONE_STEP_OUT)"

# Rollout text validation report (parallel to make validate); OUT=... controls report path.
validate-rollout: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" $(ROLLOUT_OUT)

validate-rollout-aggregate: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(AGG_SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(AGG_ROLLOUT_OUT)"

# Fast Marth-focused iteration loop: the marth suite is the same replays the aggregate
# suite carries (subset), preprocessed under datasets/marth - run these frequently while
# debugging Marth, and validate-all (which covers the same rows via aggregate) less often.
validate-marth: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(MARTH_SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" --out "$(MARTH_ONE_STEP_OUT)"

validate-rollout-marth: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(MARTH_SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(MARTH_ROLLOUT_OUT)"

validate-sheik: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SHEIK_SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" --out "$(SHEIK_ONE_STEP_OUT)"

validate-rollout-sheik: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(SHEIK_SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(SHEIK_ROLLOUT_OUT)"

validate-all: build
	@$(PY) -m tools.eval.run_validate_all --suite "$(SUITE)" --agg-suite "$(AGG_SUITE)" --doubles-suite "$(DOUBLES_SUITE)" --sheik-suite "$(SHEIK_SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" --fields "$(FIELDS)" --one-step-out reports/validation/one_step_suite_eval.txt --rollout-out reports/validation/rollout_suite_eval.txt --agg-one-step-out "$(AGG_ONE_STEP_OUT)" --agg-rollout-out "$(AGG_ROLLOUT_OUT)" --doubles-one-step-out "$(DOUBLES_ONE_STEP_OUT)" --doubles-rollout-out "$(DOUBLES_ROLLOUT_OUT)" --sheik-one-step-out "$(SHEIK_ONE_STEP_OUT)" --sheik-rollout-out "$(SHEIK_ROLLOUT_OUT)" --workers "$(VALIDATE_WORKERS)"

# Always writes to ROLLOUT_JSON (independent of OUT=...).
rollout-capture: build
	@$(PY) -m tools.eval.run_longest_rollout_streaks --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(ROLLOUT_JSON)"

rollout-summary:
	@$(PY) -m tools.eval.summarize_rollout_streaks --in "$(ROLLOUT_JSON)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_SUMMARY_OUT_ARG)

rollout-diff:
	@$(PY) -m tools.eval.diff_rollout_streaks --before "$(ROLLOUT_BEFORE)" --after "$(ROLLOUT_AFTER)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_DIFF_OUT_ARG)

rollout-locate: build
	@$(PY) -m tools.eval.locate_rollout_desyncs --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --fields "$(FIELDS)" --out "$(ROLLOUT_LOCATE_TSV)" $(ARGS)

rollout-locate-summary:
	@$(PY) -m tools.eval.summarize_rollout_locate --in "$(ROLLOUT_LOCATE_TSV)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_LOCATE_SUMMARY_JSON_ARG)

rollout-locate-diff:
	@$(PY) -m tools.eval.diff_rollout_locate --before "$(ROLLOUT_LOCATE_BEFORE)" --after "$(ROLLOUT_LOCATE_AFTER)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_LOCATE_DIFF_JSON_ARG)

rollout-disruptive: build
	@$(PY) -m tools.eval.disruptive_rollout_desyncs --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --horizons "$(DISRUPTIVE_HORIZONS)" --batch-size "$(DISRUPTIVE_BATCH_SIZE)" --workers "$(DISRUPTIVE_WORKERS)" --chunk-records "$(DISRUPTIVE_CHUNK_RECORDS)" --out-dir "$(DISRUPTIVE_OUT_DIR)" --top "$(ROLLOUT_TOP)" $(ARGS)

rollout-disruptive-rerank:
	@$(PY) -m tools.eval.disruptive_rollout_desyncs --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --horizons "$(DISRUPTIVE_HORIZONS)" --rows-in "$(DISRUPTIVE_ROWS_IN)" --out-dir "$(DISRUPTIVE_OUT_DIR)" --top "$(ROLLOUT_TOP)" $(ARGS)

build_data:
	@$(PY) -m tools.extraction.build_data --iso-dir _iso --stages grnla,grnba,griz,grps,grst,grop

viewer-build:
	@tools/viewer/build.sh

viewer:
	@MSL_VIEWER_STATIC_ROOT="build/viewer" MSL_VIEWER_URL_PATH="/tools/viewer/" MSL_VIEWER_HOST="$(HOST)" MSL_VIEWER_PORT="$(VIEWER_PORT)" MSL_VIEWER_OPEN="$(OPEN)" node tools/viewer/server.mjs

fmt:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || (echo "Missing clang-format (set CLANG_FORMAT=... or install it)."; exit 1)
	@find src bindings -type f '(' -name '*.c' -o -name '*.h' ')' -print0 | xargs -0 "$(CLANG_FORMAT)" -i

fmt-check:
	@command -v "$(CLANG_FORMAT)" >/dev/null 2>&1 || (echo "Missing clang-format (set CLANG_FORMAT=... or install it)."; exit 1)
	@find src bindings -type f '(' -name '*.c' -o -name '*.h' ')' -print0 | xargs -0 "$(CLANG_FORMAT)" --dry-run --Werror

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

build-bench-sim:
	@mkdir -p build/bench
	@$(CC) $(CFLAGS) -Isrc $(BENCH_SIM_SRCS) -lm -o "$(BENCH_SIM)"

build-bench-sim-native:
	@$(MAKE) --no-print-directory NATIVE_OPT=1 build-bench-sim

bench-sim: build-bench-sim
	@"$(BENCH_SIM)" $(ARGS)

bench-sim-native: build-bench-sim-native
	@"$(BENCH_SIM)" $(ARGS)
