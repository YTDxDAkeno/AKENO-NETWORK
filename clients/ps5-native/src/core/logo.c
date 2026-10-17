#include "logo.h"

#define SS 4 /* subsamples per axis */

akn_logo_style akn_logo_default_style(void) {
    akn_logo_style s = { 0x0d1323, 1, 0xe9edf8 };
    return s;
}

/* The "A": M32 10 L52 52 H42 L32 30 L22 52 H12 Z */
static const double POLY[6][2] = { { 32, 10 }, { 52, 52 }, { 42, 52 }, { 32, 30 }, { 22, 52 }, { 12, 52 } };

static int in_polygon(double x, double y) {
    int inside = 0;
    for (int i = 0, j = 5; i < 6; j = i++) {
        double xi = POLY[i][0], yi = POLY[i][1], xj = POLY[j][0], yj = POLY[j][1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

static int in_rounded_square(double x, double y) {
    const double r = 16.0;
    if (x < 0 || y < 0 || x > 64 || y > 64) return 0;
    double cx = x < r ? r : (x > 64 - r ? 64 - r : x);
    double cy = y < r ? r : (y > 64 - r ? 64 - r : y);
    double dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static int in_dot(double x, double y) {
    double dx = x - 32, dy = y - 44;
    return dx * dx + dy * dy <= 16.0;
}

static void gradient(double x, double y, double rgb[3]) {
    /* objectBoundingBox gradient (0,0)->(1,1) over the path's box x 12..52, y 10..52. */
    double t = ((x - 12) / 40.0 + (y - 10) / 42.0) / 2.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    rgb[0] = 0x4f + (0x8b - 0x4f) * t;
    rgb[1] = 0x7c + (0x5c - 0x7c) * t;
    rgb[2] = 0xff + (0xf6 - 0xff) * t;
}

void akn_logo_render(uint8_t *rgba, int size, int pitch, const akn_logo_style *style) {
    if (!rgba || size < 8) return;
    const double scale = 64.0 / size;
    for (int py = 0; py < size; py++) {
        uint8_t *row = rgba + (long)py * pitch;
        for (int px = 0; px < size; px++) {
            double sum[3] = { 0, 0, 0 }, alpha = 0;
            for (int sy = 0; sy < SS; sy++) {
                for (int sx = 0; sx < SS; sx++) {
                    double x = (px + (sx + 0.5) / SS) * scale, y = (py + (sy + 0.5) / SS) * scale;
                    double rgb[3];
                    if (in_dot(x, y)) {
                        rgb[0] = (style->dot >> 16) & 255;
                        rgb[1] = (style->dot >> 8) & 255;
                        rgb[2] = style->dot & 255;
                    } else if (in_polygon(x, y)) {
                        gradient(x, y, rgb);
                    } else if (style->draw_background && in_rounded_square(x, y)) {
                        rgb[0] = (style->background >> 16) & 255;
                        rgb[1] = (style->background >> 8) & 255;
                        rgb[2] = style->background & 255;
                    } else {
                        continue; /* transparent subsample */
                    }
                    sum[0] += rgb[0];
                    sum[1] += rgb[1];
                    sum[2] += rgb[2];
                    alpha += 1;
                }
            }
            uint8_t *p = row + px * 4;
            if (alpha == 0) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)(sum[c] / alpha + 0.5);
            p[3] = (uint8_t)(alpha * 255.0 / (SS * SS) + 0.5);
        }
    }
}
