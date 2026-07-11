.PHONY: bootstrap build build-native clean-native clean-native-shadow test test-parallel test-serial package-smoke slpz-convert-validation slpz-convert-suite validate validate-aggregate validate-marth validate-falcon validate-sheik validate-rollout validate-rollout-aggregate validate-rollout-marth validate-rollout-falcon validate-rollout-sheik validate-all validate-heldout rollout-summary rollout-diff build_data viewer-build viewer fmt fmt-check check dolphin-engine-dump dolphin-extract build-bench-sim build-bench-sim-native bench-sim bench-sim-native FORCE

PY := uv run python
SUITE ?= replays/suites/fox_falco_fd_ucf084_recent.json
AGG_SUITE ?= replays/suites/aggregate_recent.json
DOUBLES_SUITE ?= replays/suites/doubles_recent.json
MARTH_SUITE ?= replays/suites/marth.json
FALCON_SUITE ?= replays/suites/falcon.json
SHEIK_SUITE ?= replays/suites/sheik.json
HELDOUT_INDEX ?= replays/suites/heldout.json
CHUNK ?= 64
OUT ?=
PRIMARY_ONE_STEP_OUT ?= reports/validation/one_step_suite_eval.txt
PRIMARY_ROLLOUT_OUT ?= reports/validation/rollout_suite_eval.txt
AGG_ONE_STEP_OUT ?= reports/validation/aggregate_recent_one_step_suite_eval.txt
AGG_ROLLOUT_OUT ?= reports/validation/aggregate_recent_rollout_suite_eval.txt
DOUBLES_ONE_STEP_OUT ?= reports/validation/doubles_recent_one_step_suite_eval.txt
DOUBLES_ROLLOUT_OUT ?= reports/validation/doubles_recent_rollout_suite_eval.txt
MARTH_ONE_STEP_OUT ?= reports/validation/marth_one_step.txt
MARTH_ROLLOUT_OUT ?= reports/validation/marth_rollout.txt
FALCON_ONE_STEP_OUT ?= reports/validation/falcon_one_step.txt
FALCON_ROLLOUT_OUT ?= reports/validation/falcon_rollout.txt
SHEIK_ONE_STEP_OUT ?= reports/validation/sheik_one_step.txt
SHEIK_ROLLOUT_OUT ?= reports/validation/sheik_rollout.txt
HELDOUT_OUT_DIR ?= reports/validation/heldout
HELDOUT_SUMMARY_OUT ?= reports/validation/heldout/summary.txt
ROLLOUT_JSON ?= reports/triage/current_rollout_streaks.json
ROLLOUT_BEFORE ?= reports/triage/rollout_streaks.json
ROLLOUT_AFTER ?= reports/triage/current_rollout_streaks.json
ROLLOUT_TOP ?= 8
ROLLOUT_SUMMARY_OUT ?=
ROLLOUT_DIFF_OUT ?=
ARGS ?=
TEST_ARGS ?=
TEST_WORKERS ?= auto
VALIDATE_WORKERS ?= 0
HELDOUT_WORKERS ?= 0
HELDOUT_SUITE_WORKERS ?= 0
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
NATIVE_SOURCE_LIST_TOOL := tools/build_native_sources.py
PY_EXT_SUFFIX := $(shell $(PY) -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX") or ".so")')
PY_SOABI := $(shell $(PY) -c 'import sysconfig; print(sysconfig.get_config_var("SOABI") or "python")')
PY_INCLUDE_FLAGS := $(shell $(PY) -c 'import sysconfig, numpy; paths=[numpy.get_include(), sysconfig.get_path("include"), sysconfig.get_path("platinclude")]; print(" ".join("-I"+p for p in dict.fromkeys(paths) if p))')
PY_LINK_FLAGS := $(shell $(PY) -c 'import sysconfig; vals=[sysconfig.get_config_var(k) or "" for k in ("LDFLAGS","LIBS","SYSLIBS")]; print(" ".join(v for v in vals if v))')
# macOS extension modules must leave Python symbols unresolved until load time
# (CPython's own LDSHARED is `-bundle -undefined dynamic_lookup`); plain -shared
# fails at link with missing _Py* symbols there.
ifeq ($(shell uname -s),Darwin)
NATIVE_SO_LINK_MODE := -bundle -undefined dynamic_lookup
else
NATIVE_SO_LINK_MODE := -shared
endif
NATIVE_SO := melee_sim/_native$(PY_EXT_SUFFIX)
NATIVE_BUILD_DIR := build/native/$(PY_SOABI)
NATIVE_OBJ_DIR := $(NATIVE_BUILD_DIR)/opt$(strip $(NATIVE_OPT))-lto$(strip $(LTO))
NATIVE_CONFIG_STAMP := $(NATIVE_BUILD_DIR)/active_config.stamp
NATIVE_SOURCE_LIST_DEPS := setup.py $(NATIVE_SOURCE_LIST_TOOL)
NATIVE_SRCS := $(shell $(PY) $(NATIVE_SOURCE_LIST_TOOL) setup.py)
ifeq ($(strip $(NATIVE_SRCS)),)
$(error native extension source list is empty; check setup.py and $(NATIVE_SOURCE_LIST_TOOL))
endif
NATIVE_OBJS := $(patsubst %.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_SRCS))
LEGACY_ROOT_EXT_GLOB := msl_binding*.so
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
ifeq ($(strip $(VERBOSE)),)
BUILD_QUIET := @
else
BUILD_QUIET :=
endif
ifeq ($(strip $(BUILD_FORCE)),1)
BUILD_STAMP_DEPS := FORCE
NATIVE_OBJECT_DEPS := FORCE
else
BUILD_STAMP_DEPS :=
NATIVE_OBJECT_DEPS :=
endif

