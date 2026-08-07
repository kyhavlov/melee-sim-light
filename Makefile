# Canonical PPC reference, native, Python shared-library, and Wasm builds for
# the source-shaped simulator.

.DELETE_ON_ERROR:

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CORE := $(ROOT)/src
BUILD_ROOT := $(ROOT)/build/melee_core
PPC_BUILD := $(BUILD_ROOT)/ppc
NATIVE_BUILD := $(BUILD_ROOT)/native
NATIVE_RELEASE_BUILD := $(BUILD_ROOT)/native-release
SUBSYSTEM_PROFILE_BUILD := $(BUILD_ROOT)/subsystem-profile
WASM_BUILD := $(BUILD_ROOT)/wasm
PYTHON_BUILD := $(BUILD_ROOT)/python
VALIDATION_BUILD := $(BUILD_ROOT)/validation
PPC_OBJ_DIR := $(PPC_BUILD)/obj
NATIVE_OBJ_DIR := $(NATIVE_BUILD)/obj
WASM_OBJ_DIR := $(WASM_BUILD)/obj
PYTHON_OBJ_DIR := $(PYTHON_BUILD)/obj
NATIVE_GENERATED_DIR := $(NATIVE_BUILD)/generated
WASM_GENERATED_DIR := $(WASM_BUILD)/generated
WASM_LAYOUT_INCLUDE := $(WASM_GENERATED_DIR)/layout-include
WASM_GLIBC_STUB := $(WASM_LAYOUT_INCLUDE)/gnu/stubs-32.h
PPC_GENERATED_DIR := $(PPC_BUILD)/generated
TOOLCHAIN_ROOT := $(BUILD_ROOT)/toolchain/root
TOOLCHAIN_STAMP := $(BUILD_ROOT)/toolchain/ready.stamp
# Host-arch shared libraries the cross compiler itself links against. The
# toolchain tree is populated for whichever architecture the host (or, on
# macOS, the container) runs; see tools/build/host_arch.sh.
TOOLCHAIN_MULTIARCH := $(shell $(ROOT)/tools/build/host_arch.sh multiarch)
TOOLCHAIN_LIBDIR := $(TOOLCHAIN_ROOT)/usr/lib/$(TOOLCHAIN_MULTIARCH)
CC := $(TOOLCHAIN_ROOT)/usr/bin/powerpc-linux-gnu-gcc-13
QEMU := $(TOOLCHAIN_ROOT)/usr/bin/qemu-ppc-static
SYSROOT := $(TOOLCHAIN_ROOT)
QEMU_SYSROOT := $(TOOLCHAIN_ROOT)/usr/powerpc-linux-gnu
PY ?= $(ROOT)/.venv/bin/python
HOST_CC ?= gcc
EMCC ?= emcc

# Host portability. macOS cannot execute the Linux cross toolchain, has no
# GNU ld (different link flags), and no coreutils timeout.
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
HOST_TARGET_ARCH := $(UNAME_M)
ifeq ($(UNAME_S),Darwin)
CC := $(ROOT)/tools/build/ppc32_cc.sh
QEMU := $(ROOT)/tools/build/qemu_ppc.sh
TIMEOUT := $(ROOT)/tools/build/portable_timeout.sh
# The runtime builds for the host architecture by default. Source-width file
# addresses and HSD ids are deterministic arena-offset tokens, so no sub-4 GiB
# mappings are needed and arm64 macOS' 4 GiB __PAGEZERO floor is irrelevant.
# Override
# HOST_TARGET_ARCH=x86_64 on Apple Silicon to build the Rosetta 2 profile
# that targets the gate architecture. Neither macOS profile is
# gate-authoritative: recorded suite identities come from linux/amd64
# GNU-toolchain builds (CI or a Linux container), and Apple clang codegen
# has no recorded bit-exact equivalence to them.
HOST_ARCH_FLAGS := -arch $(HOST_TARGET_ARCH)
NATIVE_EXE_LINK_FLAGS := $(HOST_ARCH_FLAGS) -Wl,-dead_strip
SHARED_LIB_LINK_FLAGS := $(HOST_ARCH_FLAGS) -shared -Wl,-dead_strip
# The validator extension loads into the driving Python, which may be a
# different architecture than the runtime (it only talks to the runtime
# over a pipe), so it builds for the host architecture.
PY_EXT_LINK_FLAGS := -undefined dynamic_lookup
# Homebrew LLVM's clang carries every backend; use it to cross-compile the
# PPC32 DWARF reference object directly instead of running the Linux GNU
# toolchain inside Docker. -nostdlibinc keeps macOS system headers out of
# the powerpc-linux target; clang's builtin headers cover the rest.
LLVM_CLANG := $(shell brew --prefix llvm 2>/dev/null)/bin/clang
ifneq ($(wildcard $(LLVM_CLANG)),)
PPC_TYPES_TOOLCHAIN_DEP :=
PPC_TYPES_COMPILE = "$(LLVM_CLANG)" --target=powerpc-unknown-linux-gnu \
	-nostdlibinc -isystem "$(ROOT)/tools/build/cross_libc_shim"
# The wasm32 layout proxy must be a Linux i386 object: Apple clang's -m32
# produces a Darwin i386 Mach-O whose struct layouts differ (notably
# -malign-double handling), which corrupts the wasm relocation tables.
WASM_PROXY_COMPILE = "$(LLVM_CLANG)" --target=i386-unknown-linux-gnu \
	-malign-double -nostdlibinc \
	-isystem "$(ROOT)/tools/build/cross_libc_shim" \
	-I"$(WASM_LAYOUT_INCLUDE)"
endif
else
TIMEOUT := timeout
NATIVE_EXE_LINK_FLAGS := -no-pie -Wl,--gc-sections
SHARED_LIB_LINK_FLAGS := -shared -Wl,--gc-sections -Wl,-Bsymbolic -Wl,-z,defs
PY_EXT_LINK_FLAGS :=
endif
PPC_TYPES_TOOLCHAIN_DEP ?= $(TOOLCHAIN_STAMP)
PPC_TYPES_COMPILE ?= env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
	LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
	"$(CC)" --sysroot="$(SYSROOT)"
WASM_PROXY_COMPILE ?= "$(HOST_CC)" -m32 -malign-double \
	-I"$(WASM_LAYOUT_INCLUDE)" -isystem /usr/include/x86_64-linux-gnu
