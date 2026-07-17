# Isolated PPC reference and native scalar builds for the source-shaped Melee
# core. They do not include the repository's existing production extension.
# The validator asks the selected Python for its include directory once at
# build time.

.DELETE_ON_ERROR:

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
CORE := $(ROOT)/src/melee_core
BUILD_ROOT := $(ROOT)/build/melee_core
PPC_BUILD := $(BUILD_ROOT)/ppc
NATIVE_BUILD := $(BUILD_ROOT)/native
NATIVE_RELEASE_BUILD := $(BUILD_ROOT)/native-release
WASM_BUILD := $(BUILD_ROOT)/wasm
VALIDATION_BUILD := $(BUILD_ROOT)/validation
SOURCE_TREE := $(CORE)/gameplay
PPC_OBJ_DIR := $(PPC_BUILD)/obj
NATIVE_OBJ_DIR := $(NATIVE_BUILD)/obj
WASM_OBJ_DIR := $(WASM_BUILD)/obj
NATIVE_GENERATED_DIR := $(NATIVE_BUILD)/generated
WASM_GENERATED_DIR := $(WASM_BUILD)/generated
WASM_LAYOUT_INCLUDE := $(ROOT)/tools/melee_core/wasm_layout_include
PPC_GENERATED_DIR := $(PPC_BUILD)/generated
TOOLCHAIN_ROOT := $(BUILD_ROOT)/toolchain/root
TOOLCHAIN_STAMP := $(BUILD_ROOT)/toolchain/ready.stamp
CC := $(TOOLCHAIN_ROOT)/usr/bin/powerpc-linux-gnu-gcc-13
QEMU := $(TOOLCHAIN_ROOT)/usr/bin/qemu-ppc-static
SYSROOT := $(TOOLCHAIN_ROOT)
QEMU_SYSROOT := $(TOOLCHAIN_ROOT)/usr/powerpc-linux-gnu
PY ?= $(ROOT)/.venv/bin/python
HOST_CC ?= cc
EMCC ?= emcc
WASM_LINK_FLAGS ?= -sSTACK_SIZE=1048576
MSL_DATA_DIR ?= $(ROOT)/data
DATA ?= $(abspath $(MSL_DATA_DIR))/raw
VALIDATION_SUITE ?= replays/suites/melee_core_aggregate.json
VALIDATION_CHARACTERS ?= Fox,Falco,Marth,Captain Falcon,Sheik,Zelda,Jigglypuff,Peach
VALIDATION_STAGES ?= 32,31,3,2,8,28
VALIDATION_BACKEND ?= native
VALIDATION_WORKERS ?= 0
VALIDATION_BUILD_JOBS ?= 4
VALIDATION_FRAMES ?= 0
VALIDATION_ARGS ?=
BENCHMARK_MANIFEST ?= $(BUILD_ROOT)/benchmark/cases.tsv
BENCHMARK_CPU ?= 8
BENCHMARK_MATCHES ?= 256
BENCHMARK_RESIDENT_MATCHES ?= 16
BENCHMARK_MATCH_FRAMES ?= 32768
BENCHMARK_WARMUP_TICKS ?= 8
LARGE_BATCH_MATCHES ?= 256
RELEASE_ARCH_FLAGS ?= -march=native -mtune=native

SOURCE_SYNC := $(ROOT)/tools/melee_core/source_sync.sh
UPSTREAM_REL_SRCS := $(shell find $(SOURCE_TREE)/src -type f -name '*.c' -printf '%P\n' | LC_ALL=C sort)
UPSTREAM_REL_SRCS := $(filter-out \
	melee/ft/chara/ftCommon/ftCo_0A01.c \
	melee/it/it_279C.c, \
	$(UPSTREAM_REL_SRCS))
UPSTREAM_REL_SRCS := $(filter-out melee/it/items/%.c,$(UPSTREAM_REL_SRCS))
UPSTREAM_REL_SRCS += \
	melee/it/items/itfoxblaster.c \
	melee/it/items/itfoxillusion.c \
	melee/it/items/itfoxlaser.c \
	melee/it/items/itbombhei.c \
	melee/it/items/itdosei.c \
	melee/it/items/itfoods.c \
	melee/it/items/itfreeze.c \
	melee/it/items/itharisen.c \
	melee/it/items/itheiho.c \
	melee/it/items/itlinkhookshot.c \
	melee/it/items/itpeachexplode.c \
	melee/it/items/itpeachparasol.c \
	melee/it/items/itpeachtoad.c \
	melee/it/items/itpeachtoadspore.c \
	melee/it/items/itpeachturnip.c \
	melee/it/items/itseakchain.c \
	melee/it/items/itseakneedleheld.c \
	melee/it/items/itseakneedlethrown.c \
	melee/it/items/itseakvanish.c \
	melee/it/items/itsword.c \
	melee/it/items/itzeldadinfire.c \
	melee/it/items/itzeldadinfireexplode.c
UPSTREAM_SRCS := $(addprefix $(SOURCE_TREE)/src/,$(UPSTREAM_REL_SRCS))
PPC_UPSTREAM_OBJS := $(patsubst $(SOURCE_TREE)/%.c,$(PPC_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))
NATIVE_UPSTREAM_OBJS := $(patsubst $(SOURCE_TREE)/%.c,$(NATIVE_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))
WASM_UPSTREAM_OBJS := $(patsubst $(SOURCE_TREE)/%.c,$(WASM_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))

