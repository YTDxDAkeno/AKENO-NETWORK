#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logo.h"
#include "stb_truetype.h"
#include "text.h"
#include "ui.h"

#define MAX_SIZES 32 /* (pixel size, weight) pairs; the screens use about 21 */
#define GLYPH_SLOTS 512 /* open-addressing table per size, power of two */
#define MAX_SHAPES 48
#define SS 4             /* supersampling for shape coverage */

typedef struct {
    uint32_t cp;
    int used, ok;
    int x, y, w, h;     /* atlas rectangle */
    int xoff, yoff;     /* from pen position / baseline */
    float advance;
    int glyph;          /* font glyph index */
} glyph_t;

typedef struct {
    int px;
    akn_font_weight weight;
    float scale;
    int ascent, line_height;
    SDL_Texture *atlas;
    akn_packer packer;
    glyph_t slots[GLYPH_SLOTS];
} face_t;

typedef struct {
    int kind; /* 0 corner fill, 1 corner ring, 2 disc, 3 ring */
    int radius, thickness;
    SDL_Texture *tex;
} shape_t;

struct akn_gfx {
    SDL_Renderer *r;
    stbtt_fontinfo font[2];
    face_t faces[MAX_SIZES];
    int nfaces;
    shape_t shapes[MAX_SHAPES];
    int nshapes;
    SDL_Texture *logo[4];
    int logo_size[4];
    SDL_Texture *background;
    unsigned long glyph_misses;
    unsigned char *scratch;
    size_t scratch_len;
    uint32_t *upload;
    size_t upload_len;
    char *capture; /* PC test scripts: every text run drawn in the current frame, one per line */
    size_t capture_len;
};

#define CAPTURE_MAX (64 * 1024)

static void capture_run(akn_gfx *g, const char *s, size_t len) {
    if (!g->capture || g->capture_len + len + 2 > CAPTURE_MAX) return;
    memcpy(g->capture + g->capture_len, s, len);
    g->capture_len += len;
    g->capture[g->capture_len++] = '\n';
    g->capture[g->capture_len] = '\0';
}

static void set_color(SDL_Renderer *r, uint32_t argb) {
    SDL_SetRenderDrawColor(r, (Uint8)(argb >> 16), (Uint8)(argb >> 8), (Uint8)argb, (Uint8)(argb >> 24));
}

static SDL_Texture *make_texture(SDL_Renderer *r, int w, int h, const uint32_t *argb) {
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    if (argb) SDL_UpdateTexture(t, NULL, argb, w * 4);
    return t;
}

akn_gfx *akn_gfx_create(SDL_Renderer *renderer, const unsigned char *regular_ttf, size_t regular_len,
                        const unsigned char *bold_ttf, size_t bold_len, char *err, size_t err_len) {
    akn_gfx *g = calloc(1, sizeof(*g));
    if (!g) {
        snprintf(err, err_len, "out of memory");
        return NULL;
    }
    g->r = renderer;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const unsigned char *data[2] = { regular_ttf, bold_ttf };
    size_t lens[2] = { regular_len, bold_len };
    for (int i = 0; i < 2; i++) {
        if (!data[i] || lens[i] < 12 || !stbtt_InitFont(&g->font[i], data[i], stbtt_GetFontOffsetForIndex(data[i], 0))) {
            snprintf(err, err_len, "the embedded font %d could not be read", i);
            free(g);
            return NULL;
        }
    }
    return g;
}

void akn_gfx_destroy(akn_gfx *g) {
    if (!g) return;
    for (int i = 0; i < g->nfaces; i++)
        if (g->faces[i].atlas) SDL_DestroyTexture(g->faces[i].atlas);
    for (int i = 0; i < g->nshapes; i++)
        if (g->shapes[i].tex) SDL_DestroyTexture(g->shapes[i].tex);
    for (int i = 0; i < 4; i++)
        if (g->logo[i]) SDL_DestroyTexture(g->logo[i]);
    if (g->background) SDL_DestroyTexture(g->background);
    free(g->scratch);
    free(g->upload);
    free(g->capture);
    free(g);
}

int akn_gfx_capture_enable(akn_gfx *g) {
    if (!g->capture) g->capture = calloc(1, CAPTURE_MAX);
    return g->capture ? 0 : -1;
}