ifeq ($(HOST_TARGET_ARCH),x86_64)
FMA_FLAGS := -mfma
else
FMA_FLAGS :=
endif
ISO ?=
VIEWER_HOST ?= 127.0.0.1
VIEWER_PORT ?= 8001
OPEN ?= 1
WASM_LINK_FLAGS ?= -sSTACK_SIZE=1048576
MSL_DATA_DIR ?= $(ROOT)/data
DATA ?= $(abspath $(MSL_DATA_DIR))/raw
VALIDATION_SUITE ?= replays/suites/melee_core_aggregate.json
VALIDATION_CHARACTERS ?= Fox,Falco,Marth,Captain Falcon,Sheik,Zelda,Jigglypuff,Peach,Luigi,Mario,Dr. Mario,Samus,Ice Climbers,Pikachu,Donkey Kong,Ganondorf,Yoshi,Ness,Link,Young Link
VALIDATION_STAGES ?= 32,31,3,2,8,28
VALIDATION_BACKEND ?= native
VALIDATION_WORKERS ?= 0
VALIDATION_BUILD_JOBS ?= 4
VALIDATION_FRAMES ?= 0
VALIDATION_ARGS ?=
BENCHMARK_MANIFEST ?= $(BUILD_ROOT)/benchmark/cases.tsv
BENCHMARK_CPU ?= 8
BENCHMARK_MATCHES ?= 256
BENCHMARK_RESIDENT_MATCHES ?= $(BENCHMARK_MATCHES)
BENCHMARK_MATCH_FRAMES ?= 65536
BENCHMARK_WARMUP_TICKS ?= 8
LARGE_BATCH_MATCHES ?= 256
ifeq ($(UNAME_S),Darwin)
RELEASE_ARCH_FLAGS ?=
else
RELEASE_ARCH_FLAGS ?= -march=native -mtune=native
endif

SOURCE_SYNC := $(ROOT)/tools/build/source_sync.sh
UPSTREAM_ROOTS := MSL MetroTRK Runtime melee sysdolphin
# Enumerate sources from git, not the filesystem: src/Runtime (MW library)
# and src/runtime (port runtime) are distinct in git but collapse into one
# directory on case-insensitive filesystems (macOS), where find/wildcard
# would return the union for both. Includes untracked files so new sources
# build before they are staged.
MSL_SRC_FILES := $(addprefix $(ROOT)/,$(filter %.c,\
	$(shell git -C $(ROOT) ls-files --cached --others --exclude-standard -- src 2>/dev/null)))
ifeq ($(strip $(MSL_SRC_FILES)),)
$(error source enumeration via git failed; build requires a git checkout)
endif
UPSTREAM_SRCS := $(sort $(filter \
	$(addsuffix /%,$(addprefix $(CORE)/,$(UPSTREAM_ROOTS))),$(MSL_SRC_FILES)))
PPC_UPSTREAM_OBJS := $(patsubst $(CORE)/%.c,$(PPC_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))
NATIVE_UPSTREAM_OBJS := $(patsubst $(CORE)/%.c,$(NATIVE_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))
WASM_UPSTREAM_OBJS := $(patsubst $(CORE)/%.c,$(WASM_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))
PYTHON_UPSTREAM_OBJS := $(patsubst $(CORE)/%.c,$(PYTHON_OBJ_DIR)/gameplay/%.o,$(UPSTREAM_SRCS))

# Directory ownership is the build boundary. The special host entry points
# below are linked only by the targets that consume them.
NATIVE_PLATFORM_SRCS := $(CORE)/platform/native_dat.c
PLATFORM_SRCS := $(filter-out $(NATIVE_PLATFORM_SRCS),$(filter $(CORE)/platform/%.c,$(MSL_SRC_FILES)))
RUNTIME_SRCS := $(filter-out $(CORE)/runtime/main.c,$(filter $(CORE)/runtime/%.c,$(MSL_SRC_FILES)))
STUB_SRCS := $(filter $(CORE)/stubs/%.c,$(MSL_SRC_FILES))
LOCAL_SRCS := $(CORE)/api.c $(PLATFORM_SRCS) $(RUNTIME_SRCS) $(STUB_SRCS)
PPC_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(LOCAL_SRCS))
NATIVE_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(LOCAL_SRCS))
WASM_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(WASM_OBJ_DIR)/%.o,$(LOCAL_SRCS))
PYTHON_LOCAL_SRCS := $(LOCAL_SRCS) $(NATIVE_PLATFORM_SRCS) $(CORE)/python_api.c
PYTHON_LOCAL_OBJS := $(patsubst $(ROOT)/%.c,$(PYTHON_OBJ_DIR)/%.o,$(PYTHON_LOCAL_SRCS))
NATIVE_PLATFORM_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_PLATFORM_SRCS))
WASM_PLATFORM_OBJS := $(patsubst $(ROOT)/%.c,$(WASM_OBJ_DIR)/%.o,$(NATIVE_PLATFORM_SRCS))
NATIVE_DAT_TYPES_SRC := $(ROOT)/tools/build/native_dat_types.c
NATIVE_DAT_LAYOUT_GENERATOR := $(ROOT)/tools/build/generate_native_dat_layout.py
COMMAND_FIELDS_GENERATOR := $(ROOT)/tools/build/generate_command_accessors.py
NATIVE_DAT_PPC_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_ppc.o
NATIVE_DAT_NATIVE_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/native_dat_types_native.o
NATIVE_DAT_LAYOUT_SRC := $(NATIVE_GENERATED_DIR)/native_dat_layout.c
NATIVE_DAT_LAYOUT_OBJ := $(NATIVE_OBJ_DIR)/generated/native_dat_layout.o
WASM_DAT_NATIVE_TYPES_OBJ := $(WASM_GENERATED_DIR)/native_dat_types_wasm.o
WASM_DAT_LAYOUT_SRC := $(WASM_GENERATED_DIR)/native_dat_layout.c
WASM_DAT_LAYOUT_OBJ := $(WASM_OBJ_DIR)/generated/native_dat_layout.o
WASM_COMMAND_FIELDS := $(WASM_GENERATED_DIR)/msl_command_fields.h
NATIVE_COMMAND_FIELDS := $(NATIVE_GENERATED_DIR)/msl_command_fields.h
MATCH_RELOC_TYPES_SRC := $(ROOT)/tools/build/match_reloc_types.c
MATCH_RELOC_GENERATOR := $(ROOT)/tools/build/generate_match_reloc_layout.py
MATCH_RELOC_TYPES_DEF := $(CORE)/runtime/relocation_types.def
NATIVE_MATCH_RELOC_TYPES_OBJ := $(NATIVE_GENERATED_DIR)/match_reloc_types.o
NATIVE_MATCH_RELOC_LAYOUT_SRC := $(NATIVE_GENERATED_DIR)/match_reloc_layout.c
NATIVE_MATCH_RELOC_LAYOUT_OBJ := $(NATIVE_OBJ_DIR)/generated/match_reloc_layout.o
WASM_MATCH_RELOC_TYPES_OBJ := $(WASM_GENERATED_DIR)/match_reloc_types.o
WASM_MATCH_RELOC_LAYOUT_SRC := $(WASM_GENERATED_DIR)/match_reloc_layout.c
WASM_MATCH_RELOC_LAYOUT_OBJ := $(WASM_OBJ_DIR)/generated/match_reloc_layout.o
PYTHON_DAT_LAYOUT_OBJ := $(PYTHON_OBJ_DIR)/generated/native_dat_layout.o
PYTHON_MATCH_RELOC_LAYOUT_OBJ := $(PYTHON_OBJ_DIR)/generated/match_reloc_layout.o
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
PYTHON_CORE_OBJS := $(PYTHON_UPSTREAM_OBJS) $(PYTHON_LOCAL_OBJS) \
	$(PYTHON_DAT_LAYOUT_OBJ) $(PYTHON_MATCH_RELOC_LAYOUT_OBJ)
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
	$(ROOT)/tests/melee_core/batch_api_smoke.c \
	$(ROOT)/tests/melee_core/public_api_smoke.c \
	$(ROOT)/tests/melee_core/context_smoke.c \
	$(ROOT)/tests/melee_core/data_load_smoke.c \
	$(ROOT)/tests/melee_core/gameplay_parts_smoke.c \
	$(ROOT)/tests/melee_core/article_pool_smoke.c \
	$(ROOT)/tests/melee_core/map_collision_smoke.c \
	$(ROOT)/tests/melee_core/model_animation_smoke.c \
	$(ROOT)/tests/melee_core/scalar_api_smoke.c \
	$(ROOT)/tests/melee_core/scheduler_smoke.c \
	$(ROOT)/tests/melee_core/lifecycle_bench.c \
	$(ROOT)/tests/melee_core/wasm_parity.c
