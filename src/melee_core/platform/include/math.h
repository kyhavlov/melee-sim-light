#ifndef MSL_CORE_PLATFORM_MATH_H
#define MSL_CORE_PLATFORM_MATH_H

#include_next <math.h>

// Gameplay sources expect this convenience macro from the GameCube MSL
// header, while the hosted port intentionally uses libc's declarations.
#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TAU
#define M_TAU 6.283185307179586
#endif
#define M_PI_2 (M_PI / 2.0)
#define M_PI_3 (M_PI / 3.0)
#define M_PI_F 3.14159265358979323846F
#define M_TAU_F 6.283185307179586F
#define M_PI_2_F (M_PI_F / 2.0F)
#define M_PI_3_F (M_PI_F / 3.0F)

static const float deg_to_rad = (float) (M_PI / 180.0);
static const float rad_to_deg = (float) (180.0 / M_PI);

#endif