void akn_gfx_capture_begin_frame(akn_gfx *g) {
    if (!g->capture) return;
    g->capture_len = 0;
    g->capture[0] = '\0';
}

int akn_gfx_capture_contains(const akn_gfx *g, const char *needle) { return g->capture && strstr(g->capture, needle) != NULL; }

static void *grow(void **buf, size_t *len, size_t need) {
    if (*len >= need) return *buf;
    void *n = realloc(*buf, need);
    if (!n) return NULL;
    *buf = n;
    *len = need;
    return n;
}

/* ---- Shapes ---------------------------------------------------------------------------------- */

/* Coverage of a disc of radius `outer` minus `inner` (0 = filled), sampled at pixel (px, py) of
 * a texture whose disc centre is (cx, cy). */
static double ring_coverage(double cx, double cy, double outer, double inner, int px, int py) {
    int hits = 0;
    for (int sy = 0; sy < SS; sy++)
        for (int sx = 0; sx < SS; sx++) {
            double x = px + (sx + 0.5) / SS - cx, y = py + (sy + 0.5) / SS - cy;
            double d2 = x * x + y * y;
            if (d2 <= outer * outer && (inner <= 0 || d2 >= inner * inner)) hits++;
        }
    return (double)hits / (SS * SS);
}

static SDL_Texture *shape(akn_gfx *g, int kind, int radius, int thickness) {
    for (int i = 0; i < g->nshapes; i++)
        if (g->shapes[i].kind == kind && g->shapes[i].radius == radius && g->shapes[i].thickness == thickness)
            return g->shapes[i].tex;
    int size = (kind <= 1) ? radius : radius * 2;
    if (size <= 0 || size > 512) return NULL;
    uint32_t *px = grow((void **)&g->upload, &g->upload_len, (size_t)size * size * 4);
    if (!px) return NULL;
    /* Corners are the top-left quarter: centre at (radius, radius). Discs: centre in the middle. */
    double c = radius;
    double inner = (kind == 1 || kind == 3) ? radius - thickness : 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            double a = ring_coverage(c, c, radius, inner, x, y);
            px[y * size + x] = ((uint32_t)(a * 255.0 + 0.5) << 24) | 0xFFFFFFu;
        }
    SDL_Texture *t = make_texture(g->r, size, size, px);
    if (!t) return NULL;
    if (g->nshapes == MAX_SHAPES) { /* recycle the oldest */
        SDL_DestroyTexture(g->shapes[0].tex);
        memmove(&g->shapes[0], &g->shapes[1], sizeof(shape_t) * (MAX_SHAPES - 1));
        g->nshapes--;
    }
    shape_t *s = &g->shapes[g->nshapes++];
    s->kind = kind;
    s->radius = radius;
    s->thickness = thickness;
    s->tex = t;
    return t;
}

static void tinted_copy(akn_gfx *g, SDL_Texture *t, const SDL_Rect *dst, uint32_t argb, SDL_RendererFlip flip) {
    SDL_SetTextureColorMod(t, (Uint8)(argb >> 16), (Uint8)(argb >> 8), (Uint8)argb);
    SDL_SetTextureAlphaMod(t, (Uint8)(argb >> 24));
    if (flip == SDL_FLIP_NONE) SDL_RenderCopy(g->r, t, NULL, dst);
    else SDL_RenderCopyEx(g->r, t, NULL, dst, 0, NULL, flip);
}

void akn_gfx_rect(akn_gfx *g, akn_rect r, uint32_t argb) {
    if (r.w <= 0 || r.h <= 0) return;
    SDL_Rect s = { r.x, r.y, r.w, r.h };
    set_color(g->r, argb);
    SDL_RenderFillRect(g->r, &s);
}

static int clamp_radius(akn_rect r, int radius) {
    if (radius * 2 > r.w) radius = r.w / 2;
    if (radius * 2 > r.h) radius = r.h / 2;
    return radius < 0 ? 0 : radius;
}

