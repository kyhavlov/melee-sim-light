#!/usr/bin/env bash
# Windows build of the Python core library and the native runner, with
# MinGW-w64, from Git Bash or MSYS2.
#
# The root Makefile cannot run here: a stock Windows box has no GNU make, and
# the link line is ELF-only (-Wl,-z,defs, -Bsymbolic). This builds what the
# `python-library` and `native` targets build, and adds what a PE target needs:
#
#   * tools/build/win_shim on the include path (sys/mman.h, dlfcn.h, link.h
#     and realpath, each a few lines over the Win32 call it stands for);
#   * an export list for the DLL, from the MSL_API and MSL_PYTHON_API
#     declarations (PE has no symbol visibility);
#   * abort stubs (tools/build/generate_win_stubs.py) for the unreachable
#     decomp calls that the ELF link drops with --gc-sections and the PE link
#     cannot, generated from the link's own undefined-symbol list.
#
# Writes only into build/. Run it from anywhere:
#
#   bash tools/build/build_windows.sh
#
# Outputs:
#   build/melee_core/python/libmelee_core.dll    (melee_sim finds it there)
#   build/melee_core/native/melee-core-native.exe (the runner)
#
# Environment:
#   CC    host compiler (default: gcc, MinGW-w64 x86-64)
#   PY    Python for the code generators (default: .venv/Scripts/python.exe)
#   OPT   optimization level for the core (default: -O2)
#   JOBS  parallel compiles (default: the number of processors)
#
# Needs binutils' objdump on PATH (MinGW-w64 ships it) for the host DWARF.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# Paths inside a linker response file are never MSYS-converted, so the whole
# build works in Windows form.
case "$(uname -s)" in
  MINGW*|MSYS*) ROOT="$(cd "$ROOT" && pwd -W)" ;;
esac
CORE="$ROOT/src"
SHIM="$ROOT/tools/build/win_shim"
BUILD="$ROOT/build/melee_core"
GEN="$BUILD/native/generated"
OBJ="$BUILD/python/obj"

CC="${CC:-gcc}"
PY="${PY:-$ROOT/.venv/Scripts/python.exe}"
OPT="${OPT:--O2}"
JOBS="${JOBS:-${NUMBER_OF_PROCESSORS:-4}}"

CPPFLAGS=(
  -DBUGFIX
  -include "$CORE/platform/compat.h"
  -I"$CORE" -I"$CORE/platform/include" -I"$CORE/melee"
  -I"$CORE/melee/ft/chara" -I"$CORE/sysdolphin" -I"$CORE/Runtime"
  -I"$CORE/extern/dolphin/include"
  -I"$SHIM"
)
NATIVE_CPPFLAGS=("${CPPFLAGS[@]}" -DMSL_CORE_NATIVE -I"$GEN")
PYTHON_CPPFLAGS=("${NATIVE_CPPFLAGS[@]}" -DMSL_CORE_SHARED -D_GNU_SOURCE
                 -include "$SHIM/msl_realpath.h")
# Everything is built at $OPT, where the Makefile keeps most gameplay at -O0.
# The three -fno flags take away the undefined-behaviour assumptions that make
# that unsafe for decomp code written against MWCC and PPC hardware; defined
# code compiles the same either way. The one that has bitten:
# ftPe_SpecialLw.c::pickVeg reads speciallw_item_table[idx + 1] one past the
# end on its last pass, GCC's loop optimizer concludes that pass never runs,
# and a Beam Sword pull returned table[-1].kind.
CFLAGS=("$OPT" -g -std=gnu11 -fgnu89-inline -fno-short-enums
        -fno-aggressive-loop-optimizations -fno-delete-null-pointer-checks
        -fwrapv -fno-strict-aliasing -ffp-contract=off -ffunction-sections
        -fdata-sections -w -Werror=implicit-function-declaration
        -Wno-int-conversion -Wno-incompatible-pointer-types -fno-pie -fPIC
        -fvisibility=hidden)
# Melee's matching MSL trig compiles to fmadds/fnmsubs; the Makefile gives
# these two the same treatment.
FAST_CFLAGS=(-O2 -ffp-contract=fast -fno-builtin-sinf -fno-builtin-cosf -mfma)

mkdir -p "$GEN" "$OBJ" "$BUILD/python" "$BUILD/native"
step() { printf '\n== %s\n' "$*"; }

step "host DWARF types"
"$CC" "${NATIVE_CPPFLAGS[@]}" -g -gdwarf-4 -w -fno-eliminate-unused-debug-types \
  -std=gnu11 -c "$ROOT/tools/build/native_dat_types.c" \
  -o "$GEN/native_dat_types_native.o"

step "generated command accessors and DAT layout"
"$PY" "$ROOT/tools/build/generate_command_accessors.py" \
  --ppc "$ROOT/tools/build/ppc_layout.json" --output "$GEN/msl_command_fields.h"
"$PY" "$ROOT/tools/build/generate_native_dat_layout.py" \
  --ppc "$ROOT/tools/build/ppc_layout.json" --native "$GEN/native_dat_types_native.o" \
  --output "$GEN/native_dat_layout.c"