build: clean-native-shadow $(BUILD_STAMP)
	@if ! ls $(NATIVE_EXT_GLOB) >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory BUILD_FORCE=1 "$(BUILD_STAMP)"; \
	fi

build-native:
	@$(MAKE) --no-print-directory NATIVE_OPT=1 build

clean-native-shadow:
	@rm -f $(LEGACY_ROOT_EXT_GLOB)

clean-native:
	@rm -rf build/native build/temp.* build/lib.* "$(BUILD_STAMP)" $(NATIVE_EXT_GLOB) $(LEGACY_ROOT_EXT_GLOB)

$(NATIVE_CONFIG_STAMP): FORCE
	@mkdir -p "$(@D)"
	@{ \
		printf '%s\n' 'CC=$(CC)'; \
		printf '%s\n' 'CFLAGS=$(CFLAGS)'; \
		printf '%s\n' 'NATIVE_OPT=$(strip $(NATIVE_OPT))'; \
		printf '%s\n' 'LTO=$(strip $(LTO))'; \
		printf '%s\n' 'PY=$(PY)'; \
		printf '%s\n' 'PY_EXT_SUFFIX=$(PY_EXT_SUFFIX)'; \
		printf '%s\n' 'PY_INCLUDE_FLAGS=$(PY_INCLUDE_FLAGS)'; \
		printf '%s\n' 'PY_LINK_FLAGS=$(PY_LINK_FLAGS)'; \
	} > "$@.tmp"; \
	if test -f "$@" && cmp -s "$@" "$@.tmp"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

$(NATIVE_OBJ_DIR)/%.o: %.c Makefile $(NATIVE_CONFIG_STAMP) $(NATIVE_OBJECT_DEPS)
	@mkdir -p "$(@D)"
	$(BUILD_QUIET)$(CC) $(CFLAGS) -DNDEBUG -fPIC $(PY_INCLUDE_FLAGS) -Isrc -MMD -MP -c "$<" -o "$@"

$(NATIVE_SO): $(NATIVE_OBJS) $(NATIVE_SOURCE_LIST_DEPS) $(NATIVE_CONFIG_STAMP)
	@mkdir -p "$(@D)"
	$(BUILD_QUIET)$(CC) $(NATIVE_SO_LINK_MODE) $(if $(filter 1,$(strip $(LTO))),-flto,) $(filter %.o,$^) $(PY_LINK_FLAGS) -lm -o "$@"

$(BUILD_STAMP): $(NATIVE_SO) $(BUILD_STAMP_DEPS)
	@mkdir -p "$(@D)"
	@touch "$@"

FORCE:

-include $(NATIVE_OBJS:.o=.d)

bootstrap:
	@test -n "$(ISO)" || (echo "Usage: make bootstrap ISO=/path/to/SSBM.iso"; exit 2)
	@git lfs version >/dev/null 2>&1 || (echo "Missing git-lfs. Install it before bootstrapping."; exit 2)
	@git lfs install --local
	@git lfs pull
	@uv sync --dev
	@$(PY) -m melee_sim.extract_data --iso "$(ISO)" --out-dir data --iso-dir _iso
	@$(MAKE) --no-print-directory build

test: build
	@mkdir -p reports/triage
	@$(PY) -m pytest -n "$(TEST_WORKERS)" $(TEST_ARGS)

test-parallel: test

test-serial: build
	@mkdir -p reports/triage
	@$(PY) -m pytest $(TEST_ARGS)

package-smoke:
	@$(PY) scripts/package_smoke.py