static void corners(akn_gfx *g, akn_rect r, int radius, SDL_Texture *t, uint32_t argb) {
    SDL_Rect tl = { r.x, r.y, radius, radius };
    SDL_Rect tr = { r.x + r.w - radius, r.y, radius, radius };
    SDL_Rect bl = { r.x, r.y + r.h - radius, radius, radius };
    SDL_Rect br = { r.x + r.w - radius, r.y + r.h - radius, radius, radius };
    tinted_copy(g, t, &tl, argb, SDL_FLIP_NONE);
    tinted_copy(g, t, &tr, argb, SDL_FLIP_HORIZONTAL);
    tinted_copy(g, t, &bl, argb, SDL_FLIP_VERTICAL);
    /* SDL documents flip values as combinable flags. */
    tinted_copy(g, t, &br, argb, (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
}

void akn_gfx_rrect(akn_gfx *g, akn_rect r, int radius, uint32_t argb) {
    radius = clamp_radius(r, radius);
    SDL_Texture *t = radius > 0 ? shape(g, 0, radius, 0) : NULL;
    if (!t) {
        akn_gfx_rect(g, r, argb);
        return;
    }
    corners(g, r, radius, t, argb);
    akn_rect mid = { r.x + radius, r.y, r.w - 2 * radius, r.h };
    akn_rect left = { r.x, r.y + radius, radius, r.h - 2 * radius };
    akn_rect right = { r.x + r.w - radius, r.y + radius, radius, r.h - 2 * radius };
    akn_gfx_rect(g, mid, argb);
    akn_gfx_rect(g, left, argb);
    akn_gfx_rect(g, right, argb);
}

void akn_gfx_rrect_outline(akn_gfx *g, akn_rect r, int radius, int thickness, uint32_t argb) {
    radius = clamp_radius(r, radius);
    if (thickness <= 0) return;
    if (thickness > radius && radius > 0) thickness = radius;
    SDL_Texture *t = radius > 0 ? shape(g, 1, radius, thickness) : NULL;
    if (t) corners(g, r, radius, t, argb);
    int c = t ? radius : 0;
    akn_rect top = { r.x + c, r.y, r.w - 2 * c, thickness };
    akn_rect bottom = { r.x + c, r.y + r.h - thickness, r.w - 2 * c, thickness };
    akn_rect left = { r.x, r.y + (c ? c : thickness), thickness, r.h - 2 * (c ? c : thickness) };
    akn_rect right = { r.x + r.w - thickness, r.y + (c ? c : thickness), thickness, r.h - 2 * (c ? c : thickness) };
    akn_gfx_rect(g, top, argb);
    akn_gfx_rect(g, bottom, argb);
    akn_gfx_rect(g, left, argb);
    akn_gfx_rect(g, right, argb);
}

void akn_gfx_gradient(akn_gfx *g, akn_rect r, uint32_t c0, uint32_t c1, int vertical) {
    SDL_Vertex v[4];
    SDL_Color a = { (Uint8)(c0 >> 16), (Uint8)(c0 >> 8), (Uint8)c0, (Uint8)(c0 >> 24) };
    SDL_Color b = { (Uint8)(c1 >> 16), (Uint8)(c1 >> 8), (Uint8)c1, (Uint8)(c1 >> 24) };
    float x0 = (float)r.x, y0 = (float)r.y, x1 = (float)(r.x + r.w), y1 = (float)(r.y + r.h);
    v[0].position.x = x0, v[0].position.y = y0, v[0].color = a;
    v[1].position.x = x1, v[1].position.y = y0, v[1].color = vertical ? a : b;
    v[2].position.x = x1, v[2].position.y = y1, v[2].color = b;
    v[3].position.x = x0, v[3].position.y = y1, v[3].color = vertical ? b : a;
    for (int i = 0; i < 4; i++) v[i].tex_coord.x = v[i].tex_coord.y = 0;
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(g->r, NULL, v, 4, idx, 6);
}

void akn_gfx_disc(akn_gfx *g, int cx, int cy, int radius, uint32_t argb) {
    SDL_Texture *t = shape(g, 2, radius, 0);
    if (!t) return;
    SDL_Rect d = { cx - radius, cy - radius, radius * 2, radius * 2 };
    tinted_copy(g, t, &d, argb, SDL_FLIP_NONE);
}

void akn_gfx_ring(akn_gfx *g, int cx, int cy, int radius, int thickness, uint32_t argb) {
    SDL_Texture *t = shape(g, 3, radius, thickness);
    if (!t) return;
    SDL_Rect d = { cx - radius, cy - radius, radius * 2, radius * 2 };
    tinted_copy(g, t, &d, argb, SDL_FLIP_NONE);
}

/* ---- Text ------------------------------------------------------------------------------------ */

static face_t *face(akn_gfx *g, int px, akn_font_weight weight) {
    for (int i = 0; i < g->nfaces; i++)
        if (g->faces[i].px == px && g->faces[i].weight == weight) return &g->faces[i];
    if (g->nfaces == MAX_SIZES || px < 6 || px > 200) {
        g->glyph_misses++; /* reported: text in this size cannot be drawn */
        return NULL;
    }
    face_t *f = &g->faces[g->nfaces];
    memset(f, 0, sizeof(*f));
    const stbtt_fontinfo *font = &g->font[weight];
    f->px = px;
    f->weight = weight;
    f->scale = stbtt_ScaleForPixelHeight(font, (float)px);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &gap);
    f->ascent = (int)ceilf(ascent * f->scale);
    f->line_height = (int)ceilf((ascent - descent + gap) * f->scale);
    int size = px <= 32 ? 512 : 1024;
    f->atlas = make_texture(g->r, size, size, NULL);
    if (!f->atlas) {
        g->glyph_misses++;
        return NULL;
    }
    akn_packer_init(&f->packer, size, size, 2);
    g->nfaces++;
    return f;
}

static glyph_t *glyph(akn_gfx *g, face_t *f, uint32_t cp) {
    uint32_t h = (cp * 2654435761u) & (GLYPH_SLOTS - 1);
    for (int probe = 0; probe < GLYPH_SLOTS; probe++) {
        glyph_t *gl = &f->slots[(h + (uint32_t)probe) & (GLYPH_SLOTS - 1)];
        if (gl->used && gl->cp == cp) return gl;
        if (gl->used) continue;
        /* New glyph: rasterise into the atlas. */
        const stbtt_fontinfo *font = &g->font[f->weight];
        gl->used = 1;
        gl->cp = cp;
        int index = stbtt_FindGlyphIndex(font, (int)cp);
        if (index == 0 && cp != '?') index = stbtt_FindGlyphIndex(font, 0xFFFD);
        if (index == 0) index = stbtt_FindGlyphIndex(font, '?');
        gl->glyph = index;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(font, index, &adv, &lsb);
        gl->advance = adv * f->scale;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(font, index, f->scale, f->scale, &x0, &y0, &x1, &y1);
        gl->w = x1 - x0;
        gl->h = y1 - y0;
        gl->xoff = x0;
        gl->yoff = y0;
        gl->ok = 1;
        if (gl->w <= 0 || gl->h <= 0) {
            gl->w = gl->h = 0; /* space */
            return gl;
        }
        if (akn_packer_add(&f->packer, gl->w, gl->h, &gl->x, &gl->y) != 0) {
            gl->ok = 0;
            g->glyph_misses++;
            return gl;
        }
        size_t n = (size_t)gl->w * gl->h;
        unsigned char *mono = grow((void **)&g->scratch, &g->scratch_len, n);
        uint32_t *argb = grow((void **)&g->upload, &g->upload_len, n * 4);
        if (!mono || !argb) {
            gl->ok = 0;
            return gl;
        }
        stbtt_MakeGlyphBitmap(font, mono, gl->w, gl->h, gl->w, f->scale, f->scale, index);
        for (size_t i = 0; i < n; i++) argb[i] = ((uint32_t)mono[i] << 24) | 0xFFFFFFu;
        SDL_Rect dst = { gl->x, gl->y, gl->w, gl->h };
        SDL_UpdateTexture(f->atlas, &dst, argb, gl->w * 4);
        return gl;
    }
    return NULL;
}

typedef struct {
    akn_gfx *g;
    face_t *f;
} measure_ctx;

static int measure_face(akn_gfx *g, face_t *f, const char *s, size_t len) {
    if (!f) return 0;
    float x = 0;
    uint32_t prev = 0;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp;
        size_t n = akn_utf8_next(s + pos, len - pos, &cp);
        if (!n) break;
        pos += n;
        glyph_t *gl = glyph(g, f, cp);
        if (!gl) continue;
        if (prev) x += stbtt_GetGlyphKernAdvance(&g->font[f->weight], (int)prev, gl->glyph) * f->scale;
        x += gl->advance;
        prev = (uint32_t)gl->glyph;
    }
    return (int)ceilf(x);
}

