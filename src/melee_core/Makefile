# Isolated PPC reference build for the source-shaped Melee core. It does not
# include the repository's existing production extension. The validator asks
# the selected Python for its include directory once at build time.

.DELETE_ON_ERROR:

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
CORE := $(ROOT)/src/melee_core
BUILD_ROOT := $(ROOT)/build/melee_core
PPC_BUILD := $(BUILD_ROOT)/ppc
VALIDATION_BUILD := $(BUILD_ROOT)/validation
SOURCE_TREE := $(BUILD_ROOT)/source
SOURCE_STAMP := $(SOURCE_TREE)/.materialized
OBJ_DIR := $(PPC_BUILD)/obj
TOOLCHAIN_ROOT := $(BUILD_ROOT)/toolchain/root
TOOLCHAIN_STAMP := $(BUILD_ROOT)/toolchain/ready.stamp
CC := $(TOOLCHAIN_ROOT)/usr/bin/powerpc-linux-gnu-gcc-13
QEMU := $(TOOLCHAIN_ROOT)/usr/bin/qemu-ppc-static
SYSROOT := $(TOOLCHAIN_ROOT)
QEMU_SYSROOT := $(TOOLCHAIN_ROOT)/usr/powerpc-linux-gnu
PY ?= $(ROOT)/.venv/bin/python
HOST_CC ?= cc

SOURCE_SYNC := $(ROOT)/tools/melee_core/source_sync.sh
VENDOR_FILES := $(shell find $(CORE)/vendor -type f -print | LC_ALL=C sort)
PATCH_FILES := $(shell find $(CORE)/patches -type f -print | LC_ALL=C sort)
UPSTREAM_REL_SRCS := $(shell find $(CORE)/vendor/src -type f -name '*.c' -printf '%P\n' | LC_ALL=C sort)
UPSTREAM_REL_SRCS := $(filter-out \
	melee/ft/chara/ftCommon/ftCo_0A01.c \
	melee/it/it_279C.c, \
	$(UPSTREAM_REL_SRCS))
UPSTREAM_REL_SRCS := $(filter-out melee/it/items/%.c,$(UPSTREAM_REL_SRCS))
UPSTREAM_REL_SRCS += \
	melee/it/items/itfoxblaster.c \
	melee/it/items/itfoxillusion.c \
	melee/it/items/itfoxlaser.c
UPSTREAM_SRCS := $(addprefix $(SOURCE_TREE)/src/,$(UPSTREAM_REL_SRCS))
UPSTREAM_OBJS := $(patsubst $(SOURCE_TREE)/%.c,$(OBJ_DIR)/vendor/%.o,$(UPSTREAM_SRCS))

PLATFORM_SRCS := \
	$(CORE)/platform/dolphin_mtx.c \
	$(CORE)/platform/files.c \
	$(CORE)/platform/memory.c \
	$(CORE)/platform/os_report.c \
	$(CORE)/platform/platform_state.c
RUNTIME_SRCS := \
	$(CORE)/runtime/camera.c \
	$(CORE)/runtime/common.c \
	$(CORE)/runtime/effects.c \
	$(CORE)/runtime/final_destination.c \
	$(CORE)/runtime/ftco_0A01.c \
	$(CORE)/runtime/items.c \
	$(CORE)/runtime/match.c \
	$(CORE)/runtime/math.c \
	$(CORE)/runtime/wire.c
STUB_SRCS := \
	$(CORE)/stubs/headless_exclusions.c \
	$(CORE)/stubs/unreached_presentation.c \
	$(CORE)/stubs/unresolved_abort.c
LOCAL_SRCS := $(PLATFORM_SRCS) $(RUNTIME_SRCS) $(STUB_SRCS)
LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(LOCAL_SRCS))
CORE_OBJS := $(UPSTREAM_OBJS) $(LOCAL_OBJS)
MAIN_SRC := $(CORE)/runtime/main.c
MAIN_OBJ := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(MAIN_SRC))
OBJS := $(CORE_OBJS) $(MAIN_OBJ)
SMOKE_SRCS := \
	$(ROOT)/tests/melee_core/archive_smoke.c \
	$(ROOT)/tests/melee_core/data_load_smoke.c \
	$(ROOT)/tests/melee_core/map_collision_smoke.c \
	$(ROOT)/tests/melee_core/model_animation_smoke.c \
	$(ROOT)/tests/melee_core/scheduler_smoke.c
SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(SMOKE_SRCS))
DEPS := $(OBJS:.o=.d) $(SMOKE_OBJS:.o=.d)

BINARY := $(PPC_BUILD)/melee-core-ppc
ARCHIVE_SMOKE := $(PPC_BUILD)/archive-smoke
DATA_SMOKE := $(PPC_BUILD)/data-smoke
MAP_SMOKE := $(PPC_BUILD)/map-collision-smoke
MODEL_SMOKE := $(PPC_BUILD)/model-animation-smoke
SCHEDULER_SMOKE := $(PPC_BUILD)/scheduler-smoke
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
CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -w
LDLIBS := -lm