PLATFORM_SRCS := \
	$(CORE)/platform/dolphin_mtx.c \
	$(CORE)/platform/files.c \
	$(CORE)/platform/memory.c \
	$(CORE)/platform/os_report.c \
	$(CORE)/platform/platform_state.c \
	$(CORE)/platform/slippi.c
NATIVE_PLATFORM_SRCS := $(CORE)/platform/native_dat.c
RUNTIME_SRCS := \
	$(CORE)/api.c \
	$(CORE)/runtime/audio.c \
	$(CORE)/runtime/camera.c \
	$(CORE)/runtime/common.c \
	$(CORE)/runtime/context.c \
	$(CORE)/runtime/effects.c \
	$(CORE)/runtime/final_destination.c \
	$(CORE)/runtime/ftco_0A01.c \
	$(CORE)/runtime/items.c \
	$(CORE)/runtime/match.c \
	$(CORE)/runtime/math.c \
	$(CORE)/runtime/observation.c \
	$(CORE)/runtime/relocation.c \
	$(CORE)/runtime/savestate.c \
	$(CORE)/runtime/scalar.c \
	$(CORE)/runtime/viewer.c \
	$(CORE)/runtime/wire.c
STUB_SRCS := \
	$(CORE)/stubs/headless_exclusions.c \
	$(CORE)/stubs/unreached_presentation.c \
	$(CORE)/stubs/unresolved_abort.c
LOCAL_SRCS := $(PLATFORM_SRCS) $(RUNTIME_SRCS) $(STUB_SRCS)
PPC_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(LOCAL_SRCS))
NATIVE_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(LOCAL_SRCS))
WASM_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(WASM_OBJ_DIR)/%.o,$(LOCAL_SRCS))
NATIVE_PLATFORM_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_PLATFORM_SRCS))
WASM_PLATFORM_OBJS := $(patsubst $(ROOT)/%.c,$(WASM_OBJ_DIR)/%.o,$(NATIVE_PLATFORM_SRCS))
NATIVE_DAT_TYPES_SRC := $(ROOT)/tools/melee_core/native_dat_types.c
NATIVE_DAT_LAYOUT_GENERATOR := $(ROOT)/tools/melee_core/generate_native_dat_layout.py
COMMAND_FIELDS_GENERATOR := $(ROOT)/tools/melee_core/generate_command_accessors.py
NATIVE_DAT_PPC_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_ppc.o
NATIVE_DAT_NATIVE_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_native.o
NATIVE_DAT_LAYOUT_SRC := $(NATIVE_GENERATED_DIR)/native_dat_layout.c
NATIVE_DAT_LAYOUT_OBJ := $(NATIVE_OBJ_DIR)/generated/native_dat_layout.o
WASM_DAT_NATIVE_TYPES_OBJ := $(WASM_GENERATED_DIR)/native_dat_types_wasm.o
WASM_DAT_LAYOUT_SRC := $(WASM_GENERATED_DIR)/native_dat_layout.c
WASM_DAT_LAYOUT_OBJ := $(WASM_OBJ_DIR)/generated/native_dat_layout.o
WASM_COMMAND_FIELDS := $(WASM_GENERATED_DIR)/msl_command_fields.h
MATCH_RELOC_TYPES_SRC := $(ROOT)/tools/melee_core/match_reloc_types.c
MATCH_RELOC_GENERATOR := $(ROOT)/tools/melee_core/generate_match_reloc_layout.py
MATCH_RELOC_TYPES_DEF := $(CORE)/runtime/relocation_types.def
NATIVE_MATCH_RELOC_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/match_reloc_types.o
NATIVE_MATCH_RELOC_LAYOUT_SRC := $(NATIVE_GENERATED_DIR)/match_reloc_layout.c
NATIVE_MATCH_RELOC_LAYOUT_OBJ := $(NATIVE_OBJ_DIR)/generated/match_reloc_layout.o
WASM_MATCH_RELOC_TYPES_OBJ := $(WASM_GENERATED_DIR)/match_reloc_types.o
WASM_MATCH_RELOC_LAYOUT_SRC := $(WASM_GENERATED_DIR)/match_reloc_layout.c
WASM_MATCH_RELOC_LAYOUT_OBJ := $(WASM_OBJ_DIR)/generated/match_reloc_layout.o
PPC_MATCH_RELOC_TYPES_OBJ := $(PPC_GENERATED_DIR)/match_reloc_types.o
PPC_MATCH_RELOC_LAYOUT_SRC := $(PPC_GENERATED_DIR)/match_reloc_layout.c
PPC_MATCH_RELOC_LAYOUT_OBJ := $(PPC_OBJ_DIR)/generated/match_reloc_layout.o
PPC_CORE_OBJS := $(PPC_UPSTREAM_OBJS) $(PPC_LOCAL_OBJS) \
	$(PPC_MATCH_RELOC_LAYOUT_OBJ)
