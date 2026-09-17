// Tiny triangle rasterizer for the silhouette bake (built by raster.py).
#include <stdint.h>
#include <math.h>

static inline float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// tris: n * 6 floats (x0 y0 x1 y1 x2 y2) in pixel space; image: w*h bytes.
void msl_bake_fill(const float* tris, int n, uint8_t* image, int w, int h)
{
    int t;
    for (t = 0; t < n; ++t) {
        const float* p = tris + t * 6;
        float minx = fminf(p[0], fminf(p[2], p[4]));
        float maxx = fmaxf(p[0], fmaxf(p[2], p[4]));
        float miny = fminf(p[1], fminf(p[3], p[5]));
        float maxy = fmaxf(p[1], fmaxf(p[3], p[5]));
        int x0 = (int) floorf(minx), x1 = (int) ceilf(maxx);
        int y0 = (int) floorf(miny), y1 = (int) ceilf(maxy);
        float area = edge(p[0], p[1], p[2], p[3], p[4], p[5]);
        int y, x;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= w) x1 = w - 1;
        if (y1 >= h) y1 = h - 1;
        if (area == 0.0f) {
            continue;
        }
        for (y = y0; y <= y1; ++y) {
            float py = (float) y + 0.5f;
            for (x = x0; x <= x1; ++x) {
                float px = (float) x + 0.5f;
                float w0 = edge(p[2], p[3], p[4], p[5], px, py);
                float w1 = edge(p[4], p[5], p[0], p[1], px, py);
                float w2 = edge(p[0], p[1], p[2], p[3], px, py);
                if (area > 0.0f ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                : (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                    image[y * w + x] = 1;
                }
            }
        }
    }
}
