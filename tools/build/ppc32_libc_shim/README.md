Minimal libc header shims for cross-compiling the PPC32 DWARF reference
object (`native_dat_types_ppc.o`) with a hosted clang and `-nostdlibinc`,
so no PowerPC glibc sysroot (and no Docker) is needed. The object is
compile-only DWARF input for the layout generators: only declarations are
required, and clang's builtin freestanding headers (stddef.h, stdint.h,
stdarg.h, stdbool.h) provide the target-correct fundamental types.
