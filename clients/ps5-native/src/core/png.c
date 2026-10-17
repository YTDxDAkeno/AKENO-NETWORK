#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t akn_crc32(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t akn_adler32(uint32_t adler, const uint8_t *data, size_t len) {
    uint32_t a = adler & 0xffff, b = adler >> 16;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Appends one chunk; returns the new write position. */
static uint8_t *chunk(uint8_t *w, const char type[4], const uint8_t *data, uint32_t len) {
    be32(w, len);
    memcpy(w + 4, type, 4);
    if (len) memcpy(w + 8, data, len);
    be32(w + 8 + len, akn_crc32(0, w + 4, 4 + (size_t)len));
    return w + 12 + len;
}

int akn_png_encode(const uint8_t *rgba, int width, int height, int pitch, uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (!rgba || width <= 0 || height <= 0 || width > 16384 || height > 16384 || pitch < width * 4) return -1;
    size_t row = (size_t)width * 4 + 1;
    size_t raw_len = row * (size_t)height;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t zlen = 2 + raw_len + blocks * 5 + 4;
    if (zlen > 0x7fffffffu) return -1;
    uint8_t *raw = malloc(raw_len);
    uint8_t *z = malloc(zlen);
    uint8_t *png = malloc(8 + 25 + 12 + zlen + 12);
    if (!raw || !z || !png) {
        free(raw);
        free(z);
        free(png);
        return -1;
    }
    for (int y = 0; y < height; y++) {
        raw[(size_t)y * row] = 0; /* filter: none */
        memcpy(raw + (size_t)y * row + 1, rgba + (size_t)y * (size_t)pitch, (size_t)width * 4);
    }
    uint8_t *w = z;
    *w++ = 0x78; /* deflate, 32 KiB window */
    *w++ = 0x01; /* no preset dictionary, fastest; (0x7801 % 31 == 0) */
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        *w++ = off + n == raw_len ? 1 : 0; /* BFINAL, BTYPE=00 (stored) */
        *w++ = (uint8_t)n;
        *w++ = (uint8_t)(n >> 8);
        *w++ = (uint8_t)~n;
        *w++ = (uint8_t)(~n >> 8);
        memcpy(w, raw + off, n);
        w += n;
        off += n;
    }
    be32(w, akn_adler32(1, raw, raw_len));
    w += 4;

    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)width);
    be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filtering */
    ihdr[12] = 0; /* no interlace */
    uint8_t *p = png;
    memcpy(p, signature, 8);
    p += 8;
    p = chunk(p, "IHDR", ihdr, 13);
    p = chunk(p, "IDAT", z, (uint32_t)(w - z));
    p = chunk(p, "IEND", NULL, 0);
    free(raw);
    free(z);
    *out = png;
    *out_len = (size_t)(p - png);
    return 0;
}

int akn_png_write(const char *path, const uint8_t *rgba, int width, int height, int pitch) {
    uint8_t *data;
    size_t len;
    if (akn_png_encode(rgba, width, height, pitch, &data, &len) != 0) return -1;
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int rc = -1;
    if (f) {
        int ok = fwrite(data, 1, len, f) == len;
        ok = fclose(f) == 0 && ok;
        if (ok && rename(tmp, path) == 0) rc = 0;
        else remove(tmp);
    }
    free(data);
    return rc;
}