PPC_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(PPC_OBJ_DIR)/%.o,$(SMOKE_SRCS))
NATIVE_SMOKE_OBJS := $(patsubst $(ROOT)/%.c,$(NATIVE_OBJ_DIR)/%.o,$(NATIVE_SMOKE_SRCS))
NATIVE_REPLAY_BENCH_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/replay_bench.o
NATIVE_RUNTIME_CENSUS_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/runtime_census.o
NATIVE_LARGE_BATCH_SMOKE_OBJ := $(NATIVE_OBJ_DIR)/tests/melee_core/large_batch_smoke.o
NATIVE_FLAGS_STAMP := $(NATIVE_BUILD)/compile-flags.stamp
NATIVE_COMPILE_OBJS := $(NATIVE_CORE_OBJS) $(NATIVE_MAIN_OBJ) $(NATIVE_SMOKE_OBJS) \
	$(NATIVE_REPLAY_BENCH_OBJ) $(NATIVE_RUNTIME_CENSUS_OBJ) \
	$(NATIVE_LARGE_BATCH_SMOKE_OBJ)
DEPS := $(PPC_OBJS:.o=.d) $(PPC_SMOKE_OBJS:.o=.d) \
	$(NATIVE_COMPILE_OBJS:.o=.d) $(WASM_CORE_OBJS:.o=.d) \
	$(PYTHON_CORE_OBJS:.o=.d) \
	$(NATIVE_DAT_PPC_TYPES_OBJ:.o=.d) \
	$(NATIVE_DAT_NATIVE_TYPES_OBJ:.o=.d) \
	$(NATIVE_MATCH_RELOC_TYPES_OBJ:.o=.d) \
	$(WASM_DAT_NATIVE_TYPES_OBJ:.o=.d) \
	$(WASM_MATCH_RELOC_TYPES_OBJ:.o=.d) \
	$(PPC_MATCH_RELOC_TYPES_OBJ:.o=.d)

BINARY := $(PPC_BUILD)/melee-core-ppc
NATIVE_BINARY := $(NATIVE_BUILD)/melee-core-native
WASM_MODULE := $(WASM_BUILD)/melee-core.js
PYTHON_LIBRARY := $(PYTHON_BUILD)/libmelee_core.so
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
NATIVE_PUBLIC_API_SMOKE := $(NATIVE_BUILD)/public-api-smoke
NATIVE_GAMEPLAY_PARTS_SMOKE := $(NATIVE_BUILD)/gameplay-parts-smoke
NATIVE_ARTICLE_POOL_SMOKE := $(NATIVE_BUILD)/article-pool-smoke
NATIVE_WASM_PARITY := $(NATIVE_BUILD)/wasm-parity
NATIVE_LIFECYCLE_BENCH := $(NATIVE_BUILD)/lifecycle-bench
NATIVE_REPLAY_BENCH := $(NATIVE_BUILD)/replay-bench
NATIVE_RUNTIME_CENSUS := $(NATIVE_BUILD)/runtime-census
NATIVE_LARGE_BATCH_SMOKE := $(NATIVE_BUILD)/large-batch-smoke
NATIVE_RELEASE_REPLAY_BENCH := $(NATIVE_RELEASE_BUILD)/replay-bench
SUBSYSTEM_PROFILE_REPLAY_BENCH := $(SUBSYSTEM_PROFILE_BUILD)/replay-bench
SUBSYSTEM_PROFILE_RAW := $(SUBSYSTEM_PROFILE_BUILD)/profile.txt
VIEWER_SCHEMA_TOOL := $(NATIVE_BUILD)/viewer-schema
VIEWER_SCHEMA_JS := $(ROOT)/tools/viewer/live/schema.generated.js
VALIDATION_NATIVE_SRC := $(ROOT)/tools/validation/native.c
VALIDATION_NATIVE := $(VALIDATION_BUILD)/_msl_replay_validate.so

CPPFLAGS := \
	-DBUGFIX \
	-include $(CORE)/platform/compat.h \
	-I$(CORE) \
	-I$(CORE)/platform/include \
	-I$(CORE)/melee \
	-I$(CORE)/melee/ft/chara \
	-I$(CORE)/sysdolphin \
	-I$(CORE)/Runtime \
	-I$(CORE)/extern/dolphin/include
NATIVE_CPPFLAGS := $(CPPFLAGS) -DMSL_CORE_NATIVE -I$(NATIVE_GENERATED_DIR)
PYTHON_CPPFLAGS := $(NATIVE_CPPFLAGS) -DMSL_CORE_SHARED
WASM_CPPFLAGS := $(CPPFLAGS) -I$(WASM_GENERATED_DIR) \
	-DMSL_CORE_NATIVE -DMSL_CORE_WASM
CFLAGS := \
	-O0 -g -std=gnu11 -fgnu89-inline -fno-short-enums \
	-fno-strict-aliasing -ffp-contract=off -ffunction-sections \
	-fdata-sections -w
