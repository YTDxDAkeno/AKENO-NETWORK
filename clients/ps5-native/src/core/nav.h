/*
 * Spatial focus navigation for a TV interface: given the on-screen rectangles of the focusable
 * items, find the item a D-pad press should move the focus to. Portable and unit-tested; the
 * renderer only supplies rectangles.
 */
#ifndef AKN_NAV_H
#define AKN_NAV_H

typedef enum { AKN_DIR_UP = 0, AKN_DIR_DOWN, AKN_DIR_LEFT, AKN_DIR_RIGHT } akn_dir;

typedef struct {
    int x, y, w, h;
} akn_rect;

typedef struct {
    akn_rect rect;
    int enabled; /* 0: skipped by navigation (still drawn) */
} akn_nav_item;

/*
 * Index of the item to focus when moving from `current` in `dir`, or `current` when nothing lies
 * in that direction. A candidate's near edge must lie beyond the current item's centre in that
 * direction.
 * Items overlapping the current one on the perpendicular axis (same row or column) win over
 * closer items that do not; ties go to the smaller perpendicular offset of the centres.
 * A `current` outside 0..count-1 selects the first enabled item (or -1 if there is none).
 */
int akn_nav_next(const akn_nav_item *items, int count, int current, akn_dir dir);

/* First enabled item, or -1. */
int akn_nav_first(const akn_nav_item *items, int count);

#endif
