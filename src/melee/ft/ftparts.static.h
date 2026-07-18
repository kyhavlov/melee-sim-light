#include <dolphin/mtx.h>

/// .bss
/*
 * Dynamic matrix-setup scratch, live only while a JObj tree is being walked.
 * Keep it out of persistent MatchState while preventing cross-thread races.
 * refs/melee/src/melee/ft/ftparts.c::ftParts_JObjMakePositionMtx
 */
#ifdef MSL_CORE_HOSTED
static _Thread_local struct {
#else
struct {
#endif
    Mtx mtx;
    u8 has_z_scale : 1;
    char unk_31[7];
} ft_jobj_scale;
