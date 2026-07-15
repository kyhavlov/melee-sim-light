#ifndef MSL_CORE_PLATFORM_SLIPPI_H
#define MSL_CORE_PLATFORM_SLIPPI_H

#include <platform.h>

struct Fighter;

void msl_slippi_lcancel_set(struct Fighter* fp, u8 value);
u8 msl_slippi_lcancel_get(const struct Fighter* fp);

#endif
