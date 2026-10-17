/*
 * Drawing of every screen at 1920x1080. Layout rectangles of focusable items come from ui.c so
 * what is drawn is exactly what navigation uses. Colours are the website's dark theme.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "badges.h"

#define C_INK 0xFFE9EDF8u
#define C_MUTED 0xFF9AA5C4u
#define C_SUBTLE 0xFF6B7697u
#define C_SURFACE 0xD90D1323u
#define C_SURFACE2 0xE6121A2Fu
#define C_SURFACE3 0xFF19233Du
#define C_LINE 0x2694A3D1u
#define C_LINE_STRONG 0x4294A3D1u
#define C_ACCENT 0xFF4F7CFFu
#define C_ACCENT_SOFT 0x334F7CFFu
#define C_VIOLET 0xFF8B5CF6u
#define C_SUCCESS 0xFF22C55Eu
#define C_WARNING 0xFFF5A524u
#define C_DANGER 0xFFF43F5Eu
#define C_OFFLINE 0xFF5B6584u

#define FOCUS_ANIM_MS 140.0

static struct {
    akn_rect from, to;
    uint64_t t0;
    int screen, dialog;
    int valid;
} g_ring;

static akn_rect inflate(akn_rect r, int d) {
    akn_rect o = { r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d };
    return o;
}

static uint32_t with_alpha(uint32_t argb, unsigned a) { return (argb & 0x00FFFFFFu) | ((uint32_t)(a & 255u) << 24); }

/* ---- Small pictograms (geometric, original) ----------------------------------------------------- */

