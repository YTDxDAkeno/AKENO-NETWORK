/*
 * Drawing for the SDL2 software renderer at 1920x1080: filled and outlined rounded rectangles
 * with anti-aliased corners, discs, gradients, the AKENO logo, the background, and text in Inter
 * (Regular/Bold) through a per-size glyph atlas. Colours are 0xAARRGGBB.
 */
#ifndef AKN_GFX_H
#define AKN_GFX_H

#include <SDL.h>
#include <stddef.h>
#include <stdint.h>

#include "nav.h"

typedef enum { AKN_FONT_REGULAR = 0, AKN_FONT_BOLD = 1 } akn_font_weight;

typedef struct akn_gfx akn_gfx;

akn_gfx *akn_gfx_create(SDL_Renderer *renderer, const unsigned char *regular_ttf, size_t regular_len,
                        const unsigned char *bold_ttf, size_t bold_len, char *err, size_t err_len);
void akn_gfx_destroy(akn_gfx *g);

void akn_gfx_rect(akn_gfx *g, akn_rect r, uint32_t argb);
void akn_gfx_rrect(akn_gfx *g, akn_rect r, int radius, uint32_t argb);
void akn_gfx_rrect_outline(akn_gfx *g, akn_rect r, int radius, int thickness, uint32_t argb);
/* Linear gradient from c0 to c1, top to bottom (vertical) or left to right. */
void akn_gfx_gradient(akn_gfx *g, akn_rect r, uint32_t c0, uint32_t c1, int vertical);
void akn_gfx_disc(akn_gfx *g, int cx, int cy, int radius, uint32_t argb);
void akn_gfx_ring(akn_gfx *g, int cx, int cy, int radius, int thickness, uint32_t argb);

/* Text: (x, y) is the top-left of the line box; returns the advance width in pixels. */
int akn_gfx_text(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s);
int akn_gfx_text_width(akn_gfx *g, int px, akn_font_weight weight, const char *s, size_t len);
/* Draws as much as fits in max_width, ending with an ellipsis when cut. */
int akn_gfx_text_fit(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s, int max_width);
/* Word-wrapped text; returns the number of lines drawn. */
int akn_gfx_text_wrap(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s,
                      int max_width, int line_height, int max_lines);
/* Text centred horizontally in [x, x + width). */
void akn_gfx_text_center(akn_gfx *g, int x, int width, int y, int px, akn_font_weight weight, uint32_t argb, const char *s);

void akn_gfx_logo(akn_gfx *g, int x, int y, int size);
/* The full-screen background (built once). */
void akn_gfx_background(akn_gfx *g);

/* Glyphs or font sizes that could not be cached (atlas or size table full): text was lost. */
unsigned long akn_gfx_glyph_misses(const akn_gfx *g);

#endif
