#include <stdlib.h>

#include "logo.h"
#include "png.h"
#include "test.h"
#include "text.h"

static void test_crc_adler(void) {
    CHECK(akn_crc32(0, (const uint8_t *)"IEND", 4) == 0xAE426082u);
    CHECK(akn_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(akn_adler32(1, (const uint8_t *)"Wikipedia", 9) == 0x11E60398u);
    CHECK(akn_adler32(1, NULL, 0) == 1u);
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void test_png(void) {
    uint8_t px[2 * 3 * 4];
    for (int i = 0; i < (int)sizeof(px); i++) px[i] = (uint8_t)(i * 7);
    uint8_t *out;
    size_t len;
    CHECK(akn_png_encode(px, 2, 3, 8, &out, &len) == 0);
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    CHECK(memcmp(out, sig, 8) == 0);
    CHECK(be32(out + 8) == 13 && memcmp(out + 12, "IHDR", 4) == 0);
    CHECK(be32(out + 16) == 2 && be32(out + 20) == 3);
    CHECK(out[24] == 8 && out[25] == 6);
    CHECK(be32(out + 29) == akn_crc32(0, out + 12, 17));
    /* IDAT holds zlib with one stored block of 3 rows of (1 + 8) bytes. */
    size_t idat = 33;
    CHECK(memcmp(out + idat + 4, "IDAT", 4) == 0);
    uint32_t zlen = be32(out + idat);
    const uint8_t *z = out + idat + 8;
    CHECK(zlen == 2 + 5 + 27 + 4);
    CHECK(z[0] == 0x78 && ((z[0] << 8) | z[1]) % 31 == 0);
    CHECK(z[2] == 1 && z[3] == 27 && z[5] == (uint8_t)~27);
    CHECK(z[7] == 0 && memcmp(z + 8, px, 8) == 0); /* filter byte, first row */
    CHECK(be32(z + 7 + 27) == akn_adler32(1, z + 7, 27));
    CHECK(memcmp(out + len - 8, "IEND", 4) == 0);
    free(out);
    CHECK(akn_png_encode(px, 0, 3, 8, &out, &len) == -1);
    CHECK(akn_png_encode(px, 2, 3, 4, &out, &len) == -1); /* pitch too small */
    /* Multiple stored blocks for images over 64 KiB. */
    uint8_t *big = calloc(200 * 100, 4);
    CHECK(akn_png_encode(big, 200, 100, 800, &out, &len) == 0);
    CHECK(len > 80000);
    free(out);
    free(big);
}

static void test_logo(void) {
    int size = 64;
    uint8_t *rgba = malloc((size_t)size * size * 4);
    akn_logo_style style = akn_logo_default_style();
    akn_logo_render(rgba, size, size * 4, &style);
#define PX(x, y) (rgba + ((y) * size + (x)) * 4)
    CHECK(PX(0, 0)[3] < 40);                                       /* rounded corner */
    CHECK(PX(5, 32)[3] == 255 && PX(5, 32)[0] == 0x0d);             /* background */
    CHECK(PX(32, 44)[0] == 0xe9 && PX(32, 44)[2] == 0xf8);          /* core dot */
    CHECK(PX(20, 48)[2] > 0xf0 && PX(20, 48)[0] < 0x90);            /* gradient leg: blue-violet */
    CHECK(PX(32, 36)[0] == 0x0d);                                   /* inside the A's notch: background */
    style.draw_background = 0;
    akn_logo_render(rgba, size, size * 4, &style);
    CHECK(PX(5, 32)[3] == 0);                                       /* transparent without background */
    CHECK(PX(32, 44)[3] == 255);
#undef PX
    free(rgba);
}

static int mono_measure(void *user, const char *s, size_t len) {
    (void)user;
    int w = 0;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp;
        pos += akn_utf8_next(s + pos, len - pos, &cp);
        w += 10;
    }
    return w;
}

static void test_utf8(void) {
    uint32_t cp;
    CHECK(akn_utf8_next("A", 1, &cp) == 1 && cp == 'A');
    CHECK(akn_utf8_next("\xC3\xA9", 2, &cp) == 2 && cp == 0xE9);
    CHECK(akn_utf8_next("\xE2\x80\xA6", 3, &cp) == 3 && cp == 0x2026);
    CHECK(akn_utf8_next("\xF0\x9F\x8E\xAE", 4, &cp) == 4 && cp == 0x1F3AE);
    CHECK(akn_utf8_next("\xC3", 1, &cp) == 1 && cp == 0xFFFD);       /* truncated */
    CHECK(akn_utf8_next("\xC0\xAF", 2, &cp) == 2 && cp == 0xFFFD);   /* overlong */
    CHECK(akn_utf8_next("\xED\xA0\x80", 3, &cp) == 3 && cp == 0xFFFD); /* surrogate */
    CHECK(akn_utf8_next("\xFF", 1, &cp) == 1 && cp == 0xFFFD);
    CHECK(akn_utf8_next("\xE2\x28\xA1", 3, &cp) == 1 && cp == 0xFFFD); /* bad continuation */
    CHECK(akn_utf8_next("", 0, &cp) == 0);
}

static void test_packer(void) {
    akn_packer p;
    akn_packer_init(&p, 100, 50, 2);
    int x, y;
    CHECK(akn_packer_add(&p, 40, 20, &x, &y) == 0 && x == 2 && y == 2);
    CHECK(akn_packer_add(&p, 40, 10, &x, &y) == 0 && x == 44 && y == 2);
    CHECK(akn_packer_add(&p, 40, 10, &x, &y) == 0 && x == 2 && y == 24); /* next shelf */
    CHECK(akn_packer_add(&p, 200, 10, &x, &y) == -1);                    /* never fits */
    CHECK(akn_packer_add(&p, 40, 30, &x, &y) == -1);                     /* atlas full */
    CHECK(p.full);
    CHECK(akn_packer_add(&p, 1, 1, &x, &y) == -1);
    CHECK(akn_packer_add(&p, 0, 5, &x, &y) == -1);
}

static void test_fit_wrap(void) {
    int truncated;
    CHECK(akn_text_fit("hello", 50, 10, mono_measure, NULL, &truncated) == 5 && !truncated);
    CHECK(akn_text_fit("hello world", 50, 10, mono_measure, NULL, &truncated) == 4 && truncated);
    CHECK(akn_text_fit("\xC3\xA9\xC3\xA9\xC3\xA9", 25, 0, mono_measure, NULL, &truncated) == 4 && truncated); /* whole code points */
    size_t next;
    const char *s = "the quick brown fox";
    CHECK(akn_text_wrap(s, 100, mono_measure, NULL, &next) == 9 && next == 10); /* "the quick" */
    CHECK(akn_text_wrap(s + 10, 100, mono_measure, NULL, &next) == 9);           /* "brown fox" */
    CHECK(akn_text_wrap("abcdefghijkl", 50, mono_measure, NULL, &next) == 5 && next == 5); /* hard break */
    CHECK(akn_text_wrap("ab\ncd", 500, mono_measure, NULL, &next) == 2 && next == 3);
    CHECK(akn_text_wrap("", 50, mono_measure, NULL, &next) == 0);
    CHECK(akn_text_wrap("a    b", 30, mono_measure, NULL, &next) == 1 && next == 5); /* spaces skipped */
}

void run_image_tests(void) {
    test_crc_adler();
    test_png();
    test_logo();
}

void run_text_tests(void) {
    test_utf8();
    test_packer();
    test_fit_wrap();
}
