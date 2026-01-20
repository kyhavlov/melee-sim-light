#include "decomp/lb/lb_00ce.h"

#include <math.h>

int lb_8000D148(float point0_x, float point0_y, float point1_x, float point1_y, float point2_x,
                float point2_y, float threshold) {
  // Exact port of refs/melee/src/melee/lb/lb_00CE.c:163-225 (float math + branching preserved).
  float dist_01;
  float var_f0;
  {
    const float diff_01_y = point0_y - point1_y;
    const float diff_01_x = point1_x - point0_x;
    const float dist_squared_01 = (diff_01_x * diff_01_x) + (diff_01_y * diff_01_y);
    if (dist_squared_01 < 0.00001f) {
      return 0;
    }
    dist_01 = sqrtf(dist_squared_01);

    var_f0 = ((point0_x * point1_y) - (point0_y * point1_x)) +
             ((diff_01_x * point2_x) + (diff_01_y * point2_y));
    if (var_f0 < 0.0f) {
      var_f0 = -var_f0;
    }
  }

  if ((var_f0 / dist_01) <= threshold) {
    const float diff_02_x = point0_x - point2_x;
    const float diff_02_y = point0_y - point2_y;
    const float diff_12_x = point1_x - point2_x;
    const float diff_12_y = point1_y - point2_y;
    const float threshold_squared = threshold * threshold;
    const float dist_squared_02 = (diff_02_x * diff_02_x) + (diff_02_y * diff_02_y);
    const float dist_squared_12 = (diff_12_x * diff_12_x) + (diff_12_y * diff_12_y);
    if (dist_squared_02 < threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        return 1;
      }
      if (dist_squared_12 < threshold_squared) {
        return 0;
      }
      return 1;
    }
    if (dist_squared_02 > threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        // If an axis of point0 and point1 are on opposite sides of point2, return true.
        if (((point0_x > point2_x) && (point1_x < point2_x)) ||
            ((point0_x < point2_x) && (point1_x > point2_x)) ||
            ((point0_y > point2_y) && (point1_y < point2_y)) ||
            ((point0_y < point2_y) && (point1_y > point2_y))) {
          return 1;
        }
        return 0;
      }
      if (dist_squared_12 < threshold_squared) {
        return 1;
      }
      return 1;
    }
    return 1;
  }
  return 0;
}