NATIVE_CORE_OBJS := $(NATIVE_UPSTREAM_OBJS) $(NATIVE_LOCAL_OBJS) \
	$(NATIVE_PLATFORM_OBJS) $(NATIVE_DAT_LAYOUT_OBJ) \
	$(NATIVE_MATCH_RELOC_LAYOUT_OBJ)
WASM_CORE_OBJS := $(WASM_UPSTREAM_OBJS) $(WASM_LOCAL_OBJS) \
	$(WASM_PLATFORM_OBJS) $(WASM_DAT_LAYOUT_OBJ) \
	$(WASM_MATCH_RELOC_LAYOUT_OBJ)
MAIN_SRC := $(CORE)/runtime/main.c
PPC_MAIN_OBJ := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(MAIN_SRC))
NATIVE_MAIN_OBJ := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(MAIN_SRC))
PPC_OBJS := $(PPC_CORE_OBJS) $(PPC_MAIN_OBJ)
NATIVE_OBJS := $(NATIVE_CORE_OBJS) $(NATIVE_MAIN_OBJ)
SMOKE_SRCS := \
	$(ROOT)/tests/melee_core/archive_smoke.c \
	$(ROOT)/tests/melee_core/data_load_smoke.c \
	$(ROOT)/tests/melee_core/map_collision_smoke.c \
	$(ROOT)/tests/melee_core/model_animation_smoke.c \
	$(ROOT)/tests/melee_core/scalar_api_smoke.c \
	$(ROOT)/tests/melee_core/scheduler_smoke.c \
	$(ROOT)/tests/melee_core/wasm_parity.c
NATIVE_SMOKE_SRCS := \
	$(ROOT)/tests/melee_core/batch_api_smoke.c \
	$(ROOT)/tests/melee_core/context_smoke.c \
	$(ROOT)/tests/melee_core/data_load_smoke.c \
	$(ROOT)/tests/melee_core/map_collision_smoke.c \
	$(ROOT)/tests/melee_core/model_animation_smoke.c \
	$(ROOT)/tests/melee_core/scalar_api_smoke.c \
	$(ROOT)/tests/melee_core/scheduler_smoke.c \
	$(ROOT)/tests/melee_core/lifecycle_bench.c
PPC_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(SMOKE_SRCS))
NATIVE_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_SMOKE_SRCS))
NATIVE_REPLAY_BENCH_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/replay_bench.o
NATIVE_RUNTIME_CENSUS_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/runtime_census.o
NATIVE_LARGE_BATCH_SMOKE_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/large_batch_smoke.o
NATIVE_FLAGS_STAMP := $(NATIVE_BUILD)/compile-flags.stamp
NATIVE_COMPILE_OBJS := $(NATIVE_UPSTREAM_OBJS) $(NATIVE_LOCAL_OBJS) \
	$(NATIVE_PLATFORM_OBJS) $(NATIVE_MAIN_OBJ) $(NATIVE_SMOKE_OBJS) \
	$(NATIVE_REPLAY_BENCH_OBJ) $(NATIVE_RUNTIME_CENSUS_OBJ) \
	$(NATIVE_LARGE_BATCH_SMOKE_OBJ) \
	$(NATIVE_DAT_LAYOUT_OBJ) \
	$(NATIVE_MATCH_RELOC_LAYOUT_OBJ)
DEPS := $(PPC_OBJS:.o=.d) $(PPC_SMOKE_OBJS:.o=.d) $(NATIVE_OBJS:.o=.d) \
	$(NATIVE_SMOKE_OBJS:.o=.d) $(NATIVE_REPLAY_BENCH_OBJ:.o=.d) \
	$(NATIVE_RUNTIME_CENSUS_OBJ:.o=.d) $(WASM_CORE_OBJS:.o=.d) \
	$(NATIVE_DAT_PPC_TYPES_OBJ:.o=.d) \
	$(NATIVE_DAT_NATIVE_TYPES_OBJ:.o=.d) \
	$(NATIVE_MATCH_RELOC_TYPES_OBJ:.o=.d) \
	$(NATIVE_MATCH_RELOC_LAYOUT_OBJ:.o=.d) \
	$(WASM_MATCH_RELOC_TYPES_OBJ:.o=.d) \
	$(WASM_MATCH_RELOC_LAYOUT_OBJ:.o=.d) \
	$(PPC_MATCH_RELOC_TYPES_OBJ:.o=.d) \
	$(PPC_MATCH_RELOC_LAYOUT_OBJ:.o=.d)

