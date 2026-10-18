/*
 * AKENO community badges as the website and the console page show them (packages/shared/src/
 * badges.ts): names, medallion colours and the original medallion — a hexagon in the badge colour
 * with a line icon — drawn from the same vector data (src/core/badges.c).
 */
#ifndef AKN_BADGES_H
#define AKN_BADGES_H

#include <stdint.h>

typedef struct {
    const char *id;
    const char *name;
    uint32_t color; /* 0xRRGGBB */
    const char *icon[6]; /* SVG path data in a 24x24 box, NULL-terminated */
} akn_badge_def;

const akn_badge_def *akn_badge_find(const char *id);
int akn_badge_count(void);
const akn_badge_def *akn_badge_at(int i);

/* Draws the medallion of badge id (an unknown id gets a grey medallion without icon) into
 * size x size RGBA pixels (straight alpha, transparent background). 0, or -1 on bad arguments. */
int akn_badge_render(const char *id, uint8_t *rgba, int size);

#endif
