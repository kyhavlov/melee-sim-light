# Isolated Phase 1 build. It does not include the repository's main
# Python/native-extension Makefile. The optional validation-native target asks
# the selected Python for its include directory once at build time.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD := $(ROOT)/build/decomp_port/phase1
VALIDATION_BUILD := $(ROOT)/build/decomp_port/validation
OBJ_DIR := $(BUILD)/obj
TOOLCHAIN_ROOT := $(ROOT)/build/decomp_port/ppc-toolchain/root
TOOLCHAIN_STAMP := $(ROOT)/build/decomp_port/ppc-toolchain/ready.stamp
CC := $(TOOLCHAIN_ROOT)/usr/bin/powerpc-linux-gnu-gcc-13
QEMU := $(TOOLCHAIN_ROOT)/usr/bin/qemu-ppc-static
SYSROOT := $(TOOLCHAIN_ROOT)
QEMU_SYSROOT := $(TOOLCHAIN_ROOT)/usr/powerpc-linux-gnu
PY ?= $(ROOT)/.venv/bin/python
HOST_CC ?= cc

UPSTREAM_SRCS := $(shell find $(ROOT)/src/decomp_port/upstream/src -type f -name '*.c' -print | LC_ALL=C sort)
HOST_SRCS := \
	$(ROOT)/src/decomp_port/host/dolphin_mtx.c \
	$(ROOT)/src/decomp_port/host/files.c \
	$(ROOT)/src/decomp_port/host/memory.c \
	$(ROOT)/src/decomp_port/host/os_report.c \
	$(ROOT)/src/decomp_port/host/phase1_motion_states.c \
	$(ROOT)/src/decomp_port/host/platform_state.c \
	$(ROOT)/src/decomp_port/host/wire.c
STUB_SRCS := \
	$(ROOT)/src/decomp_port/stubs/phase1_exclusions.c \
	$(ROOT)/src/decomp_port/stubs/unreached_presentation.c \
	$(ROOT)/src/decomp_port/stubs/unresolved_abort.c
CORE_SRCS := $(UPSTREAM_SRCS) $(HOST_SRCS) $(STUB_SRCS)
MAIN_SRC := $(ROOT)/src/decomp_port/host/phase1_main.c
SRCS := $(CORE_SRCS) $(MAIN_SRC)
CORE_OBJS := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
OBJS := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
SMOKE_SRCS := \
	$(ROOT)/src/decomp_port/host/archive_probe.c \
	$(ROOT)/tests/decomp_port/data_load_smoke.c \
	$(ROOT)/tests/decomp_port/fighter_bootstrap_smoke.c \
	$(ROOT)/tests/decomp_port/map_collision_smoke.c \
	$(ROOT)/tests/decomp_port/model_animation_smoke.c \
	$(ROOT)/tests/decomp_port/scheduler_smoke.c
SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(SMOKE_SRCS))
DEPS := $(OBJS:.o=.d) $(SMOKE_OBJS:.o=.d)
BINARY := $(BUILD)/melee-decomp-port
ARCHIVE_SMOKE := $(BUILD)/archive-smoke
DATA_SMOKE := $(BUILD)/data-smoke
FIGHTER_SMOKE := $(BUILD)/fighter-smoke
MAP_SMOKE := $(BUILD)/map-collision-smoke
MODEL_SMOKE := $(BUILD)/model-animation-smoke
SCHEDULER_SMOKE := $(BUILD)/scheduler-smoke
VALIDATION_NATIVE_SRC := $(ROOT)/bindings/msl_decomp_validate.c
VALIDATION_NATIVE := $(VALIDATION_BUILD)/_msl_decomp_validate.so

CPPFLAGS := \
	-DBUGFIX \
	-include $(ROOT)/src/decomp_port/host/compat.h \
	-I$(ROOT)/src/decomp_port \
	-I$(ROOT)/src/decomp_port/host/include \
	-I$(ROOT)/src/decomp_port/upstream/src \
	-I$(ROOT)/src/decomp_port/upstream/src/melee \
	-I$(ROOT)/src/decomp_port/upstream/src/melee/ft/chara \
	-I$(ROOT)/src/decomp_port/upstream/src/sysdolphin \
	-I$(ROOT)/src/decomp_port/upstream/src/Runtime \
	-I$(ROOT)/src/decomp_port/upstream/extern/dolphin/include
CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -w
LDLIBS := -lm

