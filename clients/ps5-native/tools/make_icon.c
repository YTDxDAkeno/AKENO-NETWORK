/*
 * Generates the launcher icon (sce_sys/icon0.png, 512x512) from AKENO's original mark: the
 * website's dark surface with a soft blue/violet glow and the gradient "A". Built and run on the
 * PC at build time, so the icon is reproducible from source and no binary image is committed.
 *
 *   make-icon <out.png> [size]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "logo.h"
#include "png.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: make-icon <out.png> [size]\n");
        return 2;
    }
    int size = argc > 2 ? atoi(argv[2]) : 512;
    if (size < 64 || size > 2048) {
        fprintf(stderr, "size must be 64..2048\n");
        return 2;
    }
    uint8_t *px = malloc((size_t)size * size * 4);
    int mark = size * 62 / 100;
    uint8_t *logo = malloc((size_t)mark * mark * 4);
    if (!px || !logo) {
        free(px);
        free(logo);
        return 1;
    }

    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            double fx = (double)x / size, fy = (double)y / size;
            /* #0d1323 -> #121a2f, with blue light from the top left and violet from the bottom right */
            double r = 13 + 5 * fy, g = 19 + 7 * fy, b = 35 + 12 * fy;
            double d1 = (fx - 0.25) * (fx - 0.25) + (fy - 0.2) * (fy - 0.2);
            double d2 = (fx - 0.85) * (fx - 0.85) + (fy - 0.9) * (fy - 0.9);
            double gb = exp(-d1 * 7.0) * 0.28, gv = exp(-d2 * 6.0) * 0.30;
            r += 0x4f * gb + 0x8b * gv;
            g += 0x7c * gb + 0x5c * gv;
            b += 0xff * gb + 0xf6 * gv;
            uint8_t *p = px + ((size_t)y * size + x) * 4;
            p[0] = (uint8_t)(r > 255 ? 255 : r);
            p[1] = (uint8_t)(g > 255 ? 255 : g);
            p[2] = (uint8_t)(b > 255 ? 255 : b);
            p[3] = 255;
        }

    akn_logo_style style = akn_logo_default_style();
    style.draw_background = 0;
    akn_logo_render(logo, mark, mark * 4, &style);
    int ox = (size - mark) / 2, oy = (size - mark) / 2 + size / 40;
    for (int y = 0; y < mark; y++)
        for (int x = 0; x < mark; x++) {
            const uint8_t *s = logo + ((size_t)y * mark + x) * 4;
            uint8_t *d = px + ((size_t)(y + oy) * size + (x + ox)) * 4;
            double a = s[3] / 255.0;
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a) + 0.5);
        }
    int rc = akn_png_write(argv[1], px, size, size, size * 4);
    free(px);
    free(logo);
    if (rc != 0) {
        fprintf(stderr, "could not write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