static int measure_cb(void *user, const char *s, size_t len) {
    measure_ctx *m = user;
    return measure_face(m->g, m->f, s, len);
}

int akn_gfx_text_width(akn_gfx *g, int px, akn_font_weight weight, const char *s, size_t len) {
    return measure_face(g, face(g, px, weight), s, len);
}

static int draw_run(akn_gfx *g, face_t *f, int x, int y, uint32_t argb, const char *s, size_t len) {
    capture_run(g, s, len);
    if (!f || !f->atlas) return 0;
    SDL_SetTextureColorMod(f->atlas, (Uint8)(argb >> 16), (Uint8)(argb >> 8), (Uint8)argb);
    SDL_SetTextureAlphaMod(f->atlas, (Uint8)(argb >> 24));
    float pen = (float)x;
    int baseline = y + f->ascent;
    uint32_t prev = 0;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp;
        size_t n = akn_utf8_next(s + pos, len - pos, &cp);
        if (!n) break;
        pos += n;
        glyph_t *gl = glyph(g, f, cp);
        if (!gl) continue;
        if (prev) pen += stbtt_GetGlyphKernAdvance(&g->font[f->weight], (int)prev, gl->glyph) * f->scale;
        if (gl->ok && gl->w > 0) {
            SDL_Rect src = { gl->x, gl->y, gl->w, gl->h };
            SDL_Rect dst = { (int)floorf(pen + 0.5f) + gl->xoff, baseline + gl->yoff, gl->w, gl->h };
            SDL_RenderCopy(g->r, f->atlas, &src, &dst);
        }
        pen += gl->advance;
        prev = (uint32_t)gl->glyph;
    }
    return (int)ceilf(pen) - x;
}