# GCC 14+ and Clang promote these legacy-C constructs to errors even when
# diagnostics are otherwise suppressed. Wasm clang requires the same set.
HOSTED_LEGACY_CFLAGS := \
	-Wno-implicit-function-declaration -Wno-int-conversion \
	-Wno-incompatible-pointer-types
NATIVE_CFLAGS = $(CFLAGS) $(HOST_ARCH_FLAGS) -fno-pie \
	$(HOSTED_LEGACY_CFLAGS)
NATIVE_LINK_FLAGS ?=
# Native release uses a strict O1 source profile by default. The audited lists
# below preserve lower exactness profiles or admit stronger measured owners.
# `__fmadds` is an MWCC intrinsic, so release emits it directly instead of
# paying an out-of-line call from every exact O0 source translation unit.
NATIVE_RELEASE_CFLAGS := $(CFLAGS) -O1 -fno-pie -fomit-frame-pointer \
	-fcf-protection=none -fno-asynchronous-unwind-tables -fno-unwind-tables \
	$(HOSTED_LEGACY_CFLAGS) $(HOST_ARCH_FLAGS) $(FMA_FLAGS) \
	-DMSL_NATIVE_INLINE_FMADDS
NATIVE_BASE_CFLAGS := $(NATIVE_CFLAGS)
WASM_CFLAGS = $(CFLAGS) $(HOSTED_LEGACY_CFLAGS)
LDLIBS := -lm

# Melee's matching MSL trig implementation intentionally compiles its nested
# range-reduction and polynomial expressions to fmadds/fnmsubs (DOL
# 0x80326240). Preserve those operation boundaries without enabling
# contraction indiscriminately in the hosted core.
$(PPC_OBJ_DIR)/gameplay/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(PPC_OBJ_DIR)/src/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast
$(NATIVE_OBJ_DIR)/gameplay/MSL/trigf.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -fno-builtin-sinf -fno-builtin-cosf $(FMA_FLAGS)
$(NATIVE_OBJ_DIR)/src/runtime/math.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -fno-builtin-sinf -fno-builtin-cosf $(FMA_FLAGS)
$(NATIVE_OBJ_DIR)/src/runtime/savestate.o: NATIVE_CPPFLAGS += -D_GNU_SOURCE
$(PYTHON_OBJ_DIR)/src/runtime/savestate.o: PYTHON_CPPFLAGS += -D_GNU_SOURCE
$(NATIVE_OBJ_DIR)/gameplay/sysdolphin/baselib/id.o: NATIVE_CPPFLAGS += -D_GNU_SOURCE
$(PYTHON_OBJ_DIR)/gameplay/sysdolphin/baselib/id.o: PYTHON_CPPFLAGS += -D_GNU_SOURCE
$(NATIVE_RUNTIME_CENSUS_OBJ): NATIVE_CPPFLAGS += -D_GNU_SOURCE
ifeq ($(NATIVE_GPROF),1)
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_CORE_GPROF
endif
ifeq ($(NATIVE_SUBSYSTEM_PROFILE),1)
$(NATIVE_OBJ_DIR)/src/runtime/batch.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_OBJ_DIR)/src/runtime/scalar.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_OBJ_DIR)/src/runtime/subsystem_profile.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_OBJ_DIR)/gameplay/melee/ft/fighter.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_OBJ_DIR)/gameplay/melee/ft/ftanim.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_OBJ_DIR)/gameplay/sysdolphin/baselib/gobj.o: NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_SUBSYSTEM_PROFILE
endif
ifeq ($(NATIVE_CALLGRIND),1)
$(NATIVE_REPLAY_BENCH_OBJ): NATIVE_CPPFLAGS += -DMSL_CORE_CALLGRIND
endif
$(WASM_OBJ_DIR)/gameplay/MSL/trigf.o: CFLAGS += -O2 -ffp-contract=fast
$(WASM_OBJ_DIR)/src/runtime/math.o: CFLAGS += -O2 -ffp-contract=fast
$(PYTHON_OBJ_DIR)/gameplay/MSL/trigf.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -fno-builtin-sinf -fno-builtin-cosf $(FMA_FLAGS)
$(PYTHON_OBJ_DIR)/src/runtime/math.o: override NATIVE_CFLAGS += -O2 -ffp-contract=fast -fno-builtin-sinf -fno-builtin-cosf $(FMA_FLAGS)

ifeq ($(NATIVE_RELEASE_PROFILE),1)
# Fighter callback and item source closures, plus quaternion interpolation,
# change canonical outputs when admitted at O1. Preserve their measured source
# profiles while the remaining native closure uses the strict optimized default.
NATIVE_RELEASE_O0_OBJS := \
	$(filter-out $(NATIVE_OBJ_DIR)/gameplay/melee/ft/ftanim.o,\
		$(filter $(NATIVE_OBJ_DIR)/gameplay/melee/ft/%,$(NATIVE_UPSTREAM_OBJS))) \
	$(filter $(NATIVE_OBJ_DIR)/gameplay/melee/it/%,$(NATIVE_UPSTREAM_OBJS)) \
	$(NATIVE_OBJ_DIR)/gameplay/sysdolphin/baselib/quatlib.o
NATIVE_RELEASE_O1_OBJS := \
	$(NATIVE_OBJ_DIR)/gameplay/melee/mp/mpcoll.o
NATIVE_RELEASE_O2_OBJS := \
	$(NATIVE_OBJ_DIR)/gameplay/melee/lb/lb_00B0.o
NATIVE_RELEASE_OPT_OBJS := \
	$(NATIVE_OBJ_DIR)/gameplay/melee/ft/ftanim.o \
	$(NATIVE_OBJ_DIR)/gameplay/melee/lb/lbspdisplay.o \
	$(NATIVE_OBJ_DIR)/gameplay/melee/lb/lbvector.o \
	$(NATIVE_OBJ_DIR)/gameplay/melee/mp/mplib.o \
	$(NATIVE_OBJ_DIR)/gameplay/sysdolphin/baselib/fobj.o \
	$(NATIVE_OBJ_DIR)/gameplay/sysdolphin/baselib/jobj.o \
	$(NATIVE_OBJ_DIR)/src/api.o \
	$(NATIVE_OBJ_DIR)/src/platform/dolphin_mtx.o \
	$(NATIVE_OBJ_DIR)/src/runtime/fighter_pose.o \
	$(NATIVE_OBJ_DIR)/src/runtime/observation.o \
	$(NATIVE_OBJ_DIR)/src/runtime/ppc_sqrt.o \
	$(NATIVE_OBJ_DIR)/src/runtime/relocation.o \
	$(NATIVE_OBJ_DIR)/src/runtime/savestate.o \
	$(NATIVE_OBJ_DIR)/src/runtime/viewer.o \
	$(NATIVE_OBJ_DIR)/src/runtime/wire.o \
	$(NATIVE_OBJ_DIR)/src/runtime/main.o \
	$(NATIVE_OBJ_DIR)/generated/native_dat_layout.o \
	$(NATIVE_OBJ_DIR)/generated/match_reloc_layout.o \
	$(NATIVE_OBJ_DIR)/tests/melee_core/replay_bench.o
