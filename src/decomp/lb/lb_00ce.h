#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Port of lb_8000D148 from Melee decomp.
// Source: refs/melee/src/melee/lb/lb_00CE.c:163-225
int lb_8000D148(float point0_x, float point0_y, float point1_x, float point1_y, float point2_x,
                float point2_y, float threshold);

#ifdef __cplusplus
}
#endif