int akn_gfx_text(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s) {
    return draw_run(g, face(g, px, weight), x, y, argb, s, strlen(s));
}

int akn_gfx_text_fit(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s, int max_width) {
    face_t *f = face(g, px, weight);
    if (!f) return 0;
    measure_ctx m = { g, f };
    static const char ellipsis[] = "\xE2\x80\xA6"; /* U+2026 */
    int ew = measure_face(g, f, ellipsis, 3);
    int truncated;
    size_t n = akn_text_fit(s, max_width, ew, measure_cb, &m, &truncated);
    int w = draw_run(g, f, x, y, argb, s, n);
    if (truncated) w += draw_run(g, f, x + w, y, argb, ellipsis, 3);
    return w;
}

int akn_gfx_text_wrap(akn_gfx *g, int x, int y, int px, akn_font_weight weight, uint32_t argb, const char *s,
                      int max_width, int line_height, int max_lines) {
    face_t *f = face(g, px, weight);
    if (!f) return 0;
    measure_ctx m = { g, f };
    int lines = 0;
    while (*s && lines < max_lines) {
        size_t next;
        size_t n = akn_text_wrap(s, max_width, measure_cb, &m, &next);
        if (lines == max_lines - 1 && s[next]) { /* last allowed line: cut with an ellipsis */
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", s);
            akn_gfx_text_fit(g, x, y + lines * line_height, px, weight, argb, buf, max_width);
            return lines + 1;
        }
        draw_run(g, f, x, y + lines * line_height, argb, s, n);
        lines++;
        if (next <= 0) break;
        s += next;
    }
    return lines;
}

int akn_gfx_text_block(akn_gfx *g, int px, akn_font_weight weight, const char *s, int max_width, int max_lines, int *widest) {
    *widest = 0;
    face_t *f = face(g, px, weight);
    if (!f) return 0;
    measure_ctx m = { g, f };
    int lines = 0;
    while (*s && lines < max_lines) {
        size_t next;
        size_t n = akn_text_wrap(s, max_width, measure_cb, &m, &next);
        int w = measure_cb(&m, s, n);
        if (lines == max_lines - 1 && s[next]) w = max_width; /* cut with an ellipsis */
        if (w > *widest) *widest = w;
        lines++;
        if (next <= 0) break;
        s += next;
    }
    return lines;
}

void akn_gfx_text_center(akn_gfx *g, int x, int width, int y, int px, akn_font_weight weight, uint32_t argb, const char *s) {
    int w = akn_gfx_text_width(g, px, weight, s, strlen(s));
    akn_gfx_text(g, x + (width - w) / 2, y, px, weight, argb, s);
}