.PHONY: all mvp validation validation-native clean run toolchain smoke map-smoke locomotion-smoke validation-smoke test

all: mvp

mvp: $(BINARY)

validation: mvp validation-native

validation-native: $(VALIDATION_NATIVE)

$(VALIDATION_NATIVE): $(VALIDATION_NATIVE_SRC) $(ROOT)/src/decomp_port/host/wire.h
	@mkdir -p "$(@D)"
	@PY_INCLUDE="$$($(PY) -c 'import sysconfig; print(sysconfig.get_path("include"))')"; \
		"$(HOST_CC)" -O3 -std=c11 -fPIC -shared -Wall -Wextra \
			-D_POSIX_C_SOURCE=200809L -I"$$PY_INCLUDE" \
			-I"$(ROOT)/src/decomp_port" "$<" -lm -o "$@"

toolchain: $(TOOLCHAIN_STAMP)

$(TOOLCHAIN_STAMP): $(ROOT)/tools/decomp_port/setup_ppc32_toolchain.sh
	@$(ROOT)/tools/decomp_port/setup_ppc32_toolchain.sh

$(OBJ_DIR)/%.o: $(ROOT)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$<" -o "$@"

$(BINARY): $(OBJS)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

define link_smoke
$(1): $(CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/usr/lib/x86_64-linux-gnu$$$${LD_LIBRARY_PATH:+:$$$$LD_LIBRARY_PATH}" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
endef

$(eval $(call link_smoke,$(ARCHIVE_SMOKE),$(OBJ_DIR)/src/decomp_port/host/archive_probe.o))
$(eval $(call link_smoke,$(DATA_SMOKE),$(OBJ_DIR)/tests/decomp_port/data_load_smoke.o))
$(eval $(call link_smoke,$(FIGHTER_SMOKE),$(OBJ_DIR)/tests/decomp_port/fighter_bootstrap_smoke.o))
$(eval $(call link_smoke,$(MAP_SMOKE),$(OBJ_DIR)/tests/decomp_port/map_collision_smoke.o))
$(eval $(call link_smoke,$(MODEL_SMOKE),$(OBJ_DIR)/tests/decomp_port/model_animation_smoke.o))
$(eval $(call link_smoke,$(SCHEDULER_SMOKE),$(OBJ_DIR)/tests/decomp_port/scheduler_smoke.o))

smoke: $(ARCHIVE_SMOKE) $(DATA_SMOKE) $(FIGHTER_SMOKE) $(MAP_SMOKE) $(MODEL_SMOKE) $(SCHEDULER_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(ARCHIVE_SMOKE)" \
		"$(ROOT)/refs/melee-disc/files/PlCo.dat" \
		"$(ROOT)/refs/melee-disc/files/PlFx.dat" \
		"$(ROOT)/refs/melee-disc/files/PlFxNr.dat" \
		"$(ROOT)/refs/melee-disc/files/GrNLa.dat"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(DATA_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MODEL_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" "$(ROOT)/refs/melee-disc/files"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCHEDULER_SMOKE)"
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(FIGHTER_SMOKE)" "$(ROOT)/refs/melee-disc/files"

map-smoke: $(MAP_SMOKE)
	@timeout 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" \
		"$(ROOT)/refs/melee-disc/files"

locomotion-smoke: $(BINARY)
	@timeout 25s env OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
		"$(PY)" "$(ROOT)/tests/decomp_port/phase1_locomotion_smoke.py"

validation-smoke: validation
	@timeout 25s env OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
		"$(PY)" -m pytest -q "$(ROOT)/tests/test_decomp_port_validation.py"

test: smoke locomotion-smoke validation-smoke

# Example:
# make -f src/decomp_port/Makefile run CONFIG=... INPUT=... OUTPUT=... PREV=-
PREV ?= -
DATA ?= $(ROOT)/refs/melee-disc/files
run: $(BINARY)
	@test -n "$(CONFIG)" -a -n "$(INPUT)" -a -n "$(OUTPUT)" || \
		(echo 'CONFIG, INPUT, and OUTPUT are required' >&2; exit 2)
	@"$(QEMU)" -L "$(QEMU_SYSROOT)" "$(BINARY)" \
		"$(DATA)" "$(CONFIG)" "$(PREV)" "$(INPUT)" "$(OUTPUT)"

clean:
	@rm -rf "$(BUILD)" "$(VALIDATION_BUILD)"

-include $(DEPS)