# Melee's matching MSL trig implementation intentionally compiles its nested
# range-reduction and polynomial expressions to fmadds/fnmsubs (DOL
# 0x80326240). Preserve those operation boundaries without enabling
# contraction indiscriminately in the hosted core.
$(OBJ_DIR)/vendor/src/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(OBJ_DIR)/src/melee_core/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast

.PHONY: all runtime source source-check validation validator clean run toolchain smoke map-smoke validation-smoke test

all: runtime

runtime: $(BINARY)

source: $(SOURCE_STAMP)

source-check:
	@$(SOURCE_SYNC) check

validation: runtime validator

validator: $(VALIDATION_NATIVE)

$(SOURCE_STAMP): $(VENDOR_FILES) $(PATCH_FILES) $(CORE)/upstream-files.txt $(CORE)/upstream.lock $(SOURCE_SYNC)
	@$(SOURCE_SYNC) materialize "$(SOURCE_TREE)"

$(VALIDATION_NATIVE): $(VALIDATION_NATIVE_SRC) $(CORE)/runtime/wire.h
	@mkdir -p "$(@D)"
	@PY_INCLUDE="$$($(PY) -c 'import sysconfig; print(sysconfig.get_path("include"))')"; \
		"$(HOST_CC)" -O3 -std=c11 -fPIC -shared -Wall -Wextra \
			-D_POSIX_C_SOURCE=200809L -I"$$PY_INCLUDE" \
			-I"$(CORE)" "$<" -lm -o "$@"

toolchain: $(TOOLCHAIN_STAMP)

$(TOOLCHAIN_STAMP): $(ROOT)/tools/melee_core/setup_ppc32_toolchain.sh
	@$(ROOT)/tools/melee_core/setup_ppc32_toolchain.sh

$(UPSTREAM_SRCS): | $(SOURCE_STAMP)
	@test -f "$@"

$(UPSTREAM_OBJS): $(OBJ_DIR)/vendor/%.o: $(SOURCE_TREE)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$(SOURCE_TREE)/$*.c" -o "$@"

$(OBJ_DIR)/%.o: $(ROOT)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$<" -o "$@"

$(BINARY): $(OBJS)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

define link_smoke
$(1): $(CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu:$$$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
endef

$(eval $(call link_smoke,$(ARCHIVE_SMOKE),$(OBJ_DIR)/tests/melee_core/archive_smoke.o))
$(eval $(call link_smoke,$(DATA_SMOKE),$(OBJ_DIR)/tests/melee_core/data_load_smoke.o))
$(eval $(call link_smoke,$(MAP_SMOKE),$(OBJ_DIR)/tests/melee_core/map_collision_smoke.o))
$(eval $(call link_smoke,$(MODEL_SMOKE),$(OBJ_DIR)/tests/melee_core/model_animation_smoke.o))
$(eval $(call link_smoke,$(SCHEDULER_SMOKE),$(OBJ_DIR)/tests/melee_core/scheduler_smoke.o))

smoke: $(ARCHIVE_SMOKE) $(DATA_SMOKE) $(MAP_SMOKE) $(MODEL_SMOKE) $(SCHEDULER_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(ARCHIVE_SMOKE)" \
		"$(ROOT)/refs/melee-disc/files/PlCo.dat" \
		"$(ROOT)/refs/melee-disc/files/PlFx.dat" \
		"$(ROOT)/refs/melee-disc/files/PlFxNr.dat" \
		"$(ROOT)/refs/melee-disc/files/GrNLa.dat"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(DATA_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MODEL_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCHEDULER_SMOKE)"

map-smoke: $(MAP_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" \
		"$(ROOT)/refs/melee-disc/files"

validation-smoke: validation
	@timeout 25s env OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
		"$(PY)" -m pytest -q "$(ROOT)/tests/test_melee_core_validation.py"

test: source-check smoke validation-smoke

# Example:
# make -f src/melee_core/Makefile run CONFIG=... INPUT=... OUTPUT=... PREV=-
PREV ?= -
DATA ?= $(ROOT)/refs/melee-disc/files
run: $(BINARY)
	@test -n "$(CONFIG)" -a -n "$(INPUT)" -a -n "$(OUTPUT)" || \
		(echo 'CONFIG, INPUT, and OUTPUT are required' >&2; exit 2)
	@"$(QEMU)" -L "$(QEMU_SYSROOT)" "$(BINARY)" \
		"$(DATA)" "$(CONFIG)" "$(PREV)" "$(INPUT)" "$(OUTPUT)"

clean:
	@rm -rf "$(PPC_BUILD)" "$(VALIDATION_BUILD)" "$(SOURCE_TREE)"

-include $(DEPS)
