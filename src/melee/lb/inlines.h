#ifndef MELEE_LB_INLINES_H
#define MELEE_LB_INLINES_H

// GCC's scalar_storage_order attribute gives the hosted build source-order
// bitfield views over authored big-endian command bytes. Clang (wasm32 and
// Mach-O hosts alike) silently ignores the attribute and would read the
// bytes little-endian, so it must use the DWARF-derived accessors instead.
#if defined(MSL_CORE_WASM) || defined(__clang__)
#include <msl_command_fields.h>
#define MSL_CMD_FIELD(command, owner, field)                                  \
    msl_cmd_##owner##_##field((command)->u)
#define MSL_CMD_OPCODE(pointer) msl_cmd_unk0_opcode(pointer)
#define MSL_COLOR_FIELD(pointer, owner, field)                                \
    msl_color_##owner##_##field(pointer)
#else
#define MSL_CMD_FIELD(command, owner, field) ((command)->u->owner.field)
#define MSL_CMD_OPCODE(pointer)                                               \
    gmScriptEventCast(pointer, gmScriptEventDefault)->opcode
#define MSL_COLOR_FIELD(pointer, owner, field) ((pointer)->owner.field)
#endif

/// @todo Is a macro the best way?
#define SKIP_CMD(cmd, n)                                                      \
    do {                                                                      \
        int i;                                                                \
        for (i = 0; i < (n); i++) {                                           \
            ++(cmd)->u;                                                       \
        }                                                                     \
    } while (0);

#define NEXT_CMD(cmd)                                                         \
    do {                                                                      \
        ++(cmd)->u;                                                           \
    } while (0);

#endif
