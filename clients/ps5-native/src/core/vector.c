#include "vector.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CURVE_STEPS 12
#define PI_F 3.14159265358979f

/* ---- Parsing ------------------------------------------------------------------------------------ */

typedef struct {
    const char *s;
    int bad;
} lexer;

static void skip(lexer *lx) {
    while (*lx->s && (isspace((unsigned char)*lx->s) || *lx->s == ',')) lx->s++;
}

static int at_number(lexer *lx) {
    skip(lx);
    char c = *lx->s;
    return c == '-' || c == '+' || c == '.' || isdigit((unsigned char)c);
}

static float number(lexer *lx) {
    skip(lx);
    char *end;
    float v = strtof(lx->s, &end);
    if (end == lx->s) {
        lx->bad = 1;
        return 0;
    }
    lx->s = end;
    return v;
}

/* Arc flags are single digits and may be written without separators ("a1 1 0 011 2"). */
static int flag(lexer *lx) {
    skip(lx);
    if (*lx->s == '0' || *lx->s == '1') return *lx->s++ - '0';
    lx->bad = 1;
    return 0;
}

static int add_point(akn_path *p, float x, float y) {
    if (p->npts >= AKN_PATH_MAX_POINTS || p->nsub == 0) return -1;
    p->pts[p->npts++] = (akn_pt){ x, y };
    p->count[p->nsub - 1]++;
    return 0;
}

static int new_subpath(akn_path *p, float x, float y) {
    if (p->nsub >= AKN_PATH_MAX_SUBPATHS) return -1;
    p->start[p->nsub] = p->npts;
    p->count[p->nsub] = 0;
    p->closed[p->nsub] = 0;
    p->nsub++;
    return add_point(p, x, y);
}

static int cubic(akn_path *p, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
    for (int i = 1; i <= CURVE_STEPS; i++) {
        float t = (float)i / CURVE_STEPS, u = 1 - t;
        float x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
        float y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
        if (add_point(p, x, y)) return -1;
    }
    return 0;
}

static int quad(akn_path *p, float x0, float y0, float x1, float y1, float x2, float y2) {
    for (int i = 1; i <= CURVE_STEPS; i++) {
        float t = (float)i / CURVE_STEPS, u = 1 - t;
        if (add_point(p, u * u * x0 + 2 * u * t * x1 + t * t * x2, u * u * y0 + 2 * u * t * y1 + t * t * y2)) return -1;
    }
    return 0;
}

static float vec_angle(float ux, float uy, float vx, float vy) {
    float dot = ux * vx + uy * vy, len = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
    float a = len > 0 ? acosf(fmaxf(-1, fminf(1, dot / len))) : 0;
    return ux * vy - uy * vx < 0 ? -a : a;
}

/* SVG 1.1 implementation notes F.6.5: endpoint to centre parameterisation. */
static int arc(akn_path *p, float x1, float y1, float rx, float ry, float phi_deg, int large, int sweep, float x2, float y2) {
    rx = fabsf(rx), ry = fabsf(ry);
    if (rx == 0 || ry == 0 || (x1 == x2 && y1 == y2)) return add_point(p, x2, y2);
    float phi = phi_deg * PI_F / 180, c = cosf(phi), s = sinf(phi);
    float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    float xp = c * dx + s * dy, yp = -s * dx + c * dy;
    float lambda = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry);
    if (lambda > 1) rx *= sqrtf(lambda), ry *= sqrtf(lambda);
    float num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
    float den = rx * rx * yp * yp + ry * ry * xp * xp;
    float k = den > 0 && num > 0 ? sqrtf(num / den) : 0;
    if (large == sweep) k = -k;
    float cxp = k * rx * yp / ry, cyp = -k * ry * xp / rx;
    float cx = c * cxp - s * cyp + (x1 + x2) / 2, cy = s * cxp + c * cyp + (y1 + y2) / 2;
    float t1 = vec_angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry);
    float dt = vec_angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
    if (!sweep && dt > 0) dt -= 2 * PI_F;
    if (sweep && dt < 0) dt += 2 * PI_F;
    int steps = (int)ceilf(fabsf(dt) / (PI_F / 16));
    if (steps < 2) steps = 2;
    for (int i = 1; i <= steps; i++) {
        float t = t1 + dt * i / steps;
        float ex = rx * cosf(t), ey = ry * sinf(t);
        if (add_point(p, c * ex - s * ey + cx, s * ex + c * ey + cy)) return -1;
    }
    return 0;
}

