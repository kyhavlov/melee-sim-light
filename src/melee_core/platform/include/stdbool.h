#ifndef MSL_CORE_PLATFORM_STDBOOL_H
#define MSL_CORE_PLATFORM_STDBOOL_H

// The GameCube MSL ABI represents C booleans as signed integers. Keep this
// ahead of the cross-sysroot's C99 stdbool.h so copied decomp structs retain
// their source layout and integer-valued fields typed as bool retain values
// beyond zero and one. See refs/melee/src/MSL/stdbool.h.
typedef int bool;

#define true 1
#define false 0

#endif