$(NATIVE_OBJ_DIR)/gameplay/MSL/trigf.o: override NATIVE_CFLAGS += $(RELEASE_ARCH_FLAGS)
$(NATIVE_RELEASE_O0_OBJS): override NATIVE_CFLAGS += -O0
$(NATIVE_RELEASE_O1_OBJS): override NATIVE_CFLAGS += -O1 $(RELEASE_ARCH_FLAGS)
$(NATIVE_RELEASE_O2_OBJS): override NATIVE_CFLAGS += -O2 $(RELEASE_ARCH_FLAGS)
$(NATIVE_RELEASE_OPT_OBJS): override NATIVE_CFLAGS += -O3 $(RELEASE_ARCH_FLAGS)
endif

NATIVE_FLAGS_SIGNATURE := $(NATIVE_CPPFLAGS)|$(NATIVE_BASE_CFLAGS)|$(NATIVE_LINK_FLAGS)|$(NATIVE_RELEASE_PROFILE)|$(NATIVE_SUBSYSTEM_PROFILE)|$(NATIVE_RELEASE_O0_OBJS)|$(NATIVE_RELEASE_O1_OBJS)|$(NATIVE_RELEASE_O2_OBJS)|$(NATIVE_RELEASE_OPT_OBJS)

.PHONY: all bootstrap extract ppc native python-library native-release native-release-benchmark runtime-census large-batch-smoke benchmark-prepare benchmark-native subsystem-profile benchmark-9950x3d-vcache-256 benchmark-9950x3d-vcache-512 benchmark-9950x3d-frequency-256 benchmark-9950x3d-frequency-512 wasm wasm-smoke viewer-build viewer viewer-smoke viewer-production-smoke viewer-schema viewer-schema-check lifecycle-benchmark source-check validator validation-suite validation-supported-domain validation-release-supported-domain clean toolchain data-check ppc-smoke native-smoke test test-full format-check slpz-convert FORCE

all: native python-library

bootstrap:
	@test -n "$(ISO)" || (echo "Usage: make bootstrap ISO=/path/to/SSBM.iso" >&2; exit 2)
	@uv sync --dev
	@"$(PY)" -m tools.data.extract --iso "$(ISO)" --out-dir "$(MSL_DATA_DIR)"
	@$(MAKE) --no-print-directory native python-library

extract:
	@test -n "$(ISO)" || (echo "Usage: make extract ISO=/path/to/SSBM.iso" >&2; exit 2)
	@"$(PY)" -m tools.data.extract --iso "$(ISO)" --out-dir "$(MSL_DATA_DIR)"

ppc: $(BINARY)

native: $(NATIVE_BINARY)

python-library: $(PYTHON_LIBRARY)

native-release:
	@case "$(NATIVE_RELEASE_CFLAGS) $(RELEASE_ARCH_FLAGS) $(NATIVE_LINK_FLAGS)" in \
		*Ofast*|*fast-math*|*unsafe-math*|*finite-math*|*associative-math*|*reciprocal-math*|*fp-contract=fast*) \
			echo "unsafe floating-point flag in strict release build" >&2; exit 2;; \
		*flto*) \
			echo "LTO is not validated for the source-shaped gameplay build" >&2; exit 2;; \
	esac
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" native \
		NATIVE_BUILD="$(NATIVE_RELEASE_BUILD)" \
		NATIVE_CFLAGS="$(NATIVE_RELEASE_CFLAGS)" NATIVE_RELEASE_PROFILE=1

# The replay benchmark is Linux/x86-specific; portable release validation only
# needs the runtime binary above.
native-release-benchmark: native-release
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" "$(NATIVE_RELEASE_REPLAY_BENCH)" \
		NATIVE_BUILD="$(NATIVE_RELEASE_BUILD)" \
		NATIVE_CFLAGS="$(NATIVE_RELEASE_CFLAGS)" NATIVE_RELEASE_PROFILE=1

runtime-census: data-check $(NATIVE_RUNTIME_CENSUS)
	@"$(NATIVE_RUNTIME_CENSUS)" "$(DATA)"

large-batch-smoke: data-check $(NATIVE_LARGE_BATCH_SMOKE)
	@"$(NATIVE_LARGE_BATCH_SMOKE)" "$(DATA)" "$(LARGE_BATCH_MATCHES)"

wasm: $(WASM_MODULE)

viewer-schema: $(VIEWER_SCHEMA_JS)

viewer-build:
	@MSL_DATA_DIR="$(abspath $(MSL_DATA_DIR))" "$(ROOT)/tools/viewer/build.sh"

viewer:
	@MSL_VIEWER_STATIC_ROOT="build/viewer" MSL_VIEWER_URL_PATH="/tools/viewer/" \
		MSL_VIEWER_HOST="$(VIEWER_HOST)" MSL_VIEWER_PORT="$(VIEWER_PORT)" \
		MSL_VIEWER_OPEN="$(OPEN)" node "$(ROOT)/tools/viewer/server.mjs"

FORCE:

$(NATIVE_FLAGS_STAMP): FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(NATIVE_FLAGS_SIGNATURE)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(NATIVE_COMPILE_OBJS): $(NATIVE_FLAGS_STAMP)

$(VIEWER_SCHEMA_TOOL): $(ROOT)/tools/viewer/schema.c $(CORE)/runtime/wire.h
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" -std=c11 -Wall -Wextra -I"$(CORE)" "$<" -o "$@"

$(VIEWER_SCHEMA_JS): $(VIEWER_SCHEMA_TOOL)
	@tmp="$@.tmp"; "$(VIEWER_SCHEMA_TOOL)" > "$$tmp"; \
		if ! cmp -s "$$tmp" "$@"; then mv "$$tmp" "$@"; else rm "$$tmp"; fi

viewer-schema-check: $(VIEWER_SCHEMA_TOOL)
	@tmp="$(NATIVE_BUILD)/schema.generated.js"; \
		"$(VIEWER_SCHEMA_TOOL)" > "$$tmp"; cmp "$$tmp" "$(VIEWER_SCHEMA_JS)"

source-check:
	@$(SOURCE_SYNC) check

validator: $(VALIDATION_NATIVE)

$(VALIDATION_NATIVE): $(VALIDATION_NATIVE_SRC) $(CORE)/runtime/wire.h $(CORE)/runtime/benchmark_wire.h $(CORE)/runtime/item_projection.h
	@mkdir -p "$(@D)"
	@PY_INCLUDE="$$($(PY) -c 'import sysconfig; print(sysconfig.get_path("include"))')"; \
		"$(HOST_CC)" -O3 -std=c11 -fPIC -shared $(PY_EXT_LINK_FLAGS) -Wall -Wextra \
			-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -I"$$PY_INCLUDE" \
			-I"$(CORE)" "$<" -lm -o "$@"

