#include "image.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image_akn.h"

static void set_err(char *err, size_t len, const char *msg) {
    if (len) snprintf(err, len, "%s", msg);
}

static int is_jpeg(const uint8_t *d, size_t n) { return n >= 4 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF; }
static int is_png(const uint8_t *d, size_t n) {
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return n >= 8 && memcmp(d, sig, 8) == 0;
}

/* The PNG's chunks lie inside the data, and its first image-data chunk is not empty (stb_image would
 * do pointer arithmetic on NULL for that; no encoder writes it). */
static int png_chunks_ok(const uint8_t *d, size_t n) {
    size_t pos = 8;
    int idat = 0;
    while (pos + 12 <= n) {
        uint32_t len = (uint32_t)d[pos] << 24 | (uint32_t)d[pos + 1] << 16 | (uint32_t)d[pos + 2] << 8 | d[pos + 3];
        const uint8_t *type = d + pos + 4;
        if (len > n - pos - 12) return 0;
        if (memcmp(type, "IDAT", 4) == 0) {
            if (!idat && len == 0) return 0;
            idat = 1;
        }
        if (memcmp(type, "IEND", 4) == 0) return idat;
        pos += 12 + (size_t)len;
    }
    return 0;
}

int akn_image_decode(const uint8_t *data, size_t len, akn_image *out, char *err, size_t err_len) {
    memset(out, 0, sizeof(*out));
    if (!data || len == 0) {
        set_err(err, err_len, "empty");
        return -1;
    }
    if (len > AKN_IMAGE_MAX_BYTES) {
        set_err(err, err_len, "too many bytes");
        return -1;
    }
    if (!is_jpeg(data, len) && !is_png(data, len)) {
        set_err(err, err_len, "not a JPEG or PNG");
        return -1;
    }
    if (is_png(data, len) && !png_chunks_ok(data, len)) {
        set_err(err, err_len, "damaged PNG");
        return -1;
    }
    int w = 0, h = 0, comp = 0;
    if (!akn_stbi_info(data, (int)len, &w, &h, &comp)) {
        set_err(err, err_len, "unreadable header");
        return -1;
    }
    if (w <= 0 || h <= 0 || w > AKN_IMAGE_MAX_DIM || h > AKN_IMAGE_MAX_DIM || (long)w * h > AKN_IMAGE_MAX_PIXELS) {
        set_err(err, err_len, "dimensions out of range");
        return -1;
    }
    int dw = 0, dh = 0;
    uint8_t *px = akn_stbi_load_rgba(data, (int)len, &dw, &dh);
    if (!px) {
        set_err(err, err_len, akn_stbi_failure());
        return -1;
    }
    if (dw != w || dh != h) {
        akn_stbi_free(px);
        set_err(err, err_len, "size changed while decoding");
        return -1;
    }
    out->rgba = malloc((size_t)w * h * 4);
    if (!out->rgba) {
        akn_stbi_free(px);
        set_err(err, err_len, "out of memory");
        return -1;
    }
    memcpy(out->rgba, px, (size_t)w * h * 4);
    akn_stbi_free(px);
    out->w = w;
    out->h = h;
    return 0;
}

void akn_image_free(akn_image *img) {
    free(img->rgba);
    memset(img, 0, sizeof(*img));
}

/* Area average of the source over [x0,x1) x [y0,y1) (fractional, source pixels). */
static void sample_area(const akn_image *s, double x0, double y0, double x1, double y1, double acc[4]) {
    acc[0] = acc[1] = acc[2] = acc[3] = 0;
    double total = 0;
    int ix0 = (int)floor(x0), iy0 = (int)floor(y0), ix1 = (int)ceil(x1), iy1 = (int)ceil(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > s->w) ix1 = s->w;
    if (iy1 > s->h) iy1 = s->h;
    for (int y = iy0; y < iy1; y++) {
        double wy = fmin(y + 1.0, y1) - fmax((double)y, y0);
        if (wy <= 0) continue;
        for (int x = ix0; x < ix1; x++) {
            double wx = fmin(x + 1.0, x1) - fmax((double)x, x0);
            if (wx <= 0) continue;
            double wgt = wx * wy;
            const uint8_t *p = s->rgba + ((size_t)y * s->w + x) * 4;
            double a = p[3] / 255.0;
            acc[0] += p[0] * a * wgt; /* premultiplied while averaging */
            acc[1] += p[1] * a * wgt;
            acc[2] += p[2] * a * wgt;
            acc[3] += a * wgt;
            total += wgt;
        }
    }
    if (total > 0) {
        acc[0] /= total, acc[1] /= total, acc[2] /= total, acc[3] /= total;
    }
}