int akn_path_parse(const char *d, akn_path *p) {
    lexer lx = { d, 0 };
    float cx = 0, cy = 0, sx = 0, sy = 0; /* current point, subpath start */
    float lcx = 0, lcy = 0;               /* last control point (S, T) */
    char cmd = 0, prev = 0;
    for (;;) {
        skip(&lx);
        if (!*lx.s) break;
        if (isalpha((unsigned char)*lx.s)) cmd = *lx.s++;
        else if (!cmd) return -1;
        int rel = islower((unsigned char)cmd);
        float ox = rel ? cx : 0, oy = rel ? cy : 0;
        int rc = 0;
        switch (tolower((unsigned char)cmd)) {
        case 'm': {
            float x = number(&lx) + ox, y = number(&lx) + oy;
            rc = new_subpath(p, x, y);
            cx = sx = x, cy = sy = y;
            cmd = rel ? 'l' : 'L'; /* further pairs are line-tos */
            break;
        }
        case 'l': {
            float x = number(&lx) + ox, y = number(&lx) + oy;
            rc = add_point(p, x, y);
            cx = x, cy = y;
            break;
        }
        case 'h':
            cx = number(&lx) + ox;
            rc = add_point(p, cx, cy);
            break;
        case 'v':
            cy = number(&lx) + oy;
            rc = add_point(p, cx, cy);
            break;
        case 'c': {
            float x1 = number(&lx) + ox, y1 = number(&lx) + oy, x2 = number(&lx) + ox, y2 = number(&lx) + oy;
            float x = number(&lx) + ox, y = number(&lx) + oy;
            rc = cubic(p, cx, cy, x1, y1, x2, y2, x, y);
            lcx = x2, lcy = y2, cx = x, cy = y;
            break;
        }
        case 's': {
            int smooth = prev == 'c' || prev == 's';
            float x1 = smooth ? 2 * cx - lcx : cx, y1 = smooth ? 2 * cy - lcy : cy;
            float x2 = number(&lx) + ox, y2 = number(&lx) + oy, x = number(&lx) + ox, y = number(&lx) + oy;
            rc = cubic(p, cx, cy, x1, y1, x2, y2, x, y);
            lcx = x2, lcy = y2, cx = x, cy = y;
            break;
        }
        case 'q': {
            float x1 = number(&lx) + ox, y1 = number(&lx) + oy, x = number(&lx) + ox, y = number(&lx) + oy;
            rc = quad(p, cx, cy, x1, y1, x, y);
            lcx = x1, lcy = y1, cx = x, cy = y;
            break;
        }
        case 't': {
            int smooth = prev == 'q' || prev == 't';
            float x1 = smooth ? 2 * cx - lcx : cx, y1 = smooth ? 2 * cy - lcy : cy;
            float x = number(&lx) + ox, y = number(&lx) + oy;
            rc = quad(p, cx, cy, x1, y1, x, y);
            lcx = x1, lcy = y1, cx = x, cy = y;
            break;
        }
        case 'a': {
            float rx = number(&lx), ry = number(&lx), rot = number(&lx);
            int large = flag(&lx), sweep = flag(&lx);
            float x = number(&lx) + ox, y = number(&lx) + oy;
            rc = arc(p, cx, cy, rx, ry, rot, large, sweep, x, y);
            cx = x, cy = y;
            break;
        }
        case 'z':
            if (p->nsub) p->closed[p->nsub - 1] = 1;
            cx = sx, cy = sy;
            break;
        default: return -1;
        }
        if (rc || lx.bad) return -1;
        prev = (char)tolower((unsigned char)cmd);
        if (prev == 'z') {
            cmd = 0; /* a new command letter must follow */
            continue;
        }
        if (!at_number(&lx)) cmd = 0; /* the next token must be a command */
        if (cmd == 0) {
            skip(&lx);
            if (*lx.s && !isalpha((unsigned char)*lx.s)) return -1;
        }
    }
    return 0;
}

