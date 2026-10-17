#include "text.h"

#include <string.h>

size_t akn_utf8_next(const char *s, size_t len, uint32_t *cp) {
    if (len == 0) {
        *cp = 0;
        return 0;
    }
    const unsigned char *u = (const unsigned char *)s;
    unsigned char c = u[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    size_t need;
    uint32_t v, min;
    if ((c & 0xE0) == 0xC0) {
        need = 2;
        v = c & 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        need = 3;
        v = c & 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        need = 4;
        v = c & 0x07;
        min = 0x10000;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if (len < need) {
        *cp = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i < need; i++) {
        if ((u[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (u[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
        *cp = 0xFFFD;
        return need;
    }
    *cp = v;
    return need;
}

void akn_packer_init(akn_packer *p, int width, int height, int padding) {
    memset(p, 0, sizeof(*p));
    p->width = width;
    p->height = height;
    p->padding = padding < 0 ? 0 : padding;
    p->cursor_x = p->padding;
    p->shelf_y = p->padding;
}

int akn_packer_add(akn_packer *p, int w, int h, int *x, int *y) {
    if (w <= 0 || h <= 0 || p->full) return -1;
    int pad = p->padding;
    if (w + 2 * pad > p->width || h + 2 * pad > p->height) return -1;
    if (p->cursor_x + w + pad > p->width) { /* next shelf */
        p->shelf_y += p->shelf_h + pad;
        p->cursor_x = pad;
        p->shelf_h = 0;
    }
    if (p->shelf_y + h + pad > p->height) {
        p->full = 1;
        return -1;
    }
    *x = p->cursor_x;
    *y = p->shelf_y;
    p->cursor_x += w + pad;
    if (h > p->shelf_h) p->shelf_h = h;
    return 0;
}

size_t akn_text_fit(const char *s, int max_width, int ellipsis_width, akn_measure_fn measure, void *user, int *truncated) {
    size_t len = strlen(s);
    *truncated = 0;
    if (measure(user, s, len) <= max_width) return len;
    *truncated = 1;
    int room = max_width - ellipsis_width;
    size_t best = 0, pos = 0;
    while (pos < len) {
        uint32_t cp;
        size_t n = akn_utf8_next(s + pos, len - pos, &cp);
        if (measure(user, s, pos + n) > room) break;
        pos += n;
        best = pos;
    }
    return best;
}

size_t akn_text_wrap(const char *s, int max_width, akn_measure_fn measure, void *user, size_t *next) {
    size_t len = strlen(s);
    *next = len;
    if (len == 0) return 0;
    size_t pos = 0, last_space = 0, fit = 0;
    int have_space = 0;
    while (pos < len) {
        if (s[pos] == '\n') {
            *next = pos + 1;
            return pos;
        }
        uint32_t cp;
        size_t n = akn_utf8_next(s + pos, len - pos, &cp);
        if (measure(user, s, pos + n) > max_width && pos > 0) {
            if (have_space) {
                *next = last_space + 1;
                while (*next < len && s[*next] == ' ') (*next)++;
                size_t line = last_space;
                while (line > 0 && s[line - 1] == ' ') line--; /* no trailing spaces */
                return line;
            }
            *next = pos;
            return pos;
        }
        if (s[pos] == ' ') {
            last_space = pos;
            have_space = 1;
        }
        pos += n;
        fit = pos;
    }
    *next = len;
    return fit;
}