BINARY := $(PPC_BUILD)/melee-core-ppc
NATIVE_BINARY := $(NATIVE_BUILD)/melee-core-native
WASM_MODULE := $(WASM_BUILD)/melee-core.js
ARCHIVE_SMOKE := $(PPC_BUILD)/archive-smoke
DATA_SMOKE := $(PPC_BUILD)/data-smoke
MAP_SMOKE := $(PPC_BUILD)/map-collision-smoke
MODEL_SMOKE := $(PPC_BUILD)/model-animation-smoke
SCHEDULER_SMOKE := $(PPC_BUILD)/scheduler-smoke
SCALAR_API_SMOKE := $(PPC_BUILD)/scalar-api-smoke
NATIVE_DATA_SMOKE := $(NATIVE_BUILD)/data-smoke
NATIVE_MAP_SMOKE := $(NATIVE_BUILD)/map-collision-smoke
NATIVE_MODEL_SMOKE := $(NATIVE_BUILD)/model-animation-smoke
NATIVE_SCHEDULER_SMOKE := $(NATIVE_BUILD)/scheduler-smoke
NATIVE_SCALAR_API_SMOKE := $(NATIVE_BUILD)/scalar-api-smoke
NATIVE_CONTEXT_SMOKE := $(NATIVE_BUILD)/context-smoke
NATIVE_BATCH_API_SMOKE := $(NATIVE_BUILD)/batch-api-smoke
NATIVE_WASM_PARITY := $(NATIVE_BUILD)/wasm-parity
NATIVE_LIFECYCLE_BENCH := $(NATIVE_BUILD)/lifecycle-bench
NATIVE_REPLAY_BENCH := $(NATIVE_BUILD)/replay-bench
NATIVE_RUNTIME_CENSUS := $(NATIVE_BUILD)/runtime-census
NATIVE_LARGE_BATCH_SMOKE := $(NATIVE_BUILD)/large-batch-smoke
NATIVE_RELEASE_BINARY := $(NATIVE_RELEASE_BUILD)/melee-core-native
NATIVE_RELEASE_REPLAY_BENCH := $(NATIVE_RELEASE_BUILD)/replay-bench
VIEWER_SCHEMA_TOOL := $(NATIVE_BUILD)/viewer-schema
VIEWER_SCHEMA_JS := $(ROOT)/tools/viewer/live/schema.generated.js
VALIDATION_NATIVE_SRC := $(ROOT)/bindings/msl_replay_validate.c
VALIDATION_NATIVE := $(VALIDATION_BUILD)/_msl_replay_validate.so

CPPFLAGS := \
	-DBUGFIX \
	-include $(CORE)/platform/compat.h \
	-I$(CORE) \
	-I$(CORE)/platform/include \
	-I$(SOURCE_TREE)/src \
	-I$(SOURCE_TREE)/src/melee \
	-I$(SOURCE_TREE)/src/melee/ft/chara \
	-I$(SOURCE_TREE)/src/sysdolphin \
	-I$(SOURCE_TREE)/src/Runtime \
	-I$(SOURCE_TREE)/extern/dolphin/include
NATIVE_CPPFLAGS := $(CPPFLAGS) -DMSL_CORE_NATIVE
WASM_CPPFLAGS := $(CPPFLAGS) -I$(WASM_GENERATED_DIR) \
	-DMSL_CORE_NATIVE -DMSL_CORE_WASM
CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -w
NATIVE_CFLAGS = $(CFLAGS) -fno-pie
NATIVE_LINK_FLAGS ?=
# The source-shaped gameplay TUs still contain optimizer-sensitive decomp C.
# Keep them at the validation reference profile until Phase 8 closes those UB
# boundaries; Phase 7 optimizes the audited host/API shell below and requires
# the complete output-lock suite before expanding that allowlist.
NATIVE_RELEASE_CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -fno-pie -w
NATIVE_BASE_CFLAGS := $(NATIVE_CFLAGS)
WASM_CFLAGS = $(CFLAGS) -Wno-implicit-function-declaration -Wno-int-conversion \
	-Wno-incompatible-pointer-types -Wno-return-mismatch
LDLIBS := -lm

# Melee's matching MSL trig implementation intentionally compiles its nested
# range-reduction and polynomial expressions to fmadds/fnmsubs (DOL
# 0x80326240). Preserve those operation boundaries without enabling
# contraction indiscriminately in the hosted core.
$(PPC_OBJ_DIR)/gameplay/src/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(PPC_OBJ_DIR)/src/melee_core/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast
$(NATIVE_OBJ_DIR)/gameplay/src/MSL/trigf.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -mfma
$(NATIVE_OBJ_DIR)/src/melee_core/runtime/math.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -mfma
$(NATIVE_OBJ_DIR)/src/melee_core/runtime/savestate.o: NATIVE_CPPFLAGS += -D_GNU_SOURCE
$(NATIVE_RUNTIME_CENSUS_OBJ): NATIVE_CPPFLAGS += -D_GNU_SOURCE
ifeq ($(NATIVE_GPROF),1)
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_CORE_GPROF
endif
ifeq ($(NATIVE_PHASE_PROFILE),1)
$(NATIVE_OBJ_DIR)/src/melee_core/api.o: NATIVE_CPPFLAGS += -DMSL_CORE_PHASE_PROFILE
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_CORE_PHASE_PROFILE
endif
ifeq ($(NATIVE_CALLGRIND),1)
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_CORE_CALLGRIND
endif
$(WASM_OBJ_DIR)/gameplay/src/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(WASM_OBJ_DIR)/src/melee_core/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast

ifeq ($(NATIVE_RELEASE_PROFILE),1)
NATIVE_RELEASE_OPT_OBJS := \
	$(NATIVE_OBJ_DIR)/gameplay/src/melee/lb/lbspdisplay.o \
	$(NATIVE_OBJ_DIR)/gameplay/src/melee/lb/lbvector.o \
	$(NATIVE_OBJ_DIR)/gameplay/src/sysdolphin/baselib/fobj.o \
	$(NATIVE_OBJ_DIR)/gameplay/src/sysdolphin/baselib/jobj.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/api.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/platform/dolphin_mtx.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/observation.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/relocation.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/savestate.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/viewer.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/wire.o \
	$(NATIVE_OBJ_DIR)/src/melee_core/runtime/main.o \
	$(NATIVE_OBJ_DIR)/generated/native_dat_layout.o \
	$(NATIVE_OBJ_DIR)/generated/match_reloc_layout.o \
	$(NATIVE_OBJ_DIR)/tests/melee_core/replay_bench.o