slpz-convert-suite:
	@$(PY) -m tools.slippi.convert_replay_storage --suite "$(SUITE)" $(ARGS)

slpz-convert-validation:
	@$(PY) -m tools.slippi.convert_replay_storage --all-validation-suites $(ARGS)

validate: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SUITE)" --chunk "$(CHUNK)" $(VALIDATE_OUT)

validate-aggregate: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(AGG_SUITE)" --chunk "$(CHUNK)" --out "$(AGG_ONE_STEP_OUT)"

# Rollout text validation report (parallel to make validate); OUT=... controls report path.
validate-rollout: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(SUITE)" $(ROLLOUT_OUT)

validate-rollout-aggregate: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(AGG_SUITE)" --out "$(AGG_ROLLOUT_OUT)"

# Fast Marth-focused iteration loop: the marth suite is the same replays the aggregate
# suite carries (subset), stored under replays/validation/marth - run these frequently while
# debugging Marth, and validate-all (which covers the same rows via aggregate) less often.
validate-marth: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(MARTH_SUITE)" --chunk "$(CHUNK)" --out "$(MARTH_ONE_STEP_OUT)"

validate-rollout-marth: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(MARTH_SUITE)" --out "$(MARTH_ROLLOUT_OUT)"

validate-falcon: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(FALCON_SUITE)" --chunk "$(CHUNK)" --out "$(FALCON_ONE_STEP_OUT)"

validate-rollout-falcon: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(FALCON_SUITE)" --out "$(FALCON_ROLLOUT_OUT)"

validate-sheik: build
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SHEIK_SUITE)" --chunk "$(CHUNK)" --out "$(SHEIK_ONE_STEP_OUT)"

validate-rollout-sheik: build
	@$(PY) -m tools.eval.run_rollout_suite_eval --suite "$(SHEIK_SUITE)" --out "$(SHEIK_ROLLOUT_OUT)"

validate-all: build
	@$(PY) -m tools.eval.run_validate_all --suite "$(SUITE)" --agg-suite "$(AGG_SUITE)" --doubles-suite "$(DOUBLES_SUITE)" --falcon-suite "$(FALCON_SUITE)" --sheik-suite "$(SHEIK_SUITE)" --chunk "$(CHUNK)" --one-step-out reports/validation/one_step_suite_eval.txt --rollout-out reports/validation/rollout_suite_eval.txt --agg-one-step-out "$(AGG_ONE_STEP_OUT)" --agg-rollout-out "$(AGG_ROLLOUT_OUT)" --doubles-one-step-out "$(DOUBLES_ONE_STEP_OUT)" --doubles-rollout-out "$(DOUBLES_ROLLOUT_OUT)" --falcon-one-step-out "$(FALCON_ONE_STEP_OUT)" --falcon-rollout-out "$(FALCON_ROLLOUT_OUT)" --sheik-one-step-out "$(SHEIK_ONE_STEP_OUT)" --sheik-rollout-out "$(SHEIK_ROLLOUT_OUT)" --workers "$(VALIDATE_WORKERS)"

validate-heldout: build
	@$(PY) -m tools.eval.run_heldout_validation --index "$(HELDOUT_INDEX)" --chunk "$(CHUNK)" --out-dir "$(HELDOUT_OUT_DIR)" --summary-out "$(HELDOUT_SUMMARY_OUT)" --workers "$(HELDOUT_WORKERS)" --suite-workers "$(HELDOUT_SUITE_WORKERS)"

rollout-summary:
	@$(PY) -m tools.eval.summarize_rollout_streaks --in "$(ROLLOUT_JSON)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_SUMMARY_OUT_ARG)

rollout-diff:
	@$(PY) -m tools.eval.diff_rollout_streaks --before "$(ROLLOUT_BEFORE)" --after "$(ROLLOUT_AFTER)" --top "$(ROLLOUT_TOP)" $(ROLLOUT_DIFF_OUT_ARG)

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

dolphin-engine-dump:
	@$(PY) -m tools.dolphin.dolphin_engine_dump $(ARGS)

dolphin-extract:
	@$(PY) -m tools.dolphin.extract_engine_dump_rows $(ARGS)

build-bench-sim:
	@mkdir -p build/bench
	@$(CC) $(CFLAGS) -Isrc $(BENCH_SIM_SRCS) -lm -o "$(BENCH_SIM)"

build-bench-sim-native:
	@$(MAKE) --no-print-directory NATIVE_OPT=1 build-bench-sim

bench-sim: build-bench-sim
	@"$(BENCH_SIM)" $(ARGS)

bench-sim-native: build-bench-sim-native
	@"$(BENCH_SIM)" $(ARGS)
