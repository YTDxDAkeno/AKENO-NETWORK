/*
 * AKENO NETWORK's original mark (apps/web/public/favicon.svg, 64x64 view box): a rounded square,
 * an angular "A" filled with the blue-to-violet gradient and a core dot. Rasterised in code with
 * 4x4 supersampling so the console app and the launcher icon need no SVG or image library.
 */
#ifndef AKN_LOGO_H
#define AKN_LOGO_H

#include <stdint.h>

typedef struct {
    uint32_t background; /* 0xRRGGBB of the rounded square */
    int draw_background; /* 0: transparent around the "A" */
    uint32_t dot;        /* 0xRRGGBB of the core dot */
} akn_logo_style;

/* The website's colours: background #0d1323, dot #e9edf8, gradient #4f7cff -> #8b5cf6. */
akn_logo_style akn_logo_default_style(void);

/*
 * Writes size*size pixels, 4 bytes each in memory order R, G, B, A (straight alpha), row pitch
 * `pitch` bytes. size must be at least 8.
 */
void akn_logo_render(uint8_t *rgba, int size, int pitch, const akn_logo_style *style);

#endif