$(NATIVE_RELEASE_OPT_OBJS): override NATIVE_CFLAGS += -O3 $(RELEASE_ARCH_FLAGS)
endif

NATIVE_FLAGS_SIGNATURE := $(NATIVE_CPPFLAGS)|$(NATIVE_BASE_CFLAGS)|$(NATIVE_LINK_FLAGS)|$(NATIVE_RELEASE_PROFILE)|$(NATIVE_RELEASE_OPT_OBJS)

.PHONY: all runtime native native-release replay-benchmark-binary runtime-census large-batch-smoke benchmark-prepare benchmark-native benchmark-9950x3d-vcache-256 benchmark-9950x3d-vcache-512 benchmark-9950x3d-frequency-256 benchmark-9950x3d-frequency-512 wasm wasm-smoke viewer-smoke viewer-production-smoke viewer-schema viewer-schema-check lifecycle-benchmark source source-check validation validator validation-suite validation-supported-domain clean run run-native toolchain data-check smoke native-smoke map-smoke validation-smoke test FORCE

all: runtime

runtime: $(BINARY)

native: $(NATIVE_BINARY)

native-release:
	@case "$(NATIVE_RELEASE_CFLAGS) $(RELEASE_ARCH_FLAGS) $(NATIVE_LINK_FLAGS)" in \
		*Ofast*|*fast-math*|*unsafe-math*|*finite-math*|*associative-math*|*reciprocal-math*|*fp-contract=fast*) \
			echo "unsafe floating-point flag in strict release build" >&2; exit 2;; \
		*flto*) \
			echo "LTO is not validated for the source-shaped gameplay build" >&2; exit 2;; \
	esac
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" native replay-benchmark-binary \
		NATIVE_BUILD="$(NATIVE_RELEASE_BUILD)" \
		NATIVE_CFLAGS="$(NATIVE_RELEASE_CFLAGS)" NATIVE_RELEASE_PROFILE=1

replay-benchmark-binary: $(NATIVE_REPLAY_BENCH)

runtime-census: data-check $(NATIVE_RUNTIME_CENSUS)
	@"$(NATIVE_RUNTIME_CENSUS)" "$(DATA)"

large-batch-smoke: data-check $(NATIVE_LARGE_BATCH_SMOKE)
	@"$(NATIVE_LARGE_BATCH_SMOKE)" "$(DATA)" "$(LARGE_BATCH_MATCHES)"

wasm: $(WASM_MODULE)

viewer-schema: $(VIEWER_SCHEMA_JS)

FORCE:

$(NATIVE_FLAGS_STAMP): FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(NATIVE_FLAGS_SIGNATURE)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(NATIVE_COMPILE_OBJS): $(NATIVE_FLAGS_STAMP)

$(VIEWER_SCHEMA_TOOL): $(ROOT)/tools/melee_core/viewer_schema.c $(CORE)/runtime/wire.h
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -std=c11 -Wall -Wextra -I"$(CORE)" "$<" -o "$@"

$(VIEWER_SCHEMA_JS): $(VIEWER_SCHEMA_TOOL)
	@tmp="$@.tmp"; "$(VIEWER_SCHEMA_TOOL)" > "$$tmp"; \
		if ! cmp -s "$$tmp" "$@"; then mv "$$tmp" "$@"; else rm "$$tmp"; fi

viewer-schema-check: $(VIEWER_SCHEMA_TOOL)
	@tmp="$(NATIVE_BUILD)/schema.generated.js"; \
		"$(VIEWER_SCHEMA_TOOL)" > "$$tmp"; cmp "$$tmp" "$(VIEWER_SCHEMA_JS)"

source: source-check

source-check:
	@$(SOURCE_SYNC) check

validation: runtime validator

validator: $(VALIDATION_NATIVE)

$(VALIDATION_NATIVE): $(VALIDATION_NATIVE_SRC) $(CORE)/runtime/wire.h $(CORE)/runtime/benchmark_wire.h $(CORE)/runtime/item_projection.h
	@mkdir -p "$(@D)"
	@PY_INCLUDE="$$($(PY) -c 'import sysconfig; print(sysconfig.get_path("include"))')"; \
		"$(HOST_CC)" -O3 -std=c11 -fPIC -shared -Wall -Wextra \
			-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -I"$$PY_INCLUDE" \
			-I"$(CORE)" "$<" -lm -o "$@"

toolchain: $(TOOLCHAIN_STAMP)

$(TOOLCHAIN_STAMP): $(ROOT)/tools/melee_core/setup_ppc32_toolchain.sh
	@$(ROOT)/tools/melee_core/setup_ppc32_toolchain.sh

$(PPC_UPSTREAM_OBJS): $(PPC_OBJ_DIR)/gameplay/%.o: $(SOURCE_TREE)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$(SOURCE_TREE)/$*.c" -o "$@"

