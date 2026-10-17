/*
 * A small SVG path renderer for AKENO's badge medallions: parses path data (M L H V C S Q T A Z,
 * absolute and relative), flattens curves and arcs, and rasterises fills (even-odd) and strokes
 * (round caps and joins) with 4x4 supersampling into an RGBA buffer. Portable and unit-tested;
 * used at runtime so the medallions are drawn from the same geometry as on the website.
 */
#ifndef AKN_VECTOR_H
#define AKN_VECTOR_H

#include <stdint.h>

#define AKN_PATH_MAX_POINTS 1024
#define AKN_PATH_MAX_SUBPATHS 32

typedef struct {
    float x, y;
} akn_pt;

typedef struct {
    akn_pt pts[AKN_PATH_MAX_POINTS];
    int npts;
    int start[AKN_PATH_MAX_SUBPATHS];
    int count[AKN_PATH_MAX_SUBPATHS];
    int closed[AKN_PATH_MAX_SUBPATHS];
    int nsub;
} akn_path;

/* Appends the path data d (flattened) to p. Returns 0, or -1 on malformed data or overflow. */
int akn_path_parse(const char *d, akn_path *p);
/* Applies x' = x * s + tx, y' = y * s + ty to every point. */
void akn_path_transform(akn_path *p, float s, float tx, float ty);

/* Composites onto size x size RGBA (straight alpha), path units * scale = pixels. argb = colour. */
void akn_raster_fill(uint8_t *rgba, int size, const akn_path *p, float scale, uint32_t argb);
void akn_raster_stroke(uint8_t *rgba, int size, const akn_path *p, float scale, float width, uint32_t argb);

#endif