step "generated match relocation layout"
"$CC" "${NATIVE_CPPFLAGS[@]}" -g -gdwarf-4 -w -fno-eliminate-unused-debug-types \
  -std=gnu11 -c "$ROOT/tools/build/match_reloc_types.c" \
  -o "$GEN/match_reloc_types.o"
(cd "$ROOT" && "$PY" "$ROOT/tools/build/generate_match_reloc_layout.py" \
  --object "$GEN/match_reloc_types.o" \
  --types "$CORE/runtime/relocation_types.def" \
  --output "$GEN/match_reloc_layout.c")

step "enumerating sources"
mapfile -t SRCS < <(
  cd "$ROOT"
  git ls-files --cached --others --exclude-standard -- src |
    grep '\.c$' | grep -v '^src/runtime/main\.c$'
)
SRCS+=("build/melee_core/native/generated/native_dat_layout.c")
SRCS+=("build/melee_core/native/generated/match_reloc_layout.c")
echo "${#SRCS[@]} translation units"

step "compiling ($JOBS jobs, $OPT)"
printf '%s\n' "${SRCS[@]}" | xargs -P "$JOBS" -I{} bash -c '
  rel="{}"; out="'"$OBJ"'/${rel%.c}.o"; mkdir -p "$(dirname "$out")"
  extra=""
  case "$rel" in
    src/MSL/trigf.c|src/runtime/math.c)
      extra="'"${FAST_CFLAGS[*]}"'" ;;
  esac
  "'"$CC"'" '"${PYTHON_CPPFLAGS[*]}"' '"${CFLAGS[*]}"' \
    -I"'"$ROOT"'/$(dirname "$rel")" $extra \
    -c "'"$ROOT"'/$rel" -o "$out" 2>"$out.log" \
    || { echo "FAILED $rel"; head -20 "$out.log"; exit 1; }
' || { echo "compile step failed"; exit 1; }

step "export list"
{
  echo "EXPORTS"
  grep -rhoE "MSL_(PYTHON_)?API[^;]*" "$CORE" --include='*.h' |
    grep -oE "msl_[a-z0-9_]+ *\(" | tr -d ' (' | sort -u | sed 's/^/  /'
} > "$BUILD/python/melee_core.def"
echo "$(($(wc -l < "$BUILD/python/melee_core.def") - 1)) exported functions"

step "linking libmelee_core.dll"
find "$OBJ" -name '*.o' | sort > "$BUILD/python/objects.txt"
link() {
  "$CC" -shared -Wl,--gc-sections -Wl,--exclude-all-symbols \
    "@$BUILD/python/objects.txt" "$BUILD/python/melee_core.def" \
    ${1:+"$1"} -static-libgcc -lm -o "$BUILD/python/libmelee_core.dll" 2>&1
}
missing="$BUILD/python/missing.txt"
: > "$missing"
stub_obj=""
for attempt in 1 2 3 4 5 6; do
  if [ -s "$missing" ]; then
    "$PY" "$ROOT/tools/build/generate_win_stubs.py" "$missing" \
      "$BUILD/python/win_stubs.c" > /dev/null
    "$CC" -O0 -w -c "$BUILD/python/win_stubs.c" -o "$BUILD/python/win_stubs.o"
    stub_obj="$BUILD/python/win_stubs.o"
  fi
  if link "$stub_obj" > "$BUILD/python/link.log" 2>&1; then
    echo "linked with $(wc -l < "$missing" | tr -d ' ') stubbed calls"
    break
  fi
  grep -o "undefined reference to \`[A-Za-z0-9_]*'" "$BUILD/python/link.log" |
    sed "s/.*\`//; s/'//" >> "$missing"
  sort -u "$missing" -o "$missing"
done
[ -f "$BUILD/python/libmelee_core.dll" ] || { cat "$BUILD/python/link.log"; exit 1; }

step "runner"
"$CC" "${NATIVE_CPPFLAGS[@]}" "${CFLAGS[@]}" -I"$CORE/runtime" \
  -c "$CORE/runtime/main.c" -o "$BUILD/native/main.o"
# MinGW links every executable against its default-manifest.o and passes
# that path to the linker unquoted, so a toolchain installed under a path
# with a space cannot link one. A copy beside the objects, found first
# through -B, sidesteps it.
manifest_obj="$("$CC" -print-file-name=default-manifest.o)"
manifest_dir=()
if [ -f "$manifest_obj" ]; then
  cp -f "$manifest_obj" "$BUILD/native/default-manifest.o"
  manifest_dir=(-B "$BUILD/native/")
fi
"$CC" "${manifest_dir[@]}" -g -Wl,--gc-sections "$BUILD/native/main.o" \
  "@$BUILD/python/objects.txt" ${stub_obj:+"$stub_obj"} \
  -static-libgcc -lm -o "$BUILD/native/melee-core-native.exe"

# ctypes and the runner need the MinGW thread runtime beside them.
for dir in "$BUILD/python" "$BUILD/native"; do
  found="$(dirname "$(command -v "$CC")")/libwinpthread-1.dll"
  [ -f "$found" ] && cp -f "$found" "$dir/" || true
done

ls -la "$BUILD/python/libmelee_core.dll" "$BUILD/native/melee-core-native.exe"