$(PPC_OBJ_DIR)/%.o: $(ROOT)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$<" -o "$@"

$(BINARY): $(PPC_OBJS)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

$(NATIVE_UPSTREAM_OBJS): $(NATIVE_OBJ_DIR)/gameplay/%.o: $(SOURCE_TREE)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP \
		-c "$(SOURCE_TREE)/$*.c" -o "$@"

$(NATIVE_OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_UPSTREAM_OBJS): $(WASM_OBJ_DIR)/gameplay/%.o: $(SOURCE_TREE)/%.c
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP \
		-c "$(SOURCE_TREE)/$*.c" -o "$@"

$(WASM_OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_OBJ_DIR)/gameplay/src/melee/ft/ftaction.o \
$(WASM_OBJ_DIR)/gameplay/src/melee/it/itanimlist.o \
$(WASM_OBJ_DIR)/gameplay/src/melee/lb/lbcommand.o: $(WASM_COMMAND_FIELDS)

$(NATIVE_DAT_PPC_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC) $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) -MMD -MP -g -gdwarf-4 \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(NATIVE_DAT_NATIVE_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(WASM_COMMAND_FIELDS): $(NATIVE_DAT_PPC_TYPES_OBJ) $(COMMAND_FIELDS_GENERATOR)
	@mkdir -p "$(@D)"
	@"$(PY)" "$(COMMAND_FIELDS_GENERATOR)" \
		--object "$(NATIVE_DAT_PPC_TYPES_OBJ)" --output "$@"

$(NATIVE_DAT_LAYOUT_SRC): $(NATIVE_DAT_PPC_TYPES_OBJ) $(NATIVE_DAT_NATIVE_TYPES_OBJ) $(NATIVE_DAT_LAYOUT_GENERATOR)
	@"$(PY)" "$(NATIVE_DAT_LAYOUT_GENERATOR)" \
		--ppc "$(NATIVE_DAT_PPC_TYPES_OBJ)" \
		--native "$(NATIVE_DAT_NATIVE_TYPES_OBJ)" --output "$@"

$(NATIVE_DAT_LAYOUT_OBJ): $(NATIVE_DAT_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_DAT_NATIVE_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -m32 -malign-double -I"$(WASM_LAYOUT_INCLUDE)" \
		-isystem /usr/include/x86_64-linux-gnu $(WASM_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(WASM_DAT_LAYOUT_SRC): $(NATIVE_DAT_PPC_TYPES_OBJ) $(WASM_DAT_NATIVE_TYPES_OBJ) $(NATIVE_DAT_LAYOUT_GENERATOR)
	@"$(PY)" "$(NATIVE_DAT_LAYOUT_GENERATOR)" \
		--ppc "$(NATIVE_DAT_PPC_TYPES_OBJ)" \
		--native "$(WASM_DAT_NATIVE_TYPES_OBJ)" --output "$@"

$(WASM_DAT_LAYOUT_OBJ): $(WASM_DAT_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(NATIVE_MATCH_RELOC_TYPES_OBJ): $(MATCH_RELOC_TYPES_SRC) $(MATCH_RELOC_TYPES_DEF)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(NATIVE_MATCH_RELOC_LAYOUT_SRC): $(NATIVE_MATCH_RELOC_TYPES_OBJ) $(MATCH_RELOC_TYPES_DEF) $(MATCH_RELOC_GENERATOR)
	@"$(PY)" "$(MATCH_RELOC_GENERATOR)" --object "$(NATIVE_MATCH_RELOC_TYPES_OBJ)" \
		--types "$(MATCH_RELOC_TYPES_DEF)" --output "$@"

$(NATIVE_MATCH_RELOC_LAYOUT_OBJ): $(NATIVE_MATCH_RELOC_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_MATCH_RELOC_TYPES_OBJ): $(MATCH_RELOC_TYPES_SRC) $(MATCH_RELOC_TYPES_DEF)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -m32 -malign-double -I"$(WASM_LAYOUT_INCLUDE)" \
		-isystem /usr/include/x86_64-linux-gnu $(WASM_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(WASM_MATCH_RELOC_LAYOUT_SRC): $(WASM_MATCH_RELOC_TYPES_OBJ) $(MATCH_RELOC_TYPES_DEF) $(MATCH_RELOC_GENERATOR)
	@"$(PY)" "$(MATCH_RELOC_GENERATOR)" --object "$(WASM_MATCH_RELOC_TYPES_OBJ)" \
		--types "$(MATCH_RELOC_TYPES_DEF)" --output "$@"

$(WASM_MATCH_RELOC_LAYOUT_OBJ): $(WASM_MATCH_RELOC_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(PPC_MATCH_RELOC_TYPES_OBJ): $(MATCH_RELOC_TYPES_SRC) $(MATCH_RELOC_TYPES_DEF) $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(PPC_MATCH_RELOC_LAYOUT_SRC): $(PPC_MATCH_RELOC_TYPES_OBJ) $(MATCH_RELOC_TYPES_DEF) $(MATCH_RELOC_GENERATOR)
	@"$(PY)" "$(MATCH_RELOC_GENERATOR)" --object "$(PPC_MATCH_RELOC_TYPES_OBJ)" \
		--types "$(MATCH_RELOC_TYPES_DEF)" --output "$@"

$(PPC_MATCH_RELOC_LAYOUT_OBJ): $(PPC_MATCH_RELOC_LAYOUT_SRC) $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(NATIVE_BINARY): $(NATIVE_OBJS)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -no-pie $(NATIVE_LINK_FLAGS) -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

$(WASM_MODULE): $(WASM_CORE_OBJS)
	@mkdir -p "$(@D)"
	@log="$(WASM_BUILD)/link-warnings.log"; other="$(WASM_BUILD)/link-other-warnings.log"; \
	if ! "$(EMCC)" -O2 $(WASM_LINK_FLAGS) --no-entry -Wl,--gc-sections $^ $(LDLIBS) \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createMslCoreModule \
		-sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
		-sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=1073741824 \
		-sEXPORTED_FUNCTIONS='["_malloc","_free","_msl_core_game_data_create","_msl_core_game_data_destroy","_msl_core_batch_create","_msl_core_batch_destroy","_msl_core_batch_match_count","_msl_core_batch_reset_matches","_msl_core_batch_step_matches","_msl_core_batch_write_state","_msl_core_batch_write_observation","_msl_core_batch_write_terminal","_msl_core_batch_write_viewer","_msl_core_batch_copy_matches","_msl_core_batch_match_save_size","_msl_core_batch_save_match","_msl_core_batch_restore_match"]' \
		-sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
		--preload-file "$(DATA)@/data/raw" -o "$@" 2>"$$log"; then \
		cat "$$log" >&2; exit 1; \
	fi; \
	awk 'BEGIN { RS=""; ORS="\n\n" } !/warning: function signature mismatch/' \
		"$$log" >"$$other"; \
	count="$$(rg -c 'warning: function signature mismatch' "$$log" 2>/dev/null || true)"; \
	if [ "$${count:-0}" -gt 0 ]; then \
		echo "wasm link: $$count expected signature warnings from loud unsupported stubs (details: $$log)" >&2; \
	fi; \
	if [ -s "$$other" ]; then cat "$$other" >&2; fi

define link_smoke
$(1): $(PPC_CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
endef

define link_native_smoke
$(1): $(NATIVE_CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@"$(HOST_CC)" -no-pie $(NATIVE_LINK_FLAGS) -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
endef

$(eval $(call link_smoke,$(ARCHIVE_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/archive_smoke.o))
$(eval $(call link_smoke,$(DATA_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/data_load_smoke.o))
$(eval $(call link_smoke,$(MAP_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/map_collision_smoke.o))
$(eval $(call link_smoke,$(MODEL_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/model_animation_smoke.o))
$(eval $(call link_smoke,$(SCHEDULER_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/scheduler_smoke.o))
$(eval $(call link_smoke,$(SCALAR_API_SMOKE),$(PPC_OBJ_DIR)/tests/melee_core/scalar_api_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_DATA_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/data_load_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_MAP_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/map_collision_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_MODEL_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/model_animation_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_SCHEDULER_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/scheduler_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_SCALAR_API_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/scalar_api_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_CONTEXT_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/context_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_BATCH_API_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/batch_api_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_WASM_PARITY),$(NATIVE_OBJ_DIR)/tests/melee_core/wasm_parity.o))
$(eval $(call link_native_smoke,$(NATIVE_LIFECYCLE_BENCH),$(NATIVE_OBJ_DIR)/tests/melee_core/lifecycle_bench.o))
$(eval $(call link_native_smoke,$(NATIVE_REPLAY_BENCH),$(NATIVE_REPLAY_BENCH_OBJ)))

$(NATIVE_RUNTIME_CENSUS): $(NATIVE_CORE_OBJS) $(NATIVE_RUNTIME_CENSUS_OBJ)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -no-pie -Wl,--gc-sections $^ \
		$(LDLIBS) -ldl -o "$@"

$(eval $(call link_native_smoke,$(NATIVE_LARGE_BATCH_SMOKE),$(NATIVE_LARGE_BATCH_SMOKE_OBJ)))

data-check:
	@$(PY) -m tools.extraction.validate_raw_data --raw-dir "$(DATA)"

smoke: data-check $(ARCHIVE_SMOKE) $(DATA_SMOKE) $(MAP_SMOKE) $(MODEL_SMOKE) $(SCHEDULER_SMOKE) $(SCALAR_API_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(ARCHIVE_SMOKE)" \
		"$(DATA)/PlCo.dat" \
		"$(DATA)/PlFx.dat" \
		"$(DATA)/PlFxNr.dat" \
		"$(DATA)/GrNLa.dat"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(DATA_SMOKE)" "$(DATA)"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MODEL_SMOKE)" "$(DATA)"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" "$(DATA)"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCHEDULER_SMOKE)"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCALAR_API_SMOKE)" \
		"$(DATA)"

native-smoke: data-check $(NATIVE_DATA_SMOKE) $(NATIVE_MAP_SMOKE) $(NATIVE_MODEL_SMOKE) $(NATIVE_SCHEDULER_SMOKE) $(NATIVE_SCALAR_API_SMOKE) $(NATIVE_CONTEXT_SMOKE) $(NATIVE_BATCH_API_SMOKE)
	@timeout 5s "$(NATIVE_DATA_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_MODEL_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_MAP_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_SCHEDULER_SMOKE)"
	@timeout 5s "$(NATIVE_SCALAR_API_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_CONTEXT_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_BATCH_API_SMOKE)" "$(DATA)"

wasm-smoke: data-check $(WASM_MODULE) $(NATIVE_WASM_PARITY)
	@MSL_CORE_NATIVE_DIGEST="$$($(NATIVE_WASM_PARITY) "$(DATA)")" \
		node "$(ROOT)/tests/melee_core/wasm_api_smoke.mjs" "$(WASM_MODULE)"

viewer-smoke: wasm-smoke viewer-schema-check
	+@MSL_DATA_DIR="$(abspath $(MSL_DATA_DIR))" \
		"$(ROOT)/tools/viewer/live/build_wasm.sh" >/dev/null
	@node "$(ROOT)/tests/melee_core/viewer_wasm_smoke.mjs"
	@node "$(ROOT)/tests/melee_core/viewer_browser_smoke.mjs"

viewer-production-smoke: viewer-smoke
	+@"$(ROOT)/tools/viewer/build.sh" >/dev/null
	@node "$(ROOT)/tests/melee_core/viewer_browser_smoke.mjs" --production

lifecycle-benchmark: data-check $(NATIVE_LIFECYCLE_BENCH)
	@timeout 5s "$(NATIVE_LIFECYCLE_BENCH)" "$(DATA)"

benchmark-prepare: validator
	@"$(PY)" -m tools.melee_core.prepare_replay_benchmark \
		--suite "$(VALIDATION_SUITE)" --output "$(BENCHMARK_MANIFEST)" \
		--characters "$(VALIDATION_CHARACTERS)" --stages "$(VALIDATION_STAGES)"

benchmark-native: data-check benchmark-prepare native-release
	@taskset -c "$(BENCHMARK_CPU)" "$(NATIVE_RELEASE_REPLAY_BENCH)" \
		"$(DATA)" "$(BENCHMARK_MANIFEST)" --matches "$(BENCHMARK_MATCHES)" \
		--resident-matches "$(BENCHMARK_RESIDENT_MATCHES)" \
		--match-frames "$(BENCHMARK_MATCH_FRAMES)" \
		--warmup-ticks "$(BENCHMARK_WARMUP_TICKS)"

benchmark-9950x3d-vcache-256:
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" benchmark-native \
		BENCHMARK_CPU=0 BENCHMARK_MATCHES=256

benchmark-9950x3d-vcache-512:
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" benchmark-native \
		BENCHMARK_CPU=0 BENCHMARK_MATCHES=512

benchmark-9950x3d-frequency-256:
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" benchmark-native \
		BENCHMARK_CPU=8 BENCHMARK_MATCHES=256

benchmark-9950x3d-frequency-512:
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" benchmark-native \
		BENCHMARK_CPU=8 BENCHMARK_MATCHES=512

map-smoke: data-check $(MAP_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" \
		"$(DATA)"

validation-smoke: validation native
	@timeout 25s env OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
		"$(PY)" -m pytest -q "$(ROOT)/tests/test_melee_core_validation.py"

validation-suite:
	@MSL_DATA_DIR="$(abspath $(MSL_DATA_DIR))" "$(PY)" -m tools.melee_core.validate_replay \
		--suite "$(VALIDATION_SUITE)" \
		--characters "$(VALIDATION_CHARACTERS)" --stages "$(VALIDATION_STAGES)" \
		--backend "$(VALIDATION_BACKEND)" --workers "$(VALIDATION_WORKERS)" \
		--build-jobs "$(VALIDATION_BUILD_JOBS)" --frames "$(VALIDATION_FRAMES)" \
		--timing $(VALIDATION_ARGS)

validation-supported-domain:
	@$(MAKE) --no-print-directory -f "$(CORE)/Makefile" validation-suite \
		VALIDATION_SUITE=replays/suites/melee_core_aggregate.json

test: source-check smoke native-smoke validation-smoke

# Example:
# make -f src/melee_core/Makefile run CONFIG=... INPUT=... OUTPUT=... PREV=-
PREV ?= -
run: data-check $(BINARY)
	@test -n "$(CONFIG)" -a -n "$(INPUT)" -a -n "$(OUTPUT)" || \
		(echo 'CONFIG, INPUT, and OUTPUT are required' >&2; exit 2)
	@"$(QEMU)" -L "$(QEMU_SYSROOT)" "$(BINARY)" \
		"$(DATA)" "$(CONFIG)" "$(PREV)" "$(INPUT)" "$(OUTPUT)"

run-native: data-check $(NATIVE_BINARY)
	@test -n "$(CONFIG)" -a -n "$(INPUT)" -a -n "$(OUTPUT)" || \
		(echo 'CONFIG, INPUT, and OUTPUT are required' >&2; exit 2)
	@"$(NATIVE_BINARY)" "$(DATA)" "$(CONFIG)" "$(PREV)" "$(INPUT)" "$(OUTPUT)"

clean:
	@rm -rf "$(PPC_BUILD)" "$(NATIVE_BUILD)" "$(VALIDATION_BUILD)"

-include $(DEPS)
