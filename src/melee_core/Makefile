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
VALIDATION_BUILD := $(BUILD_ROOT)/validation
SOURCE_TREE := $(CORE)/gameplay
PPC_OBJ_DIR := $(PPC_BUILD)/obj
NATIVE_OBJ_DIR := $(NATIVE_BUILD)/obj
NATIVE_GENERATED_DIR := $(NATIVE_BUILD)/generated
TOOLCHAIN_ROOT := $(BUILD_ROOT)/toolchain/root
TOOLCHAIN_STAMP := $(BUILD_ROOT)/toolchain/ready.stamp
CC := $(TOOLCHAIN_ROOT)/usr/bin/powerpc-linux-gnu-gcc-13
QEMU := $(TOOLCHAIN_ROOT)/usr/bin/qemu-ppc-static
SYSROOT := $(TOOLCHAIN_ROOT)
QEMU_SYSROOT := $(TOOLCHAIN_ROOT)/usr/powerpc-linux-gnu
PY ?= $(ROOT)/.venv/bin/python
HOST_CC ?= cc
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

PLATFORM_SRCS := \
	$(CORE)/platform/dolphin_mtx.c \
	$(CORE)/platform/files.c \
	$(CORE)/platform/memory.c \
	$(CORE)/platform/os_report.c \
	$(CORE)/platform/platform_state.c \
	$(CORE)/platform/slippi.c
NATIVE_PLATFORM_SRCS := $(CORE)/platform/native_dat.c
RUNTIME_SRCS := \
	$(CORE)/runtime/audio.c \
	$(CORE)/runtime/camera.c \
	$(CORE)/runtime/common.c \
	$(CORE)/runtime/effects.c \
	$(CORE)/runtime/final_destination.c \
	$(CORE)/runtime/ftco_0A01.c \
	$(CORE)/runtime/items.c \
	$(CORE)/runtime/match.c \
	$(CORE)/runtime/math.c \
	$(CORE)/runtime/scalar.c \
	$(CORE)/runtime/wire.c
STUB_SRCS := \
	$(CORE)/stubs/headless_exclusions.c \
	$(CORE)/stubs/unreached_presentation.c \
	$(CORE)/stubs/unresolved_abort.c
LOCAL_SRCS := $(PLATFORM_SRCS) $(RUNTIME_SRCS) $(STUB_SRCS)
PPC_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(LOCAL_SRCS))
NATIVE_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(LOCAL_SRCS))
NATIVE_PLATFORM_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_PLATFORM_SRCS))
PPC_CORE_OBJS := $(PPC_UPSTREAM_OBJS) $(PPC_LOCAL_OBJS)
NATIVE_DAT_TYPES_SRC := $(ROOT)/tools/melee_core/native_dat_types.c
NATIVE_DAT_LAYOUT_GENERATOR := $(ROOT)/tools/melee_core/generate_native_dat_layout.py
NATIVE_DAT_PPC_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_ppc.o
NATIVE_DAT_NATIVE_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_native.o
NATIVE_DAT_LAYOUT_SRC := $(NATIVE_GENERATED_DIR)/native_dat_layout.c
NATIVE_DAT_LAYOUT_OBJ := $(NATIVE_OBJ_DIR)/generated/native_dat_layout.o
NATIVE_CORE_OBJS := $(NATIVE_UPSTREAM_OBJS) $(NATIVE_LOCAL_OBJS) \
	$(NATIVE_PLATFORM_OBJS) $(NATIVE_DAT_LAYOUT_OBJ)
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
	$(ROOT)/tests/melee_core/scheduler_smoke.c
NATIVE_SMOKE_SRCS := \
	$(ROOT)/tests/melee_core/data_load_smoke.c \
	$(ROOT)/tests/melee_core/map_collision_smoke.c \
	$(ROOT)/tests/melee_core/model_animation_smoke.c \
	$(ROOT)/tests/melee_core/scalar_api_smoke.c \
	$(ROOT)/tests/melee_core/scheduler_smoke.c
PPC_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(SMOKE_SRCS))
NATIVE_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_SMOKE_SRCS))
DEPS := $(PPC_OBJS:.o=.d) $(PPC_SMOKE_OBJS:.o=.d) $(NATIVE_OBJS:.o=.d) \
	$(NATIVE_SMOKE_OBJS:.o=.d) $(NATIVE_DAT_PPC_TYPES_OBJ:.o=.d) \
	$(NATIVE_DAT_NATIVE_TYPES_OBJ:.o=.d)

BINARY := $(PPC_BUILD)/melee-core-ppc
NATIVE_BINARY := $(NATIVE_BUILD)/melee-core-native
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
CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -w
NATIVE_CFLAGS = $(CFLAGS) -fno-pie
LDLIBS := -lm

# Melee's matching MSL trig implementation intentionally compiles its nested
# range-reduction and polynomial expressions to fmadds/fnmsubs (DOL
# 0x80326240). Preserve those operation boundaries without enabling
# contraction indiscriminately in the hosted core.
$(PPC_OBJ_DIR)/gameplay/src/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(PPC_OBJ_DIR)/src/melee_core/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast
$(NATIVE_OBJ_DIR)/gameplay/src/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast -mfma
$(NATIVE_OBJ_DIR)/src/melee_core/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast -mfma

.PHONY: all runtime native source source-check validation validator validation-suite validation-supported-domain clean run run-native toolchain data-check smoke native-smoke map-smoke validation-smoke test

all: runtime

runtime: $(BINARY)

native: $(NATIVE_BINARY)

source: source-check

source-check:
	@$(SOURCE_SYNC) check

validation: runtime validator

validator: $(VALIDATION_NATIVE)

$(VALIDATION_NATIVE): $(VALIDATION_NATIVE_SRC) $(CORE)/runtime/wire.h
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

$(NATIVE_DAT_LAYOUT_SRC): $(NATIVE_DAT_PPC_TYPES_OBJ) $(NATIVE_DAT_NATIVE_TYPES_OBJ) $(NATIVE_DAT_LAYOUT_GENERATOR)
	@"$(PY)" "$(NATIVE_DAT_LAYOUT_GENERATOR)" \
		--ppc "$(NATIVE_DAT_PPC_TYPES_OBJ)" \
		--native "$(NATIVE_DAT_NATIVE_TYPES_OBJ)" --output "$@"

$(NATIVE_DAT_LAYOUT_OBJ): $(NATIVE_DAT_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(NATIVE_BINARY): $(NATIVE_OBJS)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -no-pie -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

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
	@"$(HOST_CC)" -no-pie -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
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

native-smoke: data-check $(NATIVE_DATA_SMOKE) $(NATIVE_MAP_SMOKE) $(NATIVE_MODEL_SMOKE) $(NATIVE_SCHEDULER_SMOKE) $(NATIVE_SCALAR_API_SMOKE)
	@timeout 5s "$(NATIVE_DATA_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_MODEL_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_MAP_SMOKE)" "$(DATA)"
	@timeout 5s "$(NATIVE_SCHEDULER_SMOKE)"
	@timeout 5s "$(NATIVE_SCALAR_API_SMOKE)" "$(DATA)"

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