void akn_path_transform(akn_path *p, float s, float tx, float ty) {
    for (int i = 0; i < p->npts; i++) p->pts[i] = (akn_pt){ p->pts[i].x * s + tx, p->pts[i].y * s + ty };
}

/* ---- Rasterising -------------------------------------------------------------------------------- */

static void blend(uint8_t *px, uint32_t argb, float coverage) {
    float a = ((argb >> 24) & 255) / 255.0f * coverage;
    if (a <= 0) return;
    float da = px[3] / 255.0f, oa = a + da * (1 - a);
    for (int c = 0; c < 3; c++) {
        float sc = (float)((argb >> (16 - 8 * c)) & 255), dc = px[c];
        px[c] = (uint8_t)((sc * a + dc * da * (1 - a)) / oa + 0.5f);
    }
    px[3] = (uint8_t)(oa * 255 + 0.5f);
}

static int inside_evenodd(const akn_path *p, float x, float y) {
    int in = 0;
    for (int s = 0; s < p->nsub; s++) {
        const akn_pt *pts = p->pts + p->start[s];
        int n = p->count[s];
        for (int i = 0, j = n - 1; i < n; j = i++) {
            if ((pts[i].y > y) != (pts[j].y > y) && x < (pts[j].x - pts[i].x) * (y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) in = !in;
        }
    }
    return in;
}

void akn_raster_fill(uint8_t *rgba, int size, const akn_path *p, float scale, uint32_t argb) {
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                    hits += inside_evenodd(p, (x + (sx + 0.5f) / 4) / scale, (y + (sy + 0.5f) / 4) / scale);
            if (hits) blend(rgba + ((size_t)y * size + x) * 4, argb, hits / 16.0f);
        }
}

static float seg_dist2(float px, float py, akn_pt a, akn_pt b) {
    float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = a.x + t * dx - px, ey = a.y + t * dy - py;
    return ex * ex + ey * ey;
}

void akn_raster_stroke(uint8_t *rgba, int size, const akn_path *p, float scale, float width, uint32_t argb) {
    float r = width / 2, r2 = r * r;
    /* bounding box of the path, grown by the half width */
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < p->npts; i++) {
        minx = fminf(minx, p->pts[i].x), miny = fminf(miny, p->pts[i].y);
        maxx = fmaxf(maxx, p->pts[i].x), maxy = fmaxf(maxy, p->pts[i].y);
    }
    int x0 = (int)floorf((minx - r) * scale), y0 = (int)floorf((miny - r) * scale);
    int x1 = (int)ceilf((maxx + r) * scale), y1 = (int)ceilf((maxy + r) * scale);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > size) x1 = size;
    if (y1 > size) y1 = size;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = (x + (sx + 0.5f) / 4) / scale, py = (y + (sy + 0.5f) / 4) / scale;
                    int hit = 0;
                    for (int s = 0; s < p->nsub && !hit; s++) {
                        const akn_pt *pts = p->pts + p->start[s];
                        int n = p->count[s];
                        if (n == 1) hit = seg_dist2(px, py, pts[0], pts[0]) <= r2;
                        for (int i = 1; i < n && !hit; i++) hit = seg_dist2(px, py, pts[i - 1], pts[i]) <= r2;
                        if (!hit && p->closed[s] && n > 2) hit = seg_dist2(px, py, pts[n - 1], pts[0]) <= r2;
                    }
                    hits += hit;
                }
            if (hits) blend(rgba + ((size_t)y * size + x) * 4, argb, hits / 16.0f);
        }
}