/* Bilinear sample at (fx, fy) in source pixel centres, premultiplied. */
static void sample_bilinear(const akn_image *s, double fx, double fy, double acc[4]) {
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > s->w - 1) fx = s->w - 1;
    if (fy > s->h - 1) fy = s->h - 1;
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < s->w ? x0 + 1 : x0, y1 = y0 + 1 < s->h ? y0 + 1 : y0;
    double tx = fx - x0, ty = fy - y0;
    acc[0] = acc[1] = acc[2] = acc[3] = 0;
    const int xs[2] = { x0, x1 }, ys[2] = { y0, y1 };
    const double wxs[2] = { 1 - tx, tx }, wys[2] = { 1 - ty, ty };
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            const uint8_t *p = s->rgba + ((size_t)ys[j] * s->w + xs[i]) * 4;
            double wgt = wxs[i] * wys[j], a = p[3] / 255.0;
            acc[0] += p[0] * a * wgt;
            acc[1] += p[1] * a * wgt;
            acc[2] += p[2] * a * wgt;
            acc[3] += a * wgt;
        }
}

int akn_image_fit(const akn_image *src, int w, int h, akn_fit fit, akn_image *out) {
    memset(out, 0, sizeof(*out));
    if (!src->rgba || src->w <= 0 || src->h <= 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    out->rgba = malloc((size_t)w * h * 4);
    if (!out->rgba) return -1;
    out->w = w;
    out->h = h;
    /* Cover: the scale that fills the target; the source window is centred. */
    double scale = fmax((double)w / src->w, (double)h / src->h);
    double win_w = w / scale, win_h = h / scale;
    double ox = (src->w - win_w) / 2, oy = (src->h - win_h) / 2;
    double step = 1 / scale; /* source pixels per target pixel */
    double cx = w / 2.0, cy = h / 2.0, r = fmin(w, h) / 2.0;
    int rr = AKN_FIT_RADIUS(w, h);
    if (2 * rr > w || 2 * rr > h) rr = (w < h ? w : h) / 2;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double acc[4];
            if (step > 1.0) sample_area(src, ox + x * step, oy + y * step, ox + (x + 1) * step, oy + (y + 1) * step, acc);
            else sample_bilinear(src, ox + (x + 0.5) * step - 0.5, oy + (y + 0.5) * step - 0.5, acc);
            double a = acc[3];
            int corner = fit == AKN_FIT_ROUNDED && (x < rr || x >= w - rr) && (y < rr || y >= h - rr);
            if (fit == AKN_FIT_CIRCLE || corner) {
                /* 4x4 supersampled coverage of the circle (or the corner's quarter circle) at this pixel */
                double ccx = fit == AKN_FIT_CIRCLE ? cx : x < rr ? rr : w - rr, ccy = fit == AKN_FIT_CIRCLE ? cy : y < rr ? rr : h - rr;
                double cr = fit == AKN_FIT_CIRCLE ? r : rr;
                int inside = 0;
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        double dx = x + (sx + 0.5) / 4 - ccx, dy = y + (sy + 0.5) / 4 - ccy;
                        if (dx * dx + dy * dy <= cr * cr) inside++;
                    }
                a *= inside / 16.0;
                if (acc[3] > 0) {
                    double k = a / acc[3];
                    acc[0] *= k, acc[1] *= k, acc[2] *= k;
                }
            }
            uint8_t *o = out->rgba + ((size_t)y * w + x) * 4;
            if (a <= 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; c++) {
                double v = acc[c] / a;
                o[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
            o[3] = (uint8_t)(a * 255 + 0.5);
        }
    }
    return 0;
}

static int b64_value(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

long akn_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap) {
    while (in_len && in[in_len - 1] == '=') in_len--;
    if (in_len % 4 == 1) return -1;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int v = b64_value((unsigned char)in[i]);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_cap) return -1;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return (long)o;
}

int akn_is_media_id(const char *s) {
    if (!s) return 0;
    size_t n = 0;
    for (; s[n]; n++)
        if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f'))) return 0;
    return n == 32;
}

int akn_is_title_id(const char *s) {
    if (!s || strlen(s) != 9) return 0;
    for (int i = 0; i < 4; i++)
        if (s[i] < 'A' || s[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}