unsigned long akn_gfx_glyph_misses(const akn_gfx *g) { return g->glyph_misses; }

/* ---- Logo and background --------------------------------------------------------------------- */

void akn_gfx_logo(akn_gfx *g, int x, int y, int size) {
    int slot = -1;
    for (int i = 0; i < 4; i++)
        if (g->logo[i] && g->logo_size[i] == size) slot = i;
    if (slot < 0) {
        for (int i = 0; i < 4 && slot < 0; i++)
            if (!g->logo[i]) slot = i;
        if (slot < 0 || size < 8 || size > 1024) return;
        uint8_t *rgba = malloc((size_t)size * size * 4);
        uint32_t *argb = grow((void **)&g->upload, &g->upload_len, (size_t)size * size * 4);
        if (!rgba || !argb) {
            free(rgba);
            return;
        }
        akn_logo_style style = akn_logo_default_style();
        style.background = 0x121a2f; /* --akeno-surface-2, as the website's header mark */
        akn_logo_render(rgba, size, size * 4, &style);
        for (int i = 0; i < size * size; i++)
            argb[i] = ((uint32_t)rgba[i * 4 + 3] << 24) | ((uint32_t)rgba[i * 4] << 16) | ((uint32_t)rgba[i * 4 + 1] << 8) |
                      rgba[i * 4 + 2];
        free(rgba);
        g->logo[slot] = make_texture(g->r, size, size, argb);
        g->logo_size[slot] = size;
        if (!g->logo[slot]) return;
    }
    SDL_Rect d = { x, y, size, size };
    SDL_RenderCopy(g->r, g->logo[slot], NULL, &d);
}

static double glow(double x, double y, double cx, double cy, double rx, double ry) {
    double dx = (x - cx) / rx, dy = (y - cy) / ry;
    double d = dx * dx + dy * dy;
    return d >= 1 ? 0 : (1 - d) * (1 - d);
}

void akn_gfx_background(akn_gfx *g) {
    if (!g->background) {
        const int w = AKN_SCREEN_W, h = AKN_SCREEN_H;
        uint32_t *px = malloc((size_t)w * h * 4);
        if (!px) return;
        for (int y = 0; y < h; y++) {
            double t = (double)y / h;
            /* --akeno-canvas #060912 deepening towards the bottom */
            double base_r = 6 + 3 * (1 - t), base_g = 9 + 4 * (1 - t), base_b = 18 + 10 * (1 - t);
            for (int x = 0; x < w; x++) {
                double r = base_r, gg = base_g, b = base_b;
                double top = glow(x, y, 960, -160, 1300, 640);    /* the website's backdrop glow */
                double violet = glow(x, y, 1780, 1180, 1050, 700); /* --akeno-violet */
                double blue = glow(x, y, 80, 980, 900, 560);       /* --akeno-accent */
                r += 120 * 0.10 * top + 0x8b * 0.13 * violet + 0x4f * 0.10 * blue;
                gg += 140 * 0.10 * top + 0x5c * 0.13 * violet + 0x7c * 0.10 * blue;
                b += 200 * 0.10 * top + 0xf6 * 0.13 * violet + 0xff * 0.10 * blue;
                /* Faint diagonal light streaks in the upper right. */
                double d = fmod(x * 0.70710678 + y * 0.70710678 + 4000.0, 220.0);
                if (x > 1100 && y < 420 && d < 2.0) {
                    double fade = (1 - (double)y / 420) * ((x - 1100) / 820.0);
                    r += 18 * fade, gg += 22 * fade, b += 34 * fade;
                }
                uint32_t R = r > 255 ? 255 : (uint32_t)r, G = gg > 255 ? 255 : (uint32_t)gg, B = b > 255 ? 255 : (uint32_t)b;
                px[(size_t)y * w + x] = 0xFF000000u | (R << 16) | (G << 8) | B;
            }
        }
        g->background = make_texture(g->r, w, h, px);
        free(px);
        if (g->background) SDL_SetTextureBlendMode(g->background, SDL_BLENDMODE_NONE);
    }
    if (g->background) SDL_RenderCopy(g->r, g->background, NULL, NULL);
}
