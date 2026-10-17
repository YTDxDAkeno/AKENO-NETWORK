#include "nav.h"

#include <limits.h>

int akn_nav_first(const akn_nav_item *items, int count) {
    for (int i = 0; i < count; i++)
        if (items[i].enabled) return i;
    return -1;
}

/* Gap between [a0, a1) and [b0, b1); 0 when they overlap. */
static long long range_gap(long long a0, long long a1, long long b0, long long b1) {
    if (b0 >= a1) return b0 - a1;
    if (a0 >= b1) return a0 - b1;
    return 0;
}

static long long absll(long long v) { return v < 0 ? -v : v; }

int akn_nav_next(const akn_nav_item *items, int count, int current, akn_dir dir) {
    if (!items || count <= 0) return -1;
    if (current < 0 || current >= count) return akn_nav_first(items, count);
    const akn_rect *c = &items[current].rect;
    /* Doubled centres keep everything in integers. */
    long long cx2 = 2LL * c->x + c->w, cy2 = 2LL * c->y + c->h;
    int best = current;
    long long best_score = LLONG_MAX;
    for (int i = 0; i < count; i++) {
        if (i == current || !items[i].enabled) continue;
        const akn_rect *r = &items[i].rect;
        long long rx2 = 2LL * r->x + r->w, ry2 = 2LL * r->y + r->h;
        long long along, gap_along, across;
        switch (dir) {
        case AKN_DIR_RIGHT:
            along = rx2 - cx2;
            gap_along = (long long)r->x - (c->x + c->w);
            across = range_gap(c->y, (long long)c->y + c->h, r->y, (long long)r->y + r->h);
            break;
        case AKN_DIR_LEFT:
            along = cx2 - rx2;
            gap_along = (long long)c->x - (r->x + r->w);
            across = range_gap(c->y, (long long)c->y + c->h, r->y, (long long)r->y + r->h);
            break;
        case AKN_DIR_DOWN:
            along = ry2 - cy2;
            gap_along = (long long)r->y - (c->y + c->h);
            across = range_gap(c->x, (long long)c->x + c->w, r->x, (long long)r->x + r->w);
            break;
        case AKN_DIR_UP:
        default:
            along = cy2 - ry2;
            gap_along = (long long)c->y - (r->y + r->h);
            across = range_gap(c->x, (long long)c->x + c->w, r->x, (long long)r->x + r->w);
            break;
        }
        if (along <= 0) continue; /* not beyond the current centre */
        /* The candidate's near edge must be past the current item's centre: a wide neighbour in
         * another row that merely reaches further does not count as "to the right". */
        if ((dir == AKN_DIR_RIGHT && 2LL * r->x < cx2) || (dir == AKN_DIR_LEFT && 2LL * (r->x + r->w) > cx2) ||
            (dir == AKN_DIR_DOWN && 2LL * r->y < cy2) || (dir == AKN_DIR_UP && 2LL * (r->y + r->h) > cy2))
            continue;
        if (gap_along < 0) gap_along = 0;
        long long centre_offset =
            (dir == AKN_DIR_LEFT || dir == AKN_DIR_RIGHT) ? absll(ry2 - cy2) : absll(rx2 - cx2);
        /* Same row/column first, then the nearest, then the best aligned. */
        long long score = (across > 0 ? (1LL << 40) : 0) + (gap_along + 3 * across) * 4096 + centre_offset;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}