toolchain: $(TOOLCHAIN_STAMP)

$(TOOLCHAIN_STAMP): $(ROOT)/tools/build/setup_ppc32_toolchain.sh \
		$(ROOT)/tools/build/ppc32_toolchain_packages.tsv \
		$(ROOT)/tools/build/host_arch.sh
	@$(ROOT)/tools/build/setup_ppc32_toolchain.sh

$(PPC_UPSTREAM_OBJS): $(PPC_OBJ_DIR)/gameplay/%.o: $(CORE)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$(CORE)/$*.c" -o "$@"

$(PPC_OBJ_DIR)/%.o: $(ROOT)/%.c $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) \
		-MMD -MP -c "$<" -o "$@"

$(BINARY): $(PPC_OBJS)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $^ $(LDLIBS) -o "$@"

$(NATIVE_UPSTREAM_OBJS): $(NATIVE_OBJ_DIR)/gameplay/%.o: $(CORE)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP \
		-c "$(CORE)/$*.c" -o "$@"

$(NATIVE_OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(PYTHON_UPSTREAM_OBJS): $(PYTHON_OBJ_DIR)/gameplay/%.o: $(CORE)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(PYTHON_CPPFLAGS) $(NATIVE_CFLAGS) -fPIC \
		-fvisibility=hidden -MMD -MP \
		-c "$(CORE)/$*.c" -o "$@"

$(PYTHON_OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(PYTHON_CPPFLAGS) $(NATIVE_CFLAGS) -fPIC \
		-fvisibility=hidden -MMD -MP -c "$<" -o "$@"

$(WASM_UPSTREAM_OBJS): $(WASM_OBJ_DIR)/gameplay/%.o: $(CORE)/%.c
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP \
		-c "$(CORE)/$*.c" -o "$@"

$(WASM_OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p "$(@D)"
	@"$(EMCC)" $(WASM_CPPFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_OBJ_DIR)/gameplay/melee/ft/ftaction.o \
$(WASM_OBJ_DIR)/gameplay/melee/it/itanimlist.o \
$(WASM_OBJ_DIR)/gameplay/melee/lb/lbcommand.o: $(WASM_COMMAND_FIELDS)

$(NATIVE_DAT_PPC_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC) $(PPC_TYPES_TOOLCHAIN_DEP)
	@mkdir -p "$(@D)"
	@$(PPC_TYPES_COMPILE) $(CPPFLAGS) -MMD -MP -g -gdwarf-4 \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(NATIVE_DAT_NATIVE_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(HOST_ARCH_FLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(WASM_COMMAND_FIELDS): $(NATIVE_DAT_PPC_TYPES_OBJ) $(COMMAND_FIELDS_GENERATOR)
	@mkdir -p "$(@D)"
	@"$(PY)" "$(COMMAND_FIELDS_GENERATOR)" \
		--object "$(NATIVE_DAT_PPC_TYPES_OBJ)" --output "$@"

# Clang hosts (Mach-O) ignore scalar_storage_order and read the authored
# big-endian command bytes through the same generated accessors as wasm32.
$(NATIVE_COMMAND_FIELDS): $(NATIVE_DAT_PPC_TYPES_OBJ) $(COMMAND_FIELDS_GENERATOR)
	@mkdir -p "$(@D)"
	@"$(PY)" "$(COMMAND_FIELDS_GENERATOR)" \
		--object "$(NATIVE_DAT_PPC_TYPES_OBJ)" --output "$@"

$(NATIVE_OBJ_DIR)/gameplay/melee/ft/ftaction.o \
$(NATIVE_OBJ_DIR)/gameplay/melee/it/itanimlist.o \
$(NATIVE_OBJ_DIR)/gameplay/melee/lb/lbcommand.o \
$(NATIVE_OBJ_DIR)/gameplay/melee/lb/lbspdisplay.o \
$(PYTHON_OBJ_DIR)/gameplay/melee/ft/ftaction.o \
$(PYTHON_OBJ_DIR)/gameplay/melee/it/itanimlist.o \
$(PYTHON_OBJ_DIR)/gameplay/melee/lb/lbcommand.o \
$(PYTHON_OBJ_DIR)/gameplay/melee/lb/lbspdisplay.o: $(NATIVE_COMMAND_FIELDS)

$(NATIVE_DAT_LAYOUT_SRC): $(NATIVE_DAT_PPC_TYPES_OBJ) $(NATIVE_DAT_NATIVE_TYPES_OBJ) $(NATIVE_DAT_LAYOUT_GENERATOR)
	@"$(PY)" "$(NATIVE_DAT_LAYOUT_GENERATOR)" \
		--ppc "$(NATIVE_DAT_PPC_TYPES_OBJ)" \
		--native "$(NATIVE_DAT_NATIVE_TYPES_OBJ)" --output "$@"

$(NATIVE_DAT_LAYOUT_OBJ): $(NATIVE_DAT_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(WASM_GLIBC_STUB):
	@mkdir -p "$(@D)"
	@: > "$@"

$(WASM_DAT_NATIVE_TYPES_OBJ): $(NATIVE_DAT_TYPES_SRC) $(WASM_GLIBC_STUB)
	@mkdir -p "$(@D)"
	@$(WASM_PROXY_COMPILE) $(WASM_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
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
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(HOST_ARCH_FLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(NATIVE_MATCH_RELOC_LAYOUT_SRC): $(NATIVE_MATCH_RELOC_TYPES_OBJ) $(MATCH_RELOC_TYPES_DEF) $(MATCH_RELOC_GENERATOR)
	@"$(PY)" "$(MATCH_RELOC_GENERATOR)" --object "$(NATIVE_MATCH_RELOC_TYPES_OBJ)" \
		--types "$(MATCH_RELOC_TYPES_DEF)" --output "$@"

$(NATIVE_MATCH_RELOC_LAYOUT_OBJ): $(NATIVE_MATCH_RELOC_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_CPPFLAGS) $(NATIVE_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(PYTHON_DAT_LAYOUT_OBJ): $(NATIVE_DAT_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(PYTHON_CPPFLAGS) $(NATIVE_CFLAGS) -fPIC \
		-fvisibility=hidden -MMD -MP -c "$<" -o "$@"

$(PYTHON_MATCH_RELOC_LAYOUT_OBJ): $(NATIVE_MATCH_RELOC_LAYOUT_SRC)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(PYTHON_CPPFLAGS) $(NATIVE_CFLAGS) -fPIC \
		-fvisibility=hidden -MMD -MP -c "$<" -o "$@"

$(WASM_MATCH_RELOC_TYPES_OBJ): $(MATCH_RELOC_TYPES_SRC) $(MATCH_RELOC_TYPES_DEF) $(WASM_GLIBC_STUB)
	@mkdir -p "$(@D)"
	@$(WASM_PROXY_COMPILE) $(WASM_CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
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
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) -MMD -MP -g -gdwarf-4 -w \
		-fno-eliminate-unused-debug-types -std=gnu11 -c "$<" -o "$@"

$(PPC_MATCH_RELOC_LAYOUT_SRC): $(PPC_MATCH_RELOC_TYPES_OBJ) $(MATCH_RELOC_TYPES_DEF) $(MATCH_RELOC_GENERATOR)
	@"$(PY)" "$(MATCH_RELOC_GENERATOR)" --object "$(PPC_MATCH_RELOC_TYPES_OBJ)" \
		--types "$(MATCH_RELOC_TYPES_DEF)" --output "$@"

$(PPC_MATCH_RELOC_LAYOUT_OBJ): $(PPC_MATCH_RELOC_LAYOUT_SRC) $(TOOLCHAIN_STAMP)
	@mkdir -p "$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(NATIVE_BINARY): $(NATIVE_OBJS)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_EXE_LINK_FLAGS) $(NATIVE_LINK_FLAGS) $^ $(LDLIBS) -o "$@"

$(PYTHON_LIBRARY): $(PYTHON_CORE_OBJS)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(SHARED_LIB_LINK_FLAGS) \
		$^ $(LDLIBS) -o "$@"

# The link embeds $(DATA) via --preload-file, so the packaged bundle goes
# stale whenever the raw archives change; list them as prerequisites (the
# link line names $(WASM_CORE_OBJS) directly since $^ now includes data).
$(WASM_MODULE): $(WASM_CORE_OBJS) $(wildcard $(DATA)/*)
	@mkdir -p "$(@D)"
	@log="$(WASM_BUILD)/link-warnings.log"; other="$(WASM_BUILD)/link-other-warnings.log"; \
	runtime="$(WASM_BUILD)/runtime-signature-warnings.log"; \
	if ! "$(EMCC)" -O2 $(WASM_LINK_FLAGS) --no-entry -Wl,--gc-sections $(WASM_CORE_OBJS) $(LDLIBS) \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createMslCoreModule \
		-sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
		-sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=1073741824 \
		-sGROWABLE_ARRAYBUFFERS=0 \
		-sEXPORTED_FUNCTIONS='["_malloc","_free","_msl_core_game_data_create","_msl_core_game_data_destroy","_msl_core_batch_create","_msl_core_batch_destroy","_msl_core_batch_match_count","_msl_core_batch_reset_matches","_msl_core_batch_step_matches","_msl_core_batch_write_state","_msl_core_batch_write_observation","_msl_core_batch_write_terminal","_msl_core_batch_write_viewer","_msl_core_batch_copy_matches","_msl_core_batch_match_save_size","_msl_core_batch_save_match","_msl_core_batch_restore_match"]' \
		-sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
		--preload-file "$(DATA)@/data/raw" -o "$@" 2>"$$log"; then \
		cat "$$log" >&2; exit 1; \
	fi; \
	awk 'BEGIN { RS=""; ORS="\n\n" } !/warning: function signature mismatch/' \
		"$$log" >"$$other"; \
	awk 'BEGIN { RS=""; ORS="\n\n" } \
		/warning: function signature mismatch/ && /\/obj\/src\/runtime\//' \
		"$$log" >"$$runtime"; \
	if [ -s "$$runtime" ]; then \
		echo "wasm link: runtime function signature mismatch" >&2; \
		cat "$$runtime" >&2; exit 1; \
	fi; \
	rm -f "$$runtime"; \
	count="$$(rg -c 'warning: function signature mismatch' "$$log" 2>/dev/null || true)"; \
	if [ "$${count:-0}" -gt 0 ]; then \
		echo "wasm link: $$count expected signature warnings from loud unsupported stubs (details: $$log)" >&2; \
	fi; \
	if [ -s "$$other" ]; then cat "$$other" >&2; fi

define link_smoke
$(1): $(PPC_CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@env PATH="$(TOOLCHAIN_ROOT)/usr/bin:$$$$PATH" \
		LD_LIBRARY_PATH="$(TOOLCHAIN_LIBDIR):$$$$LD_LIBRARY_PATH" \
		"$(CC)" --sysroot="$(SYSROOT)" -Wl,--gc-sections $$^ $(LDLIBS) -o "$$@"
endef

define link_native_smoke
$(1): $(NATIVE_CORE_OBJS) $(2)
	@mkdir -p "$$(@D)"
	@"$(HOST_CC)" $(NATIVE_EXE_LINK_FLAGS) $(NATIVE_LINK_FLAGS) $$^ $(LDLIBS) -o "$$@"
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
$(eval $(call link_native_smoke,$(NATIVE_PUBLIC_API_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/public_api_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_GAMEPLAY_PARTS_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/gameplay_parts_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_ARTICLE_POOL_SMOKE),$(NATIVE_OBJ_DIR)/tests/melee_core/article_pool_smoke.o))
$(eval $(call link_native_smoke,$(NATIVE_WASM_PARITY),$(NATIVE_OBJ_DIR)/tests/melee_core/wasm_parity.o))
$(eval $(call link_native_smoke,$(NATIVE_LIFECYCLE_BENCH),$(NATIVE_OBJ_DIR)/tests/melee_core/lifecycle_bench.o))
$(eval $(call link_native_smoke,$(NATIVE_REPLAY_BENCH),$(NATIVE_REPLAY_BENCH_OBJ)))

$(NATIVE_RUNTIME_CENSUS): $(NATIVE_CORE_OBJS) $(NATIVE_RUNTIME_CENSUS_OBJ)
	@mkdir -p "$(@D)"
	@"$(HOST_CC)" $(NATIVE_EXE_LINK_FLAGS) $^ \
		$(LDLIBS) -ldl -o "$@"

$(eval $(call link_native_smoke,$(NATIVE_LARGE_BATCH_SMOKE),$(NATIVE_LARGE_BATCH_SMOKE_OBJ)))

data-check:
	@$(PY) -c 'from pathlib import Path; from tools.data.raw import validate_raw_dir; validate_raw_dir(Path("$(DATA)"), verify_hashes=False)'

ppc-smoke: data-check $(ARCHIVE_SMOKE) $(DATA_SMOKE) $(MAP_SMOKE) $(MODEL_SMOKE) $(SCHEDULER_SMOKE) $(SCALAR_API_SMOKE)
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(ARCHIVE_SMOKE)" \
		"$(DATA)/PlCo.dat" \
		"$(DATA)/PlFx.dat" \
		"$(DATA)/PlFxNr.dat" \
		"$(DATA)/GrNLa.dat"
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(DATA_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MODEL_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(MAP_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCHEDULER_SMOKE)"
	@$(TIMEOUT) 10s "$(QEMU)" -L "$(QEMU_SYSROOT)" "$(SCALAR_API_SMOKE)" \
		"$(DATA)"

native-smoke: data-check $(NATIVE_DATA_SMOKE) $(NATIVE_MAP_SMOKE) $(NATIVE_MODEL_SMOKE) $(NATIVE_SCHEDULER_SMOKE) $(NATIVE_SCALAR_API_SMOKE) $(NATIVE_CONTEXT_SMOKE) $(NATIVE_BATCH_API_SMOKE) $(NATIVE_PUBLIC_API_SMOKE) $(NATIVE_GAMEPLAY_PARTS_SMOKE) $(NATIVE_ARTICLE_POOL_SMOKE) $(NATIVE_RUNTIME_CENSUS)
	@$(TIMEOUT) 5s "$(NATIVE_DATA_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_MODEL_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_MAP_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_SCHEDULER_SMOKE)"
	@$(TIMEOUT) 5s "$(NATIVE_SCALAR_API_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_CONTEXT_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_BATCH_API_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_PUBLIC_API_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 5s "$(NATIVE_GAMEPLAY_PARTS_SMOKE)" "$(DATA)"
	@$(TIMEOUT) 10s "$(NATIVE_ARTICLE_POOL_SMOKE)" "$(DATA)" >/dev/null
	@$(TIMEOUT) 5s "$(NATIVE_RUNTIME_CENSUS)" "$(DATA)" >/dev/null

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
	@$(TIMEOUT) 5s "$(NATIVE_LIFECYCLE_BENCH)" "$(DATA)"

benchmark-prepare: validator
	@"$(PY)" -m tools.validation.prepare_replay_benchmark \
		--suite "$(VALIDATION_SUITE)" --output "$(BENCHMARK_MANIFEST)" \
		--characters "$(VALIDATION_CHARACTERS)" --stages "$(VALIDATION_STAGES)"

benchmark-native: data-check benchmark-prepare native-release-benchmark
	@taskset -c "$(BENCHMARK_CPU)" "$(NATIVE_RELEASE_REPLAY_BENCH)" \
		"$(DATA)" "$(BENCHMARK_MANIFEST)" --matches "$(BENCHMARK_MATCHES)" \
		--resident-matches "$(BENCHMARK_RESIDENT_MATCHES)" \
		--match-frames "$(BENCHMARK_MATCH_FRAMES)" \
		--warmup-ticks "$(BENCHMARK_WARMUP_TICKS)"

subsystem-profile: BENCHMARK_CPU=0
subsystem-profile: BENCHMARK_MATCHES=512
subsystem-profile: BENCHMARK_RESIDENT_MATCHES=512
subsystem-profile: data-check benchmark-prepare
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" \
		NATIVE_BUILD="$(SUBSYSTEM_PROFILE_BUILD)" \
		NATIVE_CFLAGS="$(NATIVE_RELEASE_CFLAGS)" NATIVE_RELEASE_PROFILE=1 \
		NATIVE_SUBSYSTEM_PROFILE=1 "$(SUBSYSTEM_PROFILE_REPLAY_BENCH)"
	@mkdir -p "$(SUBSYSTEM_PROFILE_BUILD)"
	@taskset -c "$(BENCHMARK_CPU)" "$(SUBSYSTEM_PROFILE_REPLAY_BENCH)" \
		"$(DATA)" "$(BENCHMARK_MANIFEST)" --matches "$(BENCHMARK_MATCHES)" \
		--resident-matches "$(BENCHMARK_RESIDENT_MATCHES)" \
		--match-frames "$(BENCHMARK_MATCH_FRAMES)" \
		--warmup-ticks "$(BENCHMARK_WARMUP_TICKS)" > "$(SUBSYSTEM_PROFILE_RAW)"
	@"$(PY)" "$(ROOT)/tools/performance/report_subsystem_profile.py" \
		--binary "$(SUBSYSTEM_PROFILE_REPLAY_BENCH)" \
		--input "$(SUBSYSTEM_PROFILE_RAW)"

benchmark-9950x3d-vcache-256:
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" benchmark-native \
		BENCHMARK_CPU=0 BENCHMARK_MATCHES=256

benchmark-9950x3d-vcache-512:
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" benchmark-native \
		BENCHMARK_CPU=0 BENCHMARK_MATCHES=512

benchmark-9950x3d-frequency-256:
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" benchmark-native \
		BENCHMARK_CPU=8 BENCHMARK_MATCHES=256

benchmark-9950x3d-frequency-512:
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" benchmark-native \
		BENCHMARK_CPU=8 BENCHMARK_MATCHES=512

validation-suite:
	@MSL_DATA_DIR="$(abspath $(MSL_DATA_DIR))" "$(PY)" -m tools.validation.validate_replay \
		--suite "$(VALIDATION_SUITE)" \
		--characters "$(VALIDATION_CHARACTERS)" --stages "$(VALIDATION_STAGES)" \
		--backend "$(VALIDATION_BACKEND)" --workers "$(VALIDATION_WORKERS)" \
		--build-jobs "$(VALIDATION_BUILD_JOBS)" --frames "$(VALIDATION_FRAMES)" \
		--timing $(VALIDATION_ARGS)

validation-supported-domain:
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile" validation-suite \
		VALIDATION_SUITE=replays/suites/melee_core_aggregate.json

validation-release-supported-domain: validator native-release
	@MSL_DATA_DIR="$(abspath $(MSL_DATA_DIR))" \
		MSL_CORE_NATIVE_BINARY="$(NATIVE_RELEASE_BUILD)/melee-core-native" \
		"$(PY)" -m tools.validation.validate_replay \
		--suite replays/suites/melee_core_aggregate.json --backend native \
		--workers "$(VALIDATION_WORKERS)" --no-build

test: source-check native-smoke python-library validator native ppc
	@"$(PY)" -m pytest -q

test-full: test ppc-smoke validation-supported-domain validation-release-supported-domain viewer-smoke

format-check:
	@git diff --check

slpz-convert:
	@"$(PY)" -m tools.validation.convert_replay_storage $(ARGS)

clean:
	@rm -rf "$(PPC_BUILD)" "$(NATIVE_BUILD)" "$(NATIVE_RELEASE_BUILD)" \
		"$(WASM_BUILD)" "$(PYTHON_BUILD)" "$(VALIDATION_BUILD)" \
		"$(BUILD_ROOT)/benchmark"

-include $(DEPS)
