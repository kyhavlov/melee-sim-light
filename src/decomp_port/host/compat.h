#ifndef MSL_DECOMP_PORT_HOST_COMPAT_H
#define MSL_DECOMP_PORT_HOST_COMPAT_H

// The decomp's MWCC precompiled header provides this transitively. GCC does not.
#include <stdint.h>

#define MSL_DECOMP_PORT 1

// The decomp's MSL stddef supplies this spelling transitively. Hosted libc
// does not, while several exact translation units use it directly.
typedef unsigned int usize_t;

// baselib/debug.h names these MSL callback types, but the hosted build uses
// libc's stdio rather than placing the GameCube MSL headers ahead of libc.
typedef unsigned long __file_handle;
typedef void (*__idle_proc)(void);

#endif