static void quad(akn_gfx *g, SDL_Renderer *r, float x0, float y0, float x1, float y1, float t, uint32_t c) {
    (void)g;
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    SDL_Color col = { (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)(c >> 24) };
    SDL_Vertex v[4];
    v[0].position.x = x0 + nx, v[0].position.y = y0 + ny;
    v[1].position.x = x1 + nx, v[1].position.y = y1 + ny;
    v[2].position.x = x1 - nx, v[2].position.y = y1 - ny;
    v[3].position.x = x0 - nx, v[3].position.y = y0 - ny;
    for (int i = 0; i < 4; i++) v[i].color = col, v[i].tex_coord.x = v[i].tex_coord.y = 0;
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

static void triangle(SDL_Renderer *r, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    SDL_Color col = { (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)(c >> 24) };
    SDL_Vertex v[3];
    v[0].position.x = x0, v[0].position.y = y0;
    v[1].position.x = x1, v[1].position.y = y1;
    v[2].position.x = x2, v[2].position.y = y2;
    for (int i = 0; i < 3; i++) v[i].color = col, v[i].tex_coord.x = v[i].tex_coord.y = 0;
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

/* Face-button symbols for prompts and the controller test. */
typedef enum { SYM_CROSS, SYM_CIRCLE, SYM_SQUARE, SYM_TRIANGLE } symbol_t;

static void symbol(akn_app *app, symbol_t s, int cx, int cy, int size, uint32_t c) {
    akn_gfx *g = app->gfx;
    float h = size / 2.0f, t = size > 30 ? 4.0f : 3.0f;
    switch (s) {
    case SYM_CROSS:
        quad(g, app->renderer, cx - h * 0.62f, cy - h * 0.62f, cx + h * 0.62f, cy + h * 0.62f, t, c);
        quad(g, app->renderer, cx - h * 0.62f, cy + h * 0.62f, cx + h * 0.62f, cy - h * 0.62f, t, c);
        break;
    case SYM_CIRCLE: akn_gfx_ring(g, cx, cy, (int)(h * 0.72f), (int)t, c); break;
    case SYM_SQUARE: {
        int a = (int)(h * 1.2f);
        akn_rect sq = { cx - a / 2, cy - a / 2, a, a };
        akn_gfx_rrect_outline(g, sq, 2, (int)t, c);
        break;
    }
    case SYM_TRIANGLE:
        quad(g, app->renderer, cx, cy - h * 0.66f, cx + h * 0.66f, cy + h * 0.5f, t, c);
        quad(g, app->renderer, cx + h * 0.66f, cy + h * 0.5f, cx - h * 0.66f, cy + h * 0.5f, t, c);
        quad(g, app->renderer, cx - h * 0.66f, cy + h * 0.5f, cx, cy - h * 0.66f, t, c);
        break;
    }
}

static void tile_icon(akn_app *app, akn_item_id id, int cx, int cy, uint32_t c) {
    akn_gfx *g = app->gfx;
    SDL_Renderer *r = app->renderer;
    switch (id) {
    case AKN_ITEM_FRIENDS:
        akn_gfx_disc(g, cx - 9, cy - 9, 8, c);
        akn_gfx_disc(g, cx + 11, cy - 6, 6, with_alpha(c, 170));
        akn_gfx_rrect(g, (akn_rect){ cx - 22, cy + 3, 27, 15 }, 7, c);
        akn_gfx_rrect(g, (akn_rect){ cx + 3, cy + 4, 19, 12 }, 6, with_alpha(c, 170));
        break;
    case AKN_ITEM_MESSAGES:
        akn_gfx_rrect(g, (akn_rect){ cx - 20, cy - 16, 40, 27 }, 8, c);
        triangle(r, (float)cx - 10, (float)cy + 9, (float)cx - 2, (float)cy + 9, (float)cx - 12, (float)cy + 19, c);
        break;
    case AKN_ITEM_PARTIES:
        for (int i = -1; i <= 1; i++) akn_gfx_disc(g, cx + i * 15, cy - 6, 6, i ? with_alpha(c, 170) : c);
        akn_gfx_rrect(g, (akn_rect){ cx - 24, cy + 4, 48, 12 }, 6, with_alpha(c, 200));
        break;
    case AKN_ITEM_PROFILE:
        akn_gfx_ring(g, cx, cy, 20, 3, c);
        akn_gfx_disc(g, cx, cy - 5, 7, c);
        akn_gfx_rrect(g, (akn_rect){ cx - 11, cy + 5, 22, 9 }, 4, c);
        break;
    case AKN_ITEM_ACTIVITY:
        akn_gfx_rrect(g, (akn_rect){ cx - 19, cy - 2, 9, 18 }, 3, with_alpha(c, 170));
        akn_gfx_rrect(g, (akn_rect){ cx - 4, cy - 16, 9, 32 }, 3, c);
        akn_gfx_rrect(g, (akn_rect){ cx + 11, cy - 8, 9, 24 }, 3, with_alpha(c, 200));
        break;
    case AKN_ITEM_SETTINGS:
        akn_gfx_ring(g, cx, cy, 17, 5, c);
        akn_gfx_disc(g, cx, cy, 5, c);
        for (int i = 0; i < 4; i++) {
            double a = i * 3.14159265 / 4;
            quad(g, r, cx + (float)(cos(a) * 15), cy + (float)(sin(a) * 15), cx + (float)(cos(a) * 22), cy + (float)(sin(a) * 22), 6, c);
            quad(g, r, cx - (float)(cos(a) * 15), cy - (float)(sin(a) * 15), cx - (float)(cos(a) * 22), cy - (float)(sin(a) * 22), 6, c);
        }
        break;
    case AKN_ITEM_CONTROLLER:
        akn_gfx_rrect(g, (akn_rect){ cx - 24, cy - 12, 48, 26 }, 12, c);
        akn_gfx_disc(g, cx - 10, cy + 1, 4, 0xFF0D1323u);
        akn_gfx_disc(g, cx + 10, cy + 1, 4, 0xFF0D1323u);
        break;
    case AKN_ITEM_AUDIO:
        akn_gfx_rect(g, (akn_rect){ cx - 20, cy - 6, 8, 12 }, c);
        triangle(r, (float)cx - 12, (float)cy - 6, (float)cx, (float)cy - 16, (float)cx, (float)cy + 16, c);
        triangle(r, (float)cx - 12, (float)cy - 6, (float)cx, (float)cy + 16, (float)cx - 12, (float)cy + 6, c);
        quad(g, r, (float)cx + 7, (float)cy - 7, (float)cx + 7, (float)cy + 7, 3, with_alpha(c, 200));
        quad(g, r, (float)cx + 14, (float)cy - 13, (float)cx + 14, (float)cy + 13, 3, with_alpha(c, 140));
        break;
    case AKN_ITEM_CONNECTION:
        for (int i = 0; i < 4; i++) {
            int h = 8 + i * 8;
            akn_gfx_rrect(g, (akn_rect){ cx - 20 + i * 11, cy + 16 - h, 7, h }, 2, i < 3 ? c : with_alpha(c, 120));
        }
        break;
    case AKN_ITEM_SYSTEM:
        akn_gfx_rrect_outline(g, (akn_rect){ cx - 15, cy - 15, 30, 30 }, 5, 3, c);
        akn_gfx_rect(g, (akn_rect){ cx - 6, cy - 6, 12, 12 }, c);
        for (int i = -1; i <= 1; i++) {
            akn_gfx_rect(g, (akn_rect){ cx + i * 8 - 1, cy - 21, 3, 6 }, c);
            akn_gfx_rect(g, (akn_rect){ cx + i * 8 - 1, cy + 15, 3, 6 }, c);
        }
        break;
    case AKN_ITEM_EXIT:
        akn_gfx_rrect_outline(g, (akn_rect){ cx - 20, cy - 16, 22, 32 }, 4, 3, c);
        quad(g, r, (float)cx - 6, (float)cy, (float)cx + 20, (float)cy, 4, c);
        triangle(r, (float)cx + 24, (float)cy, (float)cx + 14, (float)cy - 9, (float)cx + 14, (float)cy + 9, c);
        break;
    default: break;
    }
}

/* ---- Common chrome ------------------------------------------------------------------------------ */

static void tracked(akn_gfx *g, int x, int y, int px, akn_font_weight w, uint32_t c, const char *s, int tracking) {
    char ch[2] = { 0, 0 };
    for (; *s; s++) {
        ch[0] = *s;
        x += akn_gfx_text(g, x, y, px, w, c, ch) + tracking;
    }
}

static void chip(akn_app *app, int right_x, int y, uint32_t dot, const char *text, int *out_x) {
    akn_gfx *g = app->gfx;
    int w = akn_gfx_text_width(g, 22, AKN_FONT_REGULAR, text, strlen(text)) + 58;
    akn_rect r = { right_x - w, y, w, 46 };
    akn_gfx_rrect(g, r, 23, 0xB30D1323u);
    akn_gfx_rrect_outline(g, r, 23, 1, C_LINE_STRONG);
    akn_gfx_disc(g, r.x + 24, y + 23, 6, dot);
    akn_gfx_text(g, r.x + 40, y + 9, 22, AKN_FONT_REGULAR, C_INK, text);
    *out_x = r.x - 14;
}

static void top_bar(akn_app *app, const akn_account_view *acc) {
    akn_gfx *g = app->gfx;
    akn_gfx_logo(g, AKN_MARGIN_X, 34, 60);
    tracked(g, 176, 38, 30, AKN_FONT_BOLD, C_INK, "AKENO", 6);
    tracked(g, 177, 74, 14, AKN_FONT_REGULAR, C_MUTED, "NETWORK", 6);
    akn_rect pill = { 360, 46, 210, 36 };
    akn_gfx_rrect(g, pill, 18, C_ACCENT_SOFT);
    akn_gfx_text_center(g, pill.x, pill.w, 52, 18, AKN_FONT_BOLD, 0xFFB7C8FFu, "NATIVE PREVIEW");

    int x = AKN_SCREEN_W - AKN_MARGIN_X;
    char text[120];
    if (acc->state == AKN_ACC_SIGNED_IN) {
        snprintf(text, sizeof(text), "@%s", acc->online_id);
        chip(app, x, 40, C_SUCCESS, text, &x);
    } else {
        uint32_t dot = acc->state == AKN_ACC_CHECKING || acc->state == AKN_ACC_UNCHECKED ? C_OFFLINE : C_WARNING;
        chip(app, x, 40, dot, akn_account_state_text(acc->state), &x);
    }
    if (app->pad) chip(app, x, 40, app->pad_disconnected ? C_WARNING : C_SUCCESS, "Controller", &x);
    else if (app->pad_error[0]) chip(app, x, 40, C_DANGER, "Controller error", &x);
    else chip(app, x, 40, C_OFFLINE, "No controller", &x);
}

static void prompt(akn_app *app, int *x, int y, symbol_t s, const char *label) {
    akn_gfx_disc(app->gfx, *x + 18, y + 18, 18, 0xFF222D4Au);
    symbol(app, s, *x + 18, y + 18, 22, C_INK);
    *x += 46;
    *x += akn_gfx_text(app->gfx, *x, y + 5, 22, AKN_FONT_REGULAR, C_MUTED, label) + 34;
}

static void bottom_bar(akn_app *app) {
    akn_gfx *g = app->gfx;
    akn_gfx_rect(g, (akn_rect){ AKN_MARGIN_X, 996, AKN_SCREEN_W - 2 * AKN_MARGIN_X, 1 }, C_LINE);
    int x = AKN_MARGIN_X, y = 1018;
    if (app->ui.dialog != AKN_DIALOG_NONE) {
        prompt(app, &x, y, SYM_CROSS, "Select");
        prompt(app, &x, y, SYM_CIRCLE, app->ui.dialog == AKN_DIALOG_HEARD ? "Not sure" : "Cancel");
    } else if (app->ui.screen == AKN_SCREEN_CONTROLLER) {
        prompt(app, &x, y, SYM_CIRCLE, "Hold 1 s to return");
    } else if (app->ui.screen == AKN_SCREEN_AUDIO && app->ui.audio_busy) {
        prompt(app, &x, y, SYM_CIRCLE, "Stop the tone");
    } else if (app->ui.screen == AKN_SCREEN_PROFILE) {
        prompt(app, &x, y, SYM_CIRCLE, "Back");
        prompt(app, &x, y, SYM_TRIANGLE, "Reload");
    } else if (akn_ui_is_list(app->ui.screen)) {
        prompt(app, &x, y, SYM_CROSS, "Profile");
        prompt(app, &x, y, SYM_CIRCLE, "Back");
        prompt(app, &x, y, SYM_TRIANGLE, "Reload");
    } else {
        prompt(app, &x, y, SYM_CROSS, "Select");
        prompt(app, &x, y, SYM_CIRCLE, app->ui.screen == AKN_SCREEN_HOME ? "Exit" : "Back");
    }
    char info[160];
    snprintf(info, sizeof(info), "AKENO NETWORK Native %s  \xC2\xB7  build %s  \xC2\xB7  %s %s", AKN_NATIVE_VERSION, AKN_BUILD_ID,
             app->platform.name, app->platform.firmware);
    int w = akn_gfx_text_width(g, 20, AKN_FONT_REGULAR, info, strlen(info));
    akn_gfx_text(g, AKN_SCREEN_W - AKN_MARGIN_X - w, y + 7, 20, AKN_FONT_REGULAR, C_SUBTLE, info);
}

static void screen_title(akn_app *app, const char *title, const char *subtitle) {
    akn_gfx_text(app->gfx, AKN_MARGIN_X, 150, 56, AKN_FONT_BOLD, C_INK, title);
    if (subtitle) akn_gfx_text_fit(app->gfx, AKN_MARGIN_X, 226, 26, AKN_FONT_REGULAR, C_MUTED, subtitle, 1600);
}

static void panel(akn_app *app, akn_rect r) {
    akn_gfx_rrect(app->gfx, r, 24, C_SURFACE);
    akn_gfx_rrect_outline(app->gfx, r, 24, 1, C_LINE);
}

/* Label/value rows; returns the next y. */
static int row(akn_app *app, int x, int y, int w, const char *label, uint32_t dot, const char *value) {
    akn_gfx *g = app->gfx;
    akn_gfx_text(g, x, y, 24, AKN_FONT_REGULAR, C_MUTED, label);
    int vx = x + 340;
    if (dot) {
        akn_gfx_disc(g, vx + 7, y + 16, 7, dot);
        vx += 26;
    }
    akn_gfx_text_fit(g, vx, y, 24, AKN_FONT_REGULAR, C_INK, value, w - (vx - x));
    return y + 50;
}

static void focus_ring(akn_app *app, akn_rect target, int radius, uint64_t now) {
    if (!g_ring.valid || g_ring.screen != (int)app->ui.screen || g_ring.dialog != (int)app->ui.dialog) {
        g_ring.from = g_ring.to = target;
        g_ring.t0 = now;
        g_ring.valid = 1;
        g_ring.screen = (int)app->ui.screen;
        g_ring.dialog = (int)app->ui.dialog;
    } else if (memcmp(&target, &g_ring.to, sizeof(target)) != 0) {
        double p = (now - g_ring.t0) / FOCUS_ANIM_MS;
        if (p > 1) p = 1;
        double e = 1 - (1 - p) * (1 - p) * (1 - p);
        akn_rect cur = { (int)(g_ring.from.x + (g_ring.to.x - g_ring.from.x) * e), (int)(g_ring.from.y + (g_ring.to.y - g_ring.from.y) * e),
                         (int)(g_ring.from.w + (g_ring.to.w - g_ring.from.w) * e), (int)(g_ring.from.h + (g_ring.to.h - g_ring.from.h) * e) };
        g_ring.from = cur;
        g_ring.to = target;
        g_ring.t0 = now;
    }
    double p = (now - g_ring.t0) / FOCUS_ANIM_MS;
    if (p > 1) p = 1;
    double e = 1 - (1 - p) * (1 - p) * (1 - p);
    akn_rect r = { (int)(g_ring.from.x + (g_ring.to.x - g_ring.from.x) * e), (int)(g_ring.from.y + (g_ring.to.y - g_ring.from.y) * e),
                   (int)(g_ring.from.w + (g_ring.to.w - g_ring.from.w) * e), (int)(g_ring.from.h + (g_ring.to.h - g_ring.from.h) * e) };
    double pulse = 0.5 + 0.5 * sin(now / 380.0);
    akn_gfx_rrect_outline(app->gfx, inflate(r, 10), radius + 10, 3, with_alpha(C_ACCENT, (unsigned)(40 + 40 * pulse)));
    akn_gfx_rrect_outline(app->gfx, inflate(r, 5), radius + 5, 4, C_ACCENT);
}

static void draw_items(akn_app *app, const akn_item *items, int n, int focused, uint64_t now) {
    akn_gfx *g = app->gfx;
    for (int i = 0; i < n; i++) {
        const akn_item *it = &items[i];
        int f = i == focused;
        akn_rect r = it->rect;
        if (it->kind == AKN_KIND_TILE) {
            akn_gfx_rrect(g, r, 20, f ? 0xF21A2542u : C_SURFACE2);
            if (f) akn_gfx_gradient(g, (akn_rect){ r.x + 20, r.y, r.w - 40, 3 }, C_ACCENT, C_VIOLET, 0);
            akn_gfx_rrect_outline(g, r, 20, 1, f ? C_LINE_STRONG : C_LINE);
            int icx = r.x + 56, icy = r.y + 58;
            akn_gfx_disc(g, icx, icy, 32, f ? 0x334F7CFFu : 0x1F94A3D1u);
            tile_icon(app, it->id, icx, icy, it->preview ? (f ? 0xFFB7C8FFu : C_MUTED) : (f ? C_INK : 0xFFB7C8FFu));
            if (it->preview) {
                akn_rect soon = { r.x + r.w - 96, r.y + 22, 72, 30 };
                akn_gfx_rrect(g, soon, 15, 0x268B5CF6u);
                akn_gfx_text_center(g, soon.x, soon.w, soon.y + 4, 16, AKN_FONT_BOLD, 0xFFC4B5FDu, "SOON");
            }
            akn_gfx_text_fit(g, r.x + 28, r.y + r.h - 88, 30, AKN_FONT_BOLD, it->preview && !f ? 0xFFC9D0E4u : C_INK, it->title, r.w - 48);
            akn_gfx_text_fit(g, r.x + 28, r.y + r.h - 48, 21, AKN_FONT_REGULAR, C_MUTED, it->subtitle, r.w - 48);
        } else if (it->kind == AKN_KIND_SELECTOR) {
            akn_gfx_rrect(g, r, 18, f ? 0xF21A2542u : C_SURFACE2);
            akn_gfx_rrect_outline(g, r, 18, 1, f ? C_LINE_STRONG : C_LINE);
            akn_gfx_text(g, r.x + 30, r.y + 16, 22, AKN_FONT_REGULAR, C_MUTED, it->title);
            akn_gfx_text_fit(g, r.x + 30, r.y + 50, 28, AKN_FONT_BOLD, it->enabled ? C_INK : C_SUBTLE, it->subtitle, r.w - 140);
            uint32_t ac = f && it->enabled ? C_INK : C_SUBTLE;
            float mid = (float)r.y + (float)r.h / 2.0f, right = (float)(r.x + r.w);
            triangle(app->renderer, right - 92, mid, right - 78, mid - 10, right - 78, mid + 10, ac);
            triangle(app->renderer, right - 32, mid, right - 46, mid - 10, right - 46, mid + 10, ac);
        } else {
            akn_gfx_rrect(g, r, 18, !it->enabled ? C_SURFACE3 : f ? C_ACCENT : 0xFF223056u);
            if (it->enabled && f) akn_gfx_gradient(g, (akn_rect){ r.x + 18, r.y + r.h - 4, r.w - 36, 4 }, C_ACCENT, C_VIOLET, 0);
            if (!f) akn_gfx_rrect_outline(g, r, 18, 1, C_LINE_STRONG);
            akn_gfx_text_center(g, r.x, r.w, r.y + (r.h - 34) / 2, 28, AKN_FONT_BOLD, it->enabled ? C_INK : C_SUBTLE, it->title);
        }
    }
    if (focused >= 0 && focused < n && items[focused].enabled)
        focus_ring(app, items[focused].rect, items[focused].kind == AKN_KIND_TILE ? 20 : 18, now);
}

/* ---- Social pieces (Phase 2.1) ------------------------------------------------------------------ */

static uint32_t presence_color(akn_presence p) {
    switch (p) {
    case AKN_PRES_ONLINE: return C_SUCCESS;
    case AKN_PRES_AWAY: return C_WARNING;
    case AKN_PRES_DND: return C_DANGER;
    default: return C_OFFLINE;
    }
}

/* A picture at exactly r (drawn 1:1, prepared at this size by the image worker). Returns 1 when it
 * was drawn; until it has loaded, or when it is missing or broken, the caller's placeholder stays. */
static int picture(akn_app *app, akn_img_source source, const char *id, akn_rect r, akn_fit fit) {
    SDL_Texture *t = id && id[0] ? akn_images_get(app->images, source, id, r.w, r.h, fit) : NULL;
    if (!t) return 0;
    SDL_Rect d = { r.x, r.y, r.w, r.h };
    SDL_RenderCopy(app->renderer, t, NULL, &d);
    return 1;
}

/* The profile picture (circle) over initials on a colour picked from the user ID, with a presence dot. */
static void avatar(akn_app *app, int cx, int cy, int r, const char *id, const char *display_name, const char *online_id,
                   const char *image, int show_presence, akn_presence p) {
    static const uint32_t palette[] = { 0xFF3B5BDBu, 0xFF7048E8u, 0xFF0C8599u, 0xFF2B8A3Eu, 0xFFC2255Cu, 0xFFE8590Cu, 0xFF5F3DC4u, 0xFF1971C2u };
    unsigned h = 2166136261u;
    for (const char *c = id && id[0] ? id : online_id; c && *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    akn_gfx_disc(app->gfx, cx, cy, r, palette[h % (sizeof(palette) / sizeof(palette[0]))]);
    char ini[8];
    akn_initials(display_name, online_id, ini);
    int px = r * 9 / 10;
    akn_gfx_text_center(app->gfx, cx - r, 2 * r, cy - px * 6 / 10, px, AKN_FONT_BOLD, 0xFFFFFFFFu, ini);
    picture(app, AKN_IMG_MEDIA, image, (akn_rect){ cx - r, cy - r, 2 * r, 2 * r }, AKN_FIT_CIRCLE);
    if (show_presence) {
        int dr = r / 4 > 7 ? r / 4 : 7;
        int dx = cx + (int)(r * 0.72), dy = cy + (int)(r * 0.72);
        akn_gfx_disc(app->gfx, dx, dy, dr + 4, 0xFF0D1323u);
        akn_gfx_disc(app->gfx, dx, dy, dr, presence_color(p));
    }
}

static void pill(akn_app *app, int x, int y, uint32_t bg, uint32_t fg, const char *text) {
    int w = akn_gfx_text_width(app->gfx, 16, AKN_FONT_BOLD, text, strlen(text)) + 28;
    akn_rect r = { x, y, w, 30 };
    akn_gfx_rrect(app->gfx, r, 15, bg);
    akn_gfx_text_center(app->gfx, r.x, r.w, r.y + 5, 16, AKN_FONT_BOLD, fg, text);
}

/* When the daemon's native interface is not available: what is wrong and what to do. */
static void ipc_status_panel(akn_app *app, const akn_social *s, akn_rect p) {
    akn_gfx *g = app->gfx;
    panel(app, p);
    int x = p.x + 44, y = p.y + 38, w = p.w - 88;
    akn_gfx_text_fit(g, x, y, 34, AKN_FONT_BOLD, s->ipc == AKN_IPC_UNCHECKED ? C_INK : C_WARNING, akn_ipc_state_text(s->ipc), w);
    y += 66;
    char text[600];
    switch (s->ipc) {
    case AKN_IPC_UNCHECKED: snprintf(text, sizeof(text), "AKENO Native reads your friends through the AKENO daemon on this console."); break;
    case AKN_IPC_NO_DAEMON:
        snprintf(text, sizeof(text),
                 "Start the AKENO daemon with your ELF loader as usual (the native-IPC build, akeno-network-ps5-native-ipc.elf, "
                 "for these screens), then press Triangle. AKENO Native checks again every few seconds.");
        break;
    case AKN_IPC_NOT_SUPPORTED:
        snprintf(text, sizeof(text),
                 "The daemon that is running (%s) is the Alpha 0.5 release, which has no native interface. To use Friends, "
                 "Profile and Activity here, stop it with \"Exit AKENO\" on its page and send akeno-network-ps5-native-ipc.elf "
                 "from this build instead. The controller and audio tests and the connection check work with either daemon.",
                 s->ipc_detail[0] ? s->ipc_detail : "an older version");
        break;
    case AKN_IPC_NO_KEY:
        snprintf(text, sizeof(text), "The daemon has not written its key file (/data/akeno/native.key). Stop the daemon and start it again.");
        break;
    case AKN_IPC_UNSAFE_KEY:
        snprintf(text, sizeof(text),
                 "/data/akeno/native.key is not a private file holding a key (a link, readable by others, or wrong content), so "
                 "AKENO Native does not use it. Stop the daemon and start it again: it writes a new key.");
        break;
    case AKN_IPC_KEY_REJECTED: snprintf(text, sizeof(text), "The daemon restarted and has a new key. Reconnecting..."); break;
    case AKN_IPC_READY: snprintf(text, sizeof(text), "%s", s->ipc_detail); break;
    }
    akn_gfx_text_wrap(g, x, y, 26, AKN_FONT_REGULAR, C_MUTED, text, w, 38, 6);
    akn_gfx_text_wrap(g, x, p.y + p.h - 84, 21, AKN_FONT_REGULAR, C_SUBTLE,
                      "The daemon keeps the only connection to your AKENO server, your notifications and the game detection. "
                      "AKENO Native only reads through it.",
                      w, 30, 2);
}

static void centered_note(akn_app *app, akn_rect p, const char *title, const char *text) {
    panel(app, p);
    akn_gfx_text_center(app->gfx, p.x, p.w, p.y + p.h / 2 - 50, 32, AKN_FONT_BOLD, C_INK, title);
    if (text) akn_gfx_text_center(app->gfx, p.x, p.w, p.y + p.h / 2 + 4, 24, AKN_FONT_REGULAR, C_MUTED, text);
}

static void game_art(akn_app *app, akn_rect r, const akn_art *art, const char *title_id, const char *name, akn_art_shape shape);

static void friend_row(akn_app *app, akn_rect r, const akn_friend *f, int focused, long long now) {
    akn_gfx *g = app->gfx;
    akn_gfx_rrect(g, r, 18, focused ? 0xF21A2542u : C_SURFACE2);
    akn_gfx_rrect_outline(g, r, 18, 1, focused ? C_LINE_STRONG : C_LINE);
    avatar(app, r.x + 50, r.y + r.h / 2, 30, f->id, f->display_name, f->online_id, f->avatar_image, 1, f->presence.status);
    int x = r.x + 100, w = r.w - 130;
    if (f->presence.game.present) { /* the game's icon at the right end of the row */
        const akn_game *gm = &f->presence.game;
        game_art(app, (akn_rect){ r.x + r.w - 76, r.y + (r.h - 60) / 2, 60, 60 }, &gm->art, gm->title_id, gm->name, AKN_ART_ICON);
        w -= 76;
    }
    int nw = akn_gfx_text_fit(g, x, r.y + 10, 27, AKN_FONT_BOLD, f->deleted ? C_SUBTLE : C_INK, f->display_name, w - 220);
    char id[48];
    snprintf(id, sizeof(id), "@%s", f->online_id);
    akn_gfx_text_fit(g, x + nw + 14, r.y + 15, 21, AKN_FONT_REGULAR, C_SUBTLE, id, w - nw - 14);
    char line[200];
    akn_presence_line(&f->presence, now, line, sizeof(line));
    akn_gfx_text_fit(g, x, r.y + 48, 22, AKN_FONT_REGULAR, f->presence.game.present ? 0xFFB7C8FFu : C_MUTED, line, w);
}

static void scroll_info(akn_app *app, akn_screen screen, int x_right, int y, int visible) {
    int count = app->ui.list_count[screen], top = app->ui.list_top[screen];
    if (count <= visible) return;
    char text[64];
    int last = top + visible < count ? top + visible : count;
    snprintf(text, sizeof(text), "%d\xE2\x80\x93%d of %d  \xC2\xB7  L1/R1 page", top + 1, last, count);
    int w = akn_gfx_text_width(app->gfx, 20, AKN_FONT_REGULAR, text, strlen(text));
    akn_gfx_text(app->gfx, x_right - w, y, 20, AKN_FONT_REGULAR, C_SUBTLE, text);
}

static void draw_friends(akn_app *app, const akn_social *s, uint64_t now) {
    akn_gfx *g = app->gfx;
    long long unix_now = akn_platform_unix_time();
    const akn_friend_list *list = s->friends;
    char sub[200];
    if (list) {
        snprintf(sub, sizeof(sub), "%d online  \xC2\xB7  %d playing  \xC2\xB7  %d friend request%s waiting%s", list->online, list->playing,
                 list->incoming_requests, list->incoming_requests == 1 ? "" : "s",
                 list->incoming_requests ? " (answer them on the AKENO page)" : "");
    } else {
        snprintf(sub, sizeof(sub), "Your AKENO friends, live from the daemon on this console.");
    }
    screen_title(app, "Friends", sub);
    akn_rect area = { AKN_MARGIN_X, 290, 1728, 660 };
    if (s->ipc != AKN_IPC_READY && !list) {
        ipc_status_panel(app, s, area);
        return;
    }
    if (!list || list->count == 0) {
        if (s->friends_load == AKN_LOAD_ERROR) centered_note(app, area, "Your friends could not be loaded", s->friends_error);
        else if (!list) centered_note(app, area, "Loading your friends...", NULL);
        else centered_note(app, area, "No friends yet", "Add friends on the AKENO website; they appear here right away.");
        return;
    }
    int focus = app->ui.focus[AKN_SCREEN_FRIENDS];
    akn_rect focus_rect = { 0, 0, 0, 0 };
    for (int i = app->ui.list_top[AKN_SCREEN_FRIENDS]; i < list->count && i < app->ui.list_top[AKN_SCREEN_FRIENDS] + AKN_FRIEND_ROWS_VISIBLE; i++) {
        akn_rect r = akn_ui_row_rect(&app->ui, AKN_SCREEN_FRIENDS, i);
        friend_row(app, r, &list->items[i], i == focus && !app->ui.dialog, unix_now);
        if (i == focus) focus_rect = r;
    }
    scroll_info(app, AKN_SCREEN_FRIENDS, AKN_MARGIN_X + 1000, 966, AKN_FRIEND_ROWS_VISIBLE);
    if (s->ipc != AKN_IPC_READY)
        akn_gfx_text(g, AKN_MARGIN_X, 966, 20, AKN_FONT_REGULAR, C_WARNING, "Not connected to the daemon: this list may be out of date.");

    /* The focused friend */
    akn_rect d = { 1136, 290, 688, 660 };
    panel(app, d);
    if (focus >= 0 && focus < list->count) {
        const akn_friend *f = &list->items[focus];
        int x = d.x + 40, w = d.w - 80, y = d.y + 40;
        avatar(app, d.x + d.w / 2, y + 74, 74, f->id, f->display_name, f->online_id, f->avatar_image, 1, f->presence.status);
        y += 176;
        akn_gfx_text_center(g, d.x, d.w, y, 38, AKN_FONT_BOLD, C_INK, f->display_name);
        char id[48];
        snprintf(id, sizeof(id), "@%s", f->online_id);
        akn_gfx_text_center(g, d.x, d.w, y + 52, 24, AKN_FONT_REGULAR, C_MUTED, id);
        y += 110;
        akn_gfx_disc(g, x + 8, y + 15, 8, presence_color(f->presence.status));
        akn_gfx_text(g, x + 28, y, 26, AKN_FONT_BOLD, C_INK, akn_presence_label(f->presence.status));
        y += 46;
        char text[300];
        if (f->presence.game.present) {
            snprintf(text, sizeof(text), "Playing %s", akn_game_label(&f->presence.game));
            akn_gfx_text_fit(g, x, y, 26, AKN_FONT_BOLD, 0xFFB7C8FFu, text, w);
            y += 40;
            if (f->presence.game.started_at > 0) {
                char dur[40];
                akn_format_duration(unix_now - f->presence.game.started_at, dur, sizeof(dur));
                snprintf(text, sizeof(text), "for %s  \xC2\xB7  %s", dur, f->presence.game.title_id);
                akn_gfx_text_fit(g, x, y, 22, AKN_FONT_REGULAR, C_MUTED, text, w);
                y += 40;
            }
        } else if (f->presence.status == AKN_PRES_OFFLINE && f->presence.last_seen > 0) {
            char ago[40];
            akn_format_ago(f->presence.last_seen, unix_now, ago, sizeof(ago));
            snprintf(text, sizeof(text), "Last seen %s", ago);
            akn_gfx_text_fit(g, x, y, 24, AKN_FONT_REGULAR, C_MUTED, text, w);
            y += 40;
        } else if (f->presence.activity[0]) {
            akn_gfx_text_fit(g, x, y, 24, AKN_FONT_REGULAR, C_MUTED, f->presence.activity, w);
            y += 40;
        }
        if (f->status_message[0]) {
            y += 10;
            akn_gfx_text_wrap(g, x, y, 23, AKN_FONT_REGULAR, C_INK, f->status_message, w, 32, 3);
        }
        akn_gfx_text_center(g, d.x, d.w, d.y + d.h - 56, 21, AKN_FONT_REGULAR, C_SUBTLE, "Cross: open the profile  \xC2\xB7  Triangle: reload");
    }
    if (focus_rect.w && !app->ui.dialog) focus_ring(app, focus_rect, 18, now);
}

/* ---- Pictures of the profiles: game artwork, badges ------------------------------------------------ */

/* Game artwork as on the website: the server's artwork of the verified game (cover, wide banner
 * or icon, falling back between them), else this console's own icon of the title, else initials. */
static void game_art(akn_app *app, akn_rect r, const akn_art *art, const char *title_id, const char *name, akn_art_shape shape) {
    akn_gfx *g = app->gfx;
    int radius = AKN_FIT_RADIUS(r.w, r.h);
    /* The verified server picture first; the console's icon only when the server has none or it
     * failed; else the initials (core/artwork.h). */
    const char *media = akn_art_media_for(art, shape);
    akn_pic_state server = AKN_PIC_NONE, local = AKN_PIC_NONE;
    SDL_Texture *st = media[0] ? akn_images_lookup(app->images, AKN_IMG_MEDIA, media, r.w, r.h, AKN_FIT_ROUNDED, &server) : NULL;
    SDL_Texture *lt = NULL;
    if (akn_art_want_local(media, server, title_id))
        lt = akn_images_lookup(app->images, AKN_IMG_LOCAL_ART, title_id, r.w, r.h, AKN_FIT_ROUNDED, &local);
    SDL_Rect d = { r.x, r.y, r.w, r.h };
    switch (akn_art_choose(media, server, title_id, local)) {
    case AKN_ART_SHOW_SERVER: SDL_RenderCopy(app->renderer, st, NULL, &d); break;
    case AKN_ART_SHOW_LOCAL: SDL_RenderCopy(app->renderer, lt, NULL, &d); break;
    case AKN_ART_SHOW_PLACEHOLDER: {
        akn_gfx_rrect(g, r, radius, 0xFF1E2A4Au);
        char ini[8];
        akn_initials(name && name[0] ? name : title_id, title_id, ini);
        int px = (r.w < r.h ? r.w : r.h) / 3;
        if (px > 12) akn_gfx_text_center(g, r.x, r.w, r.y + r.h / 2 - px * 6 / 10, px, AKN_FONT_BOLD, 0xFF9AA5C4u, ini);
        break;
    }
    }
    akn_gfx_rrect_outline(g, r, radius, 1, C_LINE_STRONG);
}

/* The badge medallion (badges.c, the website's geometry), cached per badge and size. */
static void badge_medal(akn_app *app, const char *id, int x, int y, int size) {
    int idx = -1;
    for (int i = 0; i < akn_badge_count() && i < 16; i++)
        if (strcmp(akn_badge_at(i)->id, id) == 0) idx = i;
    if (idx < 0) return; /* unknown to this build: shown like the website, i.e. not at all */
    int k = size <= 40 ? 0 : size <= 64 ? 1 : 2;
    int px = k == 0 ? 40 : k == 1 ? 64 : 96;
    SDL_Texture **t = &app->badge_tex[idx][k];
    if (!*t) {
        uint8_t *rgba = malloc((size_t)px * px * 4);
        if (rgba && akn_badge_render(id, rgba, px) == 0) {
            *t = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px, px);
            if (*t) {
                SDL_UpdateTexture(*t, NULL, rgba, px * 4);
                SDL_SetTextureBlendMode(*t, SDL_BLENDMODE_BLEND);
            }
        }
        free(rgba);
        if (!*t) return;
    }
    SDL_Rect d = { x, y, size, size };
    SDL_RenderCopy(app->renderer, *t, NULL, &d);
}

/* A featured badge: medallion and name on a chip. Returns the chip's width. */
static int badge_chip(akn_app *app, const char *id, int x, int y) {
    const akn_badge_def *def = akn_badge_find(id);
    if (!def) return 0;
    int tw = akn_gfx_text_width(app->gfx, 22, AKN_FONT_BOLD, def->name, strlen(def->name));
    akn_rect r = { x, y, tw + 82, 52 };
    akn_gfx_rrect(app->gfx, r, 26, 0xCC0D1323u);
    akn_gfx_rrect_outline(app->gfx, r, 26, 1, 0x80000000u | def->color);
    badge_medal(app, id, x + 8, y + 6, 40);
    akn_gfx_text(app->gfx, x + 60, y + 13, 22, AKN_FONT_BOLD, C_INK, def->name);
    return r.w;
}

/* A game card: cover, name, up to two lines. */
static void game_card(akn_app *app, int x, int y, int w, const akn_art *art, const char *title_id, const char *name, const char *l1,
                      const char *l2) {
    akn_gfx *g = app->gfx;
    int h = w * 3 / 2;
    game_art(app, (akn_rect){ x, y, w, h }, art, title_id, name, AKN_ART_COVER);
    akn_gfx_text_fit(g, x, y + h + 10, 22, AKN_FONT_BOLD, C_INK, name && name[0] ? name : title_id, w);
    if (l1 && l1[0]) akn_gfx_text_fit(g, x, y + h + 40, 19, AKN_FONT_REGULAR, C_MUTED, l1, w);
    if (l2 && l2[0]) akn_gfx_text_fit(g, x, y + h + 66, 19, AKN_FONT_REGULAR, C_SUBTLE, l2, w);
}

static int section_title(akn_app *app, int x, int y, const char *title) {
    akn_gfx_text(app->gfx, x, y, 20, AKN_FONT_BOLD, C_SUBTLE, title);
    return y + 40;
}

/* The profile page, laid out like the website's: banner, overlapping picture, name and presence,
 * badges, then games on the left and statistics and about on the right. It scrolls (up/down,
 * L1/R1); content coordinates start at AKN_PROFILE_VIEW_Y. */
#define CARD_W 160
#define CARD_GAP 32
#define CARDS_PER_ROW 6

static void draw_profile(akn_app *app, const akn_social *s, uint64_t now) {
    akn_gfx *g = app->gfx;
    (void)now;
    long long unix_now = akn_platform_unix_time();
    akn_rect area = { AKN_MARGIN_X, 150, 1728, 800 };
    if (!s->profile.online_id[0]) {
        akn_ui_set_profile_height(&app->ui, 0);
        if (s->ipc != AKN_IPC_READY) ipc_status_panel(app, s, area);
        else if (s->profile_load == AKN_LOAD_ERROR) centered_note(app, area, "The profile could not be loaded", s->profile_error);
        else centered_note(app, area, "Loading the profile...", NULL);
        return;
    }
    const akn_profile *p = &s->profile;
    const akn_activity *a = &s->profile_activity;
    int self = strcmp(p->relationship, "self") == 0;
    SDL_Rect clip = { 0, AKN_PROFILE_VIEW_Y, AKN_SCREEN_W, AKN_PROFILE_VIEW_H };
    SDL_RenderSetClipRect(app->renderer, &clip);
    int oy = AKN_PROFILE_VIEW_Y + 8 - app->ui.profile_scroll;
    char text[400];

    /* Banner: the user's picture (absent when their details are restricted), else the theme. */
    akn_rect banner = { AKN_MARGIN_X, oy, 1728, 400 };
    uint32_t from, to;
    akn_theme_gradient(p->theme, &from, &to);
    akn_gfx_gradient(g, banner, 0xFF000000u | from, 0xFF000000u | to, 0);
    if (picture(app, AKN_IMG_MEDIA, p->banner_image, banner, AKN_FIT_COVER) && p->banner_overlay)
        akn_gfx_gradient(g, banner, 0x10000000u, 0xA0000000u, 1);
    akn_gfx_rect(g, (akn_rect){ banner.x, banner.y + banner.h - 4, banner.w, 4 }, 0xFF000000u | p->accent);

    /* Picture overlapping the banner, ringed in the page colour */
    int acx = AKN_MARGIN_X + 60 + 120, acy = oy + banner.h;
    akn_gfx_disc(g, acx, acy, 132, 0xFF0A0F1Cu);
    avatar(app, acx, acy, 120, p->id, p->display_name, p->online_id, p->avatar_image, 1, p->presence.status);

    /* Identity */
    int ix = AKN_MARGIN_X + 340, iy = oy + banner.h + 20, iw = 1728 - 340;
    akn_gfx_text_fit(g, ix, iy, 56, AKN_FONT_BOLD, C_INK, p->display_name, iw);
    iy += 74;
    snprintf(text, sizeof(text), "@%s", p->online_id);
    int hw = akn_gfx_text(g, ix, iy, 26, AKN_FONT_REGULAR, C_MUTED, text);
    const char *rel = self ? "You" : strcmp(p->relationship, "friend") == 0 ? "Friends" : strcmp(p->relationship, "request_sent") == 0 ? "Friend request sent"
                                    : strcmp(p->relationship, "request_received") == 0                                       ? "Wants to be your friend"
                                                                                                                              : "";
    if (rel[0]) pill(app, ix + hw + 18, iy + 1, 0x33000000u | (p->accent & 0xFFFFFF), 0xFF000000u | p->accent, rel);
    iy += 50;
    akn_gfx_disc(g, ix + 9, iy + 16, 9, presence_color(p->presence.status));
    akn_presence_line(&p->presence, unix_now, text, sizeof(text));
    akn_gfx_text_fit(g, ix + 30, iy, 26, AKN_FONT_BOLD, p->presence.game.present ? 0xFFB7C8FFu : C_INK, text, iw - 30);
    iy += 48;
    if (p->badge_count) { /* featured badges as chips, the others as medallions */
        int bx = ix;
        for (int i = 0; i < p->badge_count; i++) {
            if (i < p->featured_badges) {
                int w = badge_chip(app, p->badge_ids[i], bx, iy);
                if (w) bx += w + 12;
            } else if (akn_badge_find(p->badge_ids[i])) {
                badge_medal(app, p->badge_ids[i], bx, iy - 2, 56);
                bx += 66;
            }
        }
        iy += 68;
    }
    if (p->status_message[0]) {
        akn_gfx_text_fit(g, ix, iy, 24, AKN_FONT_REGULAR, C_INK, p->status_message, iw);
        iy += 42;
    }
    size_t o = 0;
    text[0] = '\0';
    if (p->joined_at > 0) {
        char ago[40];
        akn_format_ago(p->joined_at, unix_now, ago, sizeof(ago));
        o += (size_t)snprintf(text + o, sizeof(text) - o, "Joined %s", ago);
    }
    if (!self && p->mutual_friends > 0 && o < sizeof(text))
        o += (size_t)snprintf(text + o, sizeof(text) - o, "%s%d mutual friend%s", o ? "  \xC2\xB7  " : "", p->mutual_friends, p->mutual_friends == 1 ? "" : "s");
    if (p->badges && o < sizeof(text))
        snprintf(text + o, sizeof(text) - o, "%s%d badge%s", o ? "  \xC2\xB7  " : "", p->badges, p->badges == 1 ? "" : "s");
    if (text[0]) {
        akn_gfx_text_fit(g, ix, iy, 22, AKN_FONT_REGULAR, C_SUBTLE, text, iw);
        iy += 40;
    }
    if (s->profile_load == AKN_LOAD_ERROR) pill(app, AKN_SCREEN_W - AKN_MARGIN_X - 170, oy + 20, 0xCCF5A524u, 0xFF1A1205u, "NOT UPDATED");
    int top = iy + 20;
    if (top < oy + banner.h + 150) top = oy + banner.h + 150;

    /* Left column: now playing, recently played, favourite games */
    int lx = AKN_MARGIN_X, lw = CARDS_PER_ROW * CARD_W + (CARDS_PER_ROW - 1) * CARD_GAP, ly = top;
    if (p->restricted) {
        akn_rect n = { lx, ly, lw, 70 };
        akn_gfx_rrect(g, n, 16, 0x33F5A524u);
        akn_gfx_text_fit(g, n.x + 24, n.y + 20, 24, AKN_FONT_REGULAR, C_INK,
                         strcmp(p->relationship, "blocked") == 0 ? "You blocked this player. Unblock them on the AKENO NETWORK website to see their profile."
                                                                  : "This player only shares profile details with friends.",
                         n.w - 48);
        ly += 100;
    }
    if (p->presence.game.present) {
        const akn_game *gm = &p->presence.game;
        ly = section_title(app, lx, ly, "NOW PLAYING");
        akn_rect card = { lx, ly, lw, 210 };
        panel(app, card);
        game_art(app, (akn_rect){ card.x + 18, card.y + 18, 309, 174 }, &gm->art, gm->title_id, gm->name, AKN_ART_WIDE);
        akn_gfx_text_fit(g, card.x + 360, card.y + 40, 34, AKN_FONT_BOLD, C_INK, akn_game_label(gm), card.w - 400);
        char dur[40] = "";
        if (gm->started_at > 0) akn_format_duration(unix_now - gm->started_at, dur, sizeof(dur));
        snprintf(text, sizeof(text), "PS5%s%s  \xC2\xB7  %s", dur[0] ? "  \xC2\xB7  playing for " : "", dur, gm->title_id);
        akn_gfx_text_fit(g, card.x + 360, card.y + 96, 22, AKN_FONT_REGULAR, C_MUTED, text, card.w - 400);
        ly += card.h + 36;
    }
    ly = section_title(app, lx, ly, "RECENTLY PLAYED");
    if (s->profile_activity_load == AKN_LOAD_OK && a->history_visible && a->recent_count) {
        for (int i = 0; i < a->recent_count; i++) {
            const akn_recent_game *r = &a->recent[i];
            char l1[64], l2[64] = "", ago[40], dur[40];
            if (r->playing_now) snprintf(l1, sizeof(l1), "Playing now");
            else {
                akn_format_ago(r->last_played, unix_now, ago, sizeof(ago));
                snprintf(l1, sizeof(l1), "Last played %s", ago);
            }
            if (r->total_seconds >= 0) {
                akn_format_duration(r->total_seconds, dur, sizeof(dur));
                snprintf(l2, sizeof(l2), "%s tracked", dur);
            }
            int col = i % CARDS_PER_ROW, rowi = i / CARDS_PER_ROW;
            game_card(app, lx + col * (CARD_W + CARD_GAP), ly + rowi * (CARD_W * 3 / 2 + 110), CARD_W, &r->art, r->title_id, r->name, l1, l2);
        }
        ly += ((a->recent_count + CARDS_PER_ROW - 1) / CARDS_PER_ROW) * (CARD_W * 3 / 2 + 110) + 10;
    } else {
        const char *msg = s->profile_activity_load == AKN_LOAD_ERROR ? "Game activity could not be loaded."
                          : s->profile_activity_load != AKN_LOAD_OK  ? "Loading..."
                          : !a->history_visible                      ? "This player does not share their game history with you."
                          : self                                     ? "No games tracked yet. Games appear here when this console detects them."
                                                                     : "No games played recently.";
        akn_gfx_text_wrap(g, lx, ly, 24, AKN_FONT_REGULAR, C_MUTED, msg, lw, 34, 2);
        ly += 80;
    }
    if (!p->restricted) {
        ly = section_title(app, lx, ly, "FAVORITE GAMES");
        if (p->favorite_count) {
            for (int i = 0; i < p->favorite_count; i++) {
                int col = i % CARDS_PER_ROW, rowi = i / CARDS_PER_ROW;
                game_card(app, lx + col * (CARD_W + CARD_GAP), ly + rowi * (CARD_W * 3 / 2 + 60), CARD_W, &p->favorite_art[i], "", p->favorites[i],
                          NULL, NULL);
            }
            ly += ((p->favorite_count + CARDS_PER_ROW - 1) / CARDS_PER_ROW) * (CARD_W * 3 / 2 + 60) + 10;
        } else {
            akn_gfx_text(g, lx, ly, 24, AKN_FONT_REGULAR, C_MUTED,
                         self ? "Add favorite games in your profile settings on the AKENO NETWORK website." : "None listed.");
            ly += 60;
        }
    }

    /* Right column: gaming statistics, about, party */
    int rx = lx + lw + 40, rw = AKN_SCREEN_W - AKN_MARGIN_X - rx, ry = top;
    if (s->profile_activity_load == AKN_LOAD_OK && a->history_visible && a->total_sessions) {
        akn_rect st = { rx, ry, rw, 290 };
        panel(app, st);
        int sy = section_title(app, st.x + 28, st.y + 24, "GAMING ACTIVITY");
        char dur[40];
        if (a->total_seconds < 0) snprintf(dur, sizeof(dur), "Hidden");
        else akn_format_duration(a->total_seconds, dur, sizeof(dur));
        const char *labels[4] = { "Tracked playtime", "Games played", "Sessions", "Tracked since" };
        char vals[4][48];
        snprintf(vals[0], sizeof(vals[0]), "%s", dur);
        snprintf(vals[1], sizeof(vals[1]), "%d", a->total_games);
        snprintf(vals[2], sizeof(vals[2]), "%d", a->total_sessions);
        if (a->tracked_since > 0) akn_format_ago(a->tracked_since, unix_now, vals[3], sizeof(vals[3]));
        else snprintf(vals[3], sizeof(vals[3]), "-");
        for (int i = 0; i < 4; i++) {
            akn_gfx_text(g, st.x + 28, sy + i * 52, 22, AKN_FONT_REGULAR, C_MUTED, labels[i]);
            akn_gfx_text_fit(g, st.x + 270, sy + i * 52, 24, AKN_FONT_BOLD, C_INK, vals[i], st.w - 300);
        }
        ry += st.h + 24;
    }
    if (!p->restricted) {
        int lines_h = 0;
        akn_rect ab = { rx, ry, rw, 0 };
        /* measure by drawing into a clipped-away area is not possible: size the panel for up to 8 lines */
        const char *bio = p->bio[0] ? p->bio : self ? "You have not written a bio yet. Add one on the AKENO NETWORK website." : "No bio yet.";
        lines_h = 8 * 34;
        ab.h = 90 + lines_h;
        panel(app, ab);
        int by = section_title(app, ab.x + 28, ab.y + 24, "ABOUT");
        int n = akn_gfx_text_wrap(g, ab.x + 28, by, 24, AKN_FONT_REGULAR, p->bio[0] ? C_INK : C_MUTED, bio, ab.w - 56, 34, 8);
        ab.h = 90 + n * 34;
        ry += ab.h + 24;
    }
    if (p->has_party) {
        akn_rect pa = { rx, ry, rw, 150 };
        panel(app, pa);
        int py = section_title(app, pa.x + 28, pa.y + 24, "PARTY");
        akn_gfx_text_fit(g, pa.x + 28, py, 26, AKN_FONT_BOLD, C_INK, p->party_name[0] ? p->party_name : "Party", pa.w - 56);
        snprintf(text, sizeof(text), "%d of %d members", p->party_members, p->party_max);
        akn_gfx_text(g, pa.x + 28, py + 40, 22, AKN_FONT_REGULAR, C_MUTED, text);
        ry += pa.h + 24;
    }
    if (self) {
        akn_gfx_text_wrap(g, rx, ry, 20, AKN_FONT_REGULAR, C_SUBTLE,
                          "Edit your picture, banner, theme, badges and bio on the AKENO NETWORK website.", rw, 28, 3);
        ry += 90;
    }
    SDL_RenderSetClipRect(app->renderer, NULL);

    int bottom = (ly > ry ? ly : ry) - oy + 40;
    akn_ui_set_profile_height(&app->ui, bottom);
    if (bottom > AKN_PROFILE_VIEW_H) { /* scroll position */
        int track_h = AKN_PROFILE_VIEW_H - 40;
        int knob_h = track_h * AKN_PROFILE_VIEW_H / bottom;
        int knob_y = AKN_PROFILE_VIEW_Y + 20 + (track_h - knob_h) * app->ui.profile_scroll / (bottom - AKN_PROFILE_VIEW_H);
        akn_gfx_rrect(g, (akn_rect){ AKN_SCREEN_W - 40, AKN_PROFILE_VIEW_Y + 20, 6, track_h }, 3, C_LINE);
        akn_gfx_rrect(g, (akn_rect){ AKN_SCREEN_W - 40, knob_y, 6, knob_h }, 3, C_MUTED);
    }
}

static void draw_activity(akn_app *app, const akn_social *s, uint64_t now) {
    akn_gfx *g = app->gfx;
    long long unix_now = akn_platform_unix_time();
    screen_title(app, "Activity", "This console, your games and your friends playing now. The AKENO daemon detects the games.");
    if (s->ipc != AKN_IPC_READY && !s->daemon.ipc) {
        ipc_status_panel(app, s, (akn_rect){ AKN_MARGIN_X, 290, 1728, 660 });
        return;
    }
    char text[300];
    /* This console */
    akn_rect c = { AKN_MARGIN_X, 290, 840, 230 };
    panel(app, c);
    int x = c.x + 36, w = c.w - 72;
    akn_gfx_text(g, x, c.y + 28, 20, AKN_FONT_BOLD, C_SUBTLE, "THIS CONSOLE");
    const akn_daemon_info *d = &s->daemon;
    if (d->console_game.present) {
        /* The daemon knows the title ID; the verified artwork comes from your own activity answer. */
        akn_art art = d->console_game.art;
        if (!art.icon[0] && !art.cover[0] && s->own_activity_load == AKN_LOAD_OK) akn_art_for_title(&s->own_activity, d->console_game.title_id, &art);
        game_art(app, (akn_rect){ x, c.y + 66, 140, 140 }, &art, d->console_game.title_id, d->console_game.name, AKN_ART_ICON);
        x += 166;
        w -= 166;
        snprintf(text, sizeof(text), "Playing %s", akn_game_label(&d->console_game));
        akn_gfx_text_fit(g, x, c.y + 70, 34, AKN_FONT_BOLD, 0xFFB7C8FFu, text, w);
        char dur[40];
        akn_format_duration(d->console_game_seconds, dur, sizeof(dur));
        snprintf(text, sizeof(text), "%s  \xC2\xB7  for %s", d->console_game.title_id, dur);
        akn_gfx_text_fit(g, x, c.y + 124, 24, AKN_FONT_REGULAR, C_MUTED, text, w);
    } else {
        const char *st = d->activity_state;
        const char *head = !d->activity_enabled || strcmp(st, "off") == 0 ? "Game detection is off"
                           : strcmp(st, "permission-missing") == 0  ? "This console may not share games"
                           : strcmp(st, "excluded") == 0             ? "This game is not shared (excluded)"
                                                                     : "No game running";
        akn_gfx_text_fit(g, x, c.y + 70, 34, AKN_FONT_BOLD, C_INK, head, w);
        akn_gfx_text_wrap(g, x, c.y + 124, 22, AKN_FONT_REGULAR, C_MUTED,
                          "Starting AKENO Native from the launcher closes a running game. Game detection is set on the AKENO page.",
                          w, 32, 2);
    }

    /* Friends playing now */
    akn_gfx_text(g, AKN_MARGIN_X, 552, 20, AKN_FONT_BOLD, C_SUBTLE, "FRIENDS PLAYING NOW");
    int count = akn_social_playing_count(s), focus = app->ui.focus[AKN_SCREEN_ACTIVITY];
    akn_rect focus_rect = { 0, 0, 0, 0 };
    if (!count) {
        akn_gfx_text(g, AKN_MARGIN_X, 600, 24, AKN_FONT_REGULAR, C_MUTED,
                     s->friends ? "None of your friends is playing right now." : "Loading your friends...");
    }
    for (int row = app->ui.list_top[AKN_SCREEN_ACTIVITY]; row < count && row < app->ui.list_top[AKN_SCREEN_ACTIVITY] + AKN_ACTIVITY_ROWS_VISIBLE;
         row++) {
        int i = akn_social_playing_index(s, row);
        if (i < 0 || !s->friends) break;
        akn_rect r = akn_ui_row_rect(&app->ui, AKN_SCREEN_ACTIVITY, row);
        friend_row(app, r, &s->friends->items[i], row == focus && !app->ui.dialog, unix_now);
        if (row == focus) focus_rect = r;
    }
    scroll_info(app, AKN_SCREEN_ACTIVITY, AKN_MARGIN_X + 840, 552, AKN_ACTIVITY_ROWS_VISIBLE);

    /* Your games */
    akn_rect y = { 976, 290, 848, 660 };
    panel(app, y);
    int gx = y.x + 36, gw = y.w - 72, gy = y.y + 28;
    akn_gfx_text(g, gx, gy, 20, AKN_FONT_BOLD, C_SUBTLE, "YOUR RECENT GAMES");
    gy += 42;
    const akn_activity *a = &s->own_activity;
    if (s->own_activity_load == AKN_LOAD_OK) {
        char dur[40];
        if (a->total_seconds >= 0) {
            akn_format_duration(a->total_seconds, dur, sizeof(dur));
            snprintf(text, sizeof(text), "%d game%s, %d session%s, %s tracked", a->total_games, a->total_games == 1 ? "" : "s",
                     a->total_sessions, a->total_sessions == 1 ? "" : "s", dur);
        } else {
            snprintf(text, sizeof(text), "%d game%s, %d session%s", a->total_games, a->total_games == 1 ? "" : "s", a->total_sessions,
                     a->total_sessions == 1 ? "" : "s");
        }
        akn_gfx_text_fit(g, gx, gy, 22, AKN_FONT_REGULAR, C_MUTED, text, gw);
        gy += 50;
        if (!a->recent_count) akn_gfx_text(g, gx, gy, 24, AKN_FONT_REGULAR, C_SUBTLE, "No games tracked yet.");
        for (int i = 0; i < a->recent_count && i < 6; i++) {
            const akn_recent_game *r = &a->recent[i];
            game_art(app, (akn_rect){ gx, gy - 4, 56, 84 }, &r->art, r->title_id, r->name, AKN_ART_COVER);
            int tx = gx + 76, tw = gw - 76;
            akn_gfx_text_fit(g, tx, gy + 8, 24, AKN_FONT_BOLD, C_INK, r->name[0] ? r->name : r->title_id, tw);
            char ago[40];
            akn_format_ago(r->last_played, unix_now, ago, sizeof(ago));
            if (r->playing_now) snprintf(text, sizeof(text), "playing now");
            else if (r->total_seconds >= 0) {
                akn_format_duration(r->total_seconds, dur, sizeof(dur));
                snprintf(text, sizeof(text), "%s  \xC2\xB7  %s", ago, dur);
            } else snprintf(text, sizeof(text), "%s", ago);
            akn_gfx_text_fit(g, tx, gy + 42, 20, AKN_FONT_REGULAR, C_MUTED, text, tw);
            gy += 96;
        }
    } else if (s->own_activity_load == AKN_LOAD_ERROR) {
        akn_gfx_text(g, gx, gy, 24, AKN_FONT_REGULAR, C_MUTED, "Your game history could not be loaded.");
    } else {
        akn_gfx_text(g, gx, gy, 24, AKN_FONT_REGULAR, C_MUTED, "Loading...");
    }
    if (focus_rect.w && !app->ui.dialog) focus_ring(app, focus_rect, 18, now);
}

/* ---- Screens ------------------------------------------------------------------------------------ */

static const char *audio_summary(akn_app *app, uint32_t *color) {
    if (app->audio_count == 0) {
        *color = C_MUTED;
        return "Not tested yet";
    }
    const akn_audio_result *r = &app->audio_history[(app->audio_count < AKN_AUDIO_HISTORY ? app->audio_count : AKN_AUDIO_HISTORY) - 1];
    if (r->heard == AKN_HEARD_YES) {
        *color = C_SUCCESS;
        return "Audible: confirmed by you";
    }
    if (r->heard == AKN_HEARD_NO) {
        *color = C_DANGER;
        return "Accepted but not heard";
    }
    *color = r->outcome == AKN_AUDIO_ACCEPTED_PACED ? C_WARNING : C_DANGER;
    return akn_audio_outcome_name(r->outcome);
}

static void draw_home(akn_app *app, const akn_account_view *acc, uint64_t now) {
    akn_gfx *g = app->gfx;
    if (acc->state == AKN_ACC_SIGNED_IN) {
        /* your own picture, once your profile has loaded through the daemon */
        char image[33] = "", user_id[40] = "";
        SDL_LockMutex(app->lock);
        if (app->social.me_load == AKN_LOAD_OK) {
            snprintf(image, sizeof(image), "%s", app->social.me.avatar_image);
            snprintf(user_id, sizeof(user_id), "%s", app->social.me.id);
        }
        SDL_UnlockMutex(app->lock);
        avatar(app, AKN_MARGIN_X + 74, 248, 74, user_id, acc->display_name, acc->online_id, image, 0, AKN_PRES_ONLINE);
        int tx = AKN_MARGIN_X + 180;
        akn_gfx_text(g, tx, 164, 28, AKN_FONT_REGULAR, C_MUTED, "Welcome back,");
        akn_gfx_text_fit(g, tx, 204, 72, AKN_FONT_BOLD, C_INK, acc->display_name[0] ? acc->display_name : acc->online_id, 900);
        char line[160];
        snprintf(line, sizeof(line), "@%s  \xC2\xB7  signed in through this console's AKENO pairing", acc->online_id);
        akn_gfx_text_fit(g, tx, 300, 26, AKN_FONT_REGULAR, C_MUTED, line, 900);
    } else {
        akn_gfx_text(g, AKN_MARGIN_X, 164, 28, AKN_FONT_REGULAR, C_MUTED, "AKENO NETWORK for PS5");
        akn_gfx_text(g, AKN_MARGIN_X, 204, 72, AKN_FONT_BOLD, C_INK, "Native");
        akn_gfx_text_fit(g, AKN_MARGIN_X, 300, 26, AKN_FONT_REGULAR, C_MUTED, akn_account_state_text(acc->state), 1080);
    }

    /* Platform check summary */
    akn_rect card = { 1236, 150, 588, 232 };
    panel(app, card);
    akn_gfx_text(g, card.x + 32, card.y + 24, 20, AKN_FONT_BOLD, C_SUBTLE, "PLATFORM CHECKS ON THIS CONSOLE");
    char text[120];
    snprintf(text, sizeof(text), "Screen: native %dx%d", app->output_w, app->output_h);
    akn_gfx_disc(g, card.x + 40, card.y + 85, 7, C_SUCCESS);
    akn_gfx_text_fit(g, card.x + 60, card.y + 70, 24, AKN_FONT_REGULAR, C_INK, text, card.w - 90);
    uint32_t pc = app->pad && !app->pad_disconnected ? (app->pad_events ? C_SUCCESS : C_WARNING) : (app->pad_error[0] ? C_DANGER : C_OFFLINE);
    const char *pt = app->pad ? (app->pad_events ? "Controller: input received" : "Controller: opened, no input yet")
                              : (app->pad_error[0] ? "Controller: could not be opened" : "Controller: none yet");
    akn_gfx_disc(g, card.x + 40, card.y + 135, 7, pc);
    akn_gfx_text_fit(g, card.x + 60, card.y + 120, 24, AKN_FONT_REGULAR, C_INK, pt, card.w - 90);
    uint32_t ac;
    const char *at = audio_summary(app, &ac);
    snprintf(text, sizeof(text), "Audio: %s", at);
    akn_gfx_disc(g, card.x + 40, card.y + 185, 7, ac);
    akn_gfx_text_fit(g, card.x + 60, card.y + 170, 24, AKN_FONT_REGULAR, C_INK, text, card.w - 90);

    SDL_LockMutex(app->lock);
    const akn_social *s = &app->social;
    akn_gfx_text(g, AKN_MARGIN_X, 396, 20, AKN_FONT_BOLD, C_SUBTLE, "AKENO SOCIAL");
    akn_gfx_text(g, AKN_MARGIN_X + 186, 396, 20, AKN_FONT_REGULAR, s->ipc == AKN_IPC_READY ? C_SUBTLE : C_WARNING,
                 s->ipc == AKN_IPC_READY ? "Live from the AKENO daemon on this console" : akn_ipc_state_text(s->ipc));
    char badge[48] = "", playing[16] = "";
    if (s->friends) snprintf(badge, sizeof(badge), "%d ONLINE", s->friends->online);
    if (s->daemon.console_game.present) snprintf(playing, sizeof(playing), "PLAYING");
    SDL_UnlockMutex(app->lock);
    akn_gfx_text(g, AKN_MARGIN_X, 696, 20, AKN_FONT_BOLD, C_SUBTLE, "NATIVE PLATFORM CHECKS");
    akn_gfx_text(g, AKN_MARGIN_X + 296, 696, 20, AKN_FONT_REGULAR, C_SUBTLE, "Controller, sound and connection on this console");

    akn_item items[AKN_MAX_ITEMS];
    int n = akn_ui_screen_items(&app->ui, AKN_SCREEN_HOME, items, AKN_MAX_ITEMS);
    draw_items(app, items, n, app->ui.dialog ? -1 : app->ui.focus[AKN_SCREEN_HOME], now);
    for (int i = 0; i < n; i++) {
        const char *label = items[i].id == AKN_ITEM_FRIENDS ? badge : items[i].id == AKN_ITEM_ACTIVITY ? playing : "";
        if (!label[0]) continue;
        int w = akn_gfx_text_width(g, 16, AKN_FONT_BOLD, label, strlen(label)) + 28;
        pill(app, items[i].rect.x + items[i].rect.w - w - 22, items[i].rect.y + 22, 0x2622C55Eu, 0xFF86EFACu, label);
    }
}

static void button_chip(akn_app *app, akn_rect r, const char *label, int down, int sym) {
    akn_gfx *g = app->gfx;
    akn_gfx_rrect(g, r, r.h / 2, down ? C_ACCENT : C_SURFACE3);
    if (!down) akn_gfx_rrect_outline(g, r, r.h / 2, 1, C_LINE_STRONG);
    if (sym >= 0) symbol(app, (symbol_t)sym, r.x + r.w / 2, r.y + r.h / 2, r.h - 22, down ? 0xFFFFFFFFu : C_INK);
    else akn_gfx_text_center(g, r.x, r.w, r.y + (r.h - 26) / 2, 22, AKN_FONT_BOLD, down ? 0xFFFFFFFFu : C_INK, label);
}

static void stick(akn_app *app, int cx, int cy, int ax, int ay, int pressed, const char *label) {
    akn_gfx *g = app->gfx;
    akn_gfx_disc(g, cx, cy, 70, C_SURFACE3);
    akn_gfx_ring(g, cx, cy, 70, 2, C_LINE_STRONG);
    akn_gfx_rect(g, (akn_rect){ cx - 70, cy, 140, 1 }, C_LINE);
    akn_gfx_rect(g, (akn_rect){ cx, cy - 70, 1, 140 }, C_LINE);
    int dx = (int)(ax / 32768.0 * 52), dy = (int)(ay / 32768.0 * 52);
    akn_gfx_disc(g, cx + dx, cy + dy, 20, pressed ? C_ACCENT : C_INK);
    akn_gfx_text_center(g, cx - 70, 140, cy + 82, 20, AKN_FONT_REGULAR, C_MUTED, label);
}

static void trigger(akn_app *app, int x, int y, int value, const char *label) {
    akn_gfx *g = app->gfx;
    akn_rect r = { x, y, 200, 14 };
    akn_gfx_rrect(g, r, 7, C_SURFACE3);
    int w = value <= 0 ? 0 : (int)(value / 32767.0 * 200);
    if (w > 14) akn_gfx_rrect(g, (akn_rect){ x, y, w, 14 }, 7, C_ACCENT);
    akn_gfx_text(g, x, y + 22, 20, AKN_FONT_REGULAR, C_MUTED, label);
}

static void draw_controller(akn_app *app, uint64_t now) {
    akn_gfx *g = app->gfx;
    const akn_input *in = &app->input;
    screen_title(app, "Controller test", "Press every button and move both sticks. Everything here is read from the DualSense in real time.");
    akn_rect pad = { AKN_MARGIN_X, 290, 1100, 650 };
    panel(app, pad);
    int ox = pad.x, oy = pad.y;
#define DOWN(b) akn_input_is_down(in, b)
    button_chip(app, (akn_rect){ ox + 60, oy + 40, 140, 52 }, "L2", DOWN(AKN_BTN_L2), -1);
    button_chip(app, (akn_rect){ ox + 60, oy + 104, 140, 52 }, "L1", DOWN(AKN_BTN_L1), -1);
    button_chip(app, (akn_rect){ ox + 900, oy + 40, 140, 52 }, "R2", DOWN(AKN_BTN_R2), -1);
    button_chip(app, (akn_rect){ ox + 900, oy + 104, 140, 52 }, "R1", DOWN(AKN_BTN_R1), -1);
    button_chip(app, (akn_rect){ ox + 380, oy + 60, 340, 80 }, "Touchpad", DOWN(AKN_BTN_TOUCHPAD), -1);
    button_chip(app, (akn_rect){ ox + 740, oy + 74, 136, 52 }, "Options", DOWN(AKN_BTN_OPTIONS), -1);
    /* D-pad */
    int dx = ox + 200, dy = oy + 300;
    button_chip(app, (akn_rect){ dx - 32, dy - 100, 64, 64 }, "\xE2\x86\x91", DOWN(AKN_BTN_UP), -1);
    button_chip(app, (akn_rect){ dx - 32, dy + 36, 64, 64 }, "\xE2\x86\x93", DOWN(AKN_BTN_DOWN), -1);
    button_chip(app, (akn_rect){ dx - 100, dy - 32, 64, 64 }, "\xE2\x86\x90", DOWN(AKN_BTN_LEFT), -1);
    button_chip(app, (akn_rect){ dx + 36, dy - 32, 64, 64 }, "\xE2\x86\x92", DOWN(AKN_BTN_RIGHT), -1);
    /* Face buttons */
    int fx = ox + 900, fy = oy + 300;
    button_chip(app, (akn_rect){ fx - 34, fy - 104, 68, 68 }, "", DOWN(AKN_BTN_TRIANGLE), SYM_TRIANGLE);
    button_chip(app, (akn_rect){ fx - 34, fy + 36, 68, 68 }, "", DOWN(AKN_BTN_CROSS), SYM_CROSS);
    button_chip(app, (akn_rect){ fx - 104, fy - 34, 68, 68 }, "", DOWN(AKN_BTN_SQUARE), SYM_SQUARE);
    button_chip(app, (akn_rect){ fx + 36, fy - 34, 68, 68 }, "", DOWN(AKN_BTN_CIRCLE), SYM_CIRCLE);
    stick(app, ox + 400, oy + 470, in->axes[AKN_AXIS_LX], in->axes[AKN_AXIS_LY], DOWN(AKN_BTN_L3), "Left stick / L3");
    stick(app, ox + 700, oy + 470, in->axes[AKN_AXIS_RX], in->axes[AKN_AXIS_RY], DOWN(AKN_BTN_R3), "Right stick / R3");
    trigger(app, ox + 60, oy + 590, in->axes[AKN_AXIS_L2], "L2 analog");
    trigger(app, ox + 840, oy + 590, in->axes[AKN_AXIS_R2], "R2 analog");
#undef DOWN

    akn_rect info = { 1236, 290, 588, 650 };
    panel(app, info);
    int x = info.x + 32, y = info.y + 28;
    akn_gfx_text(g, x, y, 20, AKN_FONT_BOLD, C_SUBTLE, "CONTROLLER");
    y += 40;
    akn_gfx_text_fit(g, x, y, 28, AKN_FONT_BOLD, C_INK, app->pad ? app->pad_name : "No controller opened", info.w - 64);
    y += 46;
    char text[200];
    if (app->pad_error[0]) {
        akn_gfx_text_wrap(g, x, y, 22, AKN_FONT_REGULAR, C_DANGER, app->pad_error, info.w - 64, 30, 2);
        y += 64;
    }
    snprintf(text, sizeof(text), "SDL events received: %lu", app->pad_events);
    akn_gfx_text(g, x, y, 22, AKN_FONT_REGULAR, C_MUTED, text);
    y += 34;
    if (app->pad_status.available) {
        snprintf(text, sizeof(text), "System state: %s (rc 0x%08x)", app->pad_status.has_handle ? (app->pad_status.connected ? "connected" : "not connected") : "no handle",
                 (unsigned)app->pad_status.rc);
        akn_gfx_text_fit(g, x, y, 22, AKN_FONT_REGULAR, C_MUTED, text, info.w - 64);
        y += 34;
    }
    y += 16;
    akn_gfx_text(g, x, y, 20, AKN_FONT_BOLD, C_SUBTLE, "PRESSES");
    y += 36;
    for (int i = 0; i < AKN_BTN_COUNT; i++) {
        int col = i % 2, line = i / 2;
        snprintf(text, sizeof(text), "%s  %lu", akn_button_name((akn_button)i), in->presses[i]);
        akn_gfx_text(g, x + col * 270, y + line * 30, 21, AKN_FONT_REGULAR, in->presses[i] ? C_INK : C_SUBTLE, text);
    }
    y += 8 * 30 + 14;
    double p = akn_ui_back_hold_progress(&app->ui, now);
    akn_rect bar = { x, y, info.w - 64, 10 };
    akn_gfx_rrect(g, bar, 5, C_SURFACE3);
    if (p > 0.03) akn_gfx_rrect(g, (akn_rect){ bar.x, bar.y, (int)(bar.w * p), bar.h }, 5, C_ACCENT);
    akn_gfx_text(g, x, y + 18, 20, AKN_FONT_REGULAR, C_MUTED, "Hold Circle for 1 second to return");
}

static void draw_audio(akn_app *app, uint64_t now) {
    akn_gfx *g = app->gfx;
    screen_title(app, "Audio test", "Milestone V1: can this foreground app play a sound you can hear?");
    akn_item items[AKN_MAX_ITEMS];
    int n = akn_ui_screen_items(&app->ui, AKN_SCREEN_AUDIO, items, AKN_MAX_ITEMS);
    draw_items(app, items, n, app->ui.dialog || app->ui.audio_busy ? -1 : app->ui.focus[AKN_SCREEN_AUDIO], now);
    akn_gfx_text_wrap(g, AKN_MARGIN_X, 712, 22, AKN_FONT_REGULAR, C_MUTED,
                      "Plays a 440 Hz tone for 1.5 seconds through the console's main output (TV/HDMI, or the headset if the "
                      "console sends all audio there). Nothing plays until you select Play. Turn the TV to a normal volume first. "
                      "Audible output counts as confirmed only when you say you heard it.",
                      780, 32, 7);

    akn_rect res = { 940, 300, 884, 640 };
    panel(app, res);
    int x = res.x + 36, y = res.y + 30;
    akn_gfx_text(g, x, y, 20, AKN_FONT_BOLD, C_SUBTLE, "RESULT");
    y += 42;
    if (app->ui.audio_busy) {
        akn_gfx_text(g, x, y, 34, AKN_FONT_BOLD, C_INK, "Playing...");
        double t = fmod((double)now / 1000.0, 1.2) / 1.2;
        akn_rect bar = { x, y + 60, res.w - 72, 10 };
        akn_gfx_rrect(g, bar, 5, C_SURFACE3);
        akn_gfx_rrect(g, (akn_rect){ bar.x + (int)((bar.w - 160) * t), bar.y, 160, 10 }, 5, C_ACCENT);
        return;
    }
    if (app->audio_count == 0) {
        akn_gfx_text(g, x, y, 30, AKN_FONT_BOLD, C_MUTED, "No test yet");
        akn_gfx_text_wrap(g, x, y + 56, 22, AKN_FONT_REGULAR, C_MUTED,
                          "The result shows every system answer: initialisation, the output port, each output call and its "
                          "timing, and whether you heard the tone.",
                          res.w - 72, 32, 4);
        return;
    }
    const akn_audio_result *r = &app->audio_history[(app->audio_count < AKN_AUDIO_HISTORY ? app->audio_count : AKN_AUDIO_HISTORY) - 1];
    uint32_t c;
    const char *summary = audio_summary(app, &c);
    akn_gfx_text_fit(g, x, y, 32, AKN_FONT_BOLD, c, summary, res.w - 72);
    y += 58;
    char text[1600];
    akn_audio_format(r, text, sizeof(text));
    char *line = text;
    int lines = 0;
    while (*line && lines < 12) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *colon = lines ? strchr(line, ':') : NULL;
        if (colon) { /* "  Label:   value" -> two columns */
            *colon = '\0';
            const char *label = line, *value = colon + 1;
            while (*label == ' ') label++;
            while (*value == ' ') value++;
            akn_gfx_text(g, x, y, 21, AKN_FONT_REGULAR, C_SUBTLE, label);
            akn_gfx_text_fit(g, x + 150, y, 21, AKN_FONT_REGULAR, C_INK, value, res.w - 72 - 150);
        } else {
            akn_gfx_text_fit(g, x, y, 21, AKN_FONT_BOLD, C_INK, line, res.w - 72);
        }
        y += 34;
        lines++;
        if (!end) break;
        line = end + 1;
    }
    char count[80];
    snprintf(count, sizeof(count), "%d of %d tests used this launch", app->audio_count, AKN_AUDIO_MAX_TESTS);
    akn_gfx_text(g, x, res.y + res.h - 50, 20, AKN_FONT_REGULAR, C_SUBTLE, count);
}

static void draw_connection(akn_app *app, const akn_account_view *acc, int running, uint64_t now) {
    akn_gfx *g = app->gfx;
    screen_title(app, "Connection", "The local AKENO daemon, your server and the account this console is paired with.");
    akn_rect p = { AKN_MARGIN_X, 290, 1728, 540 };
    panel(app, p);
    int x = p.x + 40, y = p.y + 34, w = p.w - 80;
    char text[480];
    snprintf(text, sizeof(text), "%s%s%s", akn_daemon_state_text(acc->daemon), acc->daemon_version[0] ? "  \xC2\xB7  version " : "",
             acc->daemon_version);
    y = row(app, x, y, w, "AKENO daemon", acc->daemon == AKN_DAEMON_RUNNING ? C_SUCCESS : C_WARNING, text);
    SDL_LockMutex(app->lock);
    snprintf(text, sizeof(text), "%s%s%s", akn_ipc_state_text(app->social.ipc), app->social.ipc_detail[0] ? "  \xC2\xB7  " : "",
             app->social.ipc_detail);
    int ipc_ready = app->social.ipc == AKN_IPC_READY;
    SDL_UnlockMutex(app->lock);
    y = row(app, x, y, w, "Native interface", ipc_ready ? C_SUCCESS : C_WARNING, text);
    y = row(app, x, y, w, "Server", acc->server_desc && strcmp(acc->server_desc, "https (host hidden)") == 0 ? C_SUCCESS : C_WARNING,
            acc->server_desc ? acc->server_desc : "not checked");
    snprintf(text, sizeof(text), "%s%s", acc->credential_present ? "device credential present" : "no device credential",
             acc->credential_present ? (acc->credential_private ? " (private)" : " (readable by others!)") : "");
    y = row(app, x, y, w, "Pairing", acc->credential_present ? C_SUCCESS : C_WARNING, text);
    if (acc->state == AKN_ACC_SIGNED_IN) snprintf(text, sizeof(text), "Signed in as %s (@%s)", acc->display_name, acc->online_id);
    else snprintf(text, sizeof(text), "%s", running ? "Checking..." : akn_account_state_text(acc->state));
    y = row(app, x, y, w, "Account", acc->state == AKN_ACC_SIGNED_IN ? C_SUCCESS : running ? C_OFFLINE : C_DANGER, text);
    if (acc->state == AKN_ACC_SIGNED_IN) {
        y = row(app, x, y, w, "Device name", 0, acc->device_name);
        snprintf(text, sizeof(text), "%d granted: %s", acc->scope_count, acc->scopes);
        y = row(app, x, y, w, "Permissions", 0, text);
        if (acc->have_skew) {
            snprintf(text, sizeof(text), "%+lld s (server time minus console time)", acc->clock_skew_s);
            y = row(app, x, y, w, "Clock offset", 0, text);
        }
    } else if (acc->state == AKN_ACC_NOT_PAIRED || acc->state == AKN_ACC_NO_SERVER || acc->state == AKN_ACC_UNAUTHORIZED) {
        y = row(app, x, y, w, "What to do", 0, "Pair this console with the AKENO daemon (browser page) as before; AKENO Native then uses it.");
    }
    snprintf(text, sizeof(text), "check %lu  \xC2\xB7  library %s  \xC2\xB7  HTTP %d %s  \xC2\xB7  %u ms", acc->checks,
             acc->status_name[0] ? acc->status_name : "-", acc->http_status, acc->error_code, acc->duration_ms);
    row(app, x, y, w, "Last check", 0, text);
    akn_gfx_text_wrap(g, 680, 866, 21, AKN_FONT_REGULAR, C_SUBTLE,
                      "AKENO Native reads the existing pairing without changing it and opens no second realtime connection: "
                      "the AKENO daemon keeps your notifications and presence.",
                      1140, 30, 3);
    akn_item items[AKN_MAX_ITEMS];
    int n = akn_ui_screen_items(&app->ui, AKN_SCREEN_CONNECTION, items, AKN_MAX_ITEMS);
    draw_items(app, items, n, app->ui.dialog ? -1 : app->ui.focus[AKN_SCREEN_CONNECTION], now);
}

static void draw_system(akn_app *app, uint64_t now) {
    akn_gfx *g = app->gfx;
    screen_title(app, "System", "What this build is and how it runs on this console.");
    akn_rect p = { AKN_MARGIN_X, 290, 1000, 540 };
    panel(app, p);
    int x = p.x + 40, y = p.y + 30, w = p.w - 80;
    char text[300];
    snprintf(text, sizeof(text), "%s (build %s)", AKN_NATIVE_VERSION, AKN_BUILD_ID);
    y = row(app, x, y, w, "Version", 0, text);
    snprintf(text, sizeof(text), "%s, firmware %s", app->platform.name, app->platform.firmware);
    y = row(app, x, y, w, "Console", 0, text);
    y = row(app, x, y, w, "Process", app->context == AKN_CTX_FOREGROUND || app->context == AKN_CTX_DESKTOP ? C_SUCCESS : C_WARNING,
            akn_context_name(app->context));
    snprintf(text, sizeof(text), "%s, %s renderer, %dx%d", app->video_driver, app->renderer_name, app->output_w, app->output_h);
    y = row(app, x, y, w, "Video", 0, text);
    snprintf(text, sizeof(text), "%.1f ms average, %.1f ms slowest, %lu frames", app->frame_ms_avg, app->frame_ms_max, app->frames);
    y = row(app, x, y, w, "Draw time", 0, text);
    snprintf(text, sizeof(text), "%.0f s", (now - app->started_ms) / 1000.0);
    y = row(app, x, y, w, "Running for", 0, text);
    y = row(app, x, y, w, "Log", 0, app->log_path);
    y = row(app, x, y, w, "Report", 0, app->report_path);
    if (app->last_saved[0]) row(app, x, y, w, "Last saved", C_SUCCESS, app->last_saved);

    akn_rect lp = { 1136, 290, 688, 540 };
    panel(app, lp);
    akn_gfx_text(g, lp.x + 30, lp.y + 26, 20, AKN_FONT_BOLD, C_SUBTLE, "RECENT LOG (REDACTED)");
    char lines[AKN_LOG_RING][AKN_LOG_LINE];
    int n = akn_log_recent(&app->log, lines, AKN_LOG_RING);
    int start = n > 11 ? n - 11 : 0;
    for (int i = start; i < n; i++)
        akn_gfx_text_fit(g, lp.x + 30, lp.y + 66 + (i - start) * 40, 18, AKN_FONT_REGULAR, C_MUTED, lines[i], lp.w - 60);
    akn_item items[AKN_MAX_ITEMS];
    int ni = akn_ui_screen_items(&app->ui, AKN_SCREEN_SYSTEM, items, AKN_MAX_ITEMS);
    draw_items(app, items, ni, app->ui.dialog ? -1 : app->ui.focus[AKN_SCREEN_SYSTEM], now);
}

static void draw_dialog(akn_app *app, uint64_t now) {
    akn_gfx *g = app->gfx;
    akn_gfx_rect(g, (akn_rect){ 0, 0, AKN_SCREEN_W, AKN_SCREEN_H }, 0xB3040710u);
    akn_rect p = app->ui.dialog == AKN_DIALOG_EXIT ? (akn_rect){ 560, 340, 800, 380 } : (akn_rect){ 460, 330, 1000, 380 };
    akn_gfx_rrect(g, p, 28, 0xFA121A2Fu);
    akn_gfx_rrect_outline(g, p, 28, 1, C_LINE_STRONG);
    akn_gfx_gradient(g, (akn_rect){ p.x + 28, p.y, p.w - 56, 3 }, C_ACCENT, C_VIOLET, 0);
    if (app->ui.dialog == AKN_DIALOG_EXIT) {
        akn_gfx_text_center(g, p.x, p.w, p.y + 60, 44, AKN_FONT_BOLD, C_INK, "Exit AKENO Native?");
        akn_gfx_text_center(g, p.x, p.w, p.y + 130, 24, AKN_FONT_REGULAR, C_MUTED, "You return to the PS5. The AKENO daemon keeps running.");
        akn_gfx_text_center(g, p.x, p.w, p.y + 166, 24, AKN_FONT_REGULAR, C_MUTED, "A diagnostic report is saved on the way out.");
    } else {
        akn_gfx_text_center(g, p.x, p.w, p.y + 56, 44, AKN_FONT_BOLD, C_INK, "Did you hear the tone?");
        akn_gfx_text_center(g, p.x, p.w, p.y + 126, 24, AKN_FONT_REGULAR, C_MUTED, "A 440 Hz beep for 1.5 seconds, on the TV or your headset.");
        akn_gfx_text_center(g, p.x, p.w, p.y + 162, 24, AKN_FONT_REGULAR, C_MUTED, "Your answer is what decides whether audio works.");
    }
    akn_item items[AKN_MAX_ITEMS];
    int n = akn_ui_dialog_items(&app->ui, items, AKN_MAX_ITEMS);
    draw_items(app, items, n, app->ui.dialog_focus, now);
}

void akn_screens_draw(akn_app *app, uint64_t now) {
    SDL_SetRenderDrawColor(app->renderer, 6, 9, 18, 255);
    SDL_RenderClear(app->renderer);
    akn_gfx_background(app->gfx);
    akn_account_view acc;
    SDL_LockMutex(app->lock);
    acc = app->account;
    int running = app->net_running;
    SDL_UnlockMutex(app->lock);
    top_bar(app, &acc);
    switch (app->ui.screen) {
    case AKN_SCREEN_HOME: draw_home(app, &acc, now); break;
    case AKN_SCREEN_CONTROLLER: draw_controller(app, now); break;
    case AKN_SCREEN_AUDIO: draw_audio(app, now); break;
    case AKN_SCREEN_CONNECTION: draw_connection(app, &acc, running, now); break;
    case AKN_SCREEN_SYSTEM: draw_system(app, now); break;
    case AKN_SCREEN_FRIENDS:
    case AKN_SCREEN_PROFILE:
    case AKN_SCREEN_ACTIVITY:
        SDL_LockMutex(app->lock); /* the worker replaces the data under the same lock */
        if (app->ui.screen == AKN_SCREEN_FRIENDS) draw_friends(app, &app->social, now);
        else if (app->ui.screen == AKN_SCREEN_PROFILE) draw_profile(app, &app->social, now);
        else draw_activity(app, &app->social, now);
        SDL_UnlockMutex(app->lock);
        break;
    case AKN_SCREEN_COUNT: break;
    }
    bottom_bar(app);
    if (app->ui.toast[0]) {
        int w = akn_gfx_text_width(app->gfx, 24, AKN_FONT_REGULAR, app->ui.toast, strlen(app->ui.toast)) + 64;
        if (w > 1500) w = 1500;
        akn_rect t = { (AKN_SCREEN_W - w) / 2, 916, w, 60 };
        akn_gfx_rrect(app->gfx, t, 30, 0xF219233Du);
        akn_gfx_rrect_outline(app->gfx, t, 30, 1, C_LINE_STRONG);
        akn_gfx_text_fit(app->gfx, t.x + 32, t.y + 15, 24, AKN_FONT_REGULAR, C_INK, app->ui.toast, w - 64);
    }
    if (app->pad_disconnected && app->ui.dialog == AKN_DIALOG_NONE) {
        akn_rect b = { 560, 128, 800, 64 };
        akn_gfx_rrect(app->gfx, b, 32, 0xF2F5A524u);
        akn_gfx_text_center(app->gfx, b.x, b.w, b.y + 17, 24, AKN_FONT_BOLD, 0xFF1A1205u, "Controller disconnected: reconnect it to continue");
    }
    if (app->ui.dialog != AKN_DIALOG_NONE) draw_dialog(app, now);
}
